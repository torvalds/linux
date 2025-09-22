/*      $OpenBSD: sev_bus_space.c,v 1.1 2025/09/17 18:39:50 sf Exp $    */
/*
 * Copyright (c) 2025 genua GmbH
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/ghcb.h>

/* Re-exports form the original amd64/bus_space.c */
void *		x86_bus_space_io_vaddr(bus_space_handle_t);
paddr_t		x86_bus_space_io_mmap(bus_addr_t, off_t, int, int);
void *		x86_bus_space_mem_vaddr(bus_space_handle_t);
paddr_t		x86_bus_space_mem_mmap(bus_addr_t, off_t, int, int);
uint64_t	x86_bus_space_io_read_8(bus_space_handle_t, bus_size_t);
void		x86_bus_space_io_read_multi_8(bus_space_handle_t, bus_size_t,
		    uint64_t *, bus_size_t);
void		x86_bus_space_io_read_region_8(bus_space_handle_t, bus_size_t,
		    uint64_t *, bus_size_t);
void		x86_bus_space_io_write_8(bus_space_handle_t, bus_size_t,
		    uint64_t);
void		x86_bus_space_io_write_multi_8(bus_space_handle_t,
		    bus_size_t, const uint64_t *, bus_size_t);
void		x86_bus_space_io_write_region_8(bus_space_handle_t,
		    bus_size_t, const uint64_t *, bus_size_t);
void		x86_bus_space_io_set_multi_8(bus_space_handle_t, bus_size_t,
		    uint64_t, size_t);
void		x86_bus_space_io_set_region_8(bus_space_handle_t, bus_size_t,
		    uint64_t, size_t);
