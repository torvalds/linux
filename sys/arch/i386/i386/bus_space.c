/*	$OpenBSD: bus_space.c,v 1.10 2023/01/30 10:49:04 jsg Exp $ */
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

/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Jason R. Thorpe.  All rights reserved.
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
 *	This product includes software developed by Christopher G. Demetriou
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

#include <sys/param.h>

#include <machine/bus.h>

u_int8_t	i386_bus_space_io_read_1(bus_space_handle_t, bus_size_t);
u_int16_t	i386_bus_space_io_read_2(bus_space_handle_t, bus_size_t);
u_int32_t	i386_bus_space_io_read_4(bus_space_handle_t, bus_size_t);

void		i386_bus_space_io_read_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		i386_bus_space_io_read_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		i386_bus_space_io_read_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);

void		i386_bus_space_io_read_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		i386_bus_space_io_read_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		i386_bus_space_io_read_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);

void		i386_bus_space_io_write_1(bus_space_handle_t, bus_size_t,
		    u_int8_t);
void		i386_bus_space_io_write_2(bus_space_handle_t, bus_size_t,
		    u_int16_t);
void		i386_bus_space_io_write_4(bus_space_handle_t, bus_size_t,
		    u_int32_t);

void		i386_bus_space_io_write_multi_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		i386_bus_space_io_write_multi_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		i386_bus_space_io_write_multi_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);

void		i386_bus_space_io_write_region_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		i386_bus_space_io_write_region_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		i386_bus_space_io_write_region_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);

void		i386_bus_space_io_set_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		i386_bus_space_io_set_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		i386_bus_space_io_set_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);

void		i386_bus_space_io_set_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		i386_bus_space_io_set_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		i386_bus_space_io_set_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);

void		i386_bus_space_io_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		i386_bus_space_io_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		i386_bus_space_io_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

void *		i386_bus_space_io_vaddr(bus_space_handle_t);
paddr_t		i386_bus_space_io_mmap(bus_addr_t, off_t, int, int);

const struct i386_bus_space_ops i386_bus_space_io_ops = {
	i386_bus_space_io_read_1,
	i386_bus_space_io_read_2,
	i386_bus_space_io_read_4,
	i386_bus_space_io_read_multi_1,
	i386_bus_space_io_read_multi_2,
	i386_bus_space_io_read_multi_4,
	i386_bus_space_io_read_region_1,
	i386_bus_space_io_read_region_2,
	i386_bus_space_io_read_region_4,
	i386_bus_space_io_write_1,
	i386_bus_space_io_write_2,
	i386_bus_space_io_write_4,
	i386_bus_space_io_write_multi_1,
	i386_bus_space_io_write_multi_2,
	i386_bus_space_io_write_multi_4,
	i386_bus_space_io_write_region_1,
	i386_bus_space_io_write_region_2,
	i386_bus_space_io_write_region_4,
	i386_bus_space_io_set_multi_1,
	i386_bus_space_io_set_multi_2,
	i386_bus_space_io_set_multi_4,
	i386_bus_space_io_set_region_1,
	i386_bus_space_io_set_region_2,
	i386_bus_space_io_set_region_4,
	i386_bus_space_io_copy_1,
	i386_bus_space_io_copy_2,
	i386_bus_space_io_copy_4,
	i386_bus_space_io_vaddr,
};

u_int8_t	i386_bus_space_mem_read_1(bus_space_handle_t, bus_size_t);
u_int16_t	i386_bus_space_mem_read_2(bus_space_handle_t, bus_size_t);
u_int32_t	i386_bus_space_mem_read_4(bus_space_handle_t, bus_size_t);

void		i386_bus_space_mem_read_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		i386_bus_space_mem_read_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		i386_bus_space_mem_read_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);

void		i386_bus_space_mem_read_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t *, bus_size_t);
void		i386_bus_space_mem_read_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t *, bus_size_t);
void		i386_bus_space_mem_read_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t *, bus_size_t);

