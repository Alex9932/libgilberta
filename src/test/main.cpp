#include <stdio.h>
#include <stdlib.h>
#include <libgilberta.h>

void log_callback(GLBLogLevel level, const char* message) {
	const char* level_str = "";
	switch (level) {
		case GLB_LOG_INFO:  level_str = "**"; break;
		case GLB_LOG_WARN:  level_str = "!!"; break;
		case GLB_LOG_ERROR: level_str = "@@"; break;
		case GLB_LOG_DEBUG: level_str = "DD"; break;
	}
	printf("%s %s\n", level_str, message);
}

int main(int argc, char** argv) {

	// Use the default allocator (malloc/free)
	glballoc_t allocator = {};
	allocator.malloc = malloc;
	allocator.free = free;

	glblog_t logger = {};
	logger.log_func = log_callback;

	glbcfg_t config = {};
	config.ip    = "127.0.0.1";
	config.port  = 12345;
	config.flags = GLB_FLAG_BIND_PORT;
	config.alloc = &allocator;
	config.log   = &logger;

	// Make context
	glbctx_t* ctx = glb_create(&config);

	// Do nothing

	// Free context
	glb_destroy(ctx);

	return 0;
}