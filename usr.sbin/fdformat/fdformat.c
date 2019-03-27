/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1992-1994,2001 by Joerg Wunsch, Dresden
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/fdcio.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include "fdutil.h"

static void
format_track(int fd, int cyl, int secs, int head, int rate,
	     int gaplen, int secsize, int fill, int interleave,
	     int offset)
{
	struct fd_formb f;
	int i, j, il[FD_MAX_NSEC + 1];

	memset(il, 0, sizeof il);
	for(j = 0, i = 1 + offset; i <= secs + offset; i++) {
	    while(il[(j % secs) + 1])
		    j++;
	    il[(j % secs) + 1] = i;
	    j += interleave;
	}

	f.format_version = FD_FORMAT_VERSION;
	f.head = head;
	f.cyl = cyl;
	f.transfer_rate = rate;

	f.fd_formb_secshift = secsize;
	f.fd_formb_nsecs = secs;
	f.fd_formb_gaplen = gaplen;
	f.fd_formb_fillbyte = fill;
	for(i = 0; i < secs; i++) {
		f.fd_formb_cylno(i) = cyl;
		f.fd_formb_headno(i) = head;
		f.fd_formb_secno(i) = il[i+1];
		f.fd_formb_secsize(i) = secsize;
	}
	(void)ioctl(fd, FD_FORM, (caddr_t)&f);
}

static int
verify_track(int fd, int track, int tracksize)
{
	static char *buf;
	static int bufsz;
	int fdopts = -1, ofdopts, rv = 0;

	if (ioctl(fd, FD_GOPTS, &fdopts) < 0)
		warn("warning: ioctl(FD_GOPTS)");
	else {
		ofdopts = fdopts;
		fdopts |= FDOPT_NORETRY;
		(void)ioctl(fd, FD_SOPTS, &fdopts);
	}

	if (bufsz < tracksize)
		buf = realloc(buf, bufsz = tracksize);
	if (buf == NULL)
		errx(EX_UNAVAILABLE, "out of memory");
	if (lseek (fd, (long) track * tracksize, 0) < 0)
		rv = -1;
	/* try twice reading it, without using the normal retrier */
	else if (read (fd, buf, tracksize) != tracksize
		 && read (fd, buf, tracksize) != tracksize)
		rv = -1;
	if (fdopts != -1)
		(void)ioctl(fd, FD_SOPTS, &ofdopts);
	return (rv);
}

static void
usage (void)
{
	errx(EX_USAGE,
	     "usage: fdformat [-F fill] [-f fmt] [-s fmtstr] [-nqvy] device");
}

static int
yes (void)
{
	char reply[256], *p;

	reply[sizeof(reply) - 1] = 0;
	for (;;) {
		fflush(stdout);
		if (!fgets (reply, sizeof(reply) - 1, stdin))
			return (0);
		for (p=reply; *p==' ' || *p=='\t'; ++p)
			continue;
		if (*p=='y' || *p=='Y')
			return (1);
		if (*p=='n' || *p=='N' || *p=='\n' || *p=='\r')
			return (0);
		printf("Answer `yes' or `no': ");
	}
}

