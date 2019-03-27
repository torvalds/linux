/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Semihalf, Grzegorz Bernacki
 * Copyright (c) 2006 Peter Wemm
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From: FreeBSD: src/lib/libkvm/kvm_minidump_i386.c,v 1.2 2006/06/05 08:51:14
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * ARM machine dependent routines for kvm and minidumps.
 */

#include <sys/endian.h>
#include <sys/param.h>
#include <vm/vm.h>
#include <kvm.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../sys/arm/include/minidump.h"

#include "kvm_private.h"
#include "kvm_arm.h"

#define	arm_round_page(x)	roundup2((kvaddr_t)(x), ARM_PAGE_SIZE)

struct vmstate {
	struct		minidumphdr hdr;
	unsigned char	ei_data;
};

static arm_pt_entry_t
_arm_pte_get(kvm_t *kd, u_long pteindex)
{
	arm_pt_entry_t *pte = _kvm_pmap_get(kd, pteindex, sizeof(*pte));

	return _kvm32toh(kd, *pte);
}

static int
_arm_minidump_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS32, EM_ARM) &&
	    _kvm_is_minidump(kd));
}

static void
_arm_minidump_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	free(vm);
	kd->vmst = NULL;
}

static int
_arm_minidump_initvtop(kvm_t *kd)
{
	struct vmstate *vmst;
	off_t off, sparse_off;

	vmst = _kvm_malloc(kd, sizeof(*vmst));
	if (vmst == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}

	kd->vmst = vmst;

	if (pread(kd->pmfd, &vmst->hdr,
	    sizeof(vmst->hdr), 0) != sizeof(vmst->hdr)) {
		_kvm_err(kd, kd->program, "cannot read dump header");
		return (-1);
	}

	if (strncmp(MINIDUMP_MAGIC, vmst->hdr.magic,
	    sizeof(vmst->hdr.magic)) != 0) {
		_kvm_err(kd, kd->program, "not a minidump for this platform");
		return (-1);
	}
	vmst->hdr.version = _kvm32toh(kd, vmst->hdr.version);
	if (vmst->hdr.version != MINIDUMP_VERSION) {
		_kvm_err(kd, kd->program, "wrong minidump version. "
		    "Expected %d got %d", MINIDUMP_VERSION, vmst->hdr.version);
		return (-1);
	}
	vmst->hdr.msgbufsize = _kvm32toh(kd, vmst->hdr.msgbufsize);
	vmst->hdr.bitmapsize = _kvm32toh(kd, vmst->hdr.bitmapsize);
	vmst->hdr.ptesize = _kvm32toh(kd, vmst->hdr.ptesize);
	vmst->hdr.kernbase = _kvm32toh(kd, vmst->hdr.kernbase);
	vmst->hdr.arch = _kvm32toh(kd, vmst->hdr.arch);
	vmst->hdr.mmuformat = _kvm32toh(kd, vmst->hdr.mmuformat);
	if (vmst->hdr.mmuformat == MINIDUMP_MMU_FORMAT_UNKNOWN) {
		/* This is a safe default as 1K pages are not used. */
		vmst->hdr.mmuformat = MINIDUMP_MMU_FORMAT_V6;
	}

	/* Skip header and msgbuf */
	off = ARM_PAGE_SIZE + arm_round_page(vmst->hdr.msgbufsize);

	sparse_off = off + arm_round_page(vmst->hdr.bitmapsize) +
	    arm_round_page(vmst->hdr.ptesize);
	if (_kvm_pt_init(kd, vmst->hdr.bitmapsize, off, sparse_off,
	    ARM_PAGE_SIZE, sizeof(uint32_t)) == -1) {
		return (-1);
	}
	off += arm_round_page(vmst->hdr.bitmapsize);

	if (_kvm_pmap_init(kd, vmst->hdr.ptesize, off) == -1) {
		return (-1);
	}
	off += arm_round_page(vmst->hdr.ptesize);

	return (0);
}

