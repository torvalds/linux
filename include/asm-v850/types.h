#ifndef __V850_TYPES_H__
#define __V850_TYPES_H__

#ifndef __ASSEMBLY__

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */
#include <asm-generic/int-ll64.h>

typedef unsigned short umode_t;

#endif /* !__ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#define BITS_PER_LONG 32

#ifndef __ASSEMBLY__

/* Dma addresses are 32-bits wide.  */

typedef u32 dma_addr_t;

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __V850_TYPES_H__ */
