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

#include <sys/atomic_common.h>

#ifdef _KERNEL
#include <machine/md_var.h>
#include <machine/specialreg.h>
#endif

#ifndef __OFFSETOF_MONITORBUF
/*
 * __OFFSETOF_MONITORBUF == __pcpu_offset(pc_monitorbuf).
 *
 * The open-coded number is used instead of the symbolic expression to
 * avoid a dependency on sys/pcpu.h in machine/atomic.h consumers.
 * An assertion in i386/vm_machdep.c ensures that the value is correct.
 */
#define	__OFFSETOF_MONITORBUF	0x80

static __inline void
__mbk(void)
{

	__asm __volatile("lock; addl $0,%%fs:%0"
	    : "+m" (*(u_int *)__OFFSETOF_MONITORBUF) : : "memory", "cc");
}

static __inline void
__mbu(void)
{

	__asm __volatile("lock; addl $0,(%%esp)" : : : "memory", "cc");
}
#endif

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
int	atomic_fcmpset_char(volatile u_char *dst, u_char *expect, u_char src);
int	atomic_fcmpset_short(volatile u_short *dst, u_short *expect,
	    u_short src);
int	atomic_fcmpset_int(volatile u_int *dst, u_int *expect, u_int src);
u_int	atomic_fetchadd_int(volatile u_int *p, u_int v);
int	atomic_testandset_int(volatile u_int *p, u_int v);
int	atomic_testandclear_int(volatile u_int *p, u_int v);
void	atomic_thread_fence_acq(void);
void	atomic_thread_fence_acq_rel(void);
void	atomic_thread_fence_rel(void);
void	atomic_thread_fence_seq_cst(void);

#define	ATOMIC_LOAD(TYPE)					\
u_##TYPE	atomic_load_acq_##TYPE(volatile u_##TYPE *p)
#define	ATOMIC_STORE(TYPE)					\
void		atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

int		atomic_cmpset_64(volatile uint64_t *, uint64_t, uint64_t);
int		atomic_fcmpset_64(volatile uint64_t *, uint64_t *, uint64_t);
uint64_t	atomic_load_acq_64(volatile uint64_t *);
void		atomic_store_rel_64(volatile uint64_t *, uint64_t);
uint64_t	atomic_swap_64(volatile uint64_t *, uint64_t);
uint64_t	atomic_fetchadd_64(volatile uint64_t *, uint64_t);
void		atomic_add_64(volatile uint64_t *, uint64_t);
void		atomic_subtract_64(volatile uint64_t *, uint64_t);

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
#define	ATOMIC_CMPSET(TYPE, CONS)			\
static __inline int					\
atomic_cmpset_##TYPE(volatile u_##TYPE *dst, u_##TYPE expect, u_##TYPE src) \
{							\
	u_char res;					\
							\
	__asm __volatile(				\
	"	" MPLOCKED "		"		\
	"	cmpxchg	%3,%1 ;		"		\
	"	sete	%0 ;		"		\
	"# atomic_cmpset_" #TYPE "	"		\
	: "=q" (res),			/* 0 */		\
	  "+m" (*dst),			/* 1 */		\
	  "+a" (expect)			/* 2 */		\
	: CONS (src)			/* 3 */		\
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
	"	cmpxchg	%3,%1 ;		"		\
	"	sete	%0 ;		"		\
	"# atomic_fcmpset_" #TYPE "	"		\
	: "=q" (res),			/* 0 */		\
	  "+m" (*dst),			/* 1 */		\
	  "+a" (*expect)		/* 2 */		\
	: CONS (src)			/* 3 */		\
	: "memory", "cc");				\
	return (res);					\
}

ATOMIC_CMPSET(char, "q");
ATOMIC_CMPSET(short, "r");
ATOMIC_CMPSET(int, "r");

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

/*
 * We assume that a = b will do atomic loads and stores.  Due to the
 * IA32 memory model, a simple store guarantees release semantics.
 *
 * However, a load may pass a store if they are performed on distinct
 * addresses, so we need Store/Load barrier for sequentially
 * consistent fences in SMP kernels.  We use "lock addl $0,mem" for a
 * Store/Load barrier, as recommended by the AMD Software Optimization
 * Guide, and not mfence.  In the kernel, we use a private per-cpu
 * cache line for "mem", to avoid introducing false data
 * dependencies.  In user space, we use the word at the top of the
 * stack.
 *
 * For UP kernels, however, the memory of the single processor is
 * always consistent, so we only need to stop the compiler from
 * reordering accesses in a way that violates the semantics of acquire
 * and release.
 */

