#ifndef _GILBERTA_H
#define _GILBERTA_H

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

#ifdef DLL_EXPORT
#define GLB_DECLSPEC __declspec(dllexport)
#else
#define GLB_DECLSPEC __declspec(dllimport)
#endif

typedef enum GLBLogLevel {
	GLB_LOG_INFO = 0,
	GLB_LOG_WARN,
	GLB_LOG_ERROR,
	GLB_LOG_DEBUG
} GLBLogLevel;

typedef struct glbctx_t glbctx_t;

typedef void* (*GLBMalloc)(size_t);
typedef void  (*GLBFree)(void*);
typedef void  (*GLBLogFunc)(GLBLogLevel, const char*);

typedef struct glballoc_t {
	GLBMalloc malloc;
	GLBFree   free;
} glballoc_t;

typedef struct glblog_t {
	GLBLogFunc log_func;
} glblog_t;

typedef struct glbcfg_t {
	const char* ip;
	uint16_t    port;
	glballoc_t* alloc;
	glblog_t*   log;
} glbcfg_t;

typedef struct glbsendinfo_t {
	const void* data;
	size_t      len;
} glbsendinfo_t;

typedef struct glbrecvinfo_t {
	void*  buffer;
	size_t buflen;
} glbrecvinfo_t;

#ifdef __cplusplus
extern "C" {
#endif

GLB_DECLSPEC glbctx_t* glb_create(const glbcfg_t* config);
GLB_DECLSPEC void glb_destroy(glbctx_t* ctx);

// Push data to send queue
GLB_DECLSPEC int glb_send(glbctx_t* ctx, glbsendinfo_t* info);

// Pop data from receive queue
GLB_DECLSPEC int glb_recv(glbctx_t* ctx, glbrecvinfo_t* info);

// Call this function every frame to update internal state (send, receive and process data)
GLB_DECLSPEC void glb_tick(glbctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif