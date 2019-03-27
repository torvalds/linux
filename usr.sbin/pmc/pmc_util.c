/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 * Copyright (c) 2018, Matthew Macy
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

static struct pmcstat_stats pmcstat_stats;	/* statistics */

static struct pmc_plugins plugins[] = {
	{
		.pl_name = "none",
	},
	{
		.pl_name = NULL
	}
};

int
pmc_util_get_pid(struct pmcstat_args *args)
{
	struct pmcstat_target *pt;

	assert(args->pa_flags & FLAG_HAS_COMMANDLINE);

	/*
	 * If a command line was specified, it would be the very first
	 * in the list, before any other processes specified by -t.
	 */
	pt = SLIST_FIRST(&args->pa_targets);
	return (pt->pt_pid);
}

void
pmc_util_kill_process(struct pmcstat_args *args)
{
	struct pmcstat_target *pt;

	assert(args->pa_flags & FLAG_HAS_COMMANDLINE);

	/*
	 * If a command line was specified, it would be the very first
	 * in the list, before any other processes specified by -t.
	 */
	pt = SLIST_FIRST(&args->pa_targets);
	assert(pt != NULL);

	if (kill(pt->pt_pid, SIGINT) != 0)
		err(EX_OSERR, "ERROR: cannot signal child process");
}

void
pmc_util_shutdown_logging(struct pmcstat_args *args)
{
	pmcstat_shutdown_logging(args, plugins, &pmcstat_stats);
}

void
pmc_util_cleanup(struct pmcstat_args *args)
{
	struct pmcstat_ev *ev;

	/* release allocated PMCs. */
	STAILQ_FOREACH(ev, &args->pa_events, ev_next)
	    if (ev->ev_pmcid != PMC_ID_INVALID) {
		if (pmc_stop(ev->ev_pmcid) < 0)
			err(EX_OSERR,
			    "ERROR: cannot stop pmc 0x%x \"%s\"",
			    ev->ev_pmcid, ev->ev_name);
		if (pmc_release(ev->ev_pmcid) < 0)
			err(EX_OSERR,
			    "ERROR: cannot release pmc 0x%x \"%s\"",
			    ev->ev_pmcid, ev->ev_name);
	}
	/* de-configure the log file if present. */
	if (args->pa_flags & (FLAG_HAS_PIPE | FLAG_HAS_OUTPUT_LOGFILE))
		(void)pmc_configure_logfile(-1);

	if (args->pa_logparser) {
		pmclog_close(args->pa_logparser);
		args->pa_logparser = NULL;
	}
	pmc_util_shutdown_logging(args);
}

void
pmc_util_start_pmcs(struct pmcstat_args *args)
{
	struct pmcstat_ev *ev;

	STAILQ_FOREACH(ev, &args->pa_events, ev_next) {

		assert(ev->ev_pmcid != PMC_ID_INVALID);

		if (pmc_start(ev->ev_pmcid) < 0) {
			warn("ERROR: Cannot start pmc 0x%x \"%s\"",
			    ev->ev_pmcid, ev->ev_name);
			pmc_util_cleanup(args);
			exit(EX_OSERR);
		}
	}
}
