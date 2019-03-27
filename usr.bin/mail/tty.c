/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)tty.c	8.2 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Mail -- a mail program
 *
 * Generally useful tty stuff.
 */

#include "rcv.h"
#include "extern.h"

static	cc_t	c_erase;		/* Current erase char */
static	cc_t	c_kill;			/* Current kill char */
static	jmp_buf	rewrite;		/* Place to go when continued */
static	jmp_buf	intjmp;			/* Place to go when interrupted */
#ifndef TIOCSTI
static	int	ttyset;			/* We must now do erase/kill */
#endif

/*
 * Read all relevant header fields.
 */

int
grabh(struct header *hp, int gflags)
{
	struct termios ttybuf;
	sig_t saveint;
	sig_t savetstp;
	sig_t savettou;
	sig_t savettin;
	int errs;
#ifndef TIOCSTI
	sig_t savequit;
#else
# ifdef TIOCEXT
	int extproc, flag;
# endif /* TIOCEXT */
#endif /* TIOCSTI */

	savetstp = signal(SIGTSTP, SIG_DFL);
	savettou = signal(SIGTTOU, SIG_DFL);
	savettin = signal(SIGTTIN, SIG_DFL);
	errs = 0;
#ifndef TIOCSTI
	ttyset = 0;
#endif
	if (tcgetattr(fileno(stdin), &ttybuf) < 0) {
		warn("tcgetattr(stdin)");
		return (-1);
	}
	c_erase = ttybuf.c_cc[VERASE];
	c_kill = ttybuf.c_cc[VKILL];
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = _POSIX_VDISABLE;
	ttybuf.c_cc[VKILL] = _POSIX_VDISABLE;
	if ((saveint = signal(SIGINT, SIG_IGN)) == SIG_DFL)
		(void)signal(SIGINT, SIG_DFL);
	if ((savequit = signal(SIGQUIT, SIG_IGN)) == SIG_DFL)
		(void)signal(SIGQUIT, SIG_DFL);
#else
# ifdef		TIOCEXT
	extproc = ((ttybuf.c_lflag & EXTPROC) ? 1 : 0);
	if (extproc) {
		flag = 0;
		if (ioctl(fileno(stdin), TIOCEXT, &flag) < 0)
			warn("TIOCEXT: off");
	}
# endif	/* TIOCEXT */
	if (setjmp(intjmp))
		goto out;
	saveint = signal(SIGINT, ttyint);
#endif
	if (gflags & GTO) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_to != NULL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_to =
			extract(readtty("To: ", detract(hp->h_to, 0)), GTO);
	}
	if (gflags & GSUBJECT) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_subject != NULL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_subject = readtty("Subject: ", hp->h_subject);
	}
	if (gflags & GCC) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_cc != NULL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_cc =
			extract(readtty("Cc: ", detract(hp->h_cc, 0)), GCC);
	}
	if (gflags & GBCC) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_bcc != NULL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_bcc =
			extract(readtty("Bcc: ", detract(hp->h_bcc, 0)), GBCC);
	}
out:
	(void)signal(SIGTSTP, savetstp);
	(void)signal(SIGTTOU, savettou);
	(void)signal(SIGTTIN, savettin);
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = c_erase;
	ttybuf.c_cc[VKILL] = c_kill;
	if (ttyset)
		tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
	(void)signal(SIGQUIT, savequit);
#else
# ifdef		TIOCEXT
	if (extproc) {
		flag = 1;
		if (ioctl(fileno(stdin), TIOCEXT, &flag) < 0)
			warn("TIOCEXT: on");
	}
# endif	/* TIOCEXT */
#endif
	(void)signal(SIGINT, saveint);
	return (errs);
}

/*
 * Read up a header from standard input.
 * The source string has the preliminary contents to
 * be read.
 *
 */

char *
readtty(const char *pr, char src[])
{
	char ch, canonb[BUFSIZ];
	int c;
	char *cp, *cp2;

	fputs(pr, stdout);
	(void)fflush(stdout);
	if (src != NULL && strlen(src) > BUFSIZ - 2) {
		printf("too long to edit\n");
		return (src);
	}
#ifndef TIOCSTI
	if (src != NULL)
		strlcpy(canonb, src, sizeof(canonb));
	else
		*canonb = '\0';
	fputs(canonb, stdout);
	(void)fflush(stdout);
#else
	cp = src == NULL ? "" : src;
	while ((c = *cp++) != '\0') {
		if ((c_erase != _POSIX_VDISABLE && c == c_erase) ||
		    (c_kill != _POSIX_VDISABLE && c == c_kill)) {
			ch = '\\';
			ioctl(0, TIOCSTI, &ch);
		}
		ch = c;
		ioctl(0, TIOCSTI, &ch);
	}
	cp = canonb;
	*cp = '\0';
#endif
	cp2 = cp;
	while (cp2 < canonb + BUFSIZ)
		*cp2++ = '\0';
	cp2 = cp;
	if (setjmp(rewrite))
		goto redo;
	(void)signal(SIGTSTP, ttystop);
	(void)signal(SIGTTOU, ttystop);
	(void)signal(SIGTTIN, ttystop);
	clearerr(stdin);
	while (cp2 < canonb + BUFSIZ) {
		c = getc(stdin);
		if (c == EOF || c == '\n')
			break;
		*cp2++ = c;
	}
	*cp2 = '\0';
	(void)signal(SIGTSTP, SIG_DFL);
	(void)signal(SIGTTOU, SIG_DFL);
	(void)signal(SIGTTIN, SIG_DFL);
	if (c == EOF && ferror(stdin)) {
redo:
		cp = strlen(canonb) > 0 ? canonb : NULL;
		clearerr(stdin);
		return (readtty(pr, cp));
	}
#ifndef TIOCSTI
	if (cp == NULL || *cp == '\0')
		return (src);
	cp2 = cp;
	if (!ttyset)
		return (strlen(canonb) > 0 ? savestr(canonb) : NULL);
	while (*cp != '\0') {
		c = *cp++;
		if (c_erase != _POSIX_VDISABLE && c == c_erase) {
			if (cp2 == canonb)
				continue;
			if (cp2[-1] == '\\') {
				cp2[-1] = c;
				continue;
			}
			cp2--;
			continue;
		}
		if (c_kill != _POSIX_VDISABLE && c == c_kill) {
			if (cp2 == canonb)
				continue;
			if (cp2[-1] == '\\') {
				cp2[-1] = c;
				continue;
			}
			cp2 = canonb;
			continue;
		}
		*cp2++ = c;
	}
	*cp2 = '\0';
#endif
	if (equal("", canonb))
		return (NULL);
	return (savestr(canonb));
}

/*
 * Receipt continuation.
 */
void
ttystop(int s)
{
	sig_t old_action = signal(s, SIG_DFL);
	sigset_t nset;

	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, s);
	(void)sigprocmask(SIG_BLOCK, &nset, NULL);
	kill(0, s);
	(void)sigprocmask(SIG_UNBLOCK, &nset, NULL);
	(void)signal(s, old_action);
	longjmp(rewrite, 1);
}

void
ttyint(int s __unused)
{
	longjmp(intjmp, 1);
}
