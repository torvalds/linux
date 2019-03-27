/*	$NetBSD: obio_space.c,v 1.6 2003/07/15 00:25:05 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001, 2002, 2003 Wasabi Systems, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/devmap.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>

void
generic_bs_unimplemented(void)
{

	panic("unimplemented bus_space function called");
}

/* Prototypes for all the bus_space structure functions */
bs_protos(generic);

int
generic_bs_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	void *va;

	/*
	 * We don't even examine the passed-in flags.  For ARM, the CACHEABLE
	 * flag doesn't make sense (we create VM_MEMATTR_DEVICE mappings), and
	 * the LINEAR flag is just implied because we use kva_alloc(size).
	 */
	if ((va = pmap_mapdev(bpa, size)) == NULL)
		return (ENOMEM);
	*bshp = (bus_space_handle_t)va;
	return (0);
}

int
generic_bs_alloc(bus_space_tag_t t, bus_addr_t rstart, bus_addr_t rend, bus_size_t size,
    bus_size_t alignment, bus_size_t boundary, int flags, bus_addr_t *bpap,
    bus_space_handle_t *bshp)
{

	panic("generic_bs_alloc(): not implemented");
}


void
generic_bs_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t size)
{

	pmap_unmapdev((vm_offset_t)h, size);
}

void
generic_bs_free(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{

	panic("generic_bs_free(): not implemented");
}

int
generic_bs_subregion(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

void
generic_bs_barrier(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t len, int flags)
{

	/*
	 * dsb() will drain the L1 write buffer and establish a memory access
	 * barrier point on platforms where that has meaning.  On a write we
	 * also need to drain the L2 write buffer, because most on-chip memory
	 * mapped devices are downstream of the L2 cache.  Note that this needs
	 * to be done even for memory mapped as Device type, because while
	 * Device memory is not cached, writes to it are still buffered.
	 */
	dsb();
	if (flags & BUS_SPACE_BARRIER_WRITE) {
		cpu_l2cache_drain_writebuf();
	}
}
