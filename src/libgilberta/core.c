#define DLL_EXPORT
#include "libgilberta.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET GSocketHandle;
const GSocketHandle SOCKET_NULL_HANDLE = INVALID_SOCKET;
static int winsock_usage = 0;
static void GWinSockInit(const glbcfg_t* config) {
	GLBLogFunc logger = NULL;
	if (config->log && config->log->log_func) {
		logger = config->log->log_func;
		logger(GLB_LOG_INFO, "[gilberta] Initializing Winsock...");
	}
	if (winsock_usage == 0) {
		WSADATA wsaData;
		int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (iResult != 0 && logger) {
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
#define GLB_INIT_WINSOCK    GWinSockInit
#define GLB_DESTROY_WINSOCK GWinSockDestroy
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int GSocketHandle;
const GSocketHandle SOCKET_NULL_HANDLE = -1;
#define GLB_INIT_WINSOCK
#define GLB_DESTROY_WINSOCK
#endif

typedef struct AddrV4 {
	uint32_t addr;
	uint16_t port;
	uint16_t _offset;
} AddrV4;

struct glbctx_t {
	GSocketHandle sock;
	AddrV4        addr;
	glballoc_t    allocator;
	GLBLogFunc    logger;
};

glbctx_t* glb_create(const glbcfg_t* config) {
	GLB_INIT_WINSOCK(config);
	if (!config->alloc) {
		return NULL;
	}

	glbctx_t* ctx = (glbctx_t*)config->alloc->malloc(sizeof(glbctx_t));
	ctx->allocator.malloc = config->alloc->malloc;
	ctx->allocator.free = config->alloc->free;
	ctx->logger = config->log->log_func;
	ctx->logger(GLB_LOG_INFO, "[gilberta] Context created.");
	return ctx;
}

void glb_destroy(glbctx_t* ctx) {
	if (!ctx) return;
	ctx->logger(GLB_LOG_INFO, "[gilberta] Context destroyed.");
	GLB_DESTROY_WINSOCK(ctx->logger);
	ctx->allocator.free(ctx);
}

int glb_send(glbctx_t* ctx, glbsendinfo_t* info) {

}

int glb_recv(glbctx_t* ctx, glbrecvinfo_t* info) {

}

void glb_tick(glbctx_t* ctx) {

}