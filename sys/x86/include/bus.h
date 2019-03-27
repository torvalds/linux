/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-2-Clause-NetBSDE
 *
 * Copyright (c) KATO Takenori, 1999.
 *
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD$
 */

/*	$NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $	*/

/*-
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

/*-
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
 */

#ifndef _X86_BUS_H_
#define _X86_BUS_H_

#include <machine/_bus.h>
#include <machine/cpufunc.h>

#ifndef __GNUCLIKE_ASM
#error "no assembler code for your compiler"
#endif

/*
 * Values for the x86 bus space tag, not to be used directly by MI code.
 */
#define	X86_BUS_SPACE_IO	0	/* space is i/o space */
#define	X86_BUS_SPACE_MEM	1	/* space is mem space */

#define BUS_SPACE_MAXSIZE_24BIT	0xFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXSIZE	0xFFFFFFFF
#define BUS_SPACE_MAXADDR_24BIT	0xFFFFFF
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFF
#if defined(__amd64__) || defined(PAE)
#define BUS_SPACE_MAXADDR_48BIT	0xFFFFFFFFFFFFULL
#define BUS_SPACE_MAXADDR	0xFFFFFFFFFFFFFFFFULL
#else
#define BUS_SPACE_MAXADDR	0xFFFFFFFF
#endif

#define BUS_SPACE_INVALID_DATA	(~0)
#define BUS_SPACE_UNRESTRICTED	(~0)

/*
 * Map a region of device bus space into CPU virtual address space.
 */

int bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp);

/*
 * Unmap a region of device bus space.
 */

void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t bsh,
    bus_size_t size);

/*
 * Get a new handle for a subregion of an already-mapped area of bus space.
 */

static __inline int bus_space_subregion(bus_space_tag_t t,
					bus_space_handle_t bsh,
					bus_size_t offset, bus_size_t size,
					bus_space_handle_t *nbshp);

static __inline int
bus_space_subregion(bus_space_tag_t t __unused, bus_space_handle_t bsh,
		    bus_size_t offset, bus_size_t size __unused,
		    bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

/*
 * Allocate a region of memory that is accessible to devices in bus space.
 */

int	bus_space_alloc(bus_space_tag_t t, bus_addr_t rstart,
			bus_addr_t rend, bus_size_t size, bus_size_t align,
			bus_size_t boundary, int flags, bus_addr_t *addrp,
			bus_space_handle_t *bshp);

/*
 * Free a region of bus space accessible memory.
 */

static __inline void bus_space_free(bus_space_tag_t t, bus_space_handle_t bsh,
				    bus_size_t size);

static __inline void
bus_space_free(bus_space_tag_t t __unused, bus_space_handle_t bsh __unused,
	       bus_size_t size __unused)
{
}


/*
 * Read a 1, 2, 4, or 8 byte quantity from bus space
 * described by tag/handle/offset.
 */
static __inline u_int8_t bus_space_read_1(bus_space_tag_t tag,
					  bus_space_handle_t handle,
					  bus_size_t offset);

static __inline u_int16_t bus_space_read_2(bus_space_tag_t tag,
					   bus_space_handle_t handle,
					   bus_size_t offset);

static __inline u_int32_t bus_space_read_4(bus_space_tag_t tag,
					   bus_space_handle_t handle,
					   bus_size_t offset);

#ifdef __amd64__
static __inline uint64_t bus_space_read_8(bus_space_tag_t tag,
					  bus_space_handle_t handle,
					  bus_size_t offset);
#endif

static __inline u_int8_t
bus_space_read_1(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{

	if (tag == X86_BUS_SPACE_IO)
		return (inb(handle + offset));
	return (*(volatile u_int8_t *)(handle + offset));
}

static __inline u_int16_t
bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{

	if (tag == X86_BUS_SPACE_IO)
		return (inw(handle + offset));
	return (*(volatile u_int16_t *)(handle + offset));
}

static __inline u_int32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{

	if (tag == X86_BUS_SPACE_IO)
		return (inl(handle + offset));
	return (*(volatile u_int32_t *)(handle + offset));
}

#ifdef __amd64__
static __inline uint64_t
bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{

	if (tag == X86_BUS_SPACE_IO) /* No 8 byte IO space access on x86 */
		return (BUS_SPACE_INVALID_DATA);
	return (*(volatile uint64_t *)(handle + offset));
}
#endif

/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle/offset and copy into buffer provided.
 */
static __inline void bus_space_read_multi_1(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int8_t *addr,
					    size_t count);

static __inline void bus_space_read_multi_2(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int16_t *addr,
					    size_t count);

static __inline void bus_space_read_multi_4(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int32_t *addr,
					    size_t count);

static __inline void
bus_space_read_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		insb(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	movb (%2),%%al				\n\
			stosb					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
#endif
	}
}

static __inline void
bus_space_read_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		insw(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	movw (%2),%%ax				\n\
			stosw					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
#endif
	}
}

