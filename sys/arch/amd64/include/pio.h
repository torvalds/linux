/*	$OpenBSD: pio.h,v 1.5 2015/04/25 21:31:24 guenther Exp $	*/
/*	$NetBSD: pio.h,v 1.2 2003/02/27 11:22:46 fvdl Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

#ifndef _MACHINE_PIO_H_
#define _MACHINE_PIO_H_

/*
 * Functions to provide access to x86 programmed I/O instructions.
 *
 * The in[bwl]() and out[bwl]() functions are split into two varieties: one to
 * use a small, constant, 8-bit port number, and another to use a large or
 * variable port number.  The former can be compiled as a smaller instruction.
 */


#ifdef __OPTIMIZE__

#define	__use_immediate_port(port) \
	(__builtin_constant_p((port)) && (port) < 0x100)

#else

#define	__use_immediate_port(port)	0

#endif


#define	inb(port) \
    (/* CONSTCOND */ __use_immediate_port(port) ? __inbc(port) : __inb(port))

static __inline u_int8_t
__inbc(unsigned port)
{
	u_int8_t data;
	__asm volatile("inb %w1,%0" : "=a" (data) : "id" (port));
	return data;
}

static __inline u_int8_t
__inb(unsigned port)
{
	u_int8_t data;
	__asm volatile("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insb(unsigned port, void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\tinsb"				:
			 "=D" (dummy1), "=c" (dummy2) 		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory");
}

#define	inw(port) \
    (/* CONSTCOND */ __use_immediate_port(port) ? __inwc(port) : __inw(port))

static __inline u_int16_t
__inwc(unsigned port)
{
	u_int16_t data;
	__asm volatile("inw %w1,%0" : "=a" (data) : "id" (port));
	return data;
}

static __inline u_int16_t
__inw(unsigned port)
{
	u_int16_t data;
	__asm volatile("inw %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insw(unsigned port, void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\tinsw"				:
			 "=D" (dummy1), "=c" (dummy2)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory");
}

#define	inl(port) \
    (/* CONSTCOND */ __use_immediate_port(port) ? __inlc(port) : __inl(port))

static __inline u_int32_t
__inlc(unsigned port)
{
	u_int32_t data;
	__asm volatile("inl %w1,%0" : "=a" (data) : "id" (port));
	return data;
}

static __inline u_int32_t
__inl(unsigned port)
{
	u_int32_t data;
	__asm volatile("inl %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insl(unsigned port, void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\tinsl"				:
			 "=D" (dummy1), "=c" (dummy2)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory");
}

#define	outb(port, data) \
    (/* CONSTCOND */__use_immediate_port(port) ? __outbc(port, data) : \
						__outb(port, data))

static __inline void
__outbc(unsigned port, u_int8_t data)
{
	__asm volatile("outb %0,%w1" : : "a" (data), "id" (port));
}

static __inline void
__outb(unsigned port, u_int8_t data)
{
	__asm volatile("outb %0,%w1" : : "a" (data), "d" (port));
}

static __inline void
outsb(unsigned port, const void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\toutsb"				:
			 "=S" (dummy1), "=c" (dummy2)		:
			 "d" (port), "0" (addr), "1" (cnt));
}

#define	outw(port, data) \
    (/* CONSTCOND */ __use_immediate_port(port) ? __outwc(port, data) : \
						__outw(port, data))

static __inline void
__outwc(unsigned port, u_int16_t data)
{
	__asm volatile("outw %0,%w1" : : "a" (data), "id" (port));
}

static __inline void
__outw(unsigned port, u_int16_t data)
{
	__asm volatile("outw %0,%w1" : : "a" (data), "d" (port));
}

static __inline void
outsw(unsigned port, const void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\toutsw"				:
			 "=S" (dummy1), "=c" (dummy2)		:
			 "d" (port), "0" (addr), "1" (cnt));
}

#define	outl(port, data) \
    (/* CONSTCOND */ __use_immediate_port(port) ? __outlc(port, data) : \
						__outl(port, data))

static __inline void
__outlc(unsigned port, u_int32_t data)
{
	__asm volatile("outl %0,%w1" : : "a" (data), "id" (port));
}

static __inline void
__outl(unsigned port, u_int32_t data)
{
	__asm volatile("outl %0,%w1" : : "a" (data), "d" (port));
}

static __inline void
outsl(unsigned port, const void *addr, int cnt)
{
	void *dummy1;
	int dummy2;
	__asm volatile("repne\n\toutsl"				:
			 "=S" (dummy1), "=c" (dummy2)		:
			 "d" (port), "0" (addr), "1" (cnt));
}

#endif /* _MACHINE_PIO_H_ */
