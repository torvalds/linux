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
 *
 *	from: FreeBSD: src/lib/libkvm/kvm_i386.c,v 1.15 2001/10/10 17:48:43
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
__SCCSID("@(#)kvm_hp300.c	8.1 (Berkeley) 6/4/93");

/*
 * sparc64 machine dependent routines for kvm.
 */

#include <sys/param.h>
#include <kvm.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../sys/sparc64/include/kerneldump.h"

#include "kvm_private.h"
#include "kvm_sparc64.h"

struct vmstate {
	off_t		vm_tsb_off;
	uint64_t	vm_tsb_mask;
	int		vm_nregions;
	struct sparc64_dump_reg	*vm_regions;
};

static int
_sparc64_probe(kvm_t *kd)
{

	return (_kvm_probe_elf_kernel(kd, ELFCLASS64, EM_SPARCV9));
}

static void
_sparc64_freevtop(kvm_t *kd)
{

	free(kd->vmst->vm_regions);
	free(kd->vmst);
	kd->vmst = NULL;
}

static int
_sparc64_read_phys(kvm_t *kd, off_t pos, void *buf, size_t size)
{

	/* XXX This has to be a raw file read, kvm_read is virtual. */
	if (pread(kd->pmfd, buf, size, pos) != (ssize_t)size) {
		_kvm_syserr(kd, kd->program, "_sparc64_read_phys: pread");
		return (0);
	}
	return (1);
}

static int
_sparc64_reg_cmp(const void *a, const void *b)
{
	const struct sparc64_dump_reg *ra, *rb;

	ra = a;
	rb = b;
	if (ra->dr_pa < rb->dr_pa)
		return (-1);
	else if (ra->dr_pa >= rb->dr_pa + rb->dr_size)
		return (1);
	else
		return (0);
}

#define	KVM_OFF_NOTFOUND	0

static off_t
_sparc64_find_off(struct vmstate *vm, uint64_t pa, uint64_t size)
{
	struct sparc64_dump_reg *reg, key;
	vm_offset_t o;

	key.dr_pa = pa;
	reg = bsearch(&key, vm->vm_regions, vm->vm_nregions,
	    sizeof(*vm->vm_regions), _sparc64_reg_cmp);
	if (reg == NULL)
		return (KVM_OFF_NOTFOUND);
	o = pa - reg->dr_pa;
	if (o + size > reg->dr_size)
		return (KVM_OFF_NOTFOUND);
	return (reg->dr_offs + o);
}

static int
_sparc64_initvtop(kvm_t *kd)
{
	struct sparc64_dump_hdr hdr;
	struct sparc64_dump_reg	*regs;
	struct vmstate *vm;
	size_t regsz;
	int i;

	vm = (struct vmstate *)_kvm_malloc(kd, sizeof(*vm));
	if (vm == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate vm");
		return (-1);
	}
	kd->vmst = vm;

	if (!_sparc64_read_phys(kd, 0, &hdr, sizeof(hdr)))
		goto fail_vm;
	hdr.dh_hdr_size = be64toh(hdr.dh_hdr_size);
	hdr.dh_tsb_pa = be64toh(hdr.dh_tsb_pa);
	hdr.dh_tsb_size = be64toh(hdr.dh_tsb_size);
	hdr.dh_tsb_mask = be64toh(hdr.dh_tsb_mask);
	hdr.dh_nregions = be32toh(hdr.dh_nregions);

	regsz = hdr.dh_nregions * sizeof(*regs);
	regs = _kvm_malloc(kd, regsz);
	if (regs == NULL) {
		_kvm_err(kd, kd->program, "cannot allocate regions");
		goto fail_vm;
	}
	if (!_sparc64_read_phys(kd, sizeof(hdr), regs, regsz))
		goto fail_regs;
	for (i = 0; i < hdr.dh_nregions; i++) {
		regs[i].dr_pa = be64toh(regs[i].dr_pa);
		regs[i].dr_size = be64toh(regs[i].dr_size);
		regs[i].dr_offs = be64toh(regs[i].dr_offs);
	}
	qsort(regs, hdr.dh_nregions, sizeof(*regs), _sparc64_reg_cmp);

	vm->vm_tsb_mask = hdr.dh_tsb_mask;
	vm->vm_regions = regs;
	vm->vm_nregions = hdr.dh_nregions;
	vm->vm_tsb_off = _sparc64_find_off(vm, hdr.dh_tsb_pa, hdr.dh_tsb_size);
	if (vm->vm_tsb_off == KVM_OFF_NOTFOUND) {
		_kvm_err(kd, kd->program, "tsb not found in dump");
		goto fail_regs;
	}
	return (0);

fail_regs:
	free(regs);
fail_vm:
	free(vm);
	return (-1);
}

static int
_sparc64_kvatop(kvm_t *kd, kvaddr_t va, off_t *pa)
{
	struct sparc64_tte tte;
	off_t tte_off;
	kvaddr_t vpn;
	off_t pa_off;
	kvaddr_t pg_off;
	int rest;

	pg_off = va & SPARC64_PAGE_MASK;
	if (va >= SPARC64_MIN_DIRECT_ADDRESS)
		pa_off = SPARC64_DIRECT_TO_PHYS(va) & ~SPARC64_PAGE_MASK;
	else {
		vpn = va >> SPARC64_PAGE_SHIFT;
		tte_off = kd->vmst->vm_tsb_off +
		    ((vpn & kd->vmst->vm_tsb_mask) << SPARC64_TTE_SHIFT);
		if (!_sparc64_read_phys(kd, tte_off, &tte, sizeof(tte)))
			goto invalid;
		tte.tte_vpn = be64toh(tte.tte_vpn);
		tte.tte_data = be64toh(tte.tte_data);
		if (!sparc64_tte_match(&tte, va))
			goto invalid;
		pa_off = SPARC64_TTE_GET_PA(&tte);
	}
	rest = SPARC64_PAGE_SIZE - pg_off;
	pa_off = _sparc64_find_off(kd->vmst, pa_off, rest);
	if (pa_off == KVM_OFF_NOTFOUND)
		goto invalid;
	*pa = pa_off + pg_off;
	return (rest);

invalid:
	_kvm_err(kd, 0, "invalid address (%jx)", (uintmax_t)va);
	return (0);
}

static int
_sparc64_native(kvm_t *kd __unused)
{

#ifdef __sparc64__
	return (1);
#else
	return (0);
#endif
}

static struct kvm_arch kvm_sparc64 = {
	.ka_probe = _sparc64_probe,
	.ka_initvtop = _sparc64_initvtop,
	.ka_freevtop = _sparc64_freevtop,
	.ka_kvatop = _sparc64_kvatop,
	.ka_native = _sparc64_native,
};

KVM_ARCH(kvm_sparc64);
