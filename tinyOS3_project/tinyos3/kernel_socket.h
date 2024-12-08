#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H


#include "util.h"
#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_pipe.h"



typedef enum {
    SOCKET_LISTENER,
    SOCKET_UNBOUND,
    SOCKET_PEER
} socket_type;

typedef struct {

    rlnode queue;
    CondVar req_available;

} listener_socket;

typedef struct {

    rlnode unbound_socket;

} unbound_socket;


typedef struct SCB Socket_cb;


typedef struct {

    Socket_cb* peer;
    Pipe_cb* write_pipe;
    Pipe_cb* read_pipe;

} peer_socket;


typedef struct SCB
{
  unsigned int refcount;

  FCB *fcb;

  socket_type type;

  port_t port;

  union {

    listener_socket listener;
    unbound_socket unbound;
    peer_socket peer;

  } socket_union;


} Socket_cb;

extern file_ops socket_file_ops;


typedef struct {

    int admitted;
    Socket_cb* peer;
    CondVar connected_cv;
    rlnode queue_node;

} connection_request;

extern Socket_cb* port_map[MAX_PORT];

int port_map_init();

int check_fidt(Fid_t fidt);

Pipe_cb* create_peer_pipe(Fid_t peer_fidt, Fid_t accepted_fidt);


Fid_t sys_Socket(port_t port);

int sys_Listen(Fid_t sock);

Fid_t sys_Accept(Fid_t lsock);

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);

int sys_ShutDown(Fid_t sock, shutdown_mode how);

int socket_close();



#endif
