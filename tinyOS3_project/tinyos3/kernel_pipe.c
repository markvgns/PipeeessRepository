
#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_sched.h"
#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_cc.h"

int sys_Pipe(pipe_t *pipe)
{
	Pipe_cb *pipe_cb = (Pipe_cb *)xmalloc(sizeof(Pipe_cb));

	if (FCB_reserve(1, &pipe->read, &CURPROC->FIDT[MAX_FILEID]) &&
			FCB_reserve(1, &pipe->write, &CURPROC->FIDT[MAX_FILEID]) != 0)
	{

		pipe_cb->reader = CURPROC->FIDT[pipe->read];
		pipe_cb->writer = CURPROC->FIDT[pipe->write];

		pipe_cb->has_space = COND_INIT; /* For blocking writer if no space is available */

		pipe_cb->has_data = COND_INIT; /* For blocking reader until data are available */

		pipe_cb->w_position = 0; /* write, read position in buffer (it depends on your implementation of bounded buffer, i.e. alternatively pointers can be used) */

		pipe_cb->r_position = 0;

		pipe_cb->reader->streamobj = pipe_cb;

		pipe_cb->writer->streamobj = pipe_cb;

		pipe_cb->reader->streamfunc = &reader_file_ops;

		pipe_cb->writer->streamfunc = &writer_file_ops;

		pipe_cb->empty_space = PIPE_BUFFER_SIZE;

		return 0;
	}
	else
	{
		return -1;
	}
}

int pipe_write(void *pipecb_t, const char *buf, unsigned int n)
{
	Pipe_cb *pipe_cb = (Pipe_cb *)pipecb_t;

	/*Checking if pipe control block exists*/
	if (pipecb_t == NULL)
	{
		return -1;
	}



	/*Checking if the reader exists*/
	if (pipe_cb->reader == NULL)
	{
		return -1;
	}


	while ((pipe_cb->empty_space == 0) && (pipe_cb->reader != NULL))
	{
		kernel_wait(&pipe_cb->has_space, SCHED_PIPE);
	}

	/*Case where while loop exited because reader has been closed*/
	if (pipe_cb->reader == NULL)
	{
		return -1;
	}
	/*Reading from buffer*/
	int i = 0;
	while (i != n && pipe_cb->empty_space != 0)
	{
		/*Writing to pipe buffer*/
		pipe_cb->BUFFER[pipe_cb->w_position] = buf[i];

		/*Updating positon*/
		pipe_cb->w_position = (pipe_cb->w_position + 1) % PIPE_BUFFER_SIZE;

		/*Updating empty space*/
		pipe_cb->empty_space--;


		i++;
	}

	kernel_broadcast(&pipe_cb->has_data);

	return i;
}

int pipe_read(void *pipecb_t, char *buf, unsigned int n)
{
	Pipe_cb *pipe_cb = (Pipe_cb *)pipecb_t;

	if (pipecb_t == NULL)
	{
		return -1;
	}

	/*Checking if the reader exists*/
	if (pipe_cb->reader == NULL)
	{
		return -1;
	}

	while ((pipe_cb->empty_space == PIPE_BUFFER_SIZE) && (pipe_cb->writer != NULL))
	{ 
		kernel_wait(&pipe_cb->has_data, SCHED_PIPE);
	}

		int i = 0;
		while ((i != n) && (pipe_cb->empty_space != PIPE_BUFFER_SIZE ))
		{

			buf[i] = pipe_cb->BUFFER[pipe_cb->r_position];

			pipe_cb->r_position = (pipe_cb->r_position + 1) % PIPE_BUFFER_SIZE;

			pipe_cb->empty_space++;

			i++;
		}

		kernel_broadcast(&pipe_cb->has_space);

		return i;



}

int pipe_writer_close(void *_pipecb)
{
	Pipe_cb *pipe_cb = (Pipe_cb *)_pipecb;

	if (pipe_cb == NULL)
	{
		return -1;
	}

	if (pipe_cb->writer != NULL)
	{
		pipe_cb->writer = NULL;
	}

	if (pipe_cb->reader != NULL)
	{
		kernel_broadcast(&pipe_cb->has_data);
	}
	else
	{
		free(pipe_cb);
	}

	return 0;
}

int pipe_reader_close(void *_pipecb)
{
	Pipe_cb *pipe_cb = (Pipe_cb *)_pipecb;

	if (pipe_cb == NULL)
	{
		return -1;
	}

	if (pipe_cb->reader != NULL)
	{
		pipe_cb->reader = NULL;
	}

	if (pipe_cb->writer != NULL)
	{
		kernel_broadcast(&pipe_cb->has_space);
	}
	else
	{
		free(pipe_cb);
	}

	return 0;
}

int returnError_write(void *dev, const char *buf, unsigned int size)
{
	return -1;
}

int returnError_read(void *dev, char *buf, unsigned int size)
{
	return -1;
}

file_ops reader_file_ops = {
		.Open = NULL,
		.Read = pipe_read,
		.Write = returnError_write,
		.Close = pipe_reader_close};

file_ops writer_file_ops = {
		.Open = NULL,
		.Read = returnError_read,
		.Write = pipe_write,
		.Close = pipe_writer_close};
