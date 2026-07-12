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
#define GLB_RETRY_TIMEOUT 100  // ms
#define GLB_RECV_LIMIT_PER_TICK 100  // Limit of packets to process per tick
#define GLB_RETRANSMISSION_TIMEOUT 100 // ms
#define GLB_KEEPALIVE_RETRY 8 // keepalive retry count
#define GLB_KEEPALIVE_TIME  200 // ms, keepalive interval (disconnect after GLB_KEEPALIVE_RETRY)

static void glbpkg_init(glbpkg* pkg, glbconid_t conn_id, uint8_t ctrl_flags, uint8_t channel) {
	pkg->header.magic       = GILBERTA_PROTO_MAGIC;
	pkg->header.payload_len = 0;
	pkg->header.version     = GILBERTA_PROTO_VERSION;
	pkg->header.channel_id  = channel;
	pkg->header.chan_flags  = 0;
	pkg->header.ctrl_flags  = ctrl_flags;
	pkg->header.client_gen  = conn_id.generation;
	pkg->header.client_id   = conn_id.id;
	pkg->header.checksum    = 0xFFFF;
	pkg->header.wnd         = 0;
	pkg->header.seq         = 0;
	pkg->header.ack         = 0;
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
	if (conn->state != GLB_CONNECTION_ESTABLISHED) {
		return GLB_ERROR_CONNECTION_CLOSED;
	}

	// FIN/RST
	conn->state = GLB_CONNECTION_FIN_SENT;
	conn->retry = 0;
	glbtime_start(&conn->time, 0);

	return GLB_SUCCESS;
}

int glb_send(glbctx_t* ctx, glbsendinfo_t* info) {
	if (!ctx || !info || !info->con || !info->data || info->len <= 0 || info->channel_id >= ctx->channel_count) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	if (info->len > GILBERTA_MTU) {
		ctx->error = GLB_ERROR_MESSAGE_TOO_LONG;
		return GLB_ERROR_MESSAGE_TOO_LONG;
	}

	if (info->con->state != GLB_CONNECTION_ESTABLISHED) {
		return GLB_ERROR_CONNECTION_CLOSED;
	}
	
	glbchannel_t* chan = &info->con->channels[info->channel_id];

	// Copy data
	glbpkg pkg;
	glbpkg_init(&pkg, info->con->conn_id, GLB_CTRL_FLAG_DATA, info->channel_id);
	pkg.header.seq = chan->seq;
	pkg.header.payload_len = info->len;
	pkg.retransmit_count = 0;
	memcpy(pkg.data, info->data, info->len);

	// Add timestamp (for retransmission)
	glbtime_start(&pkg.timestamp, 0); // After first send glbtime_start(&pkg.timestamp, GLB_RETRANSMISSION_TIMEOUT);

	// TODO: Add seq

	// Add data to the send queue
	int res = glbqueue_push(chan->s_queue, &pkg);
	if (res == GLB_ERROR_QUEUE_FULL) {
		return GLB_ERROR_QUEUE_FULL;
	}
	chan->seq++; // Increment sequence number for next packet

	return GLB_SUCCESS;
}

