/*	$OpenBSD: sig.h,v 1.8 2016/03/20 22:57:59 schwarze Exp $	*/
/*	$NetBSD: sig.h,v 1.10 2016/02/16 15:53:48 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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
 *	@(#)sig.h	8.1 (Berkeley) 6/4/93
 */

/*
 * el.sig.h: Signal handling functions
 */
#ifndef _h_el_sig
#define	_h_el_sig

#include <signal.h>

/*
 * Define here all the signals we are going to handle
 * The _DO macro is used to iterate in the source code
 */
#define	ALLSIGS		\
	_DO(SIGINT)	\
	_DO(SIGTSTP)	\
	_DO(SIGQUIT)	\
	_DO(SIGHUP)	\
	_DO(SIGTERM)	\
	_DO(SIGCONT)	\
	_DO(SIGWINCH)
#define ALLSIGSNO	7

typedef struct {
	struct sigaction sig_action[ALLSIGSNO];
	sigset_t sig_set;
	volatile sig_atomic_t sig_no;
} *el_signal_t;

protected void	sig_end(EditLine*);
protected int	sig_init(EditLine*);
protected void	sig_set(EditLine*);
protected void	sig_clr(EditLine*);

#endif /* _h_el_sig */
