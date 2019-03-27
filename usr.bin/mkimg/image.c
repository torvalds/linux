/*-
 * Copyright (c) 2014 Juniper Networks, Inc.
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

#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "image.h"
#include "mkimg.h"

#ifndef MAP_NOCORE
#define	MAP_NOCORE	0
#endif
#ifndef MAP_NOSYNC
#define	MAP_NOSYNC	0
#endif

#ifndef SEEK_DATA
#define	SEEK_DATA	-1
#endif
#ifndef SEEK_HOLE
#define	SEEK_HOLE	-1
#endif

struct chunk {
	TAILQ_ENTRY(chunk) ch_list;
	size_t	ch_size;		/* Size of chunk in bytes. */
	lba_t	ch_block;		/* Block address in image. */
	union {
		struct {
			off_t	ofs;	/* Offset in backing file. */
			int	fd;	/* FD of backing file. */
		} file;
		struct {
			void	*ptr;	/* Pointer to data in memory */
		} mem;
	} ch_u;
	u_int	ch_type;
#define	CH_TYPE_ZEROES		0	/* Chunk is a gap (no data). */
#define	CH_TYPE_FILE		1	/* File-backed chunk. */
#define	CH_TYPE_MEMORY		2	/* Memory-backed chunk */
};

static TAILQ_HEAD(chunk_head, chunk) image_chunks;
static u_int image_nchunks;

static char image_swap_file[PATH_MAX];
static int image_swap_fd = -1;
static u_int image_swap_pgsz;
static off_t image_swap_size;

static lba_t image_size;

static int
is_empty_sector(void *buf)
{
	uint64_t *p = buf;
	size_t n, max;

	assert(((uintptr_t)p & 3) == 0);

	max = secsz / sizeof(uint64_t);
	for (n = 0; n < max; n++) {
		if (p[n] != 0UL)
			return (0);
	}
	return (1);
}

/*
 * Swap file handlng.
 */

static off_t
image_swap_alloc(size_t size)
{
	off_t ofs;
	size_t unit;

	unit = (secsz > image_swap_pgsz) ? secsz : image_swap_pgsz;
	assert((unit & (unit - 1)) == 0);

	size = (size + unit - 1) & ~(unit - 1);

	ofs = image_swap_size;
	image_swap_size += size;
	if (ftruncate(image_swap_fd, image_swap_size) == -1) {
		image_swap_size = ofs;
		ofs = -1LL;
	}
	return (ofs);
}

/*
 * Image chunk handling.
 */

static struct chunk *
image_chunk_find(lba_t blk)
{
	static struct chunk *last = NULL;
	struct chunk *ch;

	ch = (last != NULL && last->ch_block <= blk)
	    ? last : TAILQ_FIRST(&image_chunks);
	while (ch != NULL) {
		if (ch->ch_block <= blk &&
		    (lba_t)(ch->ch_block + (ch->ch_size / secsz)) > blk) {
			last = ch;
			break;
		}
		ch = TAILQ_NEXT(ch, ch_list);
	}
	return (ch);
}

static size_t
image_chunk_grow(struct chunk *ch, size_t sz)
{
	size_t dsz, newsz;

	newsz = ch->ch_size + sz;
	if (newsz > ch->ch_size) {
		ch->ch_size = newsz;
		return (0);
	}
	/* We would overflow -- create new chunk for remainder. */
	dsz = SIZE_MAX - ch->ch_size;
	assert(dsz < sz);
	ch->ch_size = SIZE_MAX;
	return (sz - dsz);
}