static int
_arm_minidump_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	arm_pt_entry_t pte;
	arm_physaddr_t offset, a;
	kvaddr_t pteindex;
	off_t ofs;

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "_arm_minidump_kvatop called in live kernel!");
		return (0);
	}

	vm = kd->vmst;

	if (va >= vm->hdr.kernbase) {
		pteindex = (va - vm->hdr.kernbase) >> ARM_PAGE_SHIFT;
		if (pteindex >= vm->hdr.ptesize / sizeof(pte))
			goto invalid;
		pte = _arm_pte_get(kd, pteindex);
		if ((pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_INV) {
			_kvm_err(kd, kd->program,
			    "_arm_minidump_kvatop: pte not valid");
			goto invalid;
		}
		if ((pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_L) {
			/* 64K page -> convert to be like 4K page */
			offset = va & ARM_L2_S_OFFSET;
			a = (pte & ARM_L2_L_FRAME) +
			    (va & ARM_L2_L_OFFSET & ARM_L2_S_FRAME);
		} else {
			if (kd->vmst->hdr.mmuformat == MINIDUMP_MMU_FORMAT_V4 &&
			    (pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_T) {
				_kvm_err(kd, kd->program,
				    "_arm_minidump_kvatop: pte not supported");
				goto invalid;
			}
			/* 4K page */
			offset = va & ARM_L2_S_OFFSET;
			a = pte & ARM_L2_S_FRAME;
		}

		ofs = _kvm_pt_find(kd, a, ARM_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program, "_arm_minidump_kvatop: "
			    "physical address 0x%jx not in minidump",
			    (uintmax_t)a);
			goto invalid;
		}

		*pa = ofs + offset;
		return (ARM_PAGE_SIZE - offset);
	} else
		_kvm_err(kd, kd->program, "_arm_minidump_kvatop: virtual "
		    "address 0x%jx not minidumped", (uintmax_t)va);

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static vm_prot_t
_arm_entry_to_prot(kvm_t *kd, arm_pt_entry_t pte)
{
	struct vmstate *vm = kd->vmst;
	vm_prot_t prot = VM_PROT_READ;

	/* Source: arm/arm/pmap-v4.c:pmap_fault_fixup() */
	if (vm->hdr.mmuformat == MINIDUMP_MMU_FORMAT_V4) {
		if (pte & ARM_L2_S_PROT_W)
			prot |= VM_PROT_WRITE;
		return prot;
	}

	/* Source: arm/arm/pmap-v6.c:pmap_protect() */
	if ((pte & ARM_PTE2_RO) == 0)
		prot |= VM_PROT_WRITE;
	if ((pte & ARM_PTE2_NX) == 0)
		prot |= VM_PROT_EXECUTE;
	return prot;
}

static int
_arm_minidump_walk_pages(kvm_t *kd, kvm_walk_pages_cb_t *cb, void *arg)
{
	struct vmstate *vm = kd->vmst;
	u_long nptes = vm->hdr.ptesize / sizeof(arm_pt_entry_t);
	u_long dva, pa, pteindex, va;

	for (pteindex = 0; pteindex < nptes; pteindex++) {
		arm_pt_entry_t pte = _arm_pte_get(kd, pteindex);

		if ((pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_INV)
			continue;

		va = vm->hdr.kernbase + (pteindex << ARM_PAGE_SHIFT);
		if ((pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_L) {
			/* 64K page */
			pa = (pte & ARM_L2_L_FRAME) +
			    (va & ARM_L2_L_OFFSET & ARM_L2_S_FRAME);
		} else {
			if (vm->hdr.mmuformat == MINIDUMP_MMU_FORMAT_V4 &&
			    (pte & ARM_L2_TYPE_MASK) == ARM_L2_TYPE_T) {
				continue;
			}
			/* 4K page */
			pa = pte & ARM_L2_S_FRAME;
		}

		dva = 0; /* no direct map on this platform */
		if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva,
		    _arm_entry_to_prot(kd, pte), ARM_PAGE_SIZE, 0))
			return (0);
	}
	return (1);
}

static struct kvm_arch kvm_arm_minidump = {
	.ka_probe = _arm_minidump_probe,
	.ka_initvtop = _arm_minidump_initvtop,
	.ka_freevtop = _arm_minidump_freevtop,
	.ka_kvatop = _arm_minidump_kvatop,
	.ka_native = _arm_native,
	.ka_walk_pages = _arm_minidump_walk_pages,
};

KVM_ARCH(kvm_arm_minidump);
