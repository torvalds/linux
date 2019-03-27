/*-
 * Copyright (c) 2013 Andrew Turner <andrew@freebsd.org>
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#include <sys/atomic_common.h>

#define	isb()		__asm __volatile("isb" : : : "memory")

/*
 * Options for DMB and DSB:
 *	oshld	Outer Shareable, load
 *	oshst	Outer Shareable, store
 *	osh	Outer Shareable, all
 *	nshld	Non-shareable, load
 *	nshst	Non-shareable, store
 *	nsh	Non-shareable, all
 *	ishld	Inner Shareable, load
 *	ishst	Inner Shareable, store
 *	ish	Inner Shareable, all
 *	ld	Full system, load
 *	st	Full system, store
 *	sy	Full system, all
 */
#define	dsb(opt)	__asm __volatile("dsb " __STRING(opt) : : : "memory")
#define	dmb(opt)	__asm __volatile("dmb " __STRING(opt) : : : "memory")

#define	mb()	dmb(sy)	/* Full system memory barrier all */
#define	wmb()	dmb(st)	/* Full system memory barrier store */
#define	rmb()	dmb(ld)	/* Full system memory barrier load */

#define	ATOMIC_OP(op, asm_op, bar, a, l)				\
static __inline void							\
atomic_##op##_##bar##32(volatile uint32_t *p, uint32_t val)		\
{									\
	uint32_t tmp;							\
	int res;							\
									\
	__asm __volatile(						\
	    "1: ld"#a"xr   %w0, [%2]      \n"				\
	    "   "#asm_op"  %w0, %w0, %w3  \n"				\
	    "   st"#l"xr   %w1, %w0, [%2] \n"				\
            "   cbnz       %w1, 1b        \n"				\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (val)					\
	    : "memory"							\
	);								\
}									\
									\
static __inline void							\
atomic_##op##_##bar##64(volatile uint64_t *p, uint64_t val)		\
{									\
	uint64_t tmp;							\
	int res;							\
									\
	__asm __volatile(						\
	    "1: ld"#a"xr   %0, [%2]      \n"				\
	    "   "#asm_op"  %0, %0, %3    \n"				\
	    "   st"#l"xr   %w1, %0, [%2] \n"				\
            "   cbnz       %w1, 1b       \n"				\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (val)					\
	    : "memory"							\
	);								\
}

#define	ATOMIC(op, asm_op)						\
    ATOMIC_OP(op, asm_op,     ,  ,  )					\
    ATOMIC_OP(op, asm_op, acq_, a,  )					\
    ATOMIC_OP(op, asm_op, rel_,  , l)					\

ATOMIC(add,      add)
ATOMIC(clear,    bic)
ATOMIC(set,      orr)
ATOMIC(subtract, sub)

#define	ATOMIC_FCMPSET(bar, a, l)					\
static __inline int							\
atomic_fcmpset_##bar##8(volatile uint8_t *p, uint8_t *cmpval,		\
    uint8_t newval)		 					\
{									\
	uint8_t tmp;							\
	uint8_t _cmpval = *cmpval;					\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1        \n"				\
	    "   ld"#a"xrb %w0, [%2]     \n"				\
	    "   cmp      %w0, %w3       \n"				\
	    "   b.ne     2f             \n"				\
	    "   st"#l"xrb %w1, %w4, [%2]\n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (_cmpval), "r" (newval)			\
	    : "cc", "memory"						\
	);								\
	*cmpval = tmp;							\
									\
	return (!res);							\
}									\
									\
static __inline int							\
atomic_fcmpset_##bar##16(volatile uint16_t *p, uint16_t *cmpval,	\
    uint16_t newval)		 					\
{									\
	uint16_t tmp;							\
	uint16_t _cmpval = *cmpval;					\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1        \n"				\
	    "   ld"#a"xh %w0, [%2]      \n"				\
	    "   cmp      %w0, %w3       \n"				\
	    "   b.ne     2f             \n"				\
	    "   st"#l"xh %w1, %w4, [%2] \n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (_cmpval), "r" (newval)			\
	    : "cc", "memory"						\
	);								\
	*cmpval = tmp;							\
									\
	return (!res);							\
}									\
									\
