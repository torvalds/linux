/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1990, 1993, 1994
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
static const char sccsid[] = "@(#)pw_util.c	8.3 (Berkeley) 4/2/94";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * This file is used by all the "password" programs; vipw(8), chpass(1),
 * and passwd(1).
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pw_util.h"

extern char *tempname;
static pid_t editpid = -1;
static int lockfd;
static char _default_editor[] = _PATH_VI;
static char _default_mppath[] = _PATH_PWD;
static char _default_masterpasswd[] = _PATH_MASTERPASSWD;
char *mppath = _default_mppath;
char *masterpasswd = _default_masterpasswd;

void		 pw_cont(int);

void
pw_cont(int sig)
{

	if (editpid != -1)
		kill(editpid, sig);
}

void
pw_init(void)
{
	struct rlimit rlim;

	/* Unlimited resource limits. */
	rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
	(void)setrlimit(RLIMIT_CPU, &rlim);
	(void)setrlimit(RLIMIT_FSIZE, &rlim);
	(void)setrlimit(RLIMIT_STACK, &rlim);
	(void)setrlimit(RLIMIT_DATA, &rlim);
	(void)setrlimit(RLIMIT_RSS, &rlim);

	/* Don't drop core (not really necessary, but GP's). */
	rlim.rlim_cur = rlim.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rlim);

	/* Turn off signals. */
	(void)signal(SIGALRM, SIG_IGN);
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTERM, SIG_IGN);
	(void)signal(SIGCONT, pw_cont);

	/* Create with exact permissions. */
	(void)umask(0);
}

int
pw_lock(void)
{
	/*
	 * If the master password file doesn't exist, the system is hosed.
	 * Might as well try to build one.  Set the close-on-exec bit so
	 * that users can't get at the encrypted passwords while editing.
	 * Open should allow flock'ing the file; see 4.4BSD.	XXX
	 */
	for (;;) {
		struct stat st;

		lockfd = open(masterpasswd, O_RDONLY, 0);
		if (lockfd < 0 || fcntl(lockfd, F_SETFD, 1) == -1)
			err(1, "%s", masterpasswd);
		if (flock(lockfd, LOCK_EX|LOCK_NB))
			errx(1, "the password db file is busy");

		/*
		 * If the password file was replaced while we were trying to
		 * get the lock, our hardlink count will be 0 and we have to
		 * close and retry.
		 */
		if (fstat(lockfd, &st) < 0)
			errx(1, "fstat() failed");
		if (st.st_nlink != 0)
			break;
		close(lockfd);
		lockfd = -1;
	}
	return (lockfd);
}

int
pw_tmp(void)
{
	static char path[MAXPATHLEN];
	int fd;
	char *p;

	strncpy(path, masterpasswd, MAXPATHLEN - 1);
	path[MAXPATHLEN] = '\0';

	if ((p = strrchr(path, '/')))
		++p;
	else
		p = path;
	strcpy(p, "pw.XXXXXX");
	if ((fd = mkstemp(path)) == -1)
		err(1, "%s", path);
	tempname = path;
	return (fd);
}

int
pw_mkdb(const char *username)
{
	int pstat;
	pid_t pid;

	(void)fflush(stderr);
	if (!(pid = fork())) {
		if(!username) {
			warnx("rebuilding the database...");
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p", "-d", mppath,
			    tempname, (char *)NULL);
		} else {
			warnx("updating the database...");
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p", "-d", mppath,
			    "-u", username, tempname, (char *)NULL);
		}
		pw_error(_PATH_PWD_MKDB, 1, 1);
	}
	pid = waitpid(pid, &pstat, 0);
	if (pid == -1 || !WIFEXITED(pstat) || WEXITSTATUS(pstat) != 0)
		return (0);
	warnx("done");
	return (1);
}

void
pw_edit(int notsetuid)
{
	int pstat;
	char *p, *editor;

	if (!(editor = getenv("EDITOR")))
		editor = _default_editor;
	if ((p = strrchr(editor, '/')))
		++p;
	else
		p = editor;

	if (!(editpid = fork())) {
		if (notsetuid) {
			(void)setgid(getgid());
			(void)setuid(getuid());
		}
		errno = 0;
		execlp(editor, p, tempname, (char *)NULL);
		_exit(errno);
	}
	for (;;) {
		editpid = waitpid(editpid, (int *)&pstat, WUNTRACED);
		errno = WEXITSTATUS(pstat);
		if (editpid == -1)
			pw_error(editor, 1, 1);
		else if (WIFSTOPPED(pstat))
			raise(WSTOPSIG(pstat));
		else if (WIFEXITED(pstat) && errno == 0)
			break;
		else
			pw_error(editor, 1, 1);
	}
	editpid = -1;
}

void
pw_prompt(void)
{
	int c, first;

	(void)printf("re-edit the password file? [y]: ");
	(void)fflush(stdout);
	first = c = getchar();
	while (c != '\n' && c != EOF)
		c = getchar();
	if (first == 'n')
		pw_error(NULL, 0, 0);
}

void
pw_error(const char *name, int error, int eval)
{
	if (error) {
		if (name != NULL)
			warn("%s", name);
		else
			warn(NULL);
	}
	warnx("password information unchanged");
	(void)unlink(tempname);
	exit(eval);
}
