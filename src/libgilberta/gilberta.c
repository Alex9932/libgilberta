/*
 * Gilberta – Real-time Communication Library
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alex9932
 *
 * Module:    gilberta.c
 * Purpose:   Public API implementation
 *
 * This file is part of the Gilberta library.
 * See the main header (libgilberta.h) for the public API and protocol design.
 *
 */

#define DLL_EXPORT
#include "libgilberta.h"
#include "context.h"
#include "queue.h"
#include "timer.h"

// For debug:
// DL  - Data length (payload length)
// V   - Version
// C   - Channel
// CF  - Channel flags
// F   - Control flags
// CG  - Client generation
// CID - Client ID
// CHK - CRC16 checksum
// WND - Window size
// SEQ - Sequence number
// ACK - Acknowledgment number
// 
//                        "GB"  DL    V  C  CF F  CG    CID   CHK   WND   SEQ         ACK
// Client -> server (SYN) 47 42 00 00 01 00 00 01 00 00 FF FF FF FF 00 00 00 00 00 00 00 00 00 00
// Client -> server (ACK) 47 42 00 00 01 00 00 02 00 00 00 00 FF FF 00 00 00 00 00 00 00 00 00 00

#define GLB_MAX_RETRY     5    // times
#define GLB_RETRY_TIMEOUT 1000 // ms

static void glbpkg_init(glbpkg* pkg, glbconid_t conn_id, uint8_t ctrl_flags) {
	pkg->header.magic       = GILBERTA_PROTO_MAGIC;
	pkg->header.payload_len = 0;
	pkg->header.version     = GILBERTA_PROTO_VERSION;
	pkg->header.channel_id  = 0;
	pkg->header.chan_flags  = 0;
	pkg->header.ctrl_flags  = ctrl_flags;
	pkg->header.client_gen  = conn_id.generation;
	pkg->header.client_id   = conn_id.id;
	pkg->header.checksum    = 0xFFFF;
	pkg->header.wnd = 0;
	pkg->header.seq = 0;
	pkg->header.ack = 0;
}

int glb_geterror(glbctx_t* ctx) {
	return ctx->error;
}

glbctx_t* glb_create(const glbcfg_t* config) {
	return glbctx_create(config);
}

int glb_destroy(glbctx_t* ctx) {
	if (!ctx) return GLB_ERROR_INVALID_ARGUMENT;
	return glbctx_destroy(ctx);
}

int glb_connect(glbctx_t* ctx) {
	if (!ctx) return GLB_ERROR_INVALID_ARGUMENT;

	// Use first element in client mode
	glbconn_t* con = &ctx->connections[0];

	con->conn_id.generation = 0x00;
	con->conn_id.id = 0xFFFF;
	struct sockaddr_in* server_addr = &con->peer_addr;

	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(ctx->inet_port);
	if (inet_pton(AF_INET, ctx->inet_addr, &server_addr->sin_addr) != 1) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid IP address is set.");
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	// Set state to SYN_SENT and start a 0ms timer to force send SYN in next glb_tick call
	con->state = GLB_CONNECTION_SYN_SENT;
	con->retry = 0;
	glbtime_start(&con->time, 0);

	return GLB_SUCCESS;
}

int glb_close(glbconn_t* conn) {
	if (!conn) return GLB_ERROR_INVALID_ARGUMENT;

	// FIN/RST

	return GLB_ERROR_UNKNOWN;
}

int glb_send(glbctx_t* ctx, glbsendinfo_t* info) {
	if (!ctx || !info || !info->conn || !info->data || info->len <= 0 || info->channel_id >= ctx->channel_count) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	if (info->len > GILBERTA_MTU) {
		ctx->error = GLB_ERROR_MESSAGE_TOO_LONG;
		return GLB_ERROR_MESSAGE_TOO_LONG;
	}

	if (info->conn->state != GLB_CONNECTION_ESTABLISHED) {
		return GLB_ERROR_CONNECTION_CLOSED;
	}
	
	glbchannel_t* chan = &info->conn->channels[info->channel_id];

	// Copy data
	glbpkg pkg;
	glbpkg_init(&pkg, info->conn->conn_id, GLB_CTRL_FLAG_DATA);
	pkg.header.payload_len = info->len;
	memcpy(pkg.data, info->data, info->len);

	// TODO: Add seq

	// Add data to the send queue
	int res = glbqueue_push(chan->s_queue, &pkg);
	if (res == GLB_ERROR_QUEUE_FULL) {
		return GLB_ERROR_QUEUE_FULL;
	}

	return GLB_SUCCESS;
}

