/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_CRC32_H
#define _LINUX_CRC32_H

#include <linux/types.h>
#include <linux/bitrev.h>

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len);
u32 crc32_le_base(u32 crc, const u8 *p, size_t len);
u32 crc32_be_arch(u32 crc, const u8 *p, size_t len);
u32 crc32_be_base(u32 crc, const u8 *p, size_t len);
u32 crc32c_arch(u32 crc, const u8 *p, size_t len);
u32 crc32c_base(u32 crc, const u8 *p, size_t len);

static inline u32 crc32_le(u32 crc, const void *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32_le_arch(crc, p, len);
	return crc32_le_base(crc, p, len);
}

static inline u32 crc32_be(u32 crc, const void *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32_be_arch(crc, p, len);
	return crc32_be_base(crc, p, len);
}

static inline u32 crc32c(u32 crc, const void *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC32_ARCH))
		return crc32c_arch(crc, p, len);
	return crc32c_base(crc, p, len);
}

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
