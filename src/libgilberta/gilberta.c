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

#include <string.h>

#include <stdio.h>

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
#define GLB_RECV_LIMIT_PER_TICK 16  // Receive limit of packets to process per tick
#define GLB_SEND_LIMIT_PER_TICK 128  // Transmit limit of packets to process per tick
#define GLB_RETRANSMISSION_TIMEOUT 100 // ms
#define GLB_KEEPALIVE_RETRY 8 // keepalive retry count
#define GLB_KEEPALIVE_TIME  200 // ms, keepalive interval (disconnect after GLB_KEEPALIVE_RETRY)
#define GLB_EVENT_RESERVE  4 // Event queue reserve

static inline int32_t glb_seqdiff(uint32_t a, uint32_t b) {
	return (int32_t)(a - b);
}

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
	pkg->header.ack_bits    = 0;
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

// TODO: Add getaddrinfo(AF_UNSPEC) to resolve domain names
int glb_connect(glbctx_t* ctx) {
	if (!ctx) return GLB_ERROR_INVALID_ARGUMENT;

	// Use first element in client mode
	glbconn_t* con = &ctx->connections[0];

	if (glbctx_generateseq(ctx, con, 0) != GLB_SUCCESS) {
		return GLB_ERROR_UNKNOWN;
	}

	con->conn_id.generation = 0x00;
	con->conn_id.id = 0xFFFF;
	con->loss_count = 0;
	con->rtt = 0;
#if 0
	struct sockaddr_in* server_addr = &con->peer_addr;

	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(ctx->inet_port);
	if (inet_pton(AF_INET, ctx->inet_addr, &server_addr->sin_addr) != 1) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid IP address is set.");
		return GLB_ERROR_INVALID_ARGUMENT;
	}
#endif

	memset(&con->peer_addr, 0, sizeof(con->peer_addr));
	if (ctx->flags & GLB_FLAG_NETWORK_V6) {
		struct sockaddr_in6* server_addr = (struct sockaddr_in6*)&con->peer_addr;
		server_addr->sin6_family = AF_INET6;
		server_addr->sin6_port   = htons(ctx->inet_port);

		if (inet_pton(AF_INET6, ctx->inet_addr, &server_addr->sin6_addr) != 1) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid IPv6 address is set.");
			return GLB_ERROR_INVALID_ARGUMENT;
		}

		con->peer_addr_len = sizeof(*server_addr);
	} else {
		struct sockaddr_in* server_addr = (struct sockaddr_in*)&con->peer_addr;
		server_addr->sin_family = AF_INET;
		server_addr->sin_port   = htons(ctx->inet_port);

		if (inet_pton(AF_INET, ctx->inet_addr, &server_addr->sin_addr) != 1) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid IPv4 address is set.");
			return GLB_ERROR_INVALID_ARGUMENT;
		}

		con->peer_addr_len = sizeof(*server_addr);
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
	pkg.header.ack = chan->ack;
	pkg.header.payload_len = (uint16_t)info->len; // Just cast to uint16_t, because we already checked that info->len <= GILBERTA_MTU
	pkg.retransmit_count = 0;
	memcpy(pkg.data, info->data, info->len);

	// Add timestamp (for retransmission)
	glbtime_start(&pkg.timestamp, 0); // After first send glbtime_start(&pkg.timestamp, GLB_RETRANSMISSION_TIMEOUT);

	//printf("ENQUEUED s:%u a:%u\n", pkg.header.seq, pkg.header.ack);

	// Add data to the send queue
	int res = glbqueue_push(chan->s_queue, &pkg);
	if (res == GLB_ERROR_QUEUE_FULL) {
		return GLB_ERROR_QUEUE_FULL;
	}
	chan->seq++; // Increment sequence number for next packet

	return GLB_SUCCESS;
}

