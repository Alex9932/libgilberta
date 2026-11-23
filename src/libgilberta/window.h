/**
 * @file window.h
 * @brief TODO
 *
 * TODO
 *
 * @note This header is not part of the public API.
 */

#include <stdint.h>
#include "libgilberta.h"

typedef struct glbwindow {
	glbctx_t* ctx;          /**< Parent context (for allocator) */
	void*     pool;         /**< Element storage buffer */
	// TODO
} glbwindow;

glbwindow* glbwnd_init();
void glbwnd_free(glbwindow* wnd);
