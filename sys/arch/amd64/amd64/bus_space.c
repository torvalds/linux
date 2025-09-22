/*	$OpenBSD: bus_space.c,v 1.30 2025/09/17 18:39:50 sf Exp $	*/
/*	$NetBSD: bus_space.c,v 1.2 2003/03/14 18:47:53 christos Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <machine/isa_machdep.h>

extern int pmap_initialized;

/* kernel address of "hole" (location of start of iomem in virtual) */
u_long atdevbase = 0;	

/*
 * Extent maps to manage I/O and memory space.  Allocate
 * storage for 16 regions in each, initially.  Later, ioport_malloc_safe
 * will indicate that it's safe to use malloc() to dynamically allocate
 * region descriptors.
 *
 * N.B. At least two regions are _always_ allocated from the iomem
 * extent map; (0 -> ISA hole) and (end of ISA hole -> end of RAM).
 *
 * The extent maps are not static!  Machine-dependent ISA and EISA
 * routines need access to them for bus address space allocation.
 */
static	long ioport_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
static	long iomem_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
struct	extent *ioport_ex;
struct	extent *iomem_ex;
static	int ioport_malloc_safe;

int	x86_mem_add_mapping(bus_addr_t, bus_size_t,
	    int, bus_space_handle_t *);

u_int8_t	x86_bus_space_io_read_1(bus_space_handle_t, bus_size_t);
u_int16_t	x86_bus_space_io_read_2(bus_space_handle_t, bus_size_t);
u_int32_t	x86_bus_space_io_read_4(bus_space_handle_t, bus_size_t);
u_int64_t	x86_bus_space_io_read_8(bus_space_handle_t, bus_size_t);

void		x86_bus_space_io_read_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		x86_bus_space_io_read_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		x86_bus_space_io_read_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);
void		x86_bus_space_io_read_multi_8(bus_space_handle_t, bus_size_t,
		    u_int64_t *, bus_size_t);

void		x86_bus_space_io_read_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		x86_bus_space_io_read_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		x86_bus_space_io_read_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);
void		x86_bus_space_io_read_region_8(bus_space_handle_t, bus_size_t,
		    u_int64_t *, bus_size_t);

void		x86_bus_space_io_write_1(bus_space_handle_t, bus_size_t,
		    u_int8_t);
void		x86_bus_space_io_write_2(bus_space_handle_t, bus_size_t,
		    u_int16_t);
void		x86_bus_space_io_write_4(bus_space_handle_t, bus_size_t,
		    u_int32_t);
void		x86_bus_space_io_write_8(bus_space_handle_t, bus_size_t,
		    u_int64_t);

void		x86_bus_space_io_write_multi_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		x86_bus_space_io_write_multi_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		x86_bus_space_io_write_multi_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		x86_bus_space_io_write_multi_8(bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

void		x86_bus_space_io_write_region_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		x86_bus_space_io_write_region_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		x86_bus_space_io_write_region_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		x86_bus_space_io_write_region_8(bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

void		x86_bus_space_io_set_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		x86_bus_space_io_set_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		x86_bus_space_io_set_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);
void		x86_bus_space_io_set_multi_8(bus_space_handle_t, bus_size_t,
		    u_int64_t, size_t);

void		x86_bus_space_io_set_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		x86_bus_space_io_set_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		x86_bus_space_io_set_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);
void		x86_bus_space_io_set_region_8(bus_space_handle_t, bus_size_t,
		    u_int64_t, size_t);

