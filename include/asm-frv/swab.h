#ifndef _ASM_SWAB_H
#define _ASM_SWAB_H

#include <asm/types.h>

#if defined(__GNUC__) && !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __SWAB_64_THRU_32__
#endif

#endif /* _ASM_SWAB_H */
