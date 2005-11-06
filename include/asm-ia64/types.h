#ifndef _ASM_IA64_TYPES_H
#define _ASM_IA64_TYPES_H

/*
 * This file is never included by application software unless explicitly requested (e.g.,
 * via linux/types.h) in which case the application is Linux specific so (user-) name
 * space pollution is not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 *
 * Based on <asm-alpha/types.h>.
 *
 * Modified 1998-2000, 2002
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

#ifdef __ASSEMBLY__
# define __IA64_UL(x)		(x)
# define __IA64_UL_CONST(x)	x

# ifdef __KERNEL__
#  define BITS_PER_LONG 64
# endif

#else
# define __IA64_UL(x)		((unsigned long)(x))
# define __IA64_UL_CONST(x)	x##UL

typedef unsigned int umode_t;

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

typedef __signed__ long __s64;
typedef unsigned long __u64;

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
# ifdef __KERNEL__

typedef __s8 s8;
typedef __u8 u8;

typedef __s16 s16;
typedef __u16 u16;

typedef __s32 s32;
typedef __u32 u32;

typedef __s64 s64;
typedef __u64 u64;

#define BITS_PER_LONG 64

/* DMA addresses are 64-bits wide, in general.  */

typedef u64 dma_addr_t;

# endif /* __KERNEL__ */
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_TYPES_H */
