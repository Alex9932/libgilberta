#define DLL_EXPORT
#include "glbio.h"
//#include <stdlib.h>


glbconn_t* glb_accept(glbctx_t* ctx) {
	// TODO: Not implemented yet
	return NULL;
}

glbconn_t* glb_connect(glbctx_t* ctx) {
	// TODO: Not implemented yet
	return NULL;
}

int glb_close(glbconn_t* conn) {
	// TODO: Not implemented yet
	return GLB_ERROR_UNKNOWN;
}

int glb_send(glbctx_t* ctx, glbsendinfo_t* info) {
	if (!ctx || !info || !info->data || info->len <= 0) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	if (info->len > GILBERTA_MTU) {
		return GLB_ERROR_MESSAGE_TOO_LONG;
	}

	// Add data to the send queue



	return GLB_SUCCESS;
}

int glb_recv(glbctx_t* ctx, glbrecvinfo_t* info) {
	// TODO: Not implemented yet
	return GLB_ERROR_UNKNOWN;
}

int glb_tick(glbctx_t* ctx) {
	// TODO: Not implemented yet
	return GLB_ERROR_UNKNOWN;
}