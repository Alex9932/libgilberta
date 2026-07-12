/**
* @file libgilberta.h
* @brief Public API of the Gilberta library — UDP transport for real-time communication.
* 
* Gilberta provides a simple non-blocking API for data exchange over UDP
* with support for channels, priorities, reliable delivery, and connection management.
* 
* @section usage Usage Example
* @code
*   glbcfg_t cfg = {
*       .ip = "127.0.0.1",
*       .port = 9000,
*       .flags = GLB_FLAG_BIND_PORT,
*       .channel_count = 2,
*       .eventqueue_length = 64,
*       .max_connections = 16,
*       .alloc = NULL,
*       .log = NULL,
*       .channels = NULL
*   };
*   glbctx_t* ctx = glb_create(&cfg);
*   // ... glb_tick / glb_pollevent loop ...
*   glb_destroy(ctx);
* @endcode
* 
* @author Alex9932
* @version 0.1
* @date 2026
*/

/*
 * Gilberta - Real-time Communication Library
 *
 * Copyright (c) 2026 Alex9932
 * SPDX-License-Identifier: MIT
 *
 * Gilberta is a library for real-time communication between applications,
 * designed to be simple and efficient. It provides a way to send and receive
 * data over a network connection, allowing applications to exchange information
 * in real-time.
 *
 * Features:
 *   - Simple non-blocking API for sending and receiving data
 *   - Cross-platform support (Windows, POSIX)
 *   - Channels with priority levels for message handling
 *   - UDP-based communication for low-latency data transfer
 *   - Transfer control mechanisms (reliable delivery, retransmission, ordering)
 *
 * Protocol design (packet header):
 *   [Header] (16 bytes)
 *    uint16_t magic        // Magic number to identify Gilberta packets
 *    uint16_t payload_len; // Payload length
 *    uint8_t  version;     // Protocol version (1)
 *    uint8_t  channel_id;  // Channel ID
 *    uint8_t  chan_flags;  // Channel flags
 *    uint8_t  ctrl_flags;  // Control flags ACK / SYN / PING / FIN / RST
 *    uint16_t client_gen;  // Client generation
 *    uint16_t client_id;   // Client ID (0xFFFF for initiate connection)
 *    uint16_t checksum;    // CRC16 modbus checksum (data only, for header-only packet = 0xFFFF)
 *    uint16_t wnd;         // Window size
 *    uint32_t seq;         // Sequence number
 *    uint32_t ack;         // Acknowledgment number
 *
 * Usage:
 *   - Create a context with glb_create() and a configuration.
 *   - For server mode, set ip = NULL and flag GLB_FLAG_BIND_PORT.
 *   - For client mode, set ip/port and call glb_connect().
 *   - Send messages via glb_send().
 *   - Poll events with glb_pollevent() to handle incoming data and connections.
 *
 * This header provides the public API for the Gilberta library.
 */

#ifndef GILBERTA_H
#define GILBERTA_H

#include <stdint.h>
#include <stdlib.h>

/* ================================= */
/*  Platform-specific definitions    */
/* ================================= */

#if defined(_WIN32) || defined(_WIN64)
#define GILBERTA_WINDOWS
#ifdef DLL_EXPORT
#define GLB_DECLSPEC __declspec(dllexport)
#else
#define GLB_DECLSPEC __declspec(dllimport)
#endif
#else
#define GILBERTA_POSIX
#define GLB_DECLSPEC
#endif

/* ================================= */
/*  Logging levels                   */
/* ================================= */

/**
 * @enum GLBLogLevel
 * @brief Log message verbosity levels.
 * 
 * Passed to the user-defined logging function (GLBLogFunc).
 */
typedef enum GLBLogLevel {
	GLB_LOG_INFO = 0, /**< Informational message (startup, connection, etc.) */
	GLB_LOG_WARN,     /**< Warning (non-critical error, retry, etc.) */
	GLB_LOG_ERROR,    /**< Error (operation failure, invalid data) */
	GLB_LOG_DEBUG     /**< Debug message (packet tracing) */
} GLBLogLevel;

/* ================================= */
/*  Error codes                      */
/* ================================= */

/**
 * @enum GLBErrorCode
 * @brief Library function return codes.
 *
 * All public functions return one of these codes.
 * GLB_SUCCESS (0) indicates successful execution.
 * 
 * @see glb_geterror() @see glb_getstring()
 */
