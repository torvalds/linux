/*	$OpenBSD: sig.c,v 1.19 2016/04/11 21:17:29 schwarze Exp $	*/
/*	$NetBSD: sig.c,v 1.25 2016/04/11 18:56:31 christos Exp $	*/

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
 */

#include "config.h"

/*
 * sig.c: Signal handling stuff.
 *	  our policy is to trap all signals, set a good state
 *	  and pass the ball to our caller.
 */
#include <errno.h>
#include <stdlib.h>

#include "el.h"
#include "common.h"

static EditLine *sel = NULL;

static const int sighdl[] = {
#define	_DO(a)	(a),
	ALLSIGS
#undef	_DO
	- 1
};

static void sig_handler(int);

/* sig_handler():
 *	This is the handler called for all signals
 *	XXX: we cannot pass any data so we just store the old editline
 *	state in a private variable
 */
static void
sig_handler(int signo)
{
	int i, save_errno;
	sigset_t nset, oset;

	save_errno = errno;
	(void) sigemptyset(&nset);
	(void) sigaddset(&nset, signo);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);

	sel->el_signal->sig_no = signo;

	switch (signo) {
	case SIGCONT:
		tty_rawmode(sel);
		if (ed_redisplay(sel, 0) == CC_REFRESH)
			re_refresh(sel);
		terminal__flush(sel);
		break;

	case SIGWINCH:
		el_resize(sel);
		break;

	default:
		tty_cookedmode(sel);
		break;
	}

	for (i = 0; sighdl[i] != -1; i++)
		if (signo == sighdl[i])
			break;

	(void) sigaction(signo, &sel->el_signal->sig_action[i], NULL);
	sel->el_signal->sig_action[i].sa_handler = SIG_ERR;
	sel->el_signal->sig_action[i].sa_flags = 0;
	sigemptyset(&sel->el_signal->sig_action[i].sa_mask);
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	(void) kill(0, signo);
	errno = save_errno;
}


/* sig_init():
 *	Initialize all signal stuff
 */
protected int
sig_init(EditLine *el)
{
	size_t i;
	sigset_t *nset, oset;

	el->el_signal = malloc(sizeof(*el->el_signal));
	if (el->el_signal == NULL)
		return -1;

	nset = &el->el_signal->sig_set;
	(void) sigemptyset(nset);
#define	_DO(a) (void) sigaddset(nset, a);
	ALLSIGS
#undef	_DO
	(void) sigprocmask(SIG_BLOCK, nset, &oset);

	for (i = 0; sighdl[i] != -1; i++) {
		el->el_signal->sig_action[i].sa_handler = SIG_ERR;
		el->el_signal->sig_action[i].sa_flags = 0;
		sigemptyset(&el->el_signal->sig_action[i].sa_mask);
	}

	(void) sigprocmask(SIG_SETMASK, &oset, NULL);

	return 0;
}


/* sig_end():
 *	Clear all signal stuff
 */
protected void
sig_end(EditLine *el)
{

	free(el->el_signal);
	el->el_signal = NULL;
}


/* sig_set():
 *	set all the signal handlers
 */
protected void
sig_set(EditLine *el)
{
	size_t i;
	sigset_t oset;
	struct sigaction osa, nsa;

	nsa.sa_handler = sig_handler;
	nsa.sa_flags = 0;
	sigemptyset(&nsa.sa_mask);

	(void) sigprocmask(SIG_BLOCK, &el->el_signal->sig_set, &oset);

	for (i = 0; sighdl[i] != -1; i++) {
		/* This could happen if we get interrupted */
		if (sigaction(sighdl[i], &nsa, &osa) != -1 &&
		    osa.sa_handler != sig_handler)
			el->el_signal->sig_action[i] = osa;
	}
	sel = el;
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
}


/* sig_clr():
 *	clear all the signal handlers
 */
protected void
sig_clr(EditLine *el)
{
	size_t i;
	sigset_t oset;

	(void) sigprocmask(SIG_BLOCK, &el->el_signal->sig_set, &oset);

	for (i = 0; sighdl[i] != -1; i++)
		if (el->el_signal->sig_action[i].sa_handler != SIG_ERR)
			(void)sigaction(sighdl[i],
			    &el->el_signal->sig_action[i], NULL);

	sel = NULL;		/* we are going to die if the handler is
				 * called */
	(void)sigprocmask(SIG_SETMASK, &oset, NULL);
}