static __inline int							\
atomic_fcmpset_##bar##32(volatile uint32_t *p, uint32_t *cmpval,	\
    uint32_t newval)		 					\
{									\
	uint32_t tmp;							\
	uint32_t _cmpval = *cmpval;					\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1        \n"				\
	    "   ld"#a"xr %w0, [%2]      \n"				\
	    "   cmp      %w0, %w3       \n"				\
	    "   b.ne     2f             \n"				\
	    "   st"#l"xr %w1, %w4, [%2] \n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (_cmpval), "r" (newval)			\
	    : "cc", "memory"						\
	);								\
	*cmpval = tmp;							\
									\
	return (!res);							\
}									\
									\
static __inline int							\
atomic_fcmpset_##bar##64(volatile uint64_t *p, uint64_t *cmpval,	\
    uint64_t newval)							\
{									\
	uint64_t tmp;							\
	uint64_t _cmpval = *cmpval;					\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1       \n"				\
	    "   ld"#a"xr %0, [%2]      \n"				\
	    "   cmp      %0, %3        \n"				\
	    "   b.ne     2f            \n"				\
	    "   st"#l"xr %w1, %4, [%2] \n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (_cmpval), "r" (newval)			\
	    : "cc", "memory"						\
	);								\
	*cmpval = tmp;							\
									\
	return (!res);							\
}

ATOMIC_FCMPSET(    ,  , )
ATOMIC_FCMPSET(acq_, a, )
ATOMIC_FCMPSET(rel_,  ,l)

#undef ATOMIC_FCMPSET

#define	ATOMIC_CMPSET(bar, a, l)					\
static __inline int							\
atomic_cmpset_##bar##32(volatile uint32_t *p, uint32_t cmpval,		\
    uint32_t newval)							\
{									\
	uint32_t tmp;							\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1        \n"				\
	    "   ld"#a"xr %w0, [%2]      \n"				\
	    "   cmp      %w0, %w3       \n"				\
	    "   b.ne     2f             \n"				\
	    "   st"#l"xr %w1, %w4, [%2] \n"				\
            "   cbnz     %w1, 1b        \n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (cmpval), "r" (newval)			\
	    : "cc", "memory"							\
	);								\
									\
	return (!res);							\
}									\
									\
static __inline int							\
atomic_cmpset_##bar##64(volatile uint64_t *p, uint64_t cmpval,		\
    uint64_t newval)							\
{									\
	uint64_t tmp;							\
	int res;							\
									\
	__asm __volatile(						\
	    "1: mov      %w1, #1       \n"				\
	    "   ld"#a"xr %0, [%2]      \n"				\
	    "   cmp      %0, %3        \n"				\
	    "   b.ne     2f            \n"				\
	    "   st"#l"xr %w1, %4, [%2] \n"				\
            "   cbnz     %w1, 1b       \n"				\
	    "2:"							\
	    : "=&r"(tmp), "=&r"(res)					\
	    : "r" (p), "r" (cmpval), "r" (newval)			\
	    : "cc", "memory"							\
	);								\
									\
	return (!res);							\
}

ATOMIC_CMPSET(    ,  , )
ATOMIC_CMPSET(acq_, a, )
ATOMIC_CMPSET(rel_,  ,l)

static __inline uint32_t
atomic_fetchadd_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t tmp, ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%w2, [%3]      \n"
	    "   add	%w0, %w2, %w4  \n"
	    "   stxr	%w1, %w0, [%3] \n"
            "   cbnz	%w1, 1b        \n"
	    : "=&r"(tmp), "=&r"(res), "=&r"(ret)
	    : "r" (p), "r" (val)
	    : "memory"
	);

	return (ret);
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t tmp, ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%2, [%3]      \n"
	    "   add	%0, %2, %4    \n"
	    "   stxr	%w1, %0, [%3] \n"
            "   cbnz	%w1, 1b       \n"
	    : "=&r"(tmp), "=&r"(res), "=&r"(ret)
	    : "r" (p), "r" (val)
	    : "memory"
	);

	return (ret);
}

