/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Per Fogelstrom, Opsycon AB and RTMX Inc, USA.
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
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom Opsycon AB for RTMX Inc, North Carolina, USA.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *	$NetBSD: pio.h,v 1.1 1998/05/15 10:15:54 tsubai Exp $
 *	$OpenBSD: pio.h,v 1.1 1997/10/13 10:53:47 pefo Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_PIO_H_
#define	_MACHINE_PIO_H_
/*
 * I/O macros.
 */

/*
 * Use sync so that bus space operations cannot sneak out the bottom of
 * mutex-protected sections (mutex release does not guarantee completion of
 * accesses to caching-inhibited memory on some systems)
 */
#define powerpc_iomb() __asm __volatile("sync" : : : "memory")

static __inline void
__outb(volatile u_int8_t *a, u_int8_t v)
{
	*a = v;
	powerpc_iomb();
}

static __inline void
__outw(volatile u_int16_t *a, u_int16_t v)
{
	*a = v;
	powerpc_iomb();
}

static __inline void
__outl(volatile u_int32_t *a, u_int32_t v)
{
	*a = v;
	powerpc_iomb();
}

static __inline void
__outll(volatile u_int64_t *a, u_int64_t v)
{
	*a = v;
	powerpc_iomb();
}

static __inline void
__outwrb(volatile u_int16_t *a, u_int16_t v)
{
	__asm__ volatile("sthbrx %0, 0, %1" :: "r"(v), "r"(a));
	powerpc_iomb();
}

static __inline void
__outlrb(volatile u_int32_t *a, u_int32_t v)
{
	__asm__ volatile("stwbrx %0, 0, %1" :: "r"(v), "r"(a));
	powerpc_iomb();
}

static __inline u_int8_t
__inb(volatile u_int8_t *a)
{
	u_int8_t _v_;

	_v_ = *a;
	powerpc_iomb();
	return _v_;
}

static __inline u_int16_t
__inw(volatile u_int16_t *a)
{
	u_int16_t _v_;

	_v_ = *a;
	powerpc_iomb();
	return _v_;
}

static __inline u_int32_t
__inl(volatile u_int32_t *a)
{
	u_int32_t _v_;

	_v_ = *a;
	powerpc_iomb();
	return _v_;
}

static __inline u_int64_t
__inll(volatile u_int64_t *a)
{
	u_int64_t _v_;

	_v_ = *a;
	powerpc_iomb();
	return _v_;
}

static __inline u_int16_t
__inwrb(volatile u_int16_t *a)
{
	u_int16_t _v_;

	__asm__ volatile("lhbrx %0, 0, %1" : "=r"(_v_) : "r"(a));
	powerpc_iomb();
	return _v_;
}

static __inline u_int32_t
__inlrb(volatile u_int32_t *a)
{
	u_int32_t _v_;

	__asm__ volatile("lwbrx %0, 0, %1" : "=r"(_v_) : "r"(a));
	powerpc_iomb();
	return _v_;
}

#define	outb(a,v)	(__outb((volatile u_int8_t *)(a), v))
#define	out8(a,v)	outb(a,v)
#define	outw(a,v)	(__outw((volatile u_int16_t *)(a), v))
#define	out16(a,v)	outw(a,v)
#define	outl(a,v)	(__outl((volatile u_int32_t *)(a), v))
#define	out32(a,v)	outl(a,v)
#define	outll(a,v)	(__outll((volatile u_int64_t *)(a), v))
#define	out64(a,v)	outll(a,v)
#define	inb(a)		(__inb((volatile u_int8_t *)(a)))
#define	in8(a)		inb(a)
#define	inw(a)		(__inw((volatile u_int16_t *)(a)))
#define	in16(a)		inw(a)
#define	inl(a)		(__inl((volatile u_int32_t *)(a)))
#define	in32(a)		inl(a)
#define	inll(a)		(__inll((volatile u_int64_t *)(a)))
#define	in64(a)		inll(a)

#define	out8rb(a,v)	outb(a,v)
#define	outwrb(a,v)	(__outwrb((volatile u_int16_t *)(a), v))
#define	out16rb(a,v)	outwrb(a,v)
#define	outlrb(a,v)	(__outlrb((volatile u_int32_t *)(a), v))
#define	out32rb(a,v)	outlrb(a,v)
#define	in8rb(a)	inb(a)
#define	inwrb(a)	(__inwrb((volatile u_int16_t *)(a)))
#define	in16rb(a)	inwrb(a)
#define	inlrb(a)	(__inlrb((volatile u_int32_t *)(a)))
#define	in32rb(a)	inlrb(a)


