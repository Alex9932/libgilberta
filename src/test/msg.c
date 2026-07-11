#include "module.h"
#if TESTMODULE_MSG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgilberta.h>

#include "input.h"

static uint8_t     isClient    = 0;
static const char* address     = NULL;

static glballoc_t  allocator   = { 0 };
static glblog_t    logger      = { 0 };
static glbchan_t   channels[2] = { 0 };


// WARN:
// Fixed length for example purposes, in a real application you would want to handle dynamic lengths and possibly use a more complex serialization method.
// For simplicity, we will use a fixed-size struct for messages.
typedef struct msgdata_t {
	char username[64];
	char message[512];
} msgdata_t;

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
	}
	else {
		return 1; // invalid args
	}

	return 0;
}

void LaunchClient() {
	log_callback(GLB_LOG_INFO, "Starting client");

	char username[64];
	log_callback(GLB_LOG_INFO, "Enter username:");
	fgets(username, sizeof(username), stdin);
	username[strcspn(username, "\n")] = 0;

	glbcfg_t config = { 0 };
	config.ip              = address;
	config.port            = 12345;
	config.flags           = 0;
	config.alloc           = &allocator;
	config.log             = &logger;
	config.max_connections = 16;
	config.channel_count   = 2;
	config.channels        = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);
	if (!ctx) {
		log_callback(GLB_LOG_ERROR, "Failed to create context!");
		return;
	}

	log_callback(GLB_LOG_INFO, "Connecting to the server...");
	if (glb_connect(ctx) != GLB_SUCCESS) {
		log_callback(GLB_LOG_ERROR, "Failed to connect to the server!");
		glb_destroy(ctx);
		return;
	}

	mutex_init(&mutex);
	THREAD_HANDLE thread = create_input_thread();
	glbconn_t* connection = NULL;

	while (keep_running) {
		glb_tick(ctx);
		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {

			switch (event.type) {
				case GLB_EVENT_CONNECT: {
					log_callback(GLB_LOG_INFO, "Connection established");
					connection = event.connect.connection;
					break;
				}
				case GLB_EVENT_DISCONNECT: {
					log_callback(GLB_LOG_INFO, "Disconnected from the server");
					keep_running = 0;
					break;
				}
				case GLB_EVENT_RECEIVE: {
					log_callback(GLB_LOG_INFO, "Data received from server");
					// Handle received data

					msgdata_t message = { 0 };

					glbrecvinfo_t recv_info = { 0 };
					recv_info.buffer = &message;
					recv_info.buflen = sizeof(message);
					recv_info.con = event.receive.connection;
					recv_info.channel_id = event.receive.channel;
					if (glb_popdata(ctx, &recv_info) == GLB_SUCCESS) {

						// Null-terminate the strings for preventing buffer overflows when printing
						message.username[63] = 0;
						message.message[511] = 0;

						char coninfo_str[256];
						snprintf(coninfo_str, sizeof(coninfo_str), "[%s] %s", message.username, message.message);
						log_callback(GLB_LOG_INFO, coninfo_str);
					}

					break;
				}
				default: {
					log_callback(GLB_LOG_ERROR, "Unknown event");
					break;
				}
			}

		}

		// Wait for user input and send message to the server
		mutex_lock(&mutex);
		if (got_input) {
			msgdata_t message = { 0 };
			strncpy(message.username, username, sizeof(message.username) - 1);
			message.username[sizeof(message.username) - 1] = '\0';
			strncpy(message.message, input_buffer, sizeof(message.message) - 1);
			message.message[sizeof(message.message) - 1] = '\0';

			glbsendinfo_t sendinfo = { 0 };
			sendinfo.channel_id = 0;
			sendinfo.con  = connection;
			sendinfo.data = &message;
			sendinfo.len  = sizeof(message);
			glb_send(ctx, &sendinfo);

			got_input = 0;
		}
		mutex_unlock(&mutex);
	}

	join_thread(thread);
	mutex_destroy(&mutex);

	// Free context
	glb_destroy(ctx);
}

void LaunchServer() {
	log_callback(GLB_LOG_INFO, "Starting server");

	glbcfg_t config = { 0 };
	config.ip              = NULL;
	config.port            = 12345;
	config.flags           = GLB_FLAG_BIND_PORT;
	config.alloc           = &allocator;
	config.log             = &logger;
	config.max_connections = 16;
	config.channel_count   = 2;
	config.channels        = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);
	if (!ctx) {
		log_callback(GLB_LOG_ERROR, "Failed to create context!");
		return;
	}

	log_callback(GLB_LOG_INFO, "Waiting for connections...");

	glbconn_t* connections[16] = { 0 };

	while (keep_running) {
		glb_tick(ctx);
		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {

			switch (event.type) {

				case GLB_EVENT_CONNECT: {
					log_callback(GLB_LOG_INFO, "New connection established");
					for (size_t i = 0; i < 16; i++) {
						if (!connections[i]) { connections[i] = event.connect.connection; break; }
					}
					break;
				}

				case GLB_EVENT_DISCONNECT: {
					log_callback(GLB_LOG_INFO, "User disconnected");
					for (size_t i = 0; i < 16; i++) {
						if (connections[i] == event.disconnect.connection) { connections[i] = NULL; break; }
					}
					break;
				}

				case GLB_EVENT_RECEIVE: {
					log_callback(GLB_LOG_INFO, "Data received from peer");

					msgdata_t message = { 0 };

					glbrecvinfo_t recv_info = { 0 };
					recv_info.buffer = &message;
					recv_info.buflen = sizeof(message);
					recv_info.con = event.receive.connection;
					recv_info.channel_id = event.receive.channel;
					if (glb_popdata(ctx, &recv_info) == GLB_SUCCESS) {

						// Null-terminate the strings for preventing buffer overflows when printing
						message.username[63] = 0;
						message.message[511] = 0;

						char coninfo_str[256];
						snprintf(coninfo_str, sizeof(coninfo_str), "[%s] %s", message.username, message.message);
						log_callback(GLB_LOG_INFO, coninfo_str);
					}

					// Send the received data to other clients (broadcast)
					for (size_t i = 0; i < 16; i++) {
						if (connections[i]) {
							glbsendinfo_t sendinfo = { 0 };
							sendinfo.channel_id = 0;
							sendinfo.con  = connections[i];
							sendinfo.data = &message;
							sendinfo.len  = sizeof(message);
							glb_send(ctx, &sendinfo);
						}
					}

					break;
				}

				default: {
					log_callback(GLB_LOG_ERROR, "Unknown event");
					break;
				}
			}

		}

	}

	// Free context
	glb_destroy(ctx);

}

#if GLB_TEST_CLIENT
int _main(int argc, char** argv);
int ModuleMain(int argc, char** argv) {
	int _argc = 3;
	char* _argv[] = {
		argv[0],
		"-c",
		"127.0.0.1"
	};
	return _main(_argc, _argv);
}
int _main(int argc, char** argv) {
#else
int ModuleMain(int argc, char** argv) {
#endif

	log_callback(GLB_LOG_INFO, "Process args");
	if (ProcessArgs(argc, argv)) {
		log_callback(GLB_LOG_INFO, "Invalid args!");
		return 1;
	}

	log_callback(GLB_LOG_INFO, "Starting Gilberta Messenger ...");

	// Setup common configuration for both client and server
	allocator.malloc = malloc;
	allocator.free = free;
	logger.log_func = log_callback;

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

#endif