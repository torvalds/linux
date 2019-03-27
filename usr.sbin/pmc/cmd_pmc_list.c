/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018, Matthew Macy
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <kvm.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pmc.h>
#include <pmclog.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libpmcstat.h>
#include "cmd_pmc.h"


static struct option longopts[] = {
	{"long-desc", no_argument, NULL, 'U'},
	{"desc", no_argument, NULL, 'u'},
	{"full", no_argument, NULL, 'f'},
	{NULL, 0, NULL, 0}
};

static void __dead2
usage(void)
{
	errx(EX_USAGE,
	    "\t list events\n"
	    "\t -u, desc -- short description of event\n"
	    "\t -U, long-desc -- long description of event\n"
	    "\t -f, full -- full event details\n"
	    );
}

int
cmd_pmc_list_events(int argc, char **argv)
{
	int do_long_descr, do_descr, do_full;
	int option;

	do_long_descr = do_descr = do_full = 0;
	while ((option = getopt_long(argc, argv, "Uuf", longopts, NULL)) != -1) {
		switch (option) {
		case 'U':
			do_long_descr = 1;
			break;
		case 'u':
			do_descr = 1;
			break;
		case 'f':
			do_full = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if ((do_long_descr | do_descr | do_full) && argc == 0) {
		warnx("event or event substring required when option provided\n");
		usage();
	}
	if (do_full)
		pmc_pmu_print_counter_full(argc ? argv[0] : NULL);
	else if (do_long_descr)
		pmc_pmu_print_counter_desc_long(argv[0]);
	else if (do_descr)
		pmc_pmu_print_counter_desc(argv[0]);
	else
		pmc_pmu_print_counters(argv[0]);

	return (0);
}
