/*	$OpenBSD: bus_space.c,v 1.5 2015/02/09 13:35:44 deraadt Exp $	*/
/*	$NetBSD: machdep.c,v 1.4 1996/10/16 19:33:11 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

extern int ppc_malloc_ok;
extern struct extent *devio_ex;

int bus_mem_add_mapping(bus_addr_t, bus_size_t, int, bus_space_handle_t *);
bus_addr_t bus_space_unmap_p(bus_space_tag_t, bus_space_handle_t,  bus_size_t);


/* BUS functions */
int
bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	int error;

	if  (POWERPC_BUS_TAG_BASE(t) == 0) {
		/* if bus has base of 0 fail. */
		return EINVAL;
	}

	/*
	 * The address may, or may not, be relative to the
	 * base address of this bus.
	 */
	if (bpa < POWERPC_BUS_TAG_BASE(t))
		bpa += POWERPC_BUS_TAG_BASE(t);

	if ((error = extent_alloc_region(devio_ex, bpa, size, EX_NOWAIT |
	    (ppc_malloc_ok ? EX_MALLOCOK : 0))))
		return error;

	if ((error = bus_mem_add_mapping(bpa, size, flags, bshp))) {
		if (extent_free(devio_ex, bpa, size, EX_NOWAIT |
			(ppc_malloc_ok ? EX_MALLOCOK : 0)))
		{
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
				bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}
	return error;
}

bus_addr_t
bus_space_unmap_p(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	bus_addr_t paddr;

	pmap_extract(pmap_kernel(), bsh, &paddr);
	bus_space_unmap((t), (bsh), (size));
	return paddr ;
}

void
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	bus_addr_t sva;
	bus_size_t off, len;
	bus_addr_t bpa;

	sva = trunc_page(bsh);
	off = bsh - sva;
	len = round_page(size+off);

	if (pmap_extract(pmap_kernel(), sva, &bpa) == TRUE) {
		if (extent_free(devio_ex, bpa | (bsh & PAGE_MASK), size,
		    EX_NOWAIT | (ppc_malloc_ok ? EX_MALLOCOK : 0)))
		{
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
				bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}

	pmap_kremove(sva, len);
	pmap_update(pmap_kernel());

	/* do not free memory which was stolen from the vm system */
	if (ppc_malloc_ok &&
	    ((sva >= VM_MIN_KERNEL_ADDRESS) && (sva < VM_MAX_KERNEL_ADDRESS)))
		km_free((void *)sva, len, &kv_any, &kp_none);
}

paddr_t
bus_space_mmap(bus_space_tag_t t, bus_addr_t bpa, off_t off, int prot,
    int flags)
{
	int pmapflags = PMAP_NOCACHE;

	if (POWERPC_BUS_TAG_BASE(t) == 0)
		return (-1);

	/*
	 * The address may, or may not, be relative to the
	 * base address of this bus.
	 */
	if (bpa < POWERPC_BUS_TAG_BASE(t))
		bpa += POWERPC_BUS_TAG_BASE(t);

	if (flags & BUS_SPACE_MAP_CACHEABLE)
		pmapflags &= ~PMAP_NOCACHE;

	return ((bpa + off) | pmapflags);
}

vaddr_t ppc_kvm_stolen = VM_KERN_ADDRESS_SIZE;

int
bus_mem_add_mapping(bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	bus_addr_t vaddr;
	bus_addr_t spa, epa;
	bus_size_t off, len;
	int pmapflags;

	spa = trunc_page(bpa);
	epa = bpa + size;
	off = bpa - spa;
	len = round_page(size+off);

#ifdef DIAGNOSTIC
	if (epa <= spa && epa != 0)
		panic("bus_mem_add_mapping: overflow");
#endif

	if (ppc_malloc_ok == 0) {
		/* need to steal vm space before kernel vm is initialized */
		vaddr = VM_MIN_KERNEL_ADDRESS + ppc_kvm_stolen;
		ppc_kvm_stolen += len;
		if (ppc_kvm_stolen > PPC_SEGMENT_LENGTH) {
			panic("ppc_kvm_stolen, out of space");
		}
	} else {
		vaddr = (vaddr_t)km_alloc(len, &kv_any, &kp_none, &kd_nowait);
		if (vaddr == 0)
			return (ENOMEM);
	}
	*bshp = vaddr + off;

	if (flags & BUS_SPACE_MAP_CACHEABLE)
		pmapflags = PMAP_WT;
	else
		pmapflags = PMAP_NOCACHE;

	for (; len > 0; len -= PAGE_SIZE) {
		pmap_kenter_pa(vaddr, spa | pmapflags, PROT_READ | PROT_WRITE);
		spa += PAGE_SIZE;
		vaddr += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());

	return (0);
}

int
bus_space_alloc(bus_space_tag_t tag, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *addrp, bus_space_handle_t *handlep)
{

	panic("bus_space_alloc: unimplemented");
}

