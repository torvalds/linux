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
 * $FreeBSD$
 */
#ifndef _MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif

/*
 * To express interprocessor (as opposed to processor and device) memory
 * ordering constraints, use the atomic_*() functions with acquire and release
 * semantics rather than the *mb() functions.  An architecture's memory
 * ordering (or memory consistency) model governs the order in which a
 * program's accesses to different locations may be performed by an
 * implementation of that architecture.  In general, for memory regions
 * defined as writeback cacheable, the memory ordering implemented by amd64
 * processors preserves the program ordering of a load followed by a load, a
 * load followed by a store, and a store followed by a store.  Only a store
 * followed by a load to a different memory location may be reordered.
 * Therefore, except for special cases, like non-temporal memory accesses or
 * memory regions defined as write combining, the memory ordering effects
 * provided by the sfence instruction in the wmb() function and the lfence
 * instruction in the rmb() function are redundant.  In contrast, the
 * atomic_*() functions with acquire and release semantics do not perform
 * redundant instructions for ordinary cases of interprocessor memory
 * ordering on any architecture.
 */
#define	mb()	__asm __volatile("mfence;" : : : "memory")
#define	wmb()	__asm __volatile("sfence;" : : : "memory")
#define	rmb()	__asm __volatile("lfence;" : : : "memory")

#include <sys/atomic_common.h>

/*
 * Various simple operations on memory, each of which is atomic in the
 * presence of interrupts and multiple processors.
 *
 * atomic_set_char(P, V)	(*(u_char *)(P) |= (V))
 * atomic_clear_char(P, V)	(*(u_char *)(P) &= ~(V))
 * atomic_add_char(P, V)	(*(u_char *)(P) += (V))
 * atomic_subtract_char(P, V)	(*(u_char *)(P) -= (V))
 *
 * atomic_set_short(P, V)	(*(u_short *)(P) |= (V))
 * atomic_clear_short(P, V)	(*(u_short *)(P) &= ~(V))
 * atomic_add_short(P, V)	(*(u_short *)(P) += (V))
 * atomic_subtract_short(P, V)	(*(u_short *)(P) -= (V))
 *
 * atomic_set_int(P, V)		(*(u_int *)(P) |= (V))
 * atomic_clear_int(P, V)	(*(u_int *)(P) &= ~(V))
 * atomic_add_int(P, V)		(*(u_int *)(P) += (V))
 * atomic_subtract_int(P, V)	(*(u_int *)(P) -= (V))
 * atomic_swap_int(P, V)	(return (*(u_int *)(P)); *(u_int *)(P) = (V);)
 * atomic_readandclear_int(P)	(return (*(u_int *)(P)); *(u_int *)(P) = 0;)
 *
 * atomic_set_long(P, V)	(*(u_long *)(P) |= (V))
 * atomic_clear_long(P, V)	(*(u_long *)(P) &= ~(V))
 * atomic_add_long(P, V)	(*(u_long *)(P) += (V))
 * atomic_subtract_long(P, V)	(*(u_long *)(P) -= (V))
 * atomic_swap_long(P, V)	(return (*(u_long *)(P)); *(u_long *)(P) = (V);)
 * atomic_readandclear_long(P)	(return (*(u_long *)(P)); *(u_long *)(P) = 0;)
 */

/*
 * The above functions are expanded inline in the statically-linked
 * kernel.  Lock prefixes are generated if an SMP kernel is being
 * built.
 *
 * Kernel modules call real functions which are built into the kernel.
 * This allows kernel modules to be portable between UP and SMP systems.
 */
#if !defined(__GNUCLIKE_ASM)
#define	ATOMIC_ASM(NAME, TYPE, OP, CONS, V)			\
void atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v);	\
void atomic_##NAME##_barr_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

int	atomic_cmpset_char(volatile u_char *dst, u_char expect, u_char src);
int	atomic_cmpset_short(volatile u_short *dst, u_short expect, u_short src);
int	atomic_cmpset_int(volatile u_int *dst, u_int expect, u_int src);
int	atomic_cmpset_long(volatile u_long *dst, u_long expect, u_long src);
int	atomic_fcmpset_char(volatile u_char *dst, u_char *expect, u_char src);
int	atomic_fcmpset_short(volatile u_short *dst, u_short *expect,
	    u_short src);
