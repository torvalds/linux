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

/* see: Documentation/crc32.txt for a description of algorithms */

#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include "crc32defs.h"

#if CRC_LE_BITS > 8
# define tole(x) ((__force u32) cpu_to_le32(x))
#else
# define tole(x) (x)
#endif

#if CRC_BE_BITS > 8
# define tobe(x) ((__force u32) cpu_to_be32(x))
#else
# define tobe(x) (x)
#endif

#include "crc32table.h"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@dell.com>");
MODULE_DESCRIPTION("Various CRC32 calculations");
MODULE_LICENSE("GPL");

#if CRC_LE_BITS > 8 || CRC_BE_BITS > 8

/* implements slicing-by-4 or slicing-by-8 algorithm */
static inline u32 __pure
crc32_body(u32 crc, unsigned char const *buf, size_t len, const u32 (*tab)[256])
{
# ifdef __LITTLE_ENDIAN
#  define DO_CRC(x) crc = t0[(crc ^ (x)) & 255] ^ (crc >> 8)
#  define DO_CRC4 (t3[(q) & 255] ^ t2[(q >> 8) & 255] ^ \
		   t1[(q >> 16) & 255] ^ t0[(q >> 24) & 255])
#  define DO_CRC8 (t7[(q) & 255] ^ t6[(q >> 8) & 255] ^ \
		   t5[(q >> 16) & 255] ^ t4[(q >> 24) & 255])
# else
#  define DO_CRC(x) crc = t0[((crc >> 24) ^ (x)) & 255] ^ (crc << 8)
#  define DO_CRC4 (t0[(q) & 255] ^ t1[(q >> 8) & 255] ^ \
		   t2[(q >> 16) & 255] ^ t3[(q >> 24) & 255])
#  define DO_CRC8 (t4[(q) & 255] ^ t5[(q >> 8) & 255] ^ \
		   t6[(q >> 16) & 255] ^ t7[(q >> 24) & 255])
# endif
	const u32 *b;
	size_t    rem_len;
# ifdef CONFIG_X86
	size_t i;
# endif
	const u32 *t0=tab[0], *t1=tab[1], *t2=tab[2], *t3=tab[3];
# if CRC_LE_BITS != 32
	const u32 *t4 = tab[4], *t5 = tab[5], *t6 = tab[6], *t7 = tab[7];
# endif
	u32 q;

	/* Align it */
	if (unlikely((long)buf & 3 && len)) {
		do {
			DO_CRC(*buf++);
		} while ((--len) && ((long)buf)&3);
	}

# if CRC_LE_BITS == 32
	rem_len = len & 3;
	len = len >> 2;
# else
	rem_len = len & 7;
	len = len >> 3;
# endif

	b = (const u32 *)buf;
# ifdef CONFIG_X86
	--b;
	for (i = 0; i < len; i++) {
# else
	for (--b; len; --len) {
# endif
		q = crc ^ *++b; /* use pre increment for speed */
# if CRC_LE_BITS == 32
		crc = DO_CRC4;
# else
		crc = DO_CRC8;
		q = *++b;
		crc ^= DO_CRC4;
# endif
	}
	len = rem_len;
	/* And the last few bytes */
	if (len) {
		u8 *p = (u8 *)(b + 1) - 1;
# ifdef CONFIG_X86
		for (i = 0; i < len; i++)
			DO_CRC(*++p); /* use pre increment for speed */
# else
		do {
			DO_CRC(*++p); /* use pre increment for speed */
		} while (--len);
# endif
	}
	return crc;
#undef DO_CRC
#undef DO_CRC4
#undef DO_CRC8
}
#endif


/**
 * crc32_le_generic() - Calculate bitwise little-endian Ethernet AUTODIN II
 *			CRC32/CRC32C
 * @crc: seed value for computation.  ~0 for Ethernet, sometimes 0 for other
 *	 uses, or the previous crc32/crc32c value if computing incrementally.
 * @p: pointer to buffer over which CRC32/CRC32C is run
 * @len: length of buffer @p
 * @tab: little-endian Ethernet table
 * @polynomial: CRC32/CRC32c LE polynomial
 */
static inline u32 __pure crc32_le_generic(u32 crc, unsigned char const *p,
					  size_t len, const u32 (*tab)[256],
					  u32 polynomial)
{
#if CRC_LE_BITS == 1
	int i;
	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
	}
# elif CRC_LE_BITS == 2
	while (len--) {
		crc ^= *p++;
		crc = (crc >> 2) ^ tab[0][crc & 3];
		crc = (crc >> 2) ^ tab[0][crc & 3];
		crc = (crc >> 2) ^ tab[0][crc & 3];
		crc = (crc >> 2) ^ tab[0][crc & 3];
	}