int glb_tick(glbctx_t* ctx) {
	glbpkg pkg;
	struct sockaddr_in from_addr;
	socklen_t addr_len = sizeof(from_addr);
	glbpkgheader* headerptr = (glbpkgheader*)&pkg;
	void* dataptr = (void*)((char*)&pkg + sizeof(glbpkgheader));

	ctx->recv_limit = 0;

	int glb_syn_ack  = GLB_CTRL_FLAG_SYN  | GLB_CTRL_FLAG_ACK;
	int glb_fin_ack  = GLB_CTRL_FLAG_FIN  | GLB_CTRL_FLAG_ACK;
	int glb_data_ack = GLB_CTRL_FLAG_DATA | GLB_CTRL_FLAG_ACK;

	while (1) {

		if (ctx->recv_limit >= GLB_RECV_LIMIT_PER_TICK) {
			// Limit reached, break the loop
			break;
		}

		int recv_len = 0;
		int res = glbio_read(ctx, &pkg, &recv_len, (struct sockaddr*)&from_addr, &addr_len);

		if (res == GLB_SUCCESS && recv_len == 0) { break; } // No data readed
		if (res != GLB_SUCCESS) { continue; } // Invalid packet

		ctx->recv_limit++; // Count processed packet

		// SYN only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_SYN && headerptr->client_id == 0xFFFF) {
			// New incoming connection (server mode)
			// Find empty glbconn_t slot (if NOT, send RST "server full")
			// Generate and set new client id
			// Set connection as SYN_RCVD
			// Send SYN ACK
			// Start timer for ACK wait

			glbconn_t* con = glbctx_findemptyconn(ctx);
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

			continue;
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

			if (con->state != GLB_CONNECTION_ESTABLISHED) {
				con->state = GLB_CONNECTION_ESTABLISHED;

				// Make channels
				glbctx_createconchannels(ctx, con);

				// Generate event
				glbevent_t event;
				event.type = GLB_EVENT_CONNECT;
				event.connect.connection = con;
				glbqueue_push(ctx->eventqueue, &event);

				con->keepalive_retry = 0;
				glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME); // Start timer for PING (keep-alive)
			}

			continue;
		}

		// SYN ACK only
		//if ((headerptr->ctrl_flags & glb_syn_ack) == glb_syn_ack) {
		if (headerptr->ctrl_flags == glb_syn_ack) {
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

				con->keepalive_retry = 0;
				glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME); // Start timer for PING (keep-alive)

			} // Else just resend ACK for SYN ACK server packet

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_ACK, 0);
			if (glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len) == GLB_SUCCESS) {
				con->state = GLB_CONNECTION_ESTABLISHED;
			}

			continue;
		}

		// FIN only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_FIN) {
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue; // No such connection, ignore
			}

			// Send FIN ACK and close connection (if not closed)
			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_FIN | GLB_CTRL_FLAG_ACK, 0);
			if (glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len) == GLB_SUCCESS) {

				if (con->state != GLB_CONNECTION_CLOSED) {
					con->state = GLB_CONNECTION_CLOSED;
					// Free queues
					glbctx_freeconchannels(ctx, con);
					
					// Generate event (Disconnect)
					glbevent_t event;
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_BY_PEER;
					glbqueue_push(ctx->eventqueue, &event);
				}
			}

			continue;
		}

		// FIN ACK only
		if (headerptr->ctrl_flags == glb_fin_ack) {
			// Peer aprove FIN, close connection (if not closed)
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue; // No such connection, ignore
			}
			if (con->state != GLB_CONNECTION_CLOSED) {
				con->state = GLB_CONNECTION_CLOSED;
				// Free queues
				glbctx_freeconchannels(ctx, con);

				// Generate event (Disconnect)
				glbevent_t event;
				event.type = GLB_EVENT_DISCONNECT;
				event.disconnect.connection = con;
				event.disconnect.reason = GLB_DISCONNECT_BY_PEER;
				glbqueue_push(ctx->eventqueue, &event);
			}

			continue;
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

			glbqueue* queue = con->channels[headerptr->channel_id].r_queue;
			glbqueue_push(queue, &pkg);

			// TODO: check sequence number, drop duplicates, etc.
			con->channels[headerptr->channel_id].ack = headerptr->seq; // Update ack for this channel

			// Send ACK for this data packet if reliable delivery channel
			if (con->channels[headerptr->channel_id].flags & GLB_CHANNEL_FLAG_RELIABLE) {
				glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_ACK | GLB_CTRL_FLAG_DATA, headerptr->channel_id);
				glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
			}

			glbevent_t event;
			event.type = GLB_EVENT_RECEIVE;
			event.receive.connection = con;
			event.receive.channel = headerptr->channel_id;
			glbqueue_push(ctx->eventqueue, &event);

			continue;
		}


		// DATA ACK only
		if (headerptr->ctrl_flags == glb_data_ack) {
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

			//TODO:
			// con->channels[headerptr->channel_id].ack = headerptr->ack;
			glbqueue_pop(con->channels[headerptr->channel_id].s_queue, NULL); // Remove acknowledged packet from send queue

			continue;
		}

		// PING only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_PING) {
			// Check if connection exists
