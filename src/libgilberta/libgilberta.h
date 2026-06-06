#ifndef GILBERTA_H
#define GILBERTA_H

/*
* Gilberta is a library for real-time communication between applications, designed to be simple and efficient.
* It provides a way to send and receive data over a network connection, allowing applications to exchange information in real-time.
* 
* The main features:
* - Simple non-blocking API for sending and receiving data 
* - Cross-platform support
* - Channels with priority levels for message handling
* - UDP-based communication for low-latency data transfer
* - Transfer control mechanisms to ensure reliable data delivery
* 
*/

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
	GLB_ERROR_INVALID_ARGUMENT,
	GLB_ERROR_SOCKET_CREATION,
	GLB_ERROR_SOCKET_BINDING,
	GLB_ERROR_SEND_FAILED,
	GLB_ERROR_RECV_FAILED,
	GLB_ERROR_UNKNOWN
} GLBErrorCode;

#define GLB_FLAG_BIND_PORT 0x01 // bind to the specified port (server mode)

#define GLB_CHANNEL_FLAG_RELIABLE 0x01 // reliable delivery (retransmission, ordering)

typedef struct glbctx_t glbctx_t;

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
	uint32_t    padding1; // reserved for future use
	glballoc_t* alloc;
	glblog_t*   log;
	glbchan_t*  channels;
} glbcfg_t;

// TODO: Add client identification
typedef struct glbsendinfo_t {
	const void* data;
	size_t      len;
	uint8_t     channel_id;
} glbsendinfo_t;

typedef struct glbrecvinfo_t {
	void*  buffer;
	size_t buflen;
	size_t datalen;     // set by the library to indicate how much data was received
	uint8_t channel_id; // set by the library to indicate which channel the received data belongs to
} glbrecvinfo_t;

#ifdef __cplusplus
extern "C" {
#endif

GLB_DECLSPEC glbctx_t* glb_create(const glbcfg_t* config);
GLB_DECLSPEC int glb_destroy(glbctx_t* ctx);

// Push data to send queue
GLB_DECLSPEC int glb_send(glbctx_t* ctx, glbsendinfo_t* info);

// Pop data from receive queue
GLB_DECLSPEC int glb_recv(glbctx_t* ctx, glbrecvinfo_t* info);

// Call this function every frame to update internal state (send, receive and process data)
GLB_DECLSPEC int glb_tick(glbctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif