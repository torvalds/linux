/*	$OpenBSD: kvm_sh.c,v 1.9 2021/12/01 21:45:19 deraadt Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/kcore.h>

#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <kvm.h>

#include <db.h>

#include "kvm_private.h"

#include <machine/cpu.h>
#include <machine/kcore.h>
#include <machine/pte.h>
#include <machine/vmparam.h>

void
_kvm_freevtop(kvm_t *kd)
{
}

int
_kvm_initvtop(kvm_t *kd)
{
	return (0);
}

/*
 * Translate a kernel virtual address to a physical address by walking
 * the kernel page tables.
 */

/* Stolen from sys/arch/sh/include/pmap.h we can't really include */
#define	__PMAP_PTP_N		512	/* # of page table page maps 2GB. */
/* Stolen from sys/arch/sh/sh/pmap.c */
#define	__PMAP_PTP_SHIFT	22
#define	__PMAP_PTP_PG_N		(kd->nbpg / sizeof(pt_entry_t))
#define	__PMAP_PTP_INDEX(va)	(((va) >> __PMAP_PTP_SHIFT) & (__PMAP_PTP_N - 1))
#define	__PMAP_PTP_OFSET(va)	((va / kd->nbpg) & (__PMAP_PTP_PG_N - 1))

int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	cpu_kcore_hdr_t *h = kd->cpu_data;
	u_int l1idx, l2idx;
	vaddr_t l2va;
	pt_entry_t l1pte, l2pte;
	off_t pteoffset;

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}

	/*
	 * P1 and P2 segments addresses are trivial.
	 */
	if (va >= SH3_P1SEG_BASE && va <= SH3_P1SEG_END) {
		*pa = SH3_P1SEG_TO_PHYS(va);
		return (int)((vaddr_t)SH3_P1SEG_END + 1 - va);
	}
	if (va >= SH3_P2SEG_BASE && va <= SH3_P2SEG_END) {
		*pa = SH3_P2SEG_TO_PHYS(va);
		return (int)((vaddr_t)SH3_P2SEG_END + 1 - va);
	}

	/*
	 * P3 segment addresses need kernel page table walk.
	 */
	if (va >= SH3_P3SEG_BASE && va < SH3_P3SEG_END) {
		l1idx = __PMAP_PTP_INDEX(va - VM_MIN_KERNEL_ADDRESS);
		l2idx = __PMAP_PTP_OFSET(va);

		/* read level 1 pte */
		pteoffset = h->kcore_kptp + sizeof(pt_entry_t) * l1idx;
		if (_kvm_pread(kd, kd->pmfd, (char *)&l1pte, sizeof(l1pte),
		    _kvm_pa2off(kd, pteoffset)) != sizeof(l1pte)) {
			_kvm_syserr(kd, 0, "could not read level 1 pte");
			goto bad;
		}

		/* check pte for validity */
		if ((l1pte & PG_V) == 0) {
			_kvm_err(kd, 0, "invalid level 1 pte: no valid bit");
			goto bad;
		}

		l2va = l1pte & PG_PPN;
		if (l2va < SH3_P1SEG_BASE || l2va > SH3_P1SEG_END) {
			_kvm_err(kd, 0, "invalid level 1 pte: out of P1");
			goto bad;
		}

		/* read level 2 pte */
		pteoffset = SH3_P1SEG_TO_PHYS(l2va) +
		    sizeof(pt_entry_t) * l2idx;
		if (_kvm_pread(kd, kd->pmfd, (char *)&l2pte, sizeof(l2pte),
		    _kvm_pa2off(kd, pteoffset)) != sizeof(l2pte)) {
			_kvm_syserr(kd, 0, "could not read level 2 pte");
			goto bad;
		}

		/* check pte for validity */
		if ((l2pte & PG_V) == 0) {
			_kvm_err(kd, 0, "invalid level 2 pte: no valid bit");
			goto bad;
		}

		*pa = (l2pte & PG_PPN) | (va & (kd->nbpg - 1));
		return (kd->nbpg - (va & (kd->nbpg - 1)));
	}

	/*
	 * All other addresses are incorrect.
	 */
	_kvm_err(kd, 0, "not a kernel virtual address");
bad:
	*pa = (paddr_t)-1;
	return (0);
}

/*
 * Translate a physical address to a file offset in the crash dump.
 */
off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	cpu_kcore_hdr_t *h = kd->cpu_data;
	phys_ram_seg_t *seg = h->kcore_segs;
	off_t off = kd->dump_off;
	u_int i;

	for (i = h->kcore_nsegs; i != 0; i--) {
		if (pa >= seg->start && pa < seg->start + seg->size)
			return (off + (pa - seg->start));
		off += seg->size;
	}

	_kvm_err(kd, 0, "physical address out of the image (%lx)", pa);
	return (0);
}
