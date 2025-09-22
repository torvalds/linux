/* $OpenBSD: tc_machdep.h,v 1.3 2002/05/02 22:56:06 miod Exp $ */
/* $NetBSD: tc_machdep.h,v 1.4 2000/06/01 00:04:50 cgd Exp $ */

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Machine-specific definitions for TurboChannel support.
 *
 * This file must typedef the following types:
 *
 *	tc_addr_t	TurboChannel bus address
 *	tc_offset_t	TurboChannel bus address difference (offset)
 *
 * This file must prototype or define the following functions
 * or macros (one or more of which may be no-ops):
 *
 *	tc_mb()		read/write memory barrier (any CPU<->memory
 *			reads/writes before must complete before any
 *			CPU<->memory reads/writes after).
 *	tc_wmb()	write memory barrier (any CPU<->memory writes
 *			before must complete before any CPU<->memory
 *			writes after).
 *	tc_syncbus()	sync TC bus; make sure CPU writes are
 *			propagated across the TurboChannel bus.
 *	tc_badaddr()	return non-zero if the given address is invalid.
 *	TC_DENSE_TO_SPARSE()
 *			convert the given physical address in
 *			TurboChannel dense space to the corresponding
 *			address in TurboChannel sparse space.
 *	TC_PHYS_TO_UNCACHED()
 *			convert the given system memory physical address
 *			to the physical address of the corresponding
 *			region that is not cached.
 */

typedef u_int64_t	tc_addr_t;
typedef int32_t		tc_offset_t;

#define	tc_mb()		alpha_mb()
#define	tc_wmb()	alpha_wmb()

/*
 * A junk address to read from, to make sure writes are complete.  See
 * System Programmer's Manual, section 9.3 (p. 9-4), and sacrifice a
 * chicken.
 */
#define	tc_syncbus()							\
    do {								\
	volatile u_int32_t no_optimize;					\
	no_optimize =	 						\
	    *(volatile u_int32_t *)ALPHA_PHYS_TO_K0SEG(0x00000001f0080220); \
    } while (0)

#define	tc_badaddr(tcaddr)						\
    badaddr((void *)(tcaddr), sizeof (u_int32_t))

#define	TC_SPACE_IND		0xffffffffe0000003
#define	TC_SPACE_DENSE		0x0000000000000000
#define TC_SPACE_DENSE_OFFSET	0x0000000007fffffc
#define	TC_SPACE_SPARSE		0x0000000010000000
#define	TC_SPACE_SPARSE_OFFSET	0x000000000ffffff8

#define	TC_DENSE_TO_SPARSE(addr)					\
    (((addr) & TC_SPACE_IND) | TC_SPACE_SPARSE |			\
	(((addr) & TC_SPACE_DENSE_OFFSET) << 1))
		
#define	TC_PHYS_TO_UNCACHED(addr)					\
    (addr)

/*
 * These functions are private, and may not be called by
 * machine-independent code.
 */
bus_space_tag_t tc_bus_mem_init(void *memv);
void tc_dma_init(void);

/*
 * Address of scatter/gather SRAM on the 3000/500-series.
 *
 * There is room for 32K entries, yielding 256M of sgva space.
 * The page table is readable in both dense and sparse space.
 * The page table is writable only in sparse space.
 *
 * In sparse space, the 32-bit PTEs are followed by 32-bits
 * of pad.
 */
#define	TC_SGSRAM_DENSE		0x0000001c2800000UL
#define	TC_SGSRAM_SPARSE	0x0000001d5000000UL
