/*
 * Gilberta – Real-time Communication Library
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alex9932
 *
 * Module:    window.c
 * Purpose:   TODO
 *
 * This file is part of the Gilberta library.
 * See the main header (libgilberta.h) for the public API and protocol design.
 *
 */

#include "window.h"
#include "context.h"

typedef struct glbwnd_slot {
	glbpkg pkg;
	uint8_t occupied;
	uint8_t _padding[7];
} glbwnd_slot;

glbwindow* glbwnd_init(glbctx_t* ctx, uint32_t capacity) {
	if (!ctx || capacity == 0) {
		if (ctx && ctx->logger) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid arguments for window initialization");
		}
		return NULL;
	}

	void* data = ctx->allocator.malloc(sizeof(glbwindow) + (size_t)capacity * sizeof(glbwnd_slot));
	if (!data) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to allocate memory for window");
		return NULL;
	}

	glbwindow* wnd = (glbwindow*)data;
	wnd->ctx = ctx;
	wnd->pool = (void*)((char*)data + sizeof(glbwindow)); // Pool starts right after the window struct
	wnd->capacity = capacity;
	// TODO
	return wnd;
}

void glbwnd_free(glbwindow* wnd) {
	if (!wnd) { return; }
	wnd->ctx->allocator.free(wnd);
}

int glbwnd_insert(glbwindow* wnd, const glbpkg* element) {
	if (!wnd || !element) { return GLB_ERROR_INVALID_ARGUMENT; }

	return GLB_SUCCESS;
}

int glbwnd_find(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg) {
	if (!wnd) { return GLB_ERROR_INVALID_ARGUMENT; }

	return GLB_SUCCESS;
}

int glbwnd_remove(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg) {
	if (!wnd) { return GLB_ERROR_INVALID_ARGUMENT; }

	return GLB_SUCCESS;
}

void glbwnd_clear(glbwindow* wnd) {
	if (!wnd) { return; }


}