/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
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
static char sccsid[] = "@(#)strerror.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#if defined(NLS)
#include <nl_types.h>
#endif
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "reentrant.h"
#include "un-namespace.h"

#define	UPREFIX		"Unknown signal"

static char		sig_ebuf[NL_TEXTMAX];
static char		sig_ebuf_err[NL_TEXTMAX];
static once_t		sig_init_once = ONCE_INITIALIZER;
static thread_key_t	sig_key;
static int		sig_keycreated = 0;

static void
sig_keycreate(void)
{
	sig_keycreated = (thr_keycreate(&sig_key, free) == 0);
}

static char *
sig_tlsalloc(void)
{
	char *ebuf = NULL;

	if (thr_main() != 0)
		ebuf = sig_ebuf;
	else {
		if (thr_once(&sig_init_once, sig_keycreate) != 0 ||
		    !sig_keycreated)
			goto thr_err;
		if ((ebuf = thr_getspecific(sig_key)) == NULL) {
			if ((ebuf = malloc(sizeof(sig_ebuf))) == NULL)
				goto thr_err;
			if (thr_setspecific(sig_key, ebuf) != 0) {
				free(ebuf);
				ebuf = NULL;
				goto thr_err;
			}
		}
	}
thr_err:
	if (ebuf == NULL)
		ebuf = sig_ebuf_err;
	return (ebuf);
}

/* XXX: negative 'num' ? (REGR) */
char *
strsignal(int num)
{
	char *ebuf;
	char tmp[20];
	size_t n;
	int signum;
	char *t, *p;

#if defined(NLS)
	int saved_errno = errno;
	nl_catd catd;
	catd = catopen("libc", NL_CAT_LOCALE);
#endif

	ebuf = sig_tlsalloc();

	if (num > 0 && num < sys_nsig) {
		n = strlcpy(ebuf,
#if defined(NLS)
			catgets(catd, 2, num, sys_siglist[num]),
#else
			sys_siglist[num],
#endif
			sizeof(sig_ebuf));
	} else {
		n = strlcpy(ebuf,
#if defined(NLS)
			catgets(catd, 2, 0xffff, UPREFIX),
#else
			UPREFIX,
#endif
			sizeof(sig_ebuf));

		signum = num;
		if (num < 0)
			signum = -signum;

		t = tmp;
		do {
			*t++ = "0123456789"[signum % 10];
		} while (signum /= 10);
		if (num < 0)
			*t++ = '-';

		p = (ebuf + n);
		*p++ = ':';
		*p++ = ' ';

		for (;;) {
			*p++ = *--t;
			if (t <= tmp)
				break;
		}
		*p = '\0';
	}

#if defined(NLS)
	catclose(catd);
	errno = saved_errno;
#endif
	return (ebuf);
}