void		i386_bus_space_mem_write_1(bus_space_handle_t, bus_size_t,
		    u_int8_t);
void		i386_bus_space_mem_write_2(bus_space_handle_t, bus_size_t,
		    u_int16_t);
void		i386_bus_space_mem_write_4(bus_space_handle_t, bus_size_t,
		    u_int32_t);

void		i386_bus_space_mem_write_multi_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		i386_bus_space_mem_write_multi_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		i386_bus_space_mem_write_multi_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);

void		i386_bus_space_mem_write_region_1(bus_space_handle_t,
		    bus_size_t, const u_int8_t *, bus_size_t);
void		i386_bus_space_mem_write_region_2(bus_space_handle_t,
		    bus_size_t, const u_int16_t *, bus_size_t);
void		i386_bus_space_mem_write_region_4(bus_space_handle_t,
		    bus_size_t, const u_int32_t *, bus_size_t);

void		i386_bus_space_mem_set_multi_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		i386_bus_space_mem_set_multi_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		i386_bus_space_mem_set_multi_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);

void		i386_bus_space_mem_set_region_1(bus_space_handle_t, bus_size_t,
		    u_int8_t, size_t);
void		i386_bus_space_mem_set_region_2(bus_space_handle_t, bus_size_t,
		    u_int16_t, size_t);
void		i386_bus_space_mem_set_region_4(bus_space_handle_t, bus_size_t,
		    u_int32_t, size_t);

void		i386_bus_space_mem_copy_1(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		i386_bus_space_mem_copy_2(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);
void		i386_bus_space_mem_copy_4(bus_space_handle_t, bus_size_t,
		    bus_space_handle_t, bus_size_t, size_t);

void *		i386_bus_space_mem_vaddr(bus_space_handle_t);
paddr_t		i386_bus_space_mem_mmap(bus_addr_t, off_t, int, int);

const struct i386_bus_space_ops i386_bus_space_mem_ops = {
	i386_bus_space_mem_read_1,
	i386_bus_space_mem_read_2,
	i386_bus_space_mem_read_4,
	i386_bus_space_mem_read_multi_1,
	i386_bus_space_mem_read_multi_2,
	i386_bus_space_mem_read_multi_4,
	i386_bus_space_mem_read_region_1,
	i386_bus_space_mem_read_region_2,
	i386_bus_space_mem_read_region_4,
	i386_bus_space_mem_write_1,
	i386_bus_space_mem_write_2,
	i386_bus_space_mem_write_4,
	i386_bus_space_mem_write_multi_1,
	i386_bus_space_mem_write_multi_2,
	i386_bus_space_mem_write_multi_4,
	i386_bus_space_mem_write_region_1,
	i386_bus_space_mem_write_region_2,
	i386_bus_space_mem_write_region_4,
	i386_bus_space_mem_set_multi_1,
	i386_bus_space_mem_set_multi_2,
	i386_bus_space_mem_set_multi_4,
	i386_bus_space_mem_set_region_1,
	i386_bus_space_mem_set_region_2,
	i386_bus_space_mem_set_region_4,
	i386_bus_space_mem_copy_1,
	i386_bus_space_mem_copy_2,
	i386_bus_space_mem_copy_4,
	i386_bus_space_mem_vaddr,
};

u_int8_t
i386_bus_space_io_read_1(bus_space_handle_t h, bus_size_t o)
{
	return (inb(h + o));
}

u_int16_t
i386_bus_space_io_read_2(bus_space_handle_t h, bus_size_t o)
{
	return (inw(h + o));
}

u_int32_t
i386_bus_space_io_read_4(bus_space_handle_t h, bus_size_t o)
{
	return (inl(h + o));
}

void
i386_bus_space_io_read_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t cnt)
{
	insb(h + o, a, cnt);
}

void
i386_bus_space_io_read_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t *a, bus_size_t cnt)
{
	insw(h + o, a, cnt);
}

