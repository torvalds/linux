/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: src/sys/alpha/include/atomic.h,v 1.21.2.3 2005/10/06 18:12:05 jhb
 * $FreeBSD$
 */

#ifndef _MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif

#include <sys/atomic_common.h>

/*
 * Note: All the 64-bit atomic operations are only atomic when running
 * in 64-bit mode.  It is assumed that code compiled for n32 and n64
 * fits into this definition and no further safeties are needed.
 *
 * It is also assumed that the add, subtract and other arithmetic is
 * done on numbers not pointers.  The special rules for n32 pointers
 * do not have atomic operations defined for them, but generally shouldn't
 * need atomic operations.
 */
#ifndef __MIPS_PLATFORM_SYNC_NOPS
#define __MIPS_PLATFORM_SYNC_NOPS ""
#endif

static __inline  void
mips_sync(void)
{
	__asm __volatile (".set noreorder\n"
			"\tsync\n"
			__MIPS_PLATFORM_SYNC_NOPS
			".set reorder\n"
			: : : "memory");
}

#define mb()	mips_sync()
#define wmb()	mips_sync()
#define rmb()	mips_sync()

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and SMP safe.
 */

void atomic_set_8(__volatile uint8_t *, uint8_t);
void atomic_clear_8(__volatile uint8_t *, uint8_t);
void atomic_add_8(__volatile uint8_t *, uint8_t);
void atomic_subtract_8(__volatile uint8_t *, uint8_t);

void atomic_set_16(__volatile uint16_t *, uint16_t);
void atomic_clear_16(__volatile uint16_t *, uint16_t);
void atomic_add_16(__volatile uint16_t *, uint16_t);
void atomic_subtract_16(__volatile uint16_t *, uint16_t);

static __inline void
atomic_set_32(__volatile uint32_t *p, uint32_t v)
{
	uint32_t temp;

	__asm __volatile (
		"1:\tll	%0, %3\n\t"		/* load old value */
		"or	%0, %2, %0\n\t"		/* calculate new value */
		"sc	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");

}