#if defined(_KERNEL)
#if defined(SMP) || defined(KLD_MODULE)
#define	__storeload_barrier()	__mbk()
#else /* _KERNEL && UP */
#define	__storeload_barrier()	__compiler_membar()
#endif /* SMP */
#else /* !_KERNEL */
#define	__storeload_barrier()	__mbu()
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

#ifdef _KERNEL

#ifdef WANT_FUNCTIONS
int		atomic_cmpset_64_i386(volatile uint64_t *, uint64_t, uint64_t);
int		atomic_cmpset_64_i586(volatile uint64_t *, uint64_t, uint64_t);
uint64_t	atomic_load_acq_64_i386(volatile uint64_t *);
uint64_t	atomic_load_acq_64_i586(volatile uint64_t *);
void		atomic_store_rel_64_i386(volatile uint64_t *, uint64_t);
void		atomic_store_rel_64_i586(volatile uint64_t *, uint64_t);
uint64_t	atomic_swap_64_i386(volatile uint64_t *, uint64_t);
uint64_t	atomic_swap_64_i586(volatile uint64_t *, uint64_t);
#endif

/* I486 does not support SMP or CMPXCHG8B. */
static __inline int
atomic_cmpset_64_i386(volatile uint64_t *dst, uint64_t expect, uint64_t src)
{
	volatile uint32_t *p;
	u_char res;

	p = (volatile uint32_t *)dst;
	__asm __volatile(
	"	pushfl ;		"
	"	cli ;			"
	"	xorl	%1,%%eax ;	"
	"	xorl	%2,%%edx ;	"
	"	orl	%%edx,%%eax ;	"
	"	jne	1f ;		"
	"	movl	%4,%1 ;		"
	"	movl	%5,%2 ;		"
	"1:				"
	"	sete	%3 ;		"
	"	popfl"
	: "+A" (expect),		/* 0 */
	  "+m" (*p),			/* 1 */
	  "+m" (*(p + 1)),		/* 2 */
	  "=q" (res)			/* 3 */
	: "r" ((uint32_t)src),		/* 4 */
	  "r" ((uint32_t)(src >> 32))	/* 5 */
	: "memory", "cc");
	return (res);
}

static __inline int
atomic_fcmpset_64_i386(volatile uint64_t *dst, uint64_t *expect, uint64_t src)
{

	if (atomic_cmpset_64_i386(dst, *expect, src)) {
		return (1);
	} else {
		*expect = *dst;
		return (0);
	}
}

static __inline uint64_t
atomic_load_acq_64_i386(volatile uint64_t *p)
{
	volatile uint32_t *q;
	uint64_t res;

	q = (volatile uint32_t *)p;
	__asm __volatile(
	"	pushfl ;		"
	"	cli ;			"
	"	movl	%1,%%eax ;	"
	"	movl	%2,%%edx ;	"
	"	popfl"
	: "=&A" (res)			/* 0 */
	: "m" (*q),			/* 1 */
	  "m" (*(q + 1))		/* 2 */
	: "memory");
	return (res);
}

static __inline void
atomic_store_rel_64_i386(volatile uint64_t *p, uint64_t v)
{
	volatile uint32_t *q;

	q = (volatile uint32_t *)p;
	__asm __volatile(
	"	pushfl ;		"
	"	cli ;			"
	"	movl	%%eax,%0 ;	"
	"	movl	%%edx,%1 ;	"
	"	popfl"
	: "=m" (*q),			/* 0 */
	  "=m" (*(q + 1))		/* 1 */
	: "A" (v)			/* 2 */
	: "memory");
}

static __inline uint64_t
atomic_swap_64_i386(volatile uint64_t *p, uint64_t v)
{
	volatile uint32_t *q;
	uint64_t res;

	q = (volatile uint32_t *)p;
	__asm __volatile(
	"	pushfl ;		"
	"	cli ;			"
	"	movl	%1,%%eax ;	"
	"	movl	%2,%%edx ;	"
	"	movl	%4,%2 ;		"
	"	movl	%3,%1 ;		"
	"	popfl"
	: "=&A" (res),			/* 0 */
	  "+m" (*q),			/* 1 */
	  "+m" (*(q + 1))		/* 2 */
	: "r" ((uint32_t)v),		/* 3 */
	  "r" ((uint32_t)(v >> 32)));	/* 4 */
	return (res);
}