#if 0 // (or do not do this?)
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue;
			}
#endif
			glbconid_t conn_id;
			conn_id.generation = headerptr->client_gen;
			conn_id.id = headerptr->client_id;
			glbpkg_init(&pkg, conn_id, GLB_CTRL_FLAG_PONG, headerptr->channel_id);
			glbio_send(ctx, &pkg, (struct sockaddr*)&from_addr, addr_len);

			continue;
		}

		// PONG only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_PONG) {
			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				continue;
			}

			con->keepalive_retry = 0; // Reset keepalive retry counter
			
			// TODO: Update RTT (round-trip time) based on timestamp

			// Restart keepalive timer
			glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME);

			continue;
		}
	}

	// Check timers
	// Data transmit

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

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_SYN, 0);
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

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_SYN | GLB_CTRL_FLAG_ACK, 0);

			glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
			// And start new timer
			glbtime_start(&con->time, GLB_RETRY_TIMEOUT * (con->retry + 1));
			con->retry++;
		}

		if (con->state == GLB_CONNECTION_FIN_SENT && glbtime_isexpired(&con->time)) {
			if (con->retry >= GLB_MAX_RETRY) {
				// Force close
				con->state = GLB_CONNECTION_CLOSED;

				// Free queues
				glbctx_freeconchannels(ctx, con);

				// Generate event (Disconnect)
				glbevent_t event;
				event.type = GLB_EVENT_DISCONNECT;
				event.disconnect.connection = con;
				event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
				glbqueue_push(ctx->eventqueue, &event);
				continue;
			}

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_FIN, 0);
			glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
			glbtime_start(&con->time, GLB_RETRY_TIMEOUT * (con->retry + 1));

			con->retry++;
		}

		// Data transmission (retransmission) for established connections
		if (con->state == GLB_CONNECTION_ESTABLISHED) {
			for (size_t ch = 0; ch < ctx->channel_count; ch++) {
				glbchannel_t* chan = &con->channels[ch];
				size_t queue_size = glbqueue_size(chan->s_queue);
				if (queue_size == 0) {
					continue;
				}

				glbpkg* pkg_ptr = NULL;
				if (glbqueue_peek(chan->s_queue, &pkg_ptr) != GLB_SUCCESS || !pkg_ptr) {
					continue;
				}
				if (glbtime_isexpired(&pkg_ptr->timestamp)) {
					// Resend packet
					glbio_send(ctx, pkg_ptr, (struct sockaddr*)&con->peer_addr, addr_len);

					if ((chan->flags & GLB_CHANNEL_FLAG_RELIABLE) == 0) {
						// Not reliable channel, pop the packet from the queue to avoid infinite retransmission
						glbqueue_pop(chan->s_queue, NULL);
						continue;
					}

					// Restart timer for retransmission
					glbtime_start(&pkg_ptr->timestamp, GLB_RETRANSMISSION_TIMEOUT);
					// Increment retransmission count
					pkg_ptr->retransmit_count++;
					if (pkg_ptr->retransmit_count > GLB_MAX_RETRY) {

						// Pop the packet from the queue to avoid infinite retransmission
						// TODO: Consider notifying the application about the failed packet
						glbqueue_pop(chan->s_queue, &pkg);
						con->loss_count++;
#if 0
						// Too many retransmissions, close connection
						con->state = GLB_CONNECTION_CLOSED;
						glbctx_freeconchannels(ctx, con);
						// Generate event (Disconnect)
						glbevent_t event;
						event.type = GLB_EVENT_DISCONNECT;
						event.disconnect.connection = con;
						event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
						glbqueue_push(ctx->eventqueue, &event);
#endif
					}
				}

			}

			// Ping (keep-alive)
			if (glbtime_isexpired(&con->keepalive)) {
				// Send PING packet
#if 1
				if (con->keepalive_retry > GLB_KEEPALIVE_RETRY) {
					// Too many retries, close connection
					con->state = GLB_CONNECTION_CLOSED;

					// Free queues
					glbctx_freeconchannels(ctx, con);

					// Generate event (Disconnect)
					glbevent_t event;
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
					glbqueue_push(ctx->eventqueue, &event);

					continue;
				}
#else
				if (con->keepalive_retry > GLB_KEEPALIVE_RETRY) {
					ctx->logger(GLB_LOG_WARN, "KEEPALIVE TIMEOUT");
					con->keepalive_retry = 0;
				}
#endif
				glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_PING, 0);
				glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, addr_len);
				glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME * (con->keepalive_retry + 1));
				con->keepalive_retry++;
			}
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

