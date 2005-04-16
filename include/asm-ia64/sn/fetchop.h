/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2004 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_FETCHOP_H
#define _ASM_IA64_SN_FETCHOP_H

#include <linux/config.h>

#define FETCHOP_BASENAME	"sgi_fetchop"
#define FETCHOP_FULLNAME	"/dev/sgi_fetchop"



#define FETCHOP_VAR_SIZE 64 /* 64 byte per fetchop variable */

#define FETCHOP_LOAD		0
#define FETCHOP_INCREMENT	8
#define FETCHOP_DECREMENT	16
#define FETCHOP_CLEAR		24

#define FETCHOP_STORE		0
#define FETCHOP_AND		24
#define FETCHOP_OR		32

#define FETCHOP_CLEAR_CACHE	56

#define FETCHOP_LOAD_OP(addr, op) ( \
         *(volatile long *)((char*) (addr) + (op)))

#define FETCHOP_STORE_OP(addr, op, x) ( \
         *(volatile long *)((char*) (addr) + (op)) = (long) (x))

#ifdef __KERNEL__

/*
 * Convert a region 6 (kaddr) address to the address of the fetchop variable
 */
#define FETCHOP_KADDR_TO_MSPEC_ADDR(kaddr)	TO_MSPEC(kaddr)


/*
 * Each Atomic Memory Operation (AMO formerly known as fetchop)
 * variable is 64 bytes long.  The first 8 bytes are used.  The
 * remaining 56 bytes are unaddressable due to the operation taking
 * that portion of the address.
 * 
 * NOTE: The AMO_t _MUST_ be placed in either the first or second half
 * of the cache line.  The cache line _MUST NOT_ be used for anything
 * other than additional AMO_t entries.  This is because there are two
 * addresses which reference the same physical cache line.  One will
 * be a cached entry with the memory type bits all set.  This address
 * may be loaded into processor cache.  The AMO_t will be referenced
 * uncached via the memory special memory type.  If any portion of the
 * cached cache-line is modified, when that line is flushed, it will
 * overwrite the uncached value in physical memory and lead to
 * inconsistency.
 */
typedef struct {
        u64 variable;
        u64 unused[7];
} AMO_t;


/*
 * The following APIs are externalized to the kernel to allocate/free pages of
 * fetchop variables.
 *	fetchop_kalloc_page	- Allocate/initialize 1 fetchop page on the
 *				  specified cnode. 
 *	fetchop_kfree_page	- Free a previously allocated fetchop page
 */

unsigned long fetchop_kalloc_page(int nid);
void fetchop_kfree_page(unsigned long maddr);


#endif /* __KERNEL__ */

#endif /* _ASM_IA64_SN_FETCHOP_H */

