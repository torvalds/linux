/* Public domain. */

#ifndef _ASM_BITSPERLONG_H
#define _ASM_BITSPERLONG_H

#ifdef __LP64__
#define BITS_PER_LONG		64
#else
#define BITS_PER_LONG		32
#endif
#define BITS_PER_LONG_LONG	64

#endif
