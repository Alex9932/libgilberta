/**
 * @file glbio.h
 * @brief Random number generation.
 *
 * @note This header is not part of the public API.
 */
#ifndef LIBGILBERTA_RANDOM_H
#define LIBGILBERTA_RANDOM_H

#include <stddef.h>
#include <stdint.h>

int glbrand_bytes(void* dst, size_t len);
int glbrand_u32(uint32_t* out);

#endif