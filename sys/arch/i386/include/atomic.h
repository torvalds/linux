/*	$OpenBSD: atomic.h,v 1.20 2022/08/29 02:01:18 jsg Exp $	*/
/* $NetBSD: atomic.h,v 1.1.2.2 2000/02/21 18:54:07 sommerfeld Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

/*
 * Perform atomic operations on memory. Should be atomic with respect
 * to interrupts and multiple processors.
 *
 * void atomic_setbits_int(volatile u_int *a, u_int mask) { *a |= mask; }
 * void atomic_clearbits_int(volatile u_int *a, u_int mas) { *a &= ~mask; }
 */
#if !defined(_LOCORE)

#if defined(MULTIPROCESSOR) || !defined(_KERNEL)
#define _LOCK "lock"
#else
#define _LOCK
#endif

static inline unsigned int
_atomic_cas_uint(volatile unsigned int *p, unsigned int e, unsigned int n)
{
	__asm volatile(_LOCK " cmpxchgl %2, %1"
	    : "=a" (n), "=m" (*p)
	    : "r" (n), "a" (e), "m" (*p));

	return (n);
}
#define atomic_cas_uint(_p, _e, _n) _atomic_cas_uint((_p), (_e), (_n))

static inline unsigned long
_atomic_cas_ulong(volatile unsigned long *p, unsigned long e, unsigned long n)
{
	__asm volatile(_LOCK " cmpxchgl %2, %1"
	    : "=a" (n), "=m" (*p)
	    : "r" (n), "a" (e), "m" (*p));

	return (n);
}
#define atomic_cas_ulong(_p, _e, _n) _atomic_cas_ulong((_p), (_e), (_n))

static inline void *
_atomic_cas_ptr(volatile void *p, void *e, void *n)
{
	__asm volatile(_LOCK " cmpxchgl %2, %1"
	    : "=a" (n), "=m" (*(unsigned long *)p)
	    : "r" (n), "a" (e), "m" (*(unsigned long *)p));

	return (n);
}
#define atomic_cas_ptr(_p, _e, _n) _atomic_cas_ptr((_p), (_e), (_n))

static inline unsigned int
_atomic_swap_uint(volatile unsigned int *p, unsigned int n)
{
	__asm volatile("xchgl %0, %1"
	    : "=a" (n), "=m" (*p)
	    : "0" (n), "m" (*p));

	return (n);
}
#define atomic_swap_uint(_p, _n) _atomic_swap_uint((_p), (_n))
#define atomic_swap_32(_p, _n) _atomic_swap_uint((_p), (_n))

static inline unsigned long
_atomic_swap_ulong(volatile unsigned long *p, unsigned long n)
{
	__asm volatile("xchgl %0, %1"
	    : "=a" (n), "=m" (*p)
	    : "0" (n), "m" (*p));

	return (n);
}
#define atomic_swap_ulong(_p, _n) _atomic_swap_ulong((_p), (_n))

static inline void *
_atomic_swap_ptr(volatile void *p, void *n)
{
	__asm volatile("xchgl %0, %1"
	    : "=a" (n), "=m" (*(unsigned long *)p)
	    : "0" (n), "m" (*(unsigned long *)p));

	return (n);
}
#define atomic_swap_ptr(_p, _n) _atomic_swap_ptr((_p), (_n))

static inline void
_atomic_inc_int(volatile unsigned int *p)
{
	__asm volatile(_LOCK " incl %0"
	    : "+m" (*p));
}
#define atomic_inc_int(_p) _atomic_inc_int(_p)

static inline void
_atomic_inc_long(volatile unsigned long *p)
{
	__asm volatile(_LOCK " incl %0"
	    : "+m" (*p));
}
#define atomic_inc_long(_p) _atomic_inc_long(_p)

static inline void
_atomic_dec_int(volatile unsigned int *p)
{
	__asm volatile(_LOCK " decl %0"
	    : "+m" (*p));
}
#define atomic_dec_int(_p) _atomic_dec_int(_p)

static inline void
_atomic_dec_long(volatile unsigned long *p)
{
	__asm volatile(_LOCK " decl %0"
	    : "+m" (*p));
}
#define atomic_dec_long(_p) _atomic_dec_long(_p)

static inline void
_atomic_add_int(volatile unsigned int *p, unsigned int v)
{
	__asm volatile(_LOCK " addl %1,%0"
	    : "+m" (*p)
	    : "a" (v));
}
#define atomic_add_int(_p, _v) _atomic_add_int(_p, _v)

static inline void
_atomic_add_long(volatile unsigned long *p, unsigned long v)
{
	__asm volatile(_LOCK " addl %1,%0"
	    : "+m" (*p)
	    : "a" (v));
}
#define atomic_add_long(_p, _v) _atomic_add_long(_p, _v)

static inline void
_atomic_sub_int(volatile unsigned int *p, unsigned int v)
{
	__asm volatile(_LOCK " subl %1,%0"
	    : "+m" (*p)
	    : "a" (v));
}
#define atomic_sub_int(_p, _v) _atomic_sub_int(_p, _v)

