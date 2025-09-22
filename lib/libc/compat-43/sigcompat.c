/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
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

#include <signal.h>

int
sigvec(int signo, struct sigvec *sv, struct sigvec *osv)
{
	int ret;
	struct sigvec nsv;

	if (sv) {
		nsv = *sv;
		nsv.sv_flags ^= SV_INTERRUPT;	/* !SA_INTERRUPT */
	}
	ret = WRAP(sigaction)(signo, sv ? (struct sigaction *)&nsv : NULL,
	    (struct sigaction *)osv);
	if (ret == 0 && osv)
		osv->sv_flags ^= SV_INTERRUPT;	/* !SA_INTERRUPT */
	return (ret);
}

int
sigsetmask(int mask)
{
	int omask, n;

	n = WRAP(sigprocmask)(SIG_SETMASK, (sigset_t *) &mask,
	    (sigset_t *) &omask);
	if (n)
		return (n);
	return (omask);
}

int
sigblock(int mask)
{
	int omask, n;

	n = WRAP(sigprocmask)(SIG_BLOCK, (sigset_t *) &mask,
	    (sigset_t *) &omask);
	if (n)
		return (n);
	return (omask);
}

int
sigpause(int mask)
{
	return (sigsuspend((sigset_t *)&mask));
}
