/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#include <sys/atomic_common.h>

#define	fence()	__asm __volatile("fence" ::: "memory");
#define	mb()	fence()
#define	rmb()	fence()
#define	wmb()	fence()

#define	ATOMIC_ACQ_REL(NAME, WIDTH)					\
static __inline  void							\
atomic_##NAME##_acq_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	atomic_##NAME##_##WIDTH(p, v);					\
	fence(); 							\
}									\
									\
static __inline  void							\
atomic_##NAME##_rel_##WIDTH(__volatile uint##WIDTH##_t *p, uint##WIDTH##_t v)\
{									\
	fence();							\
	atomic_##NAME##_##WIDTH(p, v);					\
}

static __inline void
atomic_add_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amoadd.w zero, %1, %0"
			: "+A" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_subtract_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amoadd.w zero, %1, %0"
			: "+A" (*p)
			: "r" (-val)
			: "memory");
}

static __inline void
atomic_set_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amoor.w zero, %1, %0"
			: "+A" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_clear_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile("amoand.w zero, %1, %0"
			: "+A" (*p)
			: "r" (~val)
			: "memory");
}

static __inline int
atomic_cmpset_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	uint32_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li   %1, 1\n" /* Preset to fail */
			"lr.w %0, %2\n"
			"bne  %0, %z3, 1f\n"
			"sc.w %1, %z4, %2\n"
			"bnez %1, 0b\n"
		"1:"
			: "=&r" (tmp), "=&r" (res), "+A" (*p)
			: "rJ" (cmpval), "rJ" (newval)
			: "memory");

	return (!res);
}

static __inline int
atomic_fcmpset_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	uint32_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li   %1, 1\n"		/* Preset to fail */
			"lr.w %0, %2\n"		/* Load old value */
			"bne  %0, %z4, 1f\n"	/* Compare */
			"sc.w %1, %z5, %2\n"	/* Try to store new value */
			"j 2f\n"
		"1:"
			"sw   %0, %3\n"		/* Save old value */
		"2:"
			: "=&r" (tmp), "=&r" (res), "+A" (*p), "+A" (*cmpval)
			: "rJ" (*cmpval), "rJ" (newval)
			: "memory");

	return (!res);
}

