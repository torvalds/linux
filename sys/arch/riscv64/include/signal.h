/*	$OpenBSD: signal.h,v 1.4 2021/05/20 15:14:30 drahn Exp $	*/

/*-
 * Copyright (c) 1986, 1989, 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
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
 *      @(#)signal.h    8.1 (Berkeley) 6/11/93
 *	from: FreeBSD: src/sys/i386/include/signal.h,v 1.13 2000/11/09
 *	from: FreeBSD: src/sys/sparc64/include/signal.h,v 1.6 2001/09/30 18:52:17
 * $FreeBSD: head/sys/riscv/include/signal.h 292407 2015-12-17 18:44:30Z br $
 */

#ifndef	_MACHINE_SIGNAL_H_
#define	_MACHINE_SIGNAL_H_

#include <sys/cdefs.h>

typedef int sig_atomic_t;

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420

#include <sys/_types.h>

struct sigcontext {
	int		__sc_unused;
	int		sc_mask;

	__register_t	sc_ra;
	__register_t	sc_sp;
	__register_t	sc_gp;
	__register_t	sc_tp;
	__register_t	sc_t[7];
	__register_t	sc_s[12];
	__register_t	sc_a[8];
	__register_t	sc_sepc;

	/* 64 bit floats as well as integer registers */
	__register_t	sc_f[32];	/* floating-point registers */
	__register_t	sc_fcsr;	/* floating-point control register */

	long	sc_cookie;
};

#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */

#endif /* !_MACHINE_SIGNAL_H_ */
