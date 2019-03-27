/*-
 * Copyright (c) 2006 Peter Wemm
 * Copyright (c) 2019 Mitchell Horne
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
 * From: FreeBSD: src/lib/libkvm/kvm_minidump_amd64.c r261799
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * RISC-V machine dependent routines for kvm and minidumps.
 */

#include <sys/param.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vm/vm.h>
#include <kvm.h>

#include "../../sys/riscv/include/minidump.h"

#include <limits.h>

#include "kvm_private.h"
#include "kvm_riscv.h"

#define	riscv_round_page(x)	roundup2((kvaddr_t)(x), RISCV_PAGE_SIZE)

struct vmstate {
	struct minidumphdr hdr;
};

static riscv_pt_entry_t
_riscv_pte_get(kvm_t *kd, u_long pteindex)
{
	riscv_pt_entry_t *pte = _kvm_pmap_get(kd, pteindex, sizeof(*pte));

	return le64toh(*pte);
}

static int
_riscv_minidump_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS64, EM_RISCV) &&
	    _kvm_is_minidump(kd));
}

static void
_riscv_minidump_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	free(vm);
	kd->vmst = NULL;
}

static int
_riscv_minidump_initvtop(kvm_t *kd)
{
	struct vmstate *vmst;
	off_t off, sparse_off;

	vmst = _kvm_malloc(kd, sizeof(*vmst));
	if (vmst == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst = vmst;
	if (pread(kd->pmfd, &vmst->hdr, sizeof(vmst->hdr), 0) !=
	    sizeof(vmst->hdr)) {
		_kvm_err(kd, kd->program, "cannot read dump header");
		return (-1);
	}
	if (strncmp(MINIDUMP_MAGIC, vmst->hdr.magic,
	    sizeof(vmst->hdr.magic)) != 0) {
		_kvm_err(kd, kd->program, "not a minidump for this platform");
		return (-1);
	}

	vmst->hdr.version = le32toh(vmst->hdr.version);
	if (vmst->hdr.version != MINIDUMP_VERSION) {
		_kvm_err(kd, kd->program, "wrong minidump version. "
		    "Expected %d got %d", MINIDUMP_VERSION, vmst->hdr.version);
		return (-1);
	}
	vmst->hdr.msgbufsize = le32toh(vmst->hdr.msgbufsize);
	vmst->hdr.bitmapsize = le32toh(vmst->hdr.bitmapsize);
	vmst->hdr.pmapsize = le32toh(vmst->hdr.pmapsize);
	vmst->hdr.kernbase = le64toh(vmst->hdr.kernbase);
	vmst->hdr.dmapphys = le64toh(vmst->hdr.dmapphys);
	vmst->hdr.dmapbase = le64toh(vmst->hdr.dmapbase);
	vmst->hdr.dmapend = le64toh(vmst->hdr.dmapend);

	/* Skip header and msgbuf */
	off = RISCV_PAGE_SIZE + riscv_round_page(vmst->hdr.msgbufsize);

	/* build physical address lookup table for sparse pages */
	sparse_off = off + riscv_round_page(vmst->hdr.bitmapsize) +
	    riscv_round_page(vmst->hdr.pmapsize);
	if (_kvm_pt_init(kd, vmst->hdr.bitmapsize, off, sparse_off,
	    RISCV_PAGE_SIZE, sizeof(uint64_t)) == -1) {
		return (-1);
	}
	off += riscv_round_page(vmst->hdr.bitmapsize);

	if (_kvm_pmap_init(kd, vmst->hdr.pmapsize, off) == -1) {
		return (-1);
	}
	off += riscv_round_page(vmst->hdr.pmapsize);

	return (0);
}

static int
_riscv_minidump_vatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	riscv_physaddr_t offset;
	riscv_pt_entry_t l3;
	kvaddr_t l3_index;
	riscv_physaddr_t a;
	off_t ofs;

	vm = kd->vmst;
	offset = va & RISCV_PAGE_MASK;

	if (va >= vm->hdr.dmapbase && va < vm->hdr.dmapend) {
		a = (va - vm->hdr.dmapbase + vm->hdr.dmapphys) &
		    ~RISCV_PAGE_MASK;
		ofs = _kvm_pt_find(kd, a, RISCV_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program, "_riscv_minidump_vatop: "
			    "direct map address 0x%jx not in minidump",
			    (uintmax_t)va);
			goto invalid;
		}
		*pa = ofs + offset;
		return (RISCV_PAGE_SIZE - offset);
	} else if (va >= vm->hdr.kernbase) {
		l3_index = (va - vm->hdr.kernbase) >> RISCV_L3_SHIFT;
		if (l3_index >= vm->hdr.pmapsize / sizeof(l3))
			goto invalid;
		l3 = _riscv_pte_get(kd, l3_index);
		if ((l3 & RISCV_PTE_V) == 0 || (l3 & RISCV_PTE_RWX) == 0) {
			_kvm_err(kd, kd->program,
			    "_riscv_minidump_vatop: pte not valid");
			goto invalid;
		}
		a = (l3 >> RISCV_PTE_PPN0_S) << RISCV_L3_SHIFT;
		ofs = _kvm_pt_find(kd, a, RISCV_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program, "_riscv_minidump_vatop: "
			    "physical address 0x%jx not in minidump",
			    (uintmax_t)a);
			goto invalid;
		}
		*pa = ofs + offset;
		return (RISCV_PAGE_SIZE - offset);
	} else {
		_kvm_err(kd, kd->program,
	    "_riscv_minidump_vatop: virtual address 0x%jx not minidumped",
		    (uintmax_t)va);
		goto invalid;
	}

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_riscv_minidump_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0,
		    "_riscv_minidump_kvatop called in live kernel!");
		return (0);
	}
	return (_riscv_minidump_vatop(kd, va, pa));
}

