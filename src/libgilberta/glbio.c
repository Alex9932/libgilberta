#define DLL_EXPORT
#include "glbio.h"
#include "context.h"
#include "queue.h"

#if 0
glbconn_t* glb_accept(glbctx_t* ctx) {
	// TODO: Not implemented yet
	return NULL;
}
#endif

int glb_connect(glbctx_t* ctx) {
	// TODO: Not implemented yet
	return GLB_ERROR_UNKNOWN;
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
		ctx->error = GLB_ERROR_MESSAGE_TOO_LONG;
		return GLB_ERROR_MESSAGE_TOO_LONG;
	}

	// Add data to the send queue



	return GLB_SUCCESS;
}

#if 0
int glb_recv(glbctx_t* ctx, glbrecvinfo_t* info) {
	// TODO: Not implemented yet
	return GLB_ERROR_UNKNOWN;
}

static int FindPendingSlot(glbctx_t* ctx) {
	for (size_t i = 0; i < GLB_MAX_PENDING_CLIENTS; i++) {
		if (!ctx->pending[i].in_use) { return i; }
	}
	return -1;
}

static void GenerateConnID(glbctx_t* ctx, glbconid_t* id) {
	id->id = 0;
	id->generation = 0;
}

int glb_tick(glbctx_t* ctx) {
	if (!ctx) { return GLB_ERROR_INVALID_ARGUMENT; }

	char buffer[GILBERTA_MTU + sizeof(glbpkgheader)];
	struct sockaddr_in from_addr;
	socklen_t addr_len = sizeof(from_addr);

	glbpkgheader* headerptr = (glbpkgheader*)buffer;
	void*         dataptr   = (void*)&buffer[sizeof(glbpkgheader)];

	// Handle incoming messages

	while (1) {
		int recv_len = recvfrom(ctx->sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &addr_len);

		if (recv_len < 0) {
#if defined(GILBERTA_WINDOWS)
			if (WSAGetLastError() == WSAEWOULDBLOCK) break;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
			ctx->error = GLB_ERROR_RECV_FAILED;
			return GLB_ERROR_RECV_FAILED;
		}

		if (headerptr->magic != 0x4247) { // "GB"
			ctx->logger(GLB_LOG_WARN, "UNKNOWN PACKED RECIEVED");
			continue; // Skip this one
		}

		if (headerptr->client_id == 0xFFFF && headerptr->client_gen == 0x00 && headerptr->status == GLB_STATUS_CONNECTION_REQUEST) {
			// New client (check header and recieve)
			
			// Set 'status' to GLB_STATUS_ACK
			// Set 'client_id' & 'client_gen' to actual values
			// Send to Client & wait response with 'status' to GLB_STATUS_SYN_ACK
			ctx->logger(GLB_LOG_INFO, "New connection, waiting client...");

			glbconid_t conid;
			GenerateConnID(ctx, &conid);

			glbsyspacket p;
			p.addr = from_addr;
			p.header = *headerptr; // just copy
			p.header.client_id  = conid.id;
			p.header.client_gen = conid.generation;
			p.header.status     = GLB_STATUS_ACK;

			int idx = FindPendingSlot(ctx);
			if (idx == -1) {
				// No free slot, reset connection
				p.header.status = GLB_STATUS_RST;
			} else {
				// Add a pending client
				ctx->pending[idx].in_use = 1;
				ctx->pending[idx].addr = from_addr;
				ctx->pending[idx].connid = conid;
			}

			glbqueue_push(ctx->sys_send, &p);
		}

		ctx->logger(GLB_LOG_INFO, "Message revcieve");
	}

	// Handle send queue

	while (!glbqueue_is_empty(ctx->sys_send)) {
		glbsyspacket p;
		glbqueue_pop(ctx->sys_send, &p);
		size_t len = sizeof(p.addr);
		int send_len = sendto(ctx->sock, &p.header, sizeof(p.header), 0, &p.addr, len);

		if (send_len != len) {
			ctx->logger(GLB_LOG_INFO, "Send failed!");
			ctx->error = GLB_ERROR_SEND_FAILED;
			return GLB_ERROR_SEND_FAILED;
		}

	}

	return GLB_SUCCESS;
}
#endif