void		x86_bus_space_io_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_io_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_io_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_io_copy_8(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

void *		x86_bus_space_io_vaddr(bus_space_handle_t);
paddr_t		x86_bus_space_io_mmap(bus_addr_t, off_t, int, int);

const struct x86_bus_space_ops default_bus_space_io_ops = {
	x86_bus_space_io_read_1,
	x86_bus_space_io_read_2,
	x86_bus_space_io_read_4,
	x86_bus_space_io_read_8,
	x86_bus_space_io_read_multi_1,
	x86_bus_space_io_read_multi_2,
	x86_bus_space_io_read_multi_4,
	x86_bus_space_io_read_multi_8,
	x86_bus_space_io_read_region_1,
	x86_bus_space_io_read_region_2,
	x86_bus_space_io_read_region_4,
	x86_bus_space_io_read_region_8,
	x86_bus_space_io_write_1,
	x86_bus_space_io_write_2,
	x86_bus_space_io_write_4,
	x86_bus_space_io_write_8,
	x86_bus_space_io_write_multi_1,
	x86_bus_space_io_write_multi_2,
	x86_bus_space_io_write_multi_4,
	x86_bus_space_io_write_multi_8,
	x86_bus_space_io_write_region_1,
	x86_bus_space_io_write_region_2,
	x86_bus_space_io_write_region_4,
	x86_bus_space_io_write_region_8,
	x86_bus_space_io_set_multi_1,
	x86_bus_space_io_set_multi_2,
	x86_bus_space_io_set_multi_4,
	x86_bus_space_io_set_multi_8,
	x86_bus_space_io_set_region_1,
	x86_bus_space_io_set_region_2,
	x86_bus_space_io_set_region_4,
	x86_bus_space_io_set_region_8,
	x86_bus_space_io_copy_1,
	x86_bus_space_io_copy_2,
	x86_bus_space_io_copy_4,
	x86_bus_space_io_copy_8,
	x86_bus_space_io_vaddr,
	x86_bus_space_io_mmap
};

const struct x86_bus_space_ops *x86_bus_space_io_ops =
    &default_bus_space_io_ops;

u_int8_t	x86_bus_space_mem_read_1(bus_space_handle_t, bus_size_t);
u_int16_t	x86_bus_space_mem_read_2(bus_space_handle_t, bus_size_t);
u_int32_t	x86_bus_space_mem_read_4(bus_space_handle_t, bus_size_t);
u_int64_t	x86_bus_space_mem_read_8(bus_space_handle_t, bus_size_t);

void		x86_bus_space_mem_read_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		x86_bus_space_mem_read_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		x86_bus_space_mem_read_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);
void		x86_bus_space_mem_read_multi_8(bus_space_handle_t, bus_size_t,
		    u_int64_t *, bus_size_t);

void		x86_bus_space_mem_read_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		x86_bus_space_mem_read_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		x86_bus_space_mem_read_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);
void		x86_bus_space_mem_read_region_8(bus_space_handle_t, bus_size_t,
		    u_int64_t *, bus_size_t);

void		x86_bus_space_mem_write_1(bus_space_handle_t, bus_size_t,
		    u_int8_t);
void		x86_bus_space_mem_write_2(bus_space_handle_t, bus_size_t,
		    u_int16_t);
void		x86_bus_space_mem_write_4(bus_space_handle_t, bus_size_t,
		    u_int32_t);
void		x86_bus_space_mem_write_8(bus_space_handle_t, bus_size_t,
		    u_int64_t);

void		x86_bus_space_mem_write_multi_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		x86_bus_space_mem_write_multi_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		x86_bus_space_mem_write_multi_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		x86_bus_space_mem_write_multi_8(bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

void		x86_bus_space_mem_write_region_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		x86_bus_space_mem_write_region_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		x86_bus_space_mem_write_region_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);
void		x86_bus_space_mem_write_region_8(bus_space_handle_t,
		    bus_size_t, const u_int64_t *, bus_size_t);

void		x86_bus_space_mem_set_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		x86_bus_space_mem_set_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		x86_bus_space_mem_set_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);
void		x86_bus_space_mem_set_multi_8(bus_space_handle_t, bus_size_t,
		    u_int64_t, size_t);

void		x86_bus_space_mem_set_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		x86_bus_space_mem_set_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		x86_bus_space_mem_set_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);
void		x86_bus_space_mem_set_region_8(bus_space_handle_t, bus_size_t,
		    u_int64_t, size_t);

