// SPDX-License-Identifier: GPL-2.0
/*
 * Generate lookup table for the table-driven CRC64 calculation.
 *
 * gen_crc64table is executed in kernel build time and generates
 * lib/crc64table.h. This header is included by lib/crc64.c for
 * the table-driven CRC64 calculation.
 *
 * See lib/crc64.c for more information about which specification
 * and polynomial arithmetic that gen_crc64table.c follows to
 * generate the lookup table.
 *
 * Copyright 2018 SUSE Linux.
 *   Author: Coly Li <colyli@suse.de>
 */
#include <inttypes.h>
#include <stdio.h>

#define CRC64_ECMA182_POLY 0x42F0E1EBA9EA3693ULL
#define CRC64_NVME_POLY 0x9A6C9329AC4BC9B5ULL

static uint64_t crc64_table[256] = {0};
static uint64_t crc64_nvme_table[256] = {0};

static void generate_reflected_crc64_table(uint64_t table[256], uint64_t poly)
{
	uint64_t i, j, c, crc;

	for (i = 0; i < 256; i++) {
		crc = 0ULL;
		c = i;

		for (j = 0; j < 8; j++) {
			if ((crc ^ (c >> j)) & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc >>= 1;
		}
		table[i] = crc;
	}
}

static void generate_crc64_table(uint64_t table[256], uint64_t poly)
{
	uint64_t i, j, c, crc;

	for (i = 0; i < 256; i++) {
		crc = 0;
		c = i << 56;

		for (j = 0; j < 8; j++) {
			if ((crc ^ c) & 0x8000000000000000ULL)
				crc = (crc << 1) ^ poly;
			else
				crc <<= 1;
			c <<= 1;
		}

		table[i] = crc;
	}
}

static void output_table(uint64_t table[256])
{
	int i;

	for (i = 0; i < 256; i++) {
		printf("\t0x%016" PRIx64 "ULL", table[i]);
		if (i & 0x1)
			printf(",\n");
		else
			printf(", ");
	}
	printf("};\n");
}

static void print_crc64_tables(void)
{
	printf("/* this file is generated - do not edit */\n\n");
	printf("#include <linux/types.h>\n");
	printf("#include <linux/cache.h>\n\n");
	printf("static const u64 ____cacheline_aligned crc64table[256] = {\n");
	output_table(crc64_table);

	printf("\nstatic const u64 ____cacheline_aligned crc64nvmetable[256] = {\n");
	output_table(crc64_nvme_table);
}

int main(int argc, char *argv[])
{
	generate_crc64_table(crc64_table, CRC64_ECMA182_POLY);
	generate_reflected_crc64_table(crc64_nvme_table, CRC64_NVME_POLY);
	print_crc64_tables();
	return 0;
}
