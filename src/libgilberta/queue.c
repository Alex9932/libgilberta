/*
 * Gilberta – Real-time Communication Library
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alex9932
 *
 * Module:    queue.c
 * Purpose:   Queue implementation with fixed capacity.
 *
 * This file is part of the Gilberta library.
 * See the main header (gilberta.h) for the public API and protocol design.
 *
 */

#include "queue.h"
#include "context.h"

glbqueue* glbqueue_init(glbctx_t* ctx, uint32_t element_size, uint32_t capacity) {
	void* data = ctx->allocator.malloc(sizeof(glbqueue) + (size_t)capacity * (size_t)element_size);
	if (!data) {
		ctx->logger(GLB_LOG_ERROR, "[gilberta] Failed to allocate memory for queue");
		return NULL;
	}
	glbqueue* queue = (glbqueue*)data;
	queue->ctx = ctx;
	queue->pool = (void*)((char*)data + sizeof(glbqueue)); // Pool starts right after the queue struct
	queue->head = 0;
	queue->length = 0;
	queue->capacity = capacity;
	queue->element_size = element_size;
	return queue;
}

void glbqueue_free(glbqueue* queue) {
	if (!queue) { return; }
	queue->ctx->allocator.free(queue);
}

int glbqueue_push(glbqueue* queue, const void* element) {
	if (!queue) { return GLB_ERROR_INVALID_ARGUMENT; }

	if (queue->length >= queue->capacity) {
		queue->ctx->logger(GLB_LOG_WARN, "[gilberta] Queue is full, cannot push new element");
		return GLB_ERROR_QUEUE_FULL;
	}

	uint32_t tail_index = ((queue->head + queue->length) % queue->capacity) * queue->element_size;
	char* dest = (char*)queue->pool + tail_index;

	// Copy the element to the queue if provided, otherwise push an empty element (all zeros)
	if (element) {
		memcpy(dest, element, queue->element_size);
	} else {
		memset(dest, 0, queue->element_size);
	}
	queue->length++;
	return GLB_SUCCESS;
}

int glbqueue_pop(glbqueue* queue, void* out_element) {
	if (!queue) { return GLB_ERROR_INVALID_ARGUMENT; }

	if (queue->length == 0) {
		queue->ctx->logger(GLB_LOG_WARN, "[gilberta] Queue is empty, cannot pop element");
		return GLB_ERROR_QUEUE_EMPTY;
	}

	char* src = (char*)queue->pool + (queue->head * queue->element_size);

	// Copy the element to the output buffer if provided
	if (out_element) {
		memcpy(out_element, src, queue->element_size);
	}
	queue->head = (queue->head + 1) % queue->capacity;
	queue->length--;
	return GLB_SUCCESS;
}

// change this function to peak the element without moving(copy) data
#if 0
int glbqueue_peek(glbqueue* queue, void* out_element) {
	if (!queue || !out_element) { return GLB_ERROR_INVALID_ARGUMENT; }

	if (queue->length == 0) {
		queue->ctx->logger(GLB_LOG_WARN, "[gilberta] Queue is empty, cannot peek element");
		return GLB_ERROR_QUEUE_EMPTY;
	}

	char* src = (char*)queue->pool + (queue->head * queue->element_size);
	memcpy(out_element, src, queue->element_size);
	return GLB_SUCCESS;
}
#endif
int glbqueue_peek(glbqueue* queue, void** out_element) {
	// Check for NULL pointers and empty queue
	if (!queue || !out_element) { return GLB_ERROR_INVALID_ARGUMENT; }
	if (queue->length == 0) {
		queue->ctx->logger(GLB_LOG_WARN, "[gilberta] Queue is empty, cannot peek element");
		return GLB_ERROR_QUEUE_EMPTY;
	}

	char* src = (char*)queue->pool + (queue->head * queue->element_size);
	// Set the output pointer to point to the element in the queue
	*out_element = src;
	return GLB_SUCCESS;
}