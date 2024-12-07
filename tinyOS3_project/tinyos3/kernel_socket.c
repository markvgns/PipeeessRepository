
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_pipe.h"

Socket_cb* port_map[MAX_PORT]; 

int port_map_init() {

    for (int i = 0; i < MAX_PORT; i++) {
        port_map[i]->port = NOPORT; 
    }


    return 0; 
}

int check_fidt(Fid_t fidt)
{
    for (int i = 0; i < MAX_FILEID; i++) {
        if ( CURPROC->FIDT[i] == CURPROC->FIDT[fidt]) {
            return 0; 
        }
    }

    return -1;

}

int portmap_find(Socket_cb *List[MAX_PORT], port_t key)
{
	int i =0;
	while (i != MAX_PORT)
	{
		if (List[i]->port == key)
			return i;
		else
			i++;
	}
	return -1;
}

Pipe_cb* create_peer_pipe(Fid_t peer_fidt, Fid_t accepted_fidt)
{
	Pipe_cb *peer_pipe = (Pipe_cb *)xmalloc(sizeof(Pipe_cb));

	peer_pipe->writer = CURPROC->FIDT[peer_fidt];

	peer_pipe->reader = CURPROC->FIDT[accepted_fidt];

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
	Socket_cb *socket_cb = (Socket_cb *)xmalloc(sizeof(Socket_cb));

	Fid_t s; 

	if(FCB_reserve(1, &s, &CURPROC->FIDT[MAX_FILEID]) != 0)
	{
		socket_cb->fcb = CURPROC->FIDT[s];
	}else {return -1;}

	socket_cb->type = SOCKET_UNBOUND;

	/*if(portmap_find(port_map,port) == -1)
	{
		return -1;      // why wrong????
	}*/
	
	
  	socket_cb->port = port;

	socket_cb->refcount = 0;

	socket_cb->fcb->streamobj = socket_cb;

	socket_cb->fcb->streamfunc = &socket_file_ops;


	return s;
}


int sys_Listen(Fid_t sock)
{

	if(check_fidt(sock) != 0)
	{
		return -1;
	}

	FCB* fcb = CURPROC->FIDT[sock]; // Retrieve the FCB associated with the Fid_t

    if(fcb == NULL)
	{
		return -1;
	}

    Socket_cb* socket_cb = (Socket_cb*)fcb->streamobj; // Return the Socket_cb

	if (socket_cb->port == NOPORT)
	{
		return -1;
	}

	for (int i = 0; i < MAX_PORT; i++) {

        Socket_cb* other_socket_cb = port_map[i];

        if (other_socket_cb != NULL &&  //check that its not null
            other_socket_cb->port == socket_cb->port &&  //and we are talking about the same port
            other_socket_cb->type == SOCKET_LISTENER) // and its a listener
		{
            return -1; 
        }
    }

	if (socket_cb->type == SOCKET_LISTENER) {
        return -1; // Already a listener
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
	if(check_fidt(lsock) != 0)   //check if fidt of listener is okay
	{
		return -1;
	}

	FCB* fcb = CURPROC->FIDT[lsock]; // Retrieve the FCB associated with the Fid_t


    if (fcb == NULL) {			//check that fcb of listener is not null
        return NOFILE; 
    }

    Socket_cb* socket_cb = (Socket_cb*)fcb->streamobj; // Retrieve the Socket_cb


    if (socket_cb == NULL || socket_cb->type != SOCKET_LISTENER) {
        return NOFILE; //  not a listener
    }

    // Check if there are available file IDs for the process in order to create the peer socket later on
    Fid_t new_sock;
    if (FCB_reserve(1, &new_sock, &CURPROC->FIDT[MAX_FILEID]) != 0) {
        CURPROC->FIDT[new_sock]=NULL; 
		new_sock = (Fid_t)0;
    }
	else
	{
		return NOFILE;  //no more file ids
	}


	socket_cb->refcount++;  //increase refcount of listener since we are now waiting for a request from connect

	//WAIT FOR A REQUESTTTT and wakes up with condvar and broadcast++++++++

	if(socket_cb->port == NOPORT || socket_cb == NULL)
	{
		return NOFILE;   //the socket or port closed
	}


	//START THE CONNECTION OF PEER SOCKETS--------------

	//get the request of connect from the listener queue
	connection_request* accepted_req = (connection_request*)(rlist_pop_front(&socket_cb->socket_union.listener.queue));

	//mark the request as admmited
	accepted_req->admitted = 1;


	//create new socket - the first peer socket (the second is the one that used connect)
	Fid_t peer_fidt = sys_Socket(socket_cb->port);

	Socket_cb* peer_socket_cb = (Socket_cb*)fcb->streamobj; // Retrieve the socket_cb of the peer

	//mark it as peer socket
	peer_socket_cb->type = SOCKET_PEER;

	peer_socket_cb->socket_union.peer.peer = peer_socket_cb;  ///????? is this needed

	//declare the fidt of the accepted peer - the one who used connect
	Fid_t accepted_fidt;

	//find the fidt of the accepted peer from its fcb
	for (int i = 0; i < MAX_FILEID; i++) {
        if (CURPROC->FIDT[i] == accepted_req->peer->fcb) {
            accepted_fidt = i;
			break; // Found the matching FIDT
        }
	}

	//create the 2 pipes that will connect the two peer sockets
	Pipe_cb* peer_writer = create_peer_pipe(peer_fidt, accepted_fidt);  //first pipe where the one who accepts is writer
	Pipe_cb* peer_reader = create_peer_pipe(accepted_fidt, peer_fidt);  // second pipe where the one who accepts is reader

	//connecting the new pipes with the peer union
	peer_socket_cb->socket_union.peer.write_pipe = peer_writer;
	peer_socket_cb->socket_union.peer.read_pipe = peer_reader;


	// SIGNAL CONNECT TO SAY WE CONNECTED +++++

	//decrease the refcount of listener since are not waiting anymore
	socket_cb->refcount--;


	return 0;
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

