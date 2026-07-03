/*
 * Gilberta - Real-time Communication Library
 *
 * Copyright (c) 2026 Alex9932
 * SPDX-License-Identifier: MIT
 *
 * Gilberta is a library for real-time communication between applications,
 * designed to be simple and efficient. It provides a way to send and receive
 * data over a network connection, allowing applications to exchange information
 * in real-time.
 *
 * Features:
 *   - Simple non-blocking API for sending and receiving data
 *   - Cross-platform support (Windows, POSIX)
 *   - Channels with priority levels for message handling
 *   - UDP-based communication for low-latency data transfer
 *   - Transfer control mechanisms (reliable delivery, retransmission, ordering)
 *
 * Protocol design (packet header):
 *   [Header] (16 bytes)
 *    uint16_t magic        // Magic number to identify Gilberta packets
 *    uint16_t payload_len; // Payload length
 *    uint8_t  version;     // Protocol version (1)
 *    uint8_t  channel_id;  // Channel ID
 *    uint8_t  chan_flags;  // Channel flags
 *    uint8_t  ctrl_flags;  // Control flags ACK / SYN / PING / FIN / RST
 *    uint16_t client_gen;  // Client generation
 *    uint16_t client_id;   // Client ID (0xFFFF for initiate connection)
 *    uint16_t checksum;    // CRC16 modbus checksum (data only, for header-only packet = 0xFFFF)
 *    uint16_t wnd;         // Window size
 *    uint32_t seq;         // Sequence number
 *    uint32_t ack;         // Acknowledgment number
 *
 * Usage:
 *   - Create a context with glb_create() and a configuration.
 *   - For server mode, set ip = NULL and flag GLB_FLAG_BIND_PORT.
 *   - For client mode, set ip/port and call glb_connect().
 *   - Send messages via glb_send().
 *   - Poll events with glb_pollevent() to handle incoming data and connections.
 *
 * This header provides the public API for the Gilberta library.
 */

#ifndef GILBERTA_H
#define GILBERTA_H

#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#define GILBERTA_WINDOWS
#ifdef DLL_EXPORT
#define GLB_DECLSPEC __declspec(dllexport)
#else
#define GLB_DECLSPEC __declspec(dllimport)
#endif
#else
#define GILBERTA_POSIX
#define GLB_DECLSPEC
#endif

typedef enum GLBLogLevel {
	GLB_LOG_INFO = 0,
	GLB_LOG_WARN,
	GLB_LOG_ERROR,
	GLB_LOG_DEBUG
} GLBLogLevel;

typedef enum GLBErrorCode {
	GLB_SUCCESS = 0,
	GLB_ERROR_UNKNOWN,
	GLB_ERROR_INVALID_ARGUMENT,
	GLB_ERROR_OUT_OF_MEMORY,
	GLB_ERROR_QUEUE_FULL,
	GLB_ERROR_QUEUE_EMPTY,
	GLB_ERROR_MESSAGE_TOO_LONG,
	GLB_ERROR_SOCKET_CREATION,
	GLB_ERROR_SOCKET_BINDING,
	GLB_ERROR_CONNECTION_CLOSED,
	GLB_ERROR_SEND_FAILED,
	GLB_ERROR_RECV_FAILED
} GLBErrorCode;

#define GLB_FLAG_BIND_PORT 0x01 // bind to the specified port (server mode)

#define GLB_CHANNEL_FLAG_RELIABLE 0x01 // reliable delivery (retransmission, ordering)

typedef struct glbctx_t glbctx_t;   // Opaque context structure
typedef struct glbconn_t glbconn_t; // Opaque connection structure

typedef void* (*GLBMalloc)(size_t);
typedef void  (*GLBFree)(void*);
typedef void  (*GLBLogFunc)(GLBLogLevel, const char*);

// Memory allocation configuration
typedef struct glballoc_t {
	GLBMalloc malloc;
	GLBFree   free;
} glballoc_t;

// Logging configuration
typedef struct glblog_t {
	GLBLogFunc log_func;
} glblog_t;

// Channel configuration
typedef struct glbchan_t {
	uint8_t priority;
	uint8_t flags;
	uint16_t padding;
} glbchan_t;

// Global configuration for creating a context
typedef struct glbcfg_t {
	const char* ip;       // May be NULL for server mode
	uint16_t    port;
	uint8_t     flags;
	uint8_t     channel_count;
	uint16_t    eventqueue_length;
	uint16_t    max_connections;
	glballoc_t* alloc;
	glblog_t*   log;
	glbchan_t*  channels;
} glbcfg_t;

typedef enum glb_event_type_t {
	GLB_EVENT_NONE = 0,
	GLB_EVENT_CONNECT,
	GLB_EVENT_DISCONNECT,
	GLB_EVENT_RECIEVE,
	GLB_EVENT_ERROR,
} glb_event_type_t;

typedef enum glb_disconnectreason_t {
	GLB_DISCONNECT_BY_PEER,
	GLB_DISCONNECT_TIMEOUT
} glb_disconnectreason_t;

typedef struct glbevent_connect_t {
	glbconn_t* connection;
} glbevent_connect_t;

typedef struct glbevent_disconnect_t {
	glbconn_t* connection;
	glb_disconnectreason_t reason;
} glbevent_disconnect_t;

typedef struct glbevent_recieve_t {
	glbconn_t* connection;
	void* data;
	size_t length;
} glbevent_recieve_t;

typedef struct glbevent_t {
	glb_event_type_t type;
	uint32_t offset; // struct padding
	union {
		glbevent_connect_t    connect;
		glbevent_disconnect_t disconnect;
		glbevent_recieve_t    recieve;
		char _raw[56];
	};
} glbevent_t;

typedef struct glbsendinfo_t {
	const void* data;
	size_t      len;
	glbconn_t*  conn;
	uint8_t     channel_id;
} glbsendinfo_t;

// DEPRECATED
typedef struct glbrecvinfo_t {
	void*  buffer;
	size_t buflen;
	// Set by the library when data is received:
	glbconn_t* conn;    // indicate which connection the received data belongs to
	size_t datalen;     // indicate how much data was received
	uint8_t channel_id; // indicate which channel the received data belongs to
} glbrecvinfo_t;

#ifdef __cplusplus
extern "C" {
#endif

GLB_DECLSPEC int glb_geterror(glbctx_t* ctx);

GLB_DECLSPEC glbctx_t* glb_create(const glbcfg_t* config);
GLB_DECLSPEC int glb_destroy(glbctx_t* ctx);

GLB_DECLSPEC int glb_connect(glbctx_t* ctx); // Connect to server
GLB_DECLSPEC int glb_close(glbconn_t* conn); // Close a connection

GLB_DECLSPEC int glb_send(glbctx_t* ctx, glbsendinfo_t* info); // Push data to send queue

GLB_DECLSPEC int glb_tick(glbctx_t* ctx);
GLB_DECLSPEC int glb_pollevent(glbctx_t* ctx, glbevent_t* event); // Return count of unhandled events

//GLB_DECLSPEC int glb_getconnectioninfo(glbconn_t* conn); // TODO: Add function to get connection info (e.g. remote address, latency, etc.)

#ifdef __cplusplus
}
#endif

#endif