static __inline void
__outsb(volatile u_int8_t *a, const u_int8_t *s, size_t c)
{
	while (c--)
		*a = *s++;
	powerpc_iomb();
}

static __inline void
__outsw(volatile u_int16_t *a, const u_int16_t *s, size_t c)
{
	while (c--)
		*a = *s++;
	powerpc_iomb();
}

static __inline void
__outsl(volatile u_int32_t *a, const u_int32_t *s, size_t c)
{
	while (c--)
		*a = *s++;
	powerpc_iomb();
}

static __inline void
__outsll(volatile u_int64_t *a, const u_int64_t *s, size_t c)
{
	while (c--)
		*a = *s++;
	powerpc_iomb();
}

static __inline void
__outswrb(volatile u_int16_t *a, const u_int16_t *s, size_t c)
{
	while (c--)
		__asm__ volatile("sthbrx %0, 0, %1" :: "r"(*s++), "r"(a));
	powerpc_iomb();
}

static __inline void
__outslrb(volatile u_int32_t *a, const u_int32_t *s, size_t c)
{
	while (c--)
		__asm__ volatile("stwbrx %0, 0, %1" :: "r"(*s++), "r"(a));
	powerpc_iomb();
}

static __inline void
__insb(volatile u_int8_t *a, u_int8_t *d, size_t c)
{
	while (c--)
		*d++ = *a;
	powerpc_iomb();
}

static __inline void
__insw(volatile u_int16_t *a, u_int16_t *d, size_t c)
{
	while (c--)
		*d++ = *a;
	powerpc_iomb();
}

static __inline void
__insl(volatile u_int32_t *a, u_int32_t *d, size_t c)
{
	while (c--)
		*d++ = *a;
	powerpc_iomb();
}

static __inline void
__insll(volatile u_int64_t *a, u_int64_t *d, size_t c)
{
	while (c--)
		*d++ = *a;
	powerpc_iomb();
}

static __inline void
__inswrb(volatile u_int16_t *a, u_int16_t *d, size_t c)
{
	while (c--)
		__asm__ volatile("lhbrx %0, 0, %1" : "=r"(*d++) : "r"(a));
	powerpc_iomb();
}

static __inline void
__inslrb(volatile u_int32_t *a, u_int32_t *d, size_t c)
{
	while (c--)
		__asm__ volatile("lwbrx %0, 0, %1" : "=r"(*d++) : "r"(a));
	powerpc_iomb();
}

#define	outsb(a,s,c)	(__outsb((volatile u_int8_t *)(a), s, c))
#define	outs8(a,s,c)	outsb(a,s,c)
#define	outsw(a,s,c)	(__outsw((volatile u_int16_t *)(a), s, c))
#define	outs16(a,s,c)	outsw(a,s,c)
#define	outsl(a,s,c)	(__outsl((volatile u_int32_t *)(a), s, c))
#define	outs32(a,s,c)	outsl(a,s,c)
#define	outsll(a,s,c)	(__outsll((volatile u_int64_t *)(a), s, c))
#define	outs64(a,s,c)	outsll(a,s,c)
#define	insb(a,d,c)	(__insb((volatile u_int8_t *)(a), d, c))
#define	ins8(a,d,c)	insb(a,d,c)
#define	insw(a,d,c)	(__insw((volatile u_int16_t *)(a), d, c))
#define	ins16(a,d,c)	insw(a,d,c)
#define	insl(a,d,c)	(__insl((volatile u_int32_t *)(a), d, c))
#define	ins32(a,d,c)	insl(a,d,c)
#define	insll(a,d,c)	(__insll((volatile u_int64_t *)(a), d, c))
#define	ins64(a,d,c)	insll(a,d,c)

#define	outs8rb(a,s,c)	outsb(a,s,c)
#define	outswrb(a,s,c)	(__outswrb((volatile u_int16_t *)(a), s, c))
#define	outs16rb(a,s,c)	outswrb(a,s,c)
#define	outslrb(a,s,c)	(__outslrb((volatile u_int32_t *)(a), s, c))
#define	outs32rb(a,s,c)	outslrb(a,s,c)
#define	ins8rb(a,d,c)	insb(a,d,c)
#define	inswrb(a,d,c)	(__inswrb((volatile u_int16_t *)(a), d, c))
#define	ins16rb(a,d,c)	inswrb(a,d,c)
#define	inslrb(a,d,c)	(__inslrb((volatile u_int32_t *)(a), d, c))
#define	ins32rb(a,d,c)	inslrb(a,d,c)

#endif /*_MACHINE_PIO_H_*/
