/*	$OpenBSD: passwd.c,v 1.56 2019/06/28 13:32:43 deraadt Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994, 1995
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <limits.h>

#include "util.h"

static char pw_defdir[] = "/etc";
static char *pw_dir = pw_defdir;
static char *pw_lck;

char *
pw_file(const char *nm)
{
	const char *p = strrchr(nm, '/');
	char *new_nm;

	if (p)
		p++;
	else
		p = nm;

	if (asprintf(&new_nm, "%s/%s", pw_dir, p) == -1)
		return NULL;
	return new_nm;
}

void
pw_setdir(const char *dir)
{
	char *p;

	if (strcmp (dir, pw_dir) == 0)
		return;
	if (pw_dir != pw_defdir)
		free(pw_dir);
	pw_dir = strdup(dir);
	if (pw_lck) {
		p = pw_file(pw_lck);
		free(pw_lck);
		pw_lck = p;
	}
}


int
pw_lock(int retries)
{
	int i, fd;
	mode_t old_mode;

	if (!pw_lck) {
		errno = EINVAL;
		return (-1);
	}
	/* Acquire the lock file.  */
	old_mode = umask(0);
	fd = open(pw_lck, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600);
	for (i = 0; i < retries && fd == -1 && errno == EEXIST; i++) {
		sleep(1);
		fd = open(pw_lck, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600);
	}
	(void) umask(old_mode);
	return (fd);
}

int
pw_mkdb(char *username, int flags)
{
	int pstat, ac;
	pid_t pid;
	char *av[8];
	struct stat sb;

	if (pw_lck == NULL)
		return(-1);

	/* A zero length passwd file is never ok */
	if (stat(pw_lck, &sb) == 0 && sb.st_size == 0) {
		warnx("%s is zero length", pw_lck);
		return (-1);
	}

	ac = 0;
	av[ac++] = "pwd_mkdb";
	av[ac++] = "-d";
	av[ac++] = pw_dir;
	if (flags & _PASSWORD_SECUREONLY)
		av[ac++] = "-s";
	else if (!(flags & _PASSWORD_OMITV7))
		av[ac++] = "-p";
	if (username) {
		av[ac++] = "-u";
		av[ac++] = username;
	}
	av[ac++] = pw_lck;
	av[ac] = NULL;

	pid = vfork();
	if (pid == -1)
		return (-1);
	if (pid == 0) {
		if (pw_lck)
			execv(_PATH_PWD_MKDB, av);
		_exit(1);
	}
	pid = waitpid(pid, &pstat, 0);
	if (pid == -1 || !WIFEXITED(pstat) || WEXITSTATUS(pstat) != 0)
		return (-1);
	return (0);
}

int
pw_abort(void)
{
	return (pw_lck ? unlink(pw_lck) : -1);
}

/* Everything below this point is intended for the convenience of programs
 * which allow a user to interactively edit the passwd file.  Errors in the
 * routines below will cause the process to abort. */

static pid_t editpid = -1;

static void
pw_cont(int signo)
{
	int save_errno = errno;

	if (editpid != -1)
		kill(editpid, signo);
	errno = save_errno;
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

	if (!pw_lck)
		pw_lck = pw_file(_PATH_MASTERPASSWD_LOCK);
}

void
pw_edit(int notsetuid, const char *filename)
{
	int pstat;
	char *p;
	char *editor;
	char *argp[] = {"sh", "-c", NULL, NULL};

	if (!filename) {
		filename = pw_lck;
		if (!filename)
			return;
	}

	if ((editor = getenv("EDITOR")) == NULL)
		editor = _PATH_VI;

	if (asprintf(&p, "%s %s", editor, filename) == -1)
		return;
	argp[2] = p;

	switch (editpid = vfork()) {
	case -1:			/* error */
		free(p);
		return;
	case 0:				/* child */
		if (notsetuid) {
			setgid(getgid());
			setuid(getuid());
		}
		execv(_PATH_BSHELL, argp);
		_exit(127);
	}

	free(p);
	for (;;) {
		editpid = waitpid(editpid, (int *)&pstat, WUNTRACED);
		if (editpid == -1)
			pw_error(editor, 1, 1);
		else if (WIFSTOPPED(pstat))
			raise(WSTOPSIG(pstat));
		else if (WIFEXITED(pstat) && WEXITSTATUS(pstat) == 0)
			break;
		else
			pw_error(editor, 1, 1);
	}
	editpid = -1;
}

void
pw_prompt(void)
{
	int first, c;

	(void)printf("re-edit the password file? [y]: ");
	(void)fflush(stdout);
	first = c = getchar();
	while (c != '\n' && c != EOF)
		c = getchar();
	switch (first) {
	case EOF:
		putchar('\n');
		/* FALLTHROUGH */
	case 'n':
	case 'N':
		pw_error(NULL, 0, 0);
		break;
	}
}

static int
pw_equal(const struct passwd *pw1, const struct passwd *pw2)
{
	return (strcmp(pw1->pw_name, pw2->pw_name) == 0 &&
	    pw1->pw_uid == pw2->pw_uid &&
	    pw1->pw_gid == pw2->pw_gid &&
	    strcmp(pw1->pw_class, pw2->pw_class) == 0 &&
	    pw1->pw_change == pw2->pw_change &&
	    pw1->pw_expire == pw2->pw_expire &&
	    strcmp(pw1->pw_gecos, pw2->pw_gecos) == 0 &&
	    strcmp(pw1->pw_dir, pw2->pw_dir) == 0 &&
	    strcmp(pw1->pw_shell, pw2->pw_shell) == 0);
}