static __inline uint32_t
atomic_readandclear_32(volatile uint32_t *p)
{
	uint32_t ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%w1, [%2]      \n"
	    "   stxr	%w0, wzr, [%2] \n"
            "   cbnz	%w0, 1b        \n"
	    : "=&r"(res), "=&r"(ret)
	    : "r" (p)
	    : "memory"
	);

	return (ret);
}

static __inline uint64_t
atomic_readandclear_64(volatile uint64_t *p)
{
	uint64_t ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%1, [%2]      \n"
	    "   stxr	%w0, xzr, [%2] \n"
            "   cbnz	%w0, 1b        \n"
	    : "=&r"(res), "=&r"(ret)
	    : "r" (p)
	    : "memory"
	);

	return (ret);
}

static __inline uint32_t
atomic_swap_32(volatile uint32_t *p, uint32_t val)
{
	uint32_t ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%w0, [%2]      \n"
	    "   stxr	%w1, %w3, [%2] \n"
	    "   cbnz	%w1, 1b        \n"
	    : "=&r"(ret), "=&r"(res)
	    : "r" (p), "r" (val)
	    : "memory"
	);

	return (ret);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t val)
{
	uint64_t ret;
	int res;

	__asm __volatile(
	    "1: ldxr	%0, [%2]      \n"
	    "   stxr	%w1, %3, [%2] \n"
	    "   cbnz	%w1, 1b       \n"
	    : "=&r"(ret), "=&r"(res)
	    : "r" (p), "r" (val)
	    : "memory"
	);

	return (ret);
}

static __inline uint32_t
atomic_load_acq_32(volatile uint32_t *p)
{
	uint32_t ret;

	__asm __volatile(
	    "ldar	%w0, [%1] \n"
	    : "=&r" (ret)
	    : "r" (p)
	    : "memory");

	return (ret);
}

static __inline uint64_t
atomic_load_acq_64(volatile uint64_t *p)
{
	uint64_t ret;

	__asm __volatile(
	    "ldar	%0, [%1] \n"
	    : "=&r" (ret)
	    : "r" (p)
	    : "memory");

	return (ret);
}

static __inline void
atomic_store_rel_32(volatile uint32_t *p, uint32_t val)
{

	__asm __volatile(
	    "stlr	%w0, [%1] \n"
	    :
	    : "r" (val), "r" (p)
	    : "memory");
}

static __inline void
atomic_store_rel_64(volatile uint64_t *p, uint64_t val)
{

	__asm __volatile(
	    "stlr	%0, [%1] \n"
	    :
	    : "r" (val), "r" (p)
	    : "memory");
}


#define	atomic_add_int			atomic_add_32
#define	atomic_fcmpset_int		atomic_fcmpset_32
#define	atomic_clear_int		atomic_clear_32
#define	atomic_cmpset_int		atomic_cmpset_32
#define	atomic_fetchadd_int		atomic_fetchadd_32
#define	atomic_readandclear_int		atomic_readandclear_32
#define	atomic_set_int			atomic_set_32
#define	atomic_swap_int			atomic_swap_32
#define	atomic_subtract_int		atomic_subtract_32

#define	atomic_add_acq_int		atomic_add_acq_32
#define	atomic_fcmpset_acq_int		atomic_fcmpset_acq_32
#define	atomic_clear_acq_int		atomic_clear_acq_32
#define	atomic_cmpset_acq_int		atomic_cmpset_acq_32
#define	atomic_load_acq_int		atomic_load_acq_32
#define	atomic_set_acq_int		atomic_set_acq_32
#define	atomic_subtract_acq_int		atomic_subtract_acq_32

