/* $Id: types.h,v 1.4 2001/10/09 02:24:35 davem Exp $ */
#ifndef _SPARC64_TYPES_H
#define _SPARC64_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */
#include <asm-generic/int-l64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__

#define BITS_PER_LONG 64

#ifndef __ASSEMBLY__

/* Dma addresses come in generic and 64-bit flavours.  */

typedef u32 dma_addr_t;
typedef u64 dma64_addr_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_TYPES_H) */