static __inline void
bus_space_read_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		insl(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	movl (%2),%%eax				\n\
			stosl					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
#endif
	}
}

#if 0	/* Cause a link error for bus_space_read_multi_8 */
#define	bus_space_read_multi_8	!!! bus_space_read_multi_8 unimplemented !!!
#endif

/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
static __inline void bus_space_read_region_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int8_t *addr,
					     size_t count);

static __inline void bus_space_read_region_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int16_t *addr,
					     size_t count);

static __inline void bus_space_read_region_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int32_t *addr,
					     size_t count);


static __inline void
bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int8_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	inb %w2,%%al				\n\
			stosb					\n\
			incl %2					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsb"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
#endif
	}
}

static __inline void
bus_space_read_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int16_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	inw %w2,%%ax				\n\
			stosw					\n\
			addl $2,%2				\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsw"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
#endif
	}
}

static __inline void
bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int32_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	inl %w2,%%eax				\n\
			stosl					\n\
			addl $4,%2				\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsl"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
#endif
	}
}

#if 0	/* Cause a link error for bus_space_read_region_8 */
#define	bus_space_read_region_8	!!! bus_space_read_region_8 unimplemented !!!
#endif

/*
 * Write the 1, 2, 4, or 8 byte value `value' to bus space
 * described by tag/handle/offset.
 */

static __inline void bus_space_write_1(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int8_t value);

static __inline void bus_space_write_2(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int16_t value);

static __inline void bus_space_write_4(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int32_t value);

#ifdef __amd64__
static __inline void bus_space_write_8(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, uint64_t value);
#endif

static __inline void
bus_space_write_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t value)
{

	if (tag == X86_BUS_SPACE_IO)
		outb(bsh + offset, value);
	else
		*(volatile u_int8_t *)(bsh + offset) = value;
}

static __inline void
bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t value)
{

	if (tag == X86_BUS_SPACE_IO)
		outw(bsh + offset, value);
	else
		*(volatile u_int16_t *)(bsh + offset) = value;
}

static __inline void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t value)
{

	if (tag == X86_BUS_SPACE_IO)
		outl(bsh + offset, value);
	else
		*(volatile u_int32_t *)(bsh + offset) = value;
}

#ifdef __amd64__
static __inline void
bus_space_write_8(bus_space_tag_t tag, bus_space_handle_t bsh,
		  bus_size_t offset, uint64_t value)
{

	if (tag == X86_BUS_SPACE_IO) /* No 8 byte IO space access on x86 */
		return;
	else
		*(volatile uint64_t *)(bsh + offset) = value;
}
#endif

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer
 * provided to bus space described by tag/handle/offset.
 */

static __inline void bus_space_write_multi_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int8_t *addr,
					     size_t count);
static __inline void bus_space_write_multi_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int16_t *addr,
					     size_t count);

static __inline void bus_space_write_multi_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int32_t *addr,
					     size_t count);

