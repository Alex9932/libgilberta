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

typedef struct sockaddr_in glbaddr_t;

typedef struct glbconid_t {
	uint16_t id;
	uint16_t generation; // for connection ID reuse after disconnection
} glbconid_t;

typedef struct glbconn_t {
	glbaddr_t  peer_addr; // Real peer address (16 bytes in windows)
	glbconid_t conn_id;   // Logical peer address (for peer identification after ip/nat changes)
	uint32_t   padding;
	uint64_t   padding2;
	uint64_t   padding3;  // Paddings for 64-byte allignment
	glbqueue*  s_queue;   // Send queue
	glbqueue*  r_queue;   // Recieve queue
	glbconn_t* next;      // Next element in linked-list
} glbconn_t;

// Proto
typedef struct glbpkgheader {
	union {
		uint16_t magic;       // 'GB'
		char     magic_c[2];
	};
	uint8_t  version;     // 1
	uint8_t  channel_id;  // 255
	uint16_t payload_len; // 0
	uint8_t  flags;       // 0
	uint8_t  ctrl_flags;  // 0
	uint16_t client_gen;  // 0
	uint16_t client_id;   // 0 (0xFFFF for initiate connection)
	uint32_t reserved;    // 0
} glbpkgheader;

#if 0
// Internal
typedef struct glbsyspacket {
	glbpkgheader header;
	glbaddr_t    addr;
} glbsyspacket;

typedef struct glbpendingclient {
	uint32_t     in_use;
	glbconid_t   connid;
	glbaddr_t    addr;
} glbpendingclient;
#endif

#endif