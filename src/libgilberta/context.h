#ifndef LIBGILBERTA_CONTEXT_H
#define LIBGILBERTA_CONTEXT_H

#include "libgilberta.h"
#include "queue.h"
#include "glbio.h"

struct glbctx_t {
	GSocketHandle sock;
	glballoc_t    allocator;
	GLBLogFunc    logger;

	// In server mode used for establish connection
	glbqueue*     sys_send;
	glbqueue*     sys_recv;
	
};



#endif