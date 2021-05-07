/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNALIGNED_LE_STRUCT_H
#define _LINUX_UNALIGNED_LE_STRUCT_H

#include <linux/unaligned/packed_struct.h>

static inline u16 get_unaligned_le16(const void *p)
{
	return __get_unaligned_cpu16((const u8 *)p);
}

static inline u32 get_unaligned_le32(const void *p)
{
	return __get_unaligned_cpu32((const u8 *)p);
}

static inline u64 get_unaligned_le64(const void *p)
{
	return __get_unaligned_cpu64((const u8 *)p);
}

static inline void put_unaligned_le16(u16 val, void *p)
{
	__put_unaligned_cpu16(val, p);
}

static inline void put_unaligned_le32(u32 val, void *p)
{
	__put_unaligned_cpu32(val, p);
}

static inline void put_unaligned_le64(u64 val, void *p)
{
	__put_unaligned_cpu64(val, p);
}

static inline u16 get_unaligned_be16(const void *p)
{
	return swab16(__get_unaligned_cpu16((const u8 *)p));
}

static inline u32 get_unaligned_be32(const void *p)
{
	return swab32(__get_unaligned_cpu32((const u8 *)p));
}

static inline u64 get_unaligned_be64(const void *p)
{
	return swab64(__get_unaligned_cpu64((const u8 *)p));
}

static inline void put_unaligned_be16(u16 val, void *p)
{
	__put_unaligned_cpu16(swab16(val), p);
}

static inline void put_unaligned_be32(u32 val, void *p)
{
	__put_unaligned_cpu32(swab32(val), p);
}

static inline void put_unaligned_be64(u64 val, void *p)
{
	__put_unaligned_cpu64(swab64(val), p);
}

#endif /* _LINUX_UNALIGNED_LE_STRUCT_H */