void		x86_bus_space_mem_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_mem_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_mem_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		x86_bus_space_mem_copy_8(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

void *		x86_bus_space_mem_vaddr(bus_space_handle_t);

paddr_t		x86_bus_space_mem_mmap(bus_addr_t, off_t, int, int);

const struct x86_bus_space_ops default_bus_space_mem_ops = {
	x86_bus_space_mem_read_1,
	x86_bus_space_mem_read_2,
	x86_bus_space_mem_read_4,
	x86_bus_space_mem_read_8,
	x86_bus_space_mem_read_multi_1,
	x86_bus_space_mem_read_multi_2,
	x86_bus_space_mem_read_multi_4,
	x86_bus_space_mem_read_multi_8,
	x86_bus_space_mem_read_region_1,
	x86_bus_space_mem_read_region_2,
	x86_bus_space_mem_read_region_4,
	x86_bus_space_mem_read_region_8,
	x86_bus_space_mem_write_1,
	x86_bus_space_mem_write_2,
	x86_bus_space_mem_write_4,
	x86_bus_space_mem_write_8,
	x86_bus_space_mem_write_multi_1,
	x86_bus_space_mem_write_multi_2,
	x86_bus_space_mem_write_multi_4,
	x86_bus_space_mem_write_multi_8,
	x86_bus_space_mem_write_region_1,
	x86_bus_space_mem_write_region_2,
	x86_bus_space_mem_write_region_4,
	x86_bus_space_mem_write_region_8,
	x86_bus_space_mem_set_multi_1,
	x86_bus_space_mem_set_multi_2,
	x86_bus_space_mem_set_multi_4,
	x86_bus_space_mem_set_multi_8,
	x86_bus_space_mem_set_region_1,
	x86_bus_space_mem_set_region_2,
	x86_bus_space_mem_set_region_4,
	x86_bus_space_mem_set_region_8,
	x86_bus_space_mem_copy_1,
	x86_bus_space_mem_copy_2,
	x86_bus_space_mem_copy_4,
	x86_bus_space_mem_copy_8,
	x86_bus_space_mem_vaddr,
	x86_bus_space_mem_mmap
};

const struct x86_bus_space_ops *x86_bus_space_mem_ops;

extern const struct x86_bus_space_ops sev_ghcb_bus_space_io_ops;
extern const struct x86_bus_space_ops sev_ghcb_bus_space_mem_ops;

void
x86_bus_space_init(void)
{
	/*
	 * Initialize the I/O port and I/O mem extent maps.
	 * Note: we don't have to check the return value since
	 * creation of a fixed extent map will never fail (since
	 * descriptor storage has already been allocated).
	 *
	 * N.B. The iomem extent manages _all_ physical addresses
	 * on the machine.  When the amount of RAM is found, the two
	 * extents of RAM are allocated from the map (0 -> ISA hole
	 * and end of ISA hole -> end of RAM).
	 */
	ioport_ex = extent_create("ioport", 0x0, 0xffff, M_DEVBUF,
	    (caddr_t)ioport_ex_storage, sizeof(ioport_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);
	iomem_ex = extent_create("iomem", 0x0, 0xffffffffffff, M_DEVBUF,
	    (caddr_t)iomem_ex_storage, sizeof(iomem_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);

	if (ISSET(cpu_sev_guestmode, SEV_STAT_ES_ENABLED)) {
		x86_bus_space_mem_ops = &sev_ghcb_bus_space_mem_ops;
		x86_bus_space_io_ops  = &sev_ghcb_bus_space_io_ops;
	} else {
		x86_bus_space_mem_ops = &default_bus_space_mem_ops;
		x86_bus_space_io_ops  = &default_bus_space_io_ops;
	}
}

void
x86_bus_space_mallocok(void)
{
	ioport_malloc_safe = 1;
}

int
bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	int error;
	struct extent *ex;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
		if (flags & BUS_SPACE_MAP_LINEAR)
			return (EINVAL);
	} else if (t == X86_BUS_SPACE_MEM)
		ex = iomem_ex;
	else
		panic("bus_space_map: bad bus space tag");

	/*
	 * Before we go any further, let's make sure that this
	 * region is available.
	 */
	error = extent_alloc_region(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0));
	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == X86_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	if (bpa >= IOM_BEGIN && (bpa + size) <= IOM_END) {
		*bshp = (bus_space_handle_t)ISA_HOLE_VADDR(bpa);
		return(0);
	}

	if (!pmap_initialized && bpa < 0x100000000) {
		*bshp = (bus_space_handle_t)PMAP_DIRECT_MAP(bpa);
		return(0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = x86_mem_add_mapping(bpa, size, flags, bshp);
	if (error) {
		if (extent_free(ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}

	return (error);
}

int
_bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{

	/*
	 * For I/O space, just fill in the handle.
	 */
	if (t == X86_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	return (x86_mem_add_mapping(bpa, size, flags, bshp));
}

int
bus_space_alloc(bus_space_tag_t t, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp)
{
	struct extent *ex;
	u_long bpa;
	int error;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
	} else if (t == X86_BUS_SPACE_MEM)
		ex = iomem_ex;
	else
		panic("bus_space_alloc: bad bus space tag");

	/*
	 * Sanity check the allocation against the extent's boundaries.
	 */
	if (rstart < ex->ex_start || rend > ex->ex_end)
		panic("bus_space_alloc: bad region start/end");

	/*
	 * Do the requested allocation.
	 */
	error = extent_alloc_subregion(ex, rstart, rend, size, alignment,
	    0, boundary,
	    EX_FAST | EX_NOWAIT | (ioport_malloc_safe ?  EX_MALLOCOK : 0),
	    &bpa);

	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == X86_BUS_SPACE_IO) {
		*bshp = *bpap = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = x86_mem_add_mapping(bpa, size, flags, bshp);
	if (error) {
		if (extent_free(iomem_ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_alloc: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_alloc: can't free region\n");
		}
	}

	*bpap = bpa;

	return (error);
}

int
x86_mem_add_mapping(bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	paddr_t pa, endpa;
	vaddr_t va;
	bus_size_t map_size;
	int pmap_flags = PMAP_NOCACHE;

	pa = trunc_page(bpa);
	endpa = round_page(bpa + size);

#ifdef DIAGNOSTIC
	if (endpa <= pa && endpa != 0)
		panic("bus_mem_add_mapping: overflow");
#endif

	map_size = endpa - pa;

	va = (vaddr_t)km_alloc(map_size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return (ENOMEM);

	*bshp = (bus_space_handle_t)(va + (bpa & PGOFSET));

	if (flags & BUS_SPACE_MAP_CACHEABLE)
		pmap_flags = 0;
	else if (flags & BUS_SPACE_MAP_PREFETCHABLE)
		pmap_flags = PMAP_WC;

	for (; map_size > 0;
	    pa += PAGE_SIZE, va += PAGE_SIZE, map_size -= PAGE_SIZE)
		pmap_kenter_pa(va, pa | pmap_flags, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	return 0;
}

/*
 * void _bus_space_unmap(bus_space_tag bst, bus_space_handle bsh,
 *                        bus_size_t size, bus_addr_t *adrp)
 *
 *   This function unmaps memory- or io-space mapped by the function
 *   _bus_space_map().  This function works nearly as same as
 *   bus_space_unmap(), but this function does not ask kernel
 *   built-in extents and returns physical address of the bus space,
 *   for the convenience of the extra extent manager.
 */
void
_bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size,
    bus_addr_t *adrp)
{
	u_long va, endva;
	bus_addr_t bpa;

	/*
	 * Find the correct bus physical address.
	 */
	if (t == X86_BUS_SPACE_IO) {
		bpa = bsh;
	} else if (t == X86_BUS_SPACE_MEM) {
		bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		if (IOM_BEGIN <= bpa && bpa <= IOM_END)
			goto ok;

		va = trunc_page(bsh);
		endva = round_page(bsh + size);

#ifdef DIAGNOSTIC
		if (endva <= va)
			panic("_bus_space_unmap: overflow");
#endif

		(void) pmap_extract(pmap_kernel(), va, &bpa);
		bpa += (bsh & PGOFSET);

		pmap_kremove(va, endva - va);
		pmap_update(pmap_kernel());

		/*
		 * Free the kernel virtual mapping.
		 */
		km_free((void *)va, endva - va, &kv_any, &kp_none);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (adrp != NULL)
		*adrp = bpa;
}

void
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	struct extent *ex;
	u_long va, endva;
	bus_addr_t bpa;

	/*
	 * Find the correct extent and bus physical address.
	 */
	if (t == X86_BUS_SPACE_IO) {
		ex = ioport_ex;
		bpa = bsh;
	} else if (t == X86_BUS_SPACE_MEM) {
		ex = iomem_ex;
		bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		if (IOM_BEGIN <= bpa && bpa <= IOM_END)
			goto ok;

		if (bsh >= PMAP_DIRECT_BASE && bsh < PMAP_DIRECT_END) {
			bpa = PMAP_DIRECT_UNMAP(bsh);
			goto ok;
		}

		va = trunc_page(bsh);
		endva = round_page(bsh + size);

#ifdef DIAGNOSTIC
		if (endva <= va)
			panic("bus_space_unmap: overflow");
#endif

		(void)pmap_extract(pmap_kernel(), va, &bpa);
		bpa += (bsh & PGOFSET);

		pmap_kremove(va, endva - va);
		pmap_update(pmap_kernel());

		/*
		 * Free the kernel virtual mapping.
		 */
		km_free((void *)va, endva - va, &kv_any, &kp_none);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (extent_free(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
		printf("bus_space_unmap: %s 0x%lx, size 0x%lx\n",
		    (t == X86_BUS_SPACE_IO) ? "port" : "pa", bpa, size);
		printf("bus_space_unmap: can't free region\n");
	}
}

void
bus_space_free(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{

	/* bus_space_unmap() does all that we need to do. */
	bus_space_unmap(t, bsh, size);
}

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

u_int8_t
x86_bus_space_io_read_1(bus_space_handle_t h, bus_size_t o)
{
	return (inb(h + o));
}

u_int16_t
x86_bus_space_io_read_2(bus_space_handle_t h, bus_size_t o)
{
	return (inw(h + o));
}

u_int32_t
x86_bus_space_io_read_4(bus_space_handle_t h, bus_size_t o)
{
	return (inl(h + o));
}

u_int64_t
x86_bus_space_io_read_8(bus_space_handle_t h, bus_size_t o)
{
	panic("bus_space_read_8: invalid bus space tag");
}

void
x86_bus_space_io_read_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t *ptr, bus_size_t cnt)
{
	insb(h + o, ptr, cnt);
}

void
x86_bus_space_io_read_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t *ptr, bus_size_t cnt)
{
	insw(h + o, ptr, cnt);
}

void
x86_bus_space_io_read_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t *ptr, bus_size_t cnt)
{
	insl(h + o, ptr, cnt);
}

void
x86_bus_space_io_read_multi_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t *ptr, bus_size_t cnt)
{
	panic("bus_space_multi_8: invalid bus space tag");
}

void
x86_bus_space_io_read_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	inb %w1,%%al				;"
	"	stosb					;"
	"	incl %1					;"
	"	loop 1b"				:
	    "=&a" (__x), "=d" (dummy1), "=D" (dummy2),
	    "=c" (dummy3)				:
	    "1" (port), "2" (ptr), "3" (cnt)	:
	    "memory");
}

void
x86_bus_space_io_read_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	inw %w1,%%ax				;"
	"	stosw					;"
	"	addl $2,%1				;"
	"	loop 1b"				:
	    "=&a" (__x), "=d" (dummy1), "=D" (dummy2),
	    "=c" (dummy3)				:
	    "1" ((port)), "2" ((ptr)), "3" ((cnt))	:
	    "memory");
}

void
x86_bus_space_io_read_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	inl %w1,%%eax				;"
	"	stosl					;"
	"	addl $4,%1				;"
	"	loop 1b"				:
	    "=&a" (__x), "=d" (dummy1), "=D" (dummy2),
	    "=c" (dummy3)				:
	    "1" (port), "2" (ptr), "3" (cnt)	:
	    "memory");
}

