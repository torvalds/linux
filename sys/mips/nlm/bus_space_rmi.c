/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/ktr.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cache.h>

static int
rmi_bus_space_map(void *t, bus_addr_t addr,
    bus_size_t size, int flags,
    bus_space_handle_t *bshp);

static void
rmi_bus_space_unmap(void *t, bus_space_handle_t bsh,
    bus_size_t size);

static int
rmi_bus_space_subregion(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size,
    bus_space_handle_t *nbshp);

static u_int8_t
rmi_bus_space_read_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset);

static u_int16_t
rmi_bus_space_read_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset);

static u_int32_t
rmi_bus_space_read_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset);

static void
rmi_bus_space_read_multi_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int8_t *addr,
    size_t count);

static void
rmi_bus_space_read_multi_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_read_multi_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int32_t *addr,
    size_t count);

static void
rmi_bus_space_read_region_1(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, u_int8_t *addr,
    size_t count);

static void
rmi_bus_space_read_region_2(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_read_region_4(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, u_int32_t *addr,
    size_t count);

static void
rmi_bus_space_write_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int8_t value);

static void
rmi_bus_space_write_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int16_t value);

static void
rmi_bus_space_write_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int32_t value);

static void
rmi_bus_space_write_multi_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int8_t *addr,
    size_t count);

static void
rmi_bus_space_write_multi_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_write_multi_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int32_t *addr,
    size_t count);

static void
rmi_bus_space_write_region_2(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset,
    const u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_write_region_4(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset,
    const u_int32_t *addr,
    size_t count);


static void
rmi_bus_space_set_region_2(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, u_int16_t value,
    size_t count);
static void
rmi_bus_space_set_region_4(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset, u_int32_t value,
    size_t count);

static void
rmi_bus_space_barrier(void *tag __unused, bus_space_handle_t bsh __unused,
    bus_size_t offset __unused, bus_size_t len __unused, int flags);

static void
rmi_bus_space_copy_region_2(void *t,
    bus_space_handle_t bsh1,
    bus_size_t off1,
    bus_space_handle_t bsh2,
    bus_size_t off2, size_t count);

u_int8_t
rmi_bus_space_read_stream_1(void *t, bus_space_handle_t handle,
    bus_size_t offset);

static u_int16_t
rmi_bus_space_read_stream_2(void *t, bus_space_handle_t handle,
    bus_size_t offset);

static u_int32_t
rmi_bus_space_read_stream_4(void *t, bus_space_handle_t handle,
    bus_size_t offset);
static void
rmi_bus_space_read_multi_stream_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int8_t *addr,
    size_t count);

static void
rmi_bus_space_read_multi_stream_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_read_multi_stream_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset, u_int32_t *addr,
    size_t count);

void
rmi_bus_space_write_stream_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int8_t value);
static void
rmi_bus_space_write_stream_2(void *t, bus_space_handle_t handle,
    bus_size_t offset, u_int16_t value);

static void
rmi_bus_space_write_stream_4(void *t, bus_space_handle_t handle,
    bus_size_t offset, u_int32_t value);

static void
rmi_bus_space_write_multi_stream_1(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int8_t *addr,
    size_t count);
static void
rmi_bus_space_write_multi_stream_2(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int16_t *addr,
    size_t count);

static void
rmi_bus_space_write_multi_stream_4(void *t,
    bus_space_handle_t handle,
    bus_size_t offset,
    const u_int32_t *addr,
    size_t count);

#define TODO() printf("XLP bus space: '%s' unimplemented\n", __func__)

static struct bus_space local_rmi_bus_space = {
	/* cookie */
	(void *)0,

	/* mapping/unmapping */
	rmi_bus_space_map,
	rmi_bus_space_unmap,
	rmi_bus_space_subregion,

	/* allocation/deallocation */
	NULL,
	NULL,

	/* barrier */
	rmi_bus_space_barrier,

	/* read (single) */
	rmi_bus_space_read_1,
	rmi_bus_space_read_2,
	rmi_bus_space_read_4,
	NULL,

	/* read multiple */
	rmi_bus_space_read_multi_1,
	rmi_bus_space_read_multi_2,
	rmi_bus_space_read_multi_4,
	NULL,

	/* read region */
	rmi_bus_space_read_region_1,
	rmi_bus_space_read_region_2,
	rmi_bus_space_read_region_4,
	NULL,

	/* write (single) */
	rmi_bus_space_write_1,
	rmi_bus_space_write_2,
	rmi_bus_space_write_4,
	NULL,

