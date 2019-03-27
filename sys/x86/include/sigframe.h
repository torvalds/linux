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
 * $FreeBSD$
 */

#ifndef _X86_SIGFRAME_H_
#define	_X86_SIGFRAME_H_

/*
 * Signal frames, arguments passed to application signal handlers.
 */

#ifdef __i386__
struct sigframe {
	/*
	 * The first four members may be used by applications.
	 *
	 * NOTE: The 4th argument is undocumented, ill commented
	 * on and seems to be somewhat BSD "standard".  Handlers
	 * installed with sigvec may be using it.
	 */
	register_t	sf_signum;
	register_t	sf_siginfo;	/* code or pointer to sf_si */
	register_t	sf_ucontext;	/* points to sf_uc */
	register_t	sf_addr;	/* undocumented 4th arg */

	union {
		__siginfohandler_t	*sf_action;
		__sighandler_t		*sf_handler;
	} sf_ahu;
	ucontext_t	sf_uc;		/* = *sf_ucontext */
	siginfo_t	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
};
#endif /* __i386__ */

#ifdef __amd64__
struct sigframe {
	union {
		__siginfohandler_t	*sf_action;
		__sighandler_t		*sf_handler;
	} sf_ahu;
	ucontext_t	sf_uc;		/* = *sf_ucontext */
	siginfo_t	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
};
#endif /* __amd64__ */

#endif /* _X86_SIGFRAME_H_ */