void
x86_bus_space_io_read_region_8(bus_space_handle_t h,
    bus_size_t o, u_int64_t *ptr, bus_size_t cnt)
{
	panic("bus_space_read_region_8: invalid bus space tag");
}

void
x86_bus_space_io_write_1(bus_space_handle_t h, bus_size_t o, u_int8_t v)
{
	outb(h + o, v);
}

void
x86_bus_space_io_write_2(bus_space_handle_t h, bus_size_t o, u_int16_t v)
{
	outw(h + o, v);
}

void
x86_bus_space_io_write_4(bus_space_handle_t h, bus_size_t o, u_int32_t v)
{
	outl(h + o, v);
}

void
x86_bus_space_io_write_8(bus_space_handle_t h, bus_size_t o, u_int64_t v)
{
	panic("bus_space_write_8: invalid bus space tag");
}

void
x86_bus_space_io_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *ptr, bus_size_t cnt)
{
	outsb(h + o, ptr, cnt);
}

void
x86_bus_space_io_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *ptr, bus_size_t cnt)
{
	outsw(h + o, ptr, cnt);
}

void
x86_bus_space_io_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *ptr, bus_size_t cnt)
{
	outsl(h + o, ptr, cnt);
}

void
x86_bus_space_io_write_multi_8(bus_space_handle_t h,
    bus_size_t o, const u_int64_t *ptr, bus_size_t cnt)
{
	panic("bus_space_write_multi_8: invalid bus space tag");
}