void		x86_bus_space_io_copy_8(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

uint8_t		sev_ghcb_io_read_1(bus_space_handle_t, bus_size_t);
uint16_t	sev_ghcb_io_read_2(bus_space_handle_t, bus_size_t);
uint32_t	sev_ghcb_io_read_4(bus_space_handle_t, bus_size_t);

void		sev_ghcb_io_read_multi_1(bus_space_handle_t, bus_size_t,
		    uint8_t *, bus_size_t);
void		sev_ghcb_io_read_multi_2(bus_space_handle_t, bus_size_t,
		    uint16_t *, bus_size_t);
void		sev_ghcb_io_read_multi_4(bus_space_handle_t, bus_size_t,
		    uint32_t *, bus_size_t);

void		sev_ghcb_io_read_region_1(bus_space_handle_t, bus_size_t,
		    uint8_t *, bus_size_t);
void		sev_ghcb_io_read_region_2(bus_space_handle_t, bus_size_t,
		    uint16_t *, bus_size_t);
void		sev_ghcb_io_read_region_4(bus_space_handle_t, bus_size_t,
		    uint32_t *, bus_size_t);

void		sev_ghcb_io_write_1(bus_space_handle_t, bus_size_t,
		    uint8_t);
void		sev_ghcb_io_write_2(bus_space_handle_t, bus_size_t,
		    uint16_t);
void		sev_ghcb_io_write_4(bus_space_handle_t, bus_size_t,
		    uint32_t);

void		sev_ghcb_io_write_multi_1(bus_space_handle_t,
		    bus_size_t, const uint8_t *, bus_size_t);
void		sev_ghcb_io_write_multi_2(bus_space_handle_t,
		    bus_size_t, const uint16_t *, bus_size_t);
void		sev_ghcb_io_write_multi_4(bus_space_handle_t,
		    bus_size_t, const uint32_t *, bus_size_t);

void		sev_ghcb_io_write_region_1(bus_space_handle_t,
		    bus_size_t, const uint8_t *, bus_size_t);
void		sev_ghcb_io_write_region_2(bus_space_handle_t,
		    bus_size_t, const uint16_t *, bus_size_t);
void		sev_ghcb_io_write_region_4(bus_space_handle_t,
		    bus_size_t, const uint32_t *, bus_size_t);

void		sev_ghcb_io_set_multi_1(bus_space_handle_t, bus_size_t,
		    uint8_t, size_t);
void		sev_ghcb_io_set_multi_2(bus_space_handle_t, bus_size_t,
		    uint16_t, size_t);
void		sev_ghcb_io_set_multi_4(bus_space_handle_t, bus_size_t,
		    uint32_t, size_t);

void		sev_ghcb_io_set_region_1(bus_space_handle_t, bus_size_t,
		    uint8_t, size_t);
void		sev_ghcb_io_set_region_2(bus_space_handle_t, bus_size_t,
		    uint16_t, size_t);
void		sev_ghcb_io_set_region_4(bus_space_handle_t, bus_size_t,
		    uint32_t, size_t);

void		sev_ghcb_io_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		sev_ghcb_io_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		sev_ghcb_io_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

const struct x86_bus_space_ops sev_ghcb_bus_space_io_ops = {
	sev_ghcb_io_read_1,
	sev_ghcb_io_read_2,
	sev_ghcb_io_read_4,
	x86_bus_space_io_read_8,
	sev_ghcb_io_read_multi_1,
	sev_ghcb_io_read_multi_2,
	sev_ghcb_io_read_multi_4,
	x86_bus_space_io_read_multi_8,
	sev_ghcb_io_read_region_1,
	sev_ghcb_io_read_region_2,
	sev_ghcb_io_read_region_4,
	x86_bus_space_io_read_region_8,
	sev_ghcb_io_write_1,
	sev_ghcb_io_write_2,
	sev_ghcb_io_write_4,
	x86_bus_space_io_write_8,
	sev_ghcb_io_write_multi_1,
	sev_ghcb_io_write_multi_2,
	sev_ghcb_io_write_multi_4,
	x86_bus_space_io_write_multi_8,
	sev_ghcb_io_write_region_1,
	sev_ghcb_io_write_region_2,
	sev_ghcb_io_write_region_4,
	x86_bus_space_io_write_region_8,
	sev_ghcb_io_set_multi_1,
	sev_ghcb_io_set_multi_2,
	sev_ghcb_io_set_multi_4,
	x86_bus_space_io_set_multi_8,
	sev_ghcb_io_set_region_1,
	sev_ghcb_io_set_region_2,
	sev_ghcb_io_set_region_4,
	x86_bus_space_io_set_region_8,
	sev_ghcb_io_copy_1,
	sev_ghcb_io_copy_2,
	sev_ghcb_io_copy_4,
	x86_bus_space_io_copy_8,
	x86_bus_space_io_vaddr,
	x86_bus_space_io_mmap
};

uint8_t	sev_ghcb_mem_read_1(bus_space_handle_t, bus_size_t);
uint16_t	sev_ghcb_mem_read_2(bus_space_handle_t, bus_size_t);
uint32_t	sev_ghcb_mem_read_4(bus_space_handle_t, bus_size_t);
uint64_t	sev_ghcb_mem_read_8(bus_space_handle_t, bus_size_t);

void		sev_ghcb_mem_read_multi_1(bus_space_handle_t, bus_size_t,
		    uint8_t *, bus_size_t);
void		sev_ghcb_mem_read_multi_2(bus_space_handle_t, bus_size_t,
		    uint16_t *, bus_size_t);
void		sev_ghcb_mem_read_multi_4(bus_space_handle_t, bus_size_t,
		    uint32_t *, bus_size_t);
void		sev_ghcb_mem_read_multi_8(bus_space_handle_t, bus_size_t,
		    uint64_t *, bus_size_t);

void		sev_ghcb_mem_read_region_1(bus_space_handle_t, bus_size_t,
		    uint8_t *, bus_size_t);
void		sev_ghcb_mem_read_region_2(bus_space_handle_t, bus_size_t,
		    uint16_t *, bus_size_t);
void		sev_ghcb_mem_read_region_4(bus_space_handle_t, bus_size_t,
		    uint32_t *, bus_size_t);
void		sev_ghcb_mem_read_region_8(bus_space_handle_t, bus_size_t,
		    uint64_t *, bus_size_t);

void		sev_ghcb_mem_write_1(bus_space_handle_t, bus_size_t,
		    uint8_t);
void		sev_ghcb_mem_write_2(bus_space_handle_t, bus_size_t,
		    uint16_t);
void		sev_ghcb_mem_write_4(bus_space_handle_t, bus_size_t,
		    uint32_t);
void		sev_ghcb_mem_write_8(bus_space_handle_t, bus_size_t,
		    uint64_t);

void		sev_ghcb_mem_write_multi_1(bus_space_handle_t,
		    bus_size_t, const uint8_t *, bus_size_t);
void		sev_ghcb_mem_write_multi_2(bus_space_handle_t,
		    bus_size_t, const uint16_t *, bus_size_t);
void		sev_ghcb_mem_write_multi_4(bus_space_handle_t,
		    bus_size_t, const uint32_t *, bus_size_t);
void		sev_ghcb_mem_write_multi_8(bus_space_handle_t,
		    bus_size_t, const uint64_t *, bus_size_t);

void		sev_ghcb_mem_write_region_1(bus_space_handle_t,
		    bus_size_t, const uint8_t *, bus_size_t);
void		sev_ghcb_mem_write_region_2(bus_space_handle_t,
		    bus_size_t, const uint16_t *, bus_size_t);
void		sev_ghcb_mem_write_region_4(bus_space_handle_t,
		    bus_size_t, const uint32_t *, bus_size_t);
void		sev_ghcb_mem_write_region_8(bus_space_handle_t,
		    bus_size_t, const uint64_t *, bus_size_t);

void		sev_ghcb_mem_set_multi_1(bus_space_handle_t, bus_size_t,
		    uint8_t, size_t);
void		sev_ghcb_mem_set_multi_2(bus_space_handle_t, bus_size_t,
		    uint16_t, size_t);
void		sev_ghcb_mem_set_multi_4(bus_space_handle_t, bus_size_t,
		    uint32_t, size_t);
void		sev_ghcb_mem_set_multi_8(bus_space_handle_t, bus_size_t,
		    uint64_t, size_t);

void		sev_ghcb_mem_set_region_1(bus_space_handle_t, bus_size_t,
		    uint8_t, size_t);
void		sev_ghcb_mem_set_region_2(bus_space_handle_t, bus_size_t,
		    uint16_t, size_t);
void		sev_ghcb_mem_set_region_4(bus_space_handle_t, bus_size_t,
		    uint32_t, size_t);
void		sev_ghcb_mem_set_region_8(bus_space_handle_t, bus_size_t,
		    uint64_t, size_t);

void		sev_ghcb_mem_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		sev_ghcb_mem_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		sev_ghcb_mem_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		sev_ghcb_mem_copy_8(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

const struct x86_bus_space_ops sev_ghcb_bus_space_mem_ops = {
	sev_ghcb_mem_read_1,
	sev_ghcb_mem_read_2,
	sev_ghcb_mem_read_4,
	sev_ghcb_mem_read_8,
	sev_ghcb_mem_read_multi_1,
	sev_ghcb_mem_read_multi_2,
	sev_ghcb_mem_read_multi_4,
	sev_ghcb_mem_read_multi_8,
	sev_ghcb_mem_read_region_1,
	sev_ghcb_mem_read_region_2,
	sev_ghcb_mem_read_region_4,
	sev_ghcb_mem_read_region_8,
	sev_ghcb_mem_write_1,
	sev_ghcb_mem_write_2,
	sev_ghcb_mem_write_4,
	sev_ghcb_mem_write_8,
	sev_ghcb_mem_write_multi_1,
	sev_ghcb_mem_write_multi_2,
	sev_ghcb_mem_write_multi_4,
	sev_ghcb_mem_write_multi_8,
	sev_ghcb_mem_write_region_1,
	sev_ghcb_mem_write_region_2,
	sev_ghcb_mem_write_region_4,
	sev_ghcb_mem_write_region_8,
	sev_ghcb_mem_set_multi_1,
	sev_ghcb_mem_set_multi_2,
	sev_ghcb_mem_set_multi_4,
	sev_ghcb_mem_set_multi_8,
	sev_ghcb_mem_set_region_1,
	sev_ghcb_mem_set_region_2,
	sev_ghcb_mem_set_region_4,
	sev_ghcb_mem_set_region_8,
	sev_ghcb_mem_copy_1,
	sev_ghcb_mem_copy_2,
	sev_ghcb_mem_copy_4,
	sev_ghcb_mem_copy_8,
	x86_bus_space_mem_vaddr,
	x86_bus_space_mem_mmap
};

uint8_t
sev_ghcb_io_read_1(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_io_read_1(h + o);
}

uint16_t
sev_ghcb_io_read_2(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_io_read_2(h + o);
}

uint32_t
sev_ghcb_io_read_4(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_io_read_4(h + o);
}

void
sev_ghcb_io_read_multi_1(bus_space_handle_t h, bus_size_t o,
    uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_1(h + o);
}

void
sev_ghcb_io_read_multi_2(bus_space_handle_t h, bus_size_t o,
    uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_2(h + o);
}

void
sev_ghcb_io_read_multi_4(bus_space_handle_t h, bus_size_t o,
    uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_4(h + o);
}

void
sev_ghcb_io_read_region_1(bus_space_handle_t h,
    bus_size_t o, uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_1(h + o + i);
}

void
sev_ghcb_io_read_region_2(bus_space_handle_t h,
    bus_size_t o, uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_2(h + o + 2 * i);
}

void
sev_ghcb_io_read_region_4(bus_space_handle_t h,
    bus_size_t o, uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_io_read_4(h + o + 4 * i);
}

void
sev_ghcb_io_write_1(bus_space_handle_t h, bus_size_t o, uint8_t v)
{
	ghcb_io_write_1(h + o, v);
}

void
sev_ghcb_io_write_2(bus_space_handle_t h, bus_size_t o, uint16_t v)
{
	ghcb_io_write_2(h + o, v);
}

void
sev_ghcb_io_write_4(bus_space_handle_t h, bus_size_t o, uint32_t v)
{
	ghcb_io_write_4(h + o, v);
}

void
sev_ghcb_io_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_1(h + o, ptr[i]);
}

void
sev_ghcb_io_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_2(h + o, ptr[i]);
}