int glb_popdata(glbctx_t* ctx, glbrecvinfo_t* info) {
	// Implementation for popping data
	if (!ctx ||!info || !info->con || !info->buffer || info->buflen == 0) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	glbqueue* queue = info->con->channels[info->channel_id].r_queue;

	glbpkg* pkg_ = NULL;
	if (glbqueue_peek(queue, &pkg_) != GLB_SUCCESS) {
		return GLB_ERROR_QUEUE_EMPTY; // No data available
	}

	//if (info->buflen < GILBERTA_MTU) {
	if (info->buflen < pkg_->header.payload_len) {
		return GLB_ERROR_INVALID_ARGUMENT; // Buffer too small
	}

	glbpkg pkg;
	if (glbqueue_pop(queue, &pkg) == GLB_SUCCESS) {
		memcpy(info->buffer, pkg.data, pkg.header.payload_len);
		info->datalen = pkg.header.payload_len;
	}

	return GLB_SUCCESS;
}

int glb_getconinfo(glbconn_t* con, glbconinfo_t* info) {
	if (!con || !info) return GLB_ERROR_INVALID_ARGUMENT;

	info->channel_count = con->ctx->channel_count;
	info->state = con->state;
	info->inet_port = ntohs(con->peer_addr.sin_port);
	
	// Convert IP address to string
	inet_ntop(AF_INET, &con->peer_addr.sin_addr, info->inet_addr, sizeof(info->inet_addr));

	return GLB_SUCCESS;
}

const char* glb_getstring(uint32_t code) {
	switch (code) {
		// Error codes
		case GLB_SUCCESS:                 return "Success";
		case GLB_ERROR_INVALID_ARGUMENT:  return "Invalid argument";
		case GLB_ERROR_OUT_OF_MEMORY:     return "Out of memory";
		case GLB_ERROR_QUEUE_FULL:        return "Queue full";
		case GLB_ERROR_QUEUE_EMPTY:       return "Queue empty";
		case GLB_ERROR_MESSAGE_TOO_LONG:  return "Message too long";
		case GLB_ERROR_SOCKET_CREATION:   return "Socket creation failed";
		case GLB_ERROR_SOCKET_BINDING:    return "Socket binding failed";
		case GLB_ERROR_CONNECTION_CLOSED: return "Connection closed";
		case GLB_ERROR_SEND_FAILED:       return "Send failed";
		case GLB_ERROR_RECV_FAILED:       return "Receive failed";
		// Disconnect reasons
		case GLB_DISCONNECT_BY_PEER:      return "Disconnected by peer";
		case GLB_DISCONNECT_TIMEOUT:      return "Disconnected by timeout";
		// Events
		case GLB_EVENT_CONNECT:           return "connect";
		case GLB_EVENT_DISCONNECT:        return "disconnect";
		case GLB_EVENT_RECEIVE:           return "receive";
		case GLB_EVENT_ERROR:             return "error";

		default: return "Unknown code";
	}
}