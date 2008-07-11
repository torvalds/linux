#ifndef _ALPHA_TYPES_H
#define _ALPHA_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */
#include <asm-generic/int-l64.h>

#ifndef __ASSEMBLY__

typedef unsigned int umode_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#define BITS_PER_LONG 64

#ifndef __ASSEMBLY__

typedef u64 dma_addr_t;
typedef u64 dma64_addr_t;

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ALPHA_TYPES_H */
