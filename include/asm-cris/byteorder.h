#ifndef _CRIS_BYTEORDER_H
#define _CRIS_BYTEORDER_H

#ifdef __GNUC__

#include <asm/arch/byteorder.h>

/* defines are necessary because the other files detect the presence
 * of a defined __arch_swab32, not an inline
 */

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#endif /* __GNUC__ */

#include <linux/byteorder/little_endian.h>

#endif


