/*      $NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $    */
/*-
 * $Id: bus.h,v 1.6 2007/08/09 11:23:32 katta Exp $
 *
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-4-Clause
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/ktr.h>
#include <sys/endian.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cache.h>

#include <mips/cavium/octopcivar.h>

#include <contrib/octeon-sdk/cvmx.h>

static struct bus_space octopci_space = {
	/* cookie */
	(void *) 0,

	/* mapping/unmapping */
	octopci_bs_map,
	octopci_bs_unmap,
	octopci_bs_subregion,

	/* allocation/deallocation */
	NULL,
	NULL,

	/* barrier */
	octopci_bs_barrier,

	/* read (single) */
	octopci_bs_r_1,
	octopci_bs_r_2,
	octopci_bs_r_4,
	NULL,

	/* read multiple */
	octopci_bs_rm_1,
	octopci_bs_rm_2,
	octopci_bs_rm_4,
	NULL,

	/* read region */
	octopci_bs_rr_1,
	octopci_bs_rr_2,
	octopci_bs_rr_4,
	NULL,

	/* write (single) */
	octopci_bs_w_1,
	octopci_bs_w_2,
	octopci_bs_w_4,
	NULL,

	/* write multiple */
	octopci_bs_wm_1,
	octopci_bs_wm_2,
	octopci_bs_wm_4,
	NULL,

	/* write region */
	NULL,
	octopci_bs_wr_2,
	octopci_bs_wr_4,
	NULL,

	/* set multiple */
	NULL,
	NULL,
	NULL,
	NULL,

	/* set region */
	NULL,
	octopci_bs_sr_2,
	octopci_bs_sr_4,
	NULL,

	/* copy */
	NULL,
	octopci_bs_c_2,
	NULL,
	NULL,

	/* read (single) stream */
	octopci_bs_r_1,
	octopci_bs_r_2,
	octopci_bs_r_4,
	NULL,

	/* read multiple stream */
	octopci_bs_rm_1,
	octopci_bs_rm_2,
	octopci_bs_rm_4,
	NULL,

	/* read region stream */
	octopci_bs_rr_1,
	octopci_bs_rr_2,
	octopci_bs_rr_4,
	NULL,

	/* write (single) stream */
	octopci_bs_w_1,
	octopci_bs_w_2,
	octopci_bs_w_4,
	NULL,

	/* write multiple stream */
	octopci_bs_wm_1,
	octopci_bs_wm_2,
	octopci_bs_wm_4,
	NULL,

	/* write region stream */
	NULL,
	octopci_bs_wr_2,
	octopci_bs_wr_4,
	NULL,
};

#define rd8(a)		cvmx_read64_uint8(a)
#define rd16(a)		le16toh(cvmx_read64_uint16(a))
#define rd32(a)		le32toh(cvmx_read64_uint32(a))
#define wr8(a, v)	cvmx_write64_uint8(a, v)
#define wr16(a, v)	cvmx_write64_uint16(a, htole16(v))
#define wr32(a, v)	cvmx_write64_uint32(a, htole32(v))

/* octopci bus_space tag */
bus_space_tag_t octopci_bus_space = &octopci_space;

int
octopci_bs_map(void *t __unused, bus_addr_t addr,
	      bus_size_t size __unused, int flags __unused,
	      bus_space_handle_t *bshp)
{

	*bshp = addr;
	return (0);
}

void
octopci_bs_unmap(void *t __unused, bus_space_handle_t bh __unused,
	      bus_size_t size __unused)
{

	/* Do nothing */
}

int
octopci_bs_subregion(void *t __unused, bus_space_handle_t handle,
	      bus_size_t offset, bus_size_t size __unused,
	      bus_space_handle_t *bshp)
{

	*bshp = handle + offset;
	return (0);
}

uint8_t
octopci_bs_r_1(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (rd8(handle + offset));
}

uint16_t
octopci_bs_r_2(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (rd16(handle + offset));
}

uint32_t
octopci_bs_r_4(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return (rd32(handle + offset));
}


void
octopci_bs_rm_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, size_t count)
{

	while (count--)
		*addr++ = rd8(bsh + offset);
}