int	atomic_fcmpset_int(volatile u_int *dst, u_int *expect, u_int src);
int	atomic_fcmpset_long(volatile u_long *dst, u_long *expect, u_long src);
u_int	atomic_fetchadd_int(volatile u_int *p, u_int v);
u_long	atomic_fetchadd_long(volatile u_long *p, u_long v);
int	atomic_testandset_int(volatile u_int *p, u_int v);
int	atomic_testandset_long(volatile u_long *p, u_int v);
int	atomic_testandclear_int(volatile u_int *p, u_int v);
int	atomic_testandclear_long(volatile u_long *p, u_int v);
void	atomic_thread_fence_acq(void);
void	atomic_thread_fence_acq_rel(void);
void	atomic_thread_fence_rel(void);
void	atomic_thread_fence_seq_cst(void);

#define	ATOMIC_LOAD(TYPE)					\
u_##TYPE	atomic_load_acq_##TYPE(volatile u_##TYPE *p)
#define	ATOMIC_STORE(TYPE)					\
void		atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

#else /* !KLD_MODULE && __GNUCLIKE_ASM */

/*
 * For userland, always use lock prefixes so that the binaries will run
 * on both SMP and !SMP systems.
 */
#if defined(SMP) || !defined(_KERNEL) || defined(KLD_MODULE)
#define	MPLOCKED	"lock ; "
#else
#define	MPLOCKED
#endif

/*
 * The assembly is volatilized to avoid code chunk removal by the compiler.
 * GCC aggressively reorders operations and memory clobbering is necessary
 * in order to avoid that for memory barriers.
 */
#define	ATOMIC_ASM(NAME, TYPE, OP, CONS, V)		\
static __inline void					\
atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(MPLOCKED OP			\
	: "+m" (*p)					\
	: CONS (V)					\
	: "cc");					\
}							\
							\
static __inline void					\
atomic_##NAME##_barr_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(MPLOCKED OP			\
	: "+m" (*p)					\
	: CONS (V)					\
	: "memory", "cc");				\
}							\
struct __hack

/*
 * Atomic compare and set, used by the mutex functions.
 *
 * cmpset:
 *	if (*dst == expect)
 *		*dst = src
 *
 * fcmpset:
 *	if (*dst == *expect)
 *		*dst = src
 *	else
 *		*expect = *dst
 *
 * Returns 0 on failure, non-zero on success.
 */
#define	ATOMIC_CMPSET(TYPE)				\
static __inline int					\
atomic_cmpset_##TYPE(volatile u_##TYPE *dst, u_##TYPE expect, u_##TYPE src) \
{							\
	u_char res;					\
							\
	__asm __volatile(				\
	"	" MPLOCKED "		"		\
	"	cmpxchg %3,%1 ;	"			\
	"	sete	%0 ;		"		\
	"# atomic_cmpset_" #TYPE "	"		\
	: "=q" (res),			/* 0 */		\
	  "+m" (*dst),			/* 1 */		\
	  "+a" (expect)			/* 2 */		\
	: "r" (src)			/* 3 */		\
	: "memory", "cc");				\
	return (res);					\
}							\
							\
static __inline int					\
atomic_fcmpset_##TYPE(volatile u_##TYPE *dst, u_##TYPE *expect, u_##TYPE src) \
{							\
	u_char res;					\
							\
	__asm __volatile(				\
	"	" MPLOCKED "		"		\
	"	cmpxchg %3,%1 ;		"		\
	"	sete	%0 ;		"		\
	"# atomic_fcmpset_" #TYPE "	"		\
	: "=q" (res),			/* 0 */		\
	  "+m" (*dst),			/* 1 */		\
	  "+a" (*expect)		/* 2 */		\
	: "r" (src)			/* 3 */		\
	: "memory", "cc");				\
	return (res);					\
}

ATOMIC_CMPSET(char);
ATOMIC_CMPSET(short);
ATOMIC_CMPSET(int);
ATOMIC_CMPSET(long);

/*
 * Atomically add the value of v to the integer pointed to by p and return
 * the previous value of *p.
 */