int glb_tick(glbctx_t* ctx) {
	glbpkg pkg;
	struct sockaddr_in from_addr;
	socklen_t addr_len = sizeof(from_addr);
	glbpkgheader* headerptr = (glbpkgheader*)&pkg;
	void* dataptr = (void*)((char*)&pkg + sizeof(glbpkgheader));

	while (1) {
		//int recv_len = recvfrom(ctx->sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &addr_len);
		int recv_len = 0;
		int res = glbio_read(ctx, &pkg, &recv_len, (struct sockaddr*)&from_addr, &addr_len);

		if (res == GLB_SUCCESS && recv_len == 0) { break; } // No data readed
		if (res != GLB_SUCCESS) { continue; } // Invalid packet

		// TODO: Process correct packet

		// SYN only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_SYN && headerptr->client_id == 0xFFFF) {
			// New incoming connection (server mode)
			// Find empty glbconn_t slot (if NOT, send RST "server full")
			// Generate and set new client id
			// Set connection as SYN_RCVD
			// Send SYN ACK
			// Start timer for ACK wait

			glbconn_t* con = glbctx_findemplyconn(ctx);
			if (!con) {
				// No empty slot, send RST
				// Or ignore it
				continue;
			}

			glbctx_generateclientid(ctx, &con->conn_id);
			con->state = GLB_CONNECTION_SYN_RCVD;
			con->retry = 0;
			con->peer_addr = from_addr;
			// Send SYN ACK
			// Set state to SYN_RCVD and start a 0ms timer to force send SYN ACK in next glb_tick call
			glbtime_start(&con->time, 0);
		}

		// ACK only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_ACK) {
			// Client sent ACK (server mode)
			// Find client by id & ckeck for state (SYN_RCVD)
			// Set connection as ESTABLISHED
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue;
			}
			if (con->state != GLB_CONNECTION_SYN_RCVD) {
				// No SYN recieved for this connection
				continue;
			}

			con->state = GLB_CONNECTION_ESTABLISHED;

			// Make channels
			glbctx_createconchannels(ctx, con);

			// Generate event
			glbevent_t event;
			event.type = GLB_EVENT_CONNECT;
			event.connect.connection = con;
			glbqueue_push(ctx->eventqueue, &event);

		}

		// SYN ACK only
		int glb_syn_ack = GLB_CTRL_FLAG_SYN | GLB_CTRL_FLAG_ACK;
		if ((headerptr->ctrl_flags & glb_syn_ack) == glb_syn_ack) {
			// Server is sent SYN ACK (client mode)
			// Set client id from server
			// Send ACK
			// Set connection as ESTABLISHED
			glbconn_t* con = &ctx->connections[0];
			con->conn_id.generation = headerptr->client_gen;
			con->conn_id.id = headerptr->client_id;
			//glbtime_start(&con->time, 0);

			if (con->state != GLB_CONNECTION_ESTABLISHED) {

				// Make channels
				glbctx_createconchannels(ctx, con);

				// Generate event
				glbevent_t event;
				event.type = GLB_EVENT_CONNECT;
				event.connect.connection = con;
				glbqueue_push(ctx->eventqueue, &event);

			} // Else just resend ACK for SYN ACK server packet

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_ACK);
			if (glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len) == GLB_SUCCESS) {
				con->state = GLB_CONNECTION_ESTABLISHED;
			}
		}

		// Data recieved
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_DATA) {
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue;
			}
			if (con->state != GLB_CONNECTION_ESTABLISHED) {
				// Connection is not ready
				continue;
			}

			if (headerptr->channel_id >= ctx->channel_count) {
				// Invalid channel id
				continue;
			}

			// TODO: check peer address

			//glbqueue* queue = con->channels[headerptr->channel_id].r_queue;

			// TODO: add data managment (recieve queue or pool, mb add glb_popeventdata?)
			glbevent_t event;
			event.type = GLB_EVENT_RECIEVE;
			//event.recieve.
			glbqueue_push(ctx->eventqueue, &event);
		}


		// DATA ACK only
		int glb_data_ack = GLB_CTRL_FLAG_DATA | GLB_CTRL_FLAG_ACK;
		if ((headerptr->ctrl_flags & glb_data_ack) == glb_data_ack) {
			
		}
	}

	// Check timers
	// TODO: add data transmit function

	for (size_t i = 0; i < ctx->connection_count; i++) {
		glbconn_t* con = &ctx->connections[i];

		// Client mode
		if (con->state == GLB_CONNECTION_SYN_SENT && glbtime_isexpired(&con->time)) {
			if (con->retry >= GLB_MAX_RETRY) {
				// Forget this connection
				con->state = GLB_CONNECTION_CLOSED;

				// Generate event (Connect failed)
				glbevent_t event;
				event.type = GLB_EVENT_DISCONNECT;
				event.disconnect.connection = con;
				event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
				glbqueue_push(ctx->eventqueue, &event);

				continue;
			}

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_SYN);
			glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
			// And start new timer
			glbtime_start(&con->time, GLB_RETRY_TIMEOUT * (con->retry + 1));
			con->retry++;
		}

		// Server mode
		if (con->state == GLB_CONNECTION_SYN_RCVD && glbtime_isexpired(&con->time)) {
			if (con->retry >= GLB_MAX_RETRY) {
				// Forget this connection
				con->state = GLB_CONNECTION_CLOSED;

				continue;
			}

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_SYN | GLB_CTRL_FLAG_ACK);

			glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
			// And start new timer
			glbtime_start(&con->time, GLB_RETRY_TIMEOUT * (con->retry + 1));
			con->retry++;
		}
	}

	return GLB_SUCCESS;
}

int glb_pollevent(glbctx_t* ctx, glbevent_t* event) {
	int len = glbqueue_size(ctx->eventqueue);
	if (len != 0) {
		glbqueue_pop(ctx->eventqueue, event);
		return GLB_SUCCESS;
	}

	return GLB_ERROR_QUEUE_EMPTY;
}

//int glb_getconnectioninfo(glbconn_t* conn) {  }