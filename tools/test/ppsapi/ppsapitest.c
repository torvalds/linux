/*-
 * Copyright (c) 1998-2003 Poul-Henning Kamp
 *
 * Please see src/share/examples/etc/bsd-style-copyright.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <sys/timepps.h>

static int aflag, Aflag, cflag, Cflag, eflag, uflag, vflag;

static void
Chew(struct timespec *tsa, struct timespec *tsc, unsigned sa, unsigned sc)
{
	printf("%jd .%09ld %u", (intmax_t)tsa->tv_sec, tsa->tv_nsec, sa);
	printf(" %jd .%09ld %u\n", (intmax_t)tsc->tv_sec, tsc->tv_nsec, sc);
	if (uflag)
		fflush(stdout);
}

int
main(int argc, char **argv)
{
	int fd;
	FILE *fdo;
	pps_info_t pi;
	pps_params_t pp;
	pps_handle_t ph;
	int i, mode;
	u_int olda, oldc;
	struct timespec to;
	char const *ofn;

	ofn = NULL;
	while ((i = getopt(argc, argv, "aAbBcCeo:uv")) != -1) {
		switch (i) {
		case 'a': aflag = 1; break;
		case 'A': Aflag = 1; break;
		case 'b': aflag = 1; cflag = 1; break;
		case 'B': Aflag = 1; Cflag = 1; break;
		case 'c': cflag = 1; break;
		case 'C': Cflag = 1; break;
		case 'e': eflag = 1; break;
		case 'o': ofn = optarg; break;
		case 'u': uflag = 1; break;
		case 'v': vflag = 1; break;
		case '?':
		default:
			fprintf(stderr,
			    "Usage: ppsapitest [-aAcC] device\n");
			exit (1);
		}
	}
	if (ofn != NULL) {
		fdo = fopen(ofn, "w");
		if (fdo == NULL)
			err(1, "Cannot open %s", ofn);
	} else {
		fdo = NULL;
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		fd = open(argv[0], O_RDONLY);
		if (fd < 0) 
			err(1, "%s", argv[0]);
	} else {
		fd = 0;
	}
	i = time_pps_create(fd, &ph);
	if (i < 0)
		err(1, "time_pps_create");

	i = time_pps_getcap(ph, &mode);
	if (i < 0)
		err(1, "time_pps_getcap");
	if (vflag) {
		fprintf(stderr, "Supported modebits:");
		if (mode & PPS_CAPTUREASSERT)
			fprintf(stderr, " CAPTUREASSERT");
		if (mode & PPS_CAPTURECLEAR)
			fprintf(stderr, " CAPTURECLEAR");
		if (mode & PPS_OFFSETASSERT)
			fprintf(stderr, " OFFSETASSERT");
		if (mode & PPS_OFFSETCLEAR)
			fprintf(stderr, " OFFSETCLEAR");
		if (mode & PPS_ECHOASSERT)
			fprintf(stderr, " ECHOASSERT");
		if (mode & PPS_ECHOCLEAR)
			fprintf(stderr, " ECHOCLEAR");
		if (mode & PPS_CANWAIT)
			fprintf(stderr, " CANWAIT");
		if (mode & PPS_CANPOLL)
			fprintf(stderr, " CANPOLL");
		if (mode & PPS_TSFMT_TSPEC)
			fprintf(stderr, " TSPEC");
		if (mode & PPS_TSFMT_NTPFP)
			fprintf(stderr, " NTPFP");
		fprintf(stderr, "\n");
	}

	if (!aflag && !cflag) {
		if (mode & PPS_CAPTUREASSERT)
			aflag = 1;
		if (mode & PPS_CAPTURECLEAR)
			cflag = 1;
	}
	if (!Aflag && !Cflag) {
		Aflag = aflag;
		Cflag = cflag;
	}

	if (Cflag && !(mode & PPS_CAPTURECLEAR))
		errx(1, "-C but cannot capture on clear flank");

	if (Aflag && !(mode & PPS_CAPTUREASSERT))
		errx(1, "-A but cannot capture on assert flank");

	i = time_pps_getparams(ph, &pp);
	if (i < 0)
		err(1, "time_pps_getparams():");

	if (aflag)
		pp.mode |= PPS_CAPTUREASSERT;
	if (cflag)
		pp.mode |= PPS_CAPTURECLEAR;

	if (eflag & aflag)
		pp.mode |= PPS_ECHOASSERT;

	if (eflag & cflag)
		pp.mode |= PPS_ECHOCLEAR;

	if (!(pp.mode & PPS_TSFMT_TSPEC))
		pp.mode |= PPS_TSFMT_TSPEC;
	
	i = time_pps_setparams(ph, &pp);
	if (i < 0) {
		err(1, "time_pps_setparams(mode %x):", pp.mode);
	}

	/*
	 * Pick up first event outside the loop in order to not
	 * get something ancient into the outfile.
	 */
	to.tv_nsec = 0;
	to.tv_sec = 0;
	i = time_pps_fetch(ph, PPS_TSFMT_TSPEC, &pi, &to);
	if (i < 0)
		err(1, "time_pps_fetch()");
	olda = pi.assert_sequence;
	oldc = pi.clear_sequence;

	while (1) {
		to.tv_nsec = 0;
		to.tv_sec = 0;
		i = time_pps_fetch(ph, PPS_TSFMT_TSPEC, &pi, &to);
		if (i < 0)
			err(1, "time_pps_fetch()");
		if (oldc != pi.clear_sequence && Cflag)
			;
		else if (olda != pi.assert_sequence && Aflag)
			;
		else {
			usleep(10000);
			continue;
		}
		if (fdo != NULL) {
			if (fwrite(&pi, sizeof pi, 1, fdo) != 1)
				err(1, "Write error on %s", ofn);
			if (uflag)
				fflush(fdo);
		}
		Chew(&pi.assert_timestamp, &pi.clear_timestamp,
			pi.assert_sequence, pi.clear_sequence);
		olda = pi.assert_sequence;
		oldc = pi.clear_sequence;
	}
	return(0);
}
