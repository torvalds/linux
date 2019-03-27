/*	$OpenBSD: signal.h,v 1.2 1999/01/27 04:10:03 imp Exp $	*/

/*
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
 *	@(#)signal.h	8.1 (Berkeley) 6/10/93
 *	JNPR: signal.h,v 1.4 2007/01/08 04:58:37 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_SIGNAL_H_
#define	_MACHINE_SIGNAL_H_

#include <sys/cdefs.h>
#include <sys/_sigset.h>

/*
 * Machine-dependent signal definitions
 */

typedef int sig_atomic_t;

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)
/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 */

struct	sigcontext {
	/*
	 * The fields following 'sc_mask' must match the definition
	 * of struct __mcontext. That way we can support
	 * struct sigcontext and ucontext_t at the same
	 * time.
	 */
	__sigset_t	sc_mask;	/* signal mask to restore */
	int		sc_onstack;	/* sigstack state to restore */
	__register_t	sc_pc;		/* pc at time of signal */
	__register_t	sc_regs[32];	/* processor regs 0 to 31 */
	__register_t	sr;		/* status register */
	__register_t	mullo, mulhi;	/* mullo and mulhi registers... */
	int		sc_fpused;	/* fp has been used */
	f_register_t	sc_fpregs[33];	/* fp regs 0 to 31 and csr */
	__register_t	sc_fpc_eir;	/* fp exception instruction reg */
	void		*sc_tls;	/* pointer to TLS area */
	int		__spare__[8];	/* XXX reserved */ 
};

#endif /* !_ANSI_SOURCE && !_POSIX_SOURCE */

#endif	/* !_MACHINE_SIGNAL_H_ */
