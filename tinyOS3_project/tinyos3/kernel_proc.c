
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_threads.h"

/*
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB *get_pcb(Pid_t pid)
{
  return PT[pid].pstate == FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB *pcb)
{
  return pcb == NULL ? NOPROC : pcb - PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB *pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;
  pcb->thread_count = 0;
  for (int i = 0; i < MAX_FILEID; i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(&pcb->children_list, NULL);
  rlnode_init(&pcb->exited_list, NULL);
  rlnode_init(&pcb->ptcb_list, NULL);
  rlnode_init(&pcb->children_node, pcb);
  rlnode_init(&pcb->exited_node, pcb);

  pcb->child_exit = COND_INIT;
}

static PCB *pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for (Pid_t p = 0; p < MAX_PROC; p++)
  {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB *pcbiter;
  pcb_freelist = NULL;
  for (pcbiter = PT + MAX_PROC; pcbiter != PT;)
  {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if (Exec(NULL, 0, NULL) != 0)
    FATAL("The scheduler process does not have pid==0");
}

/*
  Must be called with kernel_mutex held
*/
PCB *acquire_PCB()
{
  PCB *pcb = NULL;

  if (pcb_freelist != NULL)
  {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB *pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}

/*
 *
 * Process creation
 *
 */

/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call = CURPROC->main_task;
  int argl = CURPROC->argl;
  void *args = CURPROC->args;

  exitval = call(argl, args);
  Exit(exitval);
}

void add_tcb_to_process(PCB *pcb, TCB *main_tcb)
{

  // connecting the new_tcb (thread) to a ptcb
  PTCB *ptcb = (PTCB *)xmalloc(sizeof(PTCB));

  /*iNITIALIZE*/
  ptcb->argl = 0;
  ptcb->args = NULL;
  ptcb->refcount = 0;
  ptcb->exit_cv = COND_INIT;

  ptcb->detached = 0;
  ptcb->exited = 0;

  ptcb->tcb = main_tcb;
  ptcb->task = pcb->main_task;
  ptcb->argl = pcb->argl;

  ptcb->args = pcb->args;

  main_tcb->ptcb = ptcb;

  // Initialize the PTCB's node that will be pushed in the list
  rlnode_init(&ptcb->ptcb_list_node, ptcb);

  // Add the PTCB to the process's thread list
  rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node);

  pcb->thread_count++;
}

/*
  System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void *args)
{
  PCB *curproc, *newproc;

  /* The new process PCB */
  newproc = acquire_PCB();

  if (newproc == NULL)
    goto finish; /* We have run out of PIDs! */

  if (get_pid(newproc) <= 1)
  {
    /* Processes with pid<=1 (the scheduler and the init process)
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(&curproc->children_list, &newproc->children_node);

    /* Inherit file streams from parent */
    for (int i = 0; i < MAX_FILEID; i++)
    {
      newproc->FIDT[i] = curproc->FIDT[i];
      if (newproc->FIDT[i])
        FCB_incref(newproc->FIDT[i]);
    }
  }

  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if (args != NULL)
  {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args = NULL;

  /*
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */

  if (call != NULL)
  {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);

    add_tcb_to_process(newproc, newproc->main_thread);

    wakeup(newproc->main_thread);
  }

finish:
  return get_pid(newproc);
}

/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}

Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}

static void cleanup_zombie(PCB *pcb, int *status)
{
  if (status != NULL)
    *status = pcb->exitval;

  rlist_remove(&pcb->children_node);
  rlist_remove(&pcb->exited_node);

  release_PCB(pcb);
}

static Pid_t wait_for_specific_child(Pid_t cpid, int *status)
{

  /* Legality checks */
  if ((cpid < 0) || (cpid >= MAX_PROC))
  {
    cpid = NOPROC;
    goto finish;
  }

  PCB *parent = CURPROC;
  PCB *child = get_pcb(cpid);
  if (child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while (child->pstate == ALIVE)
    kernel_wait(&parent->child_exit, SCHED_USER);

  cleanup_zombie(child, status);

finish:
  return cpid;
}

static Pid_t wait_for_any_child(int *status)
{
  Pid_t cpid;

  PCB *parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while (1)
  {
    no_children = is_rlist_empty(&parent->children_list);
    if (no_children)
      break;

    has_exited = !is_rlist_empty(&parent->exited_list);
    if (has_exited)
      break;

    kernel_wait(&parent->child_exit, SCHED_USER);
  }

  if (no_children)
    return NOPROC;

  PCB *child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}

Pid_t sys_WaitChild(Pid_t cpid, int *status)
{
  /* Wait for specific child. */
  if (cpid != NOPROC)
  {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else
  {
    return wait_for_any_child(status);
  }
}

void sys_Exit(int exitval)
{

  PCB *curproc = CURPROC;

  /* First, store the exit status */
  curproc->exitval = exitval;

  if (get_pid(curproc) == 1)
  {

    while (sys_WaitChild(NOPROC, NULL) != NOPROC)
      ;
  }
  sys_ThreadExit(exitval);
}

Fid_t sys_OpenInfo()
{

  Fid_t fid;
  FCB *fcb;

  if (FCB_reserve(1, &fid, &fcb) != 0)
  {
    procinfo_cb *procinfocb = (procinfo_cb *)xmalloc(sizeof(procinfo_cb));

    fcb->streamfunc = &procinfo_ops;
    fcb->streamobj = procinfocb;
    procinfocb->cursor = 0;
    return fid;
  }
  else
  {
    return NOFILE;
  }
}

int procinfo_read(void *Procinfo_cb, char *buf, unsigned int size)
{

  procinfo_cb *procinfocb = Procinfo_cb;

  for (int i = procinfocb->cursor; i < MAX_PROC - 1; i++)
  {

    PCB *pcb = &PT[procinfocb->cursor];

    if (pcb == NULL)
    {
      procinfocb->cursor++;
      continue;
    }

    /*If PCB is alive or zombie copy it else continue*/
    if ((pcb->pstate == ALIVE) || pcb->pstate == ZOMBIE)
    {
      procinfocb->info->pid = get_pid(pcb);
      procinfocb->info->ppid = get_pid(pcb->parent);

      if (pcb->pstate == ALIVE)
      {
        procinfocb->info->alive = 1;
      }
      else
      {
        procinfocb->info->alive = 0;
      }

      procinfocb->info->thread_count = pcb->thread_count;

      procinfocb->info->main_task = pcb->main_task;

      procinfocb->info->argl = pcb->argl;
      memcpy(procinfocb->info->args, (char *)pcb->args, pcb->argl);

      /*Copying everything to buf*/
      memcpy(buf, (char *)&procinfocb->info, sizeof(procinfo));

      procinfocb->cursor++;

      if (i == MAX_PROC - 1)
      {
        /*Reached the last PCB*/
        return 0;
      }
      else
      {
        return sizeof(procinfo);
      }
    }
    else
    {
      procinfocb->cursor++;
      continue;
    }
  }

  return 0;
}

int procinfo_close(void *Procinfo_cb)
{
  procinfo_cb *procinfocb = (procinfo_cb *)Procinfo_cb;

  if (procinfocb == NULL)
  {

    return -1;
  }
  else
  {
    free(procinfocb);
    return 0;
  }
}

int return_Error_write(void *dev, const char *buf, unsigned int size)
{
  return -1;
}

file_ops procinfo_ops = {
    .Open = NULL,
    .Read = procinfo_read,
    .Write = return_Error_write,
    .Close = procinfo_close};