void
sev_ghcb_io_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_4(h + o, ptr[i]);
}

void
sev_ghcb_io_write_region_1(bus_space_handle_t h,
    bus_size_t o, const uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_1(h + o + i, ptr[i]);
}

void
sev_ghcb_io_write_region_2(bus_space_handle_t h,
    bus_size_t o, const uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_2(h + o + 2 * i, ptr[i]);
}

void
sev_ghcb_io_write_region_4(bus_space_handle_t h,
    bus_size_t o, const uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_io_write_4(h + o + 4 * i, ptr[i]);
}

void
sev_ghcb_io_set_multi_1(bus_space_handle_t h, bus_size_t o,
    uint8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_1(addr, v);
}

void
sev_ghcb_io_set_multi_2(bus_space_handle_t h, bus_size_t o,
    uint16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_2(addr, v);
}

void
sev_ghcb_io_set_multi_4(bus_space_handle_t h, bus_size_t o,
    uint32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_4(addr, v);
}

void
sev_ghcb_io_set_region_1(bus_space_handle_t h, bus_size_t o,
    uint8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_1(addr + i, v);
}

void
sev_ghcb_io_set_region_2(bus_space_handle_t h, bus_size_t o,
    uint16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_2(addr + 2 * i, v);
}

void
sev_ghcb_io_set_region_4(bus_space_handle_t h, bus_size_t o,
    uint32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_io_write_4(addr + 4 * i, v);
}

