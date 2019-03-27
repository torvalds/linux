/* $NetBSD: atomic.h,v 1.1 2002/10/19 12:22:34 bsh Exp $ */

/*-
 * Copyright (C) 2003-2004 Olivier Houchard
 * Copyright (C) 1994-1997 Mark Brinicombe
 * Copyright (C) 1994 Brini
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Brini.
 * 4. The name of Brini may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_ATOMIC_V6_H_
#define	_MACHINE_ATOMIC_V6_H_

#ifndef _MACHINE_ATOMIC_H_
#error Do not include this file directly, use <machine/atomic.h>
#endif

#if __ARM_ARCH >= 7
#define isb()  __asm __volatile("isb" : : : "memory")
#define dsb()  __asm __volatile("dsb" : : : "memory")
#define dmb()  __asm __volatile("dmb" : : : "memory")
#elif __ARM_ARCH >= 6
#define isb()  __asm __volatile("mcr p15, 0, %0, c7, c5, 4" : : "r" (0) : "memory")
#define dsb()  __asm __volatile("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory")
#define dmb()  __asm __volatile("mcr p15, 0, %0, c7, c10, 5" : : "r" (0) : "memory")
#else
#error Only use this file with ARMv6 and later
#endif

#define mb()   dmb()
#define wmb()  dmb()
#define rmb()  dmb()

#define	ARM_HAVE_ATOMIC64

#define ATOMIC_ACQ_REL_LONG(NAME)					\
static __inline void							\
atomic_##NAME##_acq_long(__volatile u_long *p, u_long v)		\
{									\
	atomic_##NAME##_long(p, v);					\
	dmb();								\
}									\
									\
static __inline  void							\
atomic_##NAME##_rel_long(__volatile u_long *p, u_long v)		\
{									\
	dmb();								\
	atomic_##NAME##_long(p, v);					\
}

#define	ATOMIC_ACQ_REL(NAME, WIDTH)					\
static __inline  void							\
atomic_##NAME##_acq_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	atomic_##NAME##_##WIDTH(p, v);					\
	dmb();								\
}									\
									\
static __inline  void							\
atomic_##NAME##_rel_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	dmb();								\
	atomic_##NAME##_##WIDTH(p, v);					\
}


static __inline void
atomic_add_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t tmp = 0, tmp2 = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%2]	\n"
	    "   add	%0, %0, %3	\n"
	    "   strex	%1, %0, [%2]	\n"
	    "   cmp	%1, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "=&r" (tmp), "+r" (tmp2)
	    ,"+r" (p), "+r" (val) : : "cc", "memory");
}

static __inline void
atomic_add_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   adds	%Q[tmp], %Q[val]			\n"
	    "   adc	%R[tmp], %R[tmp], %R[val]		\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
}

static __inline void
atomic_add_long(volatile u_long *p, u_long val)
{

	atomic_add_32((volatile uint32_t *)p, val);
}

ATOMIC_ACQ_REL(add, 32)
ATOMIC_ACQ_REL(add, 64)
ATOMIC_ACQ_REL_LONG(add)

static __inline void
atomic_clear_32(volatile uint32_t *address, uint32_t setmask)
{
	uint32_t tmp = 0, tmp2 = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%2]	\n"
	    "   bic	%0, %0, %3	\n"
	    "   strex	%1, %0, [%2]	\n"
	    "   cmp	%1, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "=&r" (tmp), "+r" (tmp2), "+r" (address), "+r" (setmask)
	    : : "cc", "memory");
}

static __inline void
atomic_clear_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   bic	%Q[tmp], %Q[val]			\n"
	    "   bic	%R[tmp], %R[val]			\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
}

static __inline void
atomic_clear_long(volatile u_long *address, u_long setmask)
{

	atomic_clear_32((volatile uint32_t *)address, setmask);
}

ATOMIC_ACQ_REL(clear, 32)
ATOMIC_ACQ_REL(clear, 64)
ATOMIC_ACQ_REL_LONG(clear)

static __inline int
atomic_fcmpset_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	uint32_t tmp;
	uint32_t _cmpval = *cmpval;
	int ret;

	__asm __volatile(
	    "   mov 	%0, #1		\n"
	    "   ldrex	%1, [%2]	\n"
	    "   cmp	%1, %3		\n"
	    "   it	eq		\n"
	    "   strexeq	%0, %4, [%2]	\n"
	    : "=&r" (ret), "=&r" (tmp), "+r" (p), "+r" (_cmpval), "+r" (newval)
	    : : "cc", "memory");
	*cmpval = tmp;
	return (!ret);
}

static __inline int
atomic_fcmpset_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	uint64_t tmp;
	uint64_t _cmpval = *cmpval;
	int ret;

	__asm __volatile(
	    "1:	mov	%[ret], #1				\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   teq	%Q[tmp], %Q[_cmpval]			\n"
	    "   ite	eq					\n"
	    "   teqeq	%R[tmp], %R[_cmpval]			\n"
	    "   bne	2f					\n"
	    "   strexd	%[ret], %Q[newval], %R[newval], [%[ptr]]\n"
	    "2:							\n"
	    : [ret]    "=&r" (ret),
	      [tmp]    "=&r" (tmp)
	    : [ptr]    "r"   (p),
	      [_cmpval] "r"   (_cmpval),
	      [newval] "r"   (newval)
	    : "cc", "memory");
	*cmpval = tmp;
	return (!ret);
}

static __inline int
atomic_fcmpset_long(volatile u_long *p, u_long *cmpval, u_long newval)
{

	return (atomic_fcmpset_32((volatile uint32_t *)p, 
	    (uint32_t *)cmpval, newval));
}

static __inline int
atomic_fcmpset_acq_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	int ret;

	ret = atomic_fcmpset_64(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_fcmpset_acq_long(volatile u_long *p, u_long *cmpval, u_long newval)
{
	int ret;

	ret = atomic_fcmpset_long(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_fcmpset_acq_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{

	int ret;

	ret = atomic_fcmpset_32(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_fcmpset_rel_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{

	dmb();
	return (atomic_fcmpset_32(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_rel_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{

	dmb();
	return (atomic_fcmpset_64(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_rel_long(volatile u_long *p, u_long *cmpval, u_long newval)
{

	dmb();
	return (atomic_fcmpset_long(p, cmpval, newval));
}

static __inline int
atomic_cmpset_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	int ret;

	__asm __volatile(
	    "1: ldrex	%0, [%1]	\n"
	    "   cmp	%0, %2		\n"
	    "   itt	ne		\n"
	    "   movne	%0, #0		\n"
	    "   bne	2f		\n"
	    "   strex	%0, %3, [%1]	\n"
	    "   cmp	%0, #0		\n"
	    "   ite	eq		\n"
	    "   moveq	%0, #1		\n"
	    "   bne	1b		\n"
	    "2:"
	    : "=&r" (ret), "+r" (p), "+r" (cmpval), "+r" (newval)
	    : : "cc", "memory");
	return (ret);
}

static __inline int
atomic_cmpset_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	uint64_t tmp;
	uint32_t ret;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   teq	%Q[tmp], %Q[cmpval]			\n"
	    "   itee	eq					\n"
	    "   teqeq	%R[tmp], %R[cmpval]			\n"
	    "   movne	%[ret], #0				\n"
	    "   bne	2f					\n"
	    "   strexd	%[ret], %Q[newval], %R[newval], [%[ptr]]\n"
	    "   teq	%[ret], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    "   mov	%[ret], #1				\n"
	    "2:							\n"
	    : [ret]    "=&r" (ret),
	      [tmp]    "=&r" (tmp)
	    : [ptr]    "r"   (p),
	      [cmpval] "r"   (cmpval),
	      [newval] "r"   (newval)
	    : "cc", "memory");
	return (ret);
}

static __inline int
atomic_cmpset_long(volatile u_long *p, u_long cmpval, u_long newval)
{

	return (atomic_cmpset_32((volatile uint32_t *)p, cmpval, newval));
}

static __inline int
atomic_cmpset_acq_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	int ret;

	ret = atomic_cmpset_32(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_cmpset_acq_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	int ret;

	ret = atomic_cmpset_64(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_cmpset_acq_long(volatile u_long *p, u_long cmpval, u_long newval)
{
	int ret;

	ret = atomic_cmpset_long(p, cmpval, newval);
	dmb();
	return (ret);
}

static __inline int
atomic_cmpset_rel_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{

	dmb();
	return (atomic_cmpset_32(p, cmpval, newval));
}

static __inline int
atomic_cmpset_rel_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{

	dmb();
	return (atomic_cmpset_64(p, cmpval, newval));
}

static __inline int
atomic_cmpset_rel_long(volatile u_long *p, u_long cmpval, u_long newval)
{

	dmb();
	return (atomic_cmpset_long(p, cmpval, newval));
}

static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t tmp = 0, tmp2 = 0, ret = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%3]	\n"
	    "   add	%1, %0, %4	\n"
	    "   strex	%2, %1, [%3]	\n"
	    "   cmp	%2, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "+r" (ret), "=&r" (tmp), "+r" (tmp2), "+r" (p), "+r" (val)
	    : : "cc", "memory");
	return (ret);
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t ret, tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[ret], %R[ret], [%[ptr]]		\n"
	    "   adds	%Q[tmp], %Q[ret], %Q[val]		\n"
	    "   adc	%R[tmp], %R[ret], %R[val]		\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [ret] "=&r" (ret),
	      [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
	return (ret);
}

static __inline u_long
atomic_fetchadd_long(volatile u_long *p, u_long val)
{

	return (atomic_fetchadd_32((volatile uint32_t *)p, val));
}

static __inline uint32_t
atomic_load_acq_32(volatile uint32_t *p)
{
	uint32_t v;

	v = *p;
	dmb();
	return (v);
}

static __inline uint64_t
atomic_load_64(volatile uint64_t *p)
{
	uint64_t ret;

	/*
	 * The only way to atomically load 64 bits is with LDREXD which puts the
	 * exclusive monitor into the exclusive state, so reset it to open state
	 * with CLREX because we don't actually need to store anything.
	 */
	__asm __volatile(
	    "ldrexd	%Q[ret], %R[ret], [%[ptr]]	\n"
	    "clrex					\n"
	    : [ret] "=&r" (ret)
	    : [ptr] "r"   (p)
	    : "cc", "memory");
	return (ret);
}