static __inline u_int
atomic_fetchadd_int(volatile u_int *p, u_int v)
{

	__asm __volatile(
	"	" MPLOCKED "		"
	"	xaddl	%0,%1 ;		"
	"# atomic_fetchadd_int"
	: "+r" (v),			/* 0 */
	  "+m" (*p)			/* 1 */
	: : "cc");
	return (v);
}

/*
 * Atomically add the value of v to the long integer pointed to by p and return
 * the previous value of *p.
 */
static __inline u_long
atomic_fetchadd_long(volatile u_long *p, u_long v)
{

	__asm __volatile(
	"	" MPLOCKED "		"
	"	xaddq	%0,%1 ;		"
	"# atomic_fetchadd_long"
	: "+r" (v),			/* 0 */
	  "+m" (*p)			/* 1 */
	: : "cc");
	return (v);
}

static __inline int
atomic_testandset_int(volatile u_int *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btsl	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandset_int"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x1f)		/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandset_long(volatile u_long *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btsq	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandset_long"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Jr" ((u_long)(v & 0x3f))	/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandclear_int(volatile u_int *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btrl	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandclear_int"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x1f)		/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandclear_long(volatile u_long *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btrq	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandclear_long"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Jr" ((u_long)(v & 0x3f))	/* 2 */
	: "cc");
	return (res);
}

/*
 * We assume that a = b will do atomic loads and stores.  Due to the
 * IA32 memory model, a simple store guarantees release semantics.
 *
 * However, a load may pass a store if they are performed on distinct
 * addresses, so we need a Store/Load barrier for sequentially
 * consistent fences in SMP kernels.  We use "lock addl $0,mem" for a
 * Store/Load barrier, as recommended by the AMD Software Optimization
 * Guide, and not mfence.  To avoid false data dependencies, we use a
 * special address for "mem".  In the kernel, we use a private per-cpu
 * cache line.  In user space, we use a word in the stack's red zone
 * (-8(%rsp)).
 *
 * For UP kernels, however, the memory of the single processor is
 * always consistent, so we only need to stop the compiler from
 * reordering accesses in a way that violates the semantics of acquire
 * and release.
 */

#if defined(_KERNEL)

/*
 * OFFSETOF_MONITORBUF == __pcpu_offset(pc_monitorbuf).
 *
 * The open-coded number is used instead of the symbolic expression to
 * avoid a dependency on sys/pcpu.h in machine/atomic.h consumers.
 * An assertion in amd64/vm_machdep.c ensures that the value is correct.
 */
#define	OFFSETOF_MONITORBUF	0x100

#if defined(SMP) || defined(KLD_MODULE)
static __inline void
__storeload_barrier(void)
{

	__asm __volatile("lock; addl $0,%%gs:%0"
	    : "+m" (*(u_int *)OFFSETOF_MONITORBUF) : : "memory", "cc");
}
#else /* _KERNEL && UP */
static __inline void
__storeload_barrier(void)
{

	__compiler_membar();
}
#endif /* SMP */
#else /* !_KERNEL */
static __inline void
__storeload_barrier(void)
{

	__asm __volatile("lock; addl $0,-8(%%rsp)" : : : "memory", "cc");
}
#endif /* _KERNEL*/

#define	ATOMIC_LOAD(TYPE)					\
static __inline u_##TYPE					\
atomic_load_acq_##TYPE(volatile u_##TYPE *p)			\
{								\
	u_##TYPE res;						\
								\
	res = *p;						\
	__compiler_membar();					\
	return (res);						\
}								\
struct __hack

#define	ATOMIC_STORE(TYPE)					\
static __inline void						\
atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)	\
{								\
								\
	__compiler_membar();					\
	*p = v;							\
}								\
struct __hack

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

	__storeload_barrier();
}

#endif /* KLD_MODULE || !__GNUCLIKE_ASM */

ATOMIC_ASM(set,	     char,  "orb %b1,%0",  "iq",  v);
ATOMIC_ASM(clear,    char,  "andb %b1,%0", "iq", ~v);
ATOMIC_ASM(add,	     char,  "addb %b1,%0", "iq",  v);
ATOMIC_ASM(subtract, char,  "subb %b1,%0", "iq",  v);

ATOMIC_ASM(set,	     short, "orw %w1,%0",  "ir",  v);
ATOMIC_ASM(clear,    short, "andw %w1,%0", "ir", ~v);
ATOMIC_ASM(add,	     short, "addw %w1,%0", "ir",  v);
ATOMIC_ASM(subtract, short, "subw %w1,%0", "ir",  v);

