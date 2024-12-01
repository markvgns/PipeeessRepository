
#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_streams.h"
#include "kernel_dev.h"

int sys_Pipe(pipe_t* pipe)
{
    pipe_cb *Pipe_cb = (pipe_cb *)xmalloc(sizeof(pipe_cb));


    if(FCB_reserve(1,&pipe->read, &CURPROC->FIDT[MAX_FILEID]) && 
    FCB_reserve(1,&pipe->write, &CURPROC->FIDT[MAX_FILEID]) != 0){

        Pipe_cb->reader = CURPROC->FIDT[pipe->read];
        Pipe_cb->writer = CURPROC->FIDT[pipe->write];

	      Pipe_cb->has_space = COND_INIT;    /* For blocking writer if no space is available */

        Pipe_cb->has_data = COND_INIT;     /* For blocking reader until data are available */

	      Pipe_cb->w_position = 0;  /* write, read position in buffer (it depends on your implementation of bounded buffer, i.e. alternatively pointers can be used) */

        Pipe_cb->r_position = 0;

		    Pipe_cb->BUFFER[PIPE_BUFFER_SIZE]; 

        Pipe_cb->reader->streamobj = Pipe_cb;

        Pipe_cb->writer->streamobj = Pipe_cb;

        Pipe_cb->reader->streamfunc = &reader_file_ops;

        Pipe_cb->writer->streamfunc = &writer_file_ops;


        return 0;

	}else
    {return -1;}


}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n)
{
    return 0; 

}

int pipe_read(void* pipecb_t, char *buf, unsigned int n)
{
    return 0; 

}


int pipe_writer_close(void* _pipecb)
{
    return 0; 

}


int pipe_reader_close(void* _pipecb)
{
    return 0; 

}

int returnError_write(void* dev, const char* buf, unsigned int size)
{
    return -1;
}

int returnError_read(void* dev, char* buf, unsigned int size)
{
    return -1;
}


file_ops reader_file_ops = {
  .Open = NULL,
  .Read = pipe_read,
  .Write = returnError_write,
  .Close = pipe_reader_close
};

file_ops writer_file_ops = {
  .Open = NULL,
  .Read = returnError_read,
  .Write = pipe_write,
  .Close = pipe_writer_close
};

