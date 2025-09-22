/*	$OpenBSD: bus_space.c,v 1.3 2024/03/27 23:10:18 kettenis Exp $	*/

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Simple generic bus access primitives.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <uvm/uvm_extern.h>

bus_space_t riscv64_bs_tag = {
	.bus_base = 0ULL, // XXX
	.bus_private = NULL,
	._space_read_1 =	generic_space_read_1,
	._space_write_1 =	generic_space_write_1,
	._space_read_2 =	generic_space_read_2,
	._space_write_2 =	generic_space_write_2,
	._space_read_4 =	generic_space_read_4,
	._space_write_4 =	generic_space_write_4,
	._space_read_8 =	generic_space_read_8,
	._space_write_8 =	generic_space_write_8,
	._space_read_raw_2 =	generic_space_read_raw_2,
	._space_write_raw_2 =	generic_space_write_raw_2,
	._space_read_raw_4 =	generic_space_read_raw_4,
	._space_write_raw_4 =	generic_space_write_raw_4,
	._space_read_raw_8 =	generic_space_read_raw_8,
	._space_write_raw_8 =	generic_space_write_raw_8,
	._space_map =		generic_space_map,
	._space_unmap =		generic_space_unmap,
	._space_subregion =	generic_space_region,
	._space_vaddr =		generic_space_vaddr,
	._space_mmap =		generic_space_mmap
};
bus_space_t *fdt_cons_bs_tag = &riscv64_bs_tag;

uint8_t
generic_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	uint8_t val = *(volatile uint8_t *)(h + o);
	__asm volatile ("fence i,ir" ::: "memory");
	return val;
}

uint16_t
generic_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	uint16_t val = *(volatile uint16_t *)(h + o);
	__asm volatile ("fence i,ir" ::: "memory");
	return val;
}

uint32_t
generic_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	uint32_t val = *(volatile uint32_t *)(h + o);
	__asm volatile ("fence i,ir" ::: "memory");
	return val;
}

uint64_t
generic_space_read_8(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	uint64_t val = *(volatile uint64_t *)(h + o);
	__asm volatile ("fence i,ir" ::: "memory");
	return val;
}

void
generic_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint8_t v)
{
	__asm volatile ("fence w,o" ::: "memory");
	*(volatile uint8_t *)(h + o) = v;
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint16_t v)
{
	__asm volatile ("fence w,o" ::: "memory");
	*(volatile uint16_t *)(h + o) = v;
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint32_t v)
{
	__asm volatile ("fence w,o" ::: "memory");
	*(volatile uint32_t *)(h + o) = v;
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_write_8(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint64_t v)
{
	__asm volatile ("fence w,o" ::: "memory");
	*(volatile uint64_t *)(h + o) = v;
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_read_raw_2(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    uint8_t *buf, bus_size_t len)
{
	volatile uint16_t *addr = (volatile uint16_t *)(h + o);
	len >>= 1;
	while (len-- != 0) {
		*(uint16_t *)buf = *addr;
		buf += 2;
	}
	__asm volatile ("fence i,ir" ::: "memory");
}

void
generic_space_write_raw_2(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    const uint8_t *buf, bus_size_t len)
{
	volatile uint16_t *addr = (volatile uint16_t *)(h + o);
	__asm volatile ("fence w,o" ::: "memory");
	len >>= 1;
	while (len-- != 0) {
		*addr = *(uint16_t *)buf;
		buf += 2;
	}
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_read_raw_4(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    uint8_t *buf, bus_size_t len)
{
	volatile uint32_t *addr = (volatile uint32_t *)(h + o);
	len >>= 2;
	while (len-- != 0) {
		*(uint32_t *)buf = *addr;
		buf += 4;
	}
	__asm volatile ("fence i,ir" ::: "memory");
}

void
generic_space_write_raw_4(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    const uint8_t *buf, bus_size_t len)
{
	volatile uint32_t *addr = (volatile uint32_t *)(h + o);
	__asm volatile ("fence w,o" ::: "memory");
	len >>= 2;
	while (len-- != 0) {
		*addr = *(uint32_t *)buf;
		buf += 4;
	}
	__asm volatile ("fence o,w" ::: "memory");
}

void
generic_space_read_raw_8(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    uint8_t *buf, bus_size_t len)
{
	volatile uint64_t *addr = (volatile uint64_t *)(h + o);
	len >>= 3;
	while (len-- != 0) {
		*(uint64_t *)buf = *addr;
		buf += 8;
	}
	__asm volatile ("fence i,ir" ::: "memory");
}

void
generic_space_write_raw_8(bus_space_tag_t t, bus_space_handle_t h, bus_addr_t o,
    const uint8_t *buf, bus_size_t len)
{
	volatile uint64_t *addr = (volatile uint64_t *)(h + o);
	__asm volatile ("fence w,o" ::: "memory");
	len >>= 3;
	while (len-- != 0) {
		*addr = *(uint64_t *)buf;
		buf += 8;
	}
	__asm volatile ("fence o,w" ::: "memory");
}

int
generic_space_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	u_long startpa, endpa, pa;
	vaddr_t va;
	int cache = flags & BUS_SPACE_MAP_CACHEABLE ?
	    PMAP_CACHE_WB : PMAP_CACHE_DEV;

	startpa = trunc_page(offs);
	endpa = round_page(offs + size);

	va = (vaddr_t)km_alloc(endpa - startpa, &kv_any, &kp_none, &kd_nowait);
	if (! va)
		return(ENOMEM);

	*bshp = (bus_space_handle_t)(va + (offs - startpa));

	for (pa = startpa; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE) {
		pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, cache);
	}
	pmap_update(pmap_kernel());

	return(0);
}

void
generic_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	vaddr_t	va, endva;

	va = trunc_page((vaddr_t)bsh);
	endva = round_page((vaddr_t)bsh + size);

	pmap_kremove(va, endva - va);
	pmap_update(pmap_kernel());
	km_free((void *)va, endva - va, &kv_any, &kp_none);
}

int
generic_space_region(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return 0;
}

void *
generic_space_vaddr(bus_space_tag_t t, bus_space_handle_t h)
{
	return (void *)h;
}

paddr_t
generic_space_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off,
    int prot, int flags)
{
	return (addr + off);
}
