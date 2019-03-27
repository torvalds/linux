/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * AMD64 machine dependent routines for kvm and minidumps.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vm/vm.h>
#include <kvm.h>

#include "../../sys/amd64/include/minidump.h"

#include <limits.h>

#include "kvm_private.h"
#include "kvm_amd64.h"

#define	amd64_round_page(x)	roundup2((kvaddr_t)(x), AMD64_PAGE_SIZE)
#define	VM_IS_V1(vm)		(vm->hdr.version == 1)
#define	VA_OFF(vm, va)		\
	(VM_IS_V1(vm) ? ((va) & (AMD64_PAGE_SIZE - 1)) : ((va) & AMD64_PAGE_MASK))

struct vmstate {
	struct minidumphdr hdr;
};

static vm_prot_t
_amd64_entry_to_prot(uint64_t entry)
{
	vm_prot_t prot = VM_PROT_READ;

	if ((entry & AMD64_PG_RW) != 0)
		prot |= VM_PROT_WRITE;
	if ((entry & AMD64_PG_NX) == 0)
		prot |= VM_PROT_EXECUTE;
	return prot;
}

/*
 * Version 2 minidumps use page directory entries, while version 1 use page
 * table entries.
 */

static amd64_pde_t
_amd64_pde_get(kvm_t *kd, u_long pdeindex)
{
	amd64_pde_t *pde = _kvm_pmap_get(kd, pdeindex, sizeof(*pde));

	return le64toh(*pde);
}

static amd64_pte_t
_amd64_pte_get(kvm_t *kd, u_long pteindex)
{
	amd64_pte_t *pte = _kvm_pmap_get(kd, pteindex, sizeof(*pte));

	return le64toh(*pte);
}

/* Get the first page table entry for a given page directory index. */
static amd64_pte_t *
_amd64_pde_first_pte(kvm_t *kd, u_long pdeindex)
{
	u_long *pa;

	pa = _kvm_pmap_get(kd, pdeindex, sizeof(amd64_pde_t));
	if (pa == NULL)
		return NULL;
	return _kvm_map_get(kd, *pa & AMD64_PG_FRAME, AMD64_PAGE_SIZE);
}

static int
_amd64_minidump_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS64, EM_X86_64) &&
	    _kvm_is_minidump(kd));
}

static void
_amd64_minidump_freevtop(kvm_t *kd)
{
	struct vmstate *vm = kd->vmst;

	free(vm);
	kd->vmst = NULL;
}

static int
_amd64_minidump_initvtop(kvm_t *kd)
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
	if (strncmp(MINIDUMP_MAGIC, vmst->hdr.magic, sizeof(vmst->hdr.magic)) != 0) {
		_kvm_err(kd, kd->program, "not a minidump for this platform");
		return (-1);
	}

	/*
	 * NB: amd64 minidump header is binary compatible between version 1
	 * and version 2; this may not be the case for the future versions.
	 */
	vmst->hdr.version = le32toh(vmst->hdr.version);
	if (vmst->hdr.version != MINIDUMP_VERSION && vmst->hdr.version != 1) {
		_kvm_err(kd, kd->program, "wrong minidump version. expected %d got %d",
		    MINIDUMP_VERSION, vmst->hdr.version);
		return (-1);
	}
	vmst->hdr.msgbufsize = le32toh(vmst->hdr.msgbufsize);
	vmst->hdr.bitmapsize = le32toh(vmst->hdr.bitmapsize);
	vmst->hdr.pmapsize = le32toh(vmst->hdr.pmapsize);
	vmst->hdr.kernbase = le64toh(vmst->hdr.kernbase);
	vmst->hdr.dmapbase = le64toh(vmst->hdr.dmapbase);
	vmst->hdr.dmapend = le64toh(vmst->hdr.dmapend);

	/* Skip header and msgbuf */
	off = AMD64_PAGE_SIZE + amd64_round_page(vmst->hdr.msgbufsize);

	sparse_off = off + amd64_round_page(vmst->hdr.bitmapsize) +
	    amd64_round_page(vmst->hdr.pmapsize);
	if (_kvm_pt_init(kd, vmst->hdr.bitmapsize, off, sparse_off,
	    AMD64_PAGE_SIZE, sizeof(uint64_t)) == -1) {
		return (-1);
	}
	off += amd64_round_page(vmst->hdr.bitmapsize);

	if (_kvm_pmap_init(kd, vmst->hdr.pmapsize, off) == -1) {
		return (-1);
	}
	off += amd64_round_page(vmst->hdr.pmapsize);

	return (0);
}