static struct chunk *
image_chunk_memory(struct chunk *ch, lba_t blk)
{
	struct chunk *new;
	void *ptr;

	ptr = calloc(1, secsz);
	if (ptr == NULL)
		return (NULL);

	if (ch->ch_block < blk) {
		new = malloc(sizeof(*new));
		if (new == NULL) {
			free(ptr);
			return (NULL);
		}
		memcpy(new, ch, sizeof(*new));
		ch->ch_size = (blk - ch->ch_block) * secsz;
		new->ch_block = blk;
		new->ch_size -= ch->ch_size;
		TAILQ_INSERT_AFTER(&image_chunks, ch, new, ch_list);
		image_nchunks++;
		ch = new;
	}

	if (ch->ch_size > secsz) {
		new = malloc(sizeof(*new));
		if (new == NULL) {
			free(ptr);
			return (NULL);
		}
		memcpy(new, ch, sizeof(*new));
		ch->ch_size = secsz;
		new->ch_block++;
		new->ch_size -= secsz;
		TAILQ_INSERT_AFTER(&image_chunks, ch, new, ch_list);
		image_nchunks++;
	}

	ch->ch_type = CH_TYPE_MEMORY;
	ch->ch_u.mem.ptr = ptr;
	return (ch);
}

static int
image_chunk_skipto(lba_t to)
{
	struct chunk *ch;
	lba_t from;
	size_t sz;

	ch = TAILQ_LAST(&image_chunks, chunk_head);
	from = (ch != NULL) ? ch->ch_block + (ch->ch_size / secsz) : 0LL;

	assert(from <= to);

	/* Nothing to do? */
	if (from == to)
		return (0);
	/* Avoid bugs due to overflows. */
	if ((uintmax_t)(to - from) > (uintmax_t)(SIZE_MAX / secsz))
		return (EFBIG);
	sz = (to - from) * secsz;
	if (ch != NULL && ch->ch_type == CH_TYPE_ZEROES) {
		sz = image_chunk_grow(ch, sz);
		if (sz == 0)
			return (0);
		from = ch->ch_block + (ch->ch_size / secsz);
	}
	ch = malloc(sizeof(*ch));
	if (ch == NULL)
		return (ENOMEM);
	memset(ch, 0, sizeof(*ch));
	ch->ch_block = from;
	ch->ch_size = sz;
	ch->ch_type = CH_TYPE_ZEROES;
	TAILQ_INSERT_TAIL(&image_chunks, ch, ch_list);
	image_nchunks++;
	return (0);
}

static int
image_chunk_append(lba_t blk, size_t sz, off_t ofs, int fd)
{
	struct chunk *ch;

	ch = TAILQ_LAST(&image_chunks, chunk_head);
	if (ch != NULL && ch->ch_type == CH_TYPE_FILE) {
		if (fd == ch->ch_u.file.fd &&
		    blk == (lba_t)(ch->ch_block + (ch->ch_size / secsz)) &&
		    ofs == (off_t)(ch->ch_u.file.ofs + ch->ch_size)) {
			sz = image_chunk_grow(ch, sz);
			if (sz == 0)
				return (0);
			blk = ch->ch_block + (ch->ch_size / secsz);
			ofs = ch->ch_u.file.ofs + ch->ch_size;
		}
	}
	ch = malloc(sizeof(*ch));
	if (ch == NULL)
		return (ENOMEM);
	memset(ch, 0, sizeof(*ch));
	ch->ch_block = blk;
	ch->ch_size = sz;
	ch->ch_type = CH_TYPE_FILE;
	ch->ch_u.file.ofs = ofs;
	ch->ch_u.file.fd = fd;
	TAILQ_INSERT_TAIL(&image_chunks, ch, ch_list);
	image_nchunks++;
	return (0);
}

static int
image_chunk_copyin(lba_t blk, void *buf, size_t sz, off_t ofs, int fd)
{
	uint8_t *p = buf;
	int error;

	error = 0;
	sz = (sz + secsz - 1) & ~(secsz - 1);
	while (!error && sz > 0) {
		if (is_empty_sector(p))
			error = image_chunk_skipto(blk + 1);
		else
			error = image_chunk_append(blk, secsz, ofs, fd);
		blk++;
		p += secsz;
		sz -= secsz;
		ofs += secsz;
	}
	return (error);
}

