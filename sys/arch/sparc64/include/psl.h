/*	$OpenBSD: psl.h,v 1.37 2024/11/06 07:11:14 miod Exp $	*/
/*	$NetBSD: psl.h,v 1.20 2001/04/13 23:30:05 thorpej Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)psl.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _SPARC64_PSL_
#define _SPARC64_PSL_

/* Interesting spl()s */
#define PIL_SCSI	3
#define PIL_BIO		5
#define PIL_VIDEO	5
#define PIL_TTY		6
#define PIL_NET		6
#define PIL_VM		7
#define	PIL_AUD		8
#define PIL_CLOCK	10
#define PIL_FD		11
#define PIL_SER		12
#define PIL_STATCLOCK	14
#define PIL_HIGH	15
#define PIL_SCHED	PIL_STATCLOCK

/* 
 * SPARC V9 CCR register
 */

#define ICC_C	0x01L
#define ICC_V	0x02L
#define ICC_Z	0x04L
#define ICC_N	0x08L
#define XCC_SHIFT	4
#define XCC_C	(ICC_C<<XCC_SHIFT)
#define XCC_V	(ICC_V<<XCC_SHIFT)
#define XCC_Z	(ICC_Z<<XCC_SHIFT)
#define XCC_N	(ICC_N<<XCC_SHIFT)


/*
 * SPARC V9 PSTATE register (what replaces the PSR in V9)
 *
 * Here's the layout:
 *
 *    11   10    9     8   7  6   5     4     3     2     1   0
 *  +------------------------------------------------------------+
 *  | IG | MG | CLE | TLE | MM | RED | PEF | AM | PRIV | IE | AG |
 *  +------------------------------------------------------------+
 */

#define PSTATE_IG	0x800	/* enable spitfire interrupt globals */
#define PSTATE_MG	0x400	/* enable spitfire MMU globals */
#define PSTATE_CLE	0x200	/* current little endian */
#define PSTATE_TLE	0x100	/* traps little endian */
#define PSTATE_MM	0x0c0	/* memory model */
#define PSTATE_MM_TSO	0x000	/* total store order */
#define PSTATE_MM_PSO	0x040	/* partial store order */
#define PSTATE_MM_RMO	0x080	/* Relaxed memory order */
#define PSTATE_RED	0x020	/* RED state */
#define PSTATE_PEF	0x010	/* enable floating point */
#define PSTATE_AM	0x008	/* 32-bit address masking */
#define PSTATE_PRIV	0x004	/* privileged mode */
#define PSTATE_IE	0x002	/* interrupt enable */
#define PSTATE_AG	0x001	/* enable alternate globals */

#define PSTATE_BITS "\20\14IG\13MG\12CLE\11TLE\10\7MM\6RED\5PEF\4AM\3PRIV\2IE\1AG"


/*
 * We're running kernel code in TSO for the moment so we don't need to worry
 * about possible memory barrier bugs.
 * Userland code sets the memory model in the ELF header.
 */

#define PSTATE_PROM	(PSTATE_MM_TSO|PSTATE_PRIV)
#define PSTATE_KERN	(PSTATE_MM_TSO|PSTATE_PRIV)
#define PSTATE_INTR	(PSTATE_KERN|PSTATE_IE)
#define PSTATE_USER	(PSTATE_MM_RMO|PSTATE_IE)


/*
 * SPARC V9 TSTATE register
 *
 *   39 32 31 24 23 18  17   8	7 5 4   0
 *  +-----+-----+-----+--------+---+-----+
 *  | CCR | ASI |  -  | PSTATE | - | CWP |
 *  +-----+-----+-----+--------+---+-----+
 */

#define TSTATE_CWP		0x01f
#define TSTATE_PSTATE		0x6ff00
#define TSTATE_PSTATE_SHIFT	8
#define TSTATE_ASI		0xff000000LL
#define TSTATE_ASI_SHIFT	24
#define TSTATE_CCR		0xff00000000LL
#define TSTATE_CCR_SHIFT	32

/* Leftover SPARC V8 PSTATE stuff */
#define PSR_ICC 0x00f00000

/*
 * These are here to simplify life.
 */
#define TSTATE_PEF	(PSTATE_PEF<<TSTATE_PSTATE_SHIFT)
#define TSTATE_PRIV	(PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)

#define TSTATE_KERN	((PSTATE_KERN)<<TSTATE_PSTATE_SHIFT)
/*
 * SPARC V9 VER version register.
 *
 *  63   48 47  32 31  24 23 16 15    8 7 5 4      0
 * +-------+------+------+-----+-------+---+--------+
 * | manuf | impl | mask |  -  | maxtl | - | maxwin |
 * +-------+------+------+-----+-------+---+--------+
 *
 */

#define VER_MANUF	0xffff000000000000ULL
#define VER_MANUF_SHIFT	48
#define VER_IMPL	0x0000ffff00000000ULL
#define VER_IMPL_SHIFT	32
#define VER_MASK	0x00000000ff000000ULL
#define VER_MASK_SHIFT	24
#define VER_MAXTL	0x000000000000ff00ULL
#define VER_MAXTL_SHIFT	8
#define VER_MAXWIN	0x000000000000001fULL

