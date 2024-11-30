#include "util.h"
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_threads.h"

#define PIPE_BUFFER_SIZE 20

typedef struct pipe_control_block {
	FCB *reader, *writer;
	CondVar has_space;    /* For blocking writer if no space is available */
    CondVar has_data;     /* For blocking reader until data are available */
	int w_position, r_position;  /* write, read position in buffer (it depends on your implementation of bounded buffer, i.e. alternatively pointers can be used)*/
	char BUFFER[PIPE_BUFFER_SIZE];   /* bounded (cyclic) byte buffer */
} pipe_cb;


int sys_Pipe(pipe_t* pipe);

int pipe_write(void* pipecb_t, const char *buf, unsigned int n);

int pipe_read(void* pipecb_t, char *buf, unsigned int n);

int pipe_writer_close(void* _pipecb);


int pipe_reader_close(void* _pipecb);