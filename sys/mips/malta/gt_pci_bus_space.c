/*      $NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $    */
/*-
 * $Id: bus.h,v 1.6 2007/08/09 11:23:32 katta Exp $
 *
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: src/sys/alpha/include/bus.h,v 1.5 1999/08/28 00:38:40 peter
 * $FreeBSD$
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/ktr.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cache.h>
#include <mips/malta/gt_pci_bus_space.h>

static bs_r_2_proto(gt_pci);
static bs_r_4_proto(gt_pci);
static bs_w_2_proto(gt_pci);
static bs_w_4_proto(gt_pci);
static bs_rm_2_proto(gt_pci);
static bs_rm_4_proto(gt_pci);
static bs_wm_2_proto(gt_pci);
static bs_wm_4_proto(gt_pci);
static bs_rr_2_proto(gt_pci);
static bs_rr_4_proto(gt_pci);
static bs_wr_2_proto(gt_pci);
static bs_wr_4_proto(gt_pci);
static bs_sm_2_proto(gt_pci);
static bs_sm_4_proto(gt_pci);
static bs_sr_2_proto(gt_pci);
static bs_sr_4_proto(gt_pci);

static struct bus_space gt_pci_space = {
	/* cookie */
	.bs_cookie =	(void *) 0,

	/* mapping/unmapping */
	.bs_map =	generic_bs_map,
	.bs_unmap =	generic_bs_unmap,
	.bs_subregion =	generic_bs_subregion,

	/* allocation/deallocation */
	.bs_alloc =	generic_bs_alloc,
	.bs_free =	generic_bs_free,

	/* barrier */
	.bs_barrier =	generic_bs_barrier,

	/* read (single) */
	.bs_r_1 =	generic_bs_r_1,
	.bs_r_2 =	gt_pci_bs_r_2,
	.bs_r_4 =	gt_pci_bs_r_4,
	.bs_r_8 =	NULL,

	/* read multiple */
	.bs_rm_1 =	generic_bs_rm_1,
	.bs_rm_2 =	gt_pci_bs_rm_2,
	.bs_rm_4 =	gt_pci_bs_rm_4,
	.bs_rm_8 =	NULL,

	/* read region */
	.bs_rr_1 =	generic_bs_rr_1,
	.bs_rr_2 =	gt_pci_bs_rr_2,
	.bs_rr_4 =	gt_pci_bs_rr_4,
	.bs_rr_8 =	NULL,

	/* write (single) */
	.bs_w_1 =	generic_bs_w_1,
	.bs_w_2 =	gt_pci_bs_w_2,
	.bs_w_4 =	gt_pci_bs_w_4,
	.bs_w_8 =	NULL,

	/* write multiple */
	.bs_wm_1 =	generic_bs_wm_1,
	.bs_wm_2 =	gt_pci_bs_wm_2,
	.bs_wm_4 =	gt_pci_bs_wm_4,
	.bs_wm_8 =	NULL,

	/* write region */
	.bs_wr_1 =	generic_bs_wr_1,
	.bs_wr_2 =	gt_pci_bs_wr_2,
	.bs_wr_4 =	gt_pci_bs_wr_4,
	.bs_wr_8 =	NULL,

	/* set multiple */
	.bs_sm_1 =	generic_bs_sm_1,
	.bs_sm_2 =	gt_pci_bs_sm_2,
	.bs_sm_4 =	gt_pci_bs_sm_4,
	.bs_sm_8 =	NULL,

	/* set region */
	.bs_sr_1 =	generic_bs_sr_1,
	.bs_sr_2 =	gt_pci_bs_sr_2,
	.bs_sr_4 =	gt_pci_bs_sr_4,
	.bs_sr_8 =	NULL,

	/* copy */
	.bs_c_1 =	generic_bs_c_1,
	.bs_c_2 =	generic_bs_c_2,
	.bs_c_4 =	generic_bs_c_4,
	.bs_c_8 =	NULL,

	/* read (single) stream */
	.bs_r_1_s =	generic_bs_r_1,
	.bs_r_2_s =	generic_bs_r_2,
	.bs_r_4_s =	generic_bs_r_4,
	.bs_r_8_s =	NULL,

	/* read multiple stream */
	.bs_rm_1_s =	generic_bs_rm_1,
	.bs_rm_2_s =	generic_bs_rm_2,
	.bs_rm_4_s =	generic_bs_rm_4,
	.bs_rm_8_s =	NULL,

	/* read region stream */
	.bs_rr_1_s =	generic_bs_rr_1,
	.bs_rr_2_s =	generic_bs_rr_2,
	.bs_rr_4_s =	generic_bs_rr_4,
	.bs_rr_8_s =	NULL,

	/* write (single) stream */
	.bs_w_1_s =	generic_bs_w_1,
	.bs_w_2_s =	generic_bs_w_2,
	.bs_w_4_s =	generic_bs_w_4,
	.bs_w_8_s =	NULL,

	/* write multiple stream */
	.bs_wm_1_s =	generic_bs_wm_1,
	.bs_wm_2_s =	generic_bs_wm_2,
	.bs_wm_4_s =	generic_bs_wm_4,
	.bs_wm_8_s =	NULL,

	/* write region stream */
	.bs_wr_1_s =	generic_bs_wr_1,
	.bs_wr_2_s =	generic_bs_wr_2,
	.bs_wr_4_s =	generic_bs_wr_4,
	.bs_wr_8_s =	NULL,
};

