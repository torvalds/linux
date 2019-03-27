/*
 * CRC64 using the polynomial from ECMA-182
 *
 * This file is similar to xz_crc32.c. See the comments there.
 *
 * Authors: Lasse Collin <lasse.collin@tukaani.org>
 *          Igor Pavlov <http://7-zip.org/>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#include "xz_private.h"

#ifndef STATIC_RW_DATA
#	define STATIC_RW_DATA static
#endif

STATIC_RW_DATA uint64_t xz_crc64_table[256];

XZ_EXTERN void xz_crc64_init(void)
{
	const uint64_t poly = 0xC96C5795D7870F42;

	uint32_t i;
	uint32_t j;
	uint64_t r;

	for (i = 0; i < 256; ++i) {
		r = i;
		for (j = 0; j < 8; ++j)
			r = (r >> 1) ^ (poly & ~((r & 1) - 1));

		xz_crc64_table[i] = r;
	}

	return;
}

XZ_EXTERN uint64_t xz_crc64(const uint8_t *buf, size_t size, uint64_t crc)
{
	crc = ~crc;

	while (size != 0) {
		crc = xz_crc64_table[*buf++ ^ (crc & 0xFF)] ^ (crc >> 8);
		--size;
	}

	return ~crc;
}
