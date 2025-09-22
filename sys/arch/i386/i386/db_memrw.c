/*	$OpenBSD: db_memrw.c,v 1.19 2024/02/23 18:19:03 cheloha Exp $	*/
/*	$NetBSD: db_memrw.c,v 1.6 1999/04/12 20:38:19 pk Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 *	db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Routines to read and write memory on behalf of the debugger, used
 * by DDB.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>

#define PG_LGFRAME	0xffc00000	/* large (4M) page frame mask */
#define PG_LGFRAME_PAE	0xffe00000	/* large (2M) page frame mask */

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *src;

	src = (char *)addr;
	while (size-- > 0)
		*data++ = *src++;
}

/*
 * Write bytes somewhere in the kernel text.  Make the text
 * pages writable temporarily.
 */
static void
db_write_text(vaddr_t addr, size_t size, char *data)
{
	vaddr_t pgva;
	size_t limit;
	uint32_t bits;
	char *dst;

	if (size == 0)
		return;

	dst = (char *)addr;

	do {
		/*
		 * Get the PTE for the page.
		 */
		bits = pmap_pte_bits(addr);

		if ((bits & PG_V) == 0) {
			printf(" address %p not a valid page\n", dst);
			return;
		}

		/*
		 * Get the VA for the page.
		 */
		if (bits & PG_PS) {
			if (cpu_pae)
				pgva = (vaddr_t)dst & PG_LGFRAME_PAE;
			else
				pgva = (vaddr_t)dst & PG_LGFRAME;
		 } else
			pgva = trunc_page((vaddr_t)dst);

		/*
		 * Compute number of bytes that can be written
		 * with this mapping and subtract it from the
		 * total size.
		 */
#ifdef NBPD_L2
		if (bits & PG_PS)
			limit = NBPD_L2 - ((vaddr_t)dst & (NBPD_L2 - 1));
		else
#endif
			limit = PAGE_SIZE - ((vaddr_t)dst & PGOFSET);
		if (limit > size)
			limit = size;
		size -= limit;

		pmap_update_pg(pgva);
		pmap_pte_setbits(addr, PG_RW, 0);

		/*
		 * Page is now writable.  Do as much access as we
		 * can in this page.
		 */
		for (; limit > 0; limit--)
			*dst++ = *data++;

		/*
		 * Restore the old PTE.
		 */
		pmap_update_pg(pgva);
		pmap_pte_setbits(addr, bits, PG_RW);
		
	} while (size != 0);
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *dst;
	extern char	etext;

	if (addr >= VM_MIN_KERNEL_ADDRESS &&
	    addr < (vaddr_t)&etext) {
		db_write_text(addr, size, data);
		return;
	}

	dst = (char *)addr;

	while (size-- > 0)
		*dst++ = *data++;
}
