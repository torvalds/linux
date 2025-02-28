/*
 * crc32.h
 * See linux/lib/crc32.c for license and changes
 */
#ifndef _LINUX_CRC32_H
#define _LINUX_CRC32_H

#include <linux/types.h>
#include <linux/bitrev.h>

u32 __pure crc32_le_arch(u32 crc, const u8 *p, size_t len);
u32 __pure crc32_le_base(u32 crc, const u8 *p, size_t len);
u32 __pure crc32_be_arch(u32 crc, const u8 *p, size_t len);
u32 __pure crc32_be_base(u32 crc, const u8 *p, size_t len);
u32 __pure crc32c_le_arch(u32 crc, const u8 *p, size_t len);
u32 __pure crc32c_le_base(u32 crc, const u8 *p, size_t len);

static inline u32 __pure crc32_le(u32 crc, const u8 *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32_le_arch(crc, p, len);
	return crc32_le_base(crc, p, len);
}

static inline u32 __pure crc32_be(u32 crc, const u8 *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32_be_arch(crc, p, len);
	return crc32_be_base(crc, p, len);
}

/* TODO: leading underscores should be dropped once callers have been updated */
static inline u32 __pure __crc32c_le(u32 crc, const u8 *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32c_le_arch(crc, p, len);
	return crc32c_le_base(crc, p, len);
}

/*
 * crc32_optimizations() returns flags that indicate which CRC32 library
 * functions are using architecture-specific optimizations.  Unlike
 * IS_ENABLED(CONFIG_CRC32_ARCH) it takes into account the different CRC32
 * variants and also whether any needed CPU features are available at runtime.
 */
#define CRC32_LE_OPTIMIZATION	BIT(0) /* crc32_le() is optimized */
#define CRC32_BE_OPTIMIZATION	BIT(1) /* crc32_be() is optimized */
#define CRC32C_OPTIMIZATION	BIT(2) /* __crc32c_le() is optimized */
#if IS_ENABLED(CONFIG_CRC32_ARCH)
u32 crc32_optimizations(void);
#else
static inline u32 crc32_optimizations(void) { return 0; }
#endif

/**
 * crc32_le_combine - Combine two crc32 check values into one. For two
 * 		      sequences of bytes, seq1 and seq2 with lengths len1
 * 		      and len2, crc32_le() check values were calculated
 * 		      for each, crc1 and crc2.
 *
 * @crc1: crc32 of the first block
 * @crc2: crc32 of the second block
 * @len2: length of the second block
 *
 * Return: The crc32_le() check value of seq1 and seq2 concatenated,
 * 	   requiring only crc1, crc2, and len2. Note: If seq_full denotes
 * 	   the concatenated memory area of seq1 with seq2, and crc_full
 * 	   the crc32_le() value of seq_full, then crc_full ==
 * 	   crc32_le_combine(crc1, crc2, len2) when crc_full was seeded
 * 	   with the same initializer as crc1, and crc2 seed was 0. See
 * 	   also crc32_combine_test().
 */
u32 __attribute_const__ crc32_le_shift(u32 crc, size_t len);

static inline u32 crc32_le_combine(u32 crc1, u32 crc2, size_t len2)
{
	return crc32_le_shift(crc1, len2) ^ crc2;
}

/**
 * __crc32c_le_combine - Combine two crc32c check values into one. For two
 * 			 sequences of bytes, seq1 and seq2 with lengths len1
 * 			 and len2, __crc32c_le() check values were calculated
 * 			 for each, crc1 and crc2.
 *
 * @crc1: crc32c of the first block
 * @crc2: crc32c of the second block
 * @len2: length of the second block
 *
 * Return: The __crc32c_le() check value of seq1 and seq2 concatenated,
 * 	   requiring only crc1, crc2, and len2. Note: If seq_full denotes
 * 	   the concatenated memory area of seq1 with seq2, and crc_full
 * 	   the __crc32c_le() value of seq_full, then crc_full ==
 * 	   __crc32c_le_combine(crc1, crc2, len2) when crc_full was
 * 	   seeded with the same initializer as crc1, and crc2 seed
 * 	   was 0. See also crc32c_combine_test().
 */
u32 __attribute_const__ __crc32c_le_shift(u32 crc, size_t len);

static inline u32 __crc32c_le_combine(u32 crc1, u32 crc2, size_t len2)
{
	return __crc32c_le_shift(crc1, len2) ^ crc2;
}

#define crc32(seed, data, length)  crc32_le(seed, (unsigned char const *)(data), length)

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