void
i386_bus_space_io_read_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t *a, bus_size_t cnt)
{
	insl(h + o, a, cnt);
}

void
i386_bus_space_io_read_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	__asm volatile(
	"1:	inb %w2,%%al				;"
	"	stosb					;"
	"	incl %2					;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt), "+d" (_port)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_read_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	__asm volatile(
	"1:	inw %w2,%%ax				;"
	"	stosw					;"
	"	addl $2,%2				;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt), "+d" (_port)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_read_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	__asm volatile(
	"1:	inl %w2,%%eax				;"
	"	stosl					;"
	"	addl $4,%2				;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt), "+d" (_port)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_write_1(bus_space_handle_t h, bus_size_t o, u_int8_t v)
{
	outb(h + o, v);
}

void
i386_bus_space_io_write_2(bus_space_handle_t h, bus_size_t o, u_int16_t v)
{
	outw(h + o, v);
}

void
i386_bus_space_io_write_4(bus_space_handle_t h, bus_size_t o, u_int32_t v)
{
	outl(h + o, v);
}

void
i386_bus_space_io_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *a, bus_size_t cnt)
{
	outsb(h + o, a, cnt);
}

void
i386_bus_space_io_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *a, bus_size_t cnt)
{
	outsw(h + o, a, cnt);
}

void
i386_bus_space_io_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *a, bus_size_t cnt)
{
	outsl(h + o, a, cnt);
}

void
i386_bus_space_io_write_region_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	__asm volatile(
	"1:	lodsb					;"
	"	outb %%al,%w0				;"
	"	incl %0					;"
	"	loop 1b"				:
	    "+d" (_port), "+S" (_addr), "+c" (_cnt)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_write_region_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	__asm volatile(
	"1:	lodsw					;"
	"	outw %%ax,%w0				;"
	"	addl $2,%0				;"
	"	loop 1b"				:
	    "+d" (_port), "+S" (_addr), "+c" (_cnt)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_write_region_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	__asm volatile(
	"1:	lodsl					;"
	"	outl %%eax,%w0				;"
	"	addl $4,%0				;"
	"	loop 1b"				:
	    "+d" (_port), "+S" (_addr), "+c" (_cnt)	::
	    "%eax", "memory", "cc");
}

void
i386_bus_space_io_set_multi_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	outb %b2, %w1				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "d" (h + o), "a" (v)		:
	    "cc");
}

void
i386_bus_space_io_set_multi_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	outw %w2, %w1				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "d" (h + o), "a" (v)	:
	    "cc");
}

void
i386_bus_space_io_set_multi_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	outl %2,%w1				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "d" (h + o), "a" (v)	:
		    "cc");
}

void
i386_bus_space_io_set_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"1:	outb %%al,%w0				;"
	"	incl %0					;"
	"	loop 1b"				:
	    "+d" (_port), "+c" (_cnt) : "a" (v)	:
		    "cc");
}

void
i386_bus_space_io_set_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"1:	outw %%ax,%w0				;"
	"	addl $2, %0				;"
	"	loop 1b"				:
	    "+d" (_port), "+c" (_cnt) : "a" (v)		:
		    "cc");
}

void
i386_bus_space_io_set_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"1:	outl %%eax,%w0				;"
	"	addl $4, %0				;"
	"	loop 1b"				:
	    "+d" (_port), "+c" (_cnt) : "a" (v)		:
	    "cc");
}

void
i386_bus_space_io_copy_1(bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt = cnt;

	__asm volatile(
	"1:	movl %k1,%%edx				;"
	"	inb  %%dx,%%al				;"
	"	movl %k0,%%edx				;"
	"	outb %%al,%%dx				;"
	"	incl %0					;"
	"	incl %1					;"
	"	loop 1b"				:
	    "+D" (_port2), "+S" (_port1), "+c" (_cnt)	::
	    "%edx", "%eax", "cc");
}

