/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Mikolaj Golub
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
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
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <libutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

static struct {
	const char *name;
	const char *suffix;
} rlimit_param[15] = {
	{"cputime",          "sec"},
	{"filesize",         "B  "},
	{"datasize",         "B  "},
	{"stacksize",        "B  "},
	{"coredumpsize",     "B  "},
	{"memoryuse",        "B  "},
	{"memorylocked",     "B  "},
	{"maxprocesses",     "   "},
	{"openfiles",        "   "},
	{"sbsize",           "B  "},
	{"vmemoryuse",       "B  "},
	{"pseudo-terminals", "   "},
	{"swapuse",          "B  "},
	{"kqueues",          "   "},
	{"umtxp",            "   "},
};

#if RLIM_NLIMITS > 15
#error "Resource limits have grown. Add new entries to rlimit_param[]."
#endif

static const char *
humanize_rlimit(int indx, rlim_t limit)
{
	static char buf[14];
	int scale;

	if (limit == RLIM_INFINITY)
		return ("infinity     ");

	scale = humanize_number(buf, sizeof(buf) - 1, (int64_t)limit,
	    rlimit_param[indx].suffix, HN_AUTOSCALE | HN_GETSCALE, HN_DECIMAL);
	(void)humanize_number(buf, sizeof(buf) - 1, (int64_t)limit,
	    rlimit_param[indx].suffix, HN_AUTOSCALE, HN_DECIMAL);
	/* Pad with one space if there is no suffix prefix. */
	if (scale == 0)
		sprintf(buf + strlen(buf), " ");
	return (buf);
}

void
procstat_rlimit(struct procstat *prstat, struct kinfo_proc *kipp)
{
	struct rlimit rlimit;
	int i;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0) {
		xo_emit("{T:/%5s %-16s %-16s %16s %16s}\n",
		    "PID", "COMM", "RLIMIT", "SOFT     ", "HARD     ");
	}
	xo_emit("{ek:process_id/%5d}{e:command/%-16s/%s}", kipp->ki_pid,
	    kipp->ki_comm);
	for (i = 0; i < RLIM_NLIMITS; i++) {
		if (procstat_getrlimit(prstat, kipp, i, &rlimit) == -1)
			return;
		xo_emit("{dk:process_id/%5d} {d:command/%-16s} "
		    "{d:rlimit_param/%-16s} ", kipp->ki_pid, kipp->ki_comm,
		    rlimit_param[i].name);

		xo_open_container(rlimit_param[i].name);
		if (rlimit.rlim_cur == RLIM_INFINITY)
			xo_emit("{e:soft_limit/infinity}");
		else
			xo_emit("{e:soft_limit/%U}", rlimit.rlim_cur);

		if (rlimit.rlim_max == RLIM_INFINITY)
			xo_emit("{e:hard_limit/infinity}");
		else
			xo_emit("{e:hard_limit/%U}", rlimit.rlim_max);
		xo_close_container(rlimit_param[i].name);

		xo_emit("{d:rlim_cur/%16s} ",
		    humanize_rlimit(i, rlimit.rlim_cur));
		xo_emit("{d:rlim_max/%16s}\n",
		    humanize_rlimit(i, rlimit.rlim_max));
	}
}
