/*	$OpenBSD: fdformat.c,v 1.25 2022/12/04 23:50:50 cheloha Exp $	*/

/*
 * Copyright (C) 1992-1994 by Joerg Wunsch, Dresden
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
 */

/*
 * FreeBSD:
 * format a floppy disk
 * 
 * Added FD_GTYPE ioctl, verifying, proportional indicators.
 * Serge Vakulenko, vak@zebub.msk.su
 * Sat Dec 18 17:45:47 MSK 1993
 *
 * Final adaptation, change format/verify logic, add separate
 * format gap/interleave values
 * Andrew A. Chernov, ache@astral.msk.su
 * Thu Jan 27 00:47:24 MSK 1994
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <err.h>
#include <util.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <machine/ioctl_fd.h>

extern const char *__progname;

static void
format_track(int fd, int cyl, int secs, int head, int rate, int gaplen,
    int secsize, int fill, int interleave)
{
	struct fd_formb f;
	int i,j;
	int il[FD_MAX_NSEC + 1];

	memset(il,0,sizeof il);
	for(j = 0, i = 1; i <= secs; i++) {
		while(il[(j%secs)+1])
			j++;
		il[(j%secs)+1] = i;
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
	if (ioctl(fd, FD_FORM, (caddr_t)&f) == -1)
		err(1, "FD_FORM");
}

static int
verify_track(int fd, int track, int tracksize)
{
	static char *buf = 0;
	static int bufsz = 0;
	int fdopts = -1, ofdopts, rv = 0;

	if (ioctl(fd, FD_GOPTS, &fdopts) == -1)
		warn("FD_GOPTS");
	else {
		ofdopts = fdopts;
		fdopts |= FDOPT_NORETRY;
		(void)ioctl(fd, FD_SOPTS, &fdopts);
	}
	
	if (bufsz < tracksize) {
		free(buf);
		bufsz = tracksize;
		buf = 0;
	}
	if (! buf)
		buf = malloc(bufsz);
	if (! buf) {
		fprintf (stderr, "\nfdformat: out of memory\n");
		exit (2);
	}
	if (lseek (fd, (off_t) track*tracksize, SEEK_SET) == -1)
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
usage(void)
{
	printf("usage: %s [-nqv] [-c cyls] [-F fillbyte] [-g gap3len] "
	    "[-h heads]\n"
	    "	[-i intleave] [-r rate] [-S secshft] [-s secs]\n"
	    "	[-t steps_per_track] device_name\n", __progname);
	exit(2);
}

static int
yes(void)
{
	char reply[256], *p;

	for (;;) {
		fflush(stdout);
		if (!fgets(reply, sizeof(reply), stdin))
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
main(int argc, char *argv[])
{
	int cyls = -1, secs = -1, heads = -1, intleave = -1;
	int rate = -1, gaplen = -1, secsize = -1, steps = -1;
	int fill = 0xf6, quiet = 0, verify = 1, verify_only = 0;
	int fd, c, track, error, tracks_per_dot, bytes_per_track, errs;
	const char *errstr;
	char *devname;
	struct fd_type fdt;

	while((c = getopt(argc, argv, "c:s:h:r:g:S:F:t:i:qvn")) != -1)
		switch (c) {
		case 'c':       /* # of cyls */
			cyls = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-c %s: %s", optarg, errstr);
			break;

		case 's':       /* # of secs per track */
			secs = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-s %s: %s", optarg, errstr);
			break;

		case 'h':       /* # of heads */
			heads = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-h %s: %s", optarg, errstr);
			break;

		case 'r':       /* transfer rate, kilobyte/sec */
			rate = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-r %s: %s", optarg, errstr);
			break;

		case 'g':       /* length of GAP3 to format with */
			gaplen = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-g %s: %s", optarg, errstr);
			break;

		case 'S':       /* sector size shift factor (1 << S)*128 */
			secsize = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-S %s: %s", optarg, errstr);
			break;

		case 'F':       /* fill byte, C-like notation allowed */
			fill = (int)strtol(optarg, NULL, 0);
			break;

		case 't':       /* steps per track */
			steps = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-t %s: %s", optarg, errstr);
			break;

		case 'i':       /* interleave factor */
			intleave = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-i %s: %s", optarg, errstr);
			break;

		case 'q':
			quiet = 1;
			break;

		case 'n':
			verify = 0;
			break;

		case 'v':
			verify = 1;
			verify_only = 1;
			break;

		default:
			usage();
		}

	if (optind != argc - 1)
		usage();

	if ((fd = opendev(argv[optind], O_RDWR, OPENDEV_PART, &devname)) == -1)
		err(1, "%s", devname);

	if (ioctl(fd, FD_GTYPE, &fdt) == -1)
		errx(1, "not a floppy disk: %s", devname);

	switch (rate) {
	case -1:
		break;
	case 250:
		fdt.rate = FDC_250KBPS;
		break;
	case 300:
		fdt.rate = FDC_300KBPS;
		break;
	case 500:
		fdt.rate = FDC_500KBPS;
		break;
	default:
		errx(1, "invalid transfer rate: %d", rate);
	}

	if (cyls >= 0)
		fdt.tracks = cyls;
	if (secs >= 0)
		fdt.sectrac = secs;
	if (fdt.sectrac > FD_MAX_NSEC)
		errx(1, "too many sectors per track, max value is %d",
			FD_MAX_NSEC);
	if (heads >= 0)
		fdt.heads = heads;
	if (gaplen >= 0)
		fdt.gap2 = gaplen;
	if (secsize >= 0)
		fdt.secsize = secsize;
	if (steps >= 0)
		fdt.step = steps;

	bytes_per_track = fdt.sectrac * (1<<fdt.secsize) * 128;
	tracks_per_dot = fdt.tracks * fdt.heads / 40;
	if (tracks_per_dot == 0)
		tracks_per_dot++;

	if (verify_only) {
		if (!quiet)
			printf("Verify %dK floppy `%s'.\n",
				fdt.tracks * fdt.heads * bytes_per_track / 1024,
				devname);
	}
	else if (!quiet) {
		printf("Format %dK floppy `%s'? (y/n): ",
			fdt.tracks * fdt.heads * bytes_per_track / 1024,
			devname);
		if (!yes()) {
			printf("Not confirmed.\n");
			exit(0);
		}
	}

	/*
	 * Formatting.
	 */
	if (!quiet) {
		printf("Processing ");
		for (track = 0; track < fdt.tracks * fdt.heads; track++) {
			if (!((track + 1) % tracks_per_dot))
				putchar('-');
		}
		putchar('\r');
		printf("Processing ");
		fflush(stdout);
	}

	error = errs = 0;

	for (track = 0; track < fdt.tracks * fdt.heads; track++) {
		if (!verify_only) {
			format_track(fd, track / fdt.heads, fdt.sectrac,
				track % fdt.heads, fdt.rate, fdt.gap2,
				     fdt.secsize, fill,
				     intleave >= 0 ? intleave : 1);
			if (!quiet && !((track + 1) % tracks_per_dot)) {
				putchar('F');
				fflush(stdout);
			}
		}
		if (verify) {
			if (verify_track(fd, track, bytes_per_track) < 0)
				error = errs = 1;
			if (!quiet && !((track + 1) % tracks_per_dot)) {
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
	close(fd);
	if (!quiet)
		printf(" done.\n");

	exit(errs);
}
