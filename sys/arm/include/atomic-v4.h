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

#ifndef _MACHINE_ATOMIC_V4_H_
#define	_MACHINE_ATOMIC_V4_H_

#ifndef _MACHINE_ATOMIC_H_
#error Do not include this file directly, use <machine/atomic.h>
#endif

#if __ARM_ARCH <= 5
#define isb()  __asm __volatile("mcr p15, 0, %0, c7, c5, 4" : : "r" (0) : "memory")
#define dsb()  __asm __volatile("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory")
#define dmb()  dsb()
#else
#error Only use this file with ARMv5 and earlier
#endif

#define mb()   dmb()
#define wmb()  dmb()
#define rmb()  dmb()

#define __with_interrupts_disabled(expr) \
	do {						\
		u_int cpsr_save, tmp;			\
							\
		__asm __volatile(			\
			"mrs  %0, cpsr;"		\
			"orr  %1, %0, %2;"		\
			"msr  cpsr_fsxc, %1;"		\
			: "=r" (cpsr_save), "=r" (tmp)	\
			: "I" (PSR_I | PSR_F)		\
		        : "cc" );		\
		(expr);				\
		 __asm __volatile(		\
			"msr  cpsr_fsxc, %0"	\
			: /* no output */	\
			: "r" (cpsr_save)	\
			: "cc" );		\
	} while(0)

static __inline uint32_t
__swp(uint32_t val, volatile uint32_t *ptr)
{
	__asm __volatile("swp	%0, %2, [%3]"
	    : "=&r" (val), "=m" (*ptr)
	    : "r" (val), "r" (ptr), "m" (*ptr)
	    : "memory");
	return (val);
}


#ifdef _KERNEL
#define	ARM_HAVE_ATOMIC64

static __inline void
atomic_add_32(volatile u_int32_t *p, u_int32_t val)
{
	__with_interrupts_disabled(*p += val);
}

static __inline void
atomic_add_64(volatile u_int64_t *p, u_int64_t val)
{
	__with_interrupts_disabled(*p += val);
}

static __inline void
atomic_clear_32(volatile uint32_t *address, uint32_t clearmask)
{
	__with_interrupts_disabled(*address &= ~clearmask);
}

static __inline void
atomic_clear_64(volatile uint64_t *address, uint64_t clearmask)
{
	__with_interrupts_disabled(*address &= ~clearmask);
}

static __inline int
atomic_fcmpset_32(volatile u_int32_t *p, volatile u_int32_t *cmpval, volatile u_int32_t newval)
{
	int ret;

	__with_interrupts_disabled(
	 {
	 	ret = *p;
	    	if (*p == *cmpval) {
			*p = newval;
			ret = 1;
		} else {
			*cmpval = *p;
			ret = 0;
		}
	});
	return (ret);
}

static __inline int
atomic_fcmpset_64(volatile u_int64_t *p, volatile u_int64_t *cmpval, volatile u_int64_t newval)
{
	int ret;

	__with_interrupts_disabled(
	 {
	    	if (*p == *cmpval) {
			*p = newval;
			ret = 1;
		} else {
			*cmpval = *p;
			ret = 0;
		}
	});
	return (ret);
}

static __inline int
atomic_cmpset_32(volatile u_int32_t *p, volatile u_int32_t cmpval, volatile u_int32_t newval)
{
	int ret;

	__with_interrupts_disabled(
	 {
	    	if (*p == cmpval) {
			*p = newval;
			ret = 1;
		} else {
			ret = 0;
		}
	});
	return (ret);
}

static __inline int
atomic_cmpset_64(volatile u_int64_t *p, volatile u_int64_t cmpval, volatile u_int64_t newval)
{
	int ret;

	__with_interrupts_disabled(
	 {
	    	if (*p == cmpval) {
			*p = newval;
			ret = 1;
		} else {
			ret = 0;
		}
	});
	return (ret);
}


static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t v)
{
	uint32_t value;

	__with_interrupts_disabled(
	{
	    	value = *p;
		*p += v;
	});
	return (value);
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t value;

	__with_interrupts_disabled(
	{
	    	value = *p;
		*p += v;
	});
	return (value);
}

static __inline uint64_t
atomic_load_64(volatile uint64_t *p)
{
	uint64_t value;

	__with_interrupts_disabled(value = *p);
	return (value);
}

static __inline void
atomic_set_32(volatile uint32_t *address, uint32_t setmask)
{
	__with_interrupts_disabled(*address |= setmask);
}

