/*-
 * Copyright (c) 2015 Jilles Tjoelker
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

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <time.h>

#ifndef	UTIME_NOW
#define	UTIME_NOW	-1
#define	UTIME_OMIT	-2
#endif

int
utimensat(int fd, const char *path, const struct timespec times[2], int flag)
{
	struct timeval now, tv[2], *tvp;
	struct stat sb;
	int osreldate;

	if ((flag & ~AT_SYMLINK_NOFOLLOW) != 0) {
		errno = EINVAL;
		return (-1);
	}
	if (times == NULL || (times[0].tv_nsec == UTIME_NOW &&
	    times[1].tv_nsec == UTIME_NOW))
		tvp = NULL;
	else if (times[0].tv_nsec == UTIME_OMIT &&
	    times[1].tv_nsec == UTIME_OMIT)
		return (0);
	else {
		if ((times[0].tv_nsec < 0 || times[0].tv_nsec > 999999999) &&
		    times[0].tv_nsec != UTIME_NOW &&
		    times[0].tv_nsec != UTIME_OMIT) {
			errno = EINVAL;
			return (-1);
		}
		if ((times[1].tv_nsec < 0 || times[1].tv_nsec > 999999999) &&
		    times[1].tv_nsec != UTIME_NOW &&
		    times[1].tv_nsec != UTIME_OMIT) {
			errno = EINVAL;
			return (-1);
		}
		tv[0].tv_sec = times[0].tv_sec;
		tv[0].tv_usec = times[0].tv_nsec / 1000;
		tv[1].tv_sec = times[1].tv_sec;
		tv[1].tv_usec = times[1].tv_nsec / 1000;
		tvp = tv;
		if (times[0].tv_nsec == UTIME_OMIT ||
		    times[1].tv_nsec == UTIME_OMIT) {
			if (fstatat(fd, path, &sb, flag) == -1)
				return (-1);
			if (times[0].tv_nsec == UTIME_OMIT) {
				tv[0].tv_sec = sb.st_atim.tv_sec;
				tv[0].tv_usec = sb.st_atim.tv_nsec / 1000;
			}
			if (times[1].tv_nsec == UTIME_OMIT) {
				tv[1].tv_sec = sb.st_mtim.tv_sec;
				tv[1].tv_usec = sb.st_mtim.tv_nsec / 1000;
			}
		}
		if (times[0].tv_nsec == UTIME_NOW ||
		    times[1].tv_nsec == UTIME_NOW) {
			if (gettimeofday(&now, NULL) == -1)
				return (-1);
			if (times[0].tv_nsec == UTIME_NOW)
				tv[0] = now;
			if (times[1].tv_nsec == UTIME_NOW)
				tv[1] = now;
		}
	}
	if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
		return (futimesat(fd, path, tvp));
	else if ((flag & AT_SYMLINK_NOFOLLOW) != 0 &&
	    (fd == AT_FDCWD || path[0] == '/'))
		return (lutimes(path, tvp));
	else {
		errno = ENOTSUP;
		return (-1);
	}
}
