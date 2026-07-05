/**
 * @file queue.h
 * @brief Fixed-size circular queue.
 *
 * Implements a FIFO queue for fixed-size elements.
 * Used for event queues and packet send queues.
 *
 * @note Thread safety is not guaranteed — calling code must synchronize access.
 */

#ifndef GILBERTA_QUEUE_H
#define GILBERTA_QUEUE_H

#include <stdint.h>
#include "libgilberta.h"

/* ======================== */
/*  Queue structure         */
/* ======================== */

/**
 * @struct glbqueue
 * @brief Circular queue.
 *
 * Stores fixed-size elements in a contiguous buffer (pool).
 * Supports push/pop/peek operations.
 *
 * @warning Not thread-safe.
 * @see glbqueue_init() @see glbqueue_push()
 */
typedef struct glbqueue {
	glbctx_t* ctx;          /**< Parent context (for allocator) */
	void*     pool;         /**< Element storage buffer */
	uint32_t  head;         /**< Index of the first element (queue head) */
	uint32_t  length;       /**< Current number of elements in queue */
	uint32_t  capacity;     /**< Maximum capacity (in elements) */
	uint32_t  element_size; /**< Size of a single element in bytes */
} glbqueue;

/* ======================== */
/*  Queue functions         */
/* ======================== */

/**
 * @brief Create and initialize a queue.
 *
 * Allocates memory for the buffer (capacity * element_size bytes) and initializes the structure.
 *
 * @param ctx          Context (for allocator, not NULL).
 * @param element_size Size of a single element in bytes (> 0).
 * @param capacity     Maximum number of elements (> 0).
 * @return Pointer to the created queue or NULL on error.
 * @retval NULL if ctx == NULL, element_size == 0, capacity == 0, or failed to allocate memory.
 *
 * @see glbqueue_free()
 */
glbqueue* glbqueue_init(glbctx_t* ctx, uint32_t element_size, uint32_t capacity);

/**
 * @brief Free the queue.
 *
 * Frees the buffer and the queue structure itself.
 *
 * @param queue Queue to free (can be NULL).
 *
 * @warning Do not use the queue after calling this function.
 * @see glbqueue_init()
 */
void glbqueue_free(glbqueue* queue);

/**
 * @brief Add an element to the end of the queue.
 *
 * Copies element_size bytes from element into the queue buffer.
 *
 * @param queue   Queue (not NULL).
 * @param element Pointer to data (can be NULL to add an "empty" element).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if queue == NULL.
 * @retval GLB_ERROR_QUEUE_FULL if queue is full.
 *
 * @note If element == NULL, an element filled with zeros is added to the queue.
 * @see glbqueue_pop() @see glbqueue_peek()
 */
int glbqueue_push(glbqueue* queue, const void* element);

/**
 * @brief Extract an element from the front of the queue.
 *
 * Copies element_size bytes from the queue head into out_element and removes the element.
 *
 * @param queue       Queue (not NULL).
 * @param out_element [out] Buffer for data (can be NULL for simple removal).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if queue == NULL.
 * @retval GLB_ERROR_QUEUE_EMPTY if queue is empty.
 *
 * @note If out_element == NULL, the element is simply removed without copying.
 * @see glbqueue_push() @see glbqueue_peek()
 */
int glbqueue_pop(glbqueue* queue, void* out_element);

/**
 * @brief Read an element from the front of the queue without removing it.
 *
 * Copies element_size bytes from the queue head into out_element, but does not remove the element.
 *
 * @param queue       Queue (not NULL).
 * @param out_element [out] Buffer for data (MUST NOT be NULL).
 * @return GLB_SUCCESS on success, error code otherwise.
 * @retval GLB_ERROR_INVALID_ARGUMENT if queue == NULL or out_element == NULL.
 * @retval GLB_ERROR_QUEUE_EMPTY if queue is empty.
 *
 * @warning Unlike glbqueue_pop(), out_element MUST be non-NULL.
 * @see glbqueue_pop()
 */
int glbqueue_peek(glbqueue* queue, void* out_element);

/**
 * @brief Check if the queue is empty.
 *
 * @param q Queue (not NULL).
 * @return Non-zero if queue is empty, 0 if there are elements.
 *
 * @note This is an inline function for fast access.
 * @see glbqueue_is_full() @see glbqueue_size()
 */
static inline int glbqueue_is_empty(glbqueue* q) { return q->length == 0; }

/**
 * @brief Check if the queue is full.
 *
 * @param q Queue (not NULL).
 * @return Non-zero if queue is full, 0 if there is space.
 *
 * @note This is an inline function for fast access.
 * @see glbqueue_is_empty() @see glbqueue_size()
 */
static inline int glbqueue_is_full(glbqueue* q) { return q->length == q->capacity; }

/**
 * @brief Get the current number of elements in the queue.
 *
 * @param q Queue (not NULL).
 * @return Number of elements (0..capacity).
 *
 * @note This is an inline function for fast access.
 * @see glbqueue_is_empty() @see glbqueue_is_full()
 */
static inline uint32_t glbqueue_size(glbqueue* q) { return q->length; }

#endif