/*
 * File mapping support.
 */

static void *
image_file_map(int fd, off_t ofs, size_t sz, off_t *iofp)
{
	void *ptr;
	size_t unit;
	int flags, prot;
	off_t x;

	/* On Linux anyway ofs must also be page aligned */
	if ((x = (ofs % image_swap_pgsz)) != 0) {
	    ofs -= x;
	    sz += x;
	    *iofp = x;
	} else
	    *iofp = 0;
	unit = (secsz > image_swap_pgsz) ? secsz : image_swap_pgsz;
	assert((unit & (unit - 1)) == 0);

	flags = MAP_NOCORE | MAP_NOSYNC | MAP_SHARED;
	/* Allow writing to our swap file only. */
	prot = PROT_READ | ((fd == image_swap_fd) ? PROT_WRITE : 0);
	sz = (sz + unit - 1) & ~(unit - 1);
	ptr = mmap(NULL, sz, prot, flags, fd, ofs);
	return ((ptr == MAP_FAILED) ? NULL : ptr);
}

static int
image_file_unmap(void *buffer, size_t sz)
{
	size_t unit;

	unit = (secsz > image_swap_pgsz) ? secsz : image_swap_pgsz;
	sz = (sz + unit - 1) & ~(unit - 1);
	if (madvise(buffer, sz, MADV_DONTNEED) != 0)
		warn("madvise");
	munmap(buffer, sz);
	return (0);
}

/*
 * Input/source file handling.
 */

static int
image_copyin_stream(lba_t blk, int fd, uint64_t *sizep)
{
	char *buffer;
	uint64_t bytesize;
	off_t swofs;
	size_t iosz;
	ssize_t rdsz;
	int error;
	off_t iof;

	/*
	 * This makes sure we're doing I/O in multiples of the page
	 * size as well as of the sector size. 2MB is the minimum
	 * by virtue of secsz at least 512 bytes and the page size
	 * at least 4K bytes.
	 */
	iosz = secsz * image_swap_pgsz;

	bytesize = 0;
	do {
		swofs = image_swap_alloc(iosz);
		if (swofs == -1LL)
			return (errno);
		buffer = image_file_map(image_swap_fd, swofs, iosz, &iof);
		if (buffer == NULL)
			return (errno);
		rdsz = read(fd, &buffer[iof], iosz);
		if (rdsz > 0)
			error = image_chunk_copyin(blk, &buffer[iof], rdsz, swofs,
			    image_swap_fd);
		else if (rdsz < 0)
			error = errno;
		else
			error = 0;
		image_file_unmap(buffer, iosz);
		/* XXX should we relinguish unused swap space? */
		if (error)
			return (error);

		bytesize += rdsz;
		blk += (rdsz + secsz - 1) / secsz;
	} while (rdsz > 0);

	if (sizep != NULL)
		*sizep = bytesize;
	return (0);
}

