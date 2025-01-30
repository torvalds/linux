/* SPDX-License-Identifier: GPL-2.0 */
/*
 * See lib/crc64.c for the related specification and polynomial arithmetic.
 */
#ifndef _LINUX_CRC64_H
#define _LINUX_CRC64_H

#include <linux/types.h>

u64 crc64_be_arch(u64 crc, const u8 *p, size_t len);
u64 crc64_be_generic(u64 crc, const u8 *p, size_t len);
u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len);
u64 crc64_nvme_generic(u64 crc, const u8 *p, size_t len);

/**
 * crc64_be - Calculate bitwise big-endian ECMA-182 CRC64
 * @crc: seed value for computation. 0 or (u64)~0 for a new CRC calculation,
 *       or the previous crc64 value if computing incrementally.
 * @p: pointer to buffer over which CRC64 is run
 * @len: length of buffer @p
 */
static inline u64 crc64_be(u64 crc, const void *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC64_ARCH))
		return crc64_be_arch(crc, p, len);
	return crc64_be_generic(crc, p, len);
}

/**
 * crc64_nvme - Calculate CRC64-NVME
 * @crc: seed value for computation. 0 for a new CRC calculation, or the
 *	 previous crc64 value if computing incrementally.
 * @p: pointer to buffer over which CRC64 is run
 * @len: length of buffer @p
 *
 * This computes the CRC64 defined in the NVME NVM Command Set Specification,
 * *including the bitwise inversion at the beginning and end*.
 */
static inline u64 crc64_nvme(u64 crc, const void *p, size_t len)
{
	if (IS_ENABLED(CONFIG_CRC64_ARCH))
		return ~crc64_nvme_arch(~crc, p, len);
	return ~crc64_nvme_generic(~crc, p, len);
}

#endif /* _LINUX_CRC64_H */
