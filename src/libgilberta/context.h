#ifndef LIBGILBERTA_CONTEXT_H
#define LIBGILBERTA_CONTEXT_H

#include "libgilberta.h"
//#include "queue.h"
#include "glbio.h"

#define GLB_MAX_PENDING_CLIENTS 16

struct glbctx_t {
	GSocketHandle sock;
	glballoc_t    allocator;
	GLBLogFunc    logger;

	glbqueue*     eventqueue;

#if 0
	// In server mode used for establish connection
	glbqueue*     sys_send;
	glbqueue*     sys_recv;

	// Pending clients
	glbpendingclient pending[GLB_MAX_PENDING_CLIENTS];
#endif

	// Last error
	int           error;
	
};

void glbctx_generateclientid(glbctx_t* ctx, glbconid_t* dst);

#endif