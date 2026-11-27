#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include "lwipopts_examples_common.h"

// Enable standard BSD sockets and FreeRTOS integration mode
#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_POSIX_SOCKETS_IO_NAMES 1
#define LWIP_TIMEVAL_PRIVATE 0

// Mailbox sizes
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8
#define TCPIP_MBOX_SIZE 8
#define TCPIP_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_STACKSIZE 1024

// --- SAFETY BUFFERS FOR CONCURRENT SOCKETS ---
// Increase active TCP connection slots so HTTP + MQTT + Watchdog 
// don't collide and exhaust the pool when the server drops connection.
#define MEMP_NUM_TCP_PCB        12   // Default is often too low (~5-8)
#define MEMP_NUM_TCP_PCB_LISTEN  6   // For your HTTP server listening socket
#define MEMP_NUM_TCP_SEG        32   // Ensure enough segments for payloads
#define MEMP_NUM_NETBUF          16
#define MEMP_NUM_NETCONN         16

// REQUIRED FOR MQTT: Increase timeout pool by 1
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)

#endif