static int
image_copyin_mapped(lba_t blk, int fd, uint64_t *sizep)
{
	off_t cur, data, end, hole, pos, iof;
	void *mp;
	char *buf;
	uint64_t bytesize;
	size_t iosz, sz;
	int error;

	/*
	 * We'd like to know the size of the file and we must
	 * be able to seek in order to mmap(2). If this isn't
	 * possible, then treat the file as a stream/pipe.
	 */
	end = lseek(fd, 0L, SEEK_END);
	if (end == -1L)
		return (image_copyin_stream(blk, fd, sizep));

	/*
	 * We need the file opened for the duration and our
	 * caller is going to close the file. Make a dup(2)
	 * so that control the faith of the descriptor.
	 */
	fd = dup(fd);
	if (fd == -1)
		return (errno);

	iosz = secsz * image_swap_pgsz;

	bytesize = 0;
	cur = pos = 0;
	error = 0;
	while (!error && cur < end) {
		hole = lseek(fd, cur, SEEK_HOLE);
		if (hole == -1)
			hole = end;
		data = lseek(fd, cur, SEEK_DATA);
		if (data == -1)
			data = end;

		/*
		 * Treat the entire file as data if sparse files
		 * are not supported by the underlying file system.
		 */
		if (hole == end && data == end)
			data = cur;

		if (cur == hole && data > hole) {
			hole = pos;
			pos = data & ~((uint64_t)secsz - 1);

			blk += (pos - hole) / secsz;
			error = image_chunk_skipto(blk);

			bytesize += pos - hole;
			cur = data;
		} else if (cur == data && hole > data) {
			data = pos;
			pos = (hole + secsz - 1) & ~((uint64_t)secsz - 1);

			while (data < pos) {
				sz = (pos - data > (off_t)iosz)
				    ? iosz : (size_t)(pos - data);

				buf = mp = image_file_map(fd, data, sz, &iof);
				if (mp != NULL) {
					buf += iof;
					error = image_chunk_copyin(blk, buf,
					    sz, data, fd);
					image_file_unmap(mp, sz);
				} else
					error = errno;

				blk += sz / secsz;
				bytesize += sz;
				data += sz;
			}
			cur = hole;
		} else {
			/*
			 * I don't know what this means or whether it
			 * can happen at all...
			 */
			assert(0);
		}
	}
	if (error)
		close(fd);
	if (!error && sizep != NULL)
		*sizep = bytesize;
	return (error);
}

int
image_copyin(lba_t blk, int fd, uint64_t *sizep)
{
	struct stat sb;
	int error;

	error = image_chunk_skipto(blk);
	if (!error) {
		if (fstat(fd, &sb) == -1 || !S_ISREG(sb.st_mode))
			error = image_copyin_stream(blk, fd, sizep);
		else
			error = image_copyin_mapped(blk, fd, sizep);
	}
	return (error);
}

/*
 * Output/sink file handling.
 */

int
image_copyout(int fd)
{
	int error;

	error = image_copyout_region(fd, 0, image_size);
	if (!error)
		error = image_copyout_done(fd);
	return (error);
}

int
image_copyout_done(int fd)
{
	off_t ofs;
	int error;

	ofs = lseek(fd, 0L, SEEK_CUR);
	if (ofs == -1)
		return (0);
	error = (ftruncate(fd, ofs) == -1) ? errno : 0;
	return (error);
}

static int
image_copyout_memory(int fd, size_t size, void *ptr)
{

	if (write(fd, ptr, size) == -1)
		return (errno);
	return (0);
}

int
image_copyout_zeroes(int fd, size_t count)
{
	static uint8_t *zeroes = NULL;
	size_t sz;
	int error;

	if (lseek(fd, (off_t)count, SEEK_CUR) != -1)
		return (0);

	/*
	 * If we can't seek, we must write.
	 */

	if (zeroes == NULL) {
		zeroes = calloc(1, secsz);
		if (zeroes == NULL)
			return (ENOMEM);
	}

	while (count > 0) {
		sz = (count > secsz) ? secsz : count;
		error = image_copyout_memory(fd, sz, zeroes);
		if (error)
			return (error);
		count -= sz;
	}
	return (0);
}

static int
image_copyout_file(int fd, size_t size, int ifd, off_t iofs)
{
	void *mp;
	char *buf;
	size_t iosz, sz;
	int error;
	off_t iof;

	iosz = secsz * image_swap_pgsz;

	while (size > 0) {
		sz = (size > iosz) ? iosz : size;
		buf = mp = image_file_map(ifd, iofs, sz, &iof);
		if (buf == NULL)
			return (errno);
		buf += iof;
		error = image_copyout_memory(fd, sz, buf);
		image_file_unmap(mp, sz);
		if (error)
			return (error);
		size -= sz;
		iofs += sz;
	}
	return (0);
}

