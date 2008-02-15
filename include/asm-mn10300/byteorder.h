/* MN10300 Byte-order primitive construction
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_BYTEORDER_H
#define _ASM_BYTEORDER_H

#include <asm/types.h>

#ifdef __GNUC__

static inline __attribute__((const))
__u32 ___arch__swab32(__u32 x)
{
	__u32 ret;
	asm("swap %1,%0" : "=r" (ret) : "r" (x));
	return ret;
}

static inline __attribute__((const))
__u16 ___arch__swab16(__u16 x)
{
	__u16 ret;
	asm("swaph %1,%0" : "=r" (ret) : "r" (x));
	return ret;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#endif /* __GNUC__ */

#include <linux/byteorder/little_endian.h>

#endif /* _ASM_BYTEORDER_H */