void
i386_bus_space_io_copy_2(bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt=cnt;

	__asm volatile(
	"1:	movl %k1,%%edx				;"
	"	inw  %%dx,%%ax				;"
	"	movl %k0,%%edx				;"
	"	outw %%ax,%%dx				;"
	"	addl $2, %0				;"
	"	addl $2, %1				;"
	"	loop 1b"				:
	    "+D" (_port2), "+S" (_port1), "+c" (_cnt)	::
	    "%edx", "%eax", "cc");
}

void
i386_bus_space_io_copy_4(bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt = cnt;

	__asm volatile(
	"1:	movl %k1,%%edx				;"
	"	inl  %%dx,%%eax				;"
	"	movl %k0,%%edx				;"
	"	outl %%eax,%%dx				;"
	"	addl $4, %0				;"
	"	addl $4, %1				;"
	"	loop 1b"				:
	    "+D" (_port2), "+S" (_port1), "+c" (_cnt)	::
	    "%edx", "%eax", "cc");
}

void *
i386_bus_space_io_vaddr(bus_space_handle_t h)
{
	return (NULL);
}

paddr_t
i386_bus_space_io_mmap(bus_addr_t addr, off_t off, int prot, int flags)
{
	/* Can't mmap I/O space. */
	return (-1);
}

u_int8_t
i386_bus_space_mem_read_1(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int8_t *)((h) + (o)));
}

u_int16_t
i386_bus_space_mem_read_2(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int16_t *)((h) + (o)));
}

u_int32_t
i386_bus_space_mem_read_4(bus_space_handle_t h, bus_size_t o)
{
	return (*(volatile u_int32_t *)((h) + (o)));
}

void
i386_bus_space_mem_read_multi_1(bus_space_handle_t h, bus_size_t o,
    u_int8_t *a, bus_size_t cnt)
{
	void *_addr=a;
	int _cnt=cnt;
	__asm volatile(
	"1:	movb (%2),%%al				;"
	"	stosb					;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt) : "r" (h + o)	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_read_multi_2(bus_space_handle_t h, bus_size_t o,
    u_int16_t *a, bus_size_t cnt)
{
	void *_addr=a;
	int _cnt=cnt;
	__asm volatile(
	"1:	movw (%2),%%ax				;"
	"	stosw					;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt) : "r" ((h) + (o))	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_read_multi_4(bus_space_handle_t h, bus_size_t o,
    u_int32_t *a, bus_size_t cnt)
{
	void *_addr=a;
	int _cnt=cnt;
	__asm volatile(
	"1:	movl (%2),%%eax				;"
	"	stosl					;"
	"	loop 1b"				:
	    "+D" (_addr), "+c" (_cnt) : "r" (h + o)	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_read_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	i386_space_copy(_port, _addr, 1, _cnt);
}

void
i386_bus_space_mem_read_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	i386_space_copy(_port, _addr, 2, _cnt);
}

void
i386_bus_space_mem_read_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t *a, bus_size_t cnt)
{
	int _cnt = cnt;
	void *_addr = a;
	int _port = h + o;

	i386_space_copy(_port, _addr, 4, _cnt);
}

void
i386_bus_space_mem_write_1(bus_space_handle_t h, bus_size_t o, u_int8_t v)
{
	((void)(*(volatile u_int8_t *)(h + o) = v));
}

void
i386_bus_space_mem_write_2(bus_space_handle_t h, bus_size_t o, u_int16_t v)
{
	((void)(*(volatile u_int16_t *)(h + o) = v));
}

void
i386_bus_space_mem_write_4(bus_space_handle_t h, bus_size_t o, u_int32_t v)
{
	((void)(*(volatile u_int32_t *)(h + o) = v));
}

void
i386_bus_space_mem_write_multi_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *a, bus_size_t cnt)
{
	const void *_addr=a;
	int _cnt=cnt;

	__asm volatile(
	"1:	lodsb					;"
	"	movb %%al,(%2)				;"
	"	loop 1b"				:
	    "+S" (_addr), "+c" (_cnt) : "r" (h + o)	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_write_multi_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *a, bus_size_t cnt)
{
	const void *_addr = a;
	int _cnt = cnt;

	__asm volatile(
	"1:	lodsw					;"
	"	movw %%ax,(%2)				;"
	"	loop 1b"				:
	    "+S" (_addr), "+c" (_cnt) : "r" (h + o)	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_write_multi_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *a, bus_size_t cnt)
{
	const void *_addr=a;
	int _cnt=cnt;

	__asm volatile(
	"1:	lodsl					;"
	"	movl %%eax,(%2)				;"
	"	loop 1b"				:
	    "+S" (_addr), "+c" (_cnt) : "r" (h + o)	:
	    "%eax", "memory", "cc");
}

void
i386_bus_space_mem_write_region_1(bus_space_handle_t h,
    bus_size_t o, const u_int8_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	i386_space_copy(_addr, _port, 1, _cnt);
}

void
i386_bus_space_mem_write_region_2(bus_space_handle_t h,
    bus_size_t o, const u_int16_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	i386_space_copy(_addr, _port, 2, _cnt);
}

void
i386_bus_space_mem_write_region_4(bus_space_handle_t h,
    bus_size_t o, const u_int32_t *a, bus_size_t cnt)
{
	int _port = h + o;
	const void *_addr = a;
	int _cnt = cnt;

	i386_space_copy(_addr, _port, 4, _cnt);
}

void
i386_bus_space_mem_set_multi_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	movb %b2, (%1)				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "D" (h + o), "a" (v)		:
	    "cc", "memory");
}