static __inline void
bus_space_write_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int8_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		outsb(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsb					\n\
			movb %%al,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
#endif
	}
}

static __inline void
bus_space_write_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int16_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		outsw(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsw					\n\
			movw %%ax,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
#endif
	}
}

static __inline void
bus_space_write_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int32_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO)
		outsl(bsh + offset, addr, count);
	else {
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsl					\n\
			movl %%eax,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
#endif
	}
}

#if 0	/* Cause a link error for bus_space_write_multi_8 */
#define	bus_space_write_multi_8(t, h, o, a, c)				\
			!!! bus_space_write_multi_8 unimplemented !!!
#endif

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer provided
 * to bus space described by tag/handle starting at `offset'.
 */

static __inline void bus_space_write_region_1(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int8_t *addr,
					      size_t count);
static __inline void bus_space_write_region_2(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int16_t *addr,
					      size_t count);
static __inline void bus_space_write_region_4(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int32_t *addr,
					      size_t count);

static __inline void
bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int8_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsb					\n\
			outb %%al,%w0				\n\
			incl %0					\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsb"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
#endif
	}
}

static __inline void
bus_space_write_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int16_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsw					\n\
			outw %%ax,%w0				\n\
			addl $2,%0				\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsw"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
#endif
	}
}

static __inline void
bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int32_t *addr, size_t count)
{

	if (tag == X86_BUS_SPACE_IO) {
		int _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsl					\n\
			outl %%eax,%w0				\n\
			addl $4,%0				\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
#endif
	} else {
		bus_space_handle_t _port_ = bsh + offset;
#ifdef __GNUCLIKE_ASM
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsl"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
#endif
	}
}

#if 0	/* Cause a link error for bus_space_write_region_8 */
#define	bus_space_write_region_8					\
			!!! bus_space_write_region_8 unimplemented !!!
#endif

/*
 * Write the 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle/offset `count' times.
 */

static __inline void bus_space_set_multi_1(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int8_t value, size_t count);
static __inline void bus_space_set_multi_2(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int16_t value, size_t count);
static __inline void bus_space_set_multi_4(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int32_t value, size_t count);

static __inline void
bus_space_set_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		      bus_size_t offset, u_int8_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		while (count--)
			outb(addr, value);
	else
		while (count--)
			*(volatile u_int8_t *)(addr) = value;
}

static __inline void
bus_space_set_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		     bus_size_t offset, u_int16_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		while (count--)
			outw(addr, value);
	else
		while (count--)
			*(volatile u_int16_t *)(addr) = value;
}

static __inline void
bus_space_set_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		      bus_size_t offset, u_int32_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		while (count--)
			outl(addr, value);
	else
		while (count--)
			*(volatile u_int32_t *)(addr) = value;
}

#if 0	/* Cause a link error for bus_space_set_multi_8 */
#define	bus_space_set_multi_8 !!! bus_space_set_multi_8 unimplemented !!!
#endif

/*
 * Write `count' 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle starting at `offset'.
 */

static __inline void bus_space_set_region_1(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int8_t value,
					    size_t count);
static __inline void bus_space_set_region_2(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int16_t value,
					    size_t count);
static __inline void bus_space_set_region_4(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int32_t value,
					    size_t count);

static __inline void
bus_space_set_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		for (; count != 0; count--, addr++)
			outb(addr, value);
	else
		for (; count != 0; count--, addr++)
			*(volatile u_int8_t *)(addr) = value;
}

static __inline void
bus_space_set_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		for (; count != 0; count--, addr += 2)
			outw(addr, value);
	else
		for (; count != 0; count--, addr += 2)
			*(volatile u_int16_t *)(addr) = value;
}

static __inline void
bus_space_set_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == X86_BUS_SPACE_IO)
		for (; count != 0; count--, addr += 4)
			outl(addr, value);
	else
		for (; count != 0; count--, addr += 4)
			*(volatile u_int32_t *)(addr) = value;
}

