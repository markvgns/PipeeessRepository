
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_pipe.h"
#include "kernel_cc.h"

Socket_cb* port_map[MAX_PORT+1] = {NULL}; 

/*int port_map_init() {
    for (int i = 0; i < MAX_PORT; i++) {

        port_map[i] = (Socket_cb *)xmalloc(sizeof(Socket_cb)); 
        port_map[i]->port = NULL;
    }
    return 0; 
}*/


Pipe_cb* create_peer_pipe(FCB* peer_fcb, FCB* accepted_fcb)
{
	Pipe_cb *peer_pipe = (Pipe_cb *)xmalloc(sizeof(Pipe_cb));

	peer_pipe->writer = peer_fcb;  

	peer_pipe->reader = accepted_fcb;

	peer_pipe->has_space = COND_INIT; /* For blocking writer if no space is available */

	peer_pipe->has_data = COND_INIT; /* For blocking reader until data are available */

	peer_pipe->w_position = 0; /* write, read position in buffer (it depends on your implementation of bounded buffer, i.e. alternatively pointers can be used) */

	peer_pipe->r_position = 0;

	peer_pipe->reader->streamobj = peer_pipe;

	peer_pipe->writer->streamobj = peer_pipe;

	peer_pipe->reader->streamfunc = &reader_file_ops;

	peer_pipe->writer->streamfunc = &writer_file_ops;

	peer_pipe->empty_space = PIPE_BUFFER_SIZE;

	return peer_pipe;

}


Fid_t sys_Socket(port_t port)
{
	if(port > MAX_PORT || port < NOPORT )
	{
		return NOFILE;   //check for illigal port
	}

	Socket_cb *socket_cb = (Socket_cb *)xmalloc(sizeof(Socket_cb));

	Fid_t s; 

	if(FCB_reserve(1, &s, &CURPROC->FIDT[MAX_FILEID]) != 0)
	{
		socket_cb->fcb = CURPROC->FIDT[s];
	}else {return NOFILE;}

	socket_cb->type = SOCKET_UNBOUND;
	
  	socket_cb->port = port;

	socket_cb->refcount = 0;

	socket_cb->fcb->streamobj = socket_cb;

	socket_cb->fcb->streamfunc = &socket_file_ops;


	return s;
}


