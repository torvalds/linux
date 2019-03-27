/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)sigcompat.c	8.1 (Berkeley) 6/2/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include "un-namespace.h"
#include "libc_private.h"

int
sigvec(int signo, struct sigvec *sv, struct sigvec *osv)
{
	struct sigaction sa, osa;
	struct sigaction *sap, *osap;
	int ret;

	if (sv != NULL) {
		sa.sa_handler = sv->sv_handler;
		sa.sa_flags = sv->sv_flags ^ SV_INTERRUPT;
		sigemptyset(&sa.sa_mask);
		sa.sa_mask.__bits[0] = sv->sv_mask;
		sap = &sa;
	} else
		sap = NULL;
	osap = osv != NULL ? &osa : NULL;
	ret = __libc_sigaction(signo, sap, osap);
	if (ret == 0 && osv != NULL) {
		osv->sv_handler = osa.sa_handler;
		osv->sv_flags = osa.sa_flags ^ SV_INTERRUPT;
		osv->sv_mask = osa.sa_mask.__bits[0];
	}
	return (ret);
}

int
sigsetmask(int mask)
{
	sigset_t set, oset;
	int n;

	sigemptyset(&set);
	set.__bits[0] = mask;
	n = __libc_sigprocmask(SIG_SETMASK, &set, &oset);
	if (n)
		return (n);
	return (oset.__bits[0]);
}

int
sigblock(int mask)
{
	sigset_t set, oset;
	int n;

	sigemptyset(&set);
	set.__bits[0] = mask;
	n = __libc_sigprocmask(SIG_BLOCK, &set, &oset);
	if (n)
		return (n);
	return (oset.__bits[0]);
}

int
sigpause(int mask)
{
	sigset_t set;

	sigemptyset(&set);
	set.__bits[0] = mask;
	return (__libc_sigsuspend(&set));
}

int
xsi_sigpause(int sig)
{
	sigset_t set;

	if (__libc_sigprocmask(SIG_BLOCK, NULL, &set) == -1)
		return (-1);
	if (sigdelset(&set, sig) == -1)
		return (-1);
	return (__libc_sigsuspend(&set));
}

int
sighold(int sig)
{
	sigset_t set;

	sigemptyset(&set);
	if (sigaddset(&set, sig) == -1)
		return (-1);
	return (__libc_sigprocmask(SIG_BLOCK, &set, NULL));
}

int
sigignore(int sig)
{
	struct sigaction sa;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	return (__libc_sigaction(sig, &sa, NULL));
}

int
sigrelse(int sig)
{
	sigset_t set;

	sigemptyset(&set);
	if (sigaddset(&set, sig) == -1)
		return (-1);
	return (__libc_sigprocmask(SIG_UNBLOCK, &set, NULL));
}

void
(*sigset(int sig, void (*disp)(int)))(int)
{
	sigset_t set, pset;
	struct sigaction sa, psa;

	sigemptyset(&set);
	if (sigaddset(&set, sig) == -1)
		return (SIG_ERR);
	if (__libc_sigprocmask(SIG_BLOCK, NULL, &pset) == -1)
		return (SIG_ERR);
	if ((__sighandler_t *)disp == SIG_HOLD) {
		if (__libc_sigprocmask(SIG_BLOCK, &set, &pset) == -1)
			return (SIG_ERR);
		if (sigismember(&pset, sig))
			return (SIG_HOLD);
		else {
			if (__libc_sigaction(sig, NULL, &psa) == -1)
				return (SIG_ERR);
			return (psa.sa_handler);
		}
	} else {
		if (__libc_sigprocmask(SIG_UNBLOCK, &set, &pset) == -1)
			return (SIG_ERR);
	}

	bzero(&sa, sizeof(sa));
	sa.sa_handler = disp;
	if (__libc_sigaction(sig, &sa, &psa) == -1)
		return (SIG_ERR);
	if (sigismember(&pset, sig))
		return (SIG_HOLD);
	else
		return (psa.sa_handler);
}
