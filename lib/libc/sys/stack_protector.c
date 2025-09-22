/*	$OpenBSD: stack_protector.c,v 1.24 2017/11/29 05:13:57 guenther Exp $	*/

/*
 * Copyright (c) 2002 Hiroaki Etoh, Federico G. Schwindt, and Miodrag Vallat.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/*
 * Note: test below is for PIC not __PIC__.  This code must only be included
 * in the shared library and not in libc.a, but __PIC__ is set for libc.a
 * objects where PIE is supported
 *
 * XXX would this work? #if defined(__PIC__) && !defined(__PIE__)
 * XXX any archs which are always PIC (like mips64) but don't have PIE?
 */
#ifdef PIC
#include <../csu/os-note-elf.h>

long __guard_local __dso_hidden __attribute__((section(".openbsd.randomdata")));
#endif /* PIC */

void
__stack_smash_handler(const char func[], int damaged)
{
	struct sigaction sa;
	sigset_t mask;
	char buf[1024];

	/* Immediately block all signal handlers from running code */
	sigfillset(&mask);
	sigdelset(&mask, SIGABRT);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	/* <10> is LOG_CRIT */
	strlcpy(buf, "<10>", sizeof buf);

	/* Make sure progname does not fill the whole buffer */
	strlcat(buf, __progname, sizeof(buf) / 2 );

	strlcat(buf, ": stack overflow in function ", sizeof buf);
	strlcat(buf, func, sizeof buf);

	sendsyslog(buf, strlen(buf), LOG_CONS);

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_DFL;
	sigaction(SIGABRT, &sa, NULL);

	thrkill(0, SIGABRT, NULL);

	_exit(127);
}
DEF_BUILTIN(__stack_smash_handler);