static __inline void
atomic_set_64(volatile uint64_t *address, uint64_t setmask)
{
	__with_interrupts_disabled(*address |= setmask);
}

static __inline void
atomic_store_64(volatile uint64_t *p, uint64_t value)
{
	__with_interrupts_disabled(*p = value);
}

static __inline void
atomic_subtract_32(volatile u_int32_t *p, u_int32_t val)
{
	__with_interrupts_disabled(*p -= val);
}

static __inline void
atomic_subtract_64(volatile u_int64_t *p, u_int64_t val)
{
	__with_interrupts_disabled(*p -= val);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t value;

	__with_interrupts_disabled(
	{
		value = *p;
		*p = v;
	});
	return (value);
}

#else /* !_KERNEL */

static __inline void
atomic_add_32(volatile u_int32_t *p, u_int32_t val)
{
	int start, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "add	%1, %1, %3\n"
	    "str	%1, [%2]\n"
	    "2:\n"
	    "mov	%1, #0\n"
	    "str	%1, [%0]\n"
	    "mov	%1, #0xffffffff\n"
	    "str	%1, [%0, #4]\n"
	    : "+r" (ras_start), "=r" (start), "+r" (p), "+r" (val)
	    : : "memory");
}

static __inline void
atomic_clear_32(volatile uint32_t *address, uint32_t clearmask)
{
	int start, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "bic	%1, %1, %3\n"
	    "str	%1, [%2]\n"
	    "2:\n"
	    "mov	%1, #0\n"
	    "str	%1, [%0]\n"
	    "mov	%1, #0xffffffff\n"
	    "str	%1, [%0, #4]\n"
	    : "+r" (ras_start), "=r" (start), "+r" (address), "+r" (clearmask)
	    : : "memory");

}

static __inline int
atomic_cmpset_32(volatile u_int32_t *p, volatile u_int32_t cmpval, volatile u_int32_t newval)
{
	int done, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "cmp	%1, %3\n"
	    "streq	%4, [%2]\n"
	    "2:\n"
	    "mov	%1, #0\n"
	    "str	%1, [%0]\n"
	    "mov	%1, #0xffffffff\n"
	    "str	%1, [%0, #4]\n"
	    "moveq	%1, #1\n"
	    "movne	%1, #0\n"
	    : "+r" (ras_start), "=r" (done)
	    ,"+r" (p), "+r" (cmpval), "+r" (newval) : : "cc", "memory");
	return (done);
}

static __inline int
atomic_fcmpset_32(volatile u_int32_t *p, volatile u_int32_t *cmpval, volatile u_int32_t newval)
{
	int done, oldval, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "ldr	%5, [%3]\n"
	    "cmp	%1, %5\n"
	    "streq	%4, [%2]\n"
	    "2:\n"
	    "mov	%5, #0\n"
	    "str	%5, [%0]\n"
	    "mov	%5, #0xffffffff\n"
	    "str	%5, [%0, #4]\n"
	    "strne	%1, [%3]\n"
	    "moveq	%1, #1\n"
	    "movne	%1, #0\n"
	    : "+r" (ras_start), "=r" (done) ,"+r" (p)
	    , "+r" (cmpval), "+r" (newval), "+r" (oldval) : : "cc", "memory");
	return (done);
}

static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t v)
{
	uint32_t start, tmp, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%3]\n"
	    "mov	%2, %1\n"
	    "add	%2, %2, %4\n"
	    "str	%2, [%3]\n"
	    "2:\n"
	    "mov	%2, #0\n"
	    "str	%2, [%0]\n"
	    "mov	%2, #0xffffffff\n"
	    "str	%2, [%0, #4]\n"
	    : "+r" (ras_start), "=r" (start), "=r" (tmp), "+r" (p), "+r" (v)
	    : : "memory");
	return (start);
}

static __inline void
atomic_set_32(volatile uint32_t *address, uint32_t setmask)
{
	int start, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "orr	%1, %1, %3\n"
	    "str	%1, [%2]\n"
	    "2:\n"
	    "mov	%1, #0\n"
	    "str	%1, [%0]\n"
	    "mov	%1, #0xffffffff\n"
	    "str	%1, [%0, #4]\n"

	    : "+r" (ras_start), "=r" (start), "+r" (address), "+r" (setmask)
	    : : "memory");
}

