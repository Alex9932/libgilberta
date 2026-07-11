#include <stdio.h>
#include <stdlib.h>
#include "module.h"

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
	return ModuleMain(argc, argv);
#endif
}