	/* write multiple */
	rmi_bus_space_write_multi_1,
	rmi_bus_space_write_multi_2,
	rmi_bus_space_write_multi_4,
	NULL,

	/* write region */
	NULL,
	rmi_bus_space_write_region_2,
	rmi_bus_space_write_region_4,
	NULL,

	/* set multiple */
	NULL,
	NULL,
	NULL,
	NULL,

	/* set region */
	NULL,
	rmi_bus_space_set_region_2,
	rmi_bus_space_set_region_4,
	NULL,

	/* copy */
	NULL,
	rmi_bus_space_copy_region_2,
	NULL,
	NULL,

	/* read (single) stream */
	rmi_bus_space_read_stream_1,
	rmi_bus_space_read_stream_2,
	rmi_bus_space_read_stream_4,
	NULL,

	/* read multiple stream */
	rmi_bus_space_read_multi_stream_1,
	rmi_bus_space_read_multi_stream_2,
	rmi_bus_space_read_multi_stream_4,
	NULL,

	/* read region stream */
	rmi_bus_space_read_region_1,
	rmi_bus_space_read_region_2,
	rmi_bus_space_read_region_4,
	NULL,

	/* write (single) stream */
	rmi_bus_space_write_stream_1,
	rmi_bus_space_write_stream_2,
	rmi_bus_space_write_stream_4,
	NULL,

	/* write multiple stream */
	rmi_bus_space_write_multi_stream_1,
	rmi_bus_space_write_multi_stream_2,
	rmi_bus_space_write_multi_stream_4,
	NULL,

	/* write region stream */
	NULL,
	rmi_bus_space_write_region_2,
	rmi_bus_space_write_region_4,
	NULL,
};

/* generic bus_space tag */
bus_space_tag_t rmi_bus_space = &local_rmi_bus_space;

/*
 * Map a region of device bus space into CPU virtual address space.
 */
static int
rmi_bus_space_map(void *t __unused, bus_addr_t addr,
    bus_size_t size __unused, int flags __unused,
    bus_space_handle_t *bshp)
{

	*bshp = MIPS_PHYS_TO_DIRECT_UNCACHED(addr);
	return (0);
}

/*
 * Unmap a region of device bus space.
 */
static void
rmi_bus_space_unmap(void *t __unused, bus_space_handle_t bsh __unused,
    bus_size_t size __unused)
{
}

/*
 * Get a new handle for a subregion of an already-mapped area of bus space.
 */

static int
rmi_bus_space_subregion(void *t __unused, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size __unused,
    bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return (0);
}

/*
 * Read a 1, 2, 4, or 8 byte quantity from bus space
 * described by tag/handle/offset.
 */

static u_int8_t
rmi_bus_space_read_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (u_int8_t) (*(volatile u_int8_t *)(handle + offset));
}

static u_int16_t
rmi_bus_space_read_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (u_int16_t)(*(volatile u_int16_t *)(handle + offset));
}

static u_int32_t
rmi_bus_space_read_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile u_int32_t *)(handle + offset));
}


/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle/offset and copy into buffer provided.
 */
static void
rmi_bus_space_read_multi_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int8_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_read_multi_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int16_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_read_multi_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int32_t *addr, size_t count)
{
	TODO();
}

/*
 * Write the 1, 2, 4, or 8 byte value `value' to bus space
 * described by tag/handle/offset.
 */

static void
rmi_bus_space_write_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int8_t value)
{
	*(volatile u_int8_t *)(handle + offset) =  value;
}

static void
rmi_bus_space_write_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int16_t value)
{
	*(volatile u_int16_t *)(handle + offset) = value;
}

static void
rmi_bus_space_write_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int32_t value)
{
	*(volatile u_int32_t *)(handle + offset) = value;
}


/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer
 * provided to bus space described by tag/handle/offset.
 */


static void
rmi_bus_space_write_multi_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int8_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_write_multi_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int16_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_write_multi_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int32_t *addr, size_t count)
{
	TODO();
}

/*
 * Write `count' 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle starting at `offset'.
 */

static void
rmi_bus_space_set_region_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int16_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 2)
		(*(volatile u_int32_t *)(addr)) = value;
}

static void
rmi_bus_space_set_region_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int32_t value, size_t count)
{
	bus_addr_t addr = bsh + offset;

	for (; count != 0; count--, addr += 4)
		(*(volatile u_int32_t *)(addr)) = value;
}


/*
 * Copy `count' 1, 2, 4, or 8 byte values from bus space starting
 * at tag/bsh1/off1 to bus space starting at tag/bsh2/off2.
 */