static int
_amd64_minidump_vatop_v1(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct vmstate *vm;
	amd64_physaddr_t offset;
	amd64_pte_t pte;
	kvaddr_t pteindex;
	amd64_physaddr_t a;
	off_t ofs;

	vm = kd->vmst;
	offset = va & AMD64_PAGE_MASK;

	if (va >= vm->hdr.kernbase) {
		pteindex = (va - vm->hdr.kernbase) >> AMD64_PAGE_SHIFT;
		if (pteindex >= vm->hdr.pmapsize / sizeof(pte))
			goto invalid;
		pte = _amd64_pte_get(kd, pteindex);
		if ((pte & AMD64_PG_V) == 0) {
			_kvm_err(kd, kd->program,
			    "_amd64_minidump_vatop_v1: pte not valid");
			goto invalid;
		}
		a = pte & AMD64_PG_FRAME;
		ofs = _kvm_pt_find(kd, a, AMD64_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program,
	    "_amd64_minidump_vatop_v1: physical address 0x%jx not in minidump",
			    (uintmax_t)a);
			goto invalid;
		}
		*pa = ofs + offset;
		return (AMD64_PAGE_SIZE - offset);
	} else if (va >= vm->hdr.dmapbase && va < vm->hdr.dmapend) {
		a = (va - vm->hdr.dmapbase) & ~AMD64_PAGE_MASK;
		ofs = _kvm_pt_find(kd, a, AMD64_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program,
    "_amd64_minidump_vatop_v1: direct map address 0x%jx not in minidump",
			    (uintmax_t)va);
			goto invalid;
		}
		*pa = ofs + offset;
		return (AMD64_PAGE_SIZE - offset);
	} else {
		_kvm_err(kd, kd->program,
	    "_amd64_minidump_vatop_v1: virtual address 0x%jx not minidumped",
		    (uintmax_t)va);
		goto invalid;
	}

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_amd64_minidump_vatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	amd64_pte_t pt[AMD64_NPTEPG];
	struct vmstate *vm;
	amd64_physaddr_t offset;
	amd64_pde_t pde;
	amd64_pte_t pte;
	kvaddr_t pteindex;
	kvaddr_t pdeindex;
	amd64_physaddr_t a;
	off_t ofs;

	vm = kd->vmst;
	offset = va & AMD64_PAGE_MASK;

	if (va >= vm->hdr.kernbase) {
		pdeindex = (va - vm->hdr.kernbase) >> AMD64_PDRSHIFT;
		if (pdeindex >= vm->hdr.pmapsize / sizeof(pde))
			goto invalid;
		pde = _amd64_pde_get(kd, pdeindex);
		if ((pde & AMD64_PG_V) == 0) {
			_kvm_err(kd, kd->program,
			    "_amd64_minidump_vatop: pde not valid");
			goto invalid;
		}
		if ((pde & AMD64_PG_PS) == 0) {
			a = pde & AMD64_PG_FRAME;
			/* TODO: Just read the single PTE */
			ofs = _kvm_pt_find(kd, a, AMD64_PAGE_SIZE);
			if (ofs == -1) {
				_kvm_err(kd, kd->program,
				    "cannot find page table entry for %ju",
				    (uintmax_t)a);
				goto invalid;
			}
			if (pread(kd->pmfd, &pt, AMD64_PAGE_SIZE, ofs) !=
			    AMD64_PAGE_SIZE) {
				_kvm_err(kd, kd->program,
				    "cannot read page table entry for %ju",
				    (uintmax_t)a);
				goto invalid;
			}
			pteindex = (va >> AMD64_PAGE_SHIFT) &
			    (AMD64_NPTEPG - 1);
			pte = le64toh(pt[pteindex]);
			if ((pte & AMD64_PG_V) == 0) {
				_kvm_err(kd, kd->program,
				    "_amd64_minidump_vatop: pte not valid");
				goto invalid;
			}
			a = pte & AMD64_PG_FRAME;
		} else {
			a = pde & AMD64_PG_PS_FRAME;
			a += (va & AMD64_PDRMASK) ^ offset;
		}
		ofs = _kvm_pt_find(kd, a, AMD64_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program,
	    "_amd64_minidump_vatop: physical address 0x%jx not in minidump",
			    (uintmax_t)a);
			goto invalid;
		}
		*pa = ofs + offset;
		return (AMD64_PAGE_SIZE - offset);
	} else if (va >= vm->hdr.dmapbase && va < vm->hdr.dmapend) {
		a = (va - vm->hdr.dmapbase) & ~AMD64_PAGE_MASK;
		ofs = _kvm_pt_find(kd, a, AMD64_PAGE_SIZE);
		if (ofs == -1) {
			_kvm_err(kd, kd->program,
	    "_amd64_minidump_vatop: direct map address 0x%jx not in minidump",
			    (uintmax_t)va);
			goto invalid;
		}
		*pa = ofs + offset;
		return (AMD64_PAGE_SIZE - offset);
	} else {
		_kvm_err(kd, kd->program,
	    "_amd64_minidump_vatop: virtual address 0x%jx not minidumped",
		    (uintmax_t)va);
		goto invalid;
	}