#define IMPL_SPARC64		0x01 /* SPARC64 */
#define IMPL_SPARC64_II		0x02 /* SPARC64-II */
#define IMPL_SPARC64_III	0x03 /* SPARC64-III */
#define IMPL_SPARC64_IV		0x04 /* SPARC64-IV */
#define IMPL_ZEUS		0x05 /* SPARC64-V */
#define IMPL_OLYMPUS_C		0x06 /* SPARC64-VI */
#define IMPL_JUPITER		0x07 /* SPARC64-VII */
#define IMPL_SPITFIRE		0x10 /* UltraSPARC */
#define IMPL_BLACKBIRD		0x11 /* UltraSPARC-II */
#define IMPL_SABRE		0x12 /* UltraSPARC-IIi */
#define IMPL_HUMMINGBIRD	0x13 /* UltraSPARC-IIe */
#define IMPL_CHEETAH		0x14 /* UltraSPARC-III */
#define IMPL_CHEETAH_PLUS	0x15 /* UltraSPARC-III+ */
#define IMPL_JALAPENO		0x16 /* UltraSPARC-IIIi */
#define IMPL_JAGUAR		0x18 /* UltraSPARC-IV */
#define IMPL_PANTHER		0x19 /* UltraSPARC-IV+ */
#define IMPL_SERRANO		0x22 /* UltraSPARC-IIIi+ */

/*
 * Here are a few things to help us transition between user and kernel mode:
 */

/* Memory models */
#define KERN_MM		PSTATE_MM_TSO
#define USER_MM		PSTATE_MM_RMO

/* 
 * Register window handlers.  These point to generic routines that check the
 * stack pointer and then vector to the real handler.  We could optimize this
 * if we could guarantee only 32-bit or 64-bit stacks.
 */
#define WSTATE_KERN	027
#define WSTATE_USER	022

#define CWP		0x01f

/* 64-byte alignment -- this seems the best place to put this. */
#define BLOCK_SIZE	64
#define BLOCK_ALIGN	0x3f

#if defined(_KERNEL) && !defined(_LOCORE)

#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void splassert_check(int, const char *);
#define splassert(__wantipl) do {			\
	if (splassert_ctl > 0) {			\
		splassert_check(__wantipl, __func__);	\
	}						\
} while (0)
#define splsoftassert(wantipl) splassert(wantipl)
#else
#define splassert(wantipl)	do { /* nada */ } while (0)
#define splsoftassert(wantipl)	do { /* nada */ } while (0)
#endif

/*
 * GCC pseudo-functions for manipulating privileged registers
 */
static inline u_int64_t getpstate(void);
static inline u_int64_t
getpstate(void)
{
	return (sparc_rdpr(pstate));
}

static inline void setpstate(u_int64_t);
static inline void
setpstate(u_int64_t newpstate)
{
	sparc_wrpr(pstate, newpstate, 0);
}

static inline int getcwp(void);
static inline int
getcwp(void)
{
	return (sparc_rdpr(cwp));
}

static inline void setcwp(u_int64_t);
static inline void
setcwp(u_int64_t newcwp)
{
	sparc_wrpr(cwp, newcwp, 0);
}

static inline u_int64_t getver(void);
static inline u_int64_t
getver(void)
{
	return (sparc_rdpr(ver));
}

static inline u_int64_t intr_disable(void);
static inline u_int64_t
intr_disable(void)
{
	u_int64_t s;

	s = sparc_rdpr(pstate);
	sparc_wrpr(pstate, s & ~PSTATE_IE, 0);
	return (s);
}

static inline void intr_restore(u_int64_t);
static inline void
intr_restore(u_int64_t s)
{
	sparc_wrpr(pstate, s, 0);
}

static inline void stxa_sync(u_int64_t, u_int64_t, u_int64_t);
static inline void
stxa_sync(u_int64_t va, u_int64_t asi, u_int64_t val)
{
	u_int64_t s = intr_disable();
	stxa_nc(va, asi, val);
	__asm volatile("membar #Sync" : : : "memory");
	intr_restore(s);
}

static inline int
_spl(int newipl)
{
	int oldpil;

	__asm volatile(	"    rdpr %%pil, %0		\n"
			"    wrpr %%g0, %1, %%pil	\n"
	    : "=&r" (oldpil)
	    : "I" (newipl)
	    : "%g0");
	__asm volatile("" : : : "memory");

	return (oldpil);
}

/* A non-priority-decreasing version of SPL */
static inline int
_splraise(int newpil)
{
	int oldpil;

	oldpil = sparc_rdpr(pil);
	if (newpil > oldpil)
		sparc_wrpr(pil, newpil, 0);
        return (oldpil);
}

static inline void
_splx(int newpil)
{
	sparc_wrpr(pil, newpil, 0);
}

#endif /* KERNEL && !_LOCORE */

#endif /* _SPARC64_PSL_ */