void
sev_ghcb_io_copy_1(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_io_write_1(addr2 + i, ghcb_io_read_1(addr1 + i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_io_write_1(addr2 + i - 1,
			    ghcb_io_read_1(addr1 + i - 1));
	}
}

void
sev_ghcb_io_copy_2(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_io_write_2(addr2 + 2 * i,
			    ghcb_io_read_2(addr1 + 2 * i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_io_write_2(addr2 + 2 * (i - 1),
			    ghcb_io_read_2(addr1 + 2 * (i - 1)));
	}
}

void
sev_ghcb_io_copy_4(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_io_write_4(addr2 + 4 * i,
			    ghcb_io_read_4(addr1 + 4 * i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_io_write_4(addr2 + 4 * (i - 1),
			    ghcb_io_read_4(addr1 + 4 * (i - 1)));
	}
}

uint8_t
sev_ghcb_mem_read_1(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_mem_read_1(h + o);
}

uint16_t
sev_ghcb_mem_read_2(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_mem_read_2(h + o);
}

uint32_t
sev_ghcb_mem_read_4(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_mem_read_4(h + o);
}

uint64_t
sev_ghcb_mem_read_8(bus_space_handle_t h, bus_size_t o)
{
	return ghcb_mem_read_8(h + o);
}

void
sev_ghcb_mem_read_multi_1(bus_space_handle_t h, bus_size_t o,
    uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_1(h + o);
}

void
sev_ghcb_mem_read_multi_2(bus_space_handle_t h, bus_size_t o,
    uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_2(h + o);
}

void
sev_ghcb_mem_read_multi_4(bus_space_handle_t h, bus_size_t o,
    uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_4(h + o);
}

void
sev_ghcb_mem_read_multi_8(bus_space_handle_t h, bus_size_t o,
    uint64_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_8(h + o);
}

void
sev_ghcb_mem_read_region_1(bus_space_handle_t h,
    bus_size_t o, uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_1(h + o + i);
}

void
sev_ghcb_mem_read_region_2(bus_space_handle_t h,
    bus_size_t o, uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_2(h + o + 2 * i);
}

void
sev_ghcb_mem_read_region_4(bus_space_handle_t h,
    bus_size_t o, uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_4(h + o + 4 * i);
}

void
sev_ghcb_mem_read_region_8(bus_space_handle_t h,
    bus_size_t o, uint64_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ptr[i] = ghcb_mem_read_8(h + o + 8 * i);
}

void
sev_ghcb_mem_write_1(bus_space_handle_t h, bus_size_t o, uint8_t v)
{
	ghcb_mem_write_1(h + o, v);
}

void
sev_ghcb_mem_write_2(bus_space_handle_t h, bus_size_t o, uint16_t v)
{
	ghcb_mem_write_2(h + o, v);
}

void
sev_ghcb_mem_write_4(bus_space_handle_t h, bus_size_t o, uint32_t v)
{
	ghcb_mem_write_4(h + o, v);
}

void
sev_ghcb_mem_write_8(bus_space_handle_t h, bus_size_t o, uint64_t v)
{
	ghcb_mem_write_8(h + o, v);
}

void
sev_ghcb_mem_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_1(h + o, ptr[i]);
}

void
sev_ghcb_mem_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_2(h + o, ptr[i]);
}

void
sev_ghcb_mem_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_4(h + o, ptr[i]);
}

