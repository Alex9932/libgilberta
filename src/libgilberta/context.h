/**
 * @file context.h
 * @brief Internal API for managing the library context.
 *
 * This header is not part of the public API.
 * Contains the glbctx_t structure and functions to work with it.
 *
 * @note Do not include this file in user code.
 */

#ifndef LIBGILBERTA_CONTEXT_H
#define LIBGILBERTA_CONTEXT_H

#include "libgilberta.h"
#include "glbio.h"

/* ================================= */
/*  Platform socket initialization   */
/* ================================= */

#if defined(GILBERTA_WINDOWS)
/**
 * @brief Initialize Winsock (Windows).
 * @param logger Logging function for error output.
 * @see GWinSockDestroy()
 */
void GWinSockInit(GLBLogFunc logger);
/**
 * @brief Shutdown Winsock (Windows).
 * @param logger Logging function for error output.
 * @see GWinSockInit()
 */
void GWinSockDestroy(GLBLogFunc logger);

#define GLB_INIT_WINSOCK    GWinSockInit    /**< Macro to call initialization */
#define GLB_DESTROY_WINSOCK GWinSockDestroy /**< Macro to call shutdown */
#elif defined(GILBERTA_POSIX)
#define GLB_INIT_WINSOCK    /**< No initialization needed on POSIX */
#define GLB_DESTROY_WINSOCK /**< No shutdown needed on POSIX */
#else
#error "[gilberta] Unsupported platform"
#endif

/**
 * @struct glbctx_t
 * @brief Internal library context structure.
 *
 * Contains all state: socket, connections, queues, configuration.
 * Created by glbctx_create() (called from glb_create()).
 *
 * @warning Do not modify fields directly — use API functions.
 * @see glbctx_create() @see glbctx_destroy()
 */
struct glbctx_t {
	GSocketHandle sock;             /**< UDP socket handle */
	glballoc_t    allocator;        /**< Custom allocator */
	GLBLogFunc    logger;           /**< Logging function (can be NULL) */

	glbqueue*     eventqueue;       /**< Event queue for the user */
	glbconn_t*    connections;      /**< Array of connections (length = connection_count) */
	uint32_t      connection_count; /**< Number of allocated connection slots */

	int           error;            /**< Last error code */
	uint8_t       flags;            /**< Context flags (GLB_FLAG_*) */
	uint8_t       channel_count;    /**< Number of channels per connection */
	uint16_t      client_gen;       /**< Current client ID generation */
	uint16_t      client_id;        /**< Current client ID */

	uint16_t      recv_limit;       /**< Packet limit per one glb_tick() */

	uint16_t      inet_port;        /**< Local port */
	uint16_t      _padding1;        /**< Padding */
	uint32_t      _padding2;        /**< Padding */
	char          inet_addr[128];   /**< Local IP address (string) */

	glbchan_t     channel_configs[256]; /**< Channel configurations (length = channel_count) */
};

/* ================================= */
/*  Context management functions     */
/* ================================= */

/**
 * @brief Create and initialize the context.
 *
 * Allocates memory for the context, connection array, event queue.
 * Creates a UDP socket and binds it to the specified address.
 *
 * @param config Configuration (not NULL). Must contain valid ip, port, alloc.
 * @return Pointer to the created context or NULL on error.
 * @retval NULL if config == NULL, alloc == NULL, failed to allocate memory,
 *         create socket, or bind().
 *
 * @note Function calls GLB_INIT_WINSOCK on Windows (increments counter).
 * @see glbctx_destroy() @see glbcfg_t
 */
glbctx_t* glbctx_create(const glbcfg_t* config);

/**
 * @brief Destroy the context and free all resources.
 *
 * Closes the socket, frees the event queue, connection array,
 * channels of each connection, and the context itself. Calls GLB_DESTROY_WINSOCK.
 *
 * @param ctx Context to destroy (not NULL).
 * @return GLB_SUCCESS on success, GLB_ERROR_INVALID_ARGUMENT if ctx == NULL.
 *
 * @warning Do not call this function if there are active connections —
 *          close them first via glbctx_freeconchannels().
 * @see glbctx_create()
 */
int glbctx_destroy(glbctx_t* ctx);

/**
 * @brief Generate a unique ID for a new client.
 *
 * Increments the internal counter and writes the new (id, generation) to dst.
 * When id overflows, generation is incremented, id is reset.
 *
 * @param ctx Context (not NULL).
 * @param dst [out] Structure to populate (not NULL).
 *
 * @note IDs are used to identify clients during NAT/IP changes.
 * @see glbconid_t
 */
void glbctx_generateclientid(glbctx_t* ctx, glbconid_t* dst);

/**
 * @brief Find a free connection slot.
 *
 * Searches for the first connection in GLB_CONNECTION_CLOSED state.
 * Used by the server when accepting a new connection.
 *
 * @param ctx Context (not NULL).
 * @return Pointer to the free slot or NULL if all slots are occupied.
 *
 * @note Function does not change the slot state — the calling code does.
 * @see glbconn_t @see GLB_CONNECTION_CLOSED
 */
glbconn_t* glbctx_findemptyconn(glbctx_t* ctx);

/**
 * @brief Find a connection by client ID.
 *
 * Searches for a connection with the specified generation and id.
 * Used to map incoming packets to connections.
 *
 * @param ctx Context (not NULL).
 * @param gen Client generation.
 * @param id  Client ID.
 * @return Pointer to the connection or NULL if not found.
 *
 * @note Both fields are compared: generation and id.
 * @see glbconid_t
 */
glbconn_t* glbctx_findconn(glbctx_t* ctx, uint16_t gen, uint16_t id);

/*
 * @brief Find a connection by address.
 * 
 * @param ctx Context (not NULL).
 * @param addr Address to search for (not NULL).
 * 
 * @return Pointer to the connection or NULL if not found.
 * 
 * @see glbaddr_t
 */
glbconn_t* glbctx_findconnbyaddr(glbctx_t* ctx, glbaddr_t* addr);

/**
 * @brief Create channels for a connection.
 *
 * Allocates memory for the channel array (according to ctx->channel_count)
 * and initializes send queues for each channel.
 *
 * @param ctx Context (not NULL).
 * @param con Connection for which channels are created (not NULL).
 * @return GLB_SUCCESS on success, GLB_ERROR_OUT_OF_MEMORY if failed to allocate.
 *
 * @note Called after successful handshake (SYN-ACK).
 * @see glbctx_freeconchannels() @see glbchannel_t
 */
int glbctx_createconchannels(glbctx_t* ctx, glbconn_t* con);

/**
 * @brief Free connection channels.
 *
 * Frees send queues and the channel array itself.
 * Called when closing a connection or destroying the context.
 *
 * @param ctx Context (not NULL).
 * @param con Connection whose channels need to be freed (not NULL).
 * @return GLB_SUCCESS on success, GLB_ERROR_INVALID_ARGUMENT if con == NULL.
 *
 * @note After the call, con->channels becomes NULL.
 * @see glbctx_createconchannels()
 */
int glbctx_freeconchannels(glbctx_t* ctx, glbconn_t* con);

#endif