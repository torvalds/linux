/*	$OpenBSD: signal.h,v 1.10 2018/06/23 22:15:14 kettenis Exp $	*/
/*	$NetBSD: signal.h,v 1.5 2003/10/18 17:57:21 briggs Exp $	*/

/*
 * Copyright (c) 1994-1996 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * signal.h
 *
 * Architecture dependant signal types and structures
 *
 * Created      : 30/09/94
 */

#ifndef _ARM_SIGNAL_H_
#define _ARM_SIGNAL_H_

#ifndef _LOCORE
#include <sys/cdefs.h>

typedef int sig_atomic_t;

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 */
struct sigcontext {
	long	sc_cookie;
	int	sc_mask;	/* signal mask to restore */

	unsigned int sc_spsr;
	unsigned int sc_r0;
	unsigned int sc_r1;
	unsigned int sc_r2;
	unsigned int sc_r3;
	unsigned int sc_r4;
	unsigned int sc_r5;
	unsigned int sc_r6;
	unsigned int sc_r7;
	unsigned int sc_r8;
	unsigned int sc_r9;
	unsigned int sc_r10;
	unsigned int sc_r11;
	unsigned int sc_r12;
	unsigned int sc_usr_sp;
	unsigned int sc_usr_lr;
	unsigned int sc_svc_lr;
	unsigned int sc_pc;

	unsigned int sc_fpused;
	unsigned int sc_fpscr;
	unsigned long long sc_fpreg[32];
};
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */
#endif /* !_LOCORE */

#endif	/* !_ARM_SIGNAL_H_ */
