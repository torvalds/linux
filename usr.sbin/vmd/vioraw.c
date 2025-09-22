/*	$OpenBSD: vioraw.c,v 1.11 2023/09/14 15:25:43 dv Exp $	*/
/*
 * Copyright (c) 2018 Ori Bernstein <ori@eigenstate.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "virtio.h"

static ssize_t
raw_pread(void *file, char *buf, size_t len, off_t off)
{
	return pread(*(int *)file, buf, len, off);
}

static ssize_t
raw_preadv(void *file, struct iovec *iov, int cnt, off_t offset)
{
	return preadv(*(int *)file, iov, cnt, offset);
}

static ssize_t
raw_pwrite(void *file, char *buf, size_t len, off_t off)
{
	return pwrite(*(int *)file, buf, len, off);
}

static ssize_t
raw_pwritev(void *file, struct iovec *iov, int cnt, off_t offset)
{
	return pwritev(*(int *)file, iov, cnt, offset);
}

static void
raw_close(void *file, int stayopen)
{
	if (!stayopen)
		close(*(int *)file);
	free(file);
}

/*
 * Initializes a raw disk image backing file from an fd.  Stores the
 * number of bytes in *szp, returning -1 for error, 0 for success.
 */
int
virtio_raw_init(struct virtio_backing *file, off_t *szp, int *fd, size_t nfd)
{
	off_t sz;
	int *fdp;

	if (nfd != 1)
		return (-1);

	sz = lseek(fd[0], 0, SEEK_END);
	if (sz == -1)
		return (-1);

	fdp = malloc(sizeof(int));
	if (!fdp)
		return (-1);
	*fdp = fd[0];
	file->p = fdp;
	file->pread = raw_pread;
	file->preadv = raw_preadv;
	file->pwrite = raw_pwrite;
	file->pwritev = raw_pwritev;
	file->close = raw_close;
	*szp = sz;
	return (0);
}

/*
 * virtio_raw_create
 *
 * Create an empty imagefile with the specified path and size.
 *
 * Parameters:
 *  imgfile_path: path to the image file to create
 *  imgsize     : size of the image file to create (in bytes)
 *
 * Return:
 *  EEXIST: The requested image file already exists
 *  0     : Image file successfully created
 *  Exxxx : Various other Exxxx errno codes due to other I/O errors
 */
int
virtio_raw_create(const char *imgfile_path, uint64_t imgsize)
{
	int fd, ret;

	/* Refuse to overwrite an existing image */
	fd = open(imgfile_path, O_RDWR | O_CREAT | O_TRUNC | O_EXCL,
	    S_IRUSR | S_IWUSR);
	if (fd == -1)
		return (errno);

	/* Extend to desired size */
	if (ftruncate(fd, (off_t)imgsize) == -1) {
		ret = errno;
		close(fd);
		unlink(imgfile_path);
		return (ret);
	}

	ret = close(fd);
	return (ret);
}
