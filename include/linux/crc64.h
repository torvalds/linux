/* SPDX-License-Identifier: GPL-2.0 */
/*
 * See lib/crc64.c for the related specification and polynomial arithmetic.
 */
#ifndef _LINUX_CRC64_H
#define _LINUX_CRC64_H

#include <linux/types.h>

u64 __pure crc64_be(u64 crc, const void *p, size_t len);
u64 __pure crc64_nvme_generic(u64 crc, const void *p, size_t len);

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
static inline u64 crc64_nvme(u64 crc, const u8 *p, size_t len)
{
	return crc64_nvme_generic(crc, p, len);
}

#endif /* _LINUX_CRC64_H */
