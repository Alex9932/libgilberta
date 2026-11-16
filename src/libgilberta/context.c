/*
 * Gilberta – Real-time Communication Library
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alex9932
 *
 * Module:    context.c
 * Purpose:   Context management. Platform-specific socket wrappers
 *
 * This file is part of the Gilberta library.
 * See the main header (libgilberta.h) for the public API and protocol design.
 * 
 */

#include "context.h"
#include "glbio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memset

#if defined(GILBERTA_WINDOWS)
static int winsock_usage = 0;
// logger is NOT a NULL pointer, guaranteed by glbctx_create() and glbctx_destroy() preconditions
// TODO: Use atomic operations for winsock_usage to avoid race conditions
//   Or add a mutex to protect winsock_usage in multithreaded scenarios
void GWinSockInit(GLBLogFunc logger) {
	if (winsock_usage == 0) {
		logger(GLB_LOG_INFO, "[gilberta] Initializing Winsock...");
		WSADATA wsaData;
		int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (iResult != 0) {
			char errstr[256];
			snprintf(errstr, sizeof(errstr), "[gilberta] WSAStartup failed: %d\n", iResult);
			logger(GLB_LOG_ERROR, errstr);
		}
	}
	winsock_usage++;

}
static void GWinSockDestroy(GLBLogFunc logger) {
	winsock_usage--;
	if (winsock_usage == 0) {
		if (logger) { logger(GLB_LOG_INFO, "[gilberta] Cleanup Winsock..."); }
		WSACleanup();
	}
}
#endif

static void GLB_DefaultLogger(GLBLogLevel level, const char* message) {
#if defined(NDEBUG)
	if (level == GLB_LOG_INFO || level == GLB_LOG_DEBUG) {
		return; // Skip INFO & DEBUG in release build
	}
#endif
	const char* level_str = "";
	switch (level) {
	case GLB_LOG_INFO:  level_str = "**"; break;
	case GLB_LOG_WARN:  level_str = "!!"; break;
	case GLB_LOG_ERROR: level_str = "@@"; break;
	case GLB_LOG_DEBUG: level_str = "DD"; break;
	}
	printf("%s [glb logger] %s\n", level_str, message);
}

static int GLB_SetNonBlock(glbctx_t* ctx, GSocketHandle socket) {
#if defined(GILBERTA_WINDOWS)
	ULONG mode = 1;
	if (ioctlsocket(socket, FIONBIO, &mode)) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to set non-blocking mode.");
		ctx->error = GLB_ERROR_SOCKET_CREATION;
		return GLB_ERROR_SOCKET_CREATION;
	}
#else
	int flags = fcntl(socket, F_GETFL, 0);
	if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to set non-blocking mode.");
		ctx->error = GLB_ERROR_SOCKET_CREATION;
		return GLB_ERROR_SOCKET_CREATION;
	}
#endif
	return GLB_SUCCESS;
}

static int GLB_CloseSocket(GSocketHandle* sock);

// family - AF_INET, AF_INET6
static int GLB_CreateSocket(glbctx_t* ctx, const glbcfg_t* config, GSocketHandle* out, int family) {
	GSocketHandle sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == SOCKET_NULL_HANDLE) {
		if (family == AF_INET) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to create IPv4 socket.");
		} else if (family == AF_INET6) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to create IPv6 socket.");
		}
		ctx->error = GLB_ERROR_SOCKET_CREATION;
		return GLB_ERROR_SOCKET_CREATION;
	}

	if (GLB_SetNonBlock(ctx, sock) != GLB_SUCCESS) {
		GLB_CloseSocket(&sock);
		return GLB_ERROR_SOCKET_CREATION;
	}
	*out = sock;
	return GLB_SUCCESS;
}

static int GLB_CloseSocket(GSocketHandle* sock) {
	if (*sock != SOCKET_NULL_HANDLE) {
#if defined(GILBERTA_WINDOWS)
		closesocket(*sock);
#else
		close(*sock);
#endif
		*sock = SOCKET_NULL_HANDLE;
	}
	return GLB_SUCCESS;
}