static void
rmi_bus_space_copy_region_2(void *t, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2,
    bus_size_t off2, size_t count)
{
	printf("bus_space_copy_region_2 - unimplemented\n");
}

/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle/offset and copy into buffer provided.
 */

u_int8_t
rmi_bus_space_read_stream_1(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{

	return *((volatile u_int8_t *)(handle + offset));
}


static u_int16_t
rmi_bus_space_read_stream_2(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{
	return *(volatile u_int16_t *)(handle + offset);
}


static u_int32_t
rmi_bus_space_read_stream_4(void *t, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile u_int32_t *)(handle + offset));
}


static void
rmi_bus_space_read_multi_stream_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int8_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_read_multi_stream_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int16_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_read_multi_stream_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int32_t *addr, size_t count)
{
	TODO();
}


/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
void
rmi_bus_space_read_region_1(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int8_t *addr, size_t count)
{
	TODO();
}

void
rmi_bus_space_read_region_2(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int16_t *addr, size_t count)
{
	TODO();
}

void
rmi_bus_space_read_region_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, u_int32_t *addr, size_t count)
{
	bus_addr_t baddr = bsh + offset;

	while (count--) {
		*addr++ = (*(volatile u_int32_t *)(baddr));
		baddr += 4;
	}
}

void
rmi_bus_space_write_stream_1(void *t, bus_space_handle_t handle,
    bus_size_t offset, u_int8_t value)
{
	TODO();
}


static void
rmi_bus_space_write_stream_2(void *t, bus_space_handle_t handle,
    bus_size_t offset, u_int16_t value)
{
	TODO();
}


static void
rmi_bus_space_write_stream_4(void *t, bus_space_handle_t handle,
    bus_size_t offset, u_int32_t value)
{
	TODO();
}


static void
rmi_bus_space_write_multi_stream_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int8_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_write_multi_stream_2(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int16_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_write_multi_stream_4(void *tag, bus_space_handle_t handle,
    bus_size_t offset, const u_int32_t *addr, size_t count)
{
	TODO();
}

void
rmi_bus_space_write_region_2(void *t,
    bus_space_handle_t bsh,
    bus_size_t offset,
    const u_int16_t *addr,
    size_t count)
{
	TODO();
}

void
rmi_bus_space_write_region_4(void *t, bus_space_handle_t bsh,
    bus_size_t offset, const u_int32_t *addr, size_t count)
{
	TODO();
}

static void
rmi_bus_space_barrier(void *tag __unused, bus_space_handle_t bsh __unused,
    bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
}

/*
 * need a special bus space for this, because the Netlogic SoC
 * UART allows only 32 bit access to its registers
 */

static u_int8_t
rmi_uart_bus_space_read_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (u_int8_t)(*(volatile u_int32_t *)(handle + offset));
}

static void
rmi_uart_bus_space_write_1(void *tag, bus_space_handle_t handle,
    bus_size_t offset, u_int8_t value)
{
	*(volatile u_int32_t *)(handle + offset) =  value;
}

static struct bus_space local_rmi_uart_bus_space = {
	/* cookie */
	(void *)0,

	/* mapping/unmapping */
	rmi_bus_space_map,
	rmi_bus_space_unmap,
	rmi_bus_space_subregion,

	/* allocation/deallocation */
	NULL,
	NULL,

	/* barrier */
	rmi_bus_space_barrier,

	/* read (single) */
	rmi_uart_bus_space_read_1, NULL, NULL, NULL,

	/* read multiple */
	NULL, NULL, NULL, NULL,

	/* read region */
	NULL, NULL, NULL, NULL,

	/* write (single) */
	rmi_uart_bus_space_write_1, NULL, NULL, NULL,

	/* write multiple */
	NULL, NULL, NULL, NULL,

	/* write region */
	NULL, NULL, NULL, NULL,

	/* set multiple */
	NULL, NULL, NULL, NULL,

	/* set region */
	NULL, NULL, NULL, NULL,

	/* copy */
	NULL, NULL, NULL, NULL,

	/* read (single) stream */
	NULL, NULL, NULL, NULL,

	/* read multiple stream */
	NULL, NULL, NULL, NULL,

	/* read region stream */
	NULL, NULL, NULL, NULL,

	/* write (single) stream */
	NULL, NULL, NULL, NULL,

	/* write multiple stream */
	NULL, NULL, NULL, NULL,

	/* write region stream */
	NULL, NULL, NULL, NULL,
};

/* generic bus_space tag */
bus_space_tag_t rmi_uart_bus_space = &local_rmi_uart_bus_space;