typedef enum GLBErrorCode {
	GLB_SUCCESS = 0,            /**< Operation completed successfully */
	GLB_ERROR_UNKNOWN,          /**< Unknown/unclassified error */
	GLB_ERROR_INVALID_ARGUMENT, /**< Invalid argument passed (NULL, wrong size, etc.) */
	GLB_ERROR_OUT_OF_MEMORY,    /**< Failed to allocate memory */
	GLB_ERROR_QUEUE_FULL,       /**< Queue is full, operation impossible */
	GLB_ERROR_QUEUE_EMPTY,      /**< Queue is empty, operation impossible */
	GLB_ERROR_MESSAGE_TOO_LONG, /**< Message exceeds MTU (1024 bytes) */
	GLB_ERROR_SOCKET_CREATION,  /**< Failed to create UDP socket */
	GLB_ERROR_SOCKET_BINDING,   /**< Failed to bind() socket to address */
	GLB_ERROR_CONNECTION_CLOSED,/**< Connection is already closed or torn down */
	GLB_ERROR_SEND_FAILED,      /**< Failed to send packet (sendto) */
	GLB_ERROR_RECV_FAILED,      /**< Failed to receive packet (recvfrom) */
	GLB_ERROR_MAX_ENUM = 0x7F   /**< Internal value — upper enum bound */
} GLBErrorCode;

/* ================================= */
/*  Flags                            */
/* ================================= */

/** @brief Server mode flag: bind to the specified port and accept incoming connections. */
#define GLB_FLAG_BIND_PORT 0x01

/** @brief Channel flag: require reliable delivery (retransmission, ordering). */
#define GLB_CHANNEL_FLAG_RELIABLE 0x01

/* ================================= */
/*  Opaque types                     */
/* ================================= */

typedef struct glbctx_t glbctx_t;   /**< Opaque library context */
typedef struct glbconn_t glbconn_t; /**< Opaque connection */

/* ================================= */
/*  User callback function pointers  */
/* ================================= */

/**
 * @typedef GLBMalloc
 * @brief User-defined memory allocation function signature.
 * @param size Size of the block to allocate in bytes.
 * @return Pointer to allocated memory or NULL on failure.
 */
typedef void* (*GLBMalloc)(size_t);

/**
 * @typedef GLBFree
 * @brief User-defined memory deallocation function signature.
 * @param ptr Pointer to a previously allocated block.
 */
typedef void  (*GLBFree)(void*);

/**
 * @typedef GLBLogFunc
 * @brief User-defined logging function signature.
 * @param level Message level (GLB_LOG_INFO / WARN / ERROR / DEBUG).
 * @param message Message string (null-terminated, UTF-8).
 */
typedef void  (*GLBLogFunc)(GLBLogLevel, const char*);


/* ================================= */
/*  Configuration structures         */
/* ================================= */

/**
 * @struct glballoc_t
 * @brief Memory allocator configuration.
 *
 * TODO: --If both fields are NULL, the library uses standard malloc/free.--
 * If at least one field is set, both must be set.
 *
 * @see glbcfg_t
 */
typedef struct glballoc_t {
	GLBMalloc malloc; /**< Memory allocation function */
	GLBFree   free;   /**< Memory deallocation function */
} glballoc_t;

/**
 * @struct glblog_t
 * @brief Logger configuration.
 *
 * If log_func == NULL, logging is disabled.
 *
 * @see glbcfg_t
 */
typedef struct glblog_t {
	GLBLogFunc log_func; /**< Log handler function */
} glblog_t;

/**
 * @struct glbchan_t
 * @brief Single channel configuration.
 *
 * An array of these structures is passed to glbcfg_t.channels.
 *
 * @see glbcfg_t @see GLB_CHANNEL_FLAG_RELIABLE
 */
typedef struct glbchan_t {
	uint8_t  priority; /**< Channel priority (0 = highest) */
	uint8_t  flags;    /**< Combination of GLB_CHANNEL_FLAG_* */
} glbchan_t;

/**
 * @struct glbcfg_t
 * @brief Configuration for creating the library context.
 *
 * Passed to glb_create(). All fields required if not said otherwise.
 *
 * @note For server mode: ip = NULL, flags |= GLB_FLAG_BIND_PORT.
 * @note For client mode: ip and port are the server's address.
 *
 * @see glb_create()
 */
