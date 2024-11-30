#ifndef __KERNEL_THREADS_H
#define __KERNEL_THREADS_H

#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/**
  @brief This structure holds all information pretaining to each of the threads of a specific process.
  */

typedef struct process_thread_control_block
{

  TCB *tcb;  /**<@brief Points to the specific TCB that is connected with the Task of this thread*/
  Task task; /**<@brief the task of this thread*/
  int argl;
  void *args;

  int exitval;

  int exited;
  int detached; /** [0, 1]; */
  CondVar exit_cv;

  int refcount;

  rlnode ptcb_list_node; /**<@brief the node of the specific thread for a task inside the list of threads of each process*/

} PTCB;

void start_process_thread();

void initialize_ptcb(PTCB *ptcb);
/**
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void *args);

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf();

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int *exitval);

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid);



/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval);

#endif