#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include "lwipopts_examples_common.h"

// Enable standard BSD sockets and FreeRTOS integration mode
#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_POSIX_SOCKETS_IO_NAMES 1
#define LWIP_TIMEVAL_PRIVATE 0

// Required mailbox and queue sizing parameters for FreeRTOS + lwIP (NO_SYS=0)
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8
#define TCPIP_MBOX_SIZE 8
#define TCPIP_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_STACKSIZE 1024

#endif