static int GLB_BindSocket(glbctx_t* ctx, const glbcfg_t* config, GSocketHandle sock, int family) {
	char logbuf[256];
	char ipbuff[128];

	if (family == AF_INET) {
		struct sockaddr_in addr4 = { 0 };
		addr4.sin_family = AF_INET;
		addr4.sin_port = htons(ctx->inet_port);
		if (config->ip) {
			if (inet_pton(AF_INET, config->ip, &addr4.sin_addr) != 1) {
				return GLB_ERROR_INVALID_ARGUMENT;
			}
		} else {
			addr4.sin_addr.s_addr = htonl(INADDR_ANY);
		}
		inet_ntop(AF_INET, &addr4.sin_addr, ipbuff, sizeof(ipbuff));
		snprintf(logbuf, sizeof(logbuf), "[gilberta] Binding IPv4 socket to %s:%d...", ipbuff, ctx->inet_port);
		ctx->logger(GLB_LOG_INFO, logbuf);
		if (bind(sock, (struct sockaddr*)&addr4, sizeof(addr4)) == 0) {
			return GLB_SUCCESS;
		}
	}
	else if (family == AF_INET6) {
		struct sockaddr_in6 addr6 = {0};
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(ctx->inet_port);
		if (config->ip) {
			if (inet_pton(AF_INET6, config->ip, &addr6.sin6_addr) != 1) {
				return GLB_ERROR_INVALID_ARGUMENT;
			}
		} else {
			addr6.sin6_addr = in6addr_any;
		}
		inet_ntop(AF_INET6, &addr6.sin6_addr, ipbuff, sizeof(ipbuff));
		snprintf(logbuf, sizeof(logbuf), "[gilberta] Binding IPv6 socket to %s:%d...", ipbuff, ctx->inet_port);
		ctx->logger(GLB_LOG_INFO, logbuf);
		if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) == 0) {
			return GLB_SUCCESS;
		}
	}
	else {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to bind socket.");
	ctx->error = GLB_ERROR_SOCKET_BINDING;
	return GLB_ERROR_SOCKET_BINDING;
}

int glbctx_createconchannels(glbctx_t* ctx, glbconn_t* con) {
	con->channels = (glbchannel_t*)ctx->allocator.malloc(sizeof(glbchannel_t) * ctx->channel_count);
	if (!con->channels) {
		return GLB_ERROR_OUT_OF_MEMORY;
	}

	for (size_t i = 0; i < ctx->channel_count; i++) {
		con->channels[i].flags    = ctx->channel_configs[i].flags;
		con->channels[i].priority = ctx->channel_configs[i].priority;

		con->channels[i].s_queue = glbqueue_init(ctx, sizeof(glbpkg), 32); // TODO: Make queue size configurable
		con->channels[i].r_queue = glbqueue_init(ctx, sizeof(glbpkg), 32); // TODO: Make queue size configurable
		if (!con->channels[i].s_queue || !con->channels[i].r_queue) {
			// Free previously allocated queues and channels
			for (size_t j = 0; j <= i; j++) {
				// We CAN pass NULL to glbqueue_free() because it checks for NULL internally, so we don't need to check here
				glbqueue_free(con->channels[j].s_queue);
				glbqueue_free(con->channels[j].r_queue);
			}
			ctx->allocator.free(con->channels);
			con->channels = NULL;

			return GLB_ERROR_OUT_OF_MEMORY;
		}
	}
	return GLB_SUCCESS;
}

int glbctx_freeconchannels(glbctx_t* ctx, glbconn_t* con) {
	if (!con->channels) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}
	for (size_t i = 0; i < ctx->channel_count; i++) {
		glbqueue_free(con->channels[i].s_queue);
		glbqueue_free(con->channels[i].r_queue);
	}
	ctx->allocator.free(con->channels);
	con->channels = NULL;
	return GLB_SUCCESS;
}