void
sev_ghcb_mem_write_multi_8(bus_space_handle_t h,
    bus_size_t o, const uint64_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_8(h + o, ptr[i]);
}

void
sev_ghcb_mem_write_region_1(bus_space_handle_t h,
    bus_size_t o, const uint8_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_1(h + o + i, ptr[i]);
}

void
sev_ghcb_mem_write_region_2(bus_space_handle_t h,
    bus_size_t o, const uint16_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_2(h + o + 2 * i, ptr[i]);
}

void
sev_ghcb_mem_write_region_4(bus_space_handle_t h,
    bus_size_t o, const uint32_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_4(h + o + 4 * i, ptr[i]);
}

void
sev_ghcb_mem_write_region_8(bus_space_handle_t h,
    bus_size_t o, const uint64_t *ptr, bus_size_t cnt)
{
	for (bus_size_t i = 0; i < cnt; ++i)
		ghcb_mem_write_8(h + o + 8 * i, ptr[i]);
}

void
sev_ghcb_mem_set_multi_1(bus_space_handle_t h, bus_size_t o,
    uint8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_1(addr, v);
}

void
sev_ghcb_mem_set_multi_2(bus_space_handle_t h, bus_size_t o,
    uint16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_2(addr, v);
}

void
sev_ghcb_mem_set_multi_4(bus_space_handle_t h, bus_size_t o,
    uint32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_4(addr, v);
}

void
sev_ghcb_mem_set_multi_8(bus_space_handle_t h, bus_size_t o,
    uint64_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_8(addr, v);
}

void
sev_ghcb_mem_set_region_1(bus_space_handle_t h, bus_size_t o,
    uint8_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_1(addr + i, v);
}

void
sev_ghcb_mem_set_region_2(bus_space_handle_t h, bus_size_t o,
    uint16_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_2(addr + 2 * i, v);
}

void
sev_ghcb_mem_set_region_4(bus_space_handle_t h, bus_size_t o,
    uint32_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_4(addr + 4 * i, v);
}

void
sev_ghcb_mem_set_region_8(bus_space_handle_t h, bus_size_t o,
    uint64_t v, size_t c)
{
	bus_addr_t addr = h + o;

	for (bus_size_t i = 0; i < c; ++i)
		ghcb_mem_write_8(addr + 8 * i, v);
}

void
sev_ghcb_mem_copy_1( bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_mem_write_1(addr2 + i,
			    ghcb_mem_read_1(addr1 + i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_mem_write_1(addr2 + i - 1,
			    ghcb_mem_read_1(addr1 + i - 1));
	}
}

void
sev_ghcb_mem_copy_2(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_mem_write_2(addr2 + 2 * i,
			    ghcb_mem_read_2(addr1 + 2 * i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_mem_write_2(addr2 + 2 * (i - 1),
			    ghcb_mem_read_2(addr1 + 2 * (i - 1)));
	}
}

void
sev_ghcb_mem_copy_4(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_mem_write_4(addr2 + 4 * i,
			    ghcb_mem_read_4(addr1 + 4 * i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_mem_write_4(addr2 + 4 * (i - 1),
			    ghcb_mem_read_4(addr1 + 4 * (i - 1)));
	}
}

void
sev_ghcb_mem_copy_8(bus_space_handle_t h1, bus_size_t o1,
    bus_space_handle_t h2, bus_size_t o2, size_t c)
{
	bus_addr_t addr1 = h1 + o1;
	bus_addr_t addr2 = h2 + o2;

	if (addr1 >= addr2) {
		for (bus_size_t i = 0; i < c; ++i)
			ghcb_mem_write_8(addr2 + 8 * i,
			    ghcb_mem_read_8(addr1 + 8 * i));
	} else {
		for (bus_size_t i = c; i > 0; --i)
			ghcb_mem_write_8(addr2 + 8 * (i - 1),
			    ghcb_mem_read_8(addr1 + 8 * (i - 1)));
	}
}