static int
pw_write_entry(FILE *to, const struct passwd *pw)
{
	char	gidstr[16], uidstr[16];

	/* Preserve gid/uid -1 */
	if (pw->pw_gid == (gid_t)-1)
		strlcpy(gidstr, "-1", sizeof(gidstr));
	else
		snprintf(gidstr, sizeof(gidstr), "%u", (u_int)pw->pw_gid);

	if (pw->pw_uid == (uid_t)-1)
		strlcpy(uidstr, "-1", sizeof(uidstr));
	else
		snprintf(uidstr, sizeof(uidstr), "%u", (u_int)pw->pw_uid);

	return fprintf(to, "%s:%s:%s:%s:%s:%lld:%lld:%s:%s:%s\n",
	    pw->pw_name, pw->pw_passwd, uidstr, gidstr, pw->pw_class,
	    (long long)pw->pw_change, (long long)pw->pw_expire,
	    pw->pw_gecos, pw->pw_dir, pw->pw_shell);
}

void
pw_copy(int ffd, int tfd, const struct passwd *pw, const struct passwd *opw)
{
	struct passwd tpw;
	FILE   *from, *to;
	int	done;
	char   *p, *ep, buf[8192];
	char   *master = pw_file(_PATH_MASTERPASSWD);

	if (!master)
		pw_error(NULL, 0, 1);
	if (!(from = fdopen(ffd, "r")))
		pw_error(master, 1, 1);
	if (!(to = fdopen(tfd, "w")))
		pw_error(pw_lck ? pw_lck : NULL, pw_lck ? 1 : 0, 1);

	for (done = 0; fgets(buf, (int)sizeof(buf), from);) {
		if ((ep = strchr(buf, '\n')) == NULL) {
			warnx("%s: line too long", master);
			pw_error(NULL, 0, 1);
		}
		if (done) {
			if (fputs(buf, to))
				goto fail;
			continue;
		}
		if (!(p = strchr(buf, ':'))) {
			warnx("%s: corrupted entry", master);
			pw_error(NULL, 0, 1);
		}
		*p = '\0';
		if (strcmp(buf, opw ? opw->pw_name : pw->pw_name)) {
			*p = ':';
			if (fputs(buf, to))
				goto fail;
			continue;
		}
		if (opw != NULL) {
			*p = ':';
			*ep = '\0';
			if (!pw_scan(buf, &tpw, NULL))
				pw_error(NULL, 0, 1);
			if (!pw_equal(&tpw, opw)) {
				warnx("%s: inconsistent entry", master);
				pw_error(NULL, 0, 1);
			}
		}
		if (pw_write_entry(to, pw) == -1)
			goto fail;
		done = 1;
	}
	if (!done && pw_write_entry(to, pw) == -1)
		goto fail;

	if (ferror(to) || fflush(to))
fail:
		pw_error(NULL, 0, 1);
	free(master);
	(void)fclose(to);
}

int
pw_scan(char *bp, struct passwd *pw, int *flags)
{
	int root;
	char *p, *sh;
	const char *errstr;

	if (flags != NULL)
		*flags = 0;

	if (!(p = strsep(&bp, ":")) || *p == '\0')	/* login */
		goto fmt;
	pw->pw_name = p;
	root = !strcmp(pw->pw_name, "root");

	if (!(pw->pw_passwd = strsep(&bp, ":")))	/* passwd */
		goto fmt;

	if (!(p = strsep(&bp, ":")))			/* uid */
		goto fmt;
	pw->pw_uid = strtonum(p, -1, UID_MAX, &errstr);
	if (errstr != NULL) {
		if (*p != '\0') {
			warnx("uid is %s", errstr);
			return (0);
		}
		if (flags != NULL)
			*flags |= _PASSWORD_NOUID;
	}
	if (root && pw->pw_uid) {
		warnx("root uid should be 0");
		return (0);
	}

	if (!(p = strsep(&bp, ":")))			/* gid */
		goto fmt;
	pw->pw_gid = strtonum(p, -1, GID_MAX, &errstr);
	if (errstr != NULL) {
		if (*p != '\0') {
			warnx("gid is %s", errstr);
			return (0);
		}
		if (flags != NULL)
			*flags |= _PASSWORD_NOGID;
	}

	pw->pw_class = strsep(&bp, ":");		/* class */
	if (!(p = strsep(&bp, ":")))			/* change */
		goto fmt;
	pw->pw_change = atoll(p);
	if ((*p == '\0') && (flags != (int *)NULL))
		*flags |= _PASSWORD_NOCHG;
	if (!(p = strsep(&bp, ":")))			/* expire */
		goto fmt;
	pw->pw_expire = atoll(p);
	if ((*p == '\0') && (flags != (int *)NULL))
		*flags |= _PASSWORD_NOEXP;
	pw->pw_gecos = strsep(&bp, ":");		/* gecos */
	pw->pw_dir = strsep(&bp, ":");			/* directory */
	if (!(pw->pw_shell = strsep(&bp, ":")))		/* shell */
		goto fmt;

	p = pw->pw_shell;
	if (root && *p) {				/* empty == /bin/sh */
		for (setusershell();;) {
			if (!(sh = getusershell())) {
				warnx("warning, unknown root shell");
				break;
			}
			if (!strcmp(p, sh))
				break;
		}
		endusershell();
	}

	if ((p = strsep(&bp, ":"))) {			/* too many */
fmt:		warnx("corrupted entry");
		return (0);
	}

	return (1);
}

__dead void
pw_error(const char *name, int error, int eval)
{
	char   *master = pw_file(_PATH_MASTERPASSWD);

	if (error) {
		if (name)
			warn("%s", name);
		else
			warn(NULL);
	}
	if (master) {
		warnx("%s: unchanged", master);
		free(master);
	}

	pw_abort();
	exit(eval);
}