static int glb_send_data_ack(glbctx_t* ctx, glbconn_t* con, glbchannel_t* channel, uint8_t channel_id) {
	glbpkg ack_pkg = { 0 };

	glbpkg_init(&ack_pkg, con->conn_id, GLB_CTRL_FLAG_ACK | GLB_CTRL_FLAG_DATA, channel_id);
	ack_pkg.header.seq = channel->seq;
	ack_pkg.header.ack = channel->ack;
	ack_pkg.header.ack_bits = channel->rx_ackbits;

	return glbio_send(ctx, &ack_pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
}

static int glb_deliver_ordered(glbctx_t* ctx, glbconn_t* con, glbchannel_t* channel, glbpkg* pkg) {
	glbqueue* queue = channel->r_queue;

	if (glbqueue_is_full(queue) || glbqueue_size(ctx->eventqueue) + GLB_EVENT_RESERVE >= ctx->eventqueue->capacity) {
		return GLB_ERROR_QUEUE_FULL;
	}

	if (glbqueue_push(queue, pkg) != GLB_SUCCESS) {
		return GLB_ERROR_QUEUE_FULL;
	}

	glbevent_t event = { 0 };
	event.type = GLB_EVENT_RECEIVE;
	event.receive.connection = con;
	event.receive.channel = pkg->header.channel_id;
	if (glbqueue_push(ctx->eventqueue, &event) != GLB_SUCCESS) {
		return GLB_ERROR_QUEUE_FULL;
	}

	return GLB_SUCCESS;
}

static int glb_drain_rx_window(glbctx_t* ctx, glbconn_t* con, glbchannel_t* channel) {
	for (;;) {
		glbpkg buffered;

		//
		if (glbwnd_find(channel->rx_window, channel->ack, &buffered) != GLB_SUCCESS) {
			break;
		}

		// Do not remove the packet if delivery is impossible
		if (glb_deliver_ordered(ctx, con, channel, &buffered) != GLB_SUCCESS) {
			break;
		}

		glbwnd_remove(channel->rx_window, channel->ack, NULL);

		channel->ack++;
		channel->rx_ackbits >>= 1;
	}

	return GLB_SUCCESS;
}

static int glb_receive_data(glbctx_t* ctx, glbconn_t* con, glbchannel_t* channel, glbpkg* pkg) {

	uint32_t seq        = pkg->header.seq;
	uint8_t  channel_id = pkg->header.channel_id;

	// Non ordered channels
	if ((channel->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) == 0) {
		glbqueue* queue = channel->r_queue;

		if (glbqueue_is_full(queue) || glbqueue_size(ctx->eventqueue) + GLB_EVENT_RESERVE >= ctx->eventqueue->capacity) {
			return GLB_ERROR_QUEUE_FULL;
		}

		if (glbqueue_push(queue, pkg) != GLB_SUCCESS) {
			return GLB_ERROR_QUEUE_FULL;
		}

		channel->ack++;

		glbevent_t event = { 0 };
		event.type = GLB_EVENT_RECEIVE;
		event.receive.connection = con;
		event.receive.channel = channel_id;
		glbqueue_push(ctx->eventqueue, &event);

		return GLB_SUCCESS;
	}

	glb_drain_rx_window(ctx, con, channel);

	int32_t diff = glb_seqdiff(seq, channel->ack);

	// Already delivered, acknowledge the duplicate again
	if (diff < 0) {
		glb_send_data_ack(ctx, con, channel, channel_id);
		return GLB_SUCCESS;
	}

	// Outside the receive window
	if (diff > (int32_t)GLB_WINDOW_CAPACITY) {
		glb_send_data_ack(ctx, con, channel, channel_id);
		return GLB_SUCCESS;
	}

	// The packet we are waiting for
	if (diff == 0) {
		if (glb_deliver_ordered(ctx, con, channel, pkg) != GLB_SUCCESS) {
			return GLB_ERROR_QUEUE_FULL;
		}

		channel->ack++;
		channel->rx_ackbits >>= 1;

		glb_send_data_ack(ctx, con, channel, channel_id);

		return GLB_SUCCESS;
	}

	// Store out-of-order packet
	int result = glbwnd_insert(channel->rx_window, pkg);

	if (result == GLB_SUCCESS || result == GLB_ERROR_WINDOW_DUPLICATE) {
		channel->rx_ackbits |= 1u << (diff - 1);
	}
	else {
		return result;
	}

	glb_send_data_ack(ctx, con, channel, channel_id);

	return GLB_SUCCESS;

#if 0
	char buffer[128];

	// Сheck sequence number, drop duplicates, etc.
	if (glb_seqdiff(pkg->header.seq, channel->ack) < 0 && channel->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) {
		// Old packet
		snprintf(buffer, 128, "[gilberta] Old packet detected! Sequence number %u, expected %u (channel: %u)", pkg->header.seq, channel->ack, (uint32_t)pkg->header.channel_id);
		ctx->logger(GLB_LOG_DEBUG, buffer);

		// Send ack

		uint32_t ack = pkg->header.seq; // Save sequence
		glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_ACK | GLB_CTRL_FLAG_DATA, pkg->header.channel_id);
		pkg->header.ack = ack;
		pkg->header.seq = channel->seq;
		//printf("RESEND s:%u a:%u\n", pkg.header.seq, pkg.header.ack);
		glbio_send(ctx, pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);

		return GLB_SUCCESS;
	}

	//if (headerptr->seq > channel->ack && channel->flags & GLB_CHANNEL_FLAG_RELIABLE) {
	if (glb_seqdiff(pkg->header.seq, channel->ack) > 0 && channel->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) {
		// packet from future, add this one to window and mark ackbits



#if 0
		con->desync_count++;
		snprintf(buffer, 128, "[gilberta] DESYNC! Sequence number %u, expected %u (channel: %u)", pkg->header.seq, channel->ack, (uint32_t)pkg->header.channel_id);
		ctx->logger(GLB_LOG_ERROR, buffer);
#endif
		return GLB_SUCCESS;
	}

	glbqueue* queue = channel->r_queue;
	if (glbqueue_is_full(queue) || glbqueue_size(ctx->eventqueue) + GLB_EVENT_RESERVE >= ctx->eventqueue->capacity) {
		ctx->logger(GLB_LOG_DEBUG, "[gilberta] Receive queue full, packet dropped");
		return GLB_ERROR_QUEUE_FULL;
	}

	//printf("[gilberta] DATA RECVD [channel seq:%u ack:%u] [header seq:%u ack:%u]\n",
	//	channel->seq, channel->ack,
	//	headerptr->seq, headerptr->ack
	//);


	glbqueue_push(queue, pkg);
	channel->ack++;// = headerptr->seq; // Update ack for this channel

	// Send ACK for this data packet if reliable delivery channel
	uint32_t cid = pkg->header.channel_id; // Save channel id
	uint32_t ack = pkg->header.seq; // Save sequence
	if (channel->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) {
		glbpkg_init(pkg, con->conn_id, GLB_CTRL_FLAG_ACK | GLB_CTRL_FLAG_DATA, cid);
		pkg->header.ack = ack;
		pkg->header.seq = channel->seq;
		//printf("SEND s:%u a:%u\n", pkg.header.seq, pkg.header.ack);
		glbio_send(ctx, pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
	}

	glbevent_t event = { 0 };
	event.type = GLB_EVENT_RECEIVE;
	event.receive.connection = con;
	event.receive.channel = cid;
	glbqueue_push(ctx->eventqueue, &event);

	return GLB_SUCCESS;
#endif
}

static int glb_send_data(glbctx_t* ctx) {
	// Check timers
	// Data transmit

	glbpkg pkg = { 0 };

#if 0
	while (1) {
		if (ctx->send_limit >= GLB_SEND_LIMIT_PER_TICK) {
			// Limit reached, break the loop
			break;
		}
		ctx->send_limit++;
#endif

		for (size_t i = 0; i < ctx->connection_count; i++) {
			glbconn_t* con = &ctx->connections[i];

			// Client mode
			if (con->state == GLB_CONNECTION_SYN_SENT && glbtime_isexpired(&con->time)) {
				if (con->retry >= GLB_MAX_RETRY) {
					// Forget this connection
					con->state = GLB_CONNECTION_CLOSED;

					// Generate event (Connect failed)
					glbevent_t event = { 0 };
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
					glbqueue_push(ctx->eventqueue, &event);

					continue;
				}

				glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_SYN, 0);
				pkg.header.seq = con->seq;
				pkg.header.ack = con->ack;
				glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
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
				pkg.header.seq = con->seq;
				pkg.header.ack = con->ack;
				glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
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
					glbevent_t event = { 0 };
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_TIMEOUT;
					glbqueue_push(ctx->eventqueue, &event);
					continue;
				}

				glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_FIN, 0);
				glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
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
					if (glbqueue_peek(chan->s_queue, (void**)&pkg_ptr) != GLB_SUCCESS || !pkg_ptr) {
						continue;
					}
					if (glbtime_isexpired(&pkg_ptr->timestamp)) {
						// Resend packet

						//printf("Sending pkg: s:%u a:%u (channel s:%u a:%u)\n", pkg_ptr->header.seq, pkg_ptr->header.ack, chan->seq, chan->ack);

						glbio_send(ctx, pkg_ptr, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);

						if ((chan->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) == 0) {
							// Not reliable channel, pop the packet from the queue to avoid infinite retransmission
							glbqueue_pop(chan->s_queue, NULL);
							continue;
						}

						// Restart timer for retransmission
						glbtime_start(&pkg_ptr->timestamp, GLB_RETRANSMISSION_TIMEOUT);
						// Increment retransmission count
						pkg_ptr->retransmit_count++;

						if (pkg_ptr->retransmit_count > GLB_MAX_RETRY) {

							// Do not give-up
							//glbqueue_pop(chan->s_queue, &pkg);
							con->loss_count++;

							// Try again
							pkg_ptr->retransmit_count = 0;

							// Notify the application about this packet / generate event
							glbevent_t event = { 0 };
							event.type = GLB_EVENT_STALLED;
							event.stalled.connection = con;
							event.stalled.seq = pkg_ptr->header.seq;
							glbqueue_push(ctx->eventqueue, &event);

							// TODO: Try RESYNC?
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
						glbevent_t event = { 0 };
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

					glbtimestamp_t time;
					glbtime_start(&time, 0);
					// Use SEQ & ACK fields as timestamp
					pkg.header.seq = (uint32_t)((time & 0xFFFFFFFF00000000) >> 32ull);
					pkg.header.ack = (uint32_t)(time & 0x00000000FFFFFFFF);
					glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len);
					glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME * (con->keepalive_retry + 1));
					con->keepalive_retry++;
				}
			}
		}