void
x86_bus_space_io_write_region_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	lodsb					;"
	"	outb %%al,%w1				;"
	"	incl %1					;"
	"	loop 1b"				:
	    "=&a" (__x), "=d" (dummy1), "=S" (dummy2),
	    "=c" (dummy3)				:
	    "1" (port), "2" (ptr), "3" (cnt)	:
	    "memory");
}

void
x86_bus_space_io_write_region_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	lodsw					;"
	"	outw %%ax,%w1				;"
	"	addl $2,%1				;"
	"	loop 1b"				: 
	    "=&a" (__x), "=d" (dummy1), "=S" (dummy2),
	    "=c" (dummy3)				:
	    "1" (port), "2" (ptr), "3" (cnt)	:
	    "memory");
}

void
x86_bus_space_io_write_region_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	int __x;
	u_int32_t port = h + o;
	__asm volatile(
	"1:	lodsl					;"
	"	outl %%eax,%w1				;"
	"	addl $4,%1				;"
	"	loop 1b"				:
	    "=&a" (__x), "=d" (dummy1), "=S" (dummy2),
	    "=c" (dummy3)				:
	    "1" (port), "2" (ptr), "3" (cnt)	:
	    "memory");
}

void
x86_bus_space_io_write_region_8(bus_space_handle_t h,
    bus_size_t o, const u_int64_t *ptr, bus_size_t cnt)
{
	panic("bus_space_write_region_8: invalid bus space tag");
}

