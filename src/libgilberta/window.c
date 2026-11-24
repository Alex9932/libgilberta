/*
 * Gilberta – Real-time Communication Library
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alex9932
 *
 * Module:    window.c
 * Purpose:   Implements packet window management for reliable transmission.
 *
 * This file is part of the Gilberta library.
 * See the main header (libgilberta.h) for the public API and protocol design.
 *
 */

#include "window.h"
#include "context.h"
#include "glbio.h"

glbwindow* glbwnd_init(glbctx_t* ctx, uint32_t capacity) {
	if (!ctx || capacity == 0 || capacity > 32) {
		if (ctx && ctx->logger) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Invalid arguments for window initialization");
		}
		return NULL;
	}

	void* data = ctx->allocator.malloc(sizeof(glbwindow) + (size_t)capacity * sizeof(glbpkg));
	if (!data) {
		if (ctx->logger) {
			ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to allocate memory for window");
		}
		return NULL;
	}

	glbwindow* wnd = (glbwindow*)data;
	wnd->ctx = ctx;
	wnd->pool = (void*)((char*)data + sizeof(glbwindow)); // Pool starts right after the window struct
	wnd->capacity = capacity;
	//wnd->base_seq = 0;
	wnd->count = 0;
	wnd->occupied = 0;
	return wnd;
}

void glbwnd_free(glbwindow* wnd) {
	if (!wnd) { return; }
	wnd->ctx->allocator.free(wnd);
}

static inline int GLBWND_GET_INDEX(glbwindow* wnd, uint32_t seq, uint32_t* index, uint32_t* bit) {
//	int32_t diff = (int32_t)(seq - wnd->base_seq);
//	if (diff < 0 || (uint32_t)diff >= wnd->capacity) {
//		return GLB_ERROR_WINDOW_OUT_OF_RANGE;
//	}

	*index = seq % wnd->capacity;
	*bit = 1u << *index;

	return GLB_SUCCESS;
}

int glbwnd_insert(glbwindow* wnd, const glbpkg* pkg) {
	if (!wnd || !pkg) { return GLB_ERROR_INVALID_ARGUMENT; }

	uint32_t seq = pkg->header.seq;
	
	glbpkg* slots = (glbpkg*)wnd->pool;
	uint32_t index = 0;
	uint32_t bit = 0;
	int result = GLBWND_GET_INDEX(wnd, seq, &index, &bit);
	if (result != GLB_SUCCESS) { return result; }

	if ((wnd->occupied & bit) && slots[index].header.seq == pkg->header.seq) {
		return GLB_ERROR_WINDOW_DUPLICATE;
	}

	if ((wnd->occupied & bit) && slots[index].header.seq != pkg->header.seq) {
		return GLB_ERROR_WINDOW_INVALID_SEQ;
	}

	slots[index] = *pkg;
	wnd->occupied |= bit;
	wnd->count++;

	return GLB_SUCCESS;
}

int glbwnd_find(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg) {
	if (!wnd) { return GLB_ERROR_INVALID_ARGUMENT; }

	glbpkg* slots = (glbpkg*)wnd->pool;
	uint32_t index = 0;
	uint32_t bit = 0;
	int result = GLBWND_GET_INDEX(wnd, seq, &index, &bit);
	if (result != GLB_SUCCESS) { return result; }

	if (!(wnd->occupied & bit)) { return GLB_ERROR_WINDOW_EMPTY; }
	if (slots[index].header.seq != seq) { return GLB_ERROR_WINDOW_INVALID_SEQ; }

	if (out_pkg) { *out_pkg = slots[index]; }
	return GLB_SUCCESS;
}

int glbwnd_remove(glbwindow* wnd, uint32_t seq, glbpkg* out_pkg) {
	if (!wnd) { return GLB_ERROR_INVALID_ARGUMENT; }

	glbpkg* slots = (glbpkg*)wnd->pool;
	uint32_t index = 0;
	uint32_t bit = 0;
	int result = GLBWND_GET_INDEX(wnd, seq, &index, &bit);
	if (result != GLB_SUCCESS) { return result; }

	if (!(wnd->occupied & bit)) { return GLB_ERROR_WINDOW_EMPTY; }
	if (slots[index].header.seq != seq) { return GLB_ERROR_WINDOW_INVALID_SEQ; }

	if (out_pkg) { *out_pkg = slots[index]; }
	wnd->occupied &= ~bit;
	wnd->count--;

	return GLB_SUCCESS;
}

void glbwnd_clear(glbwindow* wnd) {
	if (!wnd) { return; }
	wnd->occupied = 0;
	wnd->count = 0;
}