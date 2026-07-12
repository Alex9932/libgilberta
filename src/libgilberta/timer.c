#include "timer.h"
#include "libgilberta.h"

#ifdef GILBERTA_WINDOWS
#include <windows.h>
static LARGE_INTEGER freq = { 0 };
#else
#include <time.h>
static uint64_t timer_get_current_ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) + 1; // +1 to avoid 0 timestamp (0 means "not started")
}
#endif

void glbtime_start(glbtimestamp_t* ts, uint32_t interval_ms) {
#ifdef GILBERTA_WINDOWS
	if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); }
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	((LARGE_INTEGER*)ts)->QuadPart = now.QuadPart + (LONGLONG)((interval_ms * freq.QuadPart) / 1000);
#else
	*ts = timer_get_current_ms() + interval_ms;
#endif
}

int glbtime_isexpired(glbtimestamp_t* ts) {
#ifdef GILBERTA_WINDOWS
	if (((LARGE_INTEGER*)ts)->QuadPart == 0) { return 0; }
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (now.QuadPart >= ((LARGE_INTEGER*)ts)->QuadPart) { return 1; }
#else
	if (*ts == 0) { return 0; }
	if (timer_get_current_ms() >= *ts) { return 1; }
#endif
	return 0;
}

uint32_t glbtime_getremaining(glbtimestamp_t* ts) {
#ifdef GILBERTA_WINDOWS
	if (((LARGE_INTEGER*)ts)->QuadPart == 0) { return 0; }
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (now.QuadPart >= ((LARGE_INTEGER*)ts)->QuadPart) { return 0; }
	LONGLONG diff_ticks = ((LARGE_INTEGER*)ts)->QuadPart - now.QuadPart;
	return (uint32_t)((diff_ticks * 1000.0) / freq.QuadPart);
#else
	if (*ts == 0) { return 0; }
	uint64_t now = timer_get_current_ms();
	if (now >= *ts) return 0;
	return (uint32_t)(*ts - now);
#endif
}