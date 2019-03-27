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
static char sccsid[] = "@(#)popen.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rcv.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include "extern.h"

#define READ 0
#define WRITE 1

struct fp {
	FILE	*fp;
	int	pipe;
	pid_t	pid;
	struct	fp *link;
};
static struct fp *fp_head;

struct child {
	pid_t	pid;
	char	done;
	char	free;
	int	status;
	struct	child *link;
};
static struct child *child, *child_freelist = NULL;

static void delchild(struct child *);
static pid_t file_pid(FILE *);
static pid_t start_commandv(char *, sigset_t *, int, int, va_list);

FILE *
Fopen(const char *path, const char *mode)
{
	FILE *fp;

	if ((fp = fopen(path, mode)) != NULL) {
		register_file(fp, 0, 0);
		(void)fcntl(fileno(fp), F_SETFD, 1);
	}
	return (fp);
}

FILE *
Fdopen(int fd, const char *mode)
{
	FILE *fp;

	if ((fp = fdopen(fd, mode)) != NULL) {
		register_file(fp, 0, 0);
		(void)fcntl(fileno(fp), F_SETFD, 1);
	}
	return (fp);
}

int
Fclose(FILE *fp)
{

	unregister_file(fp);
	return (fclose(fp));
}

FILE *
Popen(char *cmd, const char *mode)
{
	int p[2];
	int myside, hisside, fd0, fd1;
	pid_t pid;
	sigset_t nset;
	FILE *fp;

	if (pipe(p) < 0)
		return (NULL);
	(void)fcntl(p[READ], F_SETFD, 1);
	(void)fcntl(p[WRITE], F_SETFD, 1);
	if (*mode == 'r') {
		myside = p[READ];
		hisside = fd0 = fd1 = p[WRITE];
	} else {
		myside = p[WRITE];
		hisside = fd0 = p[READ];
		fd1 = -1;
	}
	(void)sigemptyset(&nset);
	pid = start_command(value("SHELL"), &nset, fd0, fd1, "-c", cmd, NULL);
	if (pid < 0) {
		(void)close(p[READ]);
		(void)close(p[WRITE]);
		return (NULL);
	}
	(void)close(hisside);
	if ((fp = fdopen(myside, mode)) != NULL)
		register_file(fp, 1, pid);
	return (fp);
}

int
Pclose(FILE *ptr)
{
	int i;
	sigset_t nset, oset;

	i = file_pid(ptr);
	unregister_file(ptr);
	(void)fclose(ptr);
	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, SIGINT);
	(void)sigaddset(&nset, SIGHUP);
	(void)sigprocmask(SIG_BLOCK, &nset, &oset);
	i = wait_child(i);
	(void)sigprocmask(SIG_SETMASK, &oset, NULL);
	return (i);
}

void
close_all_files(void)
{

	while (fp_head != NULL)
		if (fp_head->pipe)
			(void)Pclose(fp_head->fp);
		else
			(void)Fclose(fp_head->fp);
}

void
register_file(FILE *fp, int pipe, pid_t pid)
{
	struct fp *fpp;

	if ((fpp = malloc(sizeof(*fpp))) == NULL)
		err(1, "Out of memory");
	fpp->fp = fp;
	fpp->pipe = pipe;
	fpp->pid = pid;
	fpp->link = fp_head;
	fp_head = fpp;
}

void
unregister_file(FILE *fp)
{
	struct fp **pp, *p;

	for (pp = &fp_head; (p = *pp) != NULL; pp = &p->link)
		if (p->fp == fp) {
			*pp = p->link;
			(void)free(p);
			return;
		}
	errx(1, "Invalid file pointer");
	/*NOTREACHED*/
}

pid_t
file_pid(FILE *fp)
{
	struct fp *p;

	for (p = fp_head; p != NULL; p = p->link)
		if (p->fp == fp)
			return (p->pid);
	errx(1, "Invalid file pointer");
	/*NOTREACHED*/
}

/*
 * Run a command without a shell, with optional arguments and splicing
 * of stdin (-1 means none) and stdout.  The command name can be a sequence
 * of words.
 * Signals must be handled by the caller.
 * "nset" contains the signals to ignore in the new process.
 * SIGINT is enabled unless it's in "nset".
 */
static pid_t
start_commandv(char *cmd, sigset_t *nset, int infd, int outfd, va_list args)
{
	pid_t pid;

	if ((pid = fork()) < 0) {
		warn("fork");
		return (-1);
	}
	if (pid == 0) {
		char *argv[100];
		int i = getrawlist(cmd, argv, sizeof(argv) / sizeof(*argv));

		while ((argv[i++] = va_arg(args, char *)))
			;
		argv[i] = NULL;
		prepare_child(nset, infd, outfd);
		execvp(argv[0], argv);
		warn("%s", argv[0]);
		_exit(1);
	}
	return (pid);
}