static __inline uint64_t
atomic_load_acq_64(volatile uint64_t *p)
{
	uint64_t ret;

	ret = atomic_load_64(p);
	dmb();
	return (ret);
}

static __inline u_long
atomic_load_acq_long(volatile u_long *p)
{
	u_long v;

	v = *p;
	dmb();
	return (v);
}

static __inline uint32_t
atomic_readandclear_32(volatile uint32_t *p)
{
	uint32_t ret, tmp = 0, tmp2 = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%3]	\n"
	    "   mov	%1, #0		\n"
	    "   strex	%2, %1, [%3]	\n"
	    "   cmp	%2, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "=r" (ret), "=&r" (tmp), "+r" (tmp2), "+r" (p)
	    : : "cc", "memory");
	return (ret);
}

static __inline uint64_t
atomic_readandclear_64(volatile uint64_t *p)
{
	uint64_t ret, tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[ret], %R[ret], [%[ptr]]		\n"
	    "   mov	%Q[tmp], #0				\n"
	    "   mov	%R[tmp], #0				\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [ret] "=&r" (ret),
	      [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p)
	    : "cc", "memory");
	return (ret);
}

static __inline u_long
atomic_readandclear_long(volatile u_long *p)
{

	return (atomic_readandclear_32((volatile uint32_t *)p));
}