int sys_Listen(Fid_t sock)
{

	FCB* fcb =get_fcb(sock); // Retrieve the FCB associated with the Fid_t


	if(fcb == NULL)
	{
		return -1;
	}

    Socket_cb* socket_cb = (Socket_cb*)fcb->streamobj; // Return the Socket_cb

	if(socket_cb->port > MAX_PORT || socket_cb->port <=NOPORT)
	{
		return -1;   //check for illigal port
	}

	if (port_map[socket_cb->port] != NULL) {
        return -1;  //port is occupied
    }

	if (socket_cb->type != SOCKET_UNBOUND) {
        return -1; // initialized by listener
    }


	port_t port = socket_cb->port;
	port_map[port] = socket_cb;   //install the socket_cb to the portmap 

	socket_cb->type = SOCKET_LISTENER;   //mark as listener

	rlnode_init(&socket_cb->socket_union.listener.queue, NULL);    //initialize listener
	
    socket_cb->socket_union.listener.req_available = COND_INIT;


	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{

    FCB* fcb =get_fcb(lsock); // Retrieve the FCB associated with the Fid_t


	if(fcb == NULL)
	{
		return NOFILE;
	}

    Socket_cb* socket_cb = (Socket_cb*)fcb->streamobj; // Retrieve the Socket_cb of listener


    if (socket_cb == NULL || socket_cb->type != SOCKET_LISTENER) {
        return NOFILE; //  not a listener
    }

    port_t port = socket_cb->port;

	if (port_map[port] == NULL)
	  return NOFILE;


	socket_cb->refcount++;  //increase refcount of listener since we are now waiting for a request from connect


	//wait while the list IS empty and the listener port/socket IS NOT null
	while(is_rlist_empty(&socket_cb->socket_union.listener.queue) != 0 && port_map[port] != NULL)
	{
		kernel_wait(&socket_cb->socket_union.listener.req_available, SCHED_USER);
	}

    //decrease the refcount of listener since are not waiting anymore
	socket_cb->refcount--;

    if (port_map[port] == NULL)
	   return NOFILE;


	//START THE CONNECTION OF PEER SOCKETS--------------

	//get the request of connect from the listener queue
	connection_request* accepted_req = (connection_request*)(rlist_pop_front(&socket_cb->socket_union.listener.queue)->obj);

	//create new socket - the first peer socket (the second is the one that used connect)
	Fid_t peer_fidt = sys_Socket(socket_cb->port);

	if(peer_fidt == NOFILE)
	{return NOFILE;}


	//get fcb of peer
	FCB* peer_fcb = get_fcb(peer_fidt);

	//socket of peer
	Socket_cb* peer_socket_cb = (Socket_cb*)(peer_fcb)->streamobj; // Retrieve the socket_cb of the peer

	//mark it as peer socket
	peer_socket_cb->type = SOCKET_PEER;

	//fcb of accepted
	FCB* accepted_fcb = accepted_req->peer->fcb;

	//create the 2 pipes that will connect the two peer sockets
	Pipe_cb* peer_writer = create_peer_pipe(peer_fcb, accepted_fcb);  //first pipe where the one who accepts is writer
	Pipe_cb* peer_reader = create_peer_pipe(accepted_fcb, peer_fcb);  // second pipe where the one who accepts is reader

	//connecting the new pipes to the peer socket
	peer_socket_cb->socket_union.peer.write_pipe = peer_writer;
	peer_socket_cb->socket_union.peer.read_pipe = peer_reader;

	//connected the new pipes to the accepted socket
	accepted_req->peer->socket_union.peer.write_pipe = peer_reader;
	accepted_req->peer->socket_union.peer.read_pipe = peer_writer;


    //mark the request as admmited
	accepted_req->admitted = 1;

	// signal connect from the request to say we connected
	kernel_broadcast(&accepted_req->connected_cv);    



	return peer_fidt;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	FCB* fcb =get_fcb(sock); // Retrieve the FCB associated with the Fid_t


	if(fcb == NULL)
	{
		return -1;
	}

    Socket_cb* client_socket = (Socket_cb*)fcb->streamobj; // Retrieve the Socket_cb


	if(port > MAX_PORT || port < NOPORT || port_map[port] ==NULL)
	{
		return -1;   //check for illigal port
	}

	if(client_socket->type == SOCKET_LISTENER)
	{
		return -1;   //client_socket is a listener
	}

	if(client_socket->refcount != 0)
	{
		return -1;    //client_socket is connected socket
	}

	//declare the listeners socket
	Socket_cb* listener = port_map[port];

	if (listener->type != SOCKET_LISTENER) {
        return -1;  //port has no listener
    }


	//increase refcount, we are about to join the request queue
	client_socket->refcount++;


	//building request
	connection_request *con_request = (connection_request *)xmalloc(sizeof(connection_request));

	con_request->admitted = 0;
    con_request->peer = client_socket;
    con_request->connected_cv = COND_INIT;
	rlnode_init(&con_request->queue_node, con_request);

	//add request to the listener queue
	rlist_push_back(&listener->socket_union.listener.queue, &con_request->queue_node);

	//signal listener about the new request
	kernel_signal(&listener->socket_union.listener.req_available);

	int timeout_happened;

	//wait either until timeout or we get admitted
	while(con_request->admitted != 1)
	{
		timeout_happened = kernel_timedwait(&con_request->connected_cv, SCHED_USER, timeout*1000);
		if (timeout_happened == 0)
		break;
	}

    //decrease refcount since we are out of the request queue
	client_socket->refcount--;

	//timeout has expired without a successful connection
	if(con_request->admitted != 1)
	{
		return -1;
	}

	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb =get_fcb(sock); // Retrieve the FCB associated with the Fid_t


	if(fcb == NULL)
	{
		return -1;
	}

    Socket_cb* socket_cb = (Socket_cb*)fcb->streamobj; // Retrieve the Socket_cb of peer socket

	if(socket_cb->type != SOCKET_PEER)
	{
		return -1; 		//if its not a peer socket then it doesnt have a connected pipe
	}

	if(socket_cb->socket_union.peer.read_pipe == NULL || socket_cb->socket_union.peer.write_pipe == NULL)
	{return -1;}


	switch (how)
	{
		case SHUTDOWN_READ:

		return pipe_reader_close(socket_cb->socket_union.peer.read_pipe);

		case SHUTDOWN_WRITE:

		return pipe_writer_close(socket_cb->socket_union.peer.write_pipe);

		case SHUTDOWN_BOTH:

		if(pipe_reader_close(socket_cb->socket_union.peer.read_pipe) != -1 && 
		pipe_writer_close(socket_cb->socket_union.peer.write_pipe) != -1)
		{
			return 0;
		}


		default:
		break;

	}

	socket_cb->socket_union.peer.read_pipe = NULL;
	socket_cb->socket_union.peer.write_pipe = NULL;


	return 0;
}


int socket_write(void* Sock, const char *buf, unsigned int n)
{
	Socket_cb* socket_cb = (Socket_cb*)(intptr_t)Sock;
	
	if(socket_cb->fcb == NULL)
	{
		return -1;
	}

	if(socket_cb->type != SOCKET_PEER)
	{
		return -1; 		//if its not a peer socket then it doesnt have a connected pipe
	}

	if(socket_cb->socket_union.peer.write_pipe == NULL)
	{return -1;}

	
	return pipe_write(socket_cb->socket_union.peer.write_pipe, buf, n);



	
}



int socket_read(void* Sock, char *buf, unsigned int n)
{

	Socket_cb* socket_cb = (Socket_cb*)(intptr_t)Sock;

	if(socket_cb->fcb == NULL)
	{
		return -1;
	}
	

	if(socket_cb->type != SOCKET_PEER)
	{
		return -1; 		//if its not a peer socket then it doesnt have a connected pipe
	}


	if(socket_cb->socket_union.peer.read_pipe == NULL)
	{return -1;}

 	return pipe_read(socket_cb->socket_union.peer.read_pipe, buf, n);

	
}



int socket_close(void* Sock)
{
	 
	Socket_cb* socket_cb = (Socket_cb*)(intptr_t)Sock;  // Retrieve the Socket_cb of peer socket

	Fid_t sock;

	for (int i = 0; i < MAX_FILEID; i++) {
        if (CURPROC->FIDT[i] == socket_cb->fcb) {
            sock = i;
			break; // Found the matching FIDT
        }
	}	


	switch (socket_cb->type)
	{
		case SOCKET_LISTENER:

		kernel_broadcast(&socket_cb->socket_union.listener.req_available);
		port_map[socket_cb->port] = NULL;
		socket_cb->fcb = NULL;
		return 0;

		case SOCKET_UNBOUND:

		socket_cb->fcb = NULL;
		return 0;

		case SOCKET_PEER:
		if(socket_cb->socket_union.peer.read_pipe == NULL || socket_cb->socket_union.peer.write_pipe == NULL)
		{return -1;}
		socket_cb->fcb = NULL;
		int out = sys_ShutDown(sock,SHUTDOWN_BOTH);
		socket_cb->socket_union.peer.read_pipe = NULL;
		socket_cb->socket_union.peer.write_pipe = NULL;
		return out;


		default:
		break;

	}


	if(socket_cb->refcount == 0)
	{
		free(socket_cb);
	}

	return 0;

	
}




file_ops socket_file_ops = {
		.Open = NULL,
		.Read = socket_read,
		.Write = socket_write,
		.Close = socket_close};

