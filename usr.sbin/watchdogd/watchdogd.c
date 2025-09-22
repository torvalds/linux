/*	$OpenBSD: watchdogd.c,v 1.16 2023/03/08 04:43:15 guenther Exp $ */

/*
 * Copyright (c) 2005 Marc Balmer <mbalmer@openbsd.org>
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
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/sysctl.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

volatile sig_atomic_t	quit = 0;

__dead void	usage(void);
void		sighdlr(int);
int		main(int, char *[]);

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnq] [-i interval] [-p period]\n",
	    __progname);
	exit(1);
}

void
sighdlr(int signum)
{
	quit = 1;
}

int
main(int argc, char *argv[])
{
	struct rlimit	 rlim;
	const char	*errstr;
	size_t		 len;
	u_int		 interval = 0, period = 30, nperiod;
	int		 ch, trigauto, sauto, speriod;
	int		 quiet = 0, daemonize = 1, retval = 1, do_restore = 1;
	int		 mib[3];

	while ((ch = getopt(argc, argv, "di:np:q")) != -1) {
		switch (ch) {
		case 'd':
			daemonize = 0;
			break;
		case 'i':
			interval = (u_int)strtonum(optarg, 1LL, 86400LL,
			    &errstr);
			if (errstr)
				errx(1, "interval is %s: %s", errstr, optarg);
			break;
		case 'n':
			do_restore = 0;
			break;
		case 'p':
			period = (u_int)strtonum(optarg, 2LL, 86400LL, &errstr);
			if (errstr)
				errx(1, "period is %s: %s", errstr, optarg);
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (interval == 0 && (interval = period / 3) == 0)
		interval = 1;

	if (period <= interval)
		errx(1, "retrigger interval too long");

	/* save kern.watchdog.period and kern.watchdog.auto for restore */
	mib[0] = CTL_KERN;
	mib[1] = KERN_WATCHDOG;
	mib[2] = KERN_WATCHDOG_PERIOD;

	len = sizeof(speriod);
	if (sysctl(mib, 3, &speriod, &len, &period, sizeof(period)) == -1) {
		if (errno == EOPNOTSUPP)
			errx(1, "no watchdog timer available");
		else
			err(1, "can't access kern.watchdog.period");
	}

	mib[2] = KERN_WATCHDOG_AUTO;
	len = sizeof(sauto);
	trigauto = 0;

	if (sysctl(mib, 3, &sauto, &len, &trigauto, sizeof(trigauto)) == -1)
		err(1, "can't access kern.watchdog.auto");

	/* Double check the timeout period, some devices change the value */
	mib[2] = KERN_WATCHDOG_PERIOD;
	len = sizeof(nperiod);
	if (sysctl(mib, 3, &nperiod, &len, NULL, 0) == -1) {
		warnx("can't read back kern.watchdog.period, "
		    "restoring original values");
		goto restore;
	}

	if (nperiod != period && !quiet)
		warnx("period adjusted to %d by device", nperiod);

	if (nperiod <= interval) {
		warnx("retrigger interval %d too long, "
		    "restoring original values", interval);
		goto restore;
	}

	if (daemonize && daemon(0, 0)) {
		warn("can't daemonize, restoring original values");
		goto restore;
	}

	/*
	 * mlockall() below will wire the whole stack up to the limit
	 * thus we have to reduce stack size to avoid resource abuse
	 */
	rlim.rlim_cur = 256 * 1024;
	rlim.rlim_max = 256 * 1024;
	(void)setrlimit(RLIMIT_STACK, &rlim);

	(void)mlockall(MCL_CURRENT | MCL_FUTURE);
	setpriority(PRIO_PROCESS, getpid(), -5);

	signal(SIGTERM, sighdlr);

	retval = 0;
	while (!quit) {
		if (sysctl(mib, 3, NULL, 0, &period, sizeof(period)) == -1)
			quit = retval = 1;
		sleep(interval);
	}

	if (do_restore) {
restore:	sysctl(mib, 3, NULL, 0, &speriod, sizeof(speriod));
		mib[2] = KERN_WATCHDOG_AUTO;
		sysctl(mib, 3, NULL, 0, &sauto, sizeof(sauto));
	}

	return retval;
}
