#include "module.h"
#if TESTMODULE_FILETRANSFER

#include <libgilberta.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "input.h"

#define MAX_CONNECTIONS 32

static uint8_t     is_client   = 0;
static uint8_t     is_ipv6     = 0;
static const char* address     = NULL;

static glballoc_t  allocator   = {0};
static glblog_t    logger      = {0};
static glbchan_t   channels[2] = {0};

static char        command[INPUT_BUFFER_LENGTH];

static glbconn_t*  connections[MAX_CONNECTIONS] = { 0 };

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
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-6") == 0) {
			is_ipv6 = 1;
		}
		else if (strcmp(argv[i], "-c") == 0) {
			if (i + 1 >= argc) { return 1; }
			is_client = 1;
			address = argv[++i];
		}
		else {
			return 1;
		}
	}

	return 0;
}

static char* SkipSpaces(char* str) {
	while (*str && isspace((unsigned char)*str)) {
		str++;
	}
	return str;
}

static void TrimTrailingSpaces(char* str) {
	size_t len = strlen(str);

	while (len > 0 && isspace((unsigned char)str[len - 1])) {
		str[--len] = '\0';
	}
}

static int ParsePath(char* path) {
	path = SkipSpaces(path);
	TrimTrailingSpaces(path);

	if (*path == '\0') {
		return 0;
	}

	if (*path == '"') {
		char* end = strrchr(path + 1, '"');
		if (!end) {
			return 0;
		}

		*end = '\0';

		if (*(SkipSpaces(end + 1)) != '\0') {
			return 0;
		}

		memmove(path, path + 1, strlen(path));
	}

	return (*path != '\0');
}

static int CheckFile(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file) { return 0; }
	fclose(file);
	return 1;
}

static void PrintConnectionInfo(glbconn_t* connection) {
	if (!connection) { return; }
	char buffer[256];
	glbconinfo_t coninfo = { 0 };
	glb_getconinfo(connection, &coninfo);
	snprintf(buffer, 256, "Peer - %s:%u", coninfo.inet_addr, coninfo.inet_port);
	log_callback(GLB_LOG_INFO, buffer);
}

static void ProcessCommand(const char* cmd, glbctx_t* ctx) {
	//log_callback(GLB_LOG_ERROR, cmd); // Just echo command

	char buffer[INPUT_BUFFER_LENGTH];

	strncpy(buffer, cmd, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	char* cursor = SkipSpaces(buffer);
	TrimTrailingSpaces(cursor);

	if (*cursor == '\0') {
		return;
	}

	/* Extract command name */
	char* args = cursor;

	while (*args && !isspace((unsigned char)*args)) {
		args++;
	}

	if (*args) {
		*args++ = '\0';
		args = SkipSpaces(args);
	}

	if (strcmp(cursor, "help") == 0) {
		log_callback(GLB_LOG_INFO, "----------- HELP -----------");
		log_callback(GLB_LOG_INFO, " help                 - show this message");
		log_callback(GLB_LOG_INFO, " info                 - show local information");
		log_callback(GLB_LOG_INFO, " peers                - show active connections (server)");
		log_callback(GLB_LOG_INFO, " send <path>          - send file to server (client)");
		log_callback(GLB_LOG_INFO, " send <peer> <path>   - send file to client (server)");
		log_callback(GLB_LOG_INFO, " exit                 - close program");
		log_callback(GLB_LOG_INFO, "----------------------------");
		return;
	}

	if (strcmp(cursor, "info") == 0) {
		log_callback(GLB_LOG_INFO, "----------- INFO -----------");

		if (is_client) {
			log_callback(GLB_LOG_INFO, "Mode: Client");
			if (connections[0]) {
				PrintConnectionInfo(connections[0]);
			}
			else {
				log_callback(GLB_LOG_WARN, "Not connected");
			}
		}
		else {
			uint32_t count = 0;

			log_callback(GLB_LOG_INFO, "Mode: Server");

			for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
				if (connections[i]) {
					count++;
				}
			}

			snprintf(buffer, sizeof(buffer), "Active connections: %u / %u", count, MAX_CONNECTIONS);

			log_callback(GLB_LOG_INFO, buffer);
		}

		log_callback(GLB_LOG_INFO, "----------------------------");
		return;
	}

	if (strcmp(cursor, "peers") == 0) {
		if (is_client) {
			log_callback(GLB_LOG_WARN, "This command is available on server only");
			return;
		}

		log_callback(GLB_LOG_INFO, "----------- PEERS -----------");

		uint32_t count = 0;

		for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
			if (connections[i]) {
				snprintf(buffer, sizeof(buffer), "Peer %u:", i);
				log_callback(GLB_LOG_INFO, buffer);

				PrintConnectionInfo(connections[i]);
				count++;
			}
		}

		if (count == 0) {
			log_callback(GLB_LOG_INFO, "No active connections");
		}

		log_callback(GLB_LOG_INFO, "-----------------------------");

		return;
	}

	if (strcmp(cursor, "send") == 0) {
		glbconn_t* destination = NULL;
		char* path = args;

		if (is_client) {
			/* Client syntax: send <path> */
			destination = connections[0];

			if (!destination) {
				log_callback(GLB_LOG_ERROR, "Not connected to server");
				return;
			}
		}
		else {
			/* Server syntax: send <peer id> <path> */
			char* end = NULL;
			unsigned long peer_id = strtoul(args, &end, 10);

			if (end == args || peer_id >= MAX_CONNECTIONS ||
				!isspace((unsigned char)*end)) {
				log_callback(GLB_LOG_ERROR,
					"Usage: send <peer id> <path>");
				return;
			}

			path = SkipSpaces(end);

			destination = connections[peer_id];

			if (!destination) {
				log_callback(GLB_LOG_ERROR, "Peer not found");
				return;
			}
		}

		if (!ParsePath(path)) {
			log_callback(GLB_LOG_ERROR, "Invalid file path");
			return;
		}

		if (!CheckFile(path)) {
			log_callback(GLB_LOG_ERROR, "Cannot open file");
			return;
		}

		log_callback(GLB_LOG_INFO, path);
		log_callback(GLB_LOG_INFO, "File is accessible. Transfer is not implemented yet.");

		// Send logic

		(void)destination;
		(void)ctx;
		return;
	}

	log_callback(GLB_LOG_WARN, "Unknown command. Type 'help'.");
}