typedef struct glbcfg_t {
	const char* ip;                /**< IPv4 address (NULL for server) */
	uint16_t    port;              /**< Port (required) */
	uint8_t     flags;             /**< Combination of GLB_FLAG_* */
	uint8_t     channel_count;     /**< Number of channels (0 = channels not used) */
	uint16_t    eventqueue_length; /**< Event queue size (recommended 32–256) */
	uint16_t    max_connections;   /**< Max number of connections (for server) */
	glballoc_t* alloc;             /**< Custom allocator (NULL = malloc/free) */
	glblog_t*   log;               /**< Custom logger (NULL = disabled) */
	glbchan_t*  channels;          /**< Array of channel configs (length = channel_count) */
} glbcfg_t;

/* ================================= */
/*  Connection information           */
/* ================================= */

/**
 * @struct glbconinfo_t
 * @brief Public connection information.
 *
 * Populated by glb_getconinfo().
 *
 * @see glb_getconinfo()
 */
typedef struct glbconinfo_t {
	char     inet_addr[128]; /**< Peer's IPv4 address in string form ("a.b.c.d") */
	uint16_t inet_port;      /**< Peer's port */
	uint8_t  state;          /**< Connection state (GLBConnState) */
	uint8_t  channel_count;  /**< Number of channels in the connection */
	// TODO: Add more connection info (e.g. latency, remote address, etc.)
} glbconinfo_t;

/* ==================================== */
/*  Disconnect reasons and event types  */
/* ==================================== */

/**
 * @enum glb_disconnectreason_t
 * @brief Reason for connection teardown.
 * @see glbevent_disconnect_t
 */
typedef enum glb_disconnectreason_t {
	GLB_DISCONNECT_BY_PEER = 128,  /**< Peer initiated closure (FIN/RST) */
	GLB_DISCONNECT_TIMEOUT,        /**< Response timeout exceeded */
	GLB_DISCONNECT_MAX_ENUM = 0x9F
} glb_disconnectreason_t;

/**
 * @enum glb_event_type_t
 * @brief Event type returned by glb_pollevent().
 * @see glbevent_t @see glb_pollevent()
 */
typedef enum glb_event_type_t {
	GLB_EVENT_NONE = 160,     /**< No events (not used in queue) */
	GLB_EVENT_CONNECT,        /**< New connection established */
	GLB_EVENT_DISCONNECT,     /**< Connection torn down */
	GLB_EVENT_RECEIVE,        /**< Data received from peer */
	GLB_EVENT_ERROR,          /**< An error occurred */
	GLB_EVENT_MAX_ENUM = 0xBF
} glb_event_type_t;

/* ==================================== */
/*  Event structures                    */
/* ==================================== */

/**
 * @struct glbevent_connect_t
 * @brief Data for GLB_EVENT_CONNECT event.
 */
typedef struct glbevent_connect_t {
	glbconn_t* connection; /**< Pointer to the established connection */
} glbevent_connect_t;

/**
 * @struct glbevent_disconnect_t
 * @brief Data for GLB_EVENT_DISCONNECT event.
 */
typedef struct glbevent_disconnect_t {
	glbconn_t*             connection; /**< Pointer to the torn-down connection */
	glb_disconnectreason_t reason;     /**< Reason for teardown */
} glbevent_disconnect_t;

/**
 * @struct glbevent_recieve_t
 * @brief Data for GLB_EVENT_RECIEVE event.
 *
 * @warning The data and length fields are populated by the library. The data pointer
 *          is valid only until the next call to glb_pollevent().
 */
typedef struct glbevent_receive_t {
	glbconn_t* connection; /**< Connection from which data was received */
	uint8_t    channel;    /**< Channel ID */
	uint8_t    _padding0;  /**< Padding */
	uint16_t   _padding1;  /**< Padding */
	uint32_t   _padding2;  /**< Padding */
} glbevent_receive_t;

/**
 * @struct glbevent_t
 * @brief Generic event structure.
 *
 * Returned by glb_pollevent(). The type field determines which union member is valid.
 *
 * @see glb_pollevent() @see glb_event_type_t
 */
typedef struct glbevent_t {
	glb_event_type_t type; /**< Event type */
	uint32_t offset;       /**< Padding / reserved */
	union {
		glbevent_connect_t    connect;    /**< Data for GLB_EVENT_CONNECT */
		glbevent_disconnect_t disconnect; /**< Data for GLB_EVENT_DISCONNECT */
		glbevent_receive_t    receive;    /**< Data for GLB_EVENT_RECEIVE */
		char                  _raw[56];   /**< Raw data (for fix structure length) */
	};
} glbevent_t;

/* ==================================== */
/*  Send/Receive structures             */
/* ==================================== */

