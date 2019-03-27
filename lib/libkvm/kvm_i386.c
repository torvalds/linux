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
 * i386 machine dependent routines for kvm.  Hopefully, the forthcoming
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

#ifdef __i386__
#include <machine/vmparam.h>		/* For KERNBASE. */
#endif

#include <limits.h>

#include "kvm_private.h"
#include "kvm_i386.h"

struct vmstate {
	void		*PTD;
	int		pae;
	size_t		phnum;
	GElf_Phdr	*phdr;
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
		return (I386_PAGE_SIZE - (pa & I386_PAGE_MASK));
	}

	p = vm->phdr;
	n = vm->phnum;
	while (n && (pa < p->p_paddr || pa >= p->p_paddr + p->p_memsz))
		p++, n--;
	if (n == 0)
		return (0);
	*ofs = (pa - p->p_paddr) + p->p_offset;
	return (I386_PAGE_SIZE - (pa & I386_PAGE_MASK));
}

static void
_i386_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	if (vm->PTD)
		free(vm->PTD);
	free(vm->phdr);
	free(vm);
	kd->vmst = NULL;
}

static int
_i386_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS32, EM_386) &&
	    !_kvm_is_minidump(kd));
}

static int
_i386_initvtop(kvm_t *kd)
{
	struct kvm_nlist nl[2];
	i386_physaddr_t pa;
	kvaddr_t kernbase;
	char		*PTD;
	int		i;

	kd->vmst = (struct vmstate *)_kvm_malloc(kd, sizeof(struct vmstate));
	if (kd->vmst == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst->PTD = 0;

	if (kd->rawdump == 0) {
		if (_kvm_read_core_phdrs(kd, &kd->vmst->phnum,
		    &kd->vmst->phdr) == -1)
			return (-1);
	}

	nl[0].n_name = "kernbase";
	nl[1].n_name = 0;

	if (kvm_nlist2(kd, nl) != 0) {
#ifdef __i386__
		kernbase = KERNBASE;	/* for old kernels */
#else
		_kvm_err(kd, kd->program, "cannot resolve kernbase");
		return (-1);
#endif
	} else
		kernbase = nl[0].n_value;

	nl[0].n_name = "IdlePDPT";
	nl[1].n_name = 0;

	if (kvm_nlist2(kd, nl) == 0) {
		i386_physaddr_pae_t pa64;

		if (kvm_read2(kd, (nl[0].n_value - kernbase), &pa,
		    sizeof(pa)) != sizeof(pa)) {
			_kvm_err(kd, kd->program, "cannot read IdlePDPT");
			return (-1);
		}
		pa = le32toh(pa);
		PTD = _kvm_malloc(kd, 4 * I386_PAGE_SIZE);
		if (PTD == NULL) {
			_kvm_err(kd, kd->program, "cannot allocate PTD");
			return (-1);
		}
		for (i = 0; i < 4; i++) {
			if (kvm_read2(kd, pa + (i * sizeof(pa64)), &pa64,
			    sizeof(pa64)) != sizeof(pa64)) {
				_kvm_err(kd, kd->program, "Cannot read PDPT");
				free(PTD);
				return (-1);
			}
			pa64 = le64toh(pa64);
			if (kvm_read2(kd, pa64 & I386_PG_FRAME_PAE,
			    PTD + (i * I386_PAGE_SIZE), I386_PAGE_SIZE) !=
			    I386_PAGE_SIZE) {
				_kvm_err(kd, kd->program, "cannot read PDPT");
				free(PTD);
				return (-1);
			}
		}
		kd->vmst->PTD = PTD;
		kd->vmst->pae = 1;
	} else {
		nl[0].n_name = "IdlePTD";
		nl[1].n_name = 0;

		if (kvm_nlist2(kd, nl) != 0) {
			_kvm_err(kd, kd->program, "bad namelist");
			return (-1);
		}
		if (kvm_read2(kd, (nl[0].n_value - kernbase), &pa,
		    sizeof(pa)) != sizeof(pa)) {
			_kvm_err(kd, kd->program, "cannot read IdlePTD");
			return (-1);
		}
		pa = le32toh(pa);
		PTD = _kvm_malloc(kd, I386_PAGE_SIZE);
		if (PTD == NULL) {
			_kvm_err(kd, kd->program, "cannot allocate PTD");
			return (-1);
		}
		if (kvm_read2(kd, pa, PTD, I386_PAGE_SIZE) != I386_PAGE_SIZE) {
			_kvm_err(kd, kd->program, "cannot read PTD");
			return (-1);
		}
		kd->vmst->PTD = PTD;
		kd->vmst->pae = 0;
	}
	return (0);
}

static int
_i386_vatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	i386_physaddr_t offset;
	i386_physaddr_t pte_pa;
	i386_pde_t pde;
	i386_pte_t pte;
	kvaddr_t pdeindex;
	kvaddr_t pteindex;
	size_t s;
	i386_physaddr_t a;
	off_t ofs;
	i386_pde_t *PTD;

	vm = kd->vmst;
	PTD = (i386_pde_t *)vm->PTD;
	offset = va & I386_PAGE_MASK;

	/*
	 * If we are initializing (kernel page table descriptor pointer
	 * not yet set) then return pa == va to avoid infinite recursion.
	 */
	if (PTD == NULL) {
		s = _kvm_pa2off(kd, va, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_i386_vatop: bootstrap data not in dump");
			goto invalid;
		} else
			return (I386_PAGE_SIZE - offset);
	}

	pdeindex = va >> I386_PDRSHIFT;
	pde = le32toh(PTD[pdeindex]);
	if ((pde & I386_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_i386_vatop: pde not valid");
		goto invalid;
	}

	if (pde & I386_PG_PS) {
		/*
		 * No second-level page table; ptd describes one 4MB
		 * page.  (We assume that the kernel wouldn't set
		 * PG_PS without enabling it cr0).
		 */
		offset = va & I386_PAGE_PS_MASK;
		a = (pde & I386_PG_PS_FRAME) + offset;
		s = _kvm_pa2off(kd, a, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_i386_vatop: 4MB page address not in dump");
			goto invalid;
		}
		return (I386_NBPDR - offset);
	}

	pteindex = (va >> I386_PAGE_SHIFT) & (I386_NPTEPG - 1);
	pte_pa = (pde & I386_PG_FRAME) + (pteindex * sizeof(pte));

	s = _kvm_pa2off(kd, pte_pa, &ofs);
	if (s < sizeof(pte)) {
		_kvm_err(kd, kd->program, "_i386_vatop: pte_pa not found");
		goto invalid;
	}

	/* XXX This has to be a physical address read, kvm_read is virtual */
	if (pread(kd->pmfd, &pte, sizeof(pte), ofs) != sizeof(pte)) {
		_kvm_syserr(kd, kd->program, "_i386_vatop: pread");
		goto invalid;
	}
	pte = le32toh(pte);
	if ((pte & I386_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_kvm_kvatop: pte not valid");
		goto invalid;
	}

	a = (pte & I386_PG_FRAME) + offset;
	s = _kvm_pa2off(kd, a, pa);
	if (s == 0) {
		_kvm_err(kd, kd->program, "_i386_vatop: address not in dump");
		goto invalid;
	} else
		return (I386_PAGE_SIZE - offset);

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_i386_vatop_pae(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	i386_physaddr_pae_t offset;
	i386_physaddr_pae_t pte_pa;
	i386_pde_pae_t pde;
	i386_pte_pae_t pte;
	kvaddr_t pdeindex;
	kvaddr_t pteindex;
	size_t s;
	i386_physaddr_pae_t a;
	off_t ofs;
	i386_pde_pae_t *PTD;

	vm = kd->vmst;
	PTD = (i386_pde_pae_t *)vm->PTD;
	offset = va & I386_PAGE_MASK;

	/*
	 * If we are initializing (kernel page table descriptor pointer
	 * not yet set) then return pa == va to avoid infinite recursion.
	 */
	if (PTD == NULL) {
		s = _kvm_pa2off(kd, va, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_i386_vatop_pae: bootstrap data not in dump");
			goto invalid;
		} else
			return (I386_PAGE_SIZE - offset);
	}

	pdeindex = va >> I386_PDRSHIFT_PAE;
	pde = le64toh(PTD[pdeindex]);
	if ((pde & I386_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_kvm_kvatop_pae: pde not valid");
		goto invalid;
	}

	if (pde & I386_PG_PS) {
		/*
		 * No second-level page table; ptd describes one 2MB
		 * page.  (We assume that the kernel wouldn't set
		 * PG_PS without enabling it cr0).
		 */
		offset = va & I386_PAGE_PS_MASK_PAE;
		a = (pde & I386_PG_PS_FRAME_PAE) + offset;
		s = _kvm_pa2off(kd, a, pa);
		if (s == 0) {
			_kvm_err(kd, kd->program,
			    "_i386_vatop: 2MB page address not in dump");
			goto invalid;
		}
		return (I386_NBPDR_PAE - offset);
	}

	pteindex = (va >> I386_PAGE_SHIFT) & (I386_NPTEPG_PAE - 1);
	pte_pa = (pde & I386_PG_FRAME_PAE) + (pteindex * sizeof(pde));

	s = _kvm_pa2off(kd, pte_pa, &ofs);
	if (s < sizeof(pte)) {
		_kvm_err(kd, kd->program, "_i386_vatop_pae: pdpe_pa not found");
		goto invalid;
	}

	/* XXX This has to be a physical address read, kvm_read is virtual */
	if (pread(kd->pmfd, &pte, sizeof(pte), ofs) != sizeof(pte)) {
		_kvm_syserr(kd, kd->program, "_i386_vatop_pae: read");
		goto invalid;
	}
	pte = le64toh(pte);
	if ((pte & I386_PG_V) == 0) {
		_kvm_err(kd, kd->program, "_i386_vatop_pae: pte not valid");
		goto invalid;
	}

	a = (pte & I386_PG_FRAME_PAE) + offset;
	s = _kvm_pa2off(kd, a, pa);
	if (s == 0) {
		_kvm_err(kd, kd->program,
		    "_i386_vatop_pae: address not in dump");
		goto invalid;
	} else
		return (I386_PAGE_SIZE - offset);

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_i386_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}
	if (kd->vmst->pae)
		return (_i386_vatop_pae(kd, va, pa));
	else
		return (_i386_vatop(kd, va, pa));
}

int
_i386_native(kvm_t *kd __unused)
{

#ifdef __i386__
	return (1);
#else
	return (0);
#endif
}

static struct kvm_arch kvm_i386 = {
	.ka_probe = _i386_probe,
	.ka_initvtop = _i386_initvtop,
	.ka_freevtop = _i386_freevtop,
	.ka_kvatop = _i386_kvatop,
	.ka_native = _i386_native,
};

KVM_ARCH(kvm_i386);