void
bus_space_free(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{

	panic("bus_space_free: unimplemented");
}

void *
mapiodev(paddr_t pa, psize_t len)
{
	paddr_t spa;
	vaddr_t vaddr, va;
	int off;
	int size;

	spa = trunc_page(pa);
	off = pa - spa;
	size = round_page(off+len);
	if (ppc_malloc_ok == 0) {
		/* need to steal vm space before kernel vm is initialized */
		va = VM_MIN_KERNEL_ADDRESS + ppc_kvm_stolen;
		ppc_kvm_stolen += size;
		if (ppc_kvm_stolen > PPC_SEGMENT_LENGTH) {
			panic("ppc_kvm_stolen, out of space");
		}
	} else {
		va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
		if (va == 0)
			return (NULL);
	}

	for (vaddr = va; size > 0; size -= PAGE_SIZE) {
		pmap_kenter_pa(vaddr, spa, PROT_READ | PROT_WRITE);
		spa += PAGE_SIZE;
		vaddr += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());

	return (void *) (va+off);
}

void
unmapiodev(void *kva, psize_t p_size)
{
	vaddr_t vaddr;
	int size;

	size = round_page(p_size);

	vaddr = trunc_page((vaddr_t)kva);

	pmap_kremove(vaddr, size);
	pmap_update(pmap_kernel());

	km_free((void *)vaddr, size, &kv_any, &kp_none);
}


/*
 * probably should be ppc_space_copy
 */

#define _CONCAT(A,B) A ## B
#define __C(A,B)	_CONCAT(A,B)

#define BUS_SPACE_COPY_N(BYTES,TYPE)					\
void									\
__C(bus_space_copy_,BYTES)(void *v, bus_space_handle_t h1,		\
    bus_size_t o1, bus_space_handle_t h2, bus_size_t o2,		\
    bus_size_t c)							\
{									\
	TYPE *src, *dst;						\
	int i;								\
									\
	src = (TYPE *) (h1+o1);						\
	dst = (TYPE *) (h2+o2);						\
									\
	if (h1 == h2 && o2 > o1)					\
		for (i = c-1; i >= 0; i--)				\
			dst[i] = src[i];				\
	else								\
		for (i = 0; i < c; i++)					\
			dst[i] = src[i];				\
}
BUS_SPACE_COPY_N(1,u_int8_t)
BUS_SPACE_COPY_N(2,u_int16_t)
BUS_SPACE_COPY_N(4,u_int32_t)

void
bus_space_set_region_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    u_int8_t val, bus_size_t c)
{
	u_int8_t *dst;
	int i;

	dst = (u_int8_t *) (h+o);

	for (i = 0; i < c; i++)
		dst[i] = val;
}

void
bus_space_set_region_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    u_int16_t val, bus_size_t c)
{
	u_int16_t *dst;
	int i;

	dst = (u_int16_t *) (h+o);
	val = swap16(val);

	for (i = 0; i < c; i++)
		dst[i] = val;
}
void
bus_space_set_region_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    u_int32_t val, bus_size_t c)
{
	u_int32_t *dst;
	int i;

	dst = (u_int32_t *) (h+o);
	val = swap32(val);

	for (i = 0; i < c; i++)
		dst[i] = val;
}

#define BUS_SPACE_READ_RAW_MULTI_N(BYTES,SHIFT,TYPE)			\
void									\
__C(bus_space_read_raw_multi_,BYTES)(bus_space_tag_t bst,		\
    bus_space_handle_t h, bus_addr_t o, u_int8_t *dst, bus_size_t size)	\
{									\
	TYPE *src;							\
	TYPE *rdst = (TYPE *)dst;					\
	int i;								\
	int count = size >> SHIFT;					\
									\
	src = (TYPE *)(h+o);						\
	for (i = 0; i < count; i++) {					\
		rdst[i] = *src;						\
		__asm__("eieio");					\
	}								\
}
BUS_SPACE_READ_RAW_MULTI_N(2,1,u_int16_t)
BUS_SPACE_READ_RAW_MULTI_N(4,2,u_int32_t)

#define BUS_SPACE_WRITE_RAW_MULTI_N(BYTES,SHIFT,TYPE)			\
void									\
__C(bus_space_write_raw_multi_,BYTES)( bus_space_tag_t bst,		\
    bus_space_handle_t h, bus_addr_t o, const u_int8_t *src,		\
    bus_size_t size)							\
{									\
	int i;								\
	TYPE *dst;							\
	TYPE *rsrc = (TYPE *)src;					\
	int count = size >> SHIFT;					\
									\
	dst = (TYPE *)(h+o);						\
	for (i = 0; i < count; i++) {					\
		*dst = rsrc[i];						\
		__asm__("eieio");					\
	}								\
}

BUS_SPACE_WRITE_RAW_MULTI_N(2,1,u_int16_t)
BUS_SPACE_WRITE_RAW_MULTI_N(4,2,u_int32_t)

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return (0);
}