static int
_riscv_native(kvm_t *kd __unused)
{

#ifdef __riscv
	return (1);
#else
	return (0);
#endif
}

static vm_prot_t
_riscv_entry_to_prot(riscv_pt_entry_t pte)
{
	vm_prot_t prot = VM_PROT_READ;

	if ((pte & RISCV_PTE_W) != 0)
		prot |= VM_PROT_WRITE;
	if ((pte & RISCV_PTE_X) != 0)
		prot |= VM_PROT_EXECUTE;
	return prot;
}

static int
_riscv_minidump_walk_pages(kvm_t *kd, kvm_walk_pages_cb_t *cb, void *arg)
{
	struct vmstate *vm = kd->vmst;
	u_long nptes = vm->hdr.pmapsize / sizeof(riscv_pt_entry_t);
	u_long bmindex, dva, pa, pteindex, va;
	struct kvm_bitmap bm;
	vm_prot_t prot;
	int ret = 0;

	if (!_kvm_bitmap_init(&bm, vm->hdr.bitmapsize, &bmindex))
		return (0);

	for (pteindex = 0; pteindex < nptes; pteindex++) {
		riscv_pt_entry_t pte = _riscv_pte_get(kd, pteindex);

		if (((pte & RISCV_PTE_V) == 0) ||
		    ((pte & RISCV_PTE_RWX) == 0))
			continue;

		va = vm->hdr.kernbase + (pteindex << RISCV_L3_SHIFT);
		pa = (pte >> RISCV_PTE_PPN0_S) << RISCV_L3_SHIFT;
		dva = vm->hdr.dmapbase + pa;
		if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva,
		    _riscv_entry_to_prot(pte), RISCV_PAGE_SIZE, 0)) {
			goto out;
		}
	}

	while (_kvm_bitmap_next(&bm, &bmindex)) {
		pa = bmindex * RISCV_PAGE_SIZE;
		dva = vm->hdr.dmapbase + pa;
		if (vm->hdr.dmapend < (dva + RISCV_PAGE_SIZE))
			break;
		va = 0;
		prot = VM_PROT_READ | VM_PROT_WRITE;
		if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva,
		    prot, RISCV_PAGE_SIZE, 0)) {
			goto out;
		}
	}
	ret = 1;

out:
	_kvm_bitmap_deinit(&bm);
	return (ret);
}

static struct kvm_arch kvm_riscv_minidump = {
	.ka_probe = _riscv_minidump_probe,
	.ka_initvtop = _riscv_minidump_initvtop,
	.ka_freevtop = _riscv_minidump_freevtop,
	.ka_kvatop = _riscv_minidump_kvatop,
	.ka_native = _riscv_native,
	.ka_walk_pages = _riscv_minidump_walk_pages,
};

KVM_ARCH(kvm_riscv_minidump);