#if 0	/* Cause a link error for bus_space_set_region_8 */
#define	bus_space_set_region_8	!!! bus_space_set_region_8 unimplemented !!!
#endif

/*
 * Copy `count' 1, 2, 4, or 8 byte values from bus space starting
 * at tag/bsh1/off1 to bus space starting at tag/bsh2/off2.
 */

static __inline void bus_space_copy_region_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void bus_space_copy_region_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void bus_space_copy_region_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void
bus_space_copy_region_1(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == X86_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1++, addr2++)
				outb(addr2, inb(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += (count - 1), addr2 += (count - 1);
			    count != 0; count--, addr1--, addr2--)
				outb(addr2, inb(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1++, addr2++)
				*(volatile u_int8_t *)(addr2) =
				    *(volatile u_int8_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += (count - 1), addr2 += (count - 1);
			    count != 0; count--, addr1--, addr2--)
				*(volatile u_int8_t *)(addr2) =
				    *(volatile u_int8_t *)(addr1);
		}
	}
}

static __inline void
bus_space_copy_region_2(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == X86_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 2, addr2 += 2)
				outw(addr2, inw(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 2 * (count - 1), addr2 += 2 * (count - 1);
			    count != 0; count--, addr1 -= 2, addr2 -= 2)
				outw(addr2, inw(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 2, addr2 += 2)
				*(volatile u_int16_t *)(addr2) =
				    *(volatile u_int16_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 2 * (count - 1), addr2 += 2 * (count - 1);
			    count != 0; count--, addr1 -= 2, addr2 -= 2)
				*(volatile u_int16_t *)(addr2) =
				    *(volatile u_int16_t *)(addr1);
		}
	}
}

static __inline void
bus_space_copy_region_4(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == X86_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 4, addr2 += 4)
				outl(addr2, inl(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 4 * (count - 1), addr2 += 4 * (count - 1);
			    count != 0; count--, addr1 -= 4, addr2 -= 4)
				outl(addr2, inl(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 4, addr2 += 4)
				*(volatile u_int32_t *)(addr2) =
				    *(volatile u_int32_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 4 * (count - 1), addr2 += 4 * (count - 1);
			    count != 0; count--, addr1 -= 4, addr2 -= 4)
				*(volatile u_int32_t *)(addr2) =
				    *(volatile u_int32_t *)(addr1);
		}
	}
}

#if 0	/* Cause a link error for bus_space_copy_8 */
#define	bus_space_copy_region_8	!!! bus_space_copy_region_8 unimplemented !!!
#endif

/*
 * Bus read/write barrier methods.
 *
 *	void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t bsh,
 *			       bus_size_t offset, bus_size_t len, int flags);
 *
 *
 * Note that BUS_SPACE_BARRIER_WRITE doesn't do anything other than
 * prevent reordering by the compiler; all Intel x86 processors currently
 * retire operations outside the CPU in program order.
 */
#define	BUS_SPACE_BARRIER_READ	0x01		/* force read barrier */
#define	BUS_SPACE_BARRIER_WRITE	0x02		/* force write barrier */

static __inline void
bus_space_barrier(bus_space_tag_t tag __unused, bus_space_handle_t bsh __unused,
		  bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
#ifdef __GNUCLIKE_ASM
	if (flags & BUS_SPACE_BARRIER_READ)
#ifdef __amd64__
		__asm __volatile("lock; addl $0,0(%%rsp)" : : : "memory");
#else
		__asm __volatile("lock; addl $0,0(%%esp)" : : : "memory");
#endif
	else
		__compiler_membar();
#endif
}

#ifdef BUS_SPACE_NO_LEGACY
#undef inb
#undef outb
#define inb(a) compiler_error
#define inw(a) compiler_error
#define inl(a) compiler_error
#define outb(a, b) compiler_error
#define outw(a, b) compiler_error
#define outl(a, b) compiler_error
#endif

#include <machine/bus_dma.h>

