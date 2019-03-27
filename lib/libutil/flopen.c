/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/file.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <libutil.h>

/*
 * Reliably open and lock a file.
 *
 * Please do not modify this code without first reading the revision history
 * and discussing your changes with <des@freebsd.org>.  Don't be fooled by the
 * code's apparent simplicity; there would be no need for this function if it
 * was easy to get right.
 */
static int
vflopenat(int dirfd, const char *path, int flags, va_list ap)
{
	int fd, operation, serrno, trunc;
	struct stat sb, fsb;
	mode_t mode;

#ifdef O_EXLOCK
	flags &= ~O_EXLOCK;
#endif

	mode = 0;
	if (flags & O_CREAT) {
		mode = (mode_t)va_arg(ap, int); /* mode_t promoted to int */
	}

        operation = LOCK_EX;
        if (flags & O_NONBLOCK)
                operation |= LOCK_NB;

	trunc = (flags & O_TRUNC);
	flags &= ~O_TRUNC;

	for (;;) {
		if ((fd = openat(dirfd, path, flags, mode)) == -1)
			/* non-existent or no access */
			return (-1);
		if (flock(fd, operation) == -1) {
			/* unsupported or interrupted */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		if (fstatat(dirfd, path, &sb, 0) == -1) {
			/* disappeared from under our feet */
			(void)close(fd);
			continue;
		}
		if (fstat(fd, &fsb) == -1) {
			/* can't happen [tm] */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		if (sb.st_dev != fsb.st_dev ||
		    sb.st_ino != fsb.st_ino) {
			/* changed under our feet */
			(void)close(fd);
			continue;
		}
		if (trunc && ftruncate(fd, 0) != 0) {
			/* can't happen [tm] */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		/*
		 * The following change is provided as a specific example to
		 * avoid.
		 */
#if 0
		if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
#endif
		return (fd);
	}
}

int
flopen(const char *path, int flags, ...)
{
	va_list ap;
	int ret;

	va_start(ap, flags);
	ret = vflopenat(AT_FDCWD, path, flags, ap);
	va_end(ap);
	return (ret);
}

int
flopenat(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	int ret;

	va_start(ap, flags);
	ret = vflopenat(dirfd, path, flags, ap);
	va_end(ap);
	return (ret);
}
