/*	$OpenBSD: kvm_riscv64.c,v 1.3 2021/12/01 21:45:19 deraadt Exp $	*/
/*
 * Copyright (c) 2006 Miodrag Vallat.
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
/*-
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * RISCV machine dependent routines for kvm.
 */

#include <sys/types.h>
#include <sys/kcore.h>

#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <kvm.h>

#include <db.h>

#include "kvm_private.h"

#include <machine/kcore.h>

void
_kvm_freevtop(kvm_t *kd)
{
	free(kd->vmst);
	kd->vmst = NULL;
}

int
_kvm_initvtop(kvm_t *kd)
{
	return (0);
}

/*
 * Translate a kernel virtual address to a physical address by walking
 * the kernels page table.
 */

int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	cpu_kcore_hdr_t *cpup = kd->cpu_data;

	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}

	/*
	 * This relies upon the kernel text and data being contiguous
	 * in the first memory segment.
	 * Other virtual addresses are not reachable yet.
	 */

	if (va >= cpup->kernelbase + cpup->kerneloffs &&
	    va < cpup->kernelbase + cpup->kerneloffs + cpup->staticsize) {
		*pa = (va - cpup->kernelbase) +
		    (paddr_t)cpup->ram_segs[0].start;
		return (int)(kd->nbpg - (va & (kd->nbpg - 1)));
	}

	_kvm_err(kd, 0, "kvm_vatop: va %lx unreachable", va);
	return (0);
}

off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	cpu_kcore_hdr_t *cpup = kd->cpu_data;
	phys_ram_seg_t *mp = cpup->ram_segs;
	off_t off = 0;
	int block;

	for (block = 0; block < NPHYS_RAM_SEGS; block++, mp++) {
		if (pa >= mp->start && pa < mp->start + mp->size)
			return (kd->dump_off + off +
			    (off_t)(pa - (paddr_t)mp->start));
		off += (off_t)mp->size;
	}

	_kvm_err(kd, 0, "not a physical address: %lx", pa);
	return (-1);
}
