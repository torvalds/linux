/*
 * Aug 8, 2011 Bob Pearson with help from Joakim Tjernlund and George Spelvin
 * cleaned up code to current version of sparse and added the slicing-by-8
 * algorithm to the closely similar existing slicing-by-4 algorithm.
 *
 * Oct 15, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Nicer crc32 functions/docs submitted by linux@horizon.com.  Thanks!
 * Code was from the public domain, copyright abandoned.  Code was
 * subsequently included in the kernel, thus was re-licensed under the
 * GNU GPL v2.
 *
 * Oct 12, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Same crc32 function was used in 5 other places in the kernel.
 * I made one version, and deleted the others.
 * There are various incantations of crc32().  Some use a seed of 0 or ~0.
 * Some xor at the end with ~0.  The generic crc32() function takes
 * seed as an argument, and doesn't xor at the end.  Then individual
 * users can do whatever they need.
 *   drivers/net/smc9194.c uses seed ~0, doesn't xor with ~0.
 *   fs/jffs2 uses seed 0, doesn't xor with ~0.
 *   fs/partitions/efi.c uses seed ~0, xor's with ~0.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

/* see: Documentation/staging/crc32.rst for a description of algorithms */

#include <linux/crc32.h>
#include <linux/crc32poly.h>
#include <linux/module.h>
#include <linux/types.h>

#include "crc32table.h"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@dell.com>");
MODULE_DESCRIPTION("Various CRC32 calculations");
MODULE_LICENSE("GPL");

u32 crc32_le_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32table_le[(crc & 255) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32_le_base);

u32 crc32c_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32ctable_le[(crc & 255) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32c_base);

/*
 * This multiplies the polynomials x and y modulo the given modulus.
 * This follows the "little-endian" CRC convention that the lsbit
 * represents the highest power of x, and the msbit represents x^0.
 */
static u32 gf2_multiply(u32 x, u32 y, u32 modulus)
{
	u32 product = x & 1 ? y : 0;
	int i;

	for (i = 0; i < 31; i++) {
		product = (product >> 1) ^ (product & 1 ? modulus : 0);
		x >>= 1;
		product ^= x & 1 ? y : 0;
	}

	return product;
}

/**
 * crc32_generic_shift - Append @len 0 bytes to crc, in logarithmic time
 * @crc: The original little-endian CRC (i.e. lsbit is x^31 coefficient)
 * @len: The number of bytes. @crc is multiplied by x^(8*@len)
 * @polynomial: The modulus used to reduce the result to 32 bits.
 *
 * It's possible to parallelize CRC computations by computing a CRC
 * over separate ranges of a buffer, then summing them.
 * This shifts the given CRC by 8*len bits (i.e. produces the same effect
 * as appending len bytes of zero to the data), in time proportional
 * to log(len).
 */
static u32 crc32_generic_shift(u32 crc, size_t len, u32 polynomial)
{
	u32 power = polynomial;	/* CRC of x^32 */
	int i;

	/* Shift up to 32 bits in the simple linear way */
	for (i = 0; i < 8 * (int)(len & 3); i++)
		crc = (crc >> 1) ^ (crc & 1 ? polynomial : 0);

	len >>= 2;
	if (!len)
		return crc;

	for (;;) {
		/* "power" is x^(2^i), modulo the polynomial */
		if (len & 1)
			crc = gf2_multiply(crc, power, polynomial);

		len >>= 1;
		if (!len)
			break;

		/* Square power, advancing to x^(2^(i+1)) */
		power = gf2_multiply(power, power, polynomial);
	}

	return crc;
}

u32 crc32_le_shift(u32 crc, size_t len)
{
	return crc32_generic_shift(crc, len, CRC32_POLY_LE);
}
EXPORT_SYMBOL(crc32_le_shift);

u32 crc32c_shift(u32 crc, size_t len)
{
	return crc32_generic_shift(crc, len, CRC32C_POLY_LE);
}
EXPORT_SYMBOL(crc32c_shift);

u32 crc32_be_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc << 8) ^ crc32table_be[(crc >> 24) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32_be_base);
