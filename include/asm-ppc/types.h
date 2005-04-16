#ifndef _PPC_TYPES_H
#define _PPC_TYPES_H

#ifndef __ASSEMBLY__

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif

typedef struct {
	__u32 u[4];
} __vector128;

/*
 * XXX allowed outside of __KERNEL__ for now, until glibc gets
 * a proper set of asm headers of its own.  -- paulus
 */
typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#define BITS_PER_LONG 32

#ifndef __ASSEMBLY__

#include <linux/config.h>

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

typedef __vector128 vector128;

/* DMA addresses are 32-bits wide */
typedef u32 dma_addr_t;
typedef u64 dma64_addr_t;

#ifdef CONFIG_LBD
typedef u64 sector_t;
#define HAVE_SECTOR_T
#endif

typedef unsigned int kmem_bufctl_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif
