/* atomic.h: atomic operation emulation for FR-V
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/fujitsu/frv/atomic-ops.txt
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/config.h>
#include <linux/types.h>
#include <asm/spr-regs.h>

#ifdef CONFIG_SMP
#error not SMP safe
#endif

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * We do not have SMP systems, so we don't have to deal with that.
 */

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

typedef struct {
	int counter;
} atomic_t;

#define ATOMIC_INIT(i)		{ (i) }
#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = (i))

#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS
static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long val;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	add%I2		%1,%2,%1		\n"
	    "	cst.p		%1,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    : "+U"(v->counter), "=&r"(val)
	    : "NPr"(i)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	return val;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long val;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	sub%I2		%1,%2,%1		\n"
	    "	cst.p		%1,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    : "+U"(v->counter), "=&r"(val)
	    : "NPr"(i)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	return val;
}

#else

extern int atomic_add_return(int i, atomic_t *v);
extern int atomic_sub_return(int i, atomic_t *v);

#endif

static inline int atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
}

static inline void atomic_add(int i, atomic_t *v)
{
	atomic_add_return(i, v);
}

static inline void atomic_sub(int i, atomic_t *v)
{
	atomic_sub_return(i, v);
}

static inline void atomic_inc(atomic_t *v)
{
	atomic_add_return(1, v);
}

static inline void atomic_dec(atomic_t *v)
{
	atomic_sub_return(1, v);
}

#define atomic_dec_return(v)		atomic_sub_return(1, (v))
#define atomic_inc_return(v)		atomic_add_return(1, (v))

#define atomic_sub_and_test(i,v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_sub_return(1, (v)) == 0)
#define atomic_inc_and_test(v)		(atomic_add_return(1, (v)) == 0)

#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS
static inline
unsigned long atomic_test_and_ANDNOT_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	and%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(~mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

static inline
unsigned long atomic_test_and_OR_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	or%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

static inline
unsigned long atomic_test_and_XOR_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	xor%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

#else

extern unsigned long atomic_test_and_ANDNOT_mask(unsigned long mask, volatile unsigned long *v);
extern unsigned long atomic_test_and_OR_mask(unsigned long mask, volatile unsigned long *v);
extern unsigned long atomic_test_and_XOR_mask(unsigned long mask, volatile unsigned long *v);

#endif

#define atomic_clear_mask(mask, v)	atomic_test_and_ANDNOT_mask((mask), (v))
#define atomic_set_mask(mask, v)	atomic_test_and_OR_mask((mask), (v))

/*****************************************************************************/
/*
 * exchange value with memory
 */
#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS

#define xchg(ptr, x)								\
({										\
	__typeof__(ptr) __xg_ptr = (ptr);					\
	__typeof__(*(ptr)) __xg_orig;						\
										\
	switch (sizeof(__xg_orig)) {						\
	case 4:									\
		asm volatile(							\
			"swap%I0 %2,%M0"					\
			: "+m"(*__xg_ptr), "=&r"(__xg_orig)			\
			: "r"(x)						\
			: "memory"						\
			);							\
		break;								\
										\
	default:								\
		__xg_orig = 0;							\
		asm volatile("break");						\
		break;								\
	}									\
										\
	__xg_orig;								\
})

#else

extern uint32_t __xchg_32(uint32_t i, volatile void *v);

#define xchg(ptr, x)										\
({												\
	__typeof__(ptr) __xg_ptr = (ptr);							\
	__typeof__(*(ptr)) __xg_orig;								\
												\
	switch (sizeof(__xg_orig)) {								\
	case 4: __xg_orig = (__typeof__(*(ptr))) __xchg_32((uint32_t) x, __xg_ptr);	break;	\
	default:										\
		__xg_orig = 0;									\
		asm volatile("break");								\
		break;										\
	}											\
	__xg_orig;										\
})

#endif

#define tas(ptr) (xchg((ptr), 1))

/*****************************************************************************/
/*
 * compare and conditionally exchange value with memory
 * - if (*ptr == test) then orig = *ptr; *ptr = test;
 * - if (*ptr != test) then orig = *ptr;
 */
#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS

#define cmpxchg(ptr, test, new)							\
({										\
	__typeof__(ptr) __xg_ptr = (ptr);					\
	__typeof__(*(ptr)) __xg_orig, __xg_tmp;					\
	__typeof__(*(ptr)) __xg_test = (test);					\
	__typeof__(*(ptr)) __xg_new = (new);					\
										\
	switch (sizeof(__xg_orig)) {						\
	case 4:									\
		asm volatile(							\
			"0:						\n"	\
			"	orcc		gr0,gr0,gr0,icc3	\n"	\
			"	ckeq		icc3,cc7		\n"	\
			"	ld.p		%M0,%1			\n"	\
			"	orcr		cc7,cc7,cc3		\n"	\
			"	sub%I4cc	%1,%4,%2,icc0		\n"	\
			"	bne		icc0,#0,1f		\n"	\
			"	cst.p		%3,%M0		,cc3,#1	\n"	\
			"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	\
			"	beq		icc3,#0,0b		\n"	\
			"1:						\n"	\
			: "+U"(*__xg_ptr), "=&r"(__xg_orig), "=&r"(__xg_tmp)	\
			: "r"(__xg_new), "NPr"(__xg_test)			\
			: "memory", "cc7", "cc3", "icc3", "icc0"		\
			);							\
		break;								\
										\
	default:								\
		__xg_orig = 0;							\
		asm volatile("break");						\
		break;								\
	}									\
										\
	__xg_orig;								\
})

#else

extern uint32_t __cmpxchg_32(uint32_t *v, uint32_t test, uint32_t new);

#define cmpxchg(ptr, test, new)							\
({										\
	__typeof__(ptr) __xg_ptr = (ptr);					\
	__typeof__(*(ptr)) __xg_orig;						\
	__typeof__(*(ptr)) __xg_test = (test);					\
	__typeof__(*(ptr)) __xg_new = (new);					\
										\
	switch (sizeof(__xg_orig)) {						\
	case 4: __xg_orig = __cmpxchg_32(__xg_ptr, __xg_test, __xg_new); break;	\
	default:								\
		__xg_orig = 0;							\
		asm volatile("break");						\
		break;								\
	}									\
										\
	__xg_orig;								\
})

#endif

#define atomic_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#define atomic_add_unless(v, a, u)				\
({								\
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c != (u);						\
})

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#include <asm-generic/atomic.h>
#endif /* _ASM_ATOMIC_H */
