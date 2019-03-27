/*-
 * Copyright (c) 2001-2013
 *	HATANO Tomomi.  All rights reserved.
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

#ifndef	lint
static char rcsid[] = "$Id: mkfile.c,v 1.5 2013-10-26 10:11:34+09 hatanou Exp $";
#endif	/* !lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#define	MKFILE_WBUF	((size_t)(1048576))	/* Is 1M a reasonable value? */

/* SunOS's mkfile(8) sets "sticky bit." */
#define	MKFILE_FLAG	(O_WRONLY | O_CREAT | O_TRUNC)
#define	MKFILE_MODE	(S_IRUSR | S_IWUSR | S_ISVTX)

static char	buf[MKFILE_WBUF];
static int	nofill = 0;
static int	verbose = 0;

static void
usage()
{
	fprintf(stderr,
	    "Usage: mkfile [-nv] <size>[e|p|t|g|m|k|b] <filename> ...\n");
}

static unsigned long long
getsize(char *s)
{
	int sh;
	unsigned long long length;
	char *suffix;

	/*
	 * NOTE: We don't handle 'Z' (zetta) or 'Y' (yotta) suffixes yet.
	 * These are too large to store in unsigned long long (64bits).
	 * In the future, we'll have to use larger type,
	 * something like uint128_t.
	 */
	length = strtoull(s, &suffix, 10);
	sh = 0;
	switch (tolower(*suffix)) {
	case 'e':	/* Exabytes. */
		sh = 60;
		break;
	case 'p':	/* Petabytes. */
		sh = 50;
		break;
	case 't':	/* Terabytes. */
		sh = 40;
		break;
	case 'g':	/* Gigabytes. */
		sh = 30;
		break;
	case 'm':	/* Megabytes. */
		sh = 20;
		break;
	case 'k':	/* Kilobytes. */
		sh = 10;
		break;
	case 'b':	/* Blocks. */
		sh = 9;
		break;
	case '\0':	/* Bytes. */
		break;
	default:	/* Unknown... */
		errno = EINVAL;
		return 0;
	}
	if (sh) {
		unsigned long long l;

		l = length;
		length <<= sh;
		/* Check overflow. */
		if ((length >> sh) != l) {
			errno = ERANGE;
			return 0;
		}
	}

	return length;
}

static int
create_file(char *f, unsigned long long s)
{
	int fd;
	size_t w;
	ssize_t ws;

	if (verbose) {
		fprintf(stdout, "%s %llu bytes\n", f, s);
		fflush(stdout);
	}

	/* Open file to create. */
	if ((fd = open(f, MKFILE_FLAG, MKFILE_MODE)) < 0) {
		return -1;
	}

	/* Seek to the end and write 1 byte. */
	if ((lseek(fd, (off_t)(s - 1LL), SEEK_SET) == (off_t)-1) ||
	    (write(fd, buf, (size_t)1) == (ssize_t)-1)) {
		/*
		 * We don't close(fd) here to avoid overwriting errno.
		 * This is fd-leak, but is not harmful
		 * because returning error causes mkfile(8) to exit.
		 */
		return -1;
	}

	/* Fill. */
	if (!nofill) {
		if (lseek(fd, (off_t)0, SEEK_SET) == (off_t)-1) {
			/* Same as above. */
			return -1;
		}
		while (s) {
			w = (s > MKFILE_WBUF) ? MKFILE_WBUF : s;
			if ((ws = write(fd, buf, w)) == (ssize_t)-1) {
				/* Same as above. */
				return -1;
			}
			s -= ws;
		}
	}
	close(fd);

	return 0;
}

int
main(int argc, char *argv[])
{
	unsigned long long fsize;
	char ch;

	/* We have at least 2 arguments. */
	if (argc < 3) {
		usage();
		return EXIT_FAILURE;
	}

	/* Options. */
	while ((ch = getopt(argc, argv, "nv")) != -1) {
		switch (ch) {
		case 'n':
			nofill = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}
	argc -= optind;
	argv += optind;

	/* File size to create. */
	if ((fsize = getsize(*argv)) == 0) {
		perror(*argv);
		return EXIT_FAILURE;
	}

	/* Filenames to create. */
	bzero(buf, MKFILE_WBUF);
	while (++argv, --argc) {
		if (create_file(*argv, fsize) == -1) {
			perror(*argv);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
