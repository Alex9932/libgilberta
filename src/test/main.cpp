#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "module.h"

struct memblock {
	size_t len;
	void*  ptr;
};

static std::vector<memblock> ptrs;
static size_t memalloc = 0;
static size_t memfree = 0;

static void* MALLOC(size_t len) {
	void* p = malloc(len);
	if (p) {
		memblock b = {};
		b.len = len;
		b.ptr = p;
		ptrs.push_back(b);
		memalloc += len;
	}
	return p;
}

static void FREE(void* ptr) {
	if (!ptr) {
		printf("[ALLOCATOR] Warning: free NULL pointer! Skipping...\n");
		return;
	}
	auto it = ptrs.begin();
	for (; it != ptrs.end(); it++) {
		memblock b = *it;
		if (b.ptr == ptr) {
			memfree += b.len;
			*it = std::move(ptrs.back());
			ptrs.pop_back();
			free(ptr);
			return;
		}
	}
	printf("[ALLOCATOR] Double / not allocated free detected! At: %p\n", ptr);
}

int main(int argc, char** argv) {
#if GLB_GENERATE_CRC16_TABLE
	static uint16_t crc16_table[256];
	printf("static uint16_t crc16_table[256] = {");
	for (int i = 0; i < 256; i++) {
		uint16_t crc = i;
		for (int j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xA001; // 0xA001 = reversed 0x8005
			else
				crc >>= 1;
		}
		crc16_table[i] = crc;
		printf(" 0x%.4x,", crc);
		if(i % 8 == 7) printf("\n");
	}
	printf(" };\n");
	return 0;
#else

	Allocator alloc = {0};
	alloc.mallocptr = MALLOC;
	alloc.freeptr   = FREE;
	int ret = ModuleMain(argc, argv, &alloc);

	printf("~ ~ ~ SUMMARY ~ ~ ~\n");
	printf("Memory allocated/free: %zu / %zu, leaked: %zu\n", memalloc, memfree, memalloc - memfree);
	for (auto it = ptrs.begin(); it != ptrs.end(); it++) {
		printf(" +-> %zu (%p)\n", (*it).len, (*it).ptr);
	}

	return ret;
#endif
}