static __inline void
atomic_subtract_32(volatile u_int32_t *p, u_int32_t val)
{
	int start, ras_start = ARM_RAS_START;

	__asm __volatile("1:\n"
	    "adr	%1, 1b\n"
	    "str	%1, [%0]\n"
	    "adr	%1, 2f\n"
	    "str	%1, [%0, #4]\n"
	    "ldr	%1, [%2]\n"
	    "sub	%1, %1, %3\n"
	    "str	%1, [%2]\n"
	    "2:\n"
	    "mov	%1, #0\n"
	    "str	%1, [%0]\n"
	    "mov	%1, #0xffffffff\n"
	    "str	%1, [%0, #4]\n"

	    : "+r" (ras_start), "=r" (start), "+r" (p), "+r" (val)
	    : : "memory");
}

#endif /* _KERNEL */

static __inline uint32_t
atomic_readandclear_32(volatile u_int32_t *p)
{

	return (__swp(0, p));
}

static __inline uint32_t
atomic_swap_32(volatile u_int32_t *p, u_int32_t v)
{

	return (__swp(v, p));
}

#define atomic_fcmpset_rel_32	atomic_fcmpset_32
#define atomic_fcmpset_acq_32	atomic_fcmpset_32
#ifdef _KERNEL
#define atomic_fcmpset_rel_64	atomic_fcmpset_64
#define atomic_fcmpset_acq_64	atomic_fcmpset_64
#endif
#define atomic_fcmpset_acq_long	atomic_fcmpset_long
#define atomic_fcmpset_rel_long	atomic_fcmpset_long
#define atomic_cmpset_rel_32	atomic_cmpset_32
#define atomic_cmpset_acq_32	atomic_cmpset_32
#ifdef _KERNEL
#define atomic_cmpset_rel_64	atomic_cmpset_64
#define atomic_cmpset_acq_64	atomic_cmpset_64
#endif
#define atomic_set_rel_32	atomic_set_32
#define atomic_set_acq_32	atomic_set_32
#define atomic_clear_rel_32	atomic_clear_32
#define atomic_clear_acq_32	atomic_clear_32
#define atomic_add_rel_32	atomic_add_32
#define atomic_add_acq_32	atomic_add_32
#define atomic_subtract_rel_32	atomic_subtract_32
#define atomic_subtract_acq_32	atomic_subtract_32
#define atomic_store_rel_32	atomic_store_32
#define atomic_store_rel_long	atomic_store_long
#define atomic_load_acq_32	atomic_load_32
#define atomic_load_acq_long	atomic_load_long
#define atomic_add_acq_long		atomic_add_long
#define atomic_add_rel_long		atomic_add_long
#define atomic_subtract_acq_long	atomic_subtract_long
#define atomic_subtract_rel_long	atomic_subtract_long
#define atomic_clear_acq_long		atomic_clear_long
#define atomic_clear_rel_long		atomic_clear_long
#define atomic_set_acq_long		atomic_set_long
#define atomic_set_rel_long		atomic_set_long
#define atomic_cmpset_acq_long		atomic_cmpset_long
#define atomic_cmpset_rel_long		atomic_cmpset_long
#define atomic_load_acq_long		atomic_load_long
#undef __with_interrupts_disabled

static __inline void
atomic_add_long(volatile u_long *p, u_long v)
{

	atomic_add_32((volatile uint32_t *)p, v);
}

static __inline void
atomic_clear_long(volatile u_long *p, u_long v)
{

	atomic_clear_32((volatile uint32_t *)p, v);
}

static __inline int
atomic_cmpset_long(volatile u_long *dst, u_long old, u_long newe)
{

	return (atomic_cmpset_32((volatile uint32_t *)dst, old, newe));
}

static __inline u_long
atomic_fcmpset_long(volatile u_long *dst, u_long *old, u_long newe)
{

	return (atomic_fcmpset_32((volatile uint32_t *)dst,
	    (uint32_t *)old, newe));
}

static __inline u_long
atomic_fetchadd_long(volatile u_long *p, u_long v)
{

	return (atomic_fetchadd_32((volatile uint32_t *)p, v));
}

static __inline void
atomic_readandclear_long(volatile u_long *p)
{

	atomic_readandclear_32((volatile uint32_t *)p);
}

static __inline void
atomic_set_long(volatile u_long *p, u_long v)
{

	atomic_set_32((volatile uint32_t *)p, v);
}

static __inline void
atomic_subtract_long(volatile u_long *p, u_long v)
{

	atomic_subtract_32((volatile uint32_t *)p, v);
}

/*
 * ARMv5 does not support SMP.  For both kernel and user modes, only a
 * compiler barrier is needed for fences, since CPU is always
 * self-consistent.
 */
static __inline void
atomic_thread_fence_acq(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_rel(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	__compiler_membar();
}

#endif /* _MACHINE_ATOMIC_H_ */
