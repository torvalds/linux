/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002-2007 H. Peter Anvin - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * mktables.c
 *
 * Make RAID-6 tables.  This is a host user space program to be run at
 * compile time.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

static uint8_t gfmul(uint8_t a, uint8_t b)
{
	uint8_t v = 0;

	while (b) {
		if (b & 1)
			v ^= a;
		a = (a << 1) ^ (a & 0x80 ? 0x1d : 0);
		b >>= 1;
	}

	return v;
}

static uint8_t gfpow(uint8_t a, int b)
{
	uint8_t v = 1;

	b %= 255;
	if (b < 0)
		b += 255;

	while (b) {
		if (b & 1)
			v = gfmul(v, a);
		a = gfmul(a, a);
		b >>= 1;
	}

	return v;
}

int main(int argc, char *argv[])
{
	int i, j, k;
	uint8_t v;
	uint8_t exptbl[256], invtbl[256];

	printf("#include <linux/raid/pq.h>\n");
	printf("#include <linux/export.h>\n");

	/* Compute multiplication table */
	printf("\nconst u8  __attribute__((aligned(256)))\n"
		"raid6_gfmul[256][256] =\n"
		"{\n");
	for (i = 0; i < 256; i++) {
		printf("\t{\n");
		for (j = 0; j < 256; j += 8) {
			printf("\t\t");
			for (k = 0; k < 8; k++)
				printf("0x%02x,%c", gfmul(i, j + k),
				       (k == 7) ? '\n' : ' ');
		}
		printf("\t},\n");
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_gfmul);\n");
	printf("#endif\n");

	/* Compute vector multiplication table */
	printf("\nconst u8  __attribute__((aligned(256)))\n"
		"raid6_vgfmul[256][32] =\n"
		"{\n");
	for (i = 0; i < 256; i++) {
		printf("\t{\n");
		for (j = 0; j < 16; j += 8) {
			printf("\t\t");
			for (k = 0; k < 8; k++)
				printf("0x%02x,%c", gfmul(i, j + k),
				       (k == 7) ? '\n' : ' ');
		}
		for (j = 0; j < 16; j += 8) {
			printf("\t\t");
			for (k = 0; k < 8; k++)
				printf("0x%02x,%c", gfmul(i, (j + k) << 4),
				       (k == 7) ? '\n' : ' ');
		}
		printf("\t},\n");
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_vgfmul);\n");
	printf("#endif\n");

	/* Compute power-of-2 table (exponent) */
	v = 1;
	printf("\nconst u8 __attribute__((aligned(256)))\n"
	       "raid6_gfexp[256] =\n" "{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++) {
			exptbl[i + j] = v;
			printf("0x%02x,%c", v, (j == 7) ? '\n' : ' ');
			v = gfmul(v, 2);
			if (v == 1)
				v = 0;	/* For entry 255, not a real entry */
		}
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_gfexp);\n");
	printf("#endif\n");

	/* Compute log-of-2 table */
	printf("\nconst u8 __attribute__((aligned(256)))\n"
	       "raid6_gflog[256] =\n" "{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++) {
			v = 255;
			for (k = 0; k < 256; k++)
				if (exptbl[k] == (i + j)) {
					v = k;
					break;
				}
			printf("0x%02x,%c", v, (j == 7) ? '\n' : ' ');
		}
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_gflog);\n");
	printf("#endif\n");

	/* Compute inverse table x^-1 == x^254 */
	printf("\nconst u8 __attribute__((aligned(256)))\n"
	       "raid6_gfinv[256] =\n" "{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++) {
			invtbl[i + j] = v = gfpow(i + j, 254);
			printf("0x%02x,%c", v, (j == 7) ? '\n' : ' ');
		}
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_gfinv);\n");
	printf("#endif\n");

	/* Compute inv(2^x + 1) (exponent-xor-inverse) table */
	printf("\nconst u8 __attribute__((aligned(256)))\n"
	       "raid6_gfexi[256] =\n" "{\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t");
		for (j = 0; j < 8; j++)
			printf("0x%02x,%c", invtbl[exptbl[i + j] ^ 1],
			       (j == 7) ? '\n' : ' ');
	}
	printf("};\n");
	printf("#ifdef __KERNEL__\n");
	printf("EXPORT_SYMBOL(raid6_gfexi);\n");
	printf("#endif\n");

	return 0;
}