static __inline int
atomic_cmpset_64_i586(volatile uint64_t *dst, uint64_t expect, uint64_t src)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	cmpxchg8b %1 ;		"
	"	sete	%0"
	: "=q" (res),			/* 0 */
	  "+m" (*dst),			/* 1 */
	  "+A" (expect)			/* 2 */
	: "b" ((uint32_t)src),		/* 3 */
	  "c" ((uint32_t)(src >> 32))	/* 4 */
	: "memory", "cc");
	return (res);
}

static __inline int
atomic_fcmpset_64_i586(volatile uint64_t *dst, uint64_t *expect, uint64_t src)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	cmpxchg8b %1 ;		"
	"	sete	%0"
	: "=q" (res),			/* 0 */
	  "+m" (*dst),			/* 1 */
	  "+A" (*expect)		/* 2 */
	: "b" ((uint32_t)src),		/* 3 */
	  "c" ((uint32_t)(src >> 32))	/* 4 */
	: "memory", "cc");
	return (res);
}

static __inline uint64_t
atomic_load_acq_64_i586(volatile uint64_t *p)
{
	uint64_t res;

	__asm __volatile(
	"	movl	%%ebx,%%eax ;	"
	"	movl	%%ecx,%%edx ;	"
	"	" MPLOCKED "		"
	"	cmpxchg8b %1"
	: "=&A" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: : "memory", "cc");
	return (res);
}

static __inline void
atomic_store_rel_64_i586(volatile uint64_t *p, uint64_t v)
{

	__asm __volatile(
	"	movl	%%eax,%%ebx ;	"
	"	movl	%%edx,%%ecx ;	"
	"1:				"
	"	" MPLOCKED "		"
	"	cmpxchg8b %0 ;		"
	"	jne	1b"
	: "+m" (*p),			/* 0 */
	  "+A" (v)			/* 1 */
	: : "ebx", "ecx", "memory", "cc");
}

static __inline uint64_t
atomic_swap_64_i586(volatile uint64_t *p, uint64_t v)
{

	__asm __volatile(
	"	movl	%%eax,%%ebx ;	"
	"	movl	%%edx,%%ecx ;	"
	"1:				"
	"	" MPLOCKED "		"
	"	cmpxchg8b %0 ;		"
	"	jne	1b"
	: "+m" (*p),			/* 0 */
	  "+A" (v)			/* 1 */
	: : "ebx", "ecx", "memory", "cc");
	return (v);
}

static __inline int
atomic_cmpset_64(volatile uint64_t *dst, uint64_t expect, uint64_t src)
{

	if ((cpu_feature & CPUID_CX8) == 0)
		return (atomic_cmpset_64_i386(dst, expect, src));
	else
		return (atomic_cmpset_64_i586(dst, expect, src));
}

static __inline int
atomic_fcmpset_64(volatile uint64_t *dst, uint64_t *expect, uint64_t src)
{

  	if ((cpu_feature & CPUID_CX8) == 0)
		return (atomic_fcmpset_64_i386(dst, expect, src));
	else
		return (atomic_fcmpset_64_i586(dst, expect, src));
}

static __inline uint64_t
atomic_load_acq_64(volatile uint64_t *p)
{

	if ((cpu_feature & CPUID_CX8) == 0)
		return (atomic_load_acq_64_i386(p));
	else
		return (atomic_load_acq_64_i586(p));
}

static __inline void
atomic_store_rel_64(volatile uint64_t *p, uint64_t v)
{

	if ((cpu_feature & CPUID_CX8) == 0)
		atomic_store_rel_64_i386(p, v);
	else
		atomic_store_rel_64_i586(p, v);
}

static __inline uint64_t
atomic_swap_64(volatile uint64_t *p, uint64_t v)
{

	if ((cpu_feature & CPUID_CX8) == 0)
		return (atomic_swap_64_i386(p, v));
	else
		return (atomic_swap_64_i586(p, v));
}

static __inline uint64_t
atomic_fetchadd_64(volatile uint64_t *p, uint64_t v)
{

	for (;;) {
		uint64_t t = *p;
		if (atomic_cmpset_64(p, t, t + v))
			return (t);
	}
}

static __inline void
atomic_add_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t t;

	for (;;) {
		t = *p;
		if (atomic_cmpset_64(p, t, t + v))
			break;
	}
}

static __inline void
atomic_subtract_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t t;

	for (;;) {
		t = *p;
		if (atomic_cmpset_64(p, t, t - v))
			break;
	}
}

#endif /* _KERNEL */

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

ATOMIC_ASM(set,	     long,  "orl %1,%0",   "ir",  v);
ATOMIC_ASM(clear,    long,  "andl %1,%0",  "ir", ~v);
ATOMIC_ASM(add,	     long,  "addl %1,%0",  "ir",  v);
ATOMIC_ASM(subtract, long,  "subl %1,%0",  "ir",  v);

