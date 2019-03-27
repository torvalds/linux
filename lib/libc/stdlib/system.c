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
static char sccsid[] = "@(#)system.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <errno.h>
#include "un-namespace.h"
#include "libc_private.h"

#pragma weak system
int
system(const char *command)
{

	return (((int (*)(const char *))
	    __libc_interposing[INTERPOS_system])(command));
}

int
__libc_system(const char *command)
{
	pid_t pid, savedpid;
	int pstat;
	struct sigaction ign, intact, quitact;
	sigset_t newsigblock, oldsigblock;

	if (!command)		/* just checking... */
		return(1);

	(void)sigemptyset(&newsigblock);
	(void)sigaddset(&newsigblock, SIGCHLD);
	(void)sigaddset(&newsigblock, SIGINT);
	(void)sigaddset(&newsigblock, SIGQUIT);
	(void)__libc_sigprocmask(SIG_BLOCK, &newsigblock, &oldsigblock);
	switch(pid = vfork()) {
	/*
	 * In the child, use unwrapped syscalls.  libthr is in
	 * undefined state after vfork().
	 */
	case -1:			/* error */
		(void)__libc_sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
		return (-1);
	case 0:				/* child */
		/*
		 * Restore original signal dispositions and exec the command.
		 */
		(void)__sys_sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
		execl(_PATH_BSHELL, "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
	/* 
	 * If we are running means that the child has either completed
	 * its execve, or has failed.
	 * Block SIGINT/QUIT because sh -c handles it and wait for
	 * it to clean up.
	 */
	memset(&ign, 0, sizeof(ign));
	ign.sa_handler = SIG_IGN;
	(void)sigemptyset(&ign.sa_mask);
	(void)__libc_sigaction(SIGINT, &ign, &intact);
	(void)__libc_sigaction(SIGQUIT, &ign, &quitact);
	savedpid = pid;
	do {
		pid = _wait4(savedpid, &pstat, 0, (struct rusage *)0);
	} while (pid == -1 && errno == EINTR);
	(void)__libc_sigaction(SIGINT, &intact, NULL);
	(void)__libc_sigaction(SIGQUIT,  &quitact, NULL);
	(void)__libc_sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
	return (pid == -1 ? -1 : pstat);
}

__weak_reference(__libc_system, __system);
__weak_reference(__libc_system, _system);