void
i386_bus_space_mem_set_multi_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	movw %w2, (%1)				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "D" (h + o), "a" (v)		:
	    "cc", "memory");
}

void
i386_bus_space_mem_set_multi_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t v, size_t cnt)
{
	int _cnt = cnt;

	__asm volatile(
	"1:	movl %2,(%1)				;"
	"	loop 1b"				:
	    "+c" (_cnt) : "D" (h + o), "a" (v)	:
	    "cc", "memory");
}

void
i386_bus_space_mem_set_region_1(bus_space_handle_t h,
    bus_size_t o, u_int8_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"	repne					;"
	"	stosb"					:
	    "+D" (_port), "+c" (_cnt) : "a" (v)	:
	    "memory", "cc");
}

void
i386_bus_space_mem_set_region_2(bus_space_handle_t h,
    bus_size_t o, u_int16_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"	repne					;"
	"	stosw"					:
	    "+D" (_port), "+c" (_cnt) : "a" (v)	:
	    "memory", "cc");
}

void
i386_bus_space_mem_set_region_4(bus_space_handle_t h,
    bus_size_t o, u_int32_t v, size_t cnt)
{
	int _port = h + o;
	int _cnt = cnt;

	__asm volatile(
	"	repne					;"
	"	stosl"					:
	    "+D" (_port), "+c" (_cnt) : "a" (v)	:
	    "memory", "cc");
}

void
i386_bus_space_mem_copy_1( bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt = cnt;

	i386_space_copy(_port1, _port2, 1, _cnt);
}

void
i386_bus_space_mem_copy_2( bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt=cnt;

	i386_space_copy(_port1, _port2, 2, _cnt);
}

void
i386_bus_space_mem_copy_4( bus_space_handle_t h1, bus_size_t o1,
     bus_space_handle_t h2, bus_size_t o2, bus_size_t cnt)
{
	int _port1 = h1 + o1;
	int _port2 = h2 + o2;
	int _cnt = cnt;

	i386_space_copy(_port1, _port2, 4, _cnt);
}

void *
i386_bus_space_mem_vaddr(bus_space_handle_t h)
{
	return ((void *)h);
}

paddr_t
i386_bus_space_mem_mmap(bus_addr_t addr, off_t off, int prot, int flags)
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
