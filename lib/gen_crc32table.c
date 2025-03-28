// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "../include/linux/crc32poly.h"
#include "../include/generated/autoconf.h"
#include <inttypes.h>

static uint32_t crc32table_le[256];
static uint32_t crc32table_be[256];
static uint32_t crc32ctable_le[256];

/**
 * crc32init_le() - allocate and initialize LE table data
 *
 * crc is the crc of the byte i; other entries are filled in based on the
 * fact that crctable[i^j] = crctable[i] ^ crctable[j].
 *
 */
static void crc32init_le_generic(const uint32_t polynomial, uint32_t tab[256])
{
	unsigned i, j;
	uint32_t crc = 1;

	tab[0] = 0;

	for (i = 128; i; i >>= 1) {
		crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
		for (j = 0; j < 256; j += 2 * i)
			tab[i + j] = crc ^ tab[j];
	}
}

static void crc32init_le(void)
{
	crc32init_le_generic(CRC32_POLY_LE, crc32table_le);
}

static void crc32cinit_le(void)
{
	crc32init_le_generic(CRC32C_POLY_LE, crc32ctable_le);
}

/**
 * crc32init_be() - allocate and initialize BE table data
 */
static void crc32init_be(void)
{
	unsigned i, j;
	uint32_t crc = 0x80000000;

	crc32table_be[0] = 0;

	for (i = 1; i < 256; i <<= 1) {
		crc = (crc << 1) ^ ((crc & 0x80000000) ? CRC32_POLY_BE : 0);
		for (j = 0; j < i; j++)
			crc32table_be[i + j] = crc ^ crc32table_be[j];
	}
}

static void output_table(const uint32_t table[256])
{
	int i;

	for (i = 0; i < 256; i += 4) {
		printf("\t0x%08x, 0x%08x, 0x%08x, 0x%08x,\n",
		       table[i], table[i + 1], table[i + 2], table[i + 3]);
	}
}

int main(int argc, char** argv)
{
	printf("/* this file is generated - do not edit */\n\n");

	crc32init_le();
	printf("static const u32 ____cacheline_aligned crc32table_le[256] = {\n");
	output_table(crc32table_le);
	printf("};\n");

	crc32init_be();
	printf("static const u32 ____cacheline_aligned crc32table_be[256] = {\n");
	output_table(crc32table_be);
	printf("};\n");

	crc32cinit_le();
	printf("static const u32 ____cacheline_aligned crc32ctable_le[256] = {\n");
	output_table(crc32ctable_le);
	printf("};\n");

	return 0;
}