static inline void
_atomic_sub_long(volatile unsigned long *p, unsigned long v)
{
	__asm volatile(_LOCK " subl %1,%0"
	    : "+m" (*p)
	    : "a" (v));
}
#define atomic_sub_long(_p, _v) _atomic_sub_long(_p, _v)


static inline unsigned long
_atomic_add_int_nv(volatile unsigned int *p, unsigned int v)
{
	unsigned int rv = v;

	__asm volatile(_LOCK " xaddl %0,%1"
	    : "+a" (rv), "+m" (*p));

	return (rv + v);
}
#define atomic_add_int_nv(_p, _v) _atomic_add_int_nv(_p, _v)

static inline unsigned long
_atomic_add_long_nv(volatile unsigned long *p, unsigned long v)
{
	unsigned long rv = v;

	__asm volatile(_LOCK " xaddl %0,%1"
	    : "+a" (rv), "+m" (*p));

	return (rv + v);
}
#define atomic_add_long_nv(_p, _v) _atomic_add_long_nv(_p, _v)

static inline unsigned long
_atomic_sub_int_nv(volatile unsigned int *p, unsigned int v)
{
	unsigned int rv = 0 - v;

	__asm volatile(_LOCK " xaddl %0,%1"
	    : "+a" (rv), "+m" (*p));

	return (rv - v);
}
#define atomic_sub_int_nv(_p, _v) _atomic_sub_int_nv(_p, _v)

static inline unsigned long
_atomic_sub_long_nv(volatile unsigned long *p, unsigned long v)
{
	unsigned long rv = 0 - v;

	__asm volatile(_LOCK " xaddl %0,%1"
	    : "+a" (rv), "+m" (*p));

	return (rv - v);
}
#define atomic_sub_long_nv(_p, _v) _atomic_sub_long_nv(_p, _v)

/*
 * The IA-32 architecture is rather strongly ordered.  When accessing
 * normal write-back cacheable memory, only reads may be reordered with
 * older writes to different locations.  There are a few instructions
 * (clfush, non-temporal move instructions) that obey weaker ordering
 * rules, but those instructions will only be used in (inline)
 * assembly code where we can add the necessary fence instructions
 * ourselves.
 */

#define __membar(_f) do { __asm volatile(_f ::: "memory"); } while (0)

#if defined(MULTIPROCESSOR) || !defined(_KERNEL)
#define membar_enter()		__membar("lock; addl $0,0(%%esp)")
#define membar_exit()		__membar("")
#define membar_producer()	__membar("")
#define membar_consumer()	__membar("")
#define membar_sync()		__membar("lock; addl $0,0(%%esp)")
#else
#define membar_enter()		__membar("")
#define membar_exit()		__membar("")
#define membar_producer()	__membar("")
#define membar_consumer()	__membar("")
#define membar_sync()		__membar("")
#endif

#define membar_enter_after_atomic()	__membar("")
#define membar_exit_before_atomic()	__membar("")

#ifdef _KERNEL

/* virtio needs MP membars even on SP kernels */
#define virtio_membar_producer()	__membar("")
#define virtio_membar_consumer()	__membar("")
#define virtio_membar_sync()		__membar("lock; addl $0,0(%%esp)")

static __inline u_int64_t
i386_atomic_testset_uq(volatile u_int64_t *ptr, u_int64_t val)
{
	__asm__ volatile ("\n1:\t" _LOCK " cmpxchg8b (%1); jnz 1b" : "+A" (val) :
	    "r" (ptr), "b" ((u_int32_t)val), "c" ((u_int32_t)(val >> 32)));
	return val;
}

static __inline u_int32_t
i386_atomic_testset_ul(volatile u_int32_t *ptr, unsigned long val)
{
	__asm__ volatile ("xchgl %0,(%2)" :"=r" (val):"0" (val),"r" (ptr));
	return val;
}

static __inline int
i386_atomic_testset_i(volatile int *ptr, unsigned long val)
{
	__asm__ volatile ("xchgl %0,(%2)" :"=r" (val):"0" (val),"r" (ptr));
	return val;
}

static __inline void
i386_atomic_setbits_l(volatile u_int32_t *ptr, unsigned long bits)
{
	__asm volatile(_LOCK " orl %1,%0" :  "=m" (*ptr) : "ir" (bits));
}

static __inline void
i386_atomic_clearbits_l(volatile u_int32_t *ptr, unsigned long bits)
{
	bits = ~bits;
	__asm volatile(_LOCK " andl %1,%0" :  "=m" (*ptr) : "ir" (bits));
}

#define atomic_setbits_int i386_atomic_setbits_l
#define atomic_clearbits_int i386_atomic_clearbits_l

#endif /* _KERNEL */

#undef _LOCK

#endif /* !defined(_LOCORE) */
#endif /* _MACHINE_ATOMIC_H_ */
