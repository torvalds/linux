/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct pidfh {
	int	pf_dirfd;
	int	pf_fd;
	char	pf_dir[MAXPATHLEN + 1];
	char	pf_filename[MAXPATHLEN + 1];
	dev_t	pf_dev;
	ino_t	pf_ino;
};

static int _pidfile_remove(struct pidfh *pfh, int freeit);

static int
pidfile_verify(const struct pidfh *pfh)
{
	struct stat sb;

	if (pfh == NULL || pfh->pf_fd == -1)
		return (EDOOFUS);
	/*
	 * Check remembered descriptor.
	 */
	if (fstat(pfh->pf_fd, &sb) == -1)
		return (errno);
	if (sb.st_dev != pfh->pf_dev || sb.st_ino != pfh->pf_ino)
		return (EDOOFUS);
	return (0);
}

static int
pidfile_read(int dirfd, const char *filename, pid_t *pidptr)
{
	char buf[16], *endptr;
	int error, fd, i;

	fd = openat(dirfd, filename, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return (errno);

	i = read(fd, buf, sizeof(buf) - 1);
	error = errno;	/* Remember errno in case close() wants to change it. */
	close(fd);
	if (i == -1)
		return (error);
	else if (i == 0)
		return (EAGAIN);
	buf[i] = '\0';

	*pidptr = strtol(buf, &endptr, 10);
	if (endptr != &buf[i])
		return (EINVAL);

	return (0);
}

struct pidfh *
pidfile_open(const char *path, mode_t mode, pid_t *pidptr)
{
	struct pidfh *pfh;
	struct stat sb;
	int error, fd, dirfd, dirlen, filenamelen, count;
	struct timespec rqtp;
	cap_rights_t caprights;

	pfh = malloc(sizeof(*pfh));
	if (pfh == NULL)
		return (NULL);

	if (path == NULL) {
		dirlen = snprintf(pfh->pf_dir, sizeof(pfh->pf_dir),
		    "/var/run/");
		filenamelen = snprintf(pfh->pf_filename,
		    sizeof(pfh->pf_filename), "%s.pid", getprogname());
	} else {
		dirlen = snprintf(pfh->pf_dir, sizeof(pfh->pf_dir),
		    "%s", path);
		filenamelen = snprintf(pfh->pf_filename,
		    sizeof(pfh->pf_filename), "%s", path);

		dirname(pfh->pf_dir);
		basename(pfh->pf_filename);
	}

	if (dirlen >= (int)sizeof(pfh->pf_dir) ||
	    filenamelen >= (int)sizeof(pfh->pf_filename)) {
		free(pfh);
		errno = ENAMETOOLONG;
		return (NULL);
	}

	dirfd = open(pfh->pf_dir, O_CLOEXEC | O_DIRECTORY | O_NONBLOCK);
	if (dirfd == -1) {
		error = errno;
		free(pfh);
		errno = error;
		return (NULL);
	}

	/*
	 * Open the PID file and obtain exclusive lock.
	 * We truncate PID file here only to remove old PID immediately,
	 * PID file will be truncated again in pidfile_write(), so
	 * pidfile_write() can be called multiple times.
	 */
	fd = flopenat(dirfd, pfh->pf_filename,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NONBLOCK, mode);
	if (fd == -1) {
		if (errno == EWOULDBLOCK) {
			if (pidptr == NULL) {
				errno = EEXIST;
			} else {
				count = 20;
				rqtp.tv_sec = 0;
				rqtp.tv_nsec = 5000000;
				for (;;) {
					errno = pidfile_read(dirfd,
					    pfh->pf_filename, pidptr);
					if (errno != EAGAIN || --count == 0)
						break;
					nanosleep(&rqtp, 0);
				}
				if (errno == EAGAIN)
					*pidptr = -1;
				if (errno == 0 || errno == EAGAIN)
					errno = EEXIST;
			}
		}
		error = errno;
		close(dirfd);
		free(pfh);
		errno = error;
		return (NULL);
	}

	/*
	 * Remember file information, so in pidfile_write() we are sure we write
	 * to the proper descriptor.
	 */
	if (fstat(fd, &sb) == -1) {
		goto failed;
	}

	if (cap_rights_limit(dirfd,
	    cap_rights_init(&caprights, CAP_UNLINKAT)) < 0 && errno != ENOSYS) {
		goto failed;
	}

	if (cap_rights_limit(fd, cap_rights_init(&caprights, CAP_PWRITE,
	    CAP_FSTAT, CAP_FTRUNCATE)) < 0 &&
	    errno != ENOSYS) {
		goto failed;
	}

	pfh->pf_dirfd = dirfd;
	pfh->pf_fd = fd;
	pfh->pf_dev = sb.st_dev;
	pfh->pf_ino = sb.st_ino;

	return (pfh);

failed:
	error = errno;
	unlinkat(dirfd, pfh->pf_filename, 0);
	close(dirfd);
	close(fd);
	free(pfh);
	errno = error;
	return (NULL);
}

int
pidfile_write(struct pidfh *pfh)
{
	char pidstr[16];
	int error, fd;

	/*
	 * Check remembered descriptor, so we don't overwrite some other
	 * file if pidfile was closed and descriptor reused.
	 */
	errno = pidfile_verify(pfh);
	if (errno != 0) {
		/*
		 * Don't close descriptor, because we are not sure if it's ours.
		 */
		return (-1);
	}
	fd = pfh->pf_fd;

	/*
	 * Truncate PID file, so multiple calls of pidfile_write() are allowed.
	 */
	if (ftruncate(fd, 0) == -1) {
		error = errno;
		_pidfile_remove(pfh, 0);
		errno = error;
		return (-1);
	}

	snprintf(pidstr, sizeof(pidstr), "%u", getpid());
	if (pwrite(fd, pidstr, strlen(pidstr), 0) != (ssize_t)strlen(pidstr)) {
		error = errno;
		_pidfile_remove(pfh, 0);
		errno = error;
		return (-1);
	}

	return (0);
}

int
pidfile_close(struct pidfh *pfh)
{
	int error;

	error = pidfile_verify(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	if (close(pfh->pf_fd) == -1)
		error = errno;
	if (close(pfh->pf_dirfd) == -1 && error == 0)
		error = errno;

	free(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

static int
_pidfile_remove(struct pidfh *pfh, int freeit)
{
	int error;

	error = pidfile_verify(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	if (unlinkat(pfh->pf_dirfd, pfh->pf_filename, 0) == -1)
		error = errno;
	if (close(pfh->pf_fd) == -1 && error == 0)
		error = errno;
	if (close(pfh->pf_dirfd) == -1 && error == 0)
		error = errno;
	if (freeit)
		free(pfh);
	else
		pfh->pf_fd = -1;
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
pidfile_remove(struct pidfh *pfh)
{

	return (_pidfile_remove(pfh, 1));
}

int
pidfile_fileno(const struct pidfh *pfh)
{

	if (pfh == NULL || pfh->pf_fd == -1) {
		errno = EDOOFUS;
		return (-1);
	}
	return (pfh->pf_fd);
}