ATOMIC_ASM(set,	     int,   "orl %1,%0",   "ir",  v);
ATOMIC_ASM(clear,    int,   "andl %1,%0",  "ir", ~v);
ATOMIC_ASM(add,	     int,   "addl %1,%0",  "ir",  v);
ATOMIC_ASM(subtract, int,   "subl %1,%0",  "ir",  v);

ATOMIC_ASM(set,	     long,  "orq %1,%0",   "er",  v);
ATOMIC_ASM(clear,    long,  "andq %1,%0",  "er", ~v);
ATOMIC_ASM(add,	     long,  "addq %1,%0",  "er",  v);
ATOMIC_ASM(subtract, long,  "subq %1,%0",  "er",  v);

#define	ATOMIC_LOADSTORE(TYPE)					\
	ATOMIC_LOAD(TYPE);					\
	ATOMIC_STORE(TYPE)

ATOMIC_LOADSTORE(char);
ATOMIC_LOADSTORE(short);
ATOMIC_LOADSTORE(int);
ATOMIC_LOADSTORE(long);

#undef ATOMIC_ASM
#undef ATOMIC_LOAD
#undef ATOMIC_STORE
#undef ATOMIC_LOADSTORE
#ifndef WANT_FUNCTIONS

/* Read the current value and store a new value in the destination. */
#ifdef __GNUCLIKE_ASM

static __inline u_int
atomic_swap_int(volatile u_int *p, u_int v)
{

	__asm __volatile(
	"	xchgl	%1,%0 ;		"
	"# atomic_swap_int"
	: "+r" (v),			/* 0 */
	  "+m" (*p));			/* 1 */
	return (v);
}

static __inline u_long
atomic_swap_long(volatile u_long *p, u_long v)
{

	__asm __volatile(
	"	xchgq	%1,%0 ;		"
	"# atomic_swap_long"
	: "+r" (v),			/* 0 */
	  "+m" (*p));			/* 1 */
	return (v);
}

#else /* !__GNUCLIKE_ASM */

u_int	atomic_swap_int(volatile u_int *p, u_int v);
u_long	atomic_swap_long(volatile u_long *p, u_long v);

#endif /* __GNUCLIKE_ASM */

#define	atomic_set_acq_char		atomic_set_barr_char
#define	atomic_set_rel_char		atomic_set_barr_char
#define	atomic_clear_acq_char		atomic_clear_barr_char
#define	atomic_clear_rel_char		atomic_clear_barr_char
#define	atomic_add_acq_char		atomic_add_barr_char
#define	atomic_add_rel_char		atomic_add_barr_char
#define	atomic_subtract_acq_char	atomic_subtract_barr_char
#define	atomic_subtract_rel_char	atomic_subtract_barr_char
#define	atomic_cmpset_acq_char		atomic_cmpset_char
#define	atomic_cmpset_rel_char		atomic_cmpset_char
#define	atomic_fcmpset_acq_char		atomic_fcmpset_char
#define	atomic_fcmpset_rel_char		atomic_fcmpset_char

#define	atomic_set_acq_short		atomic_set_barr_short
#define	atomic_set_rel_short		atomic_set_barr_short
#define	atomic_clear_acq_short		atomic_clear_barr_short
#define	atomic_clear_rel_short		atomic_clear_barr_short
#define	atomic_add_acq_short		atomic_add_barr_short
#define	atomic_add_rel_short		atomic_add_barr_short
#define	atomic_subtract_acq_short	atomic_subtract_barr_short
#define	atomic_subtract_rel_short	atomic_subtract_barr_short
#define	atomic_cmpset_acq_short		atomic_cmpset_short
#define	atomic_cmpset_rel_short		atomic_cmpset_short
#define	atomic_fcmpset_acq_short	atomic_fcmpset_short
#define	atomic_fcmpset_rel_short	atomic_fcmpset_short