static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t ret;

	__asm __volatile("amoadd.w %0, %2, %1"
			: "=&r" (ret), "+A" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline uint32_t
atomic_readandclear_32(volatile uint32_t *p)
{
	uint32_t ret;
	uint32_t val;

	val = 0;

	__asm __volatile("amoswap.w %0, %2, %1"
			: "=&r"(ret), "+A" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

#define	atomic_add_int		atomic_add_32
#define	atomic_clear_int	atomic_clear_32
#define	atomic_cmpset_int	atomic_cmpset_32
#define	atomic_fcmpset_int	atomic_fcmpset_32
#define	atomic_fetchadd_int	atomic_fetchadd_32
#define	atomic_readandclear_int	atomic_readandclear_32
#define	atomic_set_int		atomic_set_32
#define	atomic_subtract_int	atomic_subtract_32

ATOMIC_ACQ_REL(set, 32)
ATOMIC_ACQ_REL(clear, 32)
ATOMIC_ACQ_REL(add, 32)
ATOMIC_ACQ_REL(subtract, 32)

static __inline int
atomic_cmpset_acq_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{
	int res;

	res = atomic_cmpset_32(p, cmpval, newval);

	fence();

	return (res);
}

static __inline int
atomic_cmpset_rel_32(volatile uint32_t *p, uint32_t cmpval, uint32_t newval)
{

	fence();

	return (atomic_cmpset_32(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_acq_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{
	int res;

	res = atomic_fcmpset_32(p, cmpval, newval);

	fence();

	return (res);
}

static __inline int
atomic_fcmpset_rel_32(volatile uint32_t *p, uint32_t *cmpval, uint32_t newval)
{

	fence();

	return (atomic_fcmpset_32(p, cmpval, newval));
}

static __inline uint32_t
atomic_load_acq_32(volatile uint32_t *p)
{
	uint32_t ret;

	ret = *p;

	fence();

	return (ret);
}

static __inline void
atomic_store_rel_32(volatile uint32_t *p, uint32_t val)
{

	fence();

	*p = val;
}

#define	atomic_add_acq_int	atomic_add_acq_32
#define	atomic_clear_acq_int	atomic_clear_acq_32
#define	atomic_cmpset_acq_int	atomic_cmpset_acq_32
#define	atomic_fcmpset_acq_int	atomic_fcmpset_acq_32
#define	atomic_load_acq_int	atomic_load_acq_32
#define	atomic_set_acq_int	atomic_set_acq_32
#define	atomic_subtract_acq_int	atomic_subtract_acq_32

#define	atomic_add_rel_int	atomic_add_rel_32
#define	atomic_clear_rel_int	atomic_add_rel_32
#define	atomic_cmpset_rel_int	atomic_cmpset_rel_32
#define	atomic_fcmpset_rel_int	atomic_fcmpset_rel_32
#define	atomic_set_rel_int	atomic_set_rel_32
#define	atomic_subtract_rel_int	atomic_subtract_rel_32
#define	atomic_store_rel_int	atomic_store_rel_32

static __inline void
atomic_add_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amoadd.d zero, %1, %0"
			: "+A" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_subtract_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amoadd.d zero, %1, %0"
			: "+A" (*p)
			: "r" (-val)
			: "memory");
}

static __inline void
atomic_set_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amoor.d zero, %1, %0"
			: "+A" (*p)
			: "r" (val)
			: "memory");
}

static __inline void
atomic_clear_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile("amoand.d zero, %1, %0"
			: "+A" (*p)
			: "r" (~val)
			: "memory");
}

static __inline int
atomic_cmpset_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	uint64_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li   %1, 1\n" /* Preset to fail */
			"lr.d %0, %2\n"
			"bne  %0, %z3, 1f\n"
			"sc.d %1, %z4, %2\n"
			"bnez %1, 0b\n"
		"1:"
			: "=&r" (tmp), "=&r" (res), "+A" (*p)
			: "rJ" (cmpval), "rJ" (newval)
			: "memory");

	return (!res);
}

static __inline int
atomic_fcmpset_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	uint64_t tmp;
	int res;

	res = 0;

	__asm __volatile(
		"0:"
			"li   %1, 1\n"		/* Preset to fail */
			"lr.d %0, %2\n"		/* Load old value */
			"bne  %0, %z4, 1f\n"	/* Compare */
			"sc.d %1, %z5, %2\n"	/* Try to store new value */
			"j 2f\n"
		"1:"
			"sd   %0, %3\n"		/* Save old value */
		"2:"
			: "=&r" (tmp), "=&r" (res), "+A" (*p), "+A" (*cmpval)
			: "rJ" (*cmpval), "rJ" (newval)
			: "memory");

	return (!res);
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t ret;

	__asm __volatile("amoadd.d %0, %2, %1"
			: "=&r" (ret), "+A" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline uint64_t
atomic_readandclear_64(volatile uint64_t *p)
{
	uint64_t ret;
	uint64_t val;

	val = 0;

	__asm __volatile("amoswap.d %0, %2, %1"
			: "=&r"(ret), "+A" (*p)
			: "r" (val)
			: "memory");

	return (ret);
}

static __inline uint32_t
atomic_swap_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t old;

	__asm __volatile("amoswap.w %0, %2, %1"
			: "=&r"(old), "+A" (*p)
			: "r" (val)
			: "memory");

	return (old);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t old;

	__asm __volatile("amoswap.d %0, %2, %1"
			: "=&r"(old), "+A" (*p)
			: "r" (val)
			: "memory");

	return (old);
}

#define	atomic_swap_int			atomic_swap_32

#define	atomic_add_long			atomic_add_64
#define	atomic_clear_long		atomic_clear_64
#define	atomic_cmpset_long		atomic_cmpset_64
#define	atomic_fcmpset_long		atomic_fcmpset_64
#define	atomic_fetchadd_long		atomic_fetchadd_64
#define	atomic_readandclear_long	atomic_readandclear_64
#define	atomic_set_long			atomic_set_64
#define	atomic_subtract_long		atomic_subtract_64
#define	atomic_swap_long		atomic_swap_64

