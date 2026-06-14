#ifndef LIBGILBERTA_IO_H
#define LIBGILBERTA_IO_H

#include "libgilberta.h"
#include "queue.h"

#if defined(GILBERTA_WINDOWS)

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET GSocketHandle;
static const GSocketHandle SOCKET_NULL_HANDLE = INVALID_SOCKET;
static int winsock_usage = 0;
void GWinSockInit(GLBLogFunc logger);
void GWinSockDestroy(GLBLogFunc logger);
#define GLB_INIT_WINSOCK    GWinSockInit
#define GLB_DESTROY_WINSOCK GWinSockDestroy

#elif defined(GILBERTA_POSIX)

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int GSocketHandle;
static const GSocketHandle SOCKET_NULL_HANDLE = -1;
#define GLB_INIT_WINSOCK
#define GLB_DESTROY_WINSOCK

#else

#error "[gilberta] Unsupported platform"

#endif

// Maximum Transmission Unit for UDP packets (w/o headers)
#define GILBERTA_MTU 1024

typedef struct glbconid_t {
	uint16_t id;
	uint16_t generation; // for connection ID reuse after disconnection
} glbconid_t;

typedef struct glbconn_t {
	struct sockaddr_in peer_addr;
	struct glbconid_t  conn_id;
	glbqueue*          queue;
} glbconn_t;

typedef struct glbpkgheader {
	uint16_t magic;
	uint8_t  version;
	uint8_t  channel_id;
	uint16_t payload_len;
	uint8_t  flags;
	uint8_t  status;
} glbpkgheader;

#endif