/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Joerg Wunsch
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE DEVELOPERS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fdcio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <dev/ic/nec765.h>

#include "fdutil.h"

static int	quiet, recover;
static unsigned char fillbyte = 0xf0;	/* "foo" */

static int	doread(int fd, FILE *of, const char *_devname);
static int	doreadid(int fd, unsigned int numids, unsigned int trackno);
static void	usage(void);

static void
usage(void)
{

	errx(EX_USAGE,
	     "usage: fdread [-qr] [-d device] [-f fillbyte]\n"
	     "       fdread [-d device] -I numids [-t trackno]");
}


int
main(int argc, char **argv)
{
	int c, errs = 0;
	unsigned int numids = 0, trackno = 0;
	const char *fname = 0, *_devname = "/dev/fd0";
	char *cp;
	FILE *of = stdout;
	int fd;
	unsigned long ul;

	while ((c = getopt(argc, argv, "d:f:I:o:qrt:")) != -1)
		switch (c) {
		case 'd':
			_devname = optarg;
			break;

		case 'f':
			ul = strtoul(optarg, &cp, 0);
			if (*cp != '\0') {
				fprintf(stderr,
			"Bad argument %s to -f option; must be numeric\n",
					optarg);
				usage();
			}
			if (ul > 0xff)
				warnx(
			"Warning: fillbyte %#lx too large, truncating\n",
				      ul);
			fillbyte = ul & 0xff;
			break;

		case 'I':
			ul = strtoul(optarg, &cp, 0);
			if (*cp != '\0') {
				fprintf(stderr,
			"Bad argument %s to -I option; must be numeric\n",
					optarg);
				usage();
			}
			numids = ul;
			break;

		case 'o':
			fname = optarg;
			break;

		case 'q':
			quiet++;
			break;

		case 'r':
			recover++;
			break;

		case 't':
			ul = strtoul(optarg, &cp, 0);
			if (*cp != '\0') {
				fprintf(stderr,
			"Bad argument %s to -t option; must be numeric\n",
					optarg);
				usage();
			}
			trackno = ul;
			break;

		default:
			errs++;
		}
	argc -= optind;
	argv += optind;

	if (argc != 0 || errs)
		usage();
	/* check for mutually exclusive options */
	if (numids) {
		if (fname || quiet || recover)
			usage();
	} else {
		if (trackno)
			usage();
	}

	if (fname) {
		if ((of = fopen(fname, "w")) == NULL)
			err(EX_OSERR, "cannot create output file %s", fname);
	}

	if ((fd = open(_devname, O_RDONLY)) == -1)
		err(EX_OSERR, "cannot open device %s", _devname);

	return (numids? doreadid(fd, numids, trackno): doread(fd, of, _devname));
}

