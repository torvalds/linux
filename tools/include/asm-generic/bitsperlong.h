#ifndef __ASM_GENERIC_BITS_PER_LONG
#define __ASM_GENERIC_BITS_PER_LONG

#include <uapi/asm-generic/bitsperlong.h>

/*
 * In the kernel, where this file comes from, we can rely on CONFIG_64BIT,
 * here we have to make amends with what the various compilers provides us
 * to figure out if we're on a 64-bit machine...
 */
#ifdef __SIZEOF_LONG__
# if __SIZEOF_LONG__ == 8
#  define CONFIG_64BIT
# endif
#else
# ifdef __WORDSIZE
#  if __WORDSIZE == 64
#   define CONFIG_64BIT
#  endif
# else
#  error Failed to determine BITS_PER_LONG value
# endif
#endif

#ifdef CONFIG_64BIT
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif /* CONFIG_64BIT */

#if BITS_PER_LONG != __BITS_PER_LONG
#error Inconsistent word size. Check asm/bitsperlong.h
#endif

#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG 64
#endif

#endif /* __ASM_GENERIC_BITS_PER_LONG */
