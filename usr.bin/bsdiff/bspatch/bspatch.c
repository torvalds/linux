/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#if defined(__FreeBSD__)
#include <sys/param.h>
#if __FreeBSD_version >= 1001511
#include <sys/capsicum.h>
#define HAVE_CAPSICUM
#endif
#endif

#include <bzlib.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#define HEADER_SIZE 32

static char *newfile;
static int dirfd = -1;

static void
exit_cleanup(void)
{

	if (dirfd != -1 && newfile != NULL)
		if (unlinkat(dirfd, newfile, 0))
			warn("unlinkat");
}

static off_t offtin(u_char *buf)
{
	off_t y;

	y = buf[7] & 0x7F;
	y = y * 256; y += buf[6];
	y = y * 256; y += buf[5];
	y = y * 256; y += buf[4];
	y = y * 256; y += buf[3];
	y = y * 256; y += buf[2];
	y = y * 256; y += buf[1];
	y = y * 256; y += buf[0];

	if (buf[7] & 0x80)
		y = -y;

	return (y);
}

static void
usage(void)
{

	fprintf(stderr, "usage: bspatch oldfile newfile patchfile\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	FILE *f, *cpf, *dpf, *epf;
	BZFILE *cpfbz2, *dpfbz2, *epfbz2;
	char *directory, *namebuf;
	int cbz2err, dbz2err, ebz2err;
	int newfd, oldfd;
	off_t oldsize, newsize;
	off_t bzctrllen, bzdatalen;
	u_char header[HEADER_SIZE], buf[8];
	u_char *old, *new;
	off_t oldpos, newpos;
	off_t ctrl[3];
	off_t i, lenread, offset;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights_dir, rights_ro, rights_wr;
#endif

	if (argc != 4)
		usage();

	/* Open patch file */
	if ((f = fopen(argv[3], "rb")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	/* Open patch file for control block */
	if ((cpf = fopen(argv[3], "rb")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	/* open patch file for diff block */
	if ((dpf = fopen(argv[3], "rb")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	/* open patch file for extra block */
	if ((epf = fopen(argv[3], "rb")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	/* open oldfile */
	if ((oldfd = open(argv[1], O_RDONLY | O_BINARY, 0)) < 0)
		err(1, "open(%s)", argv[1]);
	/* open directory where we'll write newfile */
	if ((namebuf = strdup(argv[2])) == NULL ||
	    (directory = dirname(namebuf)) == NULL ||
	    (dirfd = open(directory, O_DIRECTORY)) < 0)
		err(1, "open %s", argv[2]);
	free(namebuf);
	if ((newfile = basename(argv[2])) == NULL)
		err(1, "basename");
	/* open newfile */
	if ((newfd = openat(dirfd, newfile,
	    O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666)) < 0)
		err(1, "open(%s)", argv[2]);
	atexit(exit_cleanup);

#ifdef HAVE_CAPSICUM
	if (cap_enter() < 0)
		err(1, "failed to enter security sandbox");

	cap_rights_init(&rights_ro, CAP_READ, CAP_FSTAT, CAP_SEEK);
	cap_rights_init(&rights_wr, CAP_WRITE);
	cap_rights_init(&rights_dir, CAP_UNLINKAT);

	if (cap_rights_limit(fileno(f), &rights_ro) < 0 ||
	    cap_rights_limit(fileno(cpf), &rights_ro) < 0 ||
	    cap_rights_limit(fileno(dpf), &rights_ro) < 0 ||
	    cap_rights_limit(fileno(epf), &rights_ro) < 0 ||
	    cap_rights_limit(oldfd, &rights_ro) < 0 ||
	    cap_rights_limit(newfd, &rights_wr) < 0 ||
	    cap_rights_limit(dirfd, &rights_dir) < 0)
		err(1, "cap_rights_limit() failed, could not restrict"
		    " capabilities");
#endif

	/*
	File format:
		0	8	"BSDIFF40"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(diff block)
		32+X+Y	???	bzip2(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/

	/* Read header */
	if (fread(header, 1, HEADER_SIZE, f) < HEADER_SIZE) {
		if (feof(f))
			errx(1, "Corrupt patch");
		err(1, "fread(%s)", argv[3]);
	}

	/* Check for appropriate magic */
	if (memcmp(header, "BSDIFF40", 8) != 0)
		errx(1, "Corrupt patch");

	/* Read lengths from header */
	bzctrllen = offtin(header + 8);
	bzdatalen = offtin(header + 16);
	newsize = offtin(header + 24);
	if (bzctrllen < 0 || bzctrllen > OFF_MAX - HEADER_SIZE ||
	    bzdatalen < 0 || bzctrllen + HEADER_SIZE > OFF_MAX - bzdatalen ||
	    newsize < 0 || newsize > SSIZE_MAX)
		errx(1, "Corrupt patch");

	/* Close patch file and re-open it via libbzip2 at the right places */
	if (fclose(f))
		err(1, "fclose(%s)", argv[3]);
	offset = HEADER_SIZE;
	if (fseeko(cpf, offset, SEEK_SET))
		err(1, "fseeko(%s, %jd)", argv[3], (intmax_t)offset);
	if ((cpfbz2 = BZ2_bzReadOpen(&cbz2err, cpf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", cbz2err);
	offset += bzctrllen;
	if (fseeko(dpf, offset, SEEK_SET))
		err(1, "fseeko(%s, %jd)", argv[3], (intmax_t)offset);
	if ((dpfbz2 = BZ2_bzReadOpen(&dbz2err, dpf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", dbz2err);
	offset += bzdatalen;
	if (fseeko(epf, offset, SEEK_SET))
		err(1, "fseeko(%s, %jd)", argv[3], (intmax_t)offset);
	if ((epfbz2 = BZ2_bzReadOpen(&ebz2err, epf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", ebz2err);

	if ((oldsize = lseek(oldfd, 0, SEEK_END)) == -1 ||
	    oldsize > SSIZE_MAX ||
	    (old = malloc(oldsize)) == NULL ||
	    lseek(oldfd, 0, SEEK_SET) != 0 ||
	    read(oldfd, old, oldsize) != oldsize ||
	    close(oldfd) == -1)
		err(1, "%s", argv[1]);
	if ((new = malloc(newsize)) == NULL)
		err(1, NULL);

	oldpos = 0;
	newpos = 0;
	while (newpos < newsize) {
		/* Read control data */
		for (i = 0; i <= 2; i++) {
			lenread = BZ2_bzRead(&cbz2err, cpfbz2, buf, 8);
			if ((lenread < 8) || ((cbz2err != BZ_OK) &&
			    (cbz2err != BZ_STREAM_END)))
				errx(1, "Corrupt patch");
			ctrl[i] = offtin(buf);
		}

		/* Sanity-check */
		if (ctrl[0] < 0 || ctrl[0] > INT_MAX ||
		    ctrl[1] < 0 || ctrl[1] > INT_MAX)
			errx(1, "Corrupt patch");

		/* Sanity-check */
		if (newpos + ctrl[0] > newsize)
			errx(1, "Corrupt patch");

		/* Read diff string */
		lenread = BZ2_bzRead(&dbz2err, dpfbz2, new + newpos, ctrl[0]);
		if ((lenread < ctrl[0]) ||
		    ((dbz2err != BZ_OK) && (dbz2err != BZ_STREAM_END)))
			errx(1, "Corrupt patch");

		/* Add old data to diff string */
		for (i = 0; i < ctrl[0]; i++)
			if ((oldpos + i >= 0) && (oldpos + i < oldsize))
				new[newpos + i] += old[oldpos + i];

		/* Adjust pointers */
		newpos += ctrl[0];
		oldpos += ctrl[0];

		/* Sanity-check */
		if (newpos + ctrl[1] > newsize)
			errx(1, "Corrupt patch");

		/* Read extra string */
		lenread = BZ2_bzRead(&ebz2err, epfbz2, new + newpos, ctrl[1]);
		if ((lenread < ctrl[1]) ||
		    ((ebz2err != BZ_OK) && (ebz2err != BZ_STREAM_END)))
			errx(1, "Corrupt patch");

		/* Adjust pointers */
		newpos+=ctrl[1];
		oldpos+=ctrl[2];
	}

	/* Clean up the bzip2 reads */
	BZ2_bzReadClose(&cbz2err, cpfbz2);
	BZ2_bzReadClose(&dbz2err, dpfbz2);
	BZ2_bzReadClose(&ebz2err, epfbz2);
	if (fclose(cpf) || fclose(dpf) || fclose(epf))
		err(1, "fclose(%s)", argv[3]);

	/* Write the new file */
	if (write(newfd, new, newsize) != newsize || close(newfd) == -1)
		err(1, "%s", argv[2]);
	/* Disable atexit cleanup */
	newfile = NULL;

	free(new);
	free(old);

	return (0);
}
