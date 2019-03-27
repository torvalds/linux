/*	$OpenBSD: tipout.c,v 1.18 2006/05/31 07:03:08 jason Exp $	*/
/*	$NetBSD: tipout.c,v 1.5 1996/12/29 10:34:12 cgd Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
#if 0
static char sccsid[] = "@(#)tipout.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: tipout.c,v 1.18 2006/05/31 07:03:08 jason Exp $";
#endif
#endif /* not lint */

#include "tip.h"

/*
 * tip
 *
 * lower fork of tip -- handles passive side
 *  reading from the remote host
 */

static	jmp_buf sigbuf;

static void	intIOT(int);
static void	intEMT(int);
static void	intTERM(int);
static void	intSYS(int);

/*
 * TIPOUT wait state routine --
 *   sent by TIPIN when it wants to posses the remote host
 */
/*ARGSUSED*/
static void
intIOT(int signo)
{
	write(repdes[1],&ccc,1);
	read(fildes[0], &ccc,1);
	longjmp(sigbuf, 1);
}

/*
 * Scripting command interpreter --
 *  accepts script file name over the pipe and acts accordingly
 */
/*ARGSUSED*/
static void
intEMT(int signo)
{
	char c, line[256];
	char *pline = line;
	char reply;

	read(fildes[0], &c, 1);
	while (c != '\n' && (size_t)(pline - line) < sizeof(line)) {
		*pline++ = c;
		read(fildes[0], &c, 1);
	}
	*pline = '\0';
	if (boolean(value(SCRIPT)) && fscript != NULL)
		fclose(fscript);
	if (pline == line) {
		setboolean(value(SCRIPT), FALSE);
		reply = 'y';
	} else {
		if ((fscript = fopen(line, "a")) == NULL)
			reply = 'n';
		else {
			reply = 'y';
			setboolean(value(SCRIPT), TRUE);
		}
	}
	write(repdes[1], &reply, 1);
	longjmp(sigbuf, 1);
}

static void
intTERM(int signo)
{
	if (boolean(value(SCRIPT)) && fscript != NULL)
		fclose(fscript);
	if (signo && tipin_pid)
		kill(tipin_pid, signo);
	exit(0);
}

/*ARGSUSED*/
static void
intSYS(int signo)
{
	setboolean(value(BEAUTIFY), !boolean(value(BEAUTIFY)));
	longjmp(sigbuf, 1);
}

/*
 * ****TIPOUT   TIPOUT****
 */
void
tipout(void)
{
	char buf[BUFSIZ];
	char *cp;
	ssize_t scnt;
	size_t cnt;
	sigset_t mask, omask;

	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGEMT, intEMT);		/* attention from TIPIN */
	signal(SIGTERM, intTERM);	/* time to go signal */
	signal(SIGIOT, intIOT);		/* scripting going on signal */
	signal(SIGHUP, intTERM);	/* for dial-ups */
	signal(SIGSYS, intSYS);		/* beautify toggle */
	(void) setjmp(sigbuf);
	sigprocmask(SIG_BLOCK, NULL, &omask);
	for (;;) {
		sigprocmask(SIG_SETMASK, &omask, NULL);
		scnt = read(FD, buf, BUFSIZ);
		if (scnt <= 0) {
			/* lost carrier */
			if (scnt == 0 ||
			    (scnt < 0 && (errno == EIO || errno == ENXIO))) {
				sigemptyset(&mask);
				sigaddset(&mask, SIGTERM);
				sigprocmask(SIG_BLOCK, &mask, NULL);
				intTERM(SIGHUP);
				/*NOTREACHED*/
			}
			continue;
		}
		cnt = scnt;
		sigemptyset(&mask);
		sigaddset(&mask, SIGEMT);
		sigaddset(&mask, SIGTERM);
		sigaddset(&mask, SIGIOT);
		sigaddset(&mask, SIGSYS);
		sigprocmask(SIG_BLOCK, &mask, NULL);
		for (cp = buf; cp < buf + cnt; cp++)
			*cp &= STRIP_PAR;
		write(STDOUT_FILENO, buf, cnt);
		if (boolean(value(SCRIPT)) && fscript != NULL) {
			if (!boolean(value(BEAUTIFY))) {
				fwrite(buf, 1, cnt, fscript);
			} else {
				for (cp = buf; cp < buf + cnt; cp++)
					if ((*cp >= ' ' && *cp <= '~') ||
					    any(*cp, value(EXCEPTIONS)))
						putc(*cp, fscript);
			}
			for (cp = buf; cp < buf + cnt; cp++) {
				if (!isgraph(*cp)) {
					fflush(fscript);
					break;
				}
			}
		}
	}
}
