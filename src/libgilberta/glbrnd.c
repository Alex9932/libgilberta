#include "glbrnd.h"
#include "libgilberta.h"

#if defined(GILBERTA_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

int glbrand_bytes(void* dst, size_t len) {
	if (!dst || len > 0xFFFFFFFFu) { return GLB_ERROR_INVALID_ARGUMENT; }
	if (len == 0) { return GLB_SUCCESS; }
	NTSTATUS st = BCryptGenRandom(NULL, (PUCHAR)dst, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	return (st >= 0) ? GLB_SUCCESS : GLB_ERROR_UNKNOWN;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include <stdlib.h>

int glbrand_bytes(void* dst, size_t len) {
	if (!dst) { return GLB_ERROR_INVALID_ARGUMENT; }
	arc4random_buf(dst, len);
	return GLB_SUCCESS;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif

static int glbrand_urandom(unsigned char* p, size_t left) {
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0) { return GLB_ERROR_UNKNOWN; }
	while (left > 0) {
		ssize_t n = read(fd, p, left);
		if (n < 0) {
			if (errno == EINTR) { continue; }
			close(fd);
			return GLB_ERROR_UNKNOWN;
		}
		if (n == 0) { close(fd); return GLB_ERROR_UNKNOWN; }
		p += n;
		left -= (size_t)n;
	}
	close(fd);
	return GLB_SUCCESS;
}

int glbrand_bytes(void* dst, size_t len) {
	if (!dst) { return GLB_ERROR_INVALID_ARGUMENT; }
	unsigned char* p = (unsigned char*)dst;
	size_t left = len;

#if defined(__linux__)
	while (left > 0) {
		ssize_t n = getrandom(p, left, 0);
		if (n < 0) {
			if (errno == EINTR) { continue; }
			if (errno == ENOSYS) { break; } /* Old kernel -> / dev / urandom */
			return GLB_ERROR_UNKNOWN;
		}
		p += n;
		left -= (size_t)n;
	}
	if (left == 0) { return GLB_SUCCESS; }
#endif

	return glbrand_urandom(p, left);
}

#endif

int glbrand_u32(uint32_t* out) {
	return glbrand_bytes(out, sizeof(*out));
}