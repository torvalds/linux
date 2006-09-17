/* $Id: mman.h,v 1.9 2000/03/15 02:44:23 davem Exp $ */
#ifndef __SPARC_MMAN_H__
#define __SPARC_MMAN_H__

#include <asm-generic/mman.h>

/* SunOS'ified... */

#define MAP_RENAME      MAP_ANONYMOUS   /* In SunOS terminology */
#define MAP_NORESERVE   0x40            /* don't reserve swap pages */
#define MAP_INHERIT     0x80            /* SunOS doesn't do this, but... */
#define MAP_LOCKED      0x100           /* lock the mapping */
#define _MAP_NEW        0x80000000      /* Binary compatibility is fun... */

#define MAP_GROWSDOWN	0x0200		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */

#define MCL_CURRENT     0x2000          /* lock all currently mapped pages */
#define MCL_FUTURE      0x4000          /* lock all additions to address space */

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

/* XXX Need to add flags to SunOS's mctl, mlockall, and madvise system
 * XXX calls.
 */

/* SunOS sys_mctl() stuff... */
#define MC_SYNC         1  /* Sync pages in memory with storage (usu. a file) */
#define MC_LOCK         2  /* Lock pages into core ram, do not allow swapping of them */
#define MC_UNLOCK       3  /* Unlock pages locked via previous mctl() with MC_LOCK arg */
#define MC_LOCKAS       5  /* Lock an entire address space of the calling process */
#define MC_UNLOCKAS     6  /* Unlock entire address space of calling process */

#define MADV_FREE	0x5		/* (Solaris) contents can be freed */

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#define arch_mmap_check	sparc_mmap_check
int sparc_mmap_check(unsigned long addr, unsigned long len,
		unsigned long flags);
#endif
#endif

#endif /* __SPARC_MMAN_H__ */
