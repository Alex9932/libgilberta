#ifndef GILBERTA_TIMER_H
#define GILBERTA_TIMER_H

#include <stdint.h>

typedef uint64_t glbtimestamp_t; // uint64_t (posix) & LARGE_INTEGER (windows), both 64 bits

void glbtime_start(glbtimestamp_t* ts, uint32_t interval_ms);
int glbtime_isexpired(glbtimestamp_t* ts);
uint32_t glbtime_getremaining(glbtimestamp_t* ts);

#endif