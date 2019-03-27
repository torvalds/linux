/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
__SCCSID("@(#)kvm_hp300.c	8.1 (Berkeley) 6/4/93");

/*
 * AMD64 machine dependent routines for kvm.  Hopefully, the forthcoming
 * vm code will one day obsolete this module.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vm/vm.h>
#include <kvm.h>

#include <limits.h>

#include "kvm_private.h"
#include "kvm_amd64.h"

struct vmstate {
	size_t		phnum;
	GElf_Phdr	*phdr;
	amd64_pml4e_t	*PML4;
};

/*
 * Translate a physical memory address to a file-offset in the crash-dump.
 */
static size_t
_kvm_pa2off(kvm_t *kd, uint64_t pa, off_t *ofs)
{
	struct vmstate *vm = kd->vmst;
	GElf_Phdr *p;
	size_t n;

	if (kd->rawdump) {
		*ofs = pa;
		return (AMD64_PAGE_SIZE - (pa & AMD64_PAGE_MASK));
	}

	p = vm->phdr;
	n = vm->phnum;
	while (n && (pa < p->p_paddr || pa >= p->p_paddr + p->p_memsz))
		p++, n--;
	if (n == 0)
		return (0);
	*ofs = (pa - p->p_paddr) + p->p_offset;
	return (AMD64_PAGE_SIZE - (pa & AMD64_PAGE_MASK));
}

static void
_amd64_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	if (vm->PML4)
		free(vm->PML4);
	free(vm->phdr);
	free(vm);
	kd->vmst = NULL;
}

static int
_amd64_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS64, EM_X86_64) &&
	    !_kvm_is_minidump(kd));
}

