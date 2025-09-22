/*	$OpenBSD: db_memrw.c,v 1.3 2024/02/23 18:19:03 cheloha Exp $	*/
/*	$NetBSD: db_memrw.c,v 1.1 2003/04/26 18:39:27 fvdl Exp $	*/

/*-
 * Copyright (c) 1996, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross and Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/db_machdep.h>
#include <machine/pmap.h>
#include <machine/pte.h>

#include <ddb/db_access.h>

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, size_t size, void *datap)
{
	char *data = datap, *src;

	src = (char *)addr;

	if (size == 8) {
		*((long *)data) = *((long *)src);
		return;
	}

	if (size == 4) {
		*((int *)data) = *((int *)src);
		return;
	}

	if (size == 2) {
		*((short *)data) = *((short *)src);
		return;
	}

	while (size-- > 0)
		*data++ = *src++;
}

/*
 * Write bytes somewhere in the kernel text.  Make the text
 * pages writable temporarily.
 */
void
db_write_text(vaddr_t addr, size_t size, char *data)
{
	struct pte *pte;
	uint64_t old_pte_lo;
	vaddr_t pgva;
	size_t limit;
	char *dst;

	if (size == 0)
		return;

	dst = (char *)addr;

	do {
		pte = pmap_get_kernel_pte((vaddr_t)dst);
		if (pte == NULL) {
			printf(" address %p not a valid page\n", dst);
			return;
		}

		/*
		 * Compute number of bytes that can be written
		 * with this mapping and subtract it from the
		 * total size.
		 */
		pgva = trunc_page((vaddr_t)dst);
		limit = PAGE_SIZE - ((vaddr_t)dst & PGOFSET);
		if (limit > size)
			limit = size;
		size -= limit;

		/*
		 * Gain write access to the page.  We don't need
		 * tlbie to "add access authorities" (Power ISA 3.0
		 * III 5.10.1.2 Modifying a Translation Table Entry).
		 */
		old_pte_lo = pte->pte_lo;
		pte->pte_lo = (old_pte_lo & ~PTE_PP) | PTE_RW;
		ptesync();

		for (; limit > 0; limit--)
			*dst++ = *data++;

		/* Restore the old PTE. */
		pte->pte_lo = old_pte_lo;
		ptesync();
		tlbie(pgva);
		eieio();
		tlbsync();
		ptesync();

	} while (size != 0);

	__syncicache((void *)addr, limit);
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, size_t size, void *datap)
{
	extern char _start[], _etext[];
	char *data = datap, *dst;

	/* If any part is in kernel text, use db_write_text() */
	if (addr >= (vaddr_t)_start && addr < (vaddr_t)_etext) {
		db_write_text(addr, size, data);
		return;
	}

	dst = (char *)addr;

	if (size == 8) {
		*((long *)dst) = *((long *)data);
		return;
	}

	if (size == 4) {
		*((int *)dst) = *((int *)data);
		return;
	}

	if (size == 2) {
		*((short *)dst) = *((short *)data);
		return;
	}

	while (size-- > 0)
		*dst++ = *data++;
}