static __inline void
atomic_set_32(volatile uint32_t *address, uint32_t setmask)
{
	uint32_t tmp = 0, tmp2 = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%2]	\n"
	    "   orr	%0, %0, %3	\n"
	    "   strex	%1, %0, [%2]	\n"
	    "   cmp	%1, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "=&r" (tmp), "+r" (tmp2), "+r" (address), "+r" (setmask)
	    : : "cc", "memory");
}

static __inline void
atomic_set_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   orr	%Q[tmp], %Q[val]			\n"
	    "   orr	%R[tmp], %R[val]			\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
}

static __inline void
atomic_set_long(volatile u_long *address, u_long setmask)
{

	atomic_set_32((volatile uint32_t *)address, setmask);
}

ATOMIC_ACQ_REL(set, 32)
ATOMIC_ACQ_REL(set, 64)
ATOMIC_ACQ_REL_LONG(set)

static __inline void
atomic_subtract_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t tmp = 0, tmp2 = 0;

	__asm __volatile(
	    "1: ldrex	%0, [%2]	\n"
	    "   sub	%0, %0, %3	\n"
	    "   strex	%1, %0, [%2]	\n"
	    "   cmp	%1, #0		\n"
	    "   it	ne		\n"
	    "   bne	1b		\n"
	    : "=&r" (tmp), "+r" (tmp2), "+r" (p), "+r" (val)
	    : : "cc", "memory");
}

static __inline void
atomic_subtract_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp;
	uint32_t exflag;

	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   subs	%Q[tmp], %Q[val]			\n"
	    "   sbc	%R[tmp], %R[tmp], %R[val]		\n"
	    "   strexd	%[exf], %Q[tmp], %R[tmp], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [exf] "=&r" (exflag),
	      [tmp] "=&r" (tmp)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
}

