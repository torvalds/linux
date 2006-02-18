#ifndef _ASM_POWERPC_MMAN_H
#define _ASM_POWERPC_MMAN_H

#include <asm-generic/mman.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define MAP_RENAME      MAP_ANONYMOUS   /* In SunOS terminology */
#define MAP_NORESERVE   0x40            /* don't reserve swap pages */
#define MAP_LOCKED	0x80

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */

#define MCL_CURRENT     0x2000          /* lock all currently mapped pages */
#define MCL_FUTURE      0x4000          /* lock all additions to address space */

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#endif	/* _ASM_POWERPC_MMAN_H */