#define	atomic_add_rel_int		atomic_add_rel_32
#define	atomic_fcmpset_rel_int		atomic_fcmpset_rel_32
#define	atomic_clear_rel_int		atomic_clear_rel_32
#define	atomic_cmpset_rel_int		atomic_cmpset_rel_32
#define	atomic_set_rel_int		atomic_set_rel_32
#define	atomic_subtract_rel_int		atomic_subtract_rel_32
#define	atomic_store_rel_int		atomic_store_rel_32

#define	atomic_add_long			atomic_add_64
#define	atomic_fcmpset_long		atomic_fcmpset_64
#define	atomic_clear_long		atomic_clear_64
#define	atomic_cmpset_long		atomic_cmpset_64
#define	atomic_fetchadd_long		atomic_fetchadd_64
#define	atomic_readandclear_long	atomic_readandclear_64
#define	atomic_set_long			atomic_set_64
#define	atomic_swap_long		atomic_swap_64
#define	atomic_subtract_long		atomic_subtract_64

#define	atomic_add_ptr			atomic_add_64
#define	atomic_fcmpset_ptr		atomic_fcmpset_64
#define	atomic_clear_ptr		atomic_clear_64
#define	atomic_cmpset_ptr		atomic_cmpset_64
#define	atomic_fetchadd_ptr		atomic_fetchadd_64
#define	atomic_readandclear_ptr		atomic_readandclear_64
#define	atomic_set_ptr			atomic_set_64
#define	atomic_swap_ptr			atomic_swap_64
#define	atomic_subtract_ptr		atomic_subtract_64

#define	atomic_add_acq_long		atomic_add_acq_64
#define	atomic_fcmpset_acq_long		atomic_fcmpset_acq_64
#define	atomic_clear_acq_long		atomic_clear_acq_64
#define	atomic_cmpset_acq_long		atomic_cmpset_acq_64
#define	atomic_load_acq_long		atomic_load_acq_64
#define	atomic_set_acq_long		atomic_set_acq_64
#define	atomic_subtract_acq_long	atomic_subtract_acq_64

#define	atomic_add_acq_ptr		atomic_add_acq_64
#define	atomic_fcmpset_acq_ptr		atomic_fcmpset_acq_64
#define	atomic_clear_acq_ptr		atomic_clear_acq_64
#define	atomic_cmpset_acq_ptr		atomic_cmpset_acq_64
#define	atomic_load_acq_ptr		atomic_load_acq_64
#define	atomic_set_acq_ptr		atomic_set_acq_64
#define	atomic_subtract_acq_ptr		atomic_subtract_acq_64

#define	atomic_add_rel_long		atomic_add_rel_64
#define	atomic_fcmpset_rel_long		atomic_fcmpset_rel_64
#define	atomic_clear_rel_long		atomic_clear_rel_64
#define	atomic_cmpset_rel_long		atomic_cmpset_rel_64
#define	atomic_set_rel_long		atomic_set_rel_64
#define	atomic_subtract_rel_long	atomic_subtract_rel_64
#define	atomic_store_rel_long		atomic_store_rel_64

#define	atomic_add_rel_ptr		atomic_add_rel_64
#define	atomic_fcmpset_rel_ptr		atomic_fcmpset_rel_64
#define	atomic_clear_rel_ptr		atomic_clear_rel_64
#define	atomic_cmpset_rel_ptr		atomic_cmpset_rel_64
#define	atomic_set_rel_ptr		atomic_set_rel_64
#define	atomic_subtract_rel_ptr		atomic_subtract_rel_64
#define	atomic_store_rel_ptr		atomic_store_rel_64

static __inline void
atomic_thread_fence_acq(void)
{

	dmb(ld);
}

static __inline void
atomic_thread_fence_rel(void)
{

	dmb(sy);
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	dmb(sy);
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	dmb(sy);
}

#endif /* _MACHINE_ATOMIC_H_ */