void
octopci_bs_rm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		*addr++ = rd16(baddr);
}

void
octopci_bs_rm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		*addr++ = rd32(baddr);
}


/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
void
octopci_bs_rr_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = rd8(baddr);
		baddr += 1;
	}
}

void
octopci_bs_rr_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = rd16(baddr);
		baddr += 2;
	}
}

void
octopci_bs_rr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = rd32(baddr);
		baddr += 4;
	}
}

/*
 * Write the 1, 2, 4, or 8 byte value `value' to bus space
 * described by tag/handle/offset.
 */
void
octopci_bs_w_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value)
{

	wr8(bsh + offset, value);
}

void
octopci_bs_w_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value)
{

	wr16(bsh + offset, value);
}

void
octopci_bs_w_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value)
{

	wr32(bsh + offset, value);
}

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer
 * provided to bus space described by tag/handle/offset.
 */
void
octopci_bs_wm_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		wr8(baddr, *addr++);
}

void
octopci_bs_wm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		wr16(baddr, *addr++);
}

void
octopci_bs_wm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--)
		wr32(baddr, *addr++);
}

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer provided
 * to bus space described by tag/handle starting at `offset'.
 */
void
octopci_bs_wr_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		wr8(baddr, *addr++);
		baddr += 1;
	}
}

void
octopci_bs_wr_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		wr16(baddr, *addr++);
		baddr += 2;
	}
}

void
octopci_bs_wr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		wr32(baddr, *addr++);
		baddr += 4;
	}
}

/*
 * Write the 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle/offset `count' times.
 */
void
octopci_bs_sm_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	while (count--)
		wr8(addr, value);
}

void
octopci_bs_sm_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	while (count--)
		wr16(addr, value);
}

void
octopci_bs_sm_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	while (count--)
		wr32(addr, value);
}

/*
 * Write `count' 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle starting at `offset'.
 */
void
octopci_bs_sr_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr++)
		wr8(addr, value);
}

void
octopci_bs_sr_2(void *t, bus_space_handle_t bsh,
		       bus_size_t offset, uint16_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 2)
		wr16(addr, value);
}

void
octopci_bs_sr_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 4)
		wr32(addr, value);
}

/*
 * Copy `count' 1, 2, 4, or 8 byte values from bus space starting
 * at tag/bsh1/off1 to bus space starting at tag/bsh2/off2.
 */
void
octopci_bs_c_1(void *t, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2,
    bus_size_t off2, size_t count)
{
	bus_addr_t addr1 = bsh1 + off1;
	bus_addr_t addr2 = bsh2 + off2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; count != 0; count--, addr1++, addr2++)
			wr8(addr2, rd8(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += (count - 1), addr2 += (count - 1);
		    count != 0; count--, addr1--, addr2--)
			wr8(addr2, rd8(addr1));
	}
}

void
octopci_bs_c_2(void *t, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2,
    bus_size_t off2, size_t count)
{
	bus_addr_t addr1 = bsh1 + off1;
	bus_addr_t addr2 = bsh2 + off2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; count != 0; count--, addr1 += 2, addr2 += 2)
			wr16(addr2, rd16(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 2 * (count - 1), addr2 += 2 * (count - 1);
		    count != 0; count--, addr1 -= 2, addr2 -= 2)
			wr16(addr2, rd16(addr1));
	}
}

void
octopci_bs_c_4(void *t, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2,
    bus_size_t off2, size_t count)
{
	bus_addr_t addr1 = bsh1 + off1;
	bus_addr_t addr2 = bsh2 + off2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; count != 0; count--, addr1 += 4, addr2 += 4)
			wr32(addr2, rd32(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 4 * (count - 1), addr2 += 4 * (count - 1);
		    count != 0; count--, addr1 -= 4, addr2 -= 4)
			wr32(addr2, rd32(addr1));
	}
}

void
octopci_bs_barrier(void *t __unused, 
		bus_space_handle_t bsh __unused,
		bus_size_t offset __unused, bus_size_t len __unused, 
		int flags)
{
#if 0
	if (flags & BUS_SPACE_BARRIER_WRITE)
		mips_dcache_wbinv_all();
#endif
}
