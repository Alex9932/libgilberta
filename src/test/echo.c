#include "module.h"
#if TESTMODULE_ECHO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgilberta.h>

static uint8_t     isClient    = 0;
static const char* address     = NULL;

static glballoc_t  allocator   = {0};
static glblog_t    logger      = {0};
static glbchan_t   channels[2] = {0};

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

static void LaunchClient() {
	log_callback(GLB_LOG_INFO, "Launching client");

	glbcfg_t config = {0};
	config.ip = address;
	config.port = 12345;
	config.flags = 0;
	config.alloc = &allocator;
	config.log = &logger;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);

	log_callback(GLB_LOG_INFO, "Connecting to the server...");
	int res = glb_connect(ctx);
	if (res != GLB_SUCCESS) {
		log_callback(GLB_LOG_ERROR, "Failed to connect to server!");
		goto exit;
	}

	const char* msg = "Hello, Gilberta!";
	glbconn_t* connection;

	char recv_buffer[1024];

	// Force gilberta to process the send queue and handle incoming packets (e.g. connection ack, etc.)
	int running = 1;
	while (running) {
		glb_tick(ctx);
		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {
			switch (event.type) {
			case GLB_EVENT_CONNECT: {
				// Connected to the client

				connection = event.connect.connection;

				glbconinfo_t coninfo = {0};
				glb_getconinfo(connection, &coninfo);
				char coninfo_str[256];
				snprintf(coninfo_str, sizeof(coninfo_str), "Connection info: IP: %s, Port: %d, State: %d, Channels: %d",
					coninfo.inet_addr, coninfo.inet_port, coninfo.state, coninfo.channel_count);
				log_callback(GLB_LOG_INFO, "Connected!");
				log_callback(GLB_LOG_INFO, coninfo_str);

				glbsendinfo_t send_info = {0};
				send_info.data = msg;
				send_info.len = strlen(msg);
				send_info.con = connection;
				send_info.channel_id = 1;
				log_callback(GLB_LOG_INFO, "Sending message to the server...");
				int result = glb_send(ctx, &send_info);
				if (result != GLB_SUCCESS) {
					log_callback(GLB_LOG_ERROR, "Failed to send data to server!");
				}

				break;
			}
			case GLB_EVENT_DISCONNECT: {
				// Disconnected form server
				log_callback(GLB_LOG_INFO, "Disconnected form server!");
				log_callback(GLB_LOG_INFO, glb_getstring(event.disconnect.reason));
				running = 0;
				break;
			}
			case GLB_EVENT_RECEIVE: {
				glbrecvinfo_t recv_info = {0};
				recv_info.buffer = recv_buffer;
				recv_info.buflen = sizeof(recv_buffer);
				recv_info.con = event.receive.connection;
				recv_info.channel_id = event.receive.channel;
				if (glb_popdata(ctx, &recv_info) == GLB_SUCCESS) {

					char coninfo_str[256];
					snprintf(coninfo_str, sizeof(coninfo_str), "-> Channel: %d, data: %.*s",
						recv_info.channel_id, (int)recv_info.datalen, (char*)recv_info.buffer);

					log_callback(GLB_LOG_INFO, "Received data from server:");
					log_callback(GLB_LOG_INFO, coninfo_str);
				}
				// Just close after message recieve
				glb_close(connection);
				break;
			}
			default: {
				//log_callback(GLB_LOG_ERROR, "Unknown event");
				break;
			}
			}
		}
#if 0
		int result = glb_tick(ctx);
		if (result != GLB_SUCCESS) {
			log_callback(GLB_LOG_ERROR, "Failed to tick context!");
			break;
		}
#endif


	}

	log_callback(GLB_LOG_INFO, "Exiting client...");
	glb_close(connection);

exit:
	// Free context
	glb_destroy(ctx);
}

static void LaunchServer() {
	log_callback(GLB_LOG_INFO, "Launching server");

	glbcfg_t config = {0};
	config.ip = NULL;
	config.port = 12345;
	config.flags = GLB_FLAG_BIND_PORT;
	config.alloc = &allocator;
	config.log = &logger;
	config.max_connections = 16;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);
	if (!ctx) {
		log_callback(GLB_LOG_ERROR, "Failed to create context!");
		return;
	}

	log_callback(GLB_LOG_INFO, "Waiting for connections...");
	while (1) {
		glb_tick(ctx);

		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {
			switch (event.type) {
			case GLB_EVENT_CONNECT: {
				// New client connected
				log_callback(GLB_LOG_INFO, "Accepted new connection!");

				glbconinfo_t coninfo = {0};
				glb_getconinfo(event.connect.connection, &coninfo);
				char coninfo_str[256];
				snprintf(coninfo_str, sizeof(coninfo_str), "Connection info: IP: %s, Port: %d, State: %d, Channels: %d",
					coninfo.inet_addr, coninfo.inet_port, coninfo.state, coninfo.channel_count);
				log_callback(GLB_LOG_INFO, coninfo_str);

				break;
			}
			case GLB_EVENT_DISCONNECT: {
				// Client disconnected

				log_callback(GLB_LOG_INFO, "Client disconnected!");
				log_callback(GLB_LOG_INFO, glb_getstring(event.disconnect.reason));

				break;
			}
			case GLB_EVENT_RECEIVE: {
				// TODO: Send the recieved message back to client (echo server)
				log_callback(GLB_LOG_INFO, "[server] Data received");
				char data[1024];
				glbrecvinfo_t recv_info = {0};
				recv_info.buffer = data;
				recv_info.buflen = 1024;
				recv_info.con = event.receive.connection;
				recv_info.channel_id = event.receive.channel;
				if (glb_popdata(ctx, &recv_info) == GLB_SUCCESS) {
					// Send data back to client
					glbsendinfo_t send_info = {0};
					send_info.data = data;
					send_info.len = recv_info.datalen;
					send_info.con = event.receive.connection;
					send_info.channel_id = event.receive.channel;
					log_callback(GLB_LOG_INFO, "[server] Sending back");
					glb_send(ctx, &send_info);
				}
				break;
			}
			default: {
				//log_callback(GLB_LOG_ERROR, "Unknown event");
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

	log_callback(GLB_LOG_INFO, "Starting gilberta example...");

	// Setup common configuration for both client and server
	allocator.malloc = malloc;
	allocator.free   = free;
	logger.log_func  = log_callback;

	channels[0].flags    = 0;
	channels[0].priority = 0;
	channels[1].flags    = GLB_CHANNEL_FLAG_RELIABLE;
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