# elif CRC_LE_BITS == 4
	while (len--) {
		crc ^= *p++;
		crc = (crc >> 4) ^ tab[0][crc & 15];
		crc = (crc >> 4) ^ tab[0][crc & 15];
	}
# elif CRC_LE_BITS == 8
	/* aka Sarwate algorithm */
	while (len--) {
		crc ^= *p++;
		crc = (crc >> 8) ^ tab[0][crc & 255];
	}
# else
	crc = (__force u32) __cpu_to_le32(crc);
	crc = crc32_body(crc, p, len, tab);
	crc = __le32_to_cpu((__force __le32)crc);
#endif
	return crc;
}

#if CRC_LE_BITS == 1
u32 __pure crc32_le(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_le_generic(crc, p, len, NULL, CRCPOLY_LE);
}
u32 __pure __crc32c_le(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_le_generic(crc, p, len, NULL, CRC32C_POLY_LE);
}
#else
u32 __pure crc32_le(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_le_generic(crc, p, len,
			(const u32 (*)[256])crc32table_le, CRCPOLY_LE);
}
u32 __pure __crc32c_le(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_le_generic(crc, p, len,
			(const u32 (*)[256])crc32ctable_le, CRC32C_POLY_LE);
}
#endif
EXPORT_SYMBOL(crc32_le);
EXPORT_SYMBOL(__crc32c_le);

/*
 * This multiplies the polynomials x and y modulo the given modulus.
 * This follows the "little-endian" CRC convention that the lsbit
 * represents the highest power of x, and the msbit represents x^0.
 */
static u32 __attribute_const__ gf2_multiply(u32 x, u32 y, u32 modulus)
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
 * crc32_generic_shift - Append len 0 bytes to crc, in logarithmic time
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
static u32 __attribute_const__ crc32_generic_shift(u32 crc, size_t len,
						   u32 polynomial)
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

u32 __attribute_const__ crc32_le_shift(u32 crc, size_t len)
{
	return crc32_generic_shift(crc, len, CRCPOLY_LE);
}

u32 __attribute_const__ __crc32c_le_shift(u32 crc, size_t len)
{
	return crc32_generic_shift(crc, len, CRC32C_POLY_LE);
}
EXPORT_SYMBOL(crc32_le_shift);
EXPORT_SYMBOL(__crc32c_le_shift);

/**
 * crc32_be_generic() - Calculate bitwise big-endian Ethernet AUTODIN II CRC32
 * @crc: seed value for computation.  ~0 for Ethernet, sometimes 0 for
 *	other uses, or the previous crc32 value if computing incrementally.
 * @p: pointer to buffer over which CRC32 is run
 * @len: length of buffer @p
 * @tab: big-endian Ethernet table
 * @polynomial: CRC32 BE polynomial
 */
static inline u32 __pure crc32_be_generic(u32 crc, unsigned char const *p,
					  size_t len, const u32 (*tab)[256],
					  u32 polynomial)
{
#if CRC_BE_BITS == 1
	int i;
	while (len--) {
		crc ^= *p++ << 24;
		for (i = 0; i < 8; i++)
			crc =
			    (crc << 1) ^ ((crc & 0x80000000) ? polynomial :
					  0);
	}
# elif CRC_BE_BITS == 2
	while (len--) {
		crc ^= *p++ << 24;
		crc = (crc << 2) ^ tab[0][crc >> 30];
		crc = (crc << 2) ^ tab[0][crc >> 30];
		crc = (crc << 2) ^ tab[0][crc >> 30];
		crc = (crc << 2) ^ tab[0][crc >> 30];
	}
# elif CRC_BE_BITS == 4
	while (len--) {
		crc ^= *p++ << 24;
		crc = (crc << 4) ^ tab[0][crc >> 28];
		crc = (crc << 4) ^ tab[0][crc >> 28];
	}
# elif CRC_BE_BITS == 8
	while (len--) {
		crc ^= *p++ << 24;
		crc = (crc << 8) ^ tab[0][crc >> 24];
	}
# else
	crc = (__force u32) __cpu_to_be32(crc);
	crc = crc32_body(crc, p, len, tab);
	crc = __be32_to_cpu((__force __be32)crc);
# endif
	return crc;
}

#if CRC_LE_BITS == 1
u32 __pure crc32_be(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_be_generic(crc, p, len, NULL, CRCPOLY_BE);
}
#else
u32 __pure crc32_be(u32 crc, unsigned char const *p, size_t len)
{
	return crc32_be_generic(crc, p, len,
			(const u32 (*)[256])crc32table_be, CRCPOLY_BE);
}
#endif
EXPORT_SYMBOL(crc32_be);