static __inline void
atomic_subtract_long(volatile u_long *p, u_long val)
{

	atomic_subtract_32((volatile uint32_t *)p, val);
}

ATOMIC_ACQ_REL(subtract, 32)
ATOMIC_ACQ_REL(subtract, 64)
ATOMIC_ACQ_REL_LONG(subtract)

static __inline void
atomic_store_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp;
	uint32_t exflag;

	/*
	 * The only way to atomically store 64 bits is with STREXD, which will
	 * succeed only if paired up with a preceeding LDREXD using the same
	 * address, so we read and discard the existing value before storing.
	 */
	__asm __volatile(
	    "1:							\n"
	    "   ldrexd	%Q[tmp], %R[tmp], [%[ptr]]		\n"
	    "   strexd	%[exf], %Q[val], %R[val], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [tmp] "=&r" (tmp),
	      [exf] "=&r" (exflag)
	    : [ptr] "r"   (p),
	      [val] "r"   (val)
	    : "cc", "memory");
}

static __inline void
atomic_store_rel_32(volatile uint32_t *p, uint32_t v)
{

	dmb();
	*p = v;
}

static __inline void
atomic_store_rel_64(volatile uint64_t *p, uint64_t val)
{

	dmb();
	atomic_store_64(p, val);
}

static __inline void
atomic_store_rel_long(volatile u_long *p, u_long v)
{

	dmb();
	*p = v;
}

static __inline int
atomic_testandset_32(volatile uint32_t *p, u_int v)
{
	uint32_t tmp, tmp2, res, mask;

	mask = 1u << (v & 0x1f);
	tmp = tmp2 = 0;
	__asm __volatile(
	"1:	ldrex	%0, [%4]	\n"
	"	orr	%1, %0, %3	\n"
	"	strex	%2, %1, [%4]	\n"
	"	cmp	%2, #0		\n"
	"	it	ne		\n"
	"	bne	1b		\n"
	: "=&r" (res), "=&r" (tmp), "=&r" (tmp2)
	: "r" (mask), "r" (p)
	: "cc", "memory");
	return ((res & mask) != 0);
}

static __inline int
atomic_testandset_int(volatile u_int *p, u_int v)
{

	return (atomic_testandset_32((volatile uint32_t *)p, v));
}

static __inline int
atomic_testandset_long(volatile u_long *p, u_int v)
{

	return (atomic_testandset_32((volatile uint32_t *)p, v));
}

static __inline int
atomic_testandset_64(volatile uint64_t *p, u_int v)
{
	volatile uint32_t *p32;

	p32 = (volatile uint32_t *)p;
	/* Assume little-endian */
	if (v >= 32) {
		v &= 0x1f;
		p32++;
	}
	return (atomic_testandset_32(p32, v));
}

static __inline uint32_t
atomic_swap_32(volatile uint32_t *p, uint32_t v)
{
	uint32_t ret, exflag;

	__asm __volatile(
	    "1: ldrex	%[ret], [%[ptr]]		\n"
	    "   strex	%[exf], %[val], [%[ptr]]	\n"
	    "   teq	%[exf], #0			\n"
	    "   it	ne				\n"
	    "   bne	1b				\n"
	    : [ret] "=&r"  (ret),
	      [exf] "=&r" (exflag)
	    : [val] "r"  (v),
	      [ptr] "r"  (p)
	    : "cc", "memory");
	return (ret);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t ret;
	uint32_t exflag;

	__asm __volatile(
	    "1: ldrexd	%Q[ret], %R[ret], [%[ptr]]		\n"
	    "   strexd	%[exf], %Q[val], %R[val], [%[ptr]]	\n"
	    "   teq	%[exf], #0				\n"
	    "   it	ne					\n"
	    "   bne	1b					\n"
	    : [ret] "=&r" (ret),
	      [exf] "=&r" (exflag)
	    : [val] "r"   (v),
	      [ptr] "r"   (p)
	    : "cc", "memory");
	return (ret);
}

#undef ATOMIC_ACQ_REL
#undef ATOMIC_ACQ_REL_LONG

static __inline void
atomic_thread_fence_acq(void)
{

	dmb();
}

static __inline void
atomic_thread_fence_rel(void)
{

	dmb();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	dmb();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	dmb();
}

#endif /* _MACHINE_ATOMIC_V6_H_ */