int
image_copyout_region(int fd, lba_t blk, lba_t size)
{
	struct chunk *ch;
	size_t ofs, sz;
	int error;

	size *= secsz;

	error = 0;
	while (!error && size > 0) {
		ch = image_chunk_find(blk);
		if (ch == NULL) {
			error = EINVAL;
			break;
		}
		ofs = (blk - ch->ch_block) * secsz;
		sz = ch->ch_size - ofs;
		sz = ((lba_t)sz < size) ? sz : (size_t)size;
		switch (ch->ch_type) {
		case CH_TYPE_ZEROES:
			error = image_copyout_zeroes(fd, sz);
			break;
		case CH_TYPE_FILE:
			error = image_copyout_file(fd, sz, ch->ch_u.file.fd,
			    ch->ch_u.file.ofs + ofs);
			break;
		case CH_TYPE_MEMORY:
			error = image_copyout_memory(fd, sz, ch->ch_u.mem.ptr);
			break;
		default:
			assert(0);
		}
		size -= sz;
		blk += sz / secsz;
	}
	return (error);
}

int
image_data(lba_t blk, lba_t size)
{
	struct chunk *ch;
	lba_t lim;

	while (1) {
		ch = image_chunk_find(blk);
		if (ch == NULL)
			return (0);
		if (ch->ch_type != CH_TYPE_ZEROES)
			return (1);
		lim = ch->ch_block + (ch->ch_size / secsz);
		if (lim >= blk + size)
			return (0);
		size -= lim - blk;
		blk = lim;
	}
	/*NOTREACHED*/
}

lba_t
image_get_size(void)
{

	return (image_size);
}

int
image_set_size(lba_t blk)
{
	int error;

	error = image_chunk_skipto(blk);
	if (!error)
		image_size = blk;
	return (error);
}

int
image_write(lba_t blk, void *buf, ssize_t len)
{
	struct chunk *ch;

	while (len > 0) {
		if (!is_empty_sector(buf)) {
			ch = image_chunk_find(blk);
			if (ch == NULL)
				return (ENXIO);
			/* We may not be able to write to files. */
			if (ch->ch_type == CH_TYPE_FILE)
				return (EINVAL);
			if (ch->ch_type == CH_TYPE_ZEROES) {
				ch = image_chunk_memory(ch, blk);
				if (ch == NULL)
					return (ENOMEM);
			}
			assert(ch->ch_type == CH_TYPE_MEMORY);
			memcpy(ch->ch_u.mem.ptr, buf, secsz);
		}
		blk++;
		buf = (char *)buf + secsz;
		len--;
	}
	return (0);
}

static void
image_cleanup(void)
{
	struct chunk *ch;

	while ((ch = TAILQ_FIRST(&image_chunks)) != NULL) {
		switch (ch->ch_type) {
		case CH_TYPE_FILE:
			/* We may be closing the same file multiple times. */
			if (ch->ch_u.file.fd != -1)
				close(ch->ch_u.file.fd);
			break;
		case CH_TYPE_MEMORY:
			free(ch->ch_u.mem.ptr);
			break;
		default:
			break;
		}
		TAILQ_REMOVE(&image_chunks, ch, ch_list);
		free(ch);
	}
	if (image_swap_fd != -1)
		close(image_swap_fd);
	unlink(image_swap_file);
}

int
image_init(void)
{
	const char *tmpdir;

	TAILQ_INIT(&image_chunks);
	image_nchunks = 0;

	image_swap_size = 0;
	image_swap_pgsz = getpagesize();

	if (atexit(image_cleanup) == -1)
		return (errno);
	if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
		tmpdir = _PATH_TMP;
	snprintf(image_swap_file, sizeof(image_swap_file), "%s/mkimg-XXXXXX",
	    tmpdir);
	image_swap_fd = mkstemp(image_swap_file);
	if (image_swap_fd == -1)
		return (errno);
	return (0);
}
