#ifndef GILBERTA_QUEUE_H
#define GILBERTA_QUEUE_H

#include <stdint.h>
#include "libgilberta.h"

typedef struct glbqueue {
	glbctx_t* ctx;
	void* pool; // Big buffer for storing queued data
	uint32_t head; // Index of the first byte of valid data
	uint32_t length; // Total length of valid data in the queue
	uint32_t capacity; // Total capacity of the pool in <element_size>
	uint32_t element_size; // Size of each element in bytes
} glbqueue;

glbqueue* glbqueue_init(glbctx_t* ctx, uint32_t element_size, uint32_t capacity);
void glbqueue_free(glbqueue* queue);

// element can be NULL if the caller just wants to push an empty element (e.g. for signaling)
int glbqueue_push(glbqueue* queue, const void* element);

// out_element can be NULL if the caller just wants to discard the popped element
int glbqueue_pop(glbqueue* queue, void* out_element);

// out_element CAN'T BE NULL
int glbqueue_peek(glbqueue* queue, void* out_element);

static inline int glbqueue_is_empty(glbqueue* q) { return q->length == 0; }
static inline int glbqueue_is_full(glbqueue* q) { return q->length == q->capacity; }
static inline uint32_t glbqueue_size(glbqueue* q) { return q->length; }

#endif