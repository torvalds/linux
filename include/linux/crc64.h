/* SPDX-License-Identifier: GPL-2.0 */
/*
 * See lib/crc64.c for the related specification and polynomial arithmetic.
 */
#ifndef _LINUX_CRC64_H
#define _LINUX_CRC64_H

#include <linux/types.h>

#define CRC64_ROCKSOFT_STRING "crc64-rocksoft"

u64 __pure crc64_be(u64 crc, const void *p, size_t len);
u64 __pure crc64_rocksoft_generic(u64 crc, const void *p, size_t len);

/**
 * crc64_rocksoft_update - Calculate bitwise Rocksoft CRC64
 * @crc: seed value for computation. 0 for a new CRC calculation, or the
 *	 previous crc64 value if computing incrementally.
 * @p: pointer to buffer over which CRC64 is run
 * @len: length of buffer @p
 */
static inline u64 crc64_rocksoft_update(u64 crc, const u8 *p, size_t len)
{
	return crc64_rocksoft_generic(crc, p, len);
}

#endif /* _LINUX_CRC64_H */
