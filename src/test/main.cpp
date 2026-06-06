#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgilberta.h>

static uint8_t     isClient    = 0;
static const char* address     = NULL;

static glballoc_t  allocator   = {};
static glblog_t    logger      = {};
static glbchan_t   channels[2] = {};

static void log_callback(GLBLogLevel level, const char* message) {
	const char* level_str = "";
	switch (level) {
		case GLB_LOG_INFO:  level_str = "**"; break;
		case GLB_LOG_WARN:  level_str = "!!"; break;
		case GLB_LOG_ERROR: level_str = "@@"; break;
		case GLB_LOG_DEBUG: level_str = "DD"; break;
	}
	printf("%s %s\n", level_str, message);
}

static int ProcessArgs(int argc, char** argv) {
	// -c <ip:port> for client mode, otherwise server mode
	if (argc > 3) { return 1; } // too many args

	if (argc < 2) { return 0; } // no args, default to server mode

	if (strcmp(argv[1], "-c") == 0 && argc == 3) {
		isClient = 1;
		address = argv[2];
	} else {
		return 1; // invalid args
	}

	return 0;
}

static void LaunchClient() {
	log_callback(GLB_LOG_INFO, "Launching client");

	glbcfg_t config = {};
	config.ip       = "127.0.0.1"; // TODO: parse from address
	config.port     = 12345;
	config.flags    = 0;
	config.alloc    = &allocator;
	config.log      = &logger;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);

	// Do nothing

	// Free context
	glb_destroy(ctx);
}

static void LaunchServer() {
	log_callback(GLB_LOG_INFO, "Launching server");

	glbcfg_t config = {};
	config.ip       = NULL;
	config.port     = 12345;
	config.flags    = GLB_FLAG_BIND_PORT;
	config.alloc    = &allocator;
	config.log      = &logger;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);

	// Do nothing

	// Free context
	glb_destroy(ctx);
}

int main(int argc, char** argv) {

	log_callback(GLB_LOG_INFO, "Process args");
	if (ProcessArgs(argc, argv)) {
		log_callback(GLB_LOG_INFO, "Invalid args!");
		return 1;
	}

	log_callback(GLB_LOG_INFO, "Starting gilberta example...");

	// Setup common configuration for both client and server
	allocator.malloc = malloc;
	allocator.free   = free;
	logger.log_func  = log_callback;

	channels[0].flags = 0;
	channels[0].priority = 0;
	channels[1].flags = GLB_CHANNEL_FLAG_RELIABLE;
	channels[1].priority = 0;

	if (isClient) {
		LaunchClient();
	}
	else {
		LaunchServer();
	}

	return 0;
}