/**
 * @struct glbsendinfo_t
 * @brief Parameters for glb_send() call.
 * @see glb_send()
 */
typedef struct glbsendinfo_t {
	const void* data;       /**< Pointer to data to send */
	glbconn_t*  con;        /**< Connection to send into */
	size_t      len;        /**< Data length in bytes (max GILBERTA_MTU) */
	uint8_t     channel_id; /**< Channel ID (0..channel_count-1) */
} glbsendinfo_t;

/**
 * @struct glbrecvinfo_t
 * @brief Data receive parameters.
 * @see glbevent_recieve_t
 */
typedef struct glbrecvinfo_t {
	void*      buffer;     /**< Buffer for receiving */
	glbconn_t* con;        /**< Source connection */
	size_t     buflen;     /**< Buffer size */
	size_t     datalen;    /**< [out] Number of bytes received */
	uint8_t    channel_id; /**< Channel ID */
} glbrecvinfo_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================== */
/*  Public functions                    */
/* ==================================== */

/**
 * @brief Get the last error code for the context.
 *
 * Every operation that fails saves its error code in the internal
 * context field. This function allows reading it.
 *
 * @param ctx Library context (not NULL).
 * @return Error code (GLBErrorCode) or GLB_SUCCESS if no errors occurred.
 * @retval GLB_ERROR_INVALID_ARGUMENT if ctx == NULL.
 *
 * @see glb_getstring()
 */
GLB_DECLSPEC int glb_geterror(glbctx_t* ctx);

/**
 * @brief Create the library context.
 *
 * Allocates and initializes the context: creates a UDP socket, binds it
 * to the specified address (if GLB_FLAG_BIND_PORT is set), allocates memory
 * for connections, channels, and the event queue.
 *
 * @param config Configuration (not NULL). Must remain valid
 *               only during the call (copied internally).
 * @return Pointer to the created context or NULL on error.
 * @retval NULL if config == NULL, failed to allocate memory,
 *         create socket, or bind().
 *
 * @note If config->alloc == NULL, malloc/free are used.
 * @note If config->log == NULL, logging is disabled.
 * @note For server mode, set config->ip = NULL and
 *       config->flags |= GLB_FLAG_BIND_PORT.
 *
 * @see glb_destroy() @see glbcfg_t
 */
GLB_DECLSPEC glbctx_t* glb_create(const glbcfg_t* config);

/**
 * @brief Destroy the library context.
 *
 * Closes the socket, frees all connections, channels, queues, and the context itself.
 * After this call, the ctx pointer becomes invalid.
 *
 * @param ctx Context previously created by glb_create() (can be NULL).
 * @return GLB_SUCCESS on success, GLB_ERROR_INVALID_ARGUMENT if ctx == NULL.
 *
 * @warning Do not call glb_destroy() from event callback functions.
 * @see glb_create()
 */
GLB_DECLSPEC int glb_destroy(glbctx_t* ctx);

/**
 * @brief Initiate connection to a server (client mode).
 *
 * Sends a SYN packet to the address specified during context creation.
 * The connection transitions to GLB_CONNECTION_SYN_SENT state.
 * Upon receiving SYN-ACK from the server, a GLB_EVENT_CONNECT event will be generated.
 *
 * @param ctx Context in client mode (created without GLB_FLAG_BIND_PORT).
 * @return GLB_SUCCESS if SYN was sent, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if ctx == NULL or context is in server mode.
 * @retval GLB_ERROR_SEND_FAILED if failed to send SYN.
 *
 * @note Function is non-blocking. Wait for GLB_EVENT_CONNECT via glb_pollevent().
 * @see glb_pollevent() @see GLB_EVENT_CONNECT
 */
GLB_DECLSPEC int glb_connect(glbctx_t* ctx);

/**
 * @brief Close a connection.
 *
 * Initiates graceful shutdown: sends a FIN packet, frees connection resources
 * (channels, queues). Connection transitions to GLB_CONNECTION_CLOSED.
 *
 * @param con Connection to close (not NULL).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if con == NULL.
 * @retval GLB_ERROR_CONNECTION_CLOSED if connection is already closed.
 *
 * @note After closing, the con pointer remains valid (in the context array),
 *       but its state is CLOSED.
 * @see glbconn_t
 */
GLB_DECLSPEC int glb_close(glbconn_t* con);

