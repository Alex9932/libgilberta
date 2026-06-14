#define DLL_EXPORT
#include "context.h"
#include <stdio.h>

#if defined(GILBERTA_WINDOWS)
// logger is NOT a NULL pointer, guaranteed by glb_create() and glb_destroy() preconditions
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
	const char* level_str = "";
	switch (level) {
	case GLB_LOG_INFO:  level_str = "**"; break;
	case GLB_LOG_WARN:  level_str = "!!"; break;
	case GLB_LOG_ERROR: level_str = "@@"; break;
	case GLB_LOG_DEBUG: level_str = "DD"; break;
	}
	printf("%s [glb logger] %s\n", level_str, message);
}

static int GLB_CreateSocket(glbctx_t* ctx, const glbcfg_t* config) {
	ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (ctx->sock == SOCKET_NULL_HANDLE) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to create socket.");
		return GLB_ERROR_SOCKET_CREATION;
	}
	return GLB_SUCCESS;
}

static int GLB_CloseSocket(glbctx_t* ctx) {
	if (ctx->sock != SOCKET_NULL_HANDLE) {
		closesocket(ctx->sock);
		ctx->sock = SOCKET_NULL_HANDLE;
	}
	return GLB_SUCCESS;
}

static int GLB_BindSocket(glbctx_t* ctx, const glbcfg_t* config) {
	char logbuf[256];
	const char* ip = config->ip ? config->ip : "0.0.0.0";
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(config->port);
	inet_pton(AF_INET, ip, &server_addr.sin_addr);
	snprintf(logbuf, sizeof(logbuf), "[gilberta] Binding socket to %s:%d...", ip, config->port);
	ctx->logger(GLB_LOG_INFO, logbuf);
	if (bind(ctx->sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to bind socket.");
		return GLB_ERROR_SOCKET_BINDING;
	}
	return GLB_SUCCESS;
}

glbctx_t* glb_create(const glbcfg_t* config) {
	GLBLogFunc logger = GLB_DefaultLogger;
	if (!config) { return NULL; }
	if (!config->alloc) { return NULL; }
	if (config->log && config->log->log_func) {
		logger = config->log->log_func;
	}

	GLB_INIT_WINSOCK(logger);

	glbctx_t* ctx = (glbctx_t*)config->alloc->malloc(sizeof(glbctx_t));
	ctx->allocator.malloc = config->alloc->malloc;
	ctx->allocator.free = config->alloc->free;
	ctx->logger = logger;

	if (GLB_CreateSocket(ctx, config) != GLB_SUCCESS) {
		ctx->allocator.free(ctx);
		return NULL;
	}

	if (config->flags & GLB_FLAG_BIND_PORT) {
		if (GLB_BindSocket(ctx, config) != GLB_SUCCESS) {
			GLB_CloseSocket(ctx);
			ctx->allocator.free(ctx);
			return NULL;
		}

		// Initialize system queues for server mode
		ctx->sys_send = glbqueue_init(ctx, sizeof(glbpkgheader), 128);
		ctx->sys_recv = glbqueue_init(ctx, sizeof(glbpkgheader), 128);
	}

	ctx->logger(GLB_LOG_INFO, "[gilberta] Context created.");
	return ctx;
}

int glb_destroy(glbctx_t* ctx) {
	if (!ctx) return GLB_ERROR_INVALID_ARGUMENT;

	glbqueue_free(ctx->sys_send);
	glbqueue_free(ctx->sys_recv);

	GLB_CloseSocket(ctx);

	ctx->logger(GLB_LOG_INFO, "[gilberta] Context destroyed.");
	GLB_DESTROY_WINSOCK(ctx->logger);
	ctx->allocator.free(ctx);

	return GLB_SUCCESS;
}
