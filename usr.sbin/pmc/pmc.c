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
 * $FreeBSD$
 *
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <stddef.h>
#include <stdlib.h>
#include <err.h>
#include <limits.h>
#include <string.h>
#include <pmc.h>
#include <pmclog.h>
#include <libpmcstat.h>
#include <sysexits.h>
#include <unistd.h>

#include <libpmcstat.h>
#include "cmd_pmc.h"

int	pmc_displayheight = DEFAULT_DISPLAY_HEIGHT;
int	pmc_displaywidth = DEFAULT_DISPLAY_WIDTH;
int	pmc_kq;
struct pmcstat_args pmc_args;

struct pmcstat_pmcs pmcstat_pmcs = LIST_HEAD_INITIALIZER(pmcstat_pmcs);

struct pmcstat_image_hash_list pmcstat_image_hash[PMCSTAT_NHASH];

struct pmcstat_process_hash_list pmcstat_process_hash[PMCSTAT_NHASH];

struct cmd_handler {
	const char *ch_name;
	cmd_disp_t ch_fn;
};

static struct cmd_handler disp_table[] = {
	{"stat", cmd_pmc_stat},
	{"stat-system", cmd_pmc_stat_system},
	{"list-events", cmd_pmc_list_events},
	{"filter", cmd_pmc_filter},
	{"summary", cmd_pmc_summary},
	{NULL, NULL}
};

static void __dead2
usage(void)
{
	errx(EX_USAGE,
	    "\t pmc management utility\n"
	    "\t stat <program> run program and print stats\n"
		 "\t stat-system <program> run program and print system wide stats for duration of execution\n"
		 "\t list-events list PMC events available on host\n"
		 "\t filter filter records by lwp, pid, or event\n"
	    );
}

static cmd_disp_t
disp_lookup(char *name)
{
	struct cmd_handler *hnd;

	for (hnd = disp_table; hnd->ch_name != NULL; hnd++)
		if (strcmp(hnd->ch_name, name) == 0)
			return (hnd->ch_fn);
	return (NULL);
}

int
main(int argc, char **argv)
{
	cmd_disp_t disp;

	pmc_args.pa_printfile = stderr;
	STAILQ_INIT(&pmc_args.pa_events);
	SLIST_INIT(&pmc_args.pa_targets);
	if (argc == 1)
		usage();
	if ((disp = disp_lookup(argv[1])) == NULL)
		usage();
	argc--;
	argv++;

	/* Allocate a kqueue */
	if ((pmc_kq = kqueue()) < 0)
		err(EX_OSERR, "ERROR: Cannot allocate kqueue");
	if (pmc_init() < 0)
		err(EX_UNAVAILABLE,
		    "ERROR: Initialization of the pmc(3) library failed"
		    );
	return (disp(argc, argv));
}
