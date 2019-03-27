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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
__SCCSID("@(#)uucplock.c	8.1 (Berkeley) 6/6/93");

#include <sys/types.h>
#include <sys/file.h>
#include <dirent.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libutil.h"

#define MAXTRIES 5

#define LOCKTMP "LCKTMP..%d"
#define LOCKFMT "LCK..%s"

#define GORET(level, val) { err = errno; uuerr = (val); \
			    goto __CONCAT(ret, level); }

/* Forward declarations */
static int put_pid (int fd, pid_t pid);
static pid_t get_pid (int fd,int *err);

/*
 * uucp style locking routines
 */

int
uu_lock(const char *tty_name)
{
	int fd, tmpfd, i;
	pid_t pid, pid_old;
	char lckname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN],
	     lcktmpname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];
	int err, uuerr;

	pid = getpid();
	(void)snprintf(lcktmpname, sizeof(lcktmpname), _PATH_UUCPLOCK LOCKTMP,
			pid);
	(void)snprintf(lckname, sizeof(lckname), _PATH_UUCPLOCK LOCKFMT,
			tty_name);
	if ((tmpfd = open(lcktmpname, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
	    0664)) < 0)
		GORET(0, UU_LOCK_CREAT_ERR);

	for (i = 0; i < MAXTRIES; i++) {
		if (link (lcktmpname, lckname) < 0) {
			if (errno != EEXIST)
				GORET(1, UU_LOCK_LINK_ERR);
			/*
			 * file is already locked
			 * check to see if the process holding the lock
			 * still exists
			 */
			if ((fd = open(lckname, O_RDONLY | O_CLOEXEC)) < 0)
				GORET(1, UU_LOCK_OPEN_ERR);

			if ((pid_old = get_pid (fd, &err)) == -1)
				GORET(2, UU_LOCK_READ_ERR);

			close(fd);

			if (kill(pid_old, 0) == 0 || errno != ESRCH)
				GORET(1, UU_LOCK_INUSE);
			/*
			 * The process that locked the file isn't running, so
			 * we'll lock it ourselves
			 */
			(void)unlink(lckname);
		} else {
			if (!put_pid (tmpfd, pid))
				GORET(3, UU_LOCK_WRITE_ERR);
			break;
		}
	}
	GORET(1, (i >= MAXTRIES) ? UU_LOCK_TRY_ERR : UU_LOCK_OK);

ret3:
	(void)unlink(lckname);
	goto ret1;
ret2:
	(void)close(fd);
ret1:
	(void)close(tmpfd);
	(void)unlink(lcktmpname);
ret0:
	errno = err;
	return uuerr;
}

int
uu_lock_txfr(const char *tty_name, pid_t pid)
{
	int fd, err;
	char lckname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];

	snprintf(lckname, sizeof(lckname), _PATH_UUCPLOCK LOCKFMT, tty_name);

	if ((fd = open(lckname, O_RDWR | O_CLOEXEC)) < 0)
		return UU_LOCK_OWNER_ERR;
	if (get_pid(fd, &err) != getpid())
		err = UU_LOCK_OWNER_ERR;
	else {
        	lseek(fd, (off_t)0, SEEK_SET);
		err = put_pid(fd, pid) ? 0 : UU_LOCK_WRITE_ERR;
	}
	close(fd);

	return err;
}

int
uu_unlock(const char *tty_name)
{
	char tbuf[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];

	(void)snprintf(tbuf, sizeof(tbuf), _PATH_UUCPLOCK LOCKFMT, tty_name);
	return unlink(tbuf);
}

const char *
uu_lockerr(int uu_lockresult)
{
	static char errbuf[128];
	const char *fmt;

	switch (uu_lockresult) {
		case UU_LOCK_INUSE:
			return "device in use";
		case UU_LOCK_OK:
			return "";
		case UU_LOCK_OPEN_ERR:
			fmt = "open error: %s";
			break;
		case UU_LOCK_READ_ERR:
			fmt = "read error: %s";
			break;
		case UU_LOCK_CREAT_ERR:
			fmt = "creat error: %s";
			break;
		case UU_LOCK_WRITE_ERR:
			fmt = "write error: %s";
			break;
		case UU_LOCK_LINK_ERR:
			fmt = "link error: %s";
			break;
		case UU_LOCK_TRY_ERR:
			fmt = "too many tries: %s";
			break;
		case UU_LOCK_OWNER_ERR:
			fmt = "not locking process: %s";
			break;
		default:
			fmt = "undefined error: %s";
			break;
	}

	(void)snprintf(errbuf, sizeof(errbuf), fmt, strerror(errno));
	return errbuf;
}

static int
put_pid(int fd, pid_t pid)
{
	char buf[32];
	int len;

	len = sprintf (buf, "%10d\n", (int)pid);
	return write (fd, buf, (size_t)len) == len;
}

static pid_t
get_pid(int fd, int *err)
{
	int bytes_read;
	char buf[32];
	pid_t pid;

	bytes_read = read (fd, buf, sizeof (buf) - 1);
	if (bytes_read > 0) {
		buf[bytes_read] = '\0';
		pid = (pid_t)strtol (buf, (char **) NULL, 10);
	} else {
		pid = -1;
		*err = bytes_read ? errno : EINVAL;
	}
	return pid;
}

/* end of uucplock.c */
