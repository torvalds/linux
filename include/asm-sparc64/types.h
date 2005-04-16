/* $Id: types.h,v 1.4 2001/10/09 02:24:35 davem Exp $ */
#ifndef _SPARC64_TYPES_H
#define _SPARC64_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

/*
 * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space.
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

typedef __signed__ long __s64;
typedef unsigned long __u64;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__

#define BITS_PER_LONG 64

#ifndef __ASSEMBLY__

typedef __signed__ char s8;
typedef unsigned char u8;

typedef __signed__ short s16;
typedef unsigned short u16;

typedef __signed__ int s32;
typedef unsigned int u32;

typedef __signed__ long s64;
typedef unsigned long u64;

/* Dma addresses come in generic and 64-bit flavours.  */

typedef u32 dma_addr_t;
typedef u64 dma64_addr_t;

typedef unsigned short kmem_bufctl_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_TYPES_H) */