void
x86_bus_space_io_set_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		outb(addr, v);
}

void
x86_bus_space_io_set_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		outw(addr, v);
}

void
x86_bus_space_io_set_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		outl(addr, v);
}

void
x86_bus_space_io_set_multi_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t v, size_t c)
{
	panic("bus_space_set_multi_8: invalid bus space tag");
}

void
x86_bus_space_io_set_region_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr++)
		outb(addr, v);
}

void
x86_bus_space_io_set_region_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr += sizeof(v))
		outw(addr, v);
}

void
x86_bus_space_io_set_region_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr += sizeof(v))
		outl(addr, v);
}

void
x86_bus_space_io_set_region_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t v, size_t c)
{
	panic("bus_space_set_region_8: invalid bus space tag");
}

void
x86_bus_space_io_copy_1(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1++, addr2++)
			outb(addr2, inb(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += (c - 1), addr2 += (c - 1);
		    c != 0; c--, addr1--, addr2--)
			outb(addr2, inb(addr1));
	}
}

void
x86_bus_space_io_copy_2(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1 += 2, addr2 += 2)
			outw(addr2, inw(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 2 * (c - 1), addr2 += 2 * (c - 1);
		    c != 0; c--, addr1 -= 2, addr2 -= 2)
			outw(addr2, inw(addr1));
	}
}

void
x86_bus_space_io_copy_4(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1 += 4, addr2 += 4)
			outl(addr2, inl(addr1));
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 4 * (c - 1), addr2 += 4 * (c - 1);
		    c != 0; c--, addr1 -= 4, addr2 -= 4)
			outl(addr2, inl(addr1));
	}
}

