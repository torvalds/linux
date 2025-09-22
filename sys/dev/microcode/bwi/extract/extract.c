/*	$OpenBSD: extract.c,v 1.4 2014/09/24 00:13:13 doug Exp $ */

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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct header {
	char	filename[64];
	int	filesize;
	int	fileoffset;
};

int
main(int argc, char *argv[])
{
	int		  i, fdin, fdout, nfiles;
	void		 *p;
	struct header	**h;

	if (argc < 2) {
		printf("%s <firmware file>\n", argv[0]);
		exit(1);
	}

	/* open firmware file */
	if ((fdin = open(argv[1], O_RDONLY)) == -1)
		err(1, "open of input file failed");

	/* read first header */
	if (read(fdin, &nfiles, sizeof(nfiles)) < 1)
		err(1, "first header parse failed");
	nfiles = ntohl(nfiles);

	/* allocate space for header struct */
	if ((h = reallocarray(NULL, nfiles, sizeof(*h))) == NULL)
		err(1, "malloc");
	for (i = 0; i < nfiles; i++) {
		if ((h[i] = malloc(sizeof(struct header))) == NULL)
			err(1, "malloc");
	}

	/* read header */
	for (i = 0; i < nfiles; i++) {
		if (read(fdin, h[i]->filename, sizeof(h[i]->filename)) < 1)
			err(1, "filename header read failed\n");
		if (read(fdin, &h[i]->filesize, sizeof(h[i]->filesize)) < 1)
			err(1, "filesize header read failed\n");
		h[i]->filesize = htonl(h[i]->filesize);
		if (read(fdin, &h[i]->fileoffset, sizeof(h[i]->fileoffset)) < 1)
			err(1, "fileoffset header read failed\n");
		h[i]->fileoffset = htonl(h[i]->fileoffset);
	}

	/* write each file */
	for (i = 0; i < nfiles; i++) {
		if ((fdout = open(h[i]->filename, O_CREAT|O_TRUNC|O_RDWR, 0644))
		    == -1)
			err(1, "open of output file failed");
		if ((p = malloc(h[i]->filesize)) == NULL)
			err(1, "malloc");
		if (lseek(fdin, h[i]->fileoffset, SEEK_SET) == -1)
			err(1, "lseek");
		if (read(fdin, p, h[i]->filesize) < 1)
			err(1, "read from input file failed");
		if (write(fdout, p, h[i]->filesize) < 1)
			err(1, "write to output file failed");
		free(p);
		close(fdout);
		printf("extracting %s (filesize %d, fileoffset %d)\n",
		    h[i]->filename, h[i]->filesize, h[i]->fileoffset);
	}

	/* free header space */
	for (i = 0; i < nfiles; i++)
		free(h[i]);
	free(h);

	/* game over */
	close (fdin);

	return (0);
}