static __inline void
atomic_clear_32(__volatile uint32_t *p, uint32_t v)
{
	uint32_t temp;
	v = ~v;

	__asm __volatile (
		"1:\tll	%0, %3\n\t"		/* load old value */
		"and	%0, %2, %0\n\t"		/* calculate new value */
		"sc	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline void
atomic_add_32(__volatile uint32_t *p, uint32_t v)
{
	uint32_t temp;

	__asm __volatile (
		"1:\tll	%0, %3\n\t"		/* load old value */
		"addu	%0, %2, %0\n\t"		/* calculate new value */
		"sc	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline void
atomic_subtract_32(__volatile uint32_t *p, uint32_t v)
{
	uint32_t temp;

	__asm __volatile (
		"1:\tll	%0, %3\n\t"		/* load old value */
		"subu	%0, %2\n\t"		/* calculate new value */
		"sc	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline uint32_t
atomic_readandclear_32(__volatile uint32_t *addr)
{
	uint32_t result,temp;

	__asm __volatile (
		"1:\tll	 %0,%3\n\t"	/* load current value, asserting lock */
		"li	 %1,0\n\t"		/* value to store */
		"sc	 %1,%2\n\t"	/* attempt to store */
		"beqz	 %1, 1b\n\t"		/* if the store failed, spin */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m" (*addr)
		: "memory");

	return result;
}

static __inline uint32_t
atomic_readandset_32(__volatile uint32_t *addr, uint32_t value)
{
	uint32_t result,temp;

	__asm __volatile (
		"1:\tll	 %0,%3\n\t"	/* load current value, asserting lock */
		"or      %1,$0,%4\n\t"
		"sc	 %1,%2\n\t"	/* attempt to store */
		"beqz	 %1, 1b\n\t"		/* if the store failed, spin */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m" (*addr), "r" (value)
		: "memory");

	return result;
}

#if defined(__mips_n64) || defined(__mips_n32)
static __inline void
atomic_set_64(__volatile uint64_t *p, uint64_t v)
{
	uint64_t temp;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %3\n\t"		/* load old value */
		"or	%0, %2, %0\n\t"		/* calculate new value */
		"scd	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");

}

static __inline void
atomic_clear_64(__volatile uint64_t *p, uint64_t v)
{
	uint64_t temp;
	v = ~v;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %3\n\t"		/* load old value */
		"and	%0, %2, %0\n\t"		/* calculate new value */
		"scd	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline void
atomic_add_64(__volatile uint64_t *p, uint64_t v)
{
	uint64_t temp;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %3\n\t"		/* load old value */
		"daddu	%0, %2, %0\n\t"		/* calculate new value */
		"scd	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline void
atomic_subtract_64(__volatile uint64_t *p, uint64_t v)
{
	uint64_t temp;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %3\n\t"		/* load old value */
		"dsubu	%0, %2\n\t"		/* calculate new value */
		"scd	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* spin if failed */
		: "=&r" (temp), "=m" (*p)
		: "r" (v), "m" (*p)
		: "memory");
}

static __inline uint64_t
atomic_readandclear_64(__volatile uint64_t *addr)
{
	uint64_t result,temp;

	__asm __volatile (
		"1:\n\t"
		"lld	 %0, %3\n\t"		/* load old value */
		"li	 %1, 0\n\t"		/* value to store */
		"scd	 %1, %2\n\t"		/* attempt to store */
		"beqz	 %1, 1b\n\t"		/* if the store failed, spin */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m" (*addr)
		: "memory");

	return result;
}

static __inline uint64_t
atomic_readandset_64(__volatile uint64_t *addr, uint64_t value)
{
	uint64_t result,temp;

	__asm __volatile (
		"1:\n\t"
		"lld	 %0,%3\n\t"		/* Load old value*/
		"or      %1,$0,%4\n\t"
		"scd	 %1,%2\n\t"		/* attempt to store */
		"beqz	 %1, 1b\n\t"		/* if the store failed, spin */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m" (*addr), "r" (value)
		: "memory");

	return result;
}
#endif

#define	ATOMIC_ACQ_REL(NAME, WIDTH)					\
static __inline  void							\
atomic_##NAME##_acq_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	atomic_##NAME##_##WIDTH(p, v);					\
	mips_sync(); 							\
}									\
									\
static __inline  void							\
atomic_##NAME##_rel_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	mips_sync();							\
	atomic_##NAME##_##WIDTH(p, v);					\
}

/* Variants of simple arithmetic with memory barriers. */
ATOMIC_ACQ_REL(set, 8)
ATOMIC_ACQ_REL(clear, 8)
ATOMIC_ACQ_REL(add, 8)
ATOMIC_ACQ_REL(subtract, 8)
ATOMIC_ACQ_REL(set, 16)
ATOMIC_ACQ_REL(clear, 16)
ATOMIC_ACQ_REL(add, 16)
ATOMIC_ACQ_REL(subtract, 16)
ATOMIC_ACQ_REL(set, 32)
ATOMIC_ACQ_REL(clear, 32)
ATOMIC_ACQ_REL(add, 32)
ATOMIC_ACQ_REL(subtract, 32)
#if defined(__mips_n64) || defined(__mips_n32)
ATOMIC_ACQ_REL(set, 64)
ATOMIC_ACQ_REL(clear, 64)
ATOMIC_ACQ_REL(add, 64)
ATOMIC_ACQ_REL(subtract, 64)
#endif

#undef ATOMIC_ACQ_REL

/*
 * We assume that a = b will do atomic loads and stores.
 */
#define	ATOMIC_STORE_LOAD(WIDTH)			\
static __inline  uint##WIDTH##_t			\
atomic_load_acq_##WIDTH(__volatile uint##WIDTH##_t *p)	\
{							\
	uint##WIDTH##_t v;				\
							\
	v = *p;						\
	mips_sync();					\
	return (v);					\
}							\
							\
static __inline  void					\
atomic_store_rel_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{							\
	mips_sync();					\
	*p = v;						\
}

ATOMIC_STORE_LOAD(32)
ATOMIC_STORE_LOAD(64)
#undef ATOMIC_STORE_LOAD

/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_32(__volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	int ret;

	__asm __volatile (
		"1:\tll	%0, %4\n\t"		/* load old value */
		"bne %0, %2, 2f\n\t"		/* compare */
		"move %0, %3\n\t"		/* value to store */
		"sc %0, %1\n\t"			/* attempt to store */
		"beqz %0, 1b\n\t"		/* if it failed, spin */
		"j 3f\n\t"
		"2:\n\t"
		"li	%0, 0\n\t"
		"3:\n"
		: "=&r" (ret), "=m" (*p)
		: "r" (cmpval), "r" (newval), "m" (*p)
		: "memory");

	return ret;
}

/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_acq_32(__volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	int retval;

	retval = atomic_cmpset_32(p, cmpval, newval);
	mips_sync();
	return (retval);
}

static __inline int
atomic_cmpset_rel_32(__volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	mips_sync();
	return (atomic_cmpset_32(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_32(__volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	int ret;

	__asm __volatile (
		"1:\n\t"
		"ll	%0, %1\n\t"		/* load old value */
		"bne	%0, %4, 2f\n\t"		/* compare */
		"move	%0, %3\n\t"		/* value to store */
		"sc	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* if it failed, spin */
		"j	3f\n\t"
		"2:\n\t"
		"sw	%0, %2\n\t"		/* save old value */
		"li	%0, 0\n\t"
		"3:\n"
		: "=&r" (ret), "+m" (*p), "=m" (*cmpval)
		: "r" (newval), "r" (*cmpval)
		: "memory");
	return ret;
}

static __inline int
atomic_fcmpset_acq_32(__volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	int retval;

	retval = atomic_fcmpset_32(p, cmpval, newval);
	mips_sync();
	return (retval);
}

static __inline int
atomic_fcmpset_rel_32(__volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	mips_sync();
	return (atomic_fcmpset_32(p, cmpval, newval));
}

/*
 * Atomically add the value of v to the integer pointed to by p and return
 * the previous value of *p.
 */
static __inline uint32_t
atomic_fetchadd_32(__volatile uint32_t *p, uint32_t v)
{
	uint32_t value, temp;

	__asm __volatile (
		"1:\tll %0, %1\n\t"		/* load old value */
		"addu %2, %3, %0\n\t"		/* calculate new value */
		"sc %2, %1\n\t"			/* attempt to store */
		"beqz %2, 1b\n\t"		/* spin if failed */
		: "=&r" (value), "=m" (*p), "=&r" (temp)
		: "r" (v), "m" (*p));
	return (value);
}

#if defined(__mips_n64) || defined(__mips_n32)
/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_64(__volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	int ret;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %4\n\t"		/* load old value */
		"bne	%0, %2, 2f\n\t"		/* compare */
		"move	%0, %3\n\t"		/* value to store */
		"scd	%0, %1\n\t"		/* attempt to store */
		"beqz	%0, 1b\n\t"		/* if it failed, spin */
		"j	3f\n\t"
		"2:\n\t"
		"li	%0, 0\n\t"
		"3:\n"
		: "=&r" (ret), "=m" (*p)
		: "r" (cmpval), "r" (newval), "m" (*p)
		: "memory");

	return ret;
}

/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_acq_64(__volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	int retval;

	retval = atomic_cmpset_64(p, cmpval, newval);
	mips_sync();
	return (retval);
}

static __inline int
atomic_cmpset_rel_64(__volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	mips_sync();
	return (atomic_cmpset_64(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_64(__volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
        int ret;

        __asm __volatile (
                "1:\n\t"
		"lld	%0, %1\n\t"		/* load old value */
                "bne	%0, %4, 2f\n\t"		/* compare */
                "move	%0, %3\n\t"		/* value to store */
                "scd	%0, %1\n\t"		/* attempt to store */
                "beqz	%0, 1b\n\t"		/* if it failed, spin */
                "j	3f\n\t"
                "2:\n\t"
                "sd	%0, %2\n\t"		/* save old value */
                "li	%0, 0\n\t"
                "3:\n"
                : "=&r" (ret), "+m" (*p), "=m" (*cmpval)
                : "r" (newval), "r" (*cmpval)
                : "memory");

	return ret;
}

static __inline int
atomic_fcmpset_acq_64(__volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	int retval;

	retval = atomic_fcmpset_64(p, cmpval, newval);
	mips_sync();
	return (retval);
}

static __inline int
atomic_fcmpset_rel_64(__volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	mips_sync();
	return (atomic_fcmpset_64(p, cmpval, newval));
}

/*
 * Atomically add the value of v to the integer pointed to by p and return
 * the previous value of *p.
 */
static __inline uint64_t
atomic_fetchadd_64(__volatile uint64_t *p, uint64_t v)
{
	uint64_t value, temp;

	__asm __volatile (
		"1:\n\t"
		"lld	%0, %1\n\t"		/* load old value */
		"daddu	%2, %3, %0\n\t"		/* calculate new value */
		"scd	%2, %1\n\t"		/* attempt to store */
		"beqz	%2, 1b\n\t"		/* spin if failed */
		: "=&r" (value), "=m" (*p), "=&r" (temp)
		: "r" (v), "m" (*p));
	return (value);
}
#endif

static __inline void
atomic_thread_fence_acq(void)
{

	mips_sync();
}

static __inline void
atomic_thread_fence_rel(void)
{

	mips_sync();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	mips_sync();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	mips_sync();
}

/* Operations on chars. */
#define	atomic_set_char		atomic_set_8
#define	atomic_set_acq_char	atomic_set_acq_8
#define	atomic_set_rel_char	atomic_set_rel_8
#define	atomic_clear_char	atomic_clear_8
#define	atomic_clear_acq_char	atomic_clear_acq_8
#define	atomic_clear_rel_char	atomic_clear_rel_8
#define	atomic_add_char		atomic_add_8
#define	atomic_add_acq_char	atomic_add_acq_8
#define	atomic_add_rel_char	atomic_add_rel_8
#define	atomic_subtract_char	atomic_subtract_8
#define	atomic_subtract_acq_char	atomic_subtract_acq_8
#define	atomic_subtract_rel_char	atomic_subtract_rel_8

/* Operations on shorts. */
#define	atomic_set_short	atomic_set_16
#define	atomic_set_acq_short	atomic_set_acq_16
#define	atomic_set_rel_short	atomic_set_rel_16
#define	atomic_clear_short	atomic_clear_16
#define	atomic_clear_acq_short	atomic_clear_acq_16
#define	atomic_clear_rel_short	atomic_clear_rel_16
#define	atomic_add_short	atomic_add_16
#define	atomic_add_acq_short	atomic_add_acq_16
#define	atomic_add_rel_short	atomic_add_rel_16
#define	atomic_subtract_short	atomic_subtract_16
#define	atomic_subtract_acq_short	atomic_subtract_acq_16
#define	atomic_subtract_rel_short	atomic_subtract_rel_16

/* Operations on ints. */
#define	atomic_set_int		atomic_set_32
#define	atomic_set_acq_int	atomic_set_acq_32
#define	atomic_set_rel_int	atomic_set_rel_32
#define	atomic_clear_int	atomic_clear_32
#define	atomic_clear_acq_int	atomic_clear_acq_32
#define	atomic_clear_rel_int	atomic_clear_rel_32
#define	atomic_add_int		atomic_add_32
#define	atomic_add_acq_int	atomic_add_acq_32
#define	atomic_add_rel_int	atomic_add_rel_32
#define	atomic_subtract_int	atomic_subtract_32
#define	atomic_subtract_acq_int	atomic_subtract_acq_32
#define	atomic_subtract_rel_int	atomic_subtract_rel_32
#define	atomic_cmpset_int	atomic_cmpset_32
#define	atomic_cmpset_acq_int	atomic_cmpset_acq_32
#define	atomic_cmpset_rel_int	atomic_cmpset_rel_32
#define	atomic_fcmpset_int	atomic_fcmpset_32
#define	atomic_fcmpset_acq_int	atomic_fcmpset_acq_32
#define	atomic_fcmpset_rel_int	atomic_fcmpset_rel_32
#define	atomic_load_acq_int	atomic_load_acq_32
#define	atomic_store_rel_int	atomic_store_rel_32
#define	atomic_readandclear_int	atomic_readandclear_32
#define	atomic_readandset_int	atomic_readandset_32
#define	atomic_fetchadd_int	atomic_fetchadd_32

/*
 * I think the following is right, even for n32.  For n32 the pointers
 * are still 32-bits, so we need to operate on them as 32-bit quantities,
 * even though they are sign extended in operation.  For longs, there's
 * no question because they are always 32-bits.
 */
#ifdef __mips_n64
/* Operations on longs. */
#define	atomic_set_long		atomic_set_64
#define	atomic_set_acq_long	atomic_set_acq_64
#define	atomic_set_rel_long	atomic_set_rel_64
#define	atomic_clear_long	atomic_clear_64
#define	atomic_clear_acq_long	atomic_clear_acq_64
#define	atomic_clear_rel_long	atomic_clear_rel_64
#define	atomic_add_long		atomic_add_64
#define	atomic_add_acq_long	atomic_add_acq_64
#define	atomic_add_rel_long	atomic_add_rel_64
#define	atomic_subtract_long	atomic_subtract_64
#define	atomic_subtract_acq_long	atomic_subtract_acq_64
#define	atomic_subtract_rel_long	atomic_subtract_rel_64
#define	atomic_cmpset_long	atomic_cmpset_64
#define	atomic_cmpset_acq_long	atomic_cmpset_acq_64
#define	atomic_cmpset_rel_long	atomic_cmpset_rel_64
#define	atomic_fcmpset_long	atomic_fcmpset_64
#define	atomic_fcmpset_acq_long	atomic_fcmpset_acq_64
#define	atomic_fcmpset_rel_long	atomic_fcmpset_rel_64
#define	atomic_load_acq_long	atomic_load_acq_64
#define	atomic_store_rel_long	atomic_store_rel_64
#define	atomic_fetchadd_long	atomic_fetchadd_64
#define	atomic_readandclear_long	atomic_readandclear_64

#else /* !__mips_n64 */

/* Operations on longs. */
#define	atomic_set_long(p, v)						\
	atomic_set_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_acq_long(p, v)					\
	atomic_set_acq_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_rel_long(p, v)					\
	atomic_set_rel_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_long(p, v)						\
	atomic_clear_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_acq_long(p, v)					\
	atomic_clear_acq_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_rel_long(p, v)					\
	atomic_clear_rel_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_long(p, v)						\
	atomic_add_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_acq_long(p, v)					\
	atomic_add_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_rel_long(p, v)					\
	atomic_add_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_long(p, v)					\
	atomic_subtract_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_acq_long(p, v)					\
	atomic_subtract_acq_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_rel_long(p, v)					\
	atomic_subtract_rel_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_cmpset_long(p, cmpval, newval)				\
	atomic_cmpset_32((volatile u_int *)(p), (u_int)(cmpval),	\
	    (u_int)(newval))
#define	atomic_cmpset_acq_long(p, cmpval, newval)			\
	atomic_cmpset_acq_32((volatile u_int *)(p), (u_int)(cmpval),	\
	    (u_int)(newval))
#define	atomic_cmpset_rel_long(p, cmpval, newval)			\
	atomic_cmpset_rel_32((volatile u_int *)(p), (u_int)(cmpval),	\
	    (u_int)(newval))
#define	atomic_fcmpset_long(p, cmpval, newval)				\
	atomic_fcmpset_32((volatile u_int *)(p), (u_int *)(cmpval),	\
	    (u_int)(newval))
#define	atomic_fcmpset_acq_long(p, cmpval, newval)			\
	atomic_fcmpset_acq_32((volatile u_int *)(p), (u_int *)(cmpval),	\
	    (u_int)(newval))
#define	atomic_fcmpset_rel_long(p, cmpval, newval)			\
	atomic_fcmpset_rel_32((volatile u_int *)(p), (u_int *)(cmpval),	\
	    (u_int)(newval))
#define	atomic_load_acq_long(p)						\
	(u_long)atomic_load_acq_32((volatile u_int *)(p))
#define	atomic_store_rel_long(p, v)					\
	atomic_store_rel_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_fetchadd_long(p, v)					\
	atomic_fetchadd_32((volatile u_int *)(p), (u_int)(v))
#define	atomic_readandclear_long(p)					\
	atomic_readandclear_32((volatile u_int *)(p))

#endif /* __mips_n64 */

/* Operations on pointers. */
#define	atomic_set_ptr		atomic_set_long
#define	atomic_set_acq_ptr	atomic_set_acq_long
#define	atomic_set_rel_ptr	atomic_set_rel_long
#define	atomic_clear_ptr	atomic_clear_long
#define	atomic_clear_acq_ptr	atomic_clear_acq_long
#define	atomic_clear_rel_ptr	atomic_clear_rel_long
#define	atomic_add_ptr		atomic_add_long
#define	atomic_add_acq_ptr	atomic_add_acq_long
#define	atomic_add_rel_ptr	atomic_add_rel_long
#define	atomic_subtract_ptr	atomic_subtract_long
#define	atomic_subtract_acq_ptr	atomic_subtract_acq_long
#define	atomic_subtract_rel_ptr	atomic_subtract_rel_long
#define	atomic_cmpset_ptr	atomic_cmpset_long
#define	atomic_cmpset_acq_ptr	atomic_cmpset_acq_long
#define	atomic_cmpset_rel_ptr	atomic_cmpset_rel_long
#define	atomic_fcmpset_ptr	atomic_fcmpset_long
#define	atomic_fcmpset_acq_ptr	atomic_fcmpset_acq_long
#define	atomic_fcmpset_rel_ptr	atomic_fcmpset_rel_long
#define	atomic_load_acq_ptr	atomic_load_acq_long
#define	atomic_store_rel_ptr	atomic_store_rel_long
#define	atomic_readandclear_ptr	atomic_readandclear_long

static __inline unsigned int
atomic_swap_int(volatile unsigned int *ptr, const unsigned int value)
{
	unsigned int retval;

	retval = *ptr;

	while (!atomic_fcmpset_int(ptr, &retval, value))
		;
	return (retval);
}

static __inline uint32_t
atomic_swap_32(volatile uint32_t *ptr, const uint32_t value)
{
	uint32_t retval;

	retval = *ptr;

	while (!atomic_fcmpset_32(ptr, &retval, value))
		;
	return (retval);
}

#if defined(__mips_n64) || defined(__mips_n32)
static __inline uint64_t
atomic_swap_64(volatile uint64_t *ptr, const uint64_t value)
{
	uint64_t retval;

	retval = *ptr;

	while (!atomic_fcmpset_64(ptr, &retval, value))
		;
	return (retval);
}
#endif

#ifdef __mips_n64
static __inline unsigned long
atomic_swap_long(volatile unsigned long *ptr, const unsigned long value)
{
	unsigned long retval;

	retval = *ptr;

	while (!atomic_fcmpset_64((volatile uint64_t *)ptr,
	    (uint64_t *)&retval, value))
		;
	return (retval);
}
#else
static __inline unsigned long
atomic_swap_long(volatile unsigned long *ptr, const unsigned long value)
{
	unsigned long retval;

	retval = *ptr;

	while (!atomic_fcmpset_32((volatile uint32_t *)ptr,
	    (uint32_t *)&retval, value))
		;
	return (retval);
}
#endif
#define	atomic_swap_ptr(ptr, value) atomic_swap_long((unsigned long *)(ptr), value)

#endif /* ! _MACHINE_ATOMIC_H_ */