#define	ATOMIC_LOADSTORE(TYPE)				\
	ATOMIC_LOAD(TYPE);				\
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

static __inline int
atomic_cmpset_long(volatile u_long *dst, u_long expect, u_long src)
{

	return (atomic_cmpset_int((volatile u_int *)dst, (u_int)expect,
	    (u_int)src));
}

static __inline int
atomic_fcmpset_long(volatile u_long *dst, u_long *expect, u_long src)
{

	return (atomic_fcmpset_int((volatile u_int *)dst, (u_int *)expect,
	    (u_int)src));
}

static __inline u_long
atomic_fetchadd_long(volatile u_long *p, u_long v)
{

	return (atomic_fetchadd_int((volatile u_int *)p, (u_int)v));
}

static __inline int
atomic_testandset_long(volatile u_long *p, u_int v)
{

	return (atomic_testandset_int((volatile u_int *)p, v));
}

static __inline int
atomic_testandclear_long(volatile u_long *p, u_int v)
{

	return (atomic_testandclear_int((volatile u_int *)p, v));
}

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

	return (atomic_swap_int((volatile u_int *)p, (u_int)v));
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
#define	atomic_cmpset_acq_64 atomic_cmpset_64
#define	atomic_cmpset_rel_64 atomic_cmpset_64
#define	atomic_fcmpset_acq_64 atomic_fcmpset_64
#define	atomic_fcmpset_rel_64 atomic_fcmpset_64
#define	atomic_fetchadd_acq_64	atomic_fetchadd_64
#define	atomic_fetchadd_rel_64	atomic_fetchadd_64
#define	atomic_add_acq_64 atomic_add_64
#define	atomic_add_rel_64 atomic_add_64
#define	atomic_subtract_acq_64 atomic_subtract_64
#define	atomic_subtract_rel_64 atomic_subtract_64

/* Operations on pointers. */
#define	atomic_set_ptr(p, v) \
	atomic_set_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_acq_ptr(p, v) \
	atomic_set_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_rel_ptr(p, v) \
	atomic_set_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_ptr(p, v) \
	atomic_clear_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_acq_ptr(p, v) \
	atomic_clear_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_rel_ptr(p, v) \
	atomic_clear_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_ptr(p, v) \
	atomic_add_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_acq_ptr(p, v) \
	atomic_add_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_rel_ptr(p, v) \
	atomic_add_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_ptr(p, v) \
	atomic_subtract_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_acq_ptr(p, v) \
	atomic_subtract_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_rel_ptr(p, v) \
	atomic_subtract_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_load_acq_ptr(p) \
	atomic_load_acq_int((volatile u_int *)(p))
#define	atomic_store_rel_ptr(p, v) \
	atomic_store_rel_int((volatile u_int *)(p), (v))
#define	atomic_cmpset_ptr(dst, old, new) \
	atomic_cmpset_int((volatile u_int *)(dst), (u_int)(old), (u_int)(new))
#define	atomic_cmpset_acq_ptr(dst, old, new) \
	atomic_cmpset_acq_int((volatile u_int *)(dst), (u_int)(old), \
	    (u_int)(new))
#define	atomic_cmpset_rel_ptr(dst, old, new) \
	atomic_cmpset_rel_int((volatile u_int *)(dst), (u_int)(old), \
	    (u_int)(new))
#define	atomic_fcmpset_ptr(dst, old, new) \
	atomic_fcmpset_int((volatile u_int *)(dst), (u_int *)(old), (u_int)(new))
#define	atomic_fcmpset_acq_ptr(dst, old, new) \
	atomic_fcmpset_acq_int((volatile u_int *)(dst), (u_int *)(old), \
	    (u_int)(new))
#define	atomic_fcmpset_rel_ptr(dst, old, new) \
	atomic_fcmpset_rel_int((volatile u_int *)(dst), (u_int *)(old), \
	    (u_int)(new))
#define	atomic_swap_ptr(p, v) \
	atomic_swap_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_readandclear_ptr(p) \
	atomic_readandclear_int((volatile u_int *)(p))

#endif /* !WANT_FUNCTIONS */

#if defined(_KERNEL)
#define	mb()	__mbk()
#define	wmb()	__mbk()
#define	rmb()	__mbk()
#else
#define	mb()	__mbu()
#define	wmb()	__mbu()
#define	rmb()	__mbu()
#endif

#endif /* !_MACHINE_ATOMIC_H_ */