void
x86_bus_space_io_copy_8(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	panic("bus_space_set_region_8: invalid bus space tag");
}

void *
x86_bus_space_io_vaddr(bus_space_handle_t h)
{
	return (NULL);
}

paddr_t
x86_bus_space_io_mmap(bus_addr_t addr, off_t off, int prot, int flags)
{
	/* Can't mmap I/O space. */
	return (-1);
}

void
x86_bus_space_mem_write_1(bus_space_handle_t h, bus_size_t o, u_int8_t v)
{
	*(volatile u_int8_t *)(h + o) = v;
}

void
x86_bus_space_mem_write_2(bus_space_handle_t h, bus_size_t o, u_int16_t v)
{
	*(volatile u_int16_t *)(h + o) = v;
}

u_int8_t
x86_bus_space_mem_read_1(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int8_t *)(h + o));
}

u_int16_t
x86_bus_space_mem_read_2(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int16_t *)(h + o));
}

u_int32_t
x86_bus_space_mem_read_4(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int32_t *)(h + o));
}

u_int64_t
x86_bus_space_mem_read_8(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int64_t *)(h + o));
}

void
x86_bus_space_mem_read_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	movb (%2),%%al				;"
	"	stosb					;"
	"	loop 1b"				:
	    "=D" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" ((ptr)), "1" ((cnt)), "2" (h + o)       :
	    "memory");
}

void
x86_bus_space_mem_read_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	movw (%2),%%ax				;"
	"	stosw					;"
	"	loop 1b"				:
	    "=D" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" ((ptr)), "1" ((cnt)), "2" (h + o)       :
	    "memory");
}

void
x86_bus_space_mem_read_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	movl (%2),%%eax				;"
	"	stosl					;"
	"	loop 1b"				:
	    "=D" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" ((ptr)), "1" ((cnt)), "2" (h + o)       :
	    "memory");
}

void
x86_bus_space_mem_read_multi_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	movq (%2),%%rax				;"
	"	stosq					;"
	"	loop 1b"				:
	    "=D" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" ((ptr)), "1" ((cnt)), "2" (h + o)       :
	    "memory");
}

void
x86_bus_space_mem_read_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsb"					:
	    "=S" (dummy1), "=D" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_read_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsw"					:
	    "=S" (dummy1), "=D" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_read_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsl"					:
	    "=S" (dummy1), "=D" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_read_region_8(bus_space_handle_t h,
    bus_size_t o, u_int64_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsq"					:
	    "=S" (dummy1), "=D" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_write_4(bus_space_handle_t h, bus_size_t o, u_int32_t v)
{
	*(volatile u_int32_t *)(h + o) = v;
}

void
x86_bus_space_mem_write_8(bus_space_handle_t h, bus_size_t o, u_int64_t v)
{
	*(volatile u_int64_t *)(h + o) = v;
}

void
x86_bus_space_mem_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	lodsb					;"
	"	movb %%al,(%2)				;"
	"	loop 1b"				:
	    "=S" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" (ptr), "1" (cnt), "2" (h + o));
}

void
x86_bus_space_mem_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	lodsw					;"
	"	movw %%ax,(%2)				;"
	"	loop 1b"				:
	    "=S" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" (ptr), "1" (cnt), "2" (h + o));
}

void
x86_bus_space_mem_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	lodsl					;"
	"	movl %%eax,(%2)				;"
	"	loop 1b"				:
	    "=S" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" (ptr), "1" (cnt), "2" (h + o));
}

void
x86_bus_space_mem_write_multi_8(bus_space_handle_t h,
    bus_size_t o, const u_int64_t *ptr, bus_size_t cnt)
{
	void *dummy1;
	int dummy2;
	void *dummy3;
	int __x;
	__asm volatile(
	"1:	lodsq					;"
	"	movq %%rax,(%2)				;"
	"	loop 1b"				:
	    "=S" (dummy1), "=c" (dummy2), "=r" (dummy3), "=&a" (__x) :
	    "0" (ptr), "1" (cnt), "2" (h + o));
}

