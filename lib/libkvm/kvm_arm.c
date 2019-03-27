/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 Olivier Houchard
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
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ARM machine dependent routines for kvm.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <kvm.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __arm__
#include <machine/vmparam.h>
#endif

#include "kvm_private.h"
#include "kvm_arm.h"

struct vmstate {
	arm_pd_entry_t *l1pt;
	size_t phnum;
	GElf_Phdr *phdr;
};

/*
 * Translate a physical memory address to a file-offset in the crash-dump.
 */
static size_t
_kvm_pa2off(kvm_t *kd, uint64_t pa, off_t *ofs, size_t pgsz)
{
	struct vmstate *vm = kd->vmst;
	GElf_Phdr *p;
	size_t n;

	p = vm->phdr;
	n = vm->phnum;
	while (n && (pa < p->p_paddr || pa >= p->p_paddr + p->p_memsz))
		p++, n--;
	if (n == 0)
		return (0);

	*ofs = (pa - p->p_paddr) + p->p_offset;
	if (pgsz == 0)
		return (p->p_memsz - (pa - p->p_paddr));
	return (pgsz - ((size_t)pa & (pgsz - 1)));
}

static void
_arm_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	free(vm->phdr);
	free(vm);
	kd->vmst = NULL;
}

static int
_arm_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS32, EM_ARM) &&
	    !_kvm_is_minidump(kd));
}

static int
_arm_initvtop(kvm_t *kd)
{
	struct vmstate *vm;
	struct kvm_nlist nl[2];
	kvaddr_t kernbase;
	arm_physaddr_t physaddr, pa;
	arm_pd_entry_t *l1pt;
	size_t i;
	int found;

	if (kd->rawdump) {
		_kvm_err(kd, kd->program, "raw dumps not supported on arm");
		return (-1);
	}

	vm = _kvm_malloc(kd, sizeof(*vm));
	if (vm == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst = vm;
	vm->l1pt = NULL;

	if (_kvm_read_core_phdrs(kd, &vm->phnum, &vm->phdr) == -1)
		return (-1);

	found = 0;
	for (i = 0; i < vm->phnum; i++) {
		if (vm->phdr[i].p_type == PT_DUMP_DELTA) {
			kernbase = vm->phdr[i].p_vaddr;
			physaddr = vm->phdr[i].p_paddr;
			found = 1;
			break;
		}
	}

	nl[1].n_name = NULL;
	if (!found) {
		nl[0].n_name = "kernbase";
		if (kvm_nlist2(kd, nl) != 0) {
#ifdef __arm__
			kernbase = KERNBASE;
#else
		_kvm_err(kd, kd->program, "cannot resolve kernbase");
		return (-1);
#endif
		} else
			kernbase = nl[0].n_value;

		nl[0].n_name = "physaddr";
		if (kvm_nlist2(kd, nl) != 0) {
			_kvm_err(kd, kd->program, "couldn't get phys addr");
			return (-1);
		}
		physaddr = nl[0].n_value;
	}
	nl[0].n_name = "kernel_l1pa";
	if (kvm_nlist2(kd, nl) != 0) {
		_kvm_err(kd, kd->program, "bad namelist");
		return (-1);
	}
	if (kvm_read2(kd, (nl[0].n_value - kernbase + physaddr), &pa,
	    sizeof(pa)) != sizeof(pa)) {
		_kvm_err(kd, kd->program, "cannot read kernel_l1pa");
		return (-1);
	}
	l1pt = _kvm_malloc(kd, ARM_L1_TABLE_SIZE);
	if (l1pt == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate l1pt");
		return (-1);
	}
	if (kvm_read2(kd, pa, l1pt, ARM_L1_TABLE_SIZE) != ARM_L1_TABLE_SIZE) {
		_kvm_err(kd, kd->program, "cannot read l1pt");
		free(l1pt);
		return (-1);
	}
	vm->l1pt = l1pt;
	return 0;
}

/* from arm/pmap.c */
#define	ARM_L1_IDX(va)		((va) >> ARM_L1_S_SHIFT)

#define	l1pte_section_p(pde)	(((pde) & ARM_L1_TYPE_MASK) == ARM_L1_TYPE_S)
#define	l1pte_valid(pde)	((pde) != 0)
#define	l2pte_valid(pte)	((pte) != 0)
#define l2pte_index(v)		(((v) & ARM_L1_S_OFFSET) >> ARM_L2_S_SHIFT)


static int
_arm_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm = kd->vmst;
	arm_pd_entry_t pd;
	arm_pt_entry_t pte;
	arm_physaddr_t pte_pa;
	off_t pte_off;

	if (vm->l1pt == NULL)
		return (_kvm_pa2off(kd, va, pa, ARM_PAGE_SIZE));
	pd = _kvm32toh(kd, vm->l1pt[ARM_L1_IDX(va)]);
	if (!l1pte_valid(pd))
		goto invalid;
	if (l1pte_section_p(pd)) {
		/* 1MB section mapping. */
		*pa = (pd & ARM_L1_S_ADDR_MASK) + (va & ARM_L1_S_OFFSET);
		return  (_kvm_pa2off(kd, *pa, pa, ARM_L1_S_SIZE));
	}
	pte_pa = (pd & ARM_L1_C_ADDR_MASK) + l2pte_index(va) * sizeof(pte);
	_kvm_pa2off(kd, pte_pa, &pte_off, ARM_L1_S_SIZE);
	if (pread(kd->pmfd, &pte, sizeof(pte), pte_off) != sizeof(pte)) {
		_kvm_syserr(kd, kd->program, "_arm_kvatop: pread");
		goto invalid;
	}
	pte = _kvm32toh(kd, pte);
	if (!l2pte_valid(pte)) {
		goto invalid;
	}
	if ((pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_L) {
		*pa = (pte & ARM_L2_L_FRAME) | (va & ARM_L2_L_OFFSET);
		return (_kvm_pa2off(kd, *pa, pa, ARM_L2_L_SIZE));
	}
	*pa = (pte & ARM_L2_S_FRAME) | (va & ARM_L2_S_OFFSET);
	return (_kvm_pa2off(kd, *pa, pa, ARM_PAGE_SIZE));
invalid:
	_kvm_err(kd, 0, "Invalid address (%jx)", (uintmax_t)va);
	return 0;
}

/*
 * Machine-dependent initialization for ALL open kvm descriptors,
 * not just those for a kernel crash dump.  Some architectures
 * have to deal with these NOT being constants!  (i.e. m68k)
 */
#ifdef FBSD_NOT_YET
int
_kvm_mdopen(kvm_t *kd)
{

	kd->usrstack = USRSTACK;
	kd->min_uva = VM_MIN_ADDRESS;
	kd->max_uva = VM_MAXUSER_ADDRESS;

	return (0);
}
#endif

int
#ifdef __arm__
_arm_native(kvm_t *kd)
#else
_arm_native(kvm_t *kd __unused)
#endif
{

#ifdef __arm__
#if _BYTE_ORDER == _LITTLE_ENDIAN
	return (kd->nlehdr.e_ident[EI_DATA] == ELFDATA2LSB);
#else
	return (kd->nlehdr.e_ident[EI_DATA] == ELFDATA2MSB);
#endif
#else
	return (0);
#endif
}

static struct kvm_arch kvm_arm = {
	.ka_probe = _arm_probe,
	.ka_initvtop = _arm_initvtop,
	.ka_freevtop = _arm_freevtop,
	.ka_kvatop = _arm_kvatop,
	.ka_native = _arm_native,
};

KVM_ARCH(kvm_arm);