static void LaunchClient() {
	log_callback(GLB_LOG_INFO, "Launching client");

	glbcfg_t config = { 0 };
	config.ip = address;
	config.port = 12345;
	config.flags = GLB_FLAG_NETWORK_V4;
	if (is_ipv6) {
		config.flags = GLB_FLAG_NETWORK_V6;
	}
	config.alloc = &allocator;
	config.log = &logger;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);
	if (!ctx) {
		log_callback(GLB_LOG_ERROR, "Failed to create context!");
		return;
	}

	log_callback(GLB_LOG_INFO, "Connecting to the server...");
	int res = glb_connect(ctx);
	if (res != GLB_SUCCESS) {
		log_callback(GLB_LOG_ERROR, "Failed to connect to server!");
		goto exit;
	}

	mutex_init(&mutex);
	THREAD_HANDLE thread = create_input_thread();

	while (keep_running) {
		glb_tick(ctx);

		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {
			switch (event.type) {

				case GLB_EVENT_CONNECT: {
					// Connected to the server
					connections[0] = event.connect.connection;

					//glbconinfo_t coninfo = { 0 };
					//glb_getconinfo(connections[0], &coninfo);
					//char coninfo_str[256];
					//snprintf(coninfo_str, sizeof(coninfo_str), "Connection info: IP: %s, Port: %d, State: %d, Channels: %d",
					//	coninfo.inet_addr, coninfo.inet_port, coninfo.state, coninfo.channel_count);
					log_callback(GLB_LOG_INFO, "Connected!");
					//log_callback(GLB_LOG_INFO, coninfo_str);
					PrintConnectionInfo(connections[0]);
					break;
				}

				case GLB_EVENT_DISCONNECT: {
					// Disconnected form the server
					log_callback(GLB_LOG_INFO, "Disconnected form server!");
					log_callback(GLB_LOG_INFO, glb_getstring(event.disconnect.reason));
					connections[0] = NULL;
					keep_running = 0;
					break;
				}

				default: { break; }
			}
		}

		int has_command = 0;
		mutex_lock(&mutex);
		if (got_input) {
			memcpy(command, input_buffer, INPUT_BUFFER_LENGTH);
			has_command = 1;
			got_input = 0;
		}
		mutex_unlock(&mutex);

		if (has_command) {
			ProcessCommand(command, ctx);
		}

	}


	join_thread(thread);
	mutex_destroy(&mutex);

	log_callback(GLB_LOG_INFO, "Exiting client...");
	glb_close(connections[0]);

