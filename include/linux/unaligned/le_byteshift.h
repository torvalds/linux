/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNALIGNED_LE_BYTESHIFT_H
#define _LINUX_UNALIGNED_LE_BYTESHIFT_H

#include <linux/types.h>

static inline u16 __get_unaligned_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline u32 __get_unaligned_le32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static inline u64 __get_unaligned_le64(const u8 *p)
{
	return (u64)__get_unaligned_le32(p + 4) << 32 |
	       __get_unaligned_le32(p);
}

static inline void __put_unaligned_le16(u16 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
}

static inline void __put_unaligned_le32(u32 val, u8 *p)
{
	__put_unaligned_le16(val >> 16, p + 2);
	__put_unaligned_le16(val, p);
}

static inline void __put_unaligned_le64(u64 val, u8 *p)
{
	__put_unaligned_le32(val >> 32, p + 4);
	__put_unaligned_le32(val, p);
}

static inline u16 get_unaligned_le16(const void *p)
{
	return __get_unaligned_le16((const u8 *)p);
}

static inline u32 get_unaligned_le32(const void *p)
{
	return __get_unaligned_le32((const u8 *)p);
}

static inline u64 get_unaligned_le64(const void *p)
{
	return __get_unaligned_le64((const u8 *)p);
}

static inline void put_unaligned_le16(u16 val, void *p)
{
	__put_unaligned_le16(val, p);
}

static inline void put_unaligned_le32(u32 val, void *p)
{
	__put_unaligned_le32(val, p);
}

static inline void put_unaligned_le64(u64 val, void *p)
{
	__put_unaligned_le64(val, p);
}

#endif /* _LINUX_UNALIGNED_LE_BYTESHIFT_H */
