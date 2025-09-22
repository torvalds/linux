/* $OpenBSD: fileops.c,v 1.2 2017/05/29 13:49:40 bluhm Exp $ */
/*
 * Copyright (c) 2017 Stefan Fritsch <sf@sfritsch.de>
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
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFSIZE (16 * 1024)
#define HOLESIZE (16 * BUFSIZE)

static int	 debug = 0;
static int	 fd = -1;
static off_t	 curpos = 0;
static char	*fname;
static char	*gbuf;
static char	*mbuf;

void
gen_data(void *buf, size_t size, uint32_t seed)
{
	assert(size % 4 == 0);
	if (debug)
		printf("%s: size %zd seed %#08x\n", __func__, size, seed);
	uint32_t *ibuf = buf;
	for (size_t i = 0; i < size / 4; i++)
		ibuf[i] = seed + i;
}

void
check_data(const void *buf, size_t size, uint32_t seed)
{
	assert(size % 4 == 0);
	const uint32_t *ibuf = buf;
	for (size_t i = 0; i < size / 4; i++) {
		if (ibuf[i] != seed + i) {
			errx(3, "%s: pos %zd/%zd: expected %#08zx got %#08x",
			    __func__, 4 * i, size, seed + i, ibuf[i]);
		}
	}
}

void
check_zero(const void *buf, size_t size)
{
	assert(size % 4 == 0);
	const uint32_t *ibuf = buf;
	for (size_t i = 0; i < size / 4; i++) {
		if (ibuf[i] != 0) {
			errx(3, "%s: pos %zd/%zd: expected 0 got %#08x",
			    __func__, 4 * i, size, ibuf[i]);
		}
	}
}

void
check(const char *what, int64_t have, int64_t want)
{
	if (have != want) {
		if (have == -1)
			err(2, "%s returned %lld, expected %lld",
			    what, have, want);
		else
			errx(2, "%s returned %lld, expected %lld",
			    what, have, want);
	}

	if (debug)
		printf("%s returned %lld\n", what, have);
}

void
c_write(void *buf, size_t size)
{
	ssize_t ret = write(fd, buf, size);
	check("write", ret, size);
	curpos += ret;
}

void
c_read(void *buf, size_t size)
{
	ssize_t ret = read(fd, buf, size);
	check("read", ret, size);
	curpos += ret;
}

void
c_fsync(void)
{
	int ret = fsync(fd);
	check("fsync", ret, 0);
}

void
c_lseek(off_t offset, int whence)
{
	off_t ret = lseek(fd, offset, whence);
	switch (whence) {
		case SEEK_SET:
			curpos = offset;
			break;
		case SEEK_CUR:
			curpos += offset;
			break;
		default:
			errx(1, "c_lseek not supported");
	}
	check("lseek", ret, curpos);
	if (debug)
		printf("curpos: %lld\n", (long long int)curpos);
}

void
c_mmap(size_t size)
{
	mbuf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, curpos);
	if (mbuf == MAP_FAILED)
		err(2, "mmap %zd pos %lld failed", size, (long long)curpos);
	curpos += size;
	if (debug)
		printf("mmap: %p\n", mbuf);
}

void
c_munmap(size_t size)
{
	int ret = munmap(mbuf, size);
	if (ret != 0)
		err(2, "munmap");
}

void
c_open(int flags)
{
	fd = open(fname, flags, S_IRUSR|S_IWUSR);
	if (fd == -1)
		err(1, "open");
}

void
check_read(size_t size, int hole)
{
	size_t pos = 0;
	while (pos < size) {
		size_t to_read = size - pos;
		uint32_t seed = curpos;
		if (to_read > BUFSIZE)
			to_read = BUFSIZE;
		c_read(gbuf, to_read);
		if (hole)
			check_zero(gbuf, to_read);
		else
			check_data(gbuf, to_read, seed);
		pos += to_read;
	}
}

/* XXX this assumes size is a multiple of the page size */
void
check_mmap(size_t size, int hole)
{
	size_t pos = 0;
	while (pos < size) {
		size_t to_read = size - pos;
		uint32_t seed = curpos;
		if (to_read > BUFSIZE)
			to_read = BUFSIZE;
		c_mmap(to_read);
		if (hole)
			check_zero(mbuf, to_read);
		else
			check_data(mbuf, to_read, seed);
		c_munmap(to_read);
		pos += to_read;
	}
}

void
do_create(void)
{
	unlink(fname);
	c_open(O_EXCL|O_CREAT|O_RDWR);

	gen_data(gbuf, BUFSIZE, curpos);
	c_write(gbuf, BUFSIZE);

	c_lseek(HOLESIZE, SEEK_CUR);

	gen_data(gbuf, BUFSIZE, curpos);
	c_write(gbuf, BUFSIZE);
	c_fsync();
}

void
do_read(void)
{
	c_open(O_RDWR);
	check_read(BUFSIZE, 0);
	check_read(HOLESIZE, 1);
	check_read(BUFSIZE, 0);
}

void
do_mmap(void)
{
	c_open(O_RDWR);
	check_mmap(BUFSIZE, 0);
	check_mmap(HOLESIZE, 1);
	check_mmap(BUFSIZE, 0);
}

void
usage(void)
{
	errx(1, "usage: fileops (create|read|mmap) filename");
}

int main(int argc, char **argv)
{
	if (argc != 3)
		usage();

	fname = argv[2];
	gbuf = malloc(BUFSIZE);
	if (gbuf == NULL)
		err(1, "malloc");

	if (strcmp(argv[1], "create") == 0) {
		do_create();
	} else if (strcmp(argv[1], "read") == 0) {
		do_read();
	} else if (strcmp(argv[1], "mmap") == 0) {
		do_mmap();
	} else {
		usage();
	}

	printf("pass\n");
	return 0;
}