exit:
	// Free context
	glb_destroy(ctx);
}

static int InsertConnection(glbconn_t* con) {
	for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
		if (connections[i] == NULL) {
			connections[i] = con;
			return (int)i;
		}
	}

	return -1;
}

static int RemoveConnection(glbconn_t* con) {
	for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
		if (connections[i] == con) {
			connections[i] = NULL;
			return (int)i;
		}
	}

	return -1;
}

static void LaunchServer() {
	log_callback(GLB_LOG_INFO, "Launching server");

	glbcfg_t config = { 0 };
	config.ip = NULL;
	config.port = 12345;
	config.flags = GLB_FLAG_BIND_PORT;
	if (is_ipv6) {
		config.flags |= GLB_FLAG_NETWORK_MIXED; // Allow v4 and v6 connections
	}
	config.alloc = &allocator;
	config.log = &logger;
	config.max_connections = MAX_CONNECTIONS;
	config.channel_count = 2;
	config.channels = channels;

	// Make context
	glbctx_t* ctx = glb_create(&config);
	if (!ctx) {
		log_callback(GLB_LOG_ERROR, "Failed to create context!");
		return;
	}

	mutex_init(&mutex);
	THREAD_HANDLE thread = create_input_thread();

	while (keep_running) {
		glb_tick(ctx);

		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {
			switch (event.type) {

				case GLB_EVENT_CONNECT: {
					// Client connection
					glbconn_t* connection = event.connect.connection;

					int peer_id = InsertConnection(connection);
					if (peer_id < 0) {
						log_callback(GLB_LOG_ERROR, "Connection list is full!");
					}

					//glbconinfo_t coninfo = { 0 };
					//glb_getconinfo(connection, &coninfo);
					char coninfo_str[256];
					snprintf(coninfo_str, sizeof(coninfo_str), "Peer %d connected", peer_id);
					//snprintf(coninfo_str, sizeof(coninfo_str), "Connection info: IP: %s, Port: %d, State: %d, Channels: %d",
					//	coninfo.inet_addr, coninfo.inet_port, coninfo.state, coninfo.channel_count);
					//log_callback(GLB_LOG_INFO, "Connected!");
					log_callback(GLB_LOG_INFO, coninfo_str);
					PrintConnectionInfo(connection);
					break;
				}

				case GLB_EVENT_DISCONNECT: {
					// Disconnected form the server
					glbconn_t* connection = event.disconnect.connection;

					int peer_id = RemoveConnection(connection);
					//log_callback(GLB_LOG_INFO, "Peer disconnected!");
					//log_callback(GLB_LOG_INFO, glb_getstring(event.disconnect.reason));

					char coninfo_str[256];
					if (peer_id >= 0) {
						snprintf(coninfo_str, sizeof(coninfo_str), "Peer %d disconnected: %s", peer_id, glb_getstring(event.disconnect.reason));
						log_callback(GLB_LOG_INFO, coninfo_str);
					}

					break;
				}

				default: { break; }
			}
		}

		int has_command = 0;
		mutex_lock(&mutex);
		if (got_input) {
			memcpy(command, input_buffer, INPUT_BUFFER_LENGTH);
			has_command = 1;
			got_input = 0;
		}
		mutex_unlock(&mutex);

		if (has_command) {
			ProcessCommand(command, ctx);
		}
	}


	join_thread(thread);
	mutex_destroy(&mutex);

	// Free context
	log_callback(GLB_LOG_INFO, "Exiting server...");
	glb_destroy(ctx);
}

int ModuleMain(int argc, char** argv, Allocator * alloc) {

	log_callback(GLB_LOG_INFO, "Process args");
	if (ProcessArgs(argc, argv)) {
		log_callback(GLB_LOG_INFO, "Invalid args!");
		return 1;
	}

	log_callback(GLB_LOG_INFO, "Starting gilberta filetransfer...");

	// Setup common configuration for both client and server
	allocator.malloc = alloc->mallocptr;
	allocator.free = alloc->freeptr;
	logger.log_func = log_callback;

	channels[0].flags = GLB_CHANNEL_FLAG_RELIABLE_ORDERED;
	channels[0].priority = 0;
	channels[1].flags = GLB_CHANNEL_FLAG_RELIABLE_ORDERED;
	channels[1].priority = 0;

	if (is_client) {
		LaunchClient();
	}
	else {
		LaunchServer();
	}

	return 0;
}

#endif