static int
_amd64_initvtop(kvm_t *kd)
{
	struct kvm_nlist nl[2];
	amd64_physaddr_t pa;
	kvaddr_t kernbase;
	amd64_pml4e_t *PML4;

	kd->vmst = (struct vmstate *)_kvm_malloc(kd, sizeof(*kd->vmst));
	if (kd->vmst == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst->PML4 = 0;

	if (kd->rawdump == 0) {
		if (_kvm_read_core_phdrs(kd, &kd->vmst->phnum,
		    &kd->vmst->phdr) == -1)
			return (-1);
	}

	nl[0].n_name = "kernbase";
	nl[1].n_name = 0;

	if (kvm_nlist2(kd, nl) != 0) {
		_kvm_err(kd, kd->program, "bad namelist - no kernbase");
		return (-1);
	}
	kernbase = nl[0].n_value;

	nl[0].n_name = "KPML4phys";
	nl[1].n_name = 0;

	if (kvm_nlist2(kd, nl) != 0) {
		_kvm_err(kd, kd->program, "bad namelist - no KPML4phys");
		return (-1);
	}
	if (kvm_read2(kd, (nl[0].n_value - kernbase), &pa, sizeof(pa)) !=
	    sizeof(pa)) {
		_kvm_err(kd, kd->program, "cannot read KPML4phys");
		return (-1);
	}
	pa = le64toh(pa);
	PML4 = _kvm_malloc(kd, AMD64_PAGE_SIZE);
	if (PML4 == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate PML4");
		return (-1);
	}
	if (kvm_read2(kd, pa, PML4, AMD64_PAGE_SIZE) != AMD64_PAGE_SIZE) {
		_kvm_err(kd, kd->program, "cannot read KPML4phys");
		free(PML4);
		return (-1);
	}
	kd->vmst->PML4 = PML4;
	return (0);
}

static int
_amd64_vatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	amd64_physaddr_t offset;
	amd64_physaddr_t pdpe_pa;
	amd64_physaddr_t pde_pa;
	amd64_physaddr_t pte_pa;
	amd64_pml4e_t pml4e;
	amd64_pdpe_t pdpe;
	amd64_pde_t pde;
	amd64_pte_t pte;
	kvaddr_t pml4eindex;
	kvaddr_t pdpeindex;
	kvaddr_t pdeindex;
	kvaddr_t pteindex;
	amd64_physaddr_t a;
	off_t ofs;
	size_t s;

	vm = kd->vmst;
	offset = va & AMD64_PAGE_MASK;

	/*
	 * If we are initializing (kernel page table descriptor pointer
	 * not yet set) then return pa == va to avoid infinite recursion.
	 */
	if (vm->PML4 == NULL) {
		s = _kvm_pa2off(kd, va, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_amd64_vatop: bootstrap data not in dump");
			goto invalid;
		} else
			return (AMD64_PAGE_SIZE - offset);
	}

	pml4eindex = (va >> AMD64_PML4SHIFT) & (AMD64_NPML4EPG - 1);
	pml4e = le64toh(vm->PML4[pml4eindex]);
	if ((pml4e & AMD64_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pml4e not valid");
		goto invalid;
	}

	pdpeindex = (va >> AMD64_PDPSHIFT) & (AMD64_NPDPEPG - 1);
	pdpe_pa = (pml4e & AMD64_PG_FRAME) + (pdpeindex * sizeof(amd64_pdpe_t));

	s = _kvm_pa2off(kd, pdpe_pa, &ofs);
	if (s < sizeof(pdpe)) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pdpe_pa not found");
		goto invalid;
	}
	if (pread(kd->pmfd, &pdpe, sizeof(pdpe), ofs) != sizeof(pdpe)) {
		_kvm_syserr(kd, kd->program, "_amd64_vatop: read pdpe");
		goto invalid;
	}
	pdpe = le64toh(pdpe);
	if ((pdpe & AMD64_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pdpe not valid");
		goto invalid;
	}

	if (pdpe & AMD64_PG_PS) {
		/*
		 * No next-level page table; pdpe describes one 1GB page.
		 */
		a = (pdpe & AMD64_PG_1GB_FRAME) + (va & AMD64_PDPMASK);
		s = _kvm_pa2off(kd, a, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_amd64_vatop: 1GB page address not in dump");
			goto invalid;
		} else
			return (AMD64_NBPDP - (va & AMD64_PDPMASK));
	}

	pdeindex = (va >> AMD64_PDRSHIFT) & (AMD64_NPDEPG - 1);
	pde_pa = (pdpe & AMD64_PG_FRAME) + (pdeindex * sizeof(amd64_pde_t));

	s = _kvm_pa2off(kd, pde_pa, &ofs);
	if (s < sizeof(pde)) {
		_kvm_syserr(kd, kd->program, "_amd64_vatop: pde_pa not found");
		goto invalid;
	}
	if (pread(kd->pmfd, &pde, sizeof(pde), ofs) != sizeof(pde)) {
		_kvm_syserr(kd, kd->program, "_amd64_vatop: read pde");
		goto invalid;
	}
	pde = le64toh(pde);
	if ((pde & AMD64_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pde not valid");
		goto invalid;
	}

	if (pde & AMD64_PG_PS) {
		/*
		 * No final-level page table; pde describes one 2MB page.
		 */
		a = (pde & AMD64_PG_PS_FRAME) + (va & AMD64_PDRMASK);
		s = _kvm_pa2off(kd, a, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_amd64_vatop: 2MB page address not in dump");
			goto invalid;
		} else
			return (AMD64_NBPDR - (va & AMD64_PDRMASK));
	}

	pteindex = (va >> AMD64_PAGE_SHIFT) & (AMD64_NPTEPG - 1);
	pte_pa = (pde & AMD64_PG_FRAME) + (pteindex * sizeof(amd64_pte_t));

	s = _kvm_pa2off(kd, pte_pa, &ofs);
	if (s < sizeof(pte)) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pte_pa not found");
		goto invalid;
	}
	if (pread(kd->pmfd, &pte, sizeof(pte), ofs) != sizeof(pte)) {
		_kvm_syserr(kd, kd->program, "_amd64_vatop: read");
		goto invalid;
	}
	if ((pte & AMD64_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_amd64_vatop: pte not valid");
		goto invalid;
	}

	a = (pte & AMD64_PG_FRAME) + offset;
	s = _kvm_pa2off(kd, a, pa);
	if (s == 0) {
		_kvm_err(kd, kd->program, "_amd64_vatop: address not in dump");
		goto invalid;
	} else
		return (AMD64_PAGE_SIZE - offset);

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_amd64_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "kvm_kvatop called in live kernel!");
		return (0);
	}
	return (_amd64_vatop(kd, va, pa));
}

int
_amd64_native(kvm_t *kd __unused)
{

#ifdef __amd64__
	return (1);
#else
	return (0);
#endif
}

static struct kvm_arch kvm_amd64 = {
	.ka_probe = _amd64_probe,
	.ka_initvtop = _amd64_initvtop,
	.ka_freevtop = _amd64_freevtop,
	.ka_kvatop = _amd64_kvatop,
	.ka_native = _amd64_native,
};

KVM_ARCH(kvm_amd64);
