/* $Id: types.h,v 1.13 2001/12/21 01:22:59 davem Exp $ */
#ifndef _SPARC_TYPES_H
#define _SPARC_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */
#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__

#define BITS_PER_LONG 32

#ifndef __ASSEMBLY__

typedef u32 dma_addr_t;
typedef u32 dma64_addr_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_TYPES_H) */
