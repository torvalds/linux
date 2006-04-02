#ifndef _ASM_POWERPC_TYPES_H
#define _ASM_POWERPC_TYPES_H

#ifndef __ASSEMBLY__

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __powerpc64__
typedef unsigned int umode_t;
#else
typedef unsigned short umode_t;
#endif

/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#ifdef __powerpc64__
typedef __signed__ long __s64;
typedef unsigned long __u64;
#else
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif
#endif /* __powerpc64__ */

typedef struct {
	__u32 u[4];
} __attribute((aligned(16))) __vector128;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __powerpc64__
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif

#ifndef __ASSEMBLY__

#include <linux/config.h>

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

#ifdef __powerpc64__
typedef signed long s64;
typedef unsigned long u64;
#else
typedef signed long long s64;
typedef unsigned long long u64;
#endif

typedef __vector128 vector128;

#ifdef __powerpc64__
typedef u64 dma_addr_t;
#else
typedef u32 dma_addr_t;
#endif
typedef u64 dma64_addr_t;

typedef struct {
	unsigned long entry;
	unsigned long toc;
	unsigned long env;
} func_descr_t;

#ifdef CONFIG_LBD
typedef u64 sector_t;
#define HAVE_SECTOR_T
#endif

#ifdef CONFIG_LSF
typedef u64 blkcnt_t;
#define HAVE_BLKCNT_T
#endif

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_TYPES_H */
