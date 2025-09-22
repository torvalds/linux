/*	$OpenBSD: kvm_amd64.c,v 1.16 2023/03/08 04:43:05 guenther Exp $	*/
/*	$NetBSD: kvm_x86_64.c,v 1.3 2002/06/05 22:01:55 fvdl Exp $	*/

/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * x86-64 machine dependent routines for kvm.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/kcore.h>
#include <machine/kcore.h>
#include <stdlib.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <uvm/uvm_extern.h>

#include <limits.h>
#include <db.h>

#include "kvm_private.h"

#include <machine/pmap.h>
#include <machine/pte.h>
#include <machine/vmparam.h>

void
_kvm_freevtop(kvm_t *kd)
{
	/* Not actually used for anything right now, but safe. */
	free(kd->vmst);
	kd->vmst = NULL;
}

int
_kvm_initvtop(kvm_t *kd)
{

	return (0);
}

/*
 * Translate a kernel virtual address to a physical address.
 */
int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	cpu_kcore_hdr_t *cpu_kh;
	paddr_t pde_pa, pte_pa;
	u_long page_off;
	pd_entry_t pde;
	pt_entry_t pte;

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}

	page_off = va & (kd->nbpg - 1);

	if (va >= PMAP_DIRECT_BASE && va <= PMAP_DIRECT_END) {
		*pa = va - PMAP_DIRECT_BASE;
		return (int)(kd->nbpg - page_off);
	}

	cpu_kh = kd->cpu_data;

	/*
	 * Find and read all entries to get to the pa.
	 */

	/*
	 * Level 4.
	 */
	pde_pa = cpu_kh->ptdpaddr + (pl4_pi(va) * sizeof(pd_entry_t));
	if (pread(kd->pmfd, (void *)&pde, sizeof(pde),
	    _kvm_pa2off(kd, pde_pa)) != sizeof(pde)) {
		_kvm_syserr(kd, 0, "could not read PT level 4 entry");
		goto lose;
	}
	if ((pde & PG_V) == 0) {
		_kvm_err(kd, 0, "invalid translation (invalid level 4 PDE)");
		goto lose;
	}

	/*
	 * Level 3.
	 */
	pde_pa = (pde & PG_FRAME) + (pl3_pi(va) * sizeof(pd_entry_t));
	if (pread(kd->pmfd, (void *)&pde, sizeof(pde),
	    _kvm_pa2off(kd, pde_pa)) != sizeof(pde)) {
		_kvm_syserr(kd, 0, "could not read PT level 3 entry");
		goto lose;
	}
	if ((pde & PG_V) == 0) {
		_kvm_err(kd, 0, "invalid translation (invalid level 3 PDE)");
		goto lose;
	}

	/*
	 * Level 2.
	 */
	pde_pa = (pde & PG_FRAME) + (pl2_pi(va) * sizeof(pd_entry_t));
	if (pread(kd->pmfd, (void *)&pde, sizeof(pde),
	    _kvm_pa2off(kd, pde_pa)) != sizeof(pde)) {
		_kvm_syserr(kd, 0, "could not read PT level 2 entry");
		goto lose;
	}
	if ((pde & PG_V) == 0) {
		_kvm_err(kd, 0, "invalid translation (invalid level 2 PDE)");
		goto lose;
	}

	/*
	 * Might be a large page.
	 */
	if ((pde & PG_PS) != 0) {
		page_off = va & (NBPD_L2 - 1);
		*pa = (pde & PG_LGFRAME) | page_off;
		return (int)(NBPD_L2 - page_off);
	}

	/*
	 * Level 1.
	 */
	pte_pa = (pde & PG_FRAME) + (pl1_pi(va) * sizeof(pt_entry_t));
	if (pread(kd->pmfd, (void *) &pte, sizeof(pte),
	    _kvm_pa2off(kd, pte_pa)) != sizeof(pte)) {
		_kvm_syserr(kd, 0, "could not read PTE");
		goto lose;
	}
	/*
	 * Validate the PTE and return the physical address.
	 */
	if ((pte & PG_V) == 0) {
		_kvm_err(kd, 0, "invalid translation (invalid PTE)");
		goto lose;
	}
	*pa = (pte & PG_FRAME) + page_off;
	return (int)(kd->nbpg - page_off);

 lose:
	*pa = (u_long)~0L;
	return (0);
}

/*
 * Translate a physical address to a file-offset in the crash dump.
 */
off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	cpu_kcore_hdr_t *cpu_kh;
	phys_ram_seg_t *ramsegs;
	off_t off;
	int i;

	cpu_kh = kd->cpu_data;
	ramsegs = (void *)((char *)(void *)cpu_kh + _ALIGN(sizeof *cpu_kh));

	off = 0;
	for (i = 0; i < cpu_kh->nmemsegs; i++) {
		if (pa >= ramsegs[i].start &&
		    (pa - ramsegs[i].start) < ramsegs[i].size) {
			off += (pa - ramsegs[i].start);
			break;
		}
		off += ramsegs[i].size;
	}

	if (i == cpu_kh->nmemsegs)
		_kvm_err(kd, 0, "pa %lx not in dump", pa);

	return (kd->dump_off + off);
}
