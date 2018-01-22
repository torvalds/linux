/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_BITS_PER_LONG
#define __ASM_GENERIC_BITS_PER_LONG

#include <uapi/asm-generic/bitsperlong.h>

#ifdef __SIZEOF_LONG__
#define BITS_PER_LONG (__CHAR_BIT__ * __SIZEOF_LONG__)
#else
#define BITS_PER_LONG __WORDSIZE
#endif

#if BITS_PER_LONG != __BITS_PER_LONG
#error Inconsistent word size. Check asm/bitsperlong.h
#endif

#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG 64
#endif

#endif /* __ASM_GENERIC_BITS_PER_LONG */
