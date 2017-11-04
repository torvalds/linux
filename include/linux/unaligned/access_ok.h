/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNALIGNED_ACCESS_OK_H
#define _LINUX_UNALIGNED_ACCESS_OK_H

#include <linux/kernel.h>
#include <asm/byteorder.h>

static __always_inline u16 get_unaligned_le16(const void *p)
{
	return le16_to_cpup((__le16 *)p);
}

static __always_inline u32 get_unaligned_le32(const void *p)
{
	return le32_to_cpup((__le32 *)p);
}

static __always_inline u64 get_unaligned_le64(const void *p)
{
	return le64_to_cpup((__le64 *)p);
}

static __always_inline u16 get_unaligned_be16(const void *p)
{
	return be16_to_cpup((__be16 *)p);
}

static __always_inline u32 get_unaligned_be32(const void *p)
{
	return be32_to_cpup((__be32 *)p);
}

static __always_inline u64 get_unaligned_be64(const void *p)
{
	return be64_to_cpup((__be64 *)p);
}

static __always_inline void put_unaligned_le16(u16 val, void *p)
{
	*((__le16 *)p) = cpu_to_le16(val);
}

static __always_inline void put_unaligned_le32(u32 val, void *p)
{
	*((__le32 *)p) = cpu_to_le32(val);
}

static __always_inline void put_unaligned_le64(u64 val, void *p)
{
	*((__le64 *)p) = cpu_to_le64(val);
}

static __always_inline void put_unaligned_be16(u16 val, void *p)
{
	*((__be16 *)p) = cpu_to_be16(val);
}

static __always_inline void put_unaligned_be32(u32 val, void *p)
{
	*((__be32 *)p) = cpu_to_be32(val);
}

static __always_inline void put_unaligned_be64(u64 val, void *p)
{
	*((__be64 *)p) = cpu_to_be64(val);
}

#endif /* _LINUX_UNALIGNED_ACCESS_OK_H */
