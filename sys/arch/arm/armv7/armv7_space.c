/*	$OpenBSD: armv7_space.c,v 1.11 2022/03/06 12:16:27 kettenis Exp $ */

/*
 * Copyright (c) 2001, 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1997 Mark Brinicombe.
 * Copyright (c) 1997 Causality Limited.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ichiro FUKUHARA.
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
 *	This product includes software developed by Mark Brinicombe.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * bus_space functions for Intel PXA2[51]0 application processor.
 * Derived from i80321_space.c.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

/* Prototypes for all the bus_space structure functions */
bs_protos(armv7);
bs_protos(generic);
bs_protos(bs_notimpl);

struct bus_space armv7_bs_tag = {
	/* cookie */
	(void *) 0,

	/* mapping/unmapping */
	armv7_bs_map,
	armv7_bs_unmap,
	armv7_bs_subregion,

	/* allocation/deallocation */
	armv7_bs_alloc,		/* not implemented */
	armv7_bs_free,		/* not implemented */

	/* get kernel virtual address */
	armv7_bs_vaddr,

	/* mmap */
	bs_notimpl_bs_mmap,

	/* barrier */
	armv7_bs_barrier,

	/* read (single) */
	armv7_bs_r_1,
	armv7_bs_r_2,
	armv7_bs_r_4,
	bs_notimpl_bs_r_8,

	/* read multiple */
	armv7_bs_rm_1,
	armv7_bs_rm_2,
	armv7_bs_rm_4,
	bs_notimpl_bs_rm_8,

	/* read region */
	armv7_bs_rr_1,
	armv7_bs_rr_2,
	armv7_bs_rr_4,
	bs_notimpl_bs_rr_8,

	/* write (single) */
	armv7_bs_w_1,
	armv7_bs_w_2,
	armv7_bs_w_4,
	bs_notimpl_bs_w_8,

	/* write multiple */
	armv7_bs_wm_1,
	armv7_bs_wm_2,
	armv7_bs_wm_4,
	bs_notimpl_bs_wm_8,

	/* write region */
	armv7_bs_wr_1,
	armv7_bs_wr_2,
	armv7_bs_wr_4,
	bs_notimpl_bs_wr_8,

	/* set multiple */
	bs_notimpl_bs_sm_1,
	bs_notimpl_bs_sm_2,
	bs_notimpl_bs_sm_4,
	bs_notimpl_bs_sm_8,

	/* set region */
	armv7_bs_sr_1,
	armv7_bs_sr_2,
	armv7_bs_sr_4,
	bs_notimpl_bs_sr_8,

	/* copy */
	bs_notimpl_bs_c_1,
	armv7_bs_c_2,
	bs_notimpl_bs_c_4,
	bs_notimpl_bs_c_8,
};
struct bus_space *fdt_cons_bs_tag = &armv7_bs_tag;

int
armv7_bs_map(void *t, uint64_t bpa, bus_size_t size,
	      int flags, bus_space_handle_t *bshp)
{
	u_long startpa, endpa, pa;
	vaddr_t va;
	int pmap_flags = PMAP_DEVICE;

	startpa = trunc_page(bpa);
	endpa = round_page(bpa + size);

	/* XXX use extent manager to check duplicate mapping */

	va = (vaddr_t)km_alloc(endpa - startpa, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return(ENOMEM);

	*bshp = (bus_space_handle_t)(va + (bpa - startpa));

	if (flags & BUS_SPACE_MAP_CACHEABLE)
		pmap_flags = 0;

	for (pa = startpa; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_pa(va, pa | pmap_flags, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	return(0);
}

void
armv7_bs_unmap(void *t, bus_space_handle_t bsh, bus_size_t size)
{
	vaddr_t	va, endva;

	va = trunc_page((vaddr_t)bsh);
	endva = round_page((vaddr_t)bsh + size);

	pmap_kremove(va, endva - va);
	pmap_update(pmap_kernel());

	km_free((void *)va, endva - va, &kv_any, &kp_none);
}

int
armv7_bs_subregion(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

void
armv7_bs_barrier(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t len, int flags)
{
	cpu_drain_writebuf();
}

void *
armv7_bs_vaddr(void *t, bus_space_handle_t bsh)
{

	return ((void *)bsh);
}


int
armv7_bs_alloc(void *t, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp)
{

	panic("armv7_io_bs_alloc(): not implemented");
}

void
armv7_bs_free(void *t, bus_space_handle_t bsh, bus_size_t size)
{

	panic("armv7_io_bs_free(): not implemented");
}

