/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
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
__SCCSID("@(#)popen.c	8.3 (Berkeley) 5/3/95");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/wait.h>

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"

extern char **environ;

struct pid {
	SLIST_ENTRY(pid) next;
	FILE *fp;
	pid_t pid;
};
static SLIST_HEAD(, pid) pidlist = SLIST_HEAD_INITIALIZER(pidlist);
static pthread_mutex_t pidlist_mutex = PTHREAD_MUTEX_INITIALIZER;

#define	THREAD_LOCK()	if (__isthreaded) _pthread_mutex_lock(&pidlist_mutex)
#define	THREAD_UNLOCK()	if (__isthreaded) _pthread_mutex_unlock(&pidlist_mutex)

FILE *
popen(const char *command, const char *type)
{
	struct pid *cur;
	FILE *iop;
	int pdes[2], pid, twoway, cloexec;
	int pdes_unused_in_parent;
	char *argv[4];
	struct pid *p;

	cloexec = strchr(type, 'e') != NULL;
	/*
	 * Lite2 introduced two-way popen() pipes using _socketpair().
	 * FreeBSD's pipe() is bidirectional, so we use that.
	 */
	if (strchr(type, '+')) {
		twoway = 1;
		type = "r+";
	} else  {
		twoway = 0;
		if ((*type != 'r' && *type != 'w') ||
		    (type[1] && (type[1] != 'e' || type[2])))
			return (NULL);
	}
	if (pipe2(pdes, O_CLOEXEC) < 0)
		return (NULL);

	if ((cur = malloc(sizeof(struct pid))) == NULL) {
		(void)_close(pdes[0]);
		(void)_close(pdes[1]);
		return (NULL);
	}

	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		pdes_unused_in_parent = pdes[1];
	} else {
		iop = fdopen(pdes[1], type);
		pdes_unused_in_parent = pdes[0];
	}
	if (iop == NULL) {
		(void)_close(pdes[0]);
		(void)_close(pdes[1]);
		free(cur);
		return (NULL);
	}

	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = (char *)command;
	argv[3] = NULL;

	THREAD_LOCK();
	switch (pid = vfork()) {
	case -1:			/* Error. */
		THREAD_UNLOCK();
		/*
		 * The _close() closes the unused end of pdes[], while
		 * the fclose() closes the used end of pdes[], *and* cleans
		 * up iop.
		 */
		(void)_close(pdes_unused_in_parent);
		free(cur);
		(void)fclose(iop);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
		if (*type == 'r') {
			/*
			 * The _dup2() to STDIN_FILENO is repeated to avoid
			 * writing to pdes[1], which might corrupt the
			 * parent's copy.  This isn't good enough in
			 * general, since the _exit() is no return, so
			 * the compiler is free to corrupt all the local
			 * variables.
			 */
			if (pdes[1] != STDOUT_FILENO) {
				(void)_dup2(pdes[1], STDOUT_FILENO);
				if (twoway)
					(void)_dup2(STDOUT_FILENO, STDIN_FILENO);
			} else if (twoway && (pdes[1] != STDIN_FILENO)) {
				(void)_dup2(pdes[1], STDIN_FILENO);
				(void)_fcntl(pdes[1], F_SETFD, 0);
			} else
				(void)_fcntl(pdes[1], F_SETFD, 0);
		} else {
			if (pdes[0] != STDIN_FILENO) {
				(void)_dup2(pdes[0], STDIN_FILENO);
			} else
				(void)_fcntl(pdes[0], F_SETFD, 0);
		}
		SLIST_FOREACH(p, &pidlist, next)
			(void)_close(fileno(p->fp));
		_execve(_PATH_BSHELL, argv, environ);
		_exit(127);
		/* NOTREACHED */
	}
	THREAD_UNLOCK();

	/* Parent. */
	(void)_close(pdes_unused_in_parent);

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid = pid;
	THREAD_LOCK();
	SLIST_INSERT_HEAD(&pidlist, cur, next);
	THREAD_UNLOCK();

	/*
	 * To guard against undesired fd passing with concurrent calls,
	 * only clear the close-on-exec flag after linking the file into
	 * the list which will cause an explicit close.
	 */
	if (!cloexec)
		(void)_fcntl(*type == 'r' ? pdes[0] : pdes[1], F_SETFD, 0);

	return (iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int
pclose(FILE *iop)
{
	struct pid *cur, *last = NULL;
	int pstat;
	pid_t pid;

	/*
	 * Find the appropriate file pointer and remove it from the list.
	 */
	THREAD_LOCK();
	SLIST_FOREACH(cur, &pidlist, next) {
		if (cur->fp == iop)
			break;
		last = cur;
	}
	if (cur == NULL) {
		THREAD_UNLOCK();
		return (-1);
	}
	if (last == NULL)
		SLIST_REMOVE_HEAD(&pidlist, next);
	else
		SLIST_REMOVE_AFTER(last, next);
	THREAD_UNLOCK();

	(void)fclose(iop);

	do {
		pid = _wait4(cur->pid, &pstat, 0, (struct rusage *)0);
	} while (pid == -1 && errno == EINTR);

	free(cur);

	return (pid == -1 ? -1 : pstat);
}
