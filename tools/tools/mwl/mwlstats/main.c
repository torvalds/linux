/*-
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Simple Marvell-specific tool to inspect and monitor network traffic
 * statistics.
 *
 *	mwlstats [-i interface] [-l] [-o fmtstring] [interval]
 *
 * (default interface is mv0).  If interval is specified a rolling output
 * a la netstat -i is displayed every interval seconds.  The format of
 * the rolling display can be controlled a la ps.  The -l option will
 * print a list of all possible statistics for use with the -o option.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <err.h>

#include "mwlstats.h"

#define	S_DEFAULT \
	"input,output,txtry,txretry,txmretry,txdoneput,rxfcs,rxcrypt,rxicv,rssi,rate"

static int signalled;

static void
catchalarm(int signo __unused)
{
	signalled = 1;
}

int
main(int argc, char *argv[])
{
	struct mwlstatfoo *wf;
	int c;

	wf = mwlstats_new("mwl0", S_DEFAULT);
	while ((c = getopt(argc, argv, "i:lo:")) != -1) {
		switch (c) {
		case 'i':
			wf->setifname(wf, optarg);
			break;
		case 'l':
			wf->print_fields(wf, stdout);
			return 0;
		case 'o':
			wf->setfmt(wf, optarg);
			break;
		default:
			errx(-1, "usage: %s [-a] [-i ifname] [-l] [-o fmt] [interval]\n", argv[0]);
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		u_long interval = strtoul(argv[0], NULL, 0);
		int line, omask;

		if (interval < 1)
			interval = 1;
		signal(SIGALRM, catchalarm);
		signalled = 0;
		alarm(interval);
	banner:
		wf->print_header(wf, stdout);
		line = 0;
	loop:
		if (line != 0) {
			wf->collect_cur(wf);
			wf->print_current(wf, stdout);
			wf->update_tot(wf);
		} else {
			wf->collect_tot(wf);
			wf->print_total(wf, stdout);
		}
		fflush(stdout);
		omask = sigblock(sigmask(SIGALRM));
		if (!signalled)
			sigpause(0);
		sigsetmask(omask);
		signalled = 0;
		alarm(interval);
		line++;
		if (line == 21)		/* XXX tty line count */
			goto banner;
		else
			goto loop;
		/*NOTREACHED*/
	} else {
		wf->collect_tot(wf);
		wf->print_verbose(wf, stdout);
	}
	return 0;
}