#define	atomic_add_ptr			atomic_add_64
#define	atomic_clear_ptr		atomic_clear_64
#define	atomic_cmpset_ptr		atomic_cmpset_64
#define	atomic_fcmpset_ptr		atomic_fcmpset_64
#define	atomic_fetchadd_ptr		atomic_fetchadd_64
#define	atomic_readandclear_ptr		atomic_readandclear_64
#define	atomic_set_ptr			atomic_set_64
#define	atomic_subtract_ptr		atomic_subtract_64
#define	atomic_swap_ptr			atomic_swap_64

ATOMIC_ACQ_REL(set, 64)
ATOMIC_ACQ_REL(clear, 64)
ATOMIC_ACQ_REL(add, 64)
ATOMIC_ACQ_REL(subtract, 64)

static __inline int
atomic_cmpset_acq_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{
	int res;

	res = atomic_cmpset_64(p, cmpval, newval);

	fence();

	return (res);
}

static __inline int
atomic_cmpset_rel_64(volatile uint64_t *p, uint64_t cmpval, uint64_t newval)
{

	fence();

	return (atomic_cmpset_64(p, cmpval, newval));
}

static __inline int
atomic_fcmpset_acq_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{
	int res;

	res = atomic_fcmpset_64(p, cmpval, newval);

	fence();

	return (res);
}

static __inline int
atomic_fcmpset_rel_64(volatile uint64_t *p, uint64_t *cmpval, uint64_t newval)
{

	fence();

	return (atomic_fcmpset_64(p, cmpval, newval));
}

static __inline uint64_t
atomic_load_acq_64(volatile uint64_t *p)
{
	uint64_t ret;

	ret = *p;

	fence();

	return (ret);
}

static __inline void
atomic_store_rel_64(volatile uint64_t *p, uint64_t val)
{

	fence();

	*p = val;
}

#define	atomic_add_acq_long		atomic_add_acq_64
#define	atomic_clear_acq_long		atomic_add_acq_64
#define	atomic_cmpset_acq_long		atomic_cmpset_acq_64
#define	atomic_fcmpset_acq_long		atomic_fcmpset_acq_64
#define	atomic_load_acq_long		atomic_load_acq_64
#define	atomic_set_acq_long		atomic_set_acq_64
#define	atomic_subtract_acq_long	atomic_subtract_acq_64

#define	atomic_add_acq_ptr		atomic_add_acq_64
#define	atomic_clear_acq_ptr		atomic_add_acq_64
#define	atomic_cmpset_acq_ptr		atomic_cmpset_acq_64
#define	atomic_fcmpset_acq_ptr		atomic_fcmpset_acq_64
#define	atomic_load_acq_ptr		atomic_load_acq_64
#define	atomic_set_acq_ptr		atomic_set_acq_64
#define	atomic_subtract_acq_ptr		atomic_subtract_acq_64

#undef ATOMIC_ACQ_REL

static __inline void
atomic_thread_fence_acq(void)
{

	fence();
}

static __inline void
atomic_thread_fence_rel(void)
{

	fence();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	fence();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	fence();
}

#define	atomic_add_rel_long		atomic_add_rel_64
#define	atomic_clear_rel_long		atomic_clear_rel_64

#define	atomic_add_rel_long		atomic_add_rel_64
#define	atomic_clear_rel_long		atomic_clear_rel_64
#define	atomic_cmpset_rel_long		atomic_cmpset_rel_64
#define	atomic_fcmpset_rel_long		atomic_fcmpset_rel_64
#define	atomic_set_rel_long		atomic_set_rel_64
#define	atomic_subtract_rel_long	atomic_subtract_rel_64
#define	atomic_store_rel_long		atomic_store_rel_64

#define	atomic_add_rel_ptr		atomic_add_rel_64
#define	atomic_clear_rel_ptr		atomic_clear_rel_64
#define	atomic_cmpset_rel_ptr		atomic_cmpset_rel_64
#define	atomic_fcmpset_rel_ptr		atomic_fcmpset_rel_64
#define	atomic_set_rel_ptr		atomic_set_rel_64
#define	atomic_subtract_rel_ptr		atomic_subtract_rel_64
#define	atomic_store_rel_ptr		atomic_store_rel_64

#endif /* _MACHINE_ATOMIC_H_ */