invalid:
	_kvm_err(kd, 0, "invalid address (0x%jx)", (uintmax_t)va);
	return (0);
}

static int
_amd64_minidump_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0,
		    "_amd64_minidump_kvatop called in live kernel!");
		return (0);
	}
	if (((struct vmstate *)kd->vmst)->hdr.version == 1)
		return (_amd64_minidump_vatop_v1(kd, va, pa));
	else
		return (_amd64_minidump_vatop(kd, va, pa));
}

static int
_amd64_minidump_walk_pages(kvm_t *kd, kvm_walk_pages_cb_t *cb, void *arg)
{
	struct vmstate *vm = kd->vmst;
	u_long npdes = vm->hdr.pmapsize / sizeof(amd64_pde_t);
	u_long bmindex, dva, pa, pdeindex, va;
	struct kvm_bitmap bm;
	int ret = 0;
	vm_prot_t prot;
	unsigned int pgsz = AMD64_PAGE_SIZE;

	if (vm->hdr.version < 2)
		return (0);

	if (!_kvm_bitmap_init(&bm, vm->hdr.bitmapsize, &bmindex))
		return (0);

	for (pdeindex = 0; pdeindex < npdes; pdeindex++) {
		amd64_pde_t pde = _amd64_pde_get(kd, pdeindex);
		amd64_pte_t *ptes;
		u_long i;

		va = vm->hdr.kernbase + (pdeindex << AMD64_PDRSHIFT);
		if ((pde & AMD64_PG_V) == 0)
			continue;

		if ((pde & AMD64_PG_PS) != 0) {
			/*
			 * Large page.  Iterate on each 4K page section
			 * within this page.  This differs from 4K pages in
			 * that every page here uses the same PDE to
			 * generate permissions.
			 */
			pa = (pde & AMD64_PG_PS_FRAME) +
			    ((va & AMD64_PDRMASK) ^ VA_OFF(vm, va));
			dva = vm->hdr.dmapbase + pa;
			_kvm_bitmap_set(&bm, pa, AMD64_PAGE_SIZE);
			if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva,
			    _amd64_entry_to_prot(pde), AMD64_NBPDR, pgsz)) {
				goto out;
			}
			continue;
		}

		/* 4K pages: pde references another page of entries. */
		ptes = _amd64_pde_first_pte(kd, pdeindex);
		/* Ignore page directory pages that were not dumped. */
		if (ptes == NULL)
			continue;

		for (i = 0; i < AMD64_NPTEPG; i++) {
			amd64_pte_t pte = (u_long)ptes[i];

			pa = pte & AMD64_PG_FRAME;
			dva = vm->hdr.dmapbase + pa;
			if ((pte & AMD64_PG_V) != 0) {
				_kvm_bitmap_set(&bm, pa, AMD64_PAGE_SIZE);
				if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva,
				    _amd64_entry_to_prot(pte), pgsz, 0)) {
					goto out;
				}
			}
			va += AMD64_PAGE_SIZE;
		}
	}

	while (_kvm_bitmap_next(&bm, &bmindex)) {
		pa = bmindex * AMD64_PAGE_SIZE;
		dva = vm->hdr.dmapbase + pa;
		if (vm->hdr.dmapend < (dva + pgsz))
			break;
		va = 0;
		/* amd64/pmap.c: create_pagetables(): dmap always R|W. */
		prot = VM_PROT_READ | VM_PROT_WRITE;
		if (!_kvm_visit_cb(kd, cb, arg, pa, va, dva, prot, pgsz, 0)) {
			goto out;
		}
	}

	ret = 1;

out:
	_kvm_bitmap_deinit(&bm);
	return (ret);
}

static struct kvm_arch kvm_amd64_minidump = {
	.ka_probe = _amd64_minidump_probe,
	.ka_initvtop = _amd64_minidump_initvtop,
	.ka_freevtop = _amd64_minidump_freevtop,
	.ka_kvatop = _amd64_minidump_kvatop,
	.ka_native = _amd64_native,
	.ka_walk_pages = _amd64_minidump_walk_pages,
};

KVM_ARCH(kvm_amd64_minidump);