#define	atomic_set_acq_int		atomic_set_barr_int
#define	atomic_set_rel_int		atomic_set_barr_int
#define	atomic_clear_acq_int		atomic_clear_barr_int
#define	atomic_clear_rel_int		atomic_clear_barr_int
#define	atomic_add_acq_int		atomic_add_barr_int
#define	atomic_add_rel_int		atomic_add_barr_int
#define	atomic_subtract_acq_int		atomic_subtract_barr_int
#define	atomic_subtract_rel_int		atomic_subtract_barr_int
#define	atomic_cmpset_acq_int		atomic_cmpset_int
#define	atomic_cmpset_rel_int		atomic_cmpset_int
#define	atomic_fcmpset_acq_int		atomic_fcmpset_int
#define	atomic_fcmpset_rel_int		atomic_fcmpset_int

#define	atomic_set_acq_long		atomic_set_barr_long
#define	atomic_set_rel_long		atomic_set_barr_long
#define	atomic_clear_acq_long		atomic_clear_barr_long
#define	atomic_clear_rel_long		atomic_clear_barr_long
#define	atomic_add_acq_long		atomic_add_barr_long
#define	atomic_add_rel_long		atomic_add_barr_long
#define	atomic_subtract_acq_long	atomic_subtract_barr_long
#define	atomic_subtract_rel_long	atomic_subtract_barr_long
#define	atomic_cmpset_acq_long		atomic_cmpset_long
#define	atomic_cmpset_rel_long		atomic_cmpset_long
#define	atomic_fcmpset_acq_long		atomic_fcmpset_long
#define	atomic_fcmpset_rel_long		atomic_fcmpset_long

#define	atomic_readandclear_int(p)	atomic_swap_int(p, 0)
#define	atomic_readandclear_long(p)	atomic_swap_long(p, 0)

/* Operations on 8-bit bytes. */
#define	atomic_set_8		atomic_set_char
#define	atomic_set_acq_8	atomic_set_acq_char
#define	atomic_set_rel_8	atomic_set_rel_char
#define	atomic_clear_8		atomic_clear_char
#define	atomic_clear_acq_8	atomic_clear_acq_char
#define	atomic_clear_rel_8	atomic_clear_rel_char
#define	atomic_add_8		atomic_add_char
#define	atomic_add_acq_8	atomic_add_acq_char
#define	atomic_add_rel_8	atomic_add_rel_char
#define	atomic_subtract_8	atomic_subtract_char
#define	atomic_subtract_acq_8	atomic_subtract_acq_char
#define	atomic_subtract_rel_8	atomic_subtract_rel_char
#define	atomic_load_acq_8	atomic_load_acq_char
#define	atomic_store_rel_8	atomic_store_rel_char
#define	atomic_cmpset_8		atomic_cmpset_char
#define	atomic_cmpset_acq_8	atomic_cmpset_acq_char
#define	atomic_cmpset_rel_8	atomic_cmpset_rel_char
#define	atomic_fcmpset_8	atomic_fcmpset_char
#define	atomic_fcmpset_acq_8	atomic_fcmpset_acq_char
#define	atomic_fcmpset_rel_8	atomic_fcmpset_rel_char

/* Operations on 16-bit words. */
#define	atomic_set_16		atomic_set_short
#define	atomic_set_acq_16	atomic_set_acq_short
#define	atomic_set_rel_16	atomic_set_rel_short
#define	atomic_clear_16		atomic_clear_short
#define	atomic_clear_acq_16	atomic_clear_acq_short
#define	atomic_clear_rel_16	atomic_clear_rel_short
#define	atomic_add_16		atomic_add_short
#define	atomic_add_acq_16	atomic_add_acq_short
#define	atomic_add_rel_16	atomic_add_rel_short
#define	atomic_subtract_16	atomic_subtract_short
#define	atomic_subtract_acq_16	atomic_subtract_acq_short
#define	atomic_subtract_rel_16	atomic_subtract_rel_short
#define	atomic_load_acq_16	atomic_load_acq_short
#define	atomic_store_rel_16	atomic_store_rel_short
#define	atomic_cmpset_16	atomic_cmpset_short
#define	atomic_cmpset_acq_16	atomic_cmpset_acq_short
#define	atomic_cmpset_rel_16	atomic_cmpset_rel_short
#define	atomic_fcmpset_16	atomic_fcmpset_short
#define	atomic_fcmpset_acq_16	atomic_fcmpset_acq_short
#define	atomic_fcmpset_rel_16	atomic_fcmpset_rel_short

