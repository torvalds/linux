/*
 * include/asm-xtensa/byteorder.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_BYTEORDER_H
#define _XTENSA_BYTEORDER_H

#include <asm/processor.h>
#include <asm/types.h>

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
    __u32 res;
    /* instruction sequence from Xtensa ISA release 2/2000 */
    __asm__("ssai     8           \n\t"
	    "srli     %0, %1, 16  \n\t"
	    "src      %0, %0, %1  \n\t"
	    "src      %0, %0, %0  \n\t"
	    "src      %0, %1, %0  \n"
	    : "=&a" (res)
	    : "a" (x)
	    );
    return res;
}

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 x)
{
    /* Given that 'short' values are signed (i.e., can be negative),
     * we cannot assume that the upper 16-bits of the register are
     * zero.  We are careful to mask values after shifting.
     */

    /* There exists an anomaly between xt-gcc and xt-xcc.  xt-gcc
     * inserts an extui instruction after putting this function inline
     * to ensure that it uses only the least-significant 16 bits of
     * the result.  xt-xcc doesn't use an extui, but assumes the
     * __asm__ macro follows convention that the upper 16 bits of an
     * 'unsigned short' result are still zero.  This macro doesn't
     * follow convention; indeed, it leaves garbage in the upport 16
     * bits of the register.

     * Declaring the temporary variables 'res' and 'tmp' to be 32-bit
     * types while the return type of the function is a 16-bit type
     * forces both compilers to insert exactly one extui instruction
     * (or equivalent) to mask off the upper 16 bits. */

    __u32 res;
    __u32 tmp;

    __asm__("extui    %1, %2, 8, 8\n\t"
	    "slli     %0, %2, 8   \n\t"
	    "or       %0, %0, %1  \n"
	    : "=&a" (res), "=&a" (tmp)
	    : "a" (x)
	    );

    return res;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#ifdef __XTENSA_EL__
# include <linux/byteorder/little_endian.h>
#elif defined(__XTENSA_EB__)
# include <linux/byteorder/big_endian.h>
#else
# error processor byte order undefined!
#endif

#endif /* __ASM_XTENSA_BYTEORDER_H */
