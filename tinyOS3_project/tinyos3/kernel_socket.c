
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_pipe.h"


Fid_t sys_Socket(port_t port)
{
	Socket_cb *socket_cb = (Socket_cb *)xmalloc(sizeof(Socket_cb));

	Fid_t s; 

	if(FCB_reserve(1, &s, &CURPROC->FIDT[MAX_FILEID]) != 0)
	{
		socket_cb->fcb = CURPROC->FIDT[s];
	}else {return -1;}

	socket_cb->type = SOCKET_UNBOUND;

	/*if (rlist_find(port_map, port, NULL) == NULL)
    {
    return -1; 
    }*/
	
	
  	socket_cb->port = port;

	socket_cb->refcount = 0;

	socket_cb->fcb->streamobj = socket_cb;

	socket_cb->fcb->streamfunc = &socket_file_ops;


	return s;
}

int sys_Listen(Fid_t sock)
{
	return -1;
}


Fid_t sys_Accept(Fid_t lsock)
{
	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}

int socket_read(void *pipecb_t, char *buf, unsigned int n)
{

	if (pipe_read(pipecb_t,buf,n) != -1)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

int socket_write(void *pipecb_t, const char *buf, unsigned int n)
{
	if (pipe_write(pipecb_t,buf,n) != -1)
	{
		return 0;
	}
	else
	{
		return -1;
	}

}

int socket_close()
{

	return -1;

}


file_ops socket_file_ops = {
		.Open = NULL,
		.Read = socket_read,
		.Write = socket_write,
		.Close = socket_close};