void
x86_bus_space_mem_write_region_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsb"					:
	    "=D" (dummy1), "=S" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_write_region_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsw"					:
	    "=D" (dummy1), "=S" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_write_region_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsl"					:
	    "=D" (dummy1), "=S" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_write_region_8(bus_space_handle_t h,
    bus_size_t o, const u_int64_t *ptr, bus_size_t cnt)
{
	int dummy1;
	void *dummy2;
	int dummy3;
	__asm volatile(
	"	repne					;"
	"	movsq"					:
	    "=D" (dummy1), "=S" (dummy2), "=c" (dummy3)	:
	    "0" (h + o), "1" (ptr), "2" (cnt)	:
	    "memory");
}

void
x86_bus_space_mem_set_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		*(volatile u_int8_t *)(addr) = v;
}

void
x86_bus_space_mem_set_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		*(volatile u_int16_t *)(addr) = v;
}

void
x86_bus_space_mem_set_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		*(volatile u_int32_t *)(addr) = v;
}

void
x86_bus_space_mem_set_multi_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t v, size_t c)
{
	bus_addr_t addr = h + o;

	while (c--)
		*(volatile u_int64_t *)(addr) = v;
}

void
x86_bus_space_mem_set_region_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr++)
		*(volatile u_int8_t *)(addr) = v;
}

void
x86_bus_space_mem_set_region_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr += sizeof(v))
		*(volatile u_int16_t *)(addr) = v;
}

void
x86_bus_space_mem_set_region_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr += sizeof(v))
		*(volatile u_int32_t *)(addr) = v;
}

void
x86_bus_space_mem_set_region_8(bus_space_handle_t h, bus_size_t o,
    u_int64_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (; c != 0; c--, addr += sizeof(v))
		*(volatile u_int64_t *)(addr) = v;
}

void
x86_bus_space_mem_copy_1( bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1++, addr2++)
			*(volatile u_int8_t *)(addr2) =
			    *(volatile u_int8_t *)(addr1);
	} else {
		/* dest after src: copy backwards */
		for (addr1 += (c - 1), addr2 += (c - 1);
		    c != 0; c--, addr1--, addr2--)
			*(volatile u_int8_t *)(addr2) =
			    *(volatile u_int8_t *)(addr1);
	}
}

void
x86_bus_space_mem_copy_2(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1 += 2, addr2 += 2)
			*(volatile u_int16_t *)(addr2) =
			    *(volatile u_int16_t *)(addr1);
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 2 * (c - 1), addr2 += 2 * (c - 1);
		    c != 0; c--, addr1 -= 2, addr2 -= 2)
			*(volatile u_int16_t *)(addr2) =
			    *(volatile u_int16_t *)(addr1);
	}
}

void
x86_bus_space_mem_copy_4(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1 += 4, addr2 += 4)
			*(volatile u_int32_t *)(addr2) =
			    *(volatile u_int32_t *)(addr1);
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 4 * (c - 1), addr2 += 4 * (c - 1);
		    c != 0; c--, addr1 -= 4, addr2 -= 4)
			*(volatile u_int32_t *)(addr2) =
			    *(volatile u_int32_t *)(addr1);
	}
}

void
x86_bus_space_mem_copy_8(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		/* src after dest: copy forward */
		for (; c != 0; c--, addr1 += 8, addr2 += 8)
			*(volatile u_int64_t *)(addr2) =
			    *(volatile u_int64_t *)(addr1);
	} else {
		/* dest after src: copy backwards */
		for (addr1 += 8 * (c - 1), addr2 += 8 * (c - 1);
		    c != 0; c--, addr1 -= 8, addr2 -= 8)
			*(volatile u_int64_t *)(addr2) =
			    *(volatile u_int64_t *)(addr1);
	}
}

void *
x86_bus_space_mem_vaddr(bus_space_handle_t h)
{
	return ((void *)h);
}

paddr_t
x86_bus_space_mem_mmap(bus_addr_t addr, off_t off, int prot, int flags)
{
	/*
	 * "addr" is the base address of the device we're mapping.
	 * "off" is the offset into that device.
	 *
	 * Note we are called for each "page" in the device that
	 * the upper layers want to map.
	 */
	return (addr + off);
}
