

#include "kernel_threads.h"
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

void start_process_thread()
{
  int exitval;
  PTCB* ptcb = cur_thread()->ptcb;

  Task call = ptcb->task;
  int argl = ptcb->argl;
  void *args = ptcb->args;

  exitval = call(argl, args);
  sys_ThreadExit(exitval);
}



void initialize_ptcb(PTCB *ptcb)
{
  ptcb->argl = 0;
  ptcb->args = NULL;
  ptcb->refcount = 0;

  ptcb->exit_cv = COND_INIT;

  ptcb->detached = 0;
  ptcb->exited = 0;


  rlnode_init(&ptcb->ptcb_list_node, ptcb);
}
/**
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void *args)
{
  PTCB *ptcb = (PTCB *)xmalloc(sizeof(PTCB));

  initialize_ptcb(ptcb);

  ptcb->task = task;

  ptcb->argl = argl;

  ptcb->args = args;


  if (task != NULL)
  {

    TCB *new_thread = spawn_thread(CURPROC, start_process_thread);
    ptcb->tcb = new_thread;
    ptcb->tcb->ptcb = ptcb;

    // Add the PTCB to the process's thread list
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);
    CURPROC->thread_count++;
   
     wakeup (ptcb->tcb);
  }

  return (Tid_t)ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t)cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int *exitval)
{

  // accessing the PTCB of the thread that we are joining
  PTCB *target_ptcb = (PTCB *)tid;

  // check if the target ptcb or tcb exists or has exited
  if (rlist_find(&CURPROC->ptcb_list, target_ptcb, NULL) == NULL)
  {
    return -1; // Thread does not exist or has exited
  }


  // Check if the target thread is detached
  if (target_ptcb->detached == 1 || tid == sys_ThreadSelf())
  {
    return -1;
  }

  // increase the refcount since we have joined the target thread and are waiting for it to exit
  target_ptcb->refcount++;

  while (target_ptcb->exited == 0 && target_ptcb->detached == 0)
  {
    kernel_wait(&target_ptcb->exit_cv, SCHED_USER);
  }

  target_ptcb->refcount--;
  
  // if the target thread became detached then we cant join it
  if (target_ptcb->detached == 1)
  {
    return -1;
  }

  
  // Now, the target thread has exited, get its exit value
  if (exitval != NULL)
  {
    *exitval = target_ptcb->exitval;
  }

  // check to see if the target ptcb has no more threads waiting for it to exit and if thats the case then clear its space
  if (target_ptcb->refcount == 0)
  {

    // Remove target thread from the process's thread list (ptcb_list)
    rlist_remove(&target_ptcb->ptcb_list_node);

    // clean any space used by the target thread's functions
    free(target_ptcb);

  }


  return 0;
}
/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB *ptcb = (PTCB*)tid;

  if (rlist_find(&CURPROC->ptcb_list, ptcb, NULL) == NULL || ((PTCB *)tid)->exited == 1) {
        return -1; // Invalid TID or illegal thread
  }

  //its okay to detach many times
  ptcb->detached = 1;
  kernel_broadcast(&ptcb->exit_cv);    
  return 0;
}



/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB *curproc = CURPROC;

  TCB *curthread = cur_thread(); // Current thread that called the sys_exit
  // access the currents TCB's PTCB
  PTCB *cur_ptcb = curthread->ptcb;

  cur_ptcb->exited = 1;
  cur_ptcb->exitval = exitval;

  kernel_broadcast(&cur_ptcb->exit_cv);

  curproc->thread_count--;

  if (curproc->thread_count == 0 ){
   
    if ( get_pid(curproc) != 1){

    /* Reparent any children of the exiting process to the
       initial task */
    PCB *initpcb = get_pcb(1);
    while (!is_rlist_empty(&curproc->children_list))
    {
      rlnode *child = rlist_pop_front(&curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(&initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list
       and signal the initial task */
    if (!is_rlist_empty(&curproc->exited_list))
    {
      rlist_append(&initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(&initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(&curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(&curproc->parent->child_exit);
  }

  assert(is_rlist_empty(&curproc->children_list));
  assert(is_rlist_empty(&curproc->exited_list));

  /*
    Do all the other cleanup we want here, close files etc.
   */

  rlnode *ptcb_node;
  PTCB *ptcb;
  while (!is_rlist_empty(&curproc->ptcb_list))
  {

    // Pop the first node from the process's ptcb_list
    ptcb_node = rlist_pop_front(&curproc->ptcb_list);

    // Access the PTCB
    ptcb = (PTCB *)ptcb_node->ptcb;
    free(ptcb);
  }

  /* Release the args data */
  if (curproc->args != NULL)
  {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for (int i = 0; i < MAX_FILEID; i++)
  {
    if (curproc->FIDT[i] != NULL)
    {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }


  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* mark the process as exited. */
  curproc->pstate = ZOMBIE;
  }
  
  kernel_sleep(EXITED, SCHED_USER);

}
