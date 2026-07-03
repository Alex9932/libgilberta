#ifndef LIBGILBERTA_CONTEXT_H
#define LIBGILBERTA_CONTEXT_H

#include "libgilberta.h"
//#include "queue.h"
#include "glbio.h"

#if defined(GILBERTA_WINDOWS)
void GWinSockInit(GLBLogFunc logger);
void GWinSockDestroy(GLBLogFunc logger);
#define GLB_INIT_WINSOCK    GWinSockInit
#define GLB_DESTROY_WINSOCK GWinSockDestroy
#elif defined(GILBERTA_POSIX)
#define GLB_INIT_WINSOCK
#define GLB_DESTROY_WINSOCK
#else
#error "[gilberta] Unsupported platform"
#endif

#define GLB_MAX_PENDING_CLIENTS 16

struct glbctx_t {
	GSocketHandle sock;
	glballoc_t    allocator;
	GLBLogFunc    logger;

	glbqueue*     eventqueue;
	glbconn_t*    connections;
	uint32_t      connection_count;

	// Last error
	int           error;
	
	char     inet_addr[128];
	uint16_t inet_port;
	uint8_t  flags;
	uint8_t  padding0;
	uint32_t padding1;
};

glbctx_t* glbctx_create(const glbcfg_t* config);
int glbctx_destroy(glbctx_t* ctx);
void glbctx_generateclientid(glbctx_t* ctx, glbconid_t* dst);
glbconn_t* glbctx_findemplyconn(glbctx_t* ctx);
glbconn_t* glbctx_findconn(glbctx_t* ctx, uint16_t gen, uint16_t id);

#endif