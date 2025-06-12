// SPDX-License-Identifier: GPL-2.0-only
/*
 * crc4.c - simple crc-4 calculations.
 */

#include <linux/crc4.h>
#include <linux/export.h>
#include <linux/module.h>

static const uint8_t crc4_tab[] = {
	0x0, 0x7, 0xe, 0x9, 0xb, 0xc, 0x5, 0x2,
	0x1, 0x6, 0xf, 0x8, 0xa, 0xd, 0x4, 0x3,
};

/**
 * crc4 - calculate the 4-bit crc of a value.
 * @c:    starting crc4
 * @x:    value to checksum
 * @bits: number of bits in @x to checksum
 *
 * Returns the crc4 value of @x, using polynomial 0b10111.
 *
 * The @x value is treated as left-aligned, and bits above @bits are ignored
 * in the crc calculations.
 */
uint8_t crc4(uint8_t c, uint64_t x, int bits)
{
	int i;

	/* mask off anything above the top bit */
	x &= (1ull << bits) - 1;

	/* Align to 4-bits */
	bits = (bits + 3) & ~0x3;

	/* Calculate crc4 over four-bit nibbles, starting at the MSbit */
	for (i = bits - 4; i >= 0; i -= 4)
		c = crc4_tab[c ^ ((x >> i) & 0xf)];

	return c;
}
EXPORT_SYMBOL_GPL(crc4);

MODULE_DESCRIPTION("CRC4 calculations");
MODULE_LICENSE("GPL");
