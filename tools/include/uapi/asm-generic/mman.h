/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_MMAN_H
#define __ASM_GENERIC_MMAN_H

#include <asm-generic/mman-common-tools.h>

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define MAP_LOCKED	0x2000		/* pages are locked */
#define MAP_NORESERVE	0x4000		/* don't check for reservations */
#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */
#define MAP_STACK	0x20000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x40000		/* create a huge page mapping */
#define MAP_SYNC	0x80000		/* perform synchronous page faults for the mapping */

/* Bits [26:31] are reserved, see mman-common.h for MAP_HUGETLB usage */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */
#define MCL_ONFAULT	4		/* lock all pages that are faulted in */

#endif /* __ASM_GENERIC_MMAN_H */
