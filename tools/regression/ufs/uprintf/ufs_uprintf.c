/*-
 * Copyright (c) 2005 Robert N. M. Watson
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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * This regression test attempts to exercise two instances of uprintf(9) in
 * UFS: (1) when blocks are exhausted, and (2) when inodes are exhausted, in
 * order to attempt to trigger races in the uprintf(9) code.  The test
 * accepts a pointer to a path -- ideally, a very small UFS partition -- and
 * then proceeds to fill it in various ways.
 *
 * This tool assumes that it is alright to create, and delete, entries in the
 * directory with names of integer values.  Don't run this tool against a
 * directory that has files with names along those lines if you want to keep
 * the files.
 *
 * Suggested usage is:
 *
 * mdconfig -a -t malloc -s 512
 * newfs /dev/mdX
 * mount /dev/mdX /mnt
 * ufs_uprintf /mnt
 * umount /mnt
 * mdconfig -d -u X
 */

#define	NUMTRIES	200

/*
 * Fill up the disk, then generate NUMTRIES additional ENOSPC errors.
 */
#define	BLOCKSIZE	1024
#define	BLOCKS_FILENAME	"0"
static void
fill_blocks(void)
{
	char block[BLOCKSIZE];
	ssize_t len;
	int fd, i;

	fd = open(BLOCKS_FILENAME, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		err(-1, "fill_blocks: open(%s)", BLOCKS_FILENAME);

	/*
	 * First step: fill the disk device.  Keep extending the file until
	 * we hit our first error, and hope it is ENOSPC.
	 */
	bzero(block, BLOCKSIZE);
	errno = 0;
	while (1) {
		len = write(fd, block, BLOCKSIZE);
		if (len < 0)
			break;
		if (len != BLOCKSIZE) {
			warnx("fill_blocks: write(%d) returned %zd",
			    BLOCKSIZE, len);
			close(fd);
			(void)unlink(BLOCKS_FILENAME);
			exit(-1);
		}

	}
	if (errno != ENOSPC) {
		warn("fill_blocks: write");
		close(fd);
		(void)unlink(BLOCKS_FILENAME);
		exit(-1);
	}

	/*
	 * Second step: generate NUMTRIES instances of the error by retrying
	 * the write.
	 */
	for (i = 0; i < NUMTRIES; i++) {
		len = write(fd, block, BLOCKSIZE);
		if (len < 0 && errno != ENOSPC) {
			warn("fill_blocks: write after ENOSPC");
			close(fd);
			(void)unlink(BLOCKS_FILENAME);
			exit(-1);
		}
	}

	close(fd);
	(void)unlink(BLOCKS_FILENAME);
}

/*
 * Create as many entries in the directory as we can, then once we start
 * hitting ENOSPC, try NUMTRIES additional times.  Note that we don't be able
 * to tell the difference between running out of inodes and running out of
 * room to extend the directory, so this is just a best effort.
 */
static void
fill_inodes(void)
{
	char path[PATH_MAX];
	int fd, i, max;

	/*
	 * First step, fill the directory.
	 */
	i = 0;
	while (1) {
		snprintf(path, PATH_MAX, "%d", i);
		fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
		if (fd < 0)
			break;
		close(fd);
		i++;
	}
	max = i;
	if (errno != ENOSPC) {
		warn("fill_inodes: open(%s)", path);
		goto teardown;
	}

	for (i = 0; i < NUMTRIES; i++) {
		fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
		if (fd < 0 && errno != ENOSPC) {
			warn("fill_inodes: open(%s) after ENOSPC", path);
			goto teardown;
		}
		if (fd >= 0) {
			warnx("fill_inodes: open(%s) after ENOSPC returned "
			    " %d", path, fd);
			close(fd);
			goto teardown;
		}
	}

teardown:
	for (i = 0; i < max; i++) {
		snprintf(path, PATH_MAX, "%d", i);
		(void)unlink(path);
	}
}

int
main(int argc, char *argv[])
{

	if (argc != 2)
		err(-1, "usage: ufs_uprintf /non_optional_path");

	if (chdir(argv[1]) < 0)
		err(-1, "chdir(%s)", argv[1]);

	fill_blocks();

	fill_inodes();

	return (0);
}
