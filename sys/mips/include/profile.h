/*	$OpenBSD: profile.h,v 1.2 1999/01/27 04:46:05 imp Exp $ */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	from: @(#)profile.h	8.1 (Berkeley) 6/10/93
 *	JNPR: profile.h,v 1.4 2006/12/02 09:53:41 katta
 * $FreeBSD$
 */
#ifndef _MACHINE_PROFILE_H_
#define	_MACHINE_PROFILE_H_

#define	_MCOUNT_DECL void ___mcount

/*XXX The cprestore instruction is a "dummy" to shut up as(1). */

/*XXX This is not MIPS64 safe. */

#define	MCOUNT \
	__asm(".globl _mcount;"		\
	".type _mcount,@function;"	\
	"_mcount:;"			\
	".set noreorder;"		\
	".set noat;"			\
	".cpload $25;"			\
	".cprestore 4;"			\
	"sw $4,8($29);"			\
	"sw $5,12($29);"		\
	"sw $6,16($29);"		\
	"sw $7,20($29);"		\
	"sw $1,0($29);"			\
	"sw $31,4($29);"		\
	"move $5,$31;"			\
	"jal ___mcount;"		\
	"move $4,$1;"			\
	"lw $4,8($29);"			\
	"lw $5,12($29);"		\
	"lw $6,16($29);"		\
	"lw $7,20($29);"		\
	"lw $31,4($29);"		\
	"lw $1,0($29);"			\
	"addu $29,$29,8;"		\
	"j $31;"			\
	"move $31,$1;"			\
	".set reorder;"			\
	".set at");

#ifdef _KERNEL
/*
 * The following two macros do splhigh and splx respectively.
 * They have to be defined this way because these are real
 * functions on the MIPS, and we do not want to invoke mcount
 * recursively.
 */

#define	MCOUNT_DECL(s)	u_long s;
#ifdef SMP
extern int	mcount_lock;
#define	MCOUNT_ENTER(s)	{					\
	s = intr_disable();					\
	while (!atomic_cmpset_acq_int(&mcount_lock, 0, 1))	\
		/* nothing */ ;					\
}
#define	MCOUNT_EXIT(s)	{					\
	atomic_store_rel_int(&mcount_lock, 0);			\
	intr_restore(s);						\
}
#else
#define	MCOUNT_ENTER(s)	{ s = intr_disable(); }
#define	MCOUNT_EXIT(s)	(intr_restore(s))
#endif

/* REVISIT for mips */
/*
 * Config generates something to tell the compiler to align functions on 16
 * byte boundaries.  A strict alignment is good for keeping the tables small.
 */
#define	FUNCTION_ALIGNMENT	16

#ifdef GUPROF
struct gmonparam;
void	stopguprof __P((struct gmonparam *p));
#else
#define	stopguprof(p)
#endif /* GUPROF */

#else	/* !_KERNEL */

#define	FUNCTION_ALIGNMENT	4

#ifdef __mips_n64
typedef u_long	uintfptr_t;
#else
typedef u_int	uintfptr_t;
#endif

#endif /* _KERNEL */

/*
 * An unsigned integral type that can hold non-negative difference between
 * function pointers.
 */
#ifdef __mips_n64
typedef u_long	fptrdiff_t;
#else
typedef u_int	fptrdiff_t;
#endif

#ifdef _KERNEL

void	mcount(uintfptr_t frompc, uintfptr_t selfpc);

#ifdef GUPROF
struct gmonparam;

void	nullfunc_loop_profiled(void);
void	nullfunc_profiled(void);
void	startguprof(struct gmonparam *p);
void	stopguprof(struct gmonparam *p);
#else
#define	startguprof(p)
#define	stopguprof(p)
#endif /* GUPROF */

#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
#ifdef __GNUC__
#ifdef __ELF__
void	mcount(void) __asm(".mcount");
#else
void	mcount(void) __asm("mcount");
#endif
#endif
void	_mcount(uintfptr_t frompc, uintfptr_t selfpc);
__END_DECLS

#endif /* _KERNEL */

#ifdef GUPROF
/* XXX doesn't quite work outside kernel yet. */
extern int	cputime_bias;

__BEGIN_DECLS
int	cputime(void);
void	empty_loop(void);
void	mexitcount(uintfptr_t selfpc);
void	nullfunc(void);
void	nullfunc_loop(void);
__END_DECLS
#endif

#endif /* !_MACHINE_PROFILE_H_ */
