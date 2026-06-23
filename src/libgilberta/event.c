#include "event.h"
#include "queue.h"
#include "context.h"

#include "libgilberta.h"

int glb_pollevent(glbctx_t* ctx, glbevent_t* event) {
	int len = glbqueue_size(ctx->eventqueue);

	if (len != 0) {
		glbqueue_pop(ctx->eventqueue, event);
	}

	return len;
}