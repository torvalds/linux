/*	$OpenBSD: kvm_mips64.c,v 1.17 2021/12/01 16:53:28 deraadt Exp $ */
/*	$NetBSD: kvm_mips.c,v 1.3 1996/03/18 22:33:44 thorpej Exp $	*/

/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley. Modified for MIPS by Ralph Campbell.
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
 * MIPS machine dependent routines for kvm.  Hopefully, the forthcoming
 * vm code will one day obsolete this module.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <kvm.h>

#include <limits.h>
#include <db.h>

#include "kvm_private.h"

#include <machine/cpu.h>
#include <machine/pte.h>
#include <machine/pmap.h>

#include <uvm/uvm_extern.h>

struct vmstate {
	pt_entry_t	*Sysmap;
	u_int		Sysmapsize;
	vaddr_t		Sysmapbase;
	int		pagesize;
	int		pagemask;
	int		pageshift;
};

void
_kvm_freevtop(kvm_t *kd)
{
	free(kd->vmst);
	kd->vmst = NULL;
}

int
_kvm_initvtop(kvm_t *kd)
{
	struct vmstate *vm;
	struct nlist nl[4];
	struct uvmexp uvmexp;

	vm = (struct vmstate *)_kvm_malloc(kd, sizeof(*vm));
	if (vm == 0)
		return (-1);
	kd->vmst = vm;

	nl[0].n_name = "Sysmap";
	nl[1].n_name = "Sysmapsize";
	nl[2].n_name = "uvmexp";
	nl[3].n_name = 0;

	if (kvm_nlist(kd, nl) != 0) {
		_kvm_err(kd, kd->program, "bad namelist");
		return (-1);
	}
	if (KREAD(kd, (u_long)nl[0].n_value, &vm->Sysmap)) {
		_kvm_err(kd, kd->program, "cannot read Sysmap");
		return (-1);
	}
	if (KREAD(kd, (u_long)nl[1].n_value, &vm->Sysmapsize)) {
		_kvm_err(kd, kd->program, "cannot read Sysmapsize");
		return (-1);
	}
	/*
	 * We are only interested in the first three fields of struct
	 * uvmexp, so do not try to read more than necessary (especially
	 * in case the layout changes).
	 */
	if (kvm_read(kd, (u_long)nl[2].n_value, &uvmexp,
	    3 * sizeof(int)) != 3 * sizeof(int)) {
		_kvm_err(kd, kd->program, "cannot read uvmexp");
		return (-1);
	}
	vm->pagesize = uvmexp.pagesize;
	vm->pagemask = uvmexp.pagemask;
	vm->pageshift = uvmexp.pageshift;

	/*
	 * Older kernels might not have this symbol; in which case
	 * we use the value of VM_MIN_KERNEL_ADDRESS they must have.
	 */

	nl[0].n_name = "Sysmapbase";
	nl[1].n_name = 0;
	if (kvm_nlist(kd, nl) != 0 ||
	    KREAD(kd, (u_long)nl[0].n_value, &vm->Sysmapbase))
		vm->Sysmapbase = (vaddr_t)CKSSEG_BASE;

	return (0);
}

/*
 * Translate a kernel virtual address to a physical address.
 */
int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	struct vmstate *vm;
	pt_entry_t pte;
	u_long idx, addr;
	int offset;

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return((off_t)0);
	}
	vm = kd->vmst;
	offset = (int)va & vm->pagemask;
	/*
	 * If we are initializing (kernel segment table pointer not yet set)
	 * then return pa == va to avoid infinite recursion.
	 */
	if (vm->Sysmap == 0) {
		*pa = va;
		return vm->pagesize - offset;
	}
	/*
	 * Check for direct-mapped segments
	 */
	if (IS_XKPHYS(va)) {
		*pa = XKPHYS_TO_PHYS(va);
		return vm->pagesize - offset;
	}
	if (va >= (vaddr_t)CKSEG0_BASE && va < (vaddr_t)CKSSEG_BASE) {
		*pa = CKSEG0_TO_PHYS(va);
		return vm->pagesize - offset;
	}
	if (va < vm->Sysmapbase)
		goto invalid;
	idx = (va - vm->Sysmapbase) >> vm->pageshift;
	if (idx >= vm->Sysmapsize)
		goto invalid;
	addr = (u_long)vm->Sysmap + idx;
	/*
	 * Can't use KREAD to read kernel segment table entries.
	 * Fortunately it is 1-to-1 mapped so we don't have to.
	 */
	if (_kvm_pread(kd, kd->pmfd, (char *)&pte, sizeof(pte),
	    (off_t)addr) < 0)
		goto invalid;
	if (!(pte & PG_V))
		goto invalid;
	*pa = (pte & PG_FRAME) | (paddr_t)offset;
	return vm->pagesize - offset;

invalid:
	_kvm_err(kd, 0, "invalid address (%lx)", va);
	return (0);
}

off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	_kvm_err(kd, 0, "pa2off going to be implemented!");
	return 0;
}
