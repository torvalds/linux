#ifndef _ASM_GENERIC_TYPES_H
#define _ASM_GENERIC_TYPES_H
/*
 * int-ll64 is used practically everywhere now,
 * so use it as a reasonable default.
 */
#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

#endif /* _ASM_GENERIC_TYPES_H */