/**
 * @brief Enqueue data for sending.
 *
 * Data is copied into the channel's internal queue and will be sent
 * on the next glb_tick() call. Function is non-blocking.
 *
 * @param ctx  Library context (not NULL).
 * @param info Send parameters (not NULL).
 * @return GLB_SUCCESS if data was enqueued, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if ctx == NULL or info == NULL.
 * @retval GLB_ERROR_MESSAGE_TOO_LONG if info->len > GILBERTA_MTU.
 * @retval GLB_ERROR_QUEUE_FULL if channel queue is full.
 * @retval GLB_ERROR_CONNECTION_CLOSED if connection is not in ESTABLISHED state.
 *
 * @note For reliable delivery, the channel must have the GLB_CHANNEL_FLAG_RELIABLE flag.
 * @see glb_tick() @see glbsendinfo_t
 */
GLB_DECLSPEC int glb_send(glbctx_t* ctx, glbsendinfo_t* info);

/**
 * @brief Execute one step of network processing.
 *
 * This function must be called regularly (e.g., in the game loop):
 *  - Receives incoming UDP packets and processes them (handshake, data, ACK).
 *  - Sends packets from channel queues.
 *  - Checks connection timeouts.
 *  - Generates events (connect, disconnect, receive) into the queue.
 *
 * @param ctx Library context (not NULL).
 * @return GLB_SUCCESS, error code
 *
 * @note Call glb_pollevent() after glb_tick() to process events.
 * @note Function is non-blocking and should not take more than a few ms.
 * @see glb_pollevent()
 */
GLB_DECLSPEC int glb_tick(glbctx_t* ctx);

/**
 * @brief Extract one event from the queue.
 *
 * If there are events in the queue, populates the event structure and returns GLB_SUCCESS.
 * If the queue is empty, returns GLB_ERROR_QUEUE_EMPTY.
 *
 * @param ctx   Library context (not NULL).
 * @param event [out] Structure to populate (not NULL).
 * @return GLB_SUCCESS if event was extracted.
 * @retval GLB_ERROR_INVALID_ARGUMENT if ctx == NULL or event == NULL.
 * @retval GLB_ERROR_QUEUE_EMPTY if queue is empty.
 *
 * @code
 *   glbevent_t ev;
 *   while (glb_pollevent(ctx, &ev) == GLB_SUCCESS) {
 *       switch (ev.type) {
 *           case GLB_EVENT_CONNECT:    handle_connect(ev.connect);    break;
 *           case GLB_EVENT_DISCONNECT: handle_disconnect(ev.disconnect); break;
 *           case GLB_EVENT_RECIEVE:    handle_receive(ev.recieve);    break;
 *           default: break;
 *       }
 *   }
 * @endcode
 *
 * @see glbevent_t @see glb_tick()
 */
GLB_DECLSPEC int glb_pollevent(glbctx_t* ctx, glbevent_t* event);

/**
 * @brief Pop data from the receive queue.
 *
 * If there are data packets in the queue, populates the info structure and returns GLB_SUCCESS.
 * If the queue is empty, returns GLB_ERROR_QUEUE_EMPTY.
 *
 * @param info [out] Structure to populate (not NULL).
 * @return GLB_SUCCESS if event was extracted.
 * @retval GLB_ERROR_INVALID_ARGUMENT if info == NULL.
 * @retval GLB_ERROR_QUEUE_EMPTY if queue is empty.
 *
 * @see glbrecvinfo_t @see glb_tick()
 */
GLB_DECLSPEC int glb_popdata(glbctx_t* ctx, glbrecvinfo_t* info);

/**
 * @brief Get connection information.
 *
 * Populates the glbconinfo_t structure with data from the connection's internal state:
 * peer address, port, state, number of channels.
 *
 * @param con  Connection (not NULL).
 * @param info [out] Structure to populate (not NULL).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if con == NULL or info == NULL.
 *
 * @see glbconinfo_t
 */
GLB_DECLSPEC int glb_getconinfo(glbconn_t* con, glbconinfo_t* info);

/**
 * @brief Get string description of an error or event code.
 *
 * Convenient for logging and debugging.
 *
 * @param code Error code (GLBErrorCode) or event (glb_event_type_t).
 * @return Null-terminated string with description. Never returns NULL.
 *
 * @code
 *   int err = glb_geterror(ctx);
 *   printf("Error: %s\n", glb_getstring(err));
 * @endcode
 *
 * @see glb_geterror()
 */
GLB_DECLSPEC const char* glb_getstring(uint32_t code);

#ifdef __cplusplus
}
#endif

#endif