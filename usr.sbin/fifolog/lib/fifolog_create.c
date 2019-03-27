/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Poul-Henning Kamp
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
 *
 * $FreeBSD$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <sys/disk.h>

#include "fifolog.h"
#include "libfifolog.h"

const char *
fifolog_create(const char *fn, off_t size, ssize_t recsize)
{
	int i, fd;
	ssize_t u;
	u_int uu;
	off_t ms;
	struct stat st;
	char *buf;
	int created;

	fd = open(fn, O_WRONLY | O_TRUNC | O_EXCL | O_CREAT, 0644);
	if (fd < 0) {
		created = 0;
		fd = open(fn, O_WRONLY);
		if (fd < 0)
			return ("Could not open");
	} else
		created = 1;

	/* Default sectorsize is 512 */
	if (recsize == 0)
		recsize = 512;

	/* See what we got... */
	i = fstat(fd, &st);
	assert(i == 0);
	if (!S_ISBLK(st.st_mode) &&
	    !S_ISCHR(st.st_mode) &&
	    !S_ISREG(st.st_mode)) {
		assert(!close (fd));
		return ("Wrong file type");
	}

	if(!created && S_ISREG(st.st_mode)) {
		assert(!close (fd));
		return ("Wrong file type");
	}

	/* For raw disk with larger sectors: use 1 sector */
	i = ioctl(fd, DIOCGSECTORSIZE, &uu);
	u = uu;
	if (i == 0 && (u > recsize || (recsize % u) != 0))
		recsize = u;

	/* If no configured size, or too large for disk, use device size */
	i = ioctl(fd, DIOCGMEDIASIZE, &ms);
	if (i == 0 && (size == 0 || size > ms))
		size = ms;

	if (size == 0 && S_ISREG(st.st_mode))
		size = st.st_size;

	if (size == 0)
		size = recsize * (off_t)(24*60*60);

	if (S_ISREG(st.st_mode) && ftruncate(fd, size) < 0)
		return ("Could not ftrunc");

	buf = calloc(1, recsize);
	if (buf == NULL)
		return ("Could not malloc");

	strcpy(buf, FIFOLOG_FMT_MAGIC);		/*lint !e64 */
	be32enc(buf + FIFOLOG_OFF_BS, recsize);
	if (recsize != pwrite(fd, buf, recsize, 0)) {
		i = errno;
		free(buf);
		errno = i;
		return ("Could not write first sector");
	}
	memset(buf, 0, recsize);
	if ((int)recsize != pwrite(fd, buf, recsize, recsize)) {
		i = errno;
		free(buf);
		errno = i;
		return ("Could not write second sector");
	}
	free(buf);
	assert(0 == close(fd));
	return (NULL);
}
