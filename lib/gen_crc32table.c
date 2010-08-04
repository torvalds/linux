#include <stdio.h>
#include "crc32defs.h"
#include <inttypes.h>

#define ENTRIES_PER_LINE 4

#define LE_TABLE_SIZE (1 << CRC_LE_BITS)
#define BE_TABLE_SIZE (1 << CRC_BE_BITS)

static uint32_t crc32table_le[4][LE_TABLE_SIZE];
static uint32_t crc32table_be[4][BE_TABLE_SIZE];

/**
 * crc32init_le() - allocate and initialize LE table data
 *
 * crc is the crc of the byte i; other entries are filled in based on the
 * fact that crctable[i^j] = crctable[i] ^ crctable[j].
 *
 */
static void crc32init_le(void)
{
	unsigned i, j;
	uint32_t crc = 1;

	crc32table_le[0][0] = 0;

	for (i = 1 << (CRC_LE_BITS - 1); i; i >>= 1) {
		crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_LE : 0);
		for (j = 0; j < LE_TABLE_SIZE; j += 2 * i)
			crc32table_le[0][i + j] = crc ^ crc32table_le[0][j];
	}
	for (i = 0; i < LE_TABLE_SIZE; i++) {
		crc = crc32table_le[0][i];
		for (j = 1; j < 4; j++) {
			crc = crc32table_le[0][crc & 0xff] ^ (crc >> 8);
			crc32table_le[j][i] = crc;
		}
	}
}

/**
 * crc32init_be() - allocate and initialize BE table data
 */
static void crc32init_be(void)
{
	unsigned i, j;
	uint32_t crc = 0x80000000;

	crc32table_be[0][0] = 0;

	for (i = 1; i < BE_TABLE_SIZE; i <<= 1) {
		crc = (crc << 1) ^ ((crc & 0x80000000) ? CRCPOLY_BE : 0);
		for (j = 0; j < i; j++)
			crc32table_be[0][i + j] = crc ^ crc32table_be[0][j];
	}
	for (i = 0; i < BE_TABLE_SIZE; i++) {
		crc = crc32table_be[0][i];
		for (j = 1; j < 4; j++) {
			crc = crc32table_be[0][(crc >> 24) & 0xff] ^ (crc << 8);
			crc32table_be[j][i] = crc;
		}
	}
}

static void output_table(uint32_t table[4][256], int len, char *trans)
{
	int i, j;

	for (j = 0 ; j < 4; j++) {
		printf("{");
		for (i = 0; i < len - 1; i++) {
			if (i % ENTRIES_PER_LINE == 0)
				printf("\n");
			printf("%s(0x%8.8xL), ", trans, table[j][i]);
		}
		printf("%s(0x%8.8xL)},\n", trans, table[j][len - 1]);
	}
}

int main(int argc, char** argv)
{
	printf("/* this file is generated - do not edit */\n\n");

	if (CRC_LE_BITS > 1) {
		crc32init_le();
		printf("static const u32 crc32table_le[4][256] = {");
		output_table(crc32table_le, LE_TABLE_SIZE, "tole");
		printf("};\n");
	}

	if (CRC_BE_BITS > 1) {
		crc32init_be();
		printf("static const u32 crc32table_be[4][256] = {");
		output_table(crc32table_be, BE_TABLE_SIZE, "tobe");
		printf("};\n");
	}

	return 0;
}