#if 0
	}
#endif
}

int glb_tick(glbctx_t* ctx) {
	glbpkg pkg = { 0 };
	glbaddr_t from_addr;
	glbaddrlen_t addr_len;
	glbpkgheader* headerptr = (glbpkgheader*)&pkg;
	void* dataptr = (void*)((char*)&pkg + sizeof(glbpkgheader));

	ctx->recv_limit = 0;
	ctx->send_limit = 0;

	int glb_syn_ack  = GLB_CTRL_FLAG_SYN  | GLB_CTRL_FLAG_ACK;
	int glb_fin_ack  = GLB_CTRL_FLAG_FIN  | GLB_CTRL_FLAG_ACK;
	int glb_data_ack = GLB_CTRL_FLAG_DATA | GLB_CTRL_FLAG_ACK;

	GSocketHandle read_sockets[2] = { ctx->sock, ctx->sock6 };

	int socket_index = 0;
	int empty_reads = 0;

	// Handle incoming messages

	while (ctx->recv_limit < GLB_RECV_LIMIT_PER_TICK && empty_reads < 2) {

		GSocketHandle read_socket = read_sockets[socket_index];
		socket_index ^= 1;
		if (read_socket == SOCKET_NULL_HANDLE) {
			empty_reads++;
			continue;
		}

		memset(&from_addr, 0, sizeof(from_addr));
		addr_len = sizeof(from_addr);
		int recv_len = 0;
		int res = glbio_read(ctx, read_socket, &pkg, &recv_len, (struct sockaddr*)&from_addr, &addr_len);

		if (res == GLB_SUCCESS && recv_len == 0) {
			empty_reads++;
			continue;
		}

		if (res != GLB_SUCCESS) { continue; } // Invalid packet
		empty_reads = 0;

		ctx->recv_limit++; // Count processed packet

		// SYN only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_SYN && headerptr->client_id == 0xFFFF) {
			// New incoming connection (server mode)
			// Find empty glbconn_t slot (if NOT, send RST "server full")
			// Generate and set new client id
			// Set connection as SYN_RCVD
			// Send SYN ACK
			// Start timer for ACK wait

			// Find connection by address (if already exists, resend SYN ACK)
			glbconn_t* con = glbctx_findconnbyaddr(ctx, &from_addr);
			if (!con) {
				// No such connection, find empty slot
				con = glbctx_findemptyconn(ctx);

				if (!con) {
					// No empty slot, send RST
					glbconid_t id = { .generation = 0, .id = 0xFFFF };
					glbpkg_init(&pkg, id, GLB_CTRL_FLAG_RST, 0);
					glbio_send(ctx, &pkg, (struct sockaddr*)&from_addr, addr_len);

					continue;
				}

				// Generate new client id & sequence
				if (glbctx_generateclientid(ctx, &con->conn_id) != GLB_SUCCESS || 
					glbctx_generateseq(ctx, con, pkg.header.seq) != GLB_SUCCESS) { // New seq, sync ack
					continue; // Client repeat SYN
				}
			}

			if (con->state == GLB_CONNECTION_ESTABLISHED) {
				continue; // Skip SYN for established connection
			}

			// Send or resend SYN ACK
			con->state = GLB_CONNECTION_SYN_RCVD;
			con->retry = 0;
			con->rtt   = 0;
			con->loss_count = 0;
			con->peer_addr = from_addr;
			con->peer_addr_len = addr_len;
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

			if (pkg.header.ack != con->seq || pkg.header.seq != con->ack) {
				// TODO
				// Error: Desync
				ctx->logger(GLB_LOG_ERROR, "[gilberta] Connection desync!");
			}

			if (con->state != GLB_CONNECTION_ESTABLISHED) {
				con->state = GLB_CONNECTION_ESTABLISHED;

				glbevent_t event = { 0 };

				// Make channels
				//glbctx_createconchannels(ctx, con);
				if (glbctx_createconchannels(ctx, con) != GLB_SUCCESS) {
					con->state = GLB_CONNECTION_CLOSED;
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_RESET;
					glbqueue_push(ctx->eventqueue, &event);
					continue;
				}
				glbctx_writeack(ctx, con, pkg.header.seq);
				// Generate event
				event.type = GLB_EVENT_CONNECT;
				event.connect.connection = con;
				glbqueue_push(ctx->eventqueue, &event);

				//printf("Connection established! SEQ: %u, ACK: %u\n", con->channels[0].seq, con->channels[0].ack);

				con->keepalive_retry = 0;
				glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME); // Start timer for PING (keep-alive)
			}

			continue;
		}

		// SYN ACK only
		if (headerptr->ctrl_flags == glb_syn_ack) {
			// Server is sent SYN ACK (client mode)
			// Set client id from server
			// Send ACK
			// Set connection as ESTABLISHED
			glbconn_t* con = &ctx->connections[0];

			if (con->state != GLB_CONNECTION_SYN_SENT && con->state != GLB_CONNECTION_ESTABLISHED) {
				continue;
			}

			if (glbctx_addrequal(&con->peer_addr, &from_addr) != GLB_SUCCESS) {
				continue;
			}

			if (pkg.header.ack != con->seq) {
				continue;
			}

			if (con->state == GLB_CONNECTION_SYN_SENT) {
				con->conn_id.generation = headerptr->client_gen;
				con->conn_id.id = headerptr->client_id;
			}

			if (con->state != GLB_CONNECTION_ESTABLISHED) {

				glbevent_t event = { 0 };

				// Make channels
				//glbctx_createconchannels(ctx, con);
				if (glbctx_createconchannels(ctx, con) != GLB_SUCCESS) {
					con->state = GLB_CONNECTION_CLOSED;
					event.type = GLB_EVENT_DISCONNECT;
					event.disconnect.connection = con;
					event.disconnect.reason = GLB_DISCONNECT_RESET;
					glbqueue_push(ctx->eventqueue, &event);
					continue;
				}

				// Generate event
				event.type = GLB_EVENT_CONNECT;
				event.connect.connection = con;
				glbqueue_push(ctx->eventqueue, &event);

				con->keepalive_retry = 0;
				glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME); // Start timer for PING (keep-alive)

				con->ack = pkg.header.seq;
				glbctx_writeack(ctx, con, pkg.header.seq);
				//printf("Connection established! SEQ: %u, ACK: %u\n", con->channels[0].seq, con->channels[0].ack);

			} // Else just resend ACK for SYN ACK server packet

			if (con->seq != pkg.header.ack || con->ack != pkg.header.seq) {
				// TODO
				// Error: Desync
				ctx->logger(GLB_LOG_ERROR, "[gilberta] Connection desync!");
#if DEBUG
				printf("[gilberta] Channel (S: %u, A: %u), header (S: %u, A: %u), init (S: %u, A: %u)\n",
					con->channels[0].seq, con->channels[0].ack,
					headerptr->seq, headerptr->ack,
					con->seq, con->ack
				);
#endif
			}

			glbpkg_init(&pkg, con->conn_id, GLB_CTRL_FLAG_ACK, 0);
			pkg.header.ack = con->channels[0].ack;
			pkg.header.seq = con->channels[0].seq;
			if (glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len) == GLB_SUCCESS) {
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
			if (glbio_send(ctx, &pkg, (struct sockaddr*)&con->peer_addr, con->peer_addr_len) == GLB_SUCCESS) {

				if (con->state != GLB_CONNECTION_CLOSED) {
					con->state = GLB_CONNECTION_CLOSED;
					// Free queues
					glbctx_freeconchannels(ctx, con);

					// Generate event (Disconnect)
					glbevent_t event = { 0 };
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
				glbevent_t event = { 0 };
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

			glbchannel_t* channel = &con->channels[headerptr->channel_id];

			// Check peer address
			if (glbctx_addrequal(&con->peer_addr, &from_addr) != GLB_SUCCESS) {
				// New address (potential NAT rebind or address migration)

				if ((channel->flags & GLB_CHANNEL_FLAG_RELIABLE_ORDERED) && pkg.header.seq == channel->ack) {
					ctx->logger(GLB_LOG_WARN, "[gilberta] Connection migrated to new address");
					con->peer_addr = from_addr;
					con->peer_addr_len = addr_len;

					// Generate event
					glbevent_t event = { 0 };
					event.type = GLB_EVENT_MIGRATION;
					event.migration.connection = con;
					glbqueue_push(ctx->eventqueue, &event);
				}
				else {
					// No trust, skip this one
					return GLB_SUCCESS;
				}
			}

			glb_receive_data(ctx, con, channel, &pkg);

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

			glbchannel_t* channel = &con->channels[headerptr->channel_id];

			glbpkg* lpkg = NULL;
			while (glbqueue_peek(channel->s_queue, (void**)&lpkg) == GLB_SUCCESS) {
				if (glb_seqdiff(lpkg->header.seq, headerptr->ack) >= 0) {
					break;
				}

				glbqueue_pop(channel->s_queue, NULL); // Remove acknowledged packet from send queue
			}

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
			glbconid_t conn_id = { 0 };
			conn_id.generation = headerptr->client_gen;
			conn_id.id = headerptr->client_id;
			uint32_t seq = pkg.header.seq;
			uint32_t ack = pkg.header.ack;
			glbpkg_init(&pkg, conn_id, GLB_CTRL_FLAG_PONG, headerptr->channel_id);
			pkg.header.seq = seq;
			pkg.header.ack = ack;
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

			glbtimestamp_t now;
			glbtimestamp_t time;
			uint64_t seq = (uint64_t)(pkg.header.seq) << 32;
			uint64_t ack = (uint64_t)pkg.header.ack;
			time = seq | ack;

			glbtime_start(&now, 0);
			uint32_t rtt = (uint32_t)(now - time);

			con->rtt = (con->rtt + rtt) / 2;

			// Restart keepalive timer
			glbtime_start(&con->keepalive, GLB_KEEPALIVE_TIME);

			continue;
		}

		// RST only
		if (headerptr->ctrl_flags == GLB_CTRL_FLAG_RST) {
			// Peer sent RST, close connection (if not closed)

			glbconn_t* con = glbctx_findconn(ctx, headerptr->client_gen, headerptr->client_id);
			if (!con) {
				// No such connection, ignore
				continue;
			}

			if (con->state == GLB_CONNECTION_ESTABLISHED) {
				// Free queues
				glbctx_freeconchannels(ctx, con);
			}

			con->state = GLB_CONNECTION_CLOSED;

			// Generate event GLB_DISCONNECT_RESET
			glbevent_t event = { 0 };
			event.type = GLB_EVENT_DISCONNECT;
			event.disconnect.connection = con;
			event.disconnect.reason = GLB_DISCONNECT_RESET;
			glbqueue_push(ctx->eventqueue, &event);

		}
	}

	// Precess TX data
	glb_send_data(ctx);

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

	if (info->channel_id >= ctx->channel_count) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

#if 1
	glbconn_t* con = info->con;

	if (con->state != GLB_CONNECTION_ESTABLISHED || !con->channels) {
		info->datalen = 0;
		return GLB_ERROR_CONNECTION_CLOSED;
	}

	glbqueue* queue = info->con->channels[info->channel_id].r_queue;
	glbpkg* pkg_ = NULL;

	if (glbqueue_peek(queue, (void**)&pkg_) != GLB_SUCCESS) {
		info->datalen = 0;
		return GLB_ERROR_QUEUE_EMPTY; // No data available
	}

	if (info->buflen < pkg_->header.payload_len) {
		return GLB_ERROR_INVALID_ARGUMENT; // Buffer too small
	}

	memcpy(info->buffer, pkg_->data, pkg_->header.payload_len);
	info->datalen = pkg_->header.payload_len;
	glbqueue_pop(queue, NULL);

#else
	glbqueue* queue = info->con->channels[info->channel_id].r_queue;

	glbpkg* pkg_ = NULL;
	if (glbqueue_peek(queue, (void**)&pkg_) != GLB_SUCCESS) {
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
#endif
	return GLB_SUCCESS;
}

int glb_getconinfo(glbconn_t* con, glbconinfo_t* info) {
	if (!con || !info) return GLB_ERROR_INVALID_ARGUMENT;

	info->channel_count = con->ctx->channel_count;
	info->state = con->state;
	info->rtt = con->rtt;
	info->loss = con->loss_count;
	info->desync = con->desync_count;

	// Convert IP address to string and set port
	if (con->peer_addr.ss_family == AF_INET) {
		const struct sockaddr_in* addr4 = (const struct sockaddr_in*)&con->peer_addr;
		info->inet_port = ntohs(addr4->sin_port);
		if (!inet_ntop(AF_INET, &addr4->sin_addr, info->inet_addr, sizeof(info->inet_addr))) {
			return GLB_ERROR_UNKNOWN;
		}
	}
	else if (con->peer_addr.ss_family == AF_INET6) {
		const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)&con->peer_addr;
		info->inet_port = ntohs(addr6->sin6_port);
		if (!inet_ntop(AF_INET6, &addr6->sin6_addr, info->inet_addr, sizeof(info->inet_addr))) {
			return GLB_ERROR_UNKNOWN;
		}
	}
	else {
		return GLB_ERROR_UNKNOWN;
	}

	return GLB_SUCCESS;
}

