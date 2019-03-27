/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Adrian Chadd <adrian@FreeBSD.org>.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1980, 1989, 1993
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

#include <sys/types.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <aio.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * This is a bit of a quick hack to do parallel IO testing through POSIX AIO.
 * Its specifically designed to work under FreeBSD and its derivatives;
 * note how I cheat by using aio_waitcomplete().
 *
 * TODO:
 *
 * + Add write support; so we can make sure we're not hitting throughput issues
 *   with read/modify/write of entire tracks of the disk
 * + Add in per-op stats - time and offset - so one could start mapping out
 *   the speed hotspots of the disk
 * + Add in different distributions - random, normal, left/right skewed normal,
 *   zipf, etc - and perhaps add the ability to run concurrent distributions
 *   (so a normal and a zipf; and also a random read; zipf write, etc.)
 *
 * Adrian Chadd <adrian@creative.net.au>
 */

typedef enum {
	IOT_NONE = 0x00,
	IOT_READ = 0x01,
	IOT_WRITE = 0x02
} iot_t;

static size_t
disk_getsize(int fd)
{
	off_t mediasize;	

	if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) < 0)
		err(1, "ioctl(DIOCGMEDIASIZE)");
	return (mediasize);
}

static iot_t
choose_aio(iot_t iomask)
{
	/* choose a random read or write event, limited by the mask */
	if (iomask == IOT_READ)
		return IOT_READ;
	else if (iomask == IOT_WRITE)
		return IOT_WRITE;
	return (random() & 0x01 ? IOT_READ : IOT_WRITE);
}

static void
set_aio(struct aiocb *a, iot_t iot, int fd, off_t offset, int size, char *buf)
{
	int r;
	bzero(a, sizeof(*a));
	a->aio_fildes = fd;
	a->aio_nbytes = size;
	a->aio_offset = offset;
	a->aio_buf = buf;
	if (iot == IOT_READ)
		r = aio_read(a);
	else
		r = aio_write(a);
	if (r != 0)
		err(1, "set_aio call failed");
}

int
main(int argc, char *argv[])
{
	int fd;
	struct stat sb;
	struct aiocb *aio;
	char **abuf;
	const char *fn;
	int aio_len;
	int io_size, nrun;
	off_t file_size, offset;
	struct aiocb *a;
	int i, n;
	struct timeval st, et, rt;
	float f_rt;
	iot_t iowhat;


	if (argc < 6) {
		printf("Usage: %s <file> <io size> <number of runs> <concurrency> <ro|wo|rw>\n",
		    argv[0]);
		exit(1);
	}

	fn = argv[1];
	io_size = atoi(argv[2]);
	if (io_size <= 0)
		errx(1, "the I/O size must be >0");
	nrun = atoi(argv[3]);
	if (nrun <= 0)
		errx(1, "the number of runs must be >0");
	aio_len = atoi(argv[4]);
	if (aio_len <= 0)
		errx(1, "AIO concurrency must be >0");
	if (strcmp(argv[5], "ro") == 0)
		iowhat = IOT_READ;
	else if (strcmp(argv[5], "rw") == 0)
		iowhat = IOT_READ | IOT_WRITE;
	else if (strcmp(argv[5], "wo") == 0)
		iowhat = IOT_WRITE;
	else
		errx(1, "the I/O type needs to be \"ro\", \"rw\", or \"wo\"!\n");

	/*
	 * Random returns values between 0 and (2^32)-1; only good for 4 gig.
	 * Lets instead treat random() as returning a block offset w/ block size
	 * being "io_size", so we can handle > 4 gig files.
	 */
	if (iowhat == IOT_READ)
		fd = open(fn, O_RDONLY | O_DIRECT);
	else if (iowhat == IOT_WRITE)
		fd = open(fn, O_WRONLY | O_DIRECT);
	else
		fd = open(fn, O_RDWR | O_DIRECT);

	if (fd < 0)
		err(1, "open failed");
	if (fstat(fd, &sb) < 0)
		err(1, "fstat failed");
	if (S_ISREG(sb.st_mode)) {
		file_size = sb.st_size;
	} else if (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode)) {
		file_size = disk_getsize(fd);
	} else
		errx(1, "unknown file type");
	if (file_size <= 0)
		errx(1, "path provided too small");

	printf("File: %s; File size %jd bytes\n", fn, (intmax_t)file_size);

	aio = calloc(aio_len, sizeof(struct aiocb));
	abuf = calloc(aio_len, sizeof(char *));
	for (i = 0; i < aio_len; i++)
		abuf[i] = calloc(1, io_size * sizeof(char));

	/* Fill with the initial contents */
	gettimeofday(&st, NULL);
	for (i = 0; i < aio_len; i++) {
		offset = random() % (file_size / io_size);
		offset *= io_size;
		set_aio(aio + i, choose_aio(iowhat), fd, offset, io_size, abuf[i]);
	}

	for (i = 0; i < nrun; i++) {
		aio_waitcomplete(&a, NULL);
		n = a - aio;
		assert(n < aio_len);
		assert(n >= 0);
		offset = random() % (file_size / io_size);
		offset *= io_size;
		set_aio(aio + n, choose_aio(iowhat), fd, offset, io_size, abuf[n]);
	}

	gettimeofday(&et, NULL);
	timersub(&et, &st, &rt);
	f_rt = ((float) (rt.tv_usec)) / 1000000.0;
	f_rt += (float) (rt.tv_sec);
	printf("Runtime: %.2f seconds, ", f_rt);
	printf("Op rate: %.2f ops/sec, ", ((float) (nrun))  / f_rt);
	printf("Avg transfer rate: %.2f bytes/sec\n", ((float) (nrun)) * ((float)io_size) / f_rt);



	exit(0);
}