/*
 * Stream accesses are the same as normal accesses on x86; there are no
 * supported bus systems with an endianess different from the host one.
 */
#define	bus_space_read_stream_1(t, h, o)	bus_space_read_1((t), (h), (o))
#define	bus_space_read_stream_2(t, h, o)	bus_space_read_2((t), (h), (o))
#define	bus_space_read_stream_4(t, h, o)	bus_space_read_4((t), (h), (o))

#define	bus_space_read_multi_stream_1(t, h, o, a, c) \
	bus_space_read_multi_1((t), (h), (o), (a), (c))
#define	bus_space_read_multi_stream_2(t, h, o, a, c) \
	bus_space_read_multi_2((t), (h), (o), (a), (c))
#define	bus_space_read_multi_stream_4(t, h, o, a, c) \
	bus_space_read_multi_4((t), (h), (o), (a), (c))

#define	bus_space_write_stream_1(t, h, o, v) \
	bus_space_write_1((t), (h), (o), (v))
#define	bus_space_write_stream_2(t, h, o, v) \
	bus_space_write_2((t), (h), (o), (v))
#define	bus_space_write_stream_4(t, h, o, v) \
	bus_space_write_4((t), (h), (o), (v))

#define	bus_space_write_multi_stream_1(t, h, o, a, c) \
	bus_space_write_multi_1((t), (h), (o), (a), (c))
#define	bus_space_write_multi_stream_2(t, h, o, a, c) \
	bus_space_write_multi_2((t), (h), (o), (a), (c))
#define	bus_space_write_multi_stream_4(t, h, o, a, c) \
	bus_space_write_multi_4((t), (h), (o), (a), (c))

#define	bus_space_set_multi_stream_1(t, h, o, v, c) \
	bus_space_set_multi_1((t), (h), (o), (v), (c))
#define	bus_space_set_multi_stream_2(t, h, o, v, c) \
	bus_space_set_multi_2((t), (h), (o), (v), (c))
#define	bus_space_set_multi_stream_4(t, h, o, v, c) \
	bus_space_set_multi_4((t), (h), (o), (v), (c))

#define	bus_space_read_region_stream_1(t, h, o, a, c) \
	bus_space_read_region_1((t), (h), (o), (a), (c))
#define	bus_space_read_region_stream_2(t, h, o, a, c) \
	bus_space_read_region_2((t), (h), (o), (a), (c))
#define	bus_space_read_region_stream_4(t, h, o, a, c) \
	bus_space_read_region_4((t), (h), (o), (a), (c))

#define	bus_space_write_region_stream_1(t, h, o, a, c) \
	bus_space_write_region_1((t), (h), (o), (a), (c))
#define	bus_space_write_region_stream_2(t, h, o, a, c) \
	bus_space_write_region_2((t), (h), (o), (a), (c))
#define	bus_space_write_region_stream_4(t, h, o, a, c) \
	bus_space_write_region_4((t), (h), (o), (a), (c))

#define	bus_space_set_region_stream_1(t, h, o, v, c) \
	bus_space_set_region_1((t), (h), (o), (v), (c))
#define	bus_space_set_region_stream_2(t, h, o, v, c) \
	bus_space_set_region_2((t), (h), (o), (v), (c))
#define	bus_space_set_region_stream_4(t, h, o, v, c) \
	bus_space_set_region_4((t), (h), (o), (v), (c))

#define	bus_space_copy_region_stream_1(t, h1, o1, h2, o2, c) \
	bus_space_copy_region_1((t), (h1), (o1), (h2), (o2), (c))
#define	bus_space_copy_region_stream_2(t, h1, o1, h2, o2, c) \
	bus_space_copy_region_2((t), (h1), (o1), (h2), (o2), (c))
#define	bus_space_copy_region_stream_4(t, h1, o1, h2, o2, c) \
	bus_space_copy_region_4((t), (h1), (o1), (h2), (o2), (c))

#endif /* _X86_BUS_H_ */