int
run_command(char *cmd, sigset_t *nset, int infd, int outfd, ...)
{
	pid_t pid;
	va_list args;

	va_start(args, outfd);
	pid = start_commandv(cmd, nset, infd, outfd, args);
	va_end(args);
	if (pid < 0)
		return -1;
	return wait_command(pid);
}

int
start_command(char *cmd, sigset_t *nset, int infd, int outfd, ...)
{
	va_list args;
	int r;

	va_start(args, outfd);
	r = start_commandv(cmd, nset, infd, outfd, args);
	va_end(args);
	return r;
}

void
prepare_child(sigset_t *nset, int infd, int outfd)
{
	int i;
	sigset_t eset;

	/*
	 * All file descriptors other than 0, 1, and 2 are supposed to be
	 * close-on-exec.
	 */
	if (infd >= 0)
		dup2(infd, 0);
	if (outfd >= 0)
		dup2(outfd, 1);
	for (i = 1; i < NSIG; i++)
		if (nset != NULL && sigismember(nset, i))
			(void)signal(i, SIG_IGN);
	if (nset == NULL || !sigismember(nset, SIGINT))
		(void)signal(SIGINT, SIG_DFL);
	(void)sigemptyset(&eset);
	(void)sigprocmask(SIG_SETMASK, &eset, NULL);
}

int
wait_command(pid_t pid)
{

	if (wait_child(pid) < 0) {
		printf("Fatal error in process.\n");
		return (-1);
	}
	return (0);
}

static struct child *
findchild(pid_t pid, int dont_alloc)
{
	struct child **cpp;

	for (cpp = &child; *cpp != NULL && (*cpp)->pid != pid;
	    cpp = &(*cpp)->link)
			;
	if (*cpp == NULL) {
		if (dont_alloc)
			return(NULL);
		if (child_freelist) {
			*cpp = child_freelist;
			child_freelist = (*cpp)->link;
		} else {
			*cpp = malloc(sizeof(struct child));
			if (*cpp == NULL)
				err(1, "malloc");
		}
		(*cpp)->pid = pid;
		(*cpp)->done = (*cpp)->free = 0;
		(*cpp)->link = NULL;
	}
	return (*cpp);
}

static void
delchild(struct child *cp)
{
	struct child **cpp;

	for (cpp = &child; *cpp != cp; cpp = &(*cpp)->link)
		;
	*cpp = cp->link;
	cp->link = child_freelist;
	child_freelist = cp;
}

/*ARGSUSED*/
void
sigchild(int signo __unused)
{
	pid_t pid;
	int status;
	struct child *cp;
	int save_errno;

	save_errno = errno;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		cp = findchild(pid, 1);
		if (cp == NULL)
			continue;
		if (cp->free)
			delchild(cp);
		else {
			cp->done = 1;
			cp->status = status;
		}
	}
	errno = save_errno;
}

int wait_status;

/*
 * Wait for a specific child to die.
 */
int
wait_child(pid_t pid)
{
	struct child *cp;
	sigset_t nset, oset;
	pid_t rv = 0;

	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, SIGCHLD);
	(void)sigprocmask(SIG_BLOCK, &nset, &oset);
	/*
	 * If we have not already waited on the pid (via sigchild)
	 * wait on it now.  Otherwise, use the wait status stashed
	 * by sigchild.
	 */
	cp = findchild(pid, 1);
	if (cp == NULL || !cp->done)
		rv = waitpid(pid, &wait_status, 0);
	else
		wait_status = cp->status;
	if (cp != NULL)
		delchild(cp);
	(void)sigprocmask(SIG_SETMASK, &oset, NULL);
	if (rv == -1 || (WIFEXITED(wait_status) && WEXITSTATUS(wait_status)))
		return -1;
	else
		return 0;
}

/*
 * Mark a child as don't care.
 */
void
free_child(pid_t pid)
{
	struct child *cp;
	sigset_t nset, oset;

	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, SIGCHLD);
	(void)sigprocmask(SIG_BLOCK, &nset, &oset);
	if ((cp = findchild(pid, 0)) != NULL) {
		if (cp->done)
			delchild(cp);
		else
			cp->free = 1;
	}
	(void)sigprocmask(SIG_SETMASK, &oset, NULL);
}