const char* glb_getstring(uint32_t code) {
	switch (code) {
		// Error codes
		case GLB_SUCCESS:                   return "Success";
		case GLB_ERROR_INVALID_ARGUMENT:    return "Invalid argument";
		case GLB_ERROR_OUT_OF_MEMORY:       return "Out of memory";
		case GLB_ERROR_QUEUE_FULL:          return "Queue full";
		case GLB_ERROR_QUEUE_EMPTY:         return "Queue empty";
		case GLB_ERROR_WINDOW_OUT_OF_RANGE: return "Window out of range";
		case GLB_ERROR_WINDOW_DUPLICATE:    return "Duplicate packet";
		case GLB_ERROR_WINDOW_INVALID_SEQ:  return "Invalid sequence number";
		case GLB_ERROR_WINDOW_EMPTY:        return "Window slot is empty";
		case GLB_ERROR_MESSAGE_TOO_LONG:    return "Message too long";
		case GLB_ERROR_SOCKET_CREATION:     return "Socket creation failed";
		case GLB_ERROR_SOCKET_BINDING:      return "Socket binding failed";
		case GLB_ERROR_CONNECTION_CLOSED:   return "Connection closed";
		case GLB_ERROR_SEND_FAILED:         return "Send failed";
		case GLB_ERROR_RECV_FAILED:         return "Receive failed";
		// Disconnect reasons
		case GLB_DISCONNECT_BY_PEER:        return "Disconnected by peer";
		case GLB_DISCONNECT_TIMEOUT:        return "Disconnected by timeout";
		case GLB_DISCONNECT_RESET:          return "Connection reset";
		// Events
		case GLB_EVENT_CONNECT:             return "connect";
		case GLB_EVENT_DISCONNECT:          return "disconnect";
		case GLB_EVENT_RECEIVE:             return "receive";
		case GLB_EVENT_ERROR:               return "error";

		default: return "Unknown code";
	}
}