int
main(int argc, char **argv)
{
	enum fd_drivetype type;
	struct fd_type fdt, newft, *fdtp;
	struct stat sb;
#define MAXPRINTERRS 10
	struct fdc_status fdcs[MAXPRINTERRS];
	int format, fill, quiet, verify, verify_only, confirm;
	int fd, c, i, track, error, tracks_per_dot, bytes_per_track, errs;
	int flags;
	char *fmtstring, *device;
	const char *name, *descr;

	format = quiet = verify_only = confirm = 0;
	verify = 1;
	fill = 0xf6;
	fmtstring = 0;

	while((c = getopt(argc, argv, "F:f:nqs:vy")) != -1)
		switch(c) {
		case 'F':	/* fill byte */
			if (getnum(optarg, &fill)) {
				fprintf(stderr,
			"Bad argument %s to -F option; must be numeric\n",
					optarg);
				usage();
			}
			break;

		case 'f':	/* format in kilobytes */
			if (getnum(optarg, &format)) {
				fprintf(stderr,
			"Bad argument %s to -f option; must be numeric\n",
					optarg);
				usage();
			}
			break;

		case 'n':	/* don't verify */
			verify = 0;
			break;

		case 'q':	/* quiet */
			quiet = 1;
			break;

		case 's':	/* format string with detailed options */
			fmtstring = optarg;
			break;

		case 'v':	/* verify only */
			verify = 1;
			verify_only = 1;
			break;

		case 'y':	/* confirm */
			confirm = 1;
			break;

		default:
			usage();
		}

	if(optind != argc - 1)
		usage();

	if (stat(argv[optind], &sb) == -1 && errno == ENOENT) {
		/* try prepending _PATH_DEV */
		device = malloc(strlen(argv[optind]) + sizeof(_PATH_DEV) + 1);
		if (device == NULL)
			errx(EX_UNAVAILABLE, "out of memory");
		strcpy(device, _PATH_DEV);
		strcat(device, argv[optind]);
		if (stat(device, &sb) == -1) {
			free(device);
			device = argv[optind]; /* let it fail below */
		}
	} else {
		device = argv[optind];
	}

	if ((fd = open(device, O_RDWR | O_NONBLOCK)) < 0)
		err(EX_OSERR, "open(%s)", device);

	/*
	 * Device initialization.
	 *
	 * First, get the device type descriptor.  This tells us about
	 * the media geometry data we need to format a medium.  It also
	 * lets us know quickly whether the device name actually points
	 * to a floppy disk drive.
	 *
	 * Then, obtain any drive options.  We're mainly interested to
	 * see whether we're currently working on a device with media
	 * density autoselection (FDOPT_AUTOSEL).  Then, we add the
	 * device option to tell the kernel not to log media errors,
	 * since we can handle them ourselves.  If the device does
	 * media density autoselection, we then need to set the device
	 * type appropriately, since by opening with O_NONBLOCK we
	 * told the driver to bypass media autoselection (otherwise we
	 * wouldn't stand a chance to format an unformatted or damaged
	 * medium).  We do not attempt to set the media type on any
	 * other devices since this is a privileged operation.  For the
	 * same reason, specifying -f and -s options is only possible
	 * for autoselecting devices.
	 *
	 * Finally, we are ready to turn off O_NONBLOCK, and start to
	 * actually format something.
	 */
	if(ioctl(fd, FD_GTYPE, &fdt) < 0)
		errx(EX_OSERR, "not a floppy disk: %s", device);
	if (ioctl(fd, FD_GDTYPE, &type) == -1)
		err(EX_OSERR, "ioctl(FD_GDTYPE)");
	if (format) {
		getname(type, &name, &descr);
		fdtp = get_fmt(format, type);
		if (fdtp == NULL)
			errx(EX_USAGE,
			    "unknown format %d KB for drive type %s",
			     format, name);
		fdt = *fdtp;
	}
	if (fmtstring) {
		parse_fmt(fmtstring, type, fdt, &newft);
		fdt = newft;
	}
	if (ioctl(fd, FD_STYPE, &fdt) < 0)
		err(EX_OSERR, "ioctl(FD_STYPE)");
	if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
		err(EX_OSERR, "fcntl(F_GETFL)");
	flags &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1)
		err(EX_OSERR, "fcntl(F_SETFL)");

	bytes_per_track = fdt.sectrac * (128 << fdt.secsize);

	/* XXX  20/40 = 0.5 */
	tracks_per_dot = (fdt.tracks * fdt.heads + 20) / 40;

	if (verify_only) {
		if(!quiet)
			printf("Verify %dK floppy `%s'.\n",
				fdt.tracks * fdt.heads * bytes_per_track / 1024,
				device);
	}
	else if(!quiet && !confirm) {
		printf("Format %dK floppy `%s'? (y/n): ",
			fdt.tracks * fdt.heads * bytes_per_track / 1024,
			device);
		if(!yes()) {
			printf("Not confirmed.\n");
			return (EX_UNAVAILABLE);
		}
	}

	/*
	 * Formatting.
	 */
	if(!quiet) {
		printf("Processing ");
		for (i = 0; i < (fdt.tracks * fdt.heads) / tracks_per_dot; i++)
			putchar('-');
		printf("\rProcessing ");
		fflush(stdout);
	}

	error = errs = 0;

	for (track = 0; track < fdt.tracks * fdt.heads; track++) {
		if (!verify_only) {
			format_track(fd, track / fdt.heads, fdt.sectrac,
				track % fdt.heads, fdt.trans, fdt.f_gap,
				fdt.secsize, fill, fdt.f_inter,
				track % fdt.heads? fdt.offset_side2: 0);
			if(!quiet && !((track + 1) % tracks_per_dot)) {
				putchar('F');
				fflush(stdout);
			}
		}
		if (verify) {
			if (verify_track(fd, track, bytes_per_track) < 0) {
				error = 1;
				if (errs < MAXPRINTERRS && errno == EIO) {
					if (ioctl(fd, FD_GSTAT, fdcs + errs) ==
					    -1)
						errx(EX_IOERR,
					"floppy IO error, but no FDC status");
					errs++;
				}
			}
			if(!quiet && !((track + 1) % tracks_per_dot)) {
				if (!verify_only)
					putchar('\b');
				if (error) {
					putchar('E');
					error = 0;
				}
				else
					putchar('V');
				fflush(stdout);
			}
		}
	}
	if(!quiet)
		printf(" done.\n");

	if (!quiet && errs) {
		fflush(stdout);
		fprintf(stderr, "Errors encountered:\nCyl Head Sect   Error\n");
		for (i = 0; i < errs && i < MAXPRINTERRS; i++) {
			fprintf(stderr, " %2d   %2d   %2d   ",
				fdcs[i].status[3], fdcs[i].status[4],
				fdcs[i].status[5]);
			printstatus(fdcs + i, 1);
			putc('\n', stderr);
		}
		if (errs >= MAXPRINTERRS)
			fprintf(stderr, "(Further errors not printed.)\n");
	}

	return errs != 0;
}
