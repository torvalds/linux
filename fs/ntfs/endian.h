/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * endian.h - Defines for endianness handling in NTFS Linux kernel driver.
 *	      Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_ENDIAN_H
#define _LINUX_NTFS_ENDIAN_H

#include <asm/byteorder.h>
#include "types.h"

/*
 * Signed endianness conversion functions.
 */

static inline s16 sle16_to_cpu(sle16 x)
{
	return le16_to_cpu((__force le16)x);
}

static inline s32 sle32_to_cpu(sle32 x)
{
	return le32_to_cpu((__force le32)x);
}

static inline s64 sle64_to_cpu(sle64 x)
{
	return le64_to_cpu((__force le64)x);
}

static inline s16 sle16_to_cpup(sle16 *x)
{
	return le16_to_cpu(*(__force le16*)x);
}

static inline s32 sle32_to_cpup(sle32 *x)
{
	return le32_to_cpu(*(__force le32*)x);
}

static inline s64 sle64_to_cpup(sle64 *x)
{
	return le64_to_cpu(*(__force le64*)x);
}

static inline sle16 cpu_to_sle16(s16 x)
{
	return (__force sle16)cpu_to_le16(x);
}

static inline sle32 cpu_to_sle32(s32 x)
{
	return (__force sle32)cpu_to_le32(x);
}

static inline sle64 cpu_to_sle64(s64 x)
{
	return (__force sle64)cpu_to_le64(x);
}

static inline sle16 cpu_to_sle16p(s16 *x)
{
	return (__force sle16)cpu_to_le16(*x);
}

static inline sle32 cpu_to_sle32p(s32 *x)
{
	return (__force sle32)cpu_to_le32(*x);
}

static inline sle64 cpu_to_sle64p(s64 *x)
{
	return (__force sle64)cpu_to_le64(*x);
}

#endif /* _LINUX_NTFS_ENDIAN_H */
