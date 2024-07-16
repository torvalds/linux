/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LE_BYTESHIFT_H
#define _TOOLS_LE_BYTESHIFT_H

#include <stdint.h>

static inline uint16_t __get_unaligned_le16(const uint8_t *p)
{
	return p[0] | p[1] << 8;
}

static inline uint32_t __get_unaligned_le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static inline uint64_t __get_unaligned_le64(const uint8_t *p)
{
	return (uint64_t)__get_unaligned_le32(p + 4) << 32 |
	       __get_unaligned_le32(p);
}

static inline void __put_unaligned_le16(uint16_t val, uint8_t *p)
{
	*p++ = val;
	*p++ = val >> 8;
}

static inline void __put_unaligned_le32(uint32_t val, uint8_t *p)
{
	__put_unaligned_le16(val >> 16, p + 2);
	__put_unaligned_le16(val, p);
}

static inline void __put_unaligned_le64(uint64_t val, uint8_t *p)
{
	__put_unaligned_le32(val >> 32, p + 4);
	__put_unaligned_le32(val, p);
}

static inline uint16_t get_unaligned_le16(const void *p)
{
	return __get_unaligned_le16((const uint8_t *)p);
}

static inline uint32_t get_unaligned_le32(const void *p)
{
	return __get_unaligned_le32((const uint8_t *)p);
}

static inline uint64_t get_unaligned_le64(const void *p)
{
	return __get_unaligned_le64((const uint8_t *)p);
}

static inline void put_unaligned_le16(uint16_t val, void *p)
{
	__put_unaligned_le16(val, p);
}

static inline void put_unaligned_le32(uint32_t val, void *p)
{
	__put_unaligned_le32(val, p);
}

static inline void put_unaligned_le64(uint64_t val, void *p)
{
	__put_unaligned_le64(val, p);
}

#endif /* _TOOLS_LE_BYTESHIFT_H */
