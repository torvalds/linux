/*	$OpenBSD: uucplock.c,v 1.22 2025/08/25 14:59:13 claudio Exp $	*/
/*
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
 *
 *
 */

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <paths.h>
#include <string.h>
#include "util.h"

#define MAXTRIES 5

#define LOCKTMP "LCKTMP..%ld"
#define LOCKFMT "LCK..%s"

#define GORET(level, val) { err = errno; uuerr = (val); \
			    goto __CONCAT(ret, level); }

/* Forward declarations */
static int put_pid(int fd, pid_t pid);
static pid_t get_pid(int fd,int *err);

/*
 * uucp style locking routines
 */
int
uu_lock(const char *ttyname)
{
	char lckname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN],
	     lcktmpname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];
	int fd, tmpfd, i, err, uuerr;
	pid_t pid, pid_old;

	pid = getpid();
	(void)snprintf(lcktmpname, sizeof(lcktmpname), _PATH_UUCPLOCK LOCKTMP,
	    (long)pid);
	(void)snprintf(lckname, sizeof(lckname), _PATH_UUCPLOCK LOCKFMT,
	    ttyname);
	tmpfd = open(lcktmpname, O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC, 0664);
	if (tmpfd == -1)
		GORET(0, UU_LOCK_CREAT_ERR);

	for (i = 0; i < MAXTRIES; i++) {
		if (link(lcktmpname, lckname) == -1) {
			if (errno != EEXIST)
				GORET(1, UU_LOCK_LINK_ERR);
			/*
			 * file is already locked
			 * check to see if the process holding the lock
			 * still exists
			 */
			if ((fd = open(lckname, O_RDONLY | O_CLOEXEC)) == -1)
				GORET(1, UU_LOCK_OPEN_ERR);

			if ((pid_old = get_pid(fd, &err)) == -1)
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
			if (!put_pid(tmpfd, pid))
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
uu_lock_txfr(const char *ttyname, pid_t pid)
{
	char lckname[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];
	int fd, err, ret;

	snprintf(lckname, sizeof(lckname), _PATH_UUCPLOCK LOCKFMT, ttyname);

	if ((fd = open(lckname, O_RDWR | O_CLOEXEC)) == -1)
		return UU_LOCK_OWNER_ERR;
	if (get_pid(fd, &err) != getpid())
		ret = UU_LOCK_OWNER_ERR;
	else {
		lseek(fd, 0, SEEK_SET);
		ret = put_pid(fd, pid) ? UU_LOCK_OK : UU_LOCK_WRITE_ERR;
	}

	close(fd);
	return ret;
}

int
uu_unlock(const char *ttyname)
{
	char tbuf[sizeof(_PATH_UUCPLOCK) + MAXNAMLEN];

	(void)snprintf(tbuf, sizeof(tbuf), _PATH_UUCPLOCK LOCKFMT, ttyname);
	return unlink(tbuf);
}

const char *
uu_lockerr(int uu_lockresult)
{
	static char errbuf[128];
	const char *err;

	switch (uu_lockresult) {
	case UU_LOCK_INUSE:
		return "device in use";
	case UU_LOCK_OK:
		return "";
	case UU_LOCK_OPEN_ERR:
		err = "open error";
		break;
	case UU_LOCK_READ_ERR:
		err = "read error";
		break;
	case UU_LOCK_CREAT_ERR:
		err = "creat error";
		break;
	case UU_LOCK_WRITE_ERR:
		err = "write error";
		break;
	case UU_LOCK_LINK_ERR:
		err = "link error";
		break;
	case UU_LOCK_TRY_ERR:
		err = "too many tries";
		break;
	case UU_LOCK_OWNER_ERR:
		err = "not locking process";
		break;
	default:
		err = "undefined error";
		break;
	}

	(void)snprintf(errbuf, sizeof(errbuf), "%s: %s", err, strerror(errno));
	return errbuf;
}

static int
put_pid(int fd, pid_t pid)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof buf, "%10ld\n", (long)pid);
	if (len < 0 || (size_t)len >= sizeof buf)
		return 0;

	if (write(fd, buf, len) != len)
		return 0;

	/* We don't mind too much if ftruncate() fails - see get_pid */
	ftruncate(fd, (off_t)len);
	return 1;
}

static pid_t
get_pid(int fd, int *err)
{
	ssize_t bytes_read;
	char buf[32];
	pid_t pid;

	bytes_read = read(fd, buf, sizeof (buf) - 1);
	if (bytes_read > 0) {
		buf[bytes_read] = '\0';
		pid = (pid_t)strtoul(buf, (char **) NULL, 10);
	} else {
		pid = -1;
		*err = bytes_read ? errno : EINVAL;
	}
	return pid;
}
