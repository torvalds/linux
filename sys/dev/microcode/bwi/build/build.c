/*	$OpenBSD: build.c,v 1.4 2014/07/20 02:03:21 guenther Exp $ */

/*
 * Copyright (c) 2006 Marcus Glocker <mglocker@openbsd.org>
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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#define VERBOSE		1
#define FILENAME	"bwi-airforce"

struct header {
	char	filename[64];
	int	filesize;
	int	fileoffset;
};

int
main(int argc, char *argv[])
{
	void		*p;
	int		 i, j, offset;
	int		 fdout, fdin;
	int		 nfiles, headersize;
	int		 fwsize, totalsize;
	struct header	 h[argc - 1];
	struct stat	 s;

	if (argc < 2) {
		printf("%s <firmware files>\n", argv[0]);
		exit(1);
	}

	nfiles = argc - 1; /* number of firmware files */
	headersize = sizeof(h) + sizeof(nfiles); /* size of file header */

	/* initialize header struct */
	for (i = 1, j = 0, fwsize = 0; i < argc; i++, j++) {
		bzero(h[j].filename, sizeof(h[j].filename));
		strlcpy(h[j].filename, argv[i], sizeof(h[j].filename));

		if (stat(h[j].filename, &s) == -1)
			err(1, "header initialization failed");

		h[j].filesize = s.st_size;
		h[j].fileoffset = 0;

		fwsize += h[j].filesize;
#ifdef VERBOSE
		printf("create header entry for %s (%d bytes)\n",
		    h[j].filename, h[j].filesize);
#endif
	}

	/* calculate total file size */
	totalsize = headersize + fwsize;
#if VERBOSE
	printf("\n");
	printf("header size = %d bytes, ", headersize);
	printf("fw size = %d bytes, ", fwsize);
	printf("total file size = %d bytes\n", totalsize);
	printf("\n");
#endif

	/* calculating firmware offsets */
	for (i = 0, offset = headersize; i < nfiles; i++) {
		h[i].fileoffset = offset;
		offset += h[i].filesize;
#ifdef VERBOSE
		printf("offset of %s = %d\n", h[i].filename, h[i].fileoffset);
#endif
	}

	/* open output file */
	if ((fdout = open(FILENAME, O_CREAT|O_TRUNC|O_RDWR, 0644)) == -1)
		err(1, "open output file failed");

	/* host to network byte order */
	for (i = 0; i < nfiles; i++) {
		h[i].filesize = htonl(h[i].filesize);
		h[i].fileoffset = htonl(h[i].fileoffset);
	}
	nfiles = htonl(nfiles);

	/* write header */
	if (write(fdout, &nfiles, sizeof(nfiles)) < 1) {
		int saved_errno = errno;
		close(fdout);
		errc(1, saved_errno, "write header 1 to output file failed");
	}
	if (write(fdout, h, headersize - sizeof(nfiles)) < 1) {
		int saved_errno = errno;
		close(fdout);
		errc(1, saved_errno, "write header 2 to output file failed");
	}

	/* network to host byte order */
	nfiles = ntohl(nfiles);
	for (i = 0; i < nfiles; i++) {
		h[i].filesize = ntohl(h[i].filesize);
		h[i].fileoffset = ntohl(h[i].fileoffset);
	}

	/* write each file */
	for (i = 0; i < nfiles; i++) {
		if ((fdin = open(h[i].filename, O_RDONLY)) == -1) {
			int saved_errno = errno;
			close(fdout);
			errc(1, saved_errno, "open input file failed");
		}
		if ((p = malloc(h[i].filesize)) == NULL) {
			int saved_errno = errno;
			close(fdout);
			close(fdin);
			errc(1, saved_errno, "malloc");
		}
		if (read(fdin, p, h[i].filesize) < 1) {
			int saved_errno = errno;
			free(p);
			close(fdout);
			close(fdin);
			errc(1, saved_errno, "read input file failed");
		}
		if (write(fdout, p, h[i].filesize) < 1) {
			int saved_errno = errno;
			free(p);
			close(fdout);
			close(fdin);
			errc(1, saved_errno, "write to output file failed");
		}
		free(p);
		close(fdin);
	}

	close(fdout);

#ifdef VERBOSE
	printf("\n");
#endif

	/* game over */
	printf("wrote %d files to %s (%d bytes).\n",
	    nfiles, FILENAME, totalsize);

	return (0);
}
