/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)gmon.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD$
 */

#ifndef _SYS_GMON_H_
#define _SYS_GMON_H_

#include <machine/profile.h>

/*
 * Structure prepended to gmon.out profiling data file.
 */
struct gmonhdr {
	u_long	lpc;		/* base pc address of sample buffer */
	u_long	hpc;		/* max pc address of sampled buffer */
	int	ncnt;		/* size of sample buffer (plus this header) */
	int	version;	/* version number */
	int	profrate;	/* profiling clock rate */
	int	histcounter_type; /* size (in bits) and sign of HISTCOUNTER */
	int	spare[2];	/* reserved */
};
#define GMONVERSION	0x00051879

/*
 * Type of histogram counters used in the kernel.
 */
#ifdef GPROF4
#define	HISTCOUNTER	int64_t
#else
#define	HISTCOUNTER	unsigned short
#endif

/*
 * Fraction of text space to allocate for histogram counters.
 * We allocate counters at the same or higher density as function
 * addresses, so that each counter belongs to a unique function.
 * A lower density of counters would give less resolution but a
 * higher density would be wasted.
 */
#define	HISTFRACTION	(FUNCTION_ALIGNMENT / sizeof(HISTCOUNTER) == 0 \
			 ? 1 : FUNCTION_ALIGNMENT / sizeof(HISTCOUNTER))

/*
 * Fraction of text space to allocate for from hash buckets.
 * The value of HASHFRACTION is based on the minimum number of bytes
 * of separation between two subroutine call points in the object code.
 * Given MIN_SUBR_SEPARATION bytes of separation the value of
 * HASHFRACTION is calculated as:
 *
 *	HASHFRACTION = MIN_SUBR_SEPARATION / (2 * sizeof(short) - 1);
 *
 * For example, on the VAX, the shortest two call sequence is:
 *
 *	calls	$0,(r0)
 *	calls	$0,(r0)
 *
 * which is separated by only three bytes, thus HASHFRACTION is
 * calculated as:
 *
 *	HASHFRACTION = 3 / (2 * 2 - 1) = 1
 *
 * Note that the division above rounds down, thus if MIN_SUBR_FRACTION
 * is less than three, this algorithm will not work!
 *
 * In practice, however, call instructions are rarely at a minimal
 * distance.  Hence, we will define HASHFRACTION to be 2 across all
 * architectures.  This saves a reasonable amount of space for
 * profiling data structures without (in practice) sacrificing
 * any granularity.
 */
/*
 * XXX I think the above analysis completely misses the point.  I think
 * the point is that addresses in different functions must hash to
 * different values.  Since the hash is essentially division by
 * sizeof(unsigned short), the correct formula is:
 *
 * 	HASHFRACTION = MIN_FUNCTION_ALIGNMENT / sizeof(unsigned short)
 *
 * Note that he unsigned short here has nothing to do with the one for
 * HISTFRACTION.
 *
 * Hash collisions from a two call sequence don't matter.  They get
 * handled like collisions for calls to different addresses from the
 * same address through a function pointer.
 */
#define	HASHFRACTION	(FUNCTION_ALIGNMENT / sizeof(unsigned short) == 0 \
			 ? 1 : FUNCTION_ALIGNMENT / sizeof(unsigned short))

/*
 * percent of text space to allocate for tostructs with a minimum.
 */
#define ARCDENSITY	2
#define MINARCS		50

/*
 * Limit on the number of arcs to so that arc numbers can be stored in
 * `*froms' and stored and incremented without overflow in links.
 */
#define MAXARCS		(((u_long)1 << (8 * sizeof(u_short))) - 2)

struct tostruct {
	u_long	selfpc;
	long	count;
	u_short	link;
	u_short pad;
};

/*
 * a raw arc, with pointers to the calling site and
 * the called site and a count.
 */
struct rawarc {
	u_long	raw_frompc;
	u_long	raw_selfpc;
	long	raw_count;
};

/*
 * general rounding functions.
 */
#define ROUNDDOWN(x,y)	rounddown(x,y)
#define ROUNDUP(x,y)	roundup(x,y)

/*
 * The profiling data structures are housed in this structure.
 */
struct gmonparam {
	int		state;
	HISTCOUNTER	*kcount;
	u_long		kcountsize;
	u_short		*froms;
	u_long		fromssize;
	struct tostruct	*tos;
	u_long		tossize;
	long		tolimit;
	uintfptr_t	lowpc;
	uintfptr_t	highpc;
	u_long		textsize;
	u_long		hashfraction;
	int		profrate;	/* XXX wrong type to match gmonhdr */
	HISTCOUNTER	*cputime_count;
	int		cputime_overhead;
	HISTCOUNTER	*mcount_count;
	int		mcount_overhead;
	int		mcount_post_overhead;
	int		mcount_pre_overhead;
	HISTCOUNTER	*mexitcount_count;
	int		mexitcount_overhead;
	int		mexitcount_post_overhead;
	int		mexitcount_pre_overhead;
	int		histcounter_type;
};
extern struct gmonparam _gmonparam;

/*
 * Possible states of profiling.
 */
#define	GMON_PROF_ON	0
#define	GMON_PROF_BUSY	1
#define	GMON_PROF_ERROR	2
#define	GMON_PROF_OFF	3
#define	GMON_PROF_HIRES	4

/*
 * Sysctl definitions for extracting profiling information from the kernel.
 */
#define	GPROF_STATE	0	/* int: profiling enabling variable */
#define	GPROF_COUNT	1	/* struct: profile tick count buffer */
#define	GPROF_FROMS	2	/* struct: from location hash bucket */
#define	GPROF_TOS	3	/* struct: destination/count structure */
#define	GPROF_GMONPARAM	4	/* struct: profiling parameters (see above) */

#ifdef _KERNEL

#define	KCOUNT(p,index) \
	((p)->kcount[(index) / (HISTFRACTION * sizeof(HISTCOUNTER))])
#define	PC_TO_I(p, pc)	((uintfptr_t)(pc) - (uintfptr_t)(p)->lowpc)

#ifdef GUPROF

#define	CALIB_SCALE	1000

extern int	cputime_bias;

int	cputime(void);
void	nullfunc_loop_profiled(void);
void	nullfunc_profiled(void);
void	startguprof(struct gmonparam *p);
void	stopguprof(struct gmonparam *p);

#else /* !GUPROF */

#define	startguprof(p)
#define	stopguprof(p)

#endif /* GUPROF */

void	empty_loop(void);
void	kmupetext(uintfptr_t nhighpc);
void	mexitcount(uintfptr_t selfpc);
void	nullfunc(void);
void	nullfunc_loop(void);

#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
void	moncontrol(int);
void	monstartup(u_long, u_long);
__END_DECLS

#endif /* _KERNEL */

#endif /* !_SYS_GMON_H_ */
