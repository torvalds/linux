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
#pragma ident	"@(#)file_trunc.c	1.2	07/05/25 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <string.h>

#define	FSIZE	256*1024*1024
#define	BSIZE	512

/* Initialize Globals */
static long 	fsize = FSIZE;
static size_t 	bsize = BSIZE;
static int	count = 0;
static int	rflag = 0;
static int	seed = 0;
static int	vflag = 0;
static int	errflag = 0;
static off_t	offset = 0;
static char	*filename = NULL;

static void usage(char *execname);
static void parse_options(int argc, char *argv[]);
static void do_write(int fd);
static void do_trunc(int fd);

static void
usage(char *execname)
{
	(void) fprintf(stderr,
	    "usage: %s [-b blocksize] [-c count] [-f filesize]"
	    " [-o offset] [-s seed] [-r] [-v] filename\n", execname);
	(void) exit(1);
}

int
main(int argc, char *argv[])
{
	int i = 0;
	int fd = -1;

	parse_options(argc, argv);

	fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (fd < 0) {
		perror("open");
		exit(3);
	}

	while (i < count) {
		(void) do_write(fd);
		(void) do_trunc(fd);

		i++;
	}

	(void) close(fd);
	return (0);
}

static void
parse_options(int argc, char *argv[])
{
	int c;

	extern char *optarg;
	extern int optind, optopt;

	count = fsize / bsize;
	seed = time(NULL);
	while ((c = getopt(argc, argv, "b:c:f:o:rs:v")) != -1) {
		switch (c) {
			case 'b':
				bsize = atoi(optarg);
				break;

			case 'c':
				count = atoi(optarg);
				break;

			case 'f':
				fsize = atoi(optarg);
				break;

			case 'o':
				offset = atoi(optarg);
				break;

			case 'r':
				rflag++;
				break;

			case 's':
				seed = atoi(optarg);
				break;

			case 'v':
				vflag++;
				break;

			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an operand\n", optopt);
				errflag++;
				break;

			case '?':
				(void) fprintf(stderr,
				    "Unrecognized option: -%c\n", optopt);
				errflag++;
				break;
		}

		if (errflag) {
			(void) usage(argv[0]);
		}
	}
	if (argc <= optind) {
		(void) fprintf(stderr,
		    "No filename specified\n");
		usage(argv[0]);
	}
	filename = argv[optind];

	if (vflag) {
		(void) fprintf(stderr, "Seed = %d\n", seed);
	}
	srandom(seed);
}

static void
do_write(int fd)
{
	off_t	roffset = 0;
	char 	*buf = NULL;
	char 	*rbuf = NULL;

	buf = (char *)calloc(1, bsize);
	rbuf = (char *)calloc(1, bsize);
	if (buf == NULL || rbuf == NULL) {
		perror("malloc");
		exit(4);
	}

	roffset = random() % fsize;
	if (lseek(fd, (offset + roffset), SEEK_SET) < 0) {
		perror("lseek");
		exit(5);
	}

	strcpy(buf, "ZFS Test Suite Truncation Test");
	if (write(fd, buf, bsize) < bsize) {
		perror("write");
		exit(6);
	}

	if (rflag) {
		if (lseek(fd, (offset + roffset), SEEK_SET) < 0) {
			perror("lseek");
			exit(7);
		}

		if (read(fd, rbuf, bsize) < bsize) {
			perror("read");
			exit(8);
		}

		if (memcmp(buf, rbuf, bsize) != 0) {
			perror("memcmp");
			exit(9);
		}
	}
	if (vflag) {
		(void) fprintf(stderr,
		    "Wrote to offset %ld\n", (offset + roffset));
		if (rflag) {
			(void) fprintf(stderr,
			    "Read back from offset %ld\n", (offset + roffset));
		}
	}

	(void) free(buf);
	(void) free(rbuf);
}

static void
do_trunc(int fd)
{
	off_t   roffset = 0;

	roffset = random() % fsize;
	if (ftruncate(fd, (offset + roffset))  < 0) {
		perror("truncate");
		exit(7);
	}

	if (vflag) {
		(void) fprintf(stderr,
		    "Truncated at offset %ld\n",
		    (offset + roffset));
	}
}
