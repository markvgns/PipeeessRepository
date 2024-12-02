#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "util.h"
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_threads.h"
#include "kernel_dev.h"

#define PIPE_BUFFER_SIZE 20

typedef struct Pipe_control_block
{
  FCB *reader, *writer;

  CondVar has_space; /* For blocking writer if no space is available */

  CondVar has_data; /* For blocking reader until data are available */

  int w_position, r_position; /* write, read position in buffer (it depends on your implementation of bounded buffer, i.e. alternatively pointers can be used)*/

  char BUFFER[PIPE_BUFFER_SIZE]; /* bounded (cyclic) byte buffer */

  int empty_space;

} Pipe_cb;

extern file_ops reader_file_ops;
extern file_ops writer_file_ops;

int returnError_write(void *dev, const char *buf, unsigned int size);

int returnError_read(void *dev, char *buf, unsigned int size);

int sys_Pipe(pipe_t *pipe);

int pipe_write(void *pipecb_t, const char *buf, unsigned int n);

int pipe_read(void *pipecb_t, char *buf, unsigned int n);

int pipe_writer_close(void *_pipecb);

int pipe_reader_close(void *_pipecb);

#endif
