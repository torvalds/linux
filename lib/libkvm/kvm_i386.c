/*	$OpenBSD: kvm_i386.c,v 1.28 2021/12/01 16:53:28 deraadt Exp $ */
/*	$NetBSD: kvm_i386.c,v 1.9 1996/03/18 22:33:38 thorpej Exp $	*/

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
 * i386 machine dependent routines for kvm.  Hopefully, the forthcoming
 * vm code will one day obsolete this module.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <nlist.h>
#include <kvm.h>

#include <uvm/uvm_extern.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/pmap.h>

#include <limits.h>
#include <db.h>

#include "kvm_private.h"

#include <machine/pte.h>

/*
 * We access both normal and PAE entries in 32bit chunks.
 * Use a local name to avoid conflicting with the kernel's maybe-public,
 * maybe-not p[td]_entry_t typedefs.
 */
typedef u_long ptd_entry_t;

/*
 * These must match the values in pmap.c/pmapae.c
 * First the non-PAE versions
 */
#define PD_MASK		0xffc00000	/* page directory address bits */
#define PT_MASK		0x003ff000	/* page table address bits */

/*
 * PAE versions
 *
 * paddr_t is still 32bits, so the top 32bits of PDEs and PTEs only
 * matters for the NX bit...which libkvm doesn't care about
 */
#define PAE_PDSHIFT	21
#define PAE_PD_MASK	0xffe00000	/* page directory address bits */
#define PAE_PT_MASK	0x001ff000	/* page table address bits */

#define PG_FRAME	0xfffff000

static int cpu_pae;

struct vmstate {
	ptd_entry_t *PTD;
	ptd_entry_t PD_mask;
	ptd_entry_t PT_mask;
	int PD_shift;
	int PG_shift;
};

#define pdei(vm,VA)	(((VA) & (vm)->PD_mask) >> (vm)->PD_shift)
#define ptei(vm,VA)	(((VA) & (vm)->PT_mask) >> PAGE_SHIFT)

void
_kvm_freevtop(kvm_t *kd)
{
	if (kd->vmst != NULL) {
		free(kd->vmst->PTD);

		free(kd->vmst);
		kd->vmst = NULL;
	}
}

int
_kvm_initvtop(kvm_t *kd)
{
	struct nlist nl[4];
	struct vmstate *vm;
	u_long pa, PTDsize;

	vm = _kvm_malloc(kd, sizeof(*vm));
	if (vm == NULL)
		return (-1);
	kd->vmst = vm;

	vm->PTD = NULL;

	nl[0].n_name = "_PTDpaddr";
	nl[1].n_name = "_PTDsize";
	nl[2].n_name = "_cpu_pae";
	nl[3].n_name = NULL;

	if (kvm_nlist(kd, nl) != 0) {
		_kvm_err(kd, kd->program, "bad namelist");
		return (-1);
	}

	if (_kvm_pread(kd, kd->pmfd, &cpu_pae, sizeof cpu_pae,
	    _kvm_pa2off(kd, nl[2].n_value - KERNBASE)) != sizeof cpu_pae)
		goto invalid;

	if (_kvm_pread(kd, kd->pmfd, &PTDsize, sizeof PTDsize,
	    _kvm_pa2off(kd, nl[1].n_value - KERNBASE)) != sizeof PTDsize)
		goto invalid;

	if (_kvm_pread(kd, kd->pmfd, &pa, sizeof pa,
	    _kvm_pa2off(kd, nl[0].n_value - KERNBASE)) != sizeof pa)
		goto invalid;

	vm->PTD = _kvm_malloc(kd, PTDsize);

	if (_kvm_pread(kd, kd->pmfd, vm->PTD, PTDsize,
	    _kvm_pa2off(kd, pa)) != PTDsize)
		goto invalid;

	if (cpu_pae) {
		vm->PD_mask = PAE_PD_MASK;
		vm->PT_mask = PAE_PT_MASK;
		/* -1 here because entries are twice as large */
		vm->PD_shift = PAE_PDSHIFT - 1;
		vm->PG_shift = PAGE_SHIFT - 1;
	} else {
		vm->PD_mask = PD_MASK;
		vm->PT_mask = PT_MASK;
		vm->PD_shift = PDSHIFT;
		vm->PG_shift = PAGE_SHIFT;
	}

	return (0);

invalid:
	free(vm->PTD);
	vm->PTD = NULL;
	return (-1);
}

/*
 * Translate a kernel virtual address to a physical address.
 */
int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	u_long offset, pte_pa;
	struct vmstate *vm;
	ptd_entry_t pte;

	if (!kd->vmst) {
		_kvm_err(kd, 0, "vatop called before initvtop");
		return (0);
	}

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}

	vm = kd->vmst;
	offset = va & (kd->nbpg - 1);

	/*
	 * If we are initializing (kernel page table descriptor pointer
	 * not yet set) * then return pa == va to avoid infinite recursion.
	 */
	if (vm->PTD == NULL) {
		*pa = va;
		return (kd->nbpg - (int)offset);
	}
	if ((vm->PTD[pdei(vm,va)] & PG_V) == 0)
		goto invalid;

	pte_pa = (vm->PTD[pdei(vm,va)] & PG_FRAME) +
	    (ptei(vm,va) * sizeof(ptd_entry_t));

	/* XXX READ PHYSICAL XXX */
	if (_kvm_pread(kd, kd->pmfd, &pte, sizeof pte,
	    _kvm_pa2off(kd, pte_pa)) != sizeof pte)
		goto invalid;

	if ((pte & PG_V) == 0)
		goto invalid;
	*pa = (pte & PG_FRAME) + offset;
	return (kd->nbpg - (int)offset);

invalid:
	_kvm_err(kd, 0, "invalid address (%lx)", va);
	return (0);
}

/*
 * Translate a physical address to a file-offset in the crash-dump.
 */
off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	return ((off_t)(kd->dump_off + pa));
}
