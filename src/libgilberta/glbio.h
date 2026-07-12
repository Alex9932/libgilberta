/**
 * @file glbio.h
 * @brief Low-level network I/O and protocol structure.
 *
 * Contains packet definitions, connection states, and functions for
 * sending/receiving UDP packets with checksum verification.
 *
 * @note This header is not part of the public API.
 */

#ifndef LIBGILBERTA_IO_H
#define LIBGILBERTA_IO_H

#include "libgilberta.h"
#include "queue.h"
#include "timer.h"

#if defined(GILBERTA_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET GSocketHandle; /**< Socket handle type */
static const GSocketHandle SOCKET_NULL_HANDLE = INVALID_SOCKET; /**< Invalid handle */
#elif defined(GILBERTA_POSIX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int GSocketHandle; /**< Socket handle type */
static const GSocketHandle SOCKET_NULL_HANDLE = -1; /**< Invalid handle */
#else
#error "[gilberta] Unsupported platform"
#endif

/* ================================= */
/*  Protocol constants               */
/* ================================= */

/** @brief Maximum UDP packet payload size (excluding header). */
#define GILBERTA_MTU 1024

/** @brief Protocol magic number ('GB' = 0x4242). */
#define GILBERTA_PROTO_MAGIC 0x4247 // 'GB'

/** @brief Protocol version (current = 1). */
#define GILBERTA_PROTO_VERSION 1

/* ================================= */
/*  Address and ID types             */
/* ================================= */

/** @brief IPv4 address type (synonym for sockaddr_in). */
typedef struct sockaddr_in glbaddr_t;

/**
 * @struct glbconid_t
 * @brief Logical connection ID (generation + id).
 *
 * Used to identify a client during IP/NAT changes.
 * Generation increments when id overflows.
 *
 * @see glbctx_generateclientid()
 */
typedef struct glbconid_t {
	uint16_t id;         /**< Unique client ID */
	uint16_t generation; /**< Generation (increments on id overflow) */
} glbconid_t;

/* ================================= */
/*  Connection states                */
/* ================================= */

/**
 * @enum GLBConnState
 * @brief Connection states.
 *
 * Handshake state machine:
 *   CLOSED → SYN_SENT (client) or SYN_RCVD (server) → ESTABLISHED → CLOSED
 *
 * @see glbconn_t
 */
typedef enum GLBConnState {
	GLB_CONNECTION_CLOSED = 0,  /**< Connection not established */
	GLB_CONNECTION_ESTABLISHED, /**< Connection is active */
	GLB_CONNECTION_SYN_SENT,    /**< SYN sent, waiting for SYN-ACK (client) */
	GLB_CONNECTION_SYN_RCVD,    /**< SYN received, SYN-ACK sent (server) */
	GLB_CONNECTION_FIN_SENT     /**< FIN sent, waiting for FIN-ACK */
} GLBConnState;

/* ================================= */
/*  Connection states                */
/* ================================= */

/**
 * @struct glbchannel_t
 * @brief Internal channel structure.
 *
 * Each channel has its own send queue and seq/ack counters
 * for reliable delivery.
 *
 * @see glbconn_t @see glbctx_createconchannels()
 */
typedef struct glbchannel_t {
	glbqueue* s_queue; /**< Send queue (packets awaiting transmission) */
	glbqueue* r_queue; /**< Receive queue */
	uint32_t  seq;     /**< Current sequence number (for sending) */
	uint32_t  ack;     /**< Expected acknowledgment number (for receiving) */
} glbchannel_t;

/**
 * @struct glbconn_t
 * @brief Internal connection structure.
 *
 * Contains connection state, peer address, ID, channels, and timers.
 *
 * @see glbctx_t @see glbchannel_t
 */
typedef struct glbconn_t {
	glbctx_t*  ctx;             /**< Parent context */
	glbaddr_t  peer_addr;       /**< Peer address (IP + port) */
	glbconid_t conn_id;         /**< Peer's logical ID */
	uint8_t    state;           /**< Current state (GLBConnState) */
	uint8_t    retry;           /**< Handshake retry counter */
	uint8_t    keepalive_retry; /**< Padding */
	uint8_t    padding2;        /**< Padding */
	uint32_t   loss_count;      /**< Count of lost packets (for statistics) */
	uint32_t   rtt;             /**< Round-trip time in milliseconds */
	glbchannel_t*  channels;    /**< Array of channels (NULL if CLOSED) */
	glbtimestamp_t time;        /**< Time of last activity (for timeouts) */
	glbtimestamp_t keepalive;   /**< Ping timestamp */
} glbconn_t;

/* ================================= */
/*  Control flags (ctrl_flags)       */
/* ================================= */

/** @brief SYN  — connection initiation. */
#define GLB_CTRL_FLAG_SYN  0x01
/** @brief ACK  — acknowledgment. */
#define GLB_CTRL_FLAG_ACK  0x02
/** @brief PING — keepalive request. */
#define GLB_CTRL_FLAG_PING 0x04
/** @brief PONG — keepalive response. */
#define GLB_CTRL_FLAG_PONG 0x08
/** @brief DATA — packet with data. */
#define GLB_CTRL_FLAG_DATA 0x10
/** @brief Reserved for future use. */
#define GLB_CTRL_FLAG_RES1 0x20
/** @brief FIN  — graceful shutdown. */
#define GLB_CTRL_FLAG_FIN  0x40
/** @brief RST  — forced teardown. */
#define GLB_CTRL_FLAG_RST  0x80

/* ================================= */
/*  Packet structures                */
/* ================================= */

/**
 * @struct glbpkgheader
 * @brief Gilberta protocol packet header.
 *
 * Size: 24 bytes. All fields in host byte order (little-endian).
 *
 * @note Checksum is computed only for data,
 *       excluding the header. For packets without payload checksum = 0xFFFF.
 *
 * @see glbpkg
 */
typedef struct glbpkgheader {
	union {
		uint16_t magic;      /**< Magic number (GILBERTA_PROTO_MAGIC) */
		char     magic_c[2]; /**< Same, but byte-by-byte */
	};
	uint16_t payload_len;    /**< Payload length (0..GILBERTA_MTU) */
	uint8_t  version;        /**< Protocol version (GILBERTA_PROTO_VERSION) */
	uint8_t  channel_id;     /**< Channel ID (0..255) */
	uint8_t  chan_flags;     /**< Channel flags (GLB_CHANNEL_FLAG_*) */
	uint8_t  ctrl_flags;     /**< Control flags (GLB_CTRL_FLAG_*) */
	uint16_t client_gen;     /**< Client ID generation */
	uint16_t client_id;      /**< Client ID (0xFFFF for initiation) */
	uint16_t checksum;       /**< CRC16 Modbus of data (0xFFFF if no data) */
	uint16_t wnd;            /**< Window size (flow control) */
	uint32_t seq;            /**< Sequence number */
	uint32_t ack;            /**< Acknowledgment number */
} glbpkgheader;

/**
 * @struct glbpkg
 * @brief Full packet (header + data + timestamp).
 *
 * Used for receiving and sending. The timestamp field is populated
 * upon packet receipt for RTT measurement.
 *
 * @see glbpkgheader @see glbio_send() @see glbio_read()
 */
typedef struct glbpkg {
	glbpkgheader   header;             /**< Packet header */
	char           data[GILBERTA_MTU]; /**< Payload (up to 1024 bytes) */
	glbtimestamp_t timestamp;          /**< Receipt time (for RTT) */
	uint64_t       retransmit_count;   /**< Number of retransmissions (for statistics) */
} glbpkg;

/* ================================= */
/*  I/O functions                    */
/* ================================= */

/**
 * @brief Send a UDP packet.
 *
 * Computes the checksum (CRC16 Modbus) for the payload and sends
 * the packet via the context's socket.
 *
 * @param ctx      Context with socket (not NULL).
 * @param pkg      Packet to send (not NULL). The header.checksum field will be overwritten.
 * @param to_addr  Recipient address (not NULL).
 * @param addr_len Address structure size (sizeof(struct sockaddr_in)).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if ctx == NULL or pkg == NULL.
 * @retval GLB_ERROR_SEND_FAILED if sendto() returned an error.
 *
 * @note Function is non-blocking (socket in non-blocking mode).
 * @see glbio_read()
 */
int glbio_send(glbctx_t* ctx, glbpkg* pkg, struct sockaddr* to_addr, int addr_len);

/**
 * @brief Receive a UDP packet.
 *
 * Reads a packet from the socket, checks magic, version, and checksum.
 * If the packet is invalid, returns an error.
 *
 * @param ctx       Context with socket (not NULL).
 * @param pkg       [out] Buffer for packet (not NULL).
 * @param recvd     [out] Number of bytes received (not NULL).
 * @param from_addr [out] Sender address (not NULL).
 * @param addr_len  [in/out] Address structure size (not NULL).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if any pointer == NULL.
 * @retval GLB_ERROR_RECV_FAILED if recvfrom() returned an error or EWOULDBLOCK.
 * @retval GLB_ERROR_UNKNOWN if magic/version mismatch or checksum is invalid.
 *
 * @note Function is non-blocking (socket in non-blocking mode).
 * @see glbio_send()
 */
int glbio_read(glbctx_t* ctx, glbpkg* pkg, int* recvd, struct sockaddr* from_addr, int* addr_len);

#endif