#define rd16(a) le16toh(readw(a))
#define rd32(a) le32toh(readl(a))
#define wr16(a, v) writew(a, htole16(v))
#define wr32(a, v) writel(a, htole32(v))

/* generic bus_space tag */
bus_space_tag_t gt_pci_bus_space = &gt_pci_space;

uint16_t
gt_pci_bs_r_2(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (rd16(handle + offset));
}

uint32_t
gt_pci_bs_r_4(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (rd32(handle + offset));
}

void
gt_pci_bs_rm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		*addr++ = rd16(baddr);
}

void
gt_pci_bs_rm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		*addr++ = rd32(baddr);
}

/*
 * Read `count' 2 or 4 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
void
gt_pci_bs_rr_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = rd16(baddr);
		baddr += 2;
	}
}

void
gt_pci_bs_rr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = rd32(baddr);
		baddr += 4;
	}
}

/*
 * Write the 2 or 4 byte value `value' to bus space
 * described by tag/handle/offset.
 */
void
gt_pci_bs_w_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value)
{

	wr16(bsh + offset, value);
}

void
gt_pci_bs_w_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value)
{

	wr32(bsh + offset, value);
}

/*
 * Write `count' 2 or 4 byte quantities from the buffer
 * provided to bus space described by tag/handle/offset.
 */
void
gt_pci_bs_wm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		wr16(baddr, *addr++);
}

void
gt_pci_bs_wm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		wr32(baddr, *addr++);
}

/*
 * Write `count' 2 or 4 byte quantities from the buffer provided
 * to bus space described by tag/handle starting at `offset'.
 */
void
gt_pci_bs_wr_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		wr16(baddr, *addr++);
		baddr += 2;
	}
}

void
gt_pci_bs_wr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		wr32(baddr, *addr++);
		baddr += 4;
	}
}

/*
 * Write the 2 or 4 byte value `val' to bus space described
 * by tag/handle/offset `count' times.
 */
void
gt_pci_bs_sm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	while (count--)
		wr16(addr, value);
}

void
gt_pci_bs_sm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	while (count--)
		wr32(addr, value);
}

/*
 * Write `count' 2 or 4 byte value `val' to bus space described
 * by tag/handle starting at `offset'.
 */
void
gt_pci_bs_sr_2(void *t, bus_space_handle_t bsh,
		       bus_size_t offset, uint16_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 2)
		wr16(addr, value);
}

void
gt_pci_bs_sr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 4)
		wr32(addr, value);
}
