/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_CRC32_H
#define _LINUX_CRC32_H

#include <linux/types.h>
#include <linux/bitrev.h>

/**
 * crc32_le() - Compute least-significant-bit-first IEEE CRC-32
 * @crc: Initial CRC value.  ~0 (recommended) or 0 for a new CRC computation, or
 *	 the previous CRC value if computing incrementally.
 * @p: Pointer to the data buffer
 * @len: Length of data in bytes
 *
 * This implements the CRC variant that is often known as the IEEE CRC-32, or
 * simply CRC-32, and is widely used in Ethernet and other applications:
 *
 * - Polynomial: x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 +
 *		 x^7 + x^5 + x^4 + x^2 + x^1 + x^0
 * - Bit order: Least-significant-bit-first
 * - Polynomial in integer form: 0xedb88320
 *
 * This does *not* invert the CRC at the beginning or end.  The caller is
 * expected to do that if it needs to.  Inverting at both ends is recommended.
 *
 * For new applications, prefer to use CRC-32C instead.  See crc32c().
 *
 * Context: Any context
 * Return: The new CRC value
 */
u32 crc32_le(u32 crc, const void *p, size_t len);

/* This is just an alias for crc32_le(). */
static inline u32 crc32(u32 crc, const void *p, size_t len)
{
	return crc32_le(crc, p, len);
}

/**
 * crc32_be() - Compute most-significant-bit-first IEEE CRC-32
 * @crc: Initial CRC value.  ~0 (recommended) or 0 for a new CRC computation, or
 *	 the previous CRC value if computing incrementally.
 * @p: Pointer to the data buffer
 * @len: Length of data in bytes
 *
 * crc32_be() is the same as crc32_le() except that crc32_be() computes the
 * *most-significant-bit-first* variant of the CRC.  I.e., within each byte, the
 * most significant bit is processed first (treated as highest order polynomial
 * coefficient).  The same bit order is also used for the CRC value itself:
 *
 * - Polynomial: x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 +
 *		 x^7 + x^5 + x^4 + x^2 + x^1 + x^0
 * - Bit order: Most-significant-bit-first
 * - Polynomial in integer form: 0x04c11db7
 *
 * Context: Any context
 * Return: The new CRC value
 */
u32 crc32_be(u32 crc, const void *p, size_t len);

/**
 * crc32c() - Compute CRC-32C
 * @crc: Initial CRC value.  ~0 (recommended) or 0 for a new CRC computation, or
 *	 the previous CRC value if computing incrementally.
 * @p: Pointer to the data buffer
 * @len: Length of data in bytes
 *
 * This implements CRC-32C, i.e. the Castagnoli CRC.  This is the recommended
 * CRC variant to use in new applications that want a 32-bit CRC.
 *
 * - Polynomial: x^32 + x^28 + x^27 + x^26 + x^25 + x^23 + x^22 + x^20 + x^19 +
 *		 x^18 + x^14 + x^13 + x^11 + x^10 + x^9 + x^8 + x^6 + x^0
 * - Bit order: Least-significant-bit-first
 * - Polynomial in integer form: 0x82f63b78
 *
 * This does *not* invert the CRC at the beginning or end.  The caller is
 * expected to do that if it needs to.  Inverting at both ends is recommended.
 *
 * Context: Any context
 * Return: The new CRC value
 */
u32 crc32c(u32 crc, const void *p, size_t len);

/*
 * crc32_optimizations() returns flags that indicate which CRC32 library
 * functions are using architecture-specific optimizations.  Unlike
 * IS_ENABLED(CONFIG_CRC32_ARCH) it takes into account the different CRC32
 * variants and also whether any needed CPU features are available at runtime.
 */
#define CRC32_LE_OPTIMIZATION	BIT(0) /* crc32_le() is optimized */
#define CRC32_BE_OPTIMIZATION	BIT(1) /* crc32_be() is optimized */
#define CRC32C_OPTIMIZATION	BIT(2) /* crc32c() is optimized */
#if IS_ENABLED(CONFIG_CRC32_ARCH)
u32 crc32_optimizations(void);
#else
static inline u32 crc32_optimizations(void) { return 0; }
#endif

/*
 * Helpers for hash table generation of ethernet nics:
 *
 * Ethernet sends the least significant bit of a byte first, thus crc32_le
 * is used. The output of crc32_le is bit reversed [most significant bit
 * is in bit nr 0], thus it must be reversed before use. Except for
 * nics that bit swap the result internally...
 */
#define ether_crc(length, data)    bitrev32(crc32_le(~0, data, length))
#define ether_crc_le(length, data) crc32_le(~0, data, length)

#endif /* _LINUX_CRC32_H */