static int
doread(int fd, FILE *of, const char *_devname)
{
	char *trackbuf;
	int rv, fdopts, recoverable, nerrs = 0;
	unsigned int nbytes, tracksize, mediasize, secsize, n;
	struct fdc_status fdcs;
	struct fd_type fdt;

	if (ioctl(fd, FD_GTYPE, &fdt) == -1)
		err(EX_OSERR, "ioctl(FD_GTYPE) failed -- not a floppy?");

	secsize = 128 << fdt.secsize;
	tracksize = fdt.sectrac * secsize;
	mediasize = tracksize * fdt.tracks * fdt.heads;
	if ((trackbuf = malloc(tracksize)) == NULL)
		errx(EX_TEMPFAIL, "out of memory");

	if (!quiet)
		fprintf(stderr, "Reading %d * %d * %d * %d medium at %s\n",
			fdt.tracks, fdt.heads, fdt.sectrac, secsize, _devname);

	for (nbytes = 0; nbytes < mediasize;) {
		if (lseek(fd, nbytes, SEEK_SET) != nbytes)
			err(EX_OSERR, "cannot lseek()");
		rv = read(fd, trackbuf, tracksize);
		if (rv == 0) {
			/* EOF? */
			warnx("premature EOF after %u bytes", nbytes);
			free(trackbuf);
			return (EX_OK);
		}
		if ((unsigned)rv == tracksize) {
			nbytes += rv;
			if (!quiet)
				fprintf(stderr, "%5d KB\r", nbytes / 1024);
			fwrite(trackbuf, sizeof(unsigned char), rv, of);
			fflush(of);
			continue;
		}
		if (rv == -1) {
			/* fall back reading one sector at a time */
			for (n = 0; n < tracksize; n += secsize) {
				if (lseek(fd, nbytes, SEEK_SET) != nbytes)
					err(EX_OSERR, "cannot lseek()");
				rv = read(fd, trackbuf, secsize);
				if ((unsigned) rv == secsize) {
					nbytes += rv;
					if (!quiet)
						fprintf(stderr, "%5d KB\r",
							nbytes / 1024);
					fwrite(trackbuf, sizeof(unsigned char),
					       rv, of);
					fflush(of);
					continue;
				}
				if (rv == -1) {
					if (errno != EIO) {
						if (!quiet)
							putc('\n', stderr);
						perror("non-IO error");
						free(trackbuf);
						return (EX_OSERR);
					}
					if (ioctl(fd, FD_GSTAT, &fdcs) == -1)
						errx(EX_IOERR,
				     "floppy IO error, but no FDC status");
					nerrs++;
					recoverable = fdcs.status[2] &
						NE7_ST2_DD;
					if (!quiet) {
						printstatus(&fdcs, 0);
						fputs(" (", stderr);
						if (!recoverable)
							fputs("not ", stderr);
						fputs("recoverable)", stderr);
					}
					if (!recover) {
						if (!quiet)
							putc('\n', stderr);
						free(trackbuf);
						return (EX_IOERR);
					}
					memset(trackbuf, fillbyte, secsize);
					if (recoverable) {
						fdopts |= FDOPT_NOERROR;
						if (ioctl(fd, FD_SOPTS,
							  &fdopts) == -1)
							err(EX_OSERR,
				    "ioctl(fd, FD_SOPTS, FDOPT_NOERROR)");
						rv = read(fd, trackbuf,
							  secsize);
						if ((unsigned)rv != secsize)
							err(EX_IOERR,
				    "read() with FDOPT_NOERROR still fails");
						fdopts &= ~FDOPT_NOERROR;
						(void)ioctl(fd, FD_SOPTS,
							    &fdopts);
					}
					if (!quiet) {
						if (recoverable)
							fprintf(stderr,
								": recovered");
						else
							fprintf(stderr,
								": dummy");
						fprintf(stderr,
							" data @ %#x ... %#x\n",
							nbytes,
							nbytes + secsize - 1);
					}
					nbytes += secsize;
					fwrite(trackbuf, sizeof(unsigned char),
					       secsize, of);
					fflush(of);
					continue;
				}
				errx(EX_OSERR, "unexpected read() result: %d",
				     rv);
			}
		}
		if ((unsigned)rv < tracksize) {
			/* should not happen */
			nbytes += rv;
			if (!quiet)
				fprintf(stderr, "\nshort after %5d KB\r",
					nbytes / 1024);
			fwrite(trackbuf, sizeof(unsigned char), rv, of);
			fflush(of);
			continue;
		}
	}
	free(trackbuf);
	if (!quiet) {
		putc('\n', stderr);
		if (nerrs)
			fprintf(stderr, "%d error%s\n",
				nerrs, nerrs > 1? "s": "");
	}

	return (nerrs? EX_IOERR: EX_OK);
}

static int
doreadid(int fd, unsigned int numids, unsigned int trackno)
{
	int rv = 0;
	unsigned int i;
	struct fdc_readid info;
	struct fdc_status fdcs;
	struct fd_type fdt;

	if (ioctl(fd, FD_GTYPE, &fdt) == -1)
		err(EX_OSERR, "ioctl(FD_GTYPE) failed -- not a floppy?");

	for (i = 0; i < numids; i++) {
		info.cyl = trackno / fdt.heads;
		info.head = fdt.heads > 1? trackno % fdt.heads: 0;
		if (ioctl(fd, FD_READID, &info) == 0) {
			printf("C = %d, H = %d, R = %d, N = %d\n",
			       info.cyl, info.head, info.sec, info.secshift);
		} else {
			if (errno != EIO) {
				perror("non-IO error");
				return (EX_OSERR);
			}
			if (ioctl(fd, FD_GSTAT, &fdcs) == -1)
				errx(EX_IOERR,
				     "floppy IO error, but no FDC status");
			printstatus(&fdcs, 0);
			putc('\n', stderr);
			rv = EX_IOERR;
		}
	}

	return (rv);
}
