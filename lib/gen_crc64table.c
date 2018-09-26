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

#include <linux/swab.h>

#define CRC64_ECMA182_POLY 0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256] = {0};

static void generate_crc64_table(void)
{
	uint64_t i, j, c, crc;

	for (i = 0; i < 256; i++) {
		crc = 0;
		c = i << 56;

		for (j = 0; j < 8; j++) {
			if ((crc ^ c) & 0x8000000000000000ULL)
				crc = (crc << 1) ^ CRC64_ECMA182_POLY;
			else
				crc <<= 1;
			c <<= 1;
		}

		crc64_table[i] = crc;
	}
}

static void print_crc64_table(void)
{
	int i;

	printf("/* this file is generated - do not edit */\n\n");
	printf("#include <linux/types.h>\n");
	printf("#include <linux/cache.h>\n\n");
	printf("static const u64 ____cacheline_aligned crc64table[256] = {\n");
	for (i = 0; i < 256; i++) {
		printf("\t0x%016" PRIx64 "ULL", crc64_table[i]);
		if (i & 0x1)
			printf(",\n");
		else
			printf(", ");
	}
	printf("};\n");
}

int main(int argc, char *argv[])
{
	generate_crc64_table();
	print_crc64_table();
	return 0;
}