glbctx_t* glbctx_create(const glbcfg_t* config) {
	GLBLogFunc logger = GLB_DefaultLogger;
	glballoc_t galloc = { malloc, free };
	if (!config) { return NULL; }
	if (config->channel_count == 0) {
		return NULL; // Invalid argument
	}
	if (config->alloc && config->alloc->malloc && config->alloc->free) {
		galloc = *config->alloc;
	}
	if (config->log && config->log->log_func) {
		logger = config->log->log_func;
	}

	GLB_INIT_WINSOCK(logger);

	uint32_t conns = 1; // Client has a single connection

	int is_server = (config->flags & GLB_FLAG_BIND_PORT) != 0;
	int use_v4 = (config->flags & GLB_FLAG_NETWORK_V4) != 0;
	int use_v6 = (config->flags & GLB_FLAG_NETWORK_V6) != 0;

	if (!use_v4 && !use_v6) { use_v4 = 1; } // Use IPv4 by default if no flags set
	if (!is_server && use_v4 && use_v6) { return NULL; } // No both v4 and v6 in client mode
	if (is_server) { conns = config->max_connections; }

	glbctx_t* ctx = (glbctx_t*)galloc.malloc(sizeof(glbctx_t) + sizeof(glbconn_t) * (size_t)conns + sizeof(glbchan_t) * config->channel_count);
	if (!ctx) {
		return NULL;
	}

	// Invlidate socket handles
	ctx->sock = SOCKET_NULL_HANDLE;
	ctx->sock6 = SOCKET_NULL_HANDLE;

	ctx->allocator.malloc = galloc.malloc;
	ctx->allocator.free = galloc.free;
	ctx->logger = logger;
	ctx->flags = config->flags;
	ctx->connection_count = conns;
	ctx->channel_count = config->channel_count;
	ctx->inet_port = config->port;

	ctx->client_gen = 0;
	ctx->client_id = 1;
	ctx->recv_limit = 0;

	// glbconn_t array is placed right after glbctx_t structure
	ctx->connections = (glbconn_t*)((char*)ctx + sizeof(glbctx_t));
	// glbchan_t array is placed after glbchan_t array
	ctx->channel_configs = (glbchan_t*)((char*)ctx->connections + sizeof(glbconn_t) * conns);

	// Copy channels configuration
	for (size_t i = 0; i < ctx->channel_count; i++) {
		ctx->channel_configs[i] = config->channels[i];
	}

	//const char* ip = config->ip ? config->ip : "0.0.0.0";
	const char* ip = config->ip ? config->ip : (use_v6 && !use_v4 ? "::" : "0.0.0.0");
	snprintf(ctx->inet_addr, 128, "%s", ip);

	memset(ctx->connections, 0, sizeof(glbconn_t) * (size_t)conns);
	for (size_t i = 0; i < ctx->connection_count; i++) {
		ctx->connections[i].ctx = ctx;
		ctx->connections[i].state = GLB_CONNECTION_CLOSED;
	}

	// Make IPv4 socket
	if (use_v4) {
		if (GLB_CreateSocket(ctx, config, &ctx->sock, AF_INET) != GLB_SUCCESS) {
			ctx->allocator.free(ctx);
			return NULL;
		}
	}

	// Make IPv6 socket
	if (use_v6) {
		if (GLB_CreateSocket(ctx, config, &ctx->sock6, AF_INET6) != GLB_SUCCESS) {
			GLB_CloseSocket(&ctx->sock); // Close v4 socket if created
			ctx->allocator.free(ctx);
			return NULL;
		}
	}

	// Bind socket in server mode (v4 or v6 NOT BOTH)
	if (is_server && ctx->sock != SOCKET_NULL_HANDLE) {
		if (GLB_BindSocket(ctx, config, ctx->sock, AF_INET) != GLB_SUCCESS) {
			GLB_CloseSocket(&ctx->sock);
			GLB_CloseSocket(&ctx->sock6);
			ctx->allocator.free(ctx);
			return NULL;
		}
	}

	if (is_server && ctx->sock6 != SOCKET_NULL_HANDLE) {
		if (GLB_BindSocket(ctx, config, ctx->sock6, AF_INET6) != GLB_SUCCESS) {
			GLB_CloseSocket(&ctx->sock);
			GLB_CloseSocket(&ctx->sock6);
			ctx->allocator.free(ctx);
			return NULL;
		}
	}


	// Initialize system queue, use 1024 by default
	ctx->eventqueue = glbqueue_init(ctx, sizeof(glbevent_t), config->eventqueue_length == 0 ? 1024 : config->eventqueue_length);
	if(!ctx->eventqueue) {
		if (ctx->sock != SOCKET_NULL_HANDLE)  GLB_CloseSocket(&ctx->sock);
		if (ctx->sock6 != SOCKET_NULL_HANDLE) GLB_CloseSocket(&ctx->sock6);
		ctx->allocator.free(ctx);
		return NULL;
	}

	ctx->logger(GLB_LOG_INFO, "[gilberta] Context created.");
	ctx->error = GLB_SUCCESS;
	return ctx;
}

