#include "timer.h"
#include "libgilberta.h"

#ifdef GILBERTA_WINDOWS
#include <windows.h>
static LARGE_INTEGER freq = { 0 };
static uint64_t timer_get_current_ms() {
	if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); }
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (uint64_t)((now.QuadPart * 1000) / freq.QuadPart);
}
#else
#include <time.h>
static uint64_t timer_get_current_ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) + 1; // +1 to avoid 0 timestamp (0 means "not started")
}
#endif

void glbtime_start(glbtimestamp_t* ts, uint32_t interval_ms) {
	*ts = timer_get_current_ms() + interval_ms;
}

int glbtime_isexpired(glbtimestamp_t* ts) {
	if (*ts == 0) { return 0; }
	if (timer_get_current_ms() >= *ts) { return 1; }
	return 0;
}

uint32_t glbtime_getremaining(glbtimestamp_t* ts) {
	if (*ts == 0) { return 0; }
	uint64_t now = timer_get_current_ms();
	if (now >= *ts) return 0;
	return (uint32_t)(*ts - now);
}