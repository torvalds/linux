/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: src/sys/alpha/include/sigframe.h,v 1.1 1999/09/29 15:06:26 marcel
 *	from: sigframe.h,v 1.1 2006/08/07 05:38:57 katta
 * $FreeBSD$
 */
#ifndef _MACHINE_SIGFRAME_H_
#define	_MACHINE_SIGFRAME_H_

/*
 * WARNING: code in locore.s assumes the layout shown for sf_signum
 * thru sf_addr so... don't alter them!
 */
struct sigframe {
	register_t	sf_signum;
	register_t	sf_siginfo;	/* code or pointer to sf_si */
	register_t	sf_ucontext;	/* points to sf_uc */
	register_t	sf_addr;	/* undocumented 4th arg */
	ucontext_t	sf_uc;		/* = *sf_ucontext */
	siginfo_t	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
	unsigned long	__spare__[2];
};

#if (defined(__mips_n32) || defined(__mips_n64)) && defined(COMPAT_FREEBSD32)
#include <compat/freebsd32/freebsd32_signal.h>

struct sigframe32 {
	int32_t		sf_signum;
	int32_t		sf_siginfo;	/* code or pointer to sf_si */
	int32_t		sf_ucontext;	/* points to sf_uc */
	int32_t		sf_addr;	/* undocumented 4th arg */
	ucontext32_t	sf_uc;		/* = *sf_ucontext */
	struct siginfo32	sf_si;	/* = *sf_siginfo (SA_SIGINFO case) */
	uint32_t	__spare__[2];
};
#endif

#endif /* !_MACHINE_SIGFRAME_H_ */