int glbctx_destroy(glbctx_t* ctx) {
	glbqueue_free(ctx->eventqueue);
	for (size_t i = 0; i < ctx->connection_count; i++) {
		glbconn_t* con = &ctx->connections[i];
		// NULL check NOT needed because glbctx_freeconchannels() checks for NULL
		glbctx_freeconchannels(ctx, con);
	}

	if (ctx->sock != SOCKET_NULL_HANDLE)  GLB_CloseSocket(&ctx->sock);
	if (ctx->sock6 != SOCKET_NULL_HANDLE) GLB_CloseSocket(&ctx->sock6);

	ctx->logger(GLB_LOG_INFO, "[gilberta] Context destroyed.");
	GLB_DESTROY_WINSOCK(ctx->logger);
	ctx->allocator.free(ctx);

	return GLB_SUCCESS;
}

void glbctx_generateclientid(glbctx_t* ctx, glbconid_t* dst) {
	dst->generation = ctx->client_gen;
	dst->id = ctx->client_id;
	ctx->client_id++;
	if (ctx->client_id == 0xFFFF) {
		ctx->client_id = 0;
		dst->generation++;
	}
}

glbconn_t* glbctx_findemptyconn(glbctx_t* ctx) {
	for (size_t i = 0; i < ctx->connection_count; i++) {
		glbconn_t* con = &ctx->connections[i];
		if (con->state == GLB_CONNECTION_CLOSED) {
			con->conn_id.generation = 0;
			con->conn_id.id = 0;
			con->retry = 0;
			con->padding2 = 0;
			con->channels = NULL;
			con->time = 0;
			return con;
		}
	}
	return NULL;
}

glbconn_t* glbctx_findconn(glbctx_t* ctx, uint16_t gen, uint16_t id) {
	for (size_t i = 0; i < ctx->connection_count; i++) {
		glbconn_t* con = &ctx->connections[i];
		if (con->conn_id.generation == gen && con->conn_id.id == id) {
			return con;
		}
	}
	return NULL;
}

glbconn_t* glbctx_findconnbyaddr(glbctx_t* ctx, glbaddr_t* addr) {
	for (size_t i = 0; i < ctx->connection_count; i++) {
		glbconn_t* con = &ctx->connections[i];
		if (glbctx_addrequal(&con->peer_addr, addr) == GLB_SUCCESS) {
			return con;
		}
	}
	return NULL;
}

int glbctx_generateseq(glbctx_t* ctx, glbconn_t* con, uint32_t ack) {
	if (!ctx || !con) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	// TODO: Use random number
	con->seq = (uint32_t)((size_t)ctx & 0xFFFFFFFF) ^ 0xA5A5A5A5;
	con->ack = ack;

	return GLB_SUCCESS;
}

int glbctx_writeack(glbctx_t* ctx, glbconn_t* con, uint32_t ack) {
	if (!ctx || !con) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	for (size_t i = 0; i < ctx->channel_count; i++) {
		con->channels[i].seq = con->seq;
		con->channels[i].ack = ack;
	}

	return GLB_SUCCESS;
}

int glbctx_addrequal(glbaddr_t* a, glbaddr_t* b) {
	if (!a || !b) {
		return GLB_ERROR_INVALID_ARGUMENT;
	}

	if (a->ss_family != b->ss_family) {
		return GLB_ERROR_UNKNOWN; // Missmatch
	}

	if (a->ss_family == AF_INET) {
		// Check for IPv4
		const struct sockaddr_in* a4 = (const struct sockaddr_in*)a;
		const struct sockaddr_in* b4 = (const struct sockaddr_in*)b;
		return a4->sin_addr.s_addr == b4->sin_addr.s_addr && a4->sin_port == b4->sin_port
			? GLB_SUCCESS
			: GLB_ERROR_UNKNOWN;
	}

	if (a->ss_family == AF_INET6) {
		// Check for IPv6
		const struct sockaddr_in6* a6 = (const struct sockaddr_in6*)a;
		const struct sockaddr_in6* b6 = (const struct sockaddr_in6*)b;
		return a6->sin6_port == b6->sin6_port && a6->sin6_scope_id == b6->sin6_scope_id &&
			memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(struct in6_addr)) == 0
			? GLB_SUCCESS
			: GLB_ERROR_UNKNOWN;
	}

	// Unknown family
	return GLB_ERROR_UNKNOWN;
}