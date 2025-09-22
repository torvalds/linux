/*	$OpenBSD: jmptest.c,v 1.7 2003/09/02 23:52:16 david Exp $	*/
/*	$NetBSD: jmptest.c,v 1.2 1995/01/01 20:55:35 jtc Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

#include <sys/types.h>
#include <err.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if (TEST_SETJMP + TEST_U_SETJMP + TEST_SIGSETJMP) != 1
#error one of TEST_SETJMP, TEST_U_SETJMP, or TEST_SIGSETJMP must be defined
#endif

#ifdef TEST_SETJMP
#define BUF		jmp_buf
#define	SET(b, m)	setjmp(b)
#define	JMP(b, v)	longjmp(b, v)
#endif

#ifdef TEST_U_SETJMP
#define BUF		jmp_buf
#define	SET(b, m)	_setjmp(b)
#define	JMP(b, v)	_longjmp(b, v)
#endif

#ifdef TEST_SIGSETJMP
#define BUF		sigjmp_buf
#define	SET(b, m)	sigsetjmp(b, m)
#define	JMP(b, v)	siglongjmp(b, v)
#endif

int expectsignal;

static void
aborthandler(int signo)
{

	if (expectsignal)
		_exit(0);
	else {
		warnx("kill(SIGABRT) succeeded");
		_exit(1);
	}
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	BUF jb;
	sigset_t ss;
	int i, x;

	i = getpid();

#ifdef TEST_SETJMP
	expectsignal = 0;
#endif
#ifdef TEST_U_SETJMP
	expectsignal = 1;
#endif
#ifdef TEST_SIGSETJMP
	if (argc != 2 ||
	    (strcmp(argv[1], "save") && strcmp(argv[1], "nosave"))) {
		fprintf(stderr, "usage: %s [save|nosave]\n", argv[0]);
		exit(1);
	}
	expectsignal = (strcmp(argv[1], "save") != 0);
#endif

	sa.sa_handler = aborthandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGABRT, &sa, NULL) == -1)
		err(1, "sigaction failed");

	if (sigemptyset(&ss) == -1)
		err(1, "sigemptyset failed");
	if (sigaddset(&ss, SIGABRT) == -1)
		err(1, "sigaddset failed");
	if (sigprocmask(SIG_BLOCK, &ss, NULL) == -1)
		err(1, "sigprocmask (1) failed");

	x = SET(jb, !expectsignal);
	if (x != 0) {
		if (x != i)
			errx(1, "setjmp returned wrong value");

		kill(i, SIGABRT);
		if (expectsignal)
			errx(1, "kill(SIGABRT) failed");
		else
			exit(0);
	}

	if (sigprocmask(SIG_UNBLOCK, &ss, NULL) == -1)
		err(1, "sigprocmask (2) failed");

	JMP(jb, i);

	errx(1, "jmp failed");
}
