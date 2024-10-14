// SPDX-License-Identifier: 0BSD

/*
 * CRC32 using the polynomial from IEEE-802.3
 *
 * Authors: Lasse Collin <lasse.collin@tukaani.org>
 *          Igor Pavlov <https://7-zip.org/>
 */

/*
 * This is not the fastest implementation, but it is pretty compact.
 * The fastest versions of xz_crc32() on modern CPUs without hardware
 * accelerated CRC instruction are 3-5 times as fast as this version,
 * but they are bigger and use more memory for the lookup table.
 */

#include "xz_private.h"

/*
 * STATIC_RW_DATA is used in the pre-boot environment on some architectures.
 * See <linux/decompress/mm.h> for details.
 */
#ifndef STATIC_RW_DATA
#	define STATIC_RW_DATA static
#endif

STATIC_RW_DATA uint32_t xz_crc32_table[256];

void xz_crc32_init(void)
{
	const uint32_t poly = 0xEDB88320;

	uint32_t i;
	uint32_t j;
	uint32_t r;

	for (i = 0; i < 256; ++i) {
		r = i;
		for (j = 0; j < 8; ++j)
			r = (r >> 1) ^ (poly & ~((r & 1) - 1));

		xz_crc32_table[i] = r;
	}

	return;
}

uint32_t xz_crc32(const uint8_t *buf, size_t size, uint32_t crc)
{
	crc = ~crc;

	while (size != 0) {
		crc = xz_crc32_table[*buf++ ^ (crc & 0xFF)] ^ (crc >> 8);
		--size;
	}

	return ~crc;
}
