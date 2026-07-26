#include "module.h"
#if TESTMODULE_SPEED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgilberta.h>

static uint8_t     isClient     = 0;
static const char* address      = NULL;

static glballoc_t  allocator    = { 0 };
static glblog_t    logger       = { 0 };
static glbchan_t   channels[2]  = { 0 };

static int         keep_running = 1;

typedef struct {
	uint64_t start_time_ms;
	uint64_t last_print_time_ms;
	uint64_t bytes_sent;
	uint64_t bytes_received;
	uint32_t packets_sent;
	uint32_t packets_received;
	uint64_t rtt;
} speed_stats_t;

#ifdef _WIN32
#include <windows.h>
static uint64_t get_time_ms(void) {
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
	LARGE_INTEGER count;
	QueryPerformanceCounter(&count);
	return (uint64_t)((count.QuadPart * 1000) / freq.QuadPart);
}
#else
#include <sys/time.h>
static uint64_t get_time_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
#endif

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

static void PrintStats(speed_stats_t* stats, const char* side) {
	uint64_t now = get_time_ms();
	uint64_t elapsed = now - stats->last_print_time_ms;

	if (elapsed >= 1000) {

		double mbps = ((stats->bytes_received) * 8.0) / 1000000.0;

		printf("[%s] Speed: %.2f Mbps | Total: %u KB | Packets: %u | Ping: %zu ms\n",
			side,
			mbps,
			(unsigned int)(stats->bytes_received / 1024),
			stats->packets_received,
			stats->rtt);

		stats->bytes_received     = 0;
		stats->packets_received   = 0;
		stats->last_print_time_ms = now;
	}
}

static void LaunchClient() {
	log_callback(GLB_LOG_INFO, "Starting Gilberta speed test client...");
	glbcfg_t config = { 0 };
	config.ip              = address;
	config.port            = 12345;
	config.flags           = 0;
	config.alloc           = &allocator;
	config.log             = &logger;
	config.max_connections = 0;
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


	uint8_t recv_buffer[1024];

	// Test payload
	uint8_t payload[1024];
	memset(payload, 0xAB, sizeof(payload));

	speed_stats_t stats = {0};

	int test_started = 0;

	glbconn_t* connection = NULL;

	while (keep_running) {
		glb_tick(ctx);
		glbevent_t event;
		while (glb_pollevent(ctx, &event) == GLB_SUCCESS) {
			switch (event.type) {
				case GLB_EVENT_CONNECT: {
					connection = event.connect.connection;
					test_started = 1;
					log_callback(GLB_LOG_INFO, "Connected! Starting speed test...");
					stats.start_time_ms = get_time_ms();
					stats.last_print_time_ms = stats.start_time_ms;
					break;
				}
				case GLB_EVENT_DISCONNECT: {
					log_callback(GLB_LOG_INFO, "Disconnected from server.");
					keep_running = 0;
					break;
				}
				case GLB_EVENT_RECEIVE: {
					glbrecvinfo_t rinfo = {
						.buffer     = recv_buffer,
						.buflen     = sizeof(recv_buffer),
						.con        = event.receive.connection,
						.channel_id = event.receive.channel
					};
					if (glb_popdata(ctx, &rinfo) == GLB_SUCCESS) {
						uint64_t timestamp = 0;
						memcpy(&timestamp, payload, sizeof(uint64_t));
						if (stats.rtt != 0) {
							stats.rtt += get_time_ms() - timestamp;
							stats.rtt /= 2;
						}
						else {
							stats.rtt = get_time_ms() - timestamp;
						}

						stats.bytes_received += rinfo.datalen;
						stats.packets_received++;
					}
					break;
				}
				default: { break; }
			}
		}

		if (connection && test_started) {
			// Add timestamp
			uint64_t timestamp = get_time_ms();
			memcpy(payload, &timestamp, sizeof(uint64_t));

			glbsendinfo_t sinfo = {
				.channel_id = 0,
				.con        = connection,
				.data       = payload,
				.len        = sizeof(payload)
			};
			int res = glb_send(ctx, &sinfo);
			if (res == GLB_SUCCESS) {
				stats.bytes_sent += sizeof(payload);
				stats.packets_sent++;
			} else if (res == GLB_ERROR_QUEUE_FULL) {
				// Wait
			} else {
				log_callback(GLB_LOG_ERROR, "Send failed!");
				keep_running = 0;
			}
		}

		if (test_started) {
			PrintStats(&stats, "CLIENT");
		}
	}

	// Free context
	glb_destroy(ctx);
}

static void LaunchServer() {
	log_callback(GLB_LOG_INFO, "Starting Gilberta speed test server...");
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

	uint8_t recv_buffer[1024];
	glbconn_t* connections[16] = { 0 };

	log_callback(GLB_LOG_INFO, "Waiting for connections...");
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
					// Receive the data and send back
					glbrecvinfo_t rinfo = {
						.buffer = recv_buffer,
						.buflen = sizeof(recv_buffer),
						.con = event.receive.connection,
						.channel_id = event.receive.channel
					};
					if (glb_popdata(ctx, &rinfo) == GLB_SUCCESS) {
						glbsendinfo_t sinfo = {
							.channel_id = event.receive.channel,
							.con = event.receive.connection,
							.data = recv_buffer,
							.len = sizeof(recv_buffer)
						};
						int res = glb_send(ctx, &sinfo);
						if (res != GLB_SUCCESS && res != GLB_ERROR_QUEUE_FULL) {
							// Send failed
							log_callback(GLB_LOG_ERROR, "Send failed!");
						}
					}
					break;
				}
				default: { break; }
			}
		}
	}

	// Free context
	glb_destroy(ctx);
}

#if GLB_TEST_CLIENT
int _main(int argc, char** argv, Allocator* alloc);
int ModuleMain(int argc, char** argv, Allocator* alloc) {
	int _argc = 3;
	char* _argv[] = {
		argv[0],
		"-c",
		"127.0.0.1"
	};
	return _main(_argc, _argv, alloc);
}
int _main(int argc, char** argv, Allocator* alloc) {
#else
int ModuleMain(int argc, char** argv, Allocator * alloc) {
#endif

	log_callback(GLB_LOG_INFO, "Process args");
	if (ProcessArgs(argc, argv)) {
		log_callback(GLB_LOG_INFO, "Invalid args!");
		return 1;
	}

	// Setup common configuration for both client and server
	allocator.malloc = alloc->mallocptr;
	allocator.free   = alloc->freeptr;
	logger.log_func  = log_callback;

	channels[0].flags    = GLB_CHANNEL_FLAG_RELIABLE;
	channels[0].priority = 0;
	channels[1].flags    = 0;
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