#ifndef LIBGILBERTA_IO_H
#define LIBGILBERTA_IO_H

#include "libgilberta.h"
#include "queue.h"
#include "timer.h"

#if defined(GILBERTA_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET GSocketHandle;
static const GSocketHandle SOCKET_NULL_HANDLE = INVALID_SOCKET;
#elif defined(GILBERTA_POSIX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int GSocketHandle;
static const GSocketHandle SOCKET_NULL_HANDLE = -1;
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

typedef enum GLBConnState {
	GLB_CONNECTION_CLOSED = 0,
	GLB_CONNECTION_ESTABLISHED,
	GLB_CONNECTION_SYN_SENT,
	GLB_CONNECTION_SYN_RCVD
} GLBConnState;

typedef struct glbchannel_t {
	glbqueue* s_queue;   // Send queue
	//glbqueue* r_queue;   // Recieve queue
	uint32_t  seq;
	uint32_t  ack;
} glbchannel_t;

typedef struct glbconn_t {
	glbaddr_t  peer_addr; // Real peer address (16 bytes in windows)
	glbconid_t conn_id;   // Logical peer address (for peer identification after ip/nat changes)
	uint8_t    state;
	uint8_t    retry;
	uint16_t   padding2;
	glbchannel_t* channels; // NULL pointer in GLB_CONNECTION_CLOSED state
	glbtimestamp_t time; // Last op time
} glbconn_t;

// Proto
#define GLB_CTRL_FLAG_SYN  0x01
#define GLB_CTRL_FLAG_ACK  0x02
#define GLB_CTRL_FLAG_PING 0x04
#define GLB_CTRL_FLAG_PONG 0x08
#define GLB_CTRL_FLAG_RES1 0x10 // RESERVED
#define GLB_CTRL_FLAG_RES2 0x20 // RESERVED
#define GLB_CTRL_FLAG_FIN  0x40
#define GLB_CTRL_FLAG_RST  0x80

typedef struct glbpkgheader {
	union {
		uint16_t magic;       // 'GB'
		char     magic_c[2];
	};
	uint16_t payload_len; // Payload length
	uint8_t  version;     // Protocol version (1)
	uint8_t  channel_id;  // Channel ID
	uint8_t  chan_flags;  // Channel flags
	uint8_t  ctrl_flags;  // Control flags
	uint16_t client_gen;  // Client generation
	uint16_t client_id;   // Client ID (0xFFFF for initiate connection)
	uint16_t checksum;    // CRC16 modbus checksum (data only, for header-only packet = 0xFFFF)
	uint16_t wnd;         // Window size
	uint32_t seq;         // Sequence number
	uint32_t ack;         // Acknowledgment number
} glbpkgheader;

typedef struct glbpkg {
	glbpkgheader header;
	char data[GILBERTA_MTU];
	glbtimestamp_t timestamp;
} glbpkg;

// Calculate checksum & send data
int glbio_send(glbctx_t* ctx, glbpkg* pkg, struct sockaddr* to_addr, int* addr_len);

// Recieve data & check checksum
int glbio_read(glbctx_t* ctx, glbpkg* pkg, int* recvd, struct sockaddr* from_addr, int* addr_len);

#endif