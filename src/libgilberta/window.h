/**
 * @file window.h
 * @brief Internal packet window management.
 *
 * Defines the packet window structure and internal functions
 * used to store, retrieve, and manage packets during reliable
 * transmission.
 *
 * @note This header is not part of the public API.
 */

#ifndef LIBGILBERTA_WINDOW_H
#define LIBGILBERTA_WINDOW_H

#include <stdint.h>
#include "libgilberta.h"
#include "glbio.h"

typedef struct glbwindow {
	glbctx_t* ctx;          /**< Parent context (for allocator) */
	void*     pool;         /**< Element storage buffer */
	uint32_t  capacity;     /**< Maximum capacity (in elements) */
	uint32_t  base_seq;
	uint32_t  count;
	uint32_t  occupied;
} glbwindow;

glbwindow* glbwnd_init(glbctx_t* ctx, uint32_t capacity);
void glbwnd_free(glbwindow* wnd);

int glbwnd_insert(glbwindow* wnd, const glbpkg* pkg);
int glbwnd_find(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg);
int glbwnd_remove(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg);

void glbwnd_clear(glbwindow* wnd);

#endif