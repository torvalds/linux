/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)readmmap.c	1.4	07/05/25 SMI"

/*
 * --------------------------------------------------------------
 *	BugId 5047993 : Getting bad read data.
 *
 *	Usage: readmmap <filename>
 *
 *	where:
 *		filename is an absolute path to the file name.
 *
 *	Return values:
 *		1 : error
 *		0 : no errors
 * --------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

int
main(int argc, char **argv)
{
	char *filename = "badfile";
	size_t size = 4395;
	size_t idx = 0;
	char *buf = NULL;
	char *map = NULL;
	int fd = -1, bytes, retval = 0;
	unsigned seed;

	if (argc < 2 || optind == argc) {
		(void) fprintf(stderr,
		    "usage: %s <file name>\n", argv[0]);
		exit(1);
	}

	if ((buf = calloc(1, size)) == NULL) {
		perror("calloc");
		exit(1);
	}

	filename = argv[optind];

	(void) remove(filename);

	fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fd == -1) {
		perror("open to create");
		retval = 1;
		goto end;
	}

	bytes = write(fd, buf, size);
	if (bytes != size) {
		(void) printf("short write: %d != %ud\n", bytes, size);
		retval = 1;
		goto end;
	}

	map = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		retval = 1;
		goto end;
	}
	seed = time(NULL);
	srandom(seed);

	idx = random() % size;
	map[idx] = 1;

	if (msync(map, size, MS_SYNC) != 0) {
		perror("msync");
		retval = 1;
		goto end;
	}

	if (munmap(map, size) != 0) {
		perror("munmap");
		retval = 1;
		goto end;
	}

	bytes = pread(fd, buf, size, 0);
	if (bytes != size) {
		(void) printf("short read: %d != %ud\n", bytes, size);
		retval = 1;
		goto end;
	}

	if (buf[idx] != 1) {
		(void) printf(
		    "bad data from read!  got buf[%ud]=%d, expected 1\n",
		    idx, buf[idx]);
		retval = 1;
		goto end;
	}

	(void) printf("good data from read: buf[%ud]=1\n", idx);
end:
	if (fd != -1) {
		(void) close(fd);
	}
	if (buf != NULL) {
		free(buf);
	}

	return (retval);
}