/* Operations on 32-bit double words. */
#define	atomic_set_32		atomic_set_int
#define	atomic_set_acq_32	atomic_set_acq_int
#define	atomic_set_rel_32	atomic_set_rel_int
#define	atomic_clear_32		atomic_clear_int
#define	atomic_clear_acq_32	atomic_clear_acq_int
#define	atomic_clear_rel_32	atomic_clear_rel_int
#define	atomic_add_32		atomic_add_int
#define	atomic_add_acq_32	atomic_add_acq_int
#define	atomic_add_rel_32	atomic_add_rel_int
#define	atomic_subtract_32	atomic_subtract_int
#define	atomic_subtract_acq_32	atomic_subtract_acq_int
#define	atomic_subtract_rel_32	atomic_subtract_rel_int
#define	atomic_load_acq_32	atomic_load_acq_int
#define	atomic_store_rel_32	atomic_store_rel_int
#define	atomic_cmpset_32	atomic_cmpset_int
#define	atomic_cmpset_acq_32	atomic_cmpset_acq_int
#define	atomic_cmpset_rel_32	atomic_cmpset_rel_int
#define	atomic_fcmpset_32	atomic_fcmpset_int
#define	atomic_fcmpset_acq_32	atomic_fcmpset_acq_int
#define	atomic_fcmpset_rel_32	atomic_fcmpset_rel_int
#define	atomic_swap_32		atomic_swap_int
#define	atomic_readandclear_32	atomic_readandclear_int
#define	atomic_fetchadd_32	atomic_fetchadd_int
#define	atomic_testandset_32	atomic_testandset_int
#define	atomic_testandclear_32	atomic_testandclear_int

/* Operations on 64-bit quad words. */
#define	atomic_set_64		atomic_set_long
#define	atomic_set_acq_64	atomic_set_acq_long
#define	atomic_set_rel_64	atomic_set_rel_long
#define	atomic_clear_64		atomic_clear_long
#define	atomic_clear_acq_64	atomic_clear_acq_long
#define	atomic_clear_rel_64	atomic_clear_rel_long
#define	atomic_add_64		atomic_add_long
#define	atomic_add_acq_64	atomic_add_acq_long
#define	atomic_add_rel_64	atomic_add_rel_long
#define	atomic_subtract_64	atomic_subtract_long
#define	atomic_subtract_acq_64	atomic_subtract_acq_long
#define	atomic_subtract_rel_64	atomic_subtract_rel_long
#define	atomic_load_acq_64	atomic_load_acq_long
#define	atomic_store_rel_64	atomic_store_rel_long
#define	atomic_cmpset_64	atomic_cmpset_long
#define	atomic_cmpset_acq_64	atomic_cmpset_acq_long
#define	atomic_cmpset_rel_64	atomic_cmpset_rel_long
#define	atomic_fcmpset_64	atomic_fcmpset_long
#define	atomic_fcmpset_acq_64	atomic_fcmpset_acq_long
#define	atomic_fcmpset_rel_64	atomic_fcmpset_rel_long
#define	atomic_swap_64		atomic_swap_long
#define	atomic_readandclear_64	atomic_readandclear_long
#define	atomic_fetchadd_64	atomic_fetchadd_long
#define	atomic_testandset_64	atomic_testandset_long
#define	atomic_testandclear_64	atomic_testandclear_long

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
#define	atomic_load_acq_ptr	atomic_load_acq_long
#define	atomic_store_rel_ptr	atomic_store_rel_long
#define	atomic_cmpset_ptr	atomic_cmpset_long
#define	atomic_cmpset_acq_ptr	atomic_cmpset_acq_long
#define	atomic_cmpset_rel_ptr	atomic_cmpset_rel_long
#define	atomic_fcmpset_ptr	atomic_fcmpset_long
#define	atomic_fcmpset_acq_ptr	atomic_fcmpset_acq_long
#define	atomic_fcmpset_rel_ptr	atomic_fcmpset_rel_long
#define	atomic_swap_ptr		atomic_swap_long
#define	atomic_readandclear_ptr	atomic_readandclear_long

#endif /* !WANT_FUNCTIONS */

#endif /* !_MACHINE_ATOMIC_H_ */
