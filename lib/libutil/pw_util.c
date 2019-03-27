/*--
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
__SCCSID("@(#)pw_util.c	8.3 (Berkeley) 4/2/94");

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

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libutil.h"

static pid_t editpid = -1;
static int lockfd = -1;
static char masterpasswd[PATH_MAX];
static char passwd_dir[PATH_MAX];
static char tempname[PATH_MAX];
static int initialized;

#if 0
void
pw_cont(int sig)
{

	if (editpid != -1)
		kill(editpid, sig);
}
#endif

/*
 * Initialize statics and set limits, signals & umask to try to avoid
 * interruptions, crashes etc. that might expose passord data.
 */
int
pw_init(const char *dir, const char *master)
{
#if 0
	struct rlimit rlim;
#endif

	if (dir == NULL) {
		strcpy(passwd_dir, _PATH_ETC);
	} else {
		if (strlen(dir) >= sizeof(passwd_dir)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(passwd_dir, dir);
	}

	if (master == NULL) {
		if (dir == NULL) {
			strcpy(masterpasswd, _PATH_MASTERPASSWD);
		} else if (snprintf(masterpasswd, sizeof(masterpasswd), "%s/%s",
		    passwd_dir, _MASTERPASSWD) > (int)sizeof(masterpasswd)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
	} else {
		if (strlen(master) >= sizeof(masterpasswd)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(masterpasswd, master);
	}

	/*
	 * The code that follows is extremely disruptive to the calling
	 * process, and is therefore disabled until someone can conceive
	 * of a realistic scenario where it would fend off a compromise.
	 * Race conditions concerning the temporary files can be guarded
	 * against in other ways than masking signals (by checking stat(2)
	 * results after creation).
	 */
#if 0
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
#endif
	initialized = 1;
	return (0);
}

/*
 * Lock the master password file.
 */
int
pw_lock(void)
{

	if (*masterpasswd == '\0')
		return (-1);

	/*
	 * If the master password file doesn't exist, the system is hosed.
	 * Might as well try to build one.  Set the close-on-exec bit so
	 * that users can't get at the encrypted passwords while editing.
	 * Open should allow flock'ing the file; see 4.4BSD.	XXX
	 */
	for (;;) {
		struct stat st;

		lockfd = flopen(masterpasswd, O_RDONLY|O_NONBLOCK|O_CLOEXEC, 0);
		if (lockfd == -1) {
			if (errno == EWOULDBLOCK) {
				errx(1, "the password db file is busy");
			} else {
				err(1, "could not lock the passwd file: ");
			}
		}

		/*
		 * If the password file was replaced while we were trying to
		 * get the lock, our hardlink count will be 0 and we have to
		 * close and retry.
		 */
		if (fstat(lockfd, &st) == -1)
			err(1, "fstat() failed: ");
		if (st.st_nlink != 0)
			break;
		close(lockfd);
		lockfd = -1;
	}
	return (lockfd);
}

/*
 * Create and open a presumably safe temp file for editing the password
 * data, and copy the master password file into it.
 */
int
pw_tmp(int mfd)
{
	char buf[8192];
	ssize_t nr;
	const char *p;
	int tfd;

	if (*masterpasswd == '\0')
		return (-1);
	if ((p = strrchr(masterpasswd, '/')))
		++p;
	else
		p = masterpasswd;
	if (snprintf(tempname, sizeof(tempname), "%.*spw.XXXXXX",
		(int)(p - masterpasswd), masterpasswd) >= (int)sizeof(tempname)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if ((tfd = mkostemp(tempname, 0)) == -1)
		return (-1);
	if (mfd != -1) {
		while ((nr = read(mfd, buf, sizeof(buf))) > 0)
			if (write(tfd, buf, (size_t)nr) != nr)
				break;
		if (nr != 0) {
			unlink(tempname);
			*tempname = '\0';
			close(tfd);
			return (-1);
		}
	}
	return (tfd);
}

/*
 * Regenerate the password database.
 */
int
pw_mkdb(const char *user)
{
	int pstat;
	pid_t pid;

	(void)fflush(stderr);
	switch ((pid = fork())) {
	case -1:
		return (-1);
	case 0:
		/* child */
		if (user == NULL)
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p",
			    "-d", passwd_dir, tempname, (char *)NULL);
		else
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p",
			    "-d", passwd_dir, "-u", user, tempname,
			    (char *)NULL);
		_exit(1);
		/* NOTREACHED */
	default:
		/* parent */
		break;
	}
	if (waitpid(pid, &pstat, 0) == -1)
		return (-1);
	if (WIFEXITED(pstat) && WEXITSTATUS(pstat) == 0)
		return (0);
	errno = 0;
	return (-1);
}

/*
 * Edit the temp file.  Return -1 on error, >0 if the file was modified, 0
 * if it was not.
 */
int
pw_edit(int notsetuid)
{
	struct sigaction sa, sa_int, sa_quit;
	sigset_t oldsigset, nsigset;
	struct stat st1, st2;
	const char *editor;
	int pstat;

	if ((editor = getenv("EDITOR")) == NULL)
		editor = _PATH_VI;
	if (stat(tempname, &st1) == -1)
		return (-1);
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, &sa_int);
	sigaction(SIGQUIT, &sa, &sa_quit);
	sigemptyset(&nsigset);
	sigaddset(&nsigset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nsigset, &oldsigset);
	switch ((editpid = fork())) {
	case -1:
		return (-1);
	case 0:
		sigaction(SIGINT, &sa_int, NULL);
		sigaction(SIGQUIT, &sa_quit, NULL);
		sigprocmask(SIG_SETMASK, &oldsigset, NULL);
		if (notsetuid) {
			(void)setgid(getgid());
			(void)setuid(getuid());
		}
		errno = 0;
		execlp(editor, editor, tempname, (char *)NULL);
		_exit(errno);
	default:
		/* parent */
		break;
	}
	for (;;) {
		if (waitpid(editpid, &pstat, WUNTRACED) == -1) {
			if (errno == EINTR)
				continue;
			unlink(tempname);
			editpid = -1;
			break;
		} else if (WIFSTOPPED(pstat)) {
			raise(WSTOPSIG(pstat));
		} else if (WIFEXITED(pstat) && WEXITSTATUS(pstat) == 0) {
			editpid = -1;
			break;
		} else {
			unlink(tempname);
			editpid = -1;
			break;
		}
	}
	sigaction(SIGINT, &sa_int, NULL);
	sigaction(SIGQUIT, &sa_quit, NULL);
	sigprocmask(SIG_SETMASK, &oldsigset, NULL);
	if (stat(tempname, &st2) == -1)
		return (-1);
	return (st1.st_mtim.tv_sec != st2.st_mtim.tv_sec ||
	    st1.st_mtim.tv_nsec != st2.st_mtim.tv_nsec);
}

/*
 * Clean up.  Preserve errno for the caller's convenience.
 */
void
pw_fini(void)
{
	int serrno, status;

	if (!initialized)
		return;
	initialized = 0;
	serrno = errno;
	if (editpid != -1) {
		kill(editpid, SIGTERM);
		kill(editpid, SIGCONT);
		waitpid(editpid, &status, 0);
		editpid = -1;
	}
	if (*tempname != '\0') {
		unlink(tempname);
		*tempname = '\0';
	}
	if (lockfd != -1)
		close(lockfd);
	errno = serrno;
}

/*
 * Compares two struct pwds.
 */
int
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

/*
 * Make a passwd line out of a struct passwd.
 */
char *
pw_make(const struct passwd *pw)
{
	char *line;

	asprintf(&line, "%s:%s:%ju:%ju:%s:%ju:%ju:%s:%s:%s", pw->pw_name,
	    pw->pw_passwd, (uintmax_t)pw->pw_uid, (uintmax_t)pw->pw_gid,
	    pw->pw_class, (uintmax_t)pw->pw_change, (uintmax_t)pw->pw_expire,
	    pw->pw_gecos, pw->pw_dir, pw->pw_shell);
	return (line);
}

/*
 * Make a passwd line (in v7 format) out of a struct passwd
 */
char *
pw_make_v7(const struct passwd *pw)
{
	char *line;

	asprintf(&line, "%s:*:%ju:%ju:%s:%s:%s", pw->pw_name,
	    (uintmax_t)pw->pw_uid, (uintmax_t)pw->pw_gid,
	    pw->pw_gecos, pw->pw_dir, pw->pw_shell);
	return (line);
}

/*
 * Copy password file from one descriptor to another, replacing, deleting
 * or adding a single record on the way.
 */
int
pw_copy(int ffd, int tfd, const struct passwd *pw, struct passwd *old_pw)
{
	char *buf, *end, *line, *p, *q, *r, *tmp;
	struct passwd *fpw;
	const struct passwd *spw;
	size_t len, size;
	int eof, readlen;
	char t;

	if (old_pw == NULL && pw == NULL)
			return (-1);

	spw = old_pw;
	/* deleting a user */
	if (pw == NULL) {
		line = NULL;
	} else {
		if ((line = pw_make(pw)) == NULL)
			return (-1);
	}

	/* adding a user */
	if (spw == NULL)
		spw = pw;

	/* initialize the buffer */
	if ((buf = malloc(size = 1024)) == NULL)
		goto err;

	eof = 0;
	len = 0;
	p = q = end = buf;
	for (;;) {
		/* find the end of the current line */
		for (p = q; q < end && *q != '\0'; ++q)
			if (*q == '\n')
				break;

		/* if we don't have a complete line, fill up the buffer */
		if (q >= end) {
			if (eof)
				break;
			while ((size_t)(q - p) >= size) {
				if ((tmp = reallocarray(buf, 2, size)) == NULL) {
					warnx("passwd line too long");
					goto err;
				}
				p = tmp + (p - buf);
				q = tmp + (q - buf);
				end = tmp + (end - buf);
				buf = tmp;
				size = size * 2;
			}
			if (p < end) {
				q = memmove(buf, p, end - p);
				end -= p - buf;
			} else {
				p = q = end = buf;
			}
			readlen = read(ffd, end, size - (end - buf));
			if (readlen == -1)
				goto err;
			else
				len = (size_t)readlen;
			if (len == 0 && p == buf)
				break;
			end += len;
			len = end - buf;
			if (len < size) {
				eof = 1;
				if (len > 0 && buf[len - 1] != '\n')
					++len, *end++ = '\n';
			}
			continue;
		}

		/* is it a blank line or a comment? */
		for (r = p; r < q && isspace(*r); ++r)
			/* nothing */ ;
		if (r == q || *r == '#') {
			/* yep */
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}

		/* is it the one we're looking for? */

		t = *q;
		*q = '\0';

		fpw = pw_scan(r, PWSCAN_MASTER);

		/*
		 * fpw is either the struct passwd for the current line,
		 * or NULL if the line is malformed.
		 */

		*q = t;
		if (fpw == NULL || strcmp(fpw->pw_name, spw->pw_name) != 0) {
			/* nope */
			if (fpw != NULL)
				free(fpw);
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}
		if (old_pw && !pw_equal(fpw, old_pw)) {
			warnx("entry inconsistent");
			free(fpw);
			errno = EINVAL; /* hack */
			goto err;
		}
		free(fpw);

		/* it is, replace or remove it */
		if (line != NULL) {
			len = strlen(line);
			if (write(tfd, line, len) != (int)len)
				goto err;
		} else {
			/* when removed, avoid the \n */
			q++;
		}
		/* we're done, just copy the rest over */
		for (;;) {
			if (write(tfd, q, end - q) != end - q)
				goto err;
			q = buf;
			readlen = read(ffd, buf, size);
			if (readlen == 0)
				break;
			else
				len = (size_t)readlen;
			if (readlen == -1)
				goto err;
			end = buf + len;
		}
		goto done;
	}

	/* if we got here, we didn't find the old entry */
	if (line == NULL) {
		errno = ENOENT;
		goto err;
	}
	len = strlen(line);
	if ((size_t)write(tfd, line, len) != len ||
	    write(tfd, "\n", 1) != 1)
		goto err;
 done:
	free(line);
	free(buf);
	return (0);
 err:
	free(line);
	free(buf);
	return (-1);
}

/*
 * Return the current value of tempname.
 */
const char *
pw_tempname(void)
{

	return (tempname);
}

/*
 * Duplicate a struct passwd.
 */
struct passwd *
pw_dup(const struct passwd *pw)
{
	char *dst;
	struct passwd *npw;
	ssize_t len;

	len = sizeof(*npw);
	if (pw->pw_name != NULL)
		len += strlen(pw->pw_name) + 1;
	if (pw->pw_passwd != NULL)
		len += strlen(pw->pw_passwd) + 1;
	if (pw->pw_class != NULL)
		len += strlen(pw->pw_class) + 1;
	if (pw->pw_gecos != NULL)
		len += strlen(pw->pw_gecos) + 1;
	if (pw->pw_dir != NULL)
		len += strlen(pw->pw_dir) + 1;
	if (pw->pw_shell != NULL)
		len += strlen(pw->pw_shell) + 1;
	if ((npw = malloc((size_t)len)) == NULL)
		return (NULL);
	memcpy(npw, pw, sizeof(*npw));
	dst = (char *)npw + sizeof(*npw);
	if (pw->pw_name != NULL) {
		npw->pw_name = dst;
		dst = stpcpy(npw->pw_name, pw->pw_name) + 1;
	}
	if (pw->pw_passwd != NULL) {
		npw->pw_passwd = dst;
		dst = stpcpy(npw->pw_passwd, pw->pw_passwd) + 1;
	}
	if (pw->pw_class != NULL) {
		npw->pw_class = dst;
		dst = stpcpy(npw->pw_class, pw->pw_class) + 1;
	}
	if (pw->pw_gecos != NULL) {
		npw->pw_gecos = dst;
		dst = stpcpy(npw->pw_gecos, pw->pw_gecos) + 1;
	}
	if (pw->pw_dir != NULL) {
		npw->pw_dir = dst;
		dst = stpcpy(npw->pw_dir, pw->pw_dir) + 1;
	}
	if (pw->pw_shell != NULL) {
		npw->pw_shell = dst;
		dst = stpcpy(npw->pw_shell, pw->pw_shell) + 1;
	}
	return (npw);
}

#include "pw_scan.h"

/*
 * Wrapper around some internal libc functions.
 */

void
pw_initpwd(struct passwd *pw)
{

	__pw_initpwd(pw);
}

struct passwd *
pw_scan(const char *line, int flags)
{
	struct passwd pw, *ret;
	char *bp;

	if ((bp = strdup(line)) == NULL)
		return (NULL);
	__pw_initpwd(&pw);
	if (!__pw_scan(bp, &pw, flags)) {
		free(bp);
		return (NULL);
	}
	ret = pw_dup(&pw);
	free(bp);
	return (ret);
}
