/*-
 * Copyright (c) 2012 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <libprocstat.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libutil.h>

#include "procstat.h"

static struct {
	const char *ri_name;
	bool	ri_humanize;
	int	ri_scale;
} rusage_info[] = {
	{ "maximum RSS", true, 1 },
	{ "integral shared memory", true, 1 },
	{ "integral unshared data", true, 1 },
	{ "integral unshared stack", true, 1 },
	{ "page reclaims", false, 0 },
	{ "page faults", false, 0 },
	{ "swaps", false, 0 },
	{ "block reads", false, 0 },
	{ "block writes", false, 0 },
	{ "messages sent", false, 0 },
	{ "messages received", false, 0 },
	{ "signals received", false, 0 },
	{ "voluntary context switches", false, 0 },
	{ "involuntary context switches", false, 0 }
};

/* xxx days hh:mm:ss.uuuuuu */
static const char *
format_time(struct timeval *tv)
{
	static char buffer[32];
	int days, hours, minutes, seconds, used;

	minutes = tv->tv_sec / 60;
	seconds = tv->tv_sec % 60;
	hours = minutes / 60;
	minutes %= 60;
	days = hours / 24;
	hours %= 24;
	used = 0;
	if (days == 1)
		used += snprintf(buffer, sizeof(buffer), "1 day ");
	else if (days > 0)
		used += snprintf(buffer, sizeof(buffer), "%u days ", days);
	
	snprintf(buffer + used, sizeof(buffer) - used, "%02u:%02u:%02u.%06u",
	    hours, minutes, seconds, (unsigned int)tv->tv_usec);
	return (buffer);
}

static const char *
format_value(long value, bool humanize, int scale)
{
	static char buffer[14];

	if (scale != 0)
		value <<= scale * 10;
	if (humanize)
		humanize_number(buffer, sizeof(buffer), value, "B",
		    scale, HN_DECIMAL);
	else
		snprintf(buffer, sizeof(buffer), "%ld   ", value);
	return (buffer);
}

static void
print_prefix(struct kinfo_proc *kipp)
{

	xo_emit("{d:process_id/%5d/%d} ", kipp->ki_pid);
	if ((procstat_opts & PS_OPT_PERTHREAD) != 0)
		xo_emit("{d:thread_id/%6d/%d} ", kipp->ki_tid);
	xo_emit("{d:command/%-16s/%s} ", kipp->ki_comm);
}

static void
print_rusage(struct kinfo_proc *kipp)
{
	long *lp;
	unsigned int i;
	char *field, *threadid;

	print_prefix(kipp);
	xo_emit("{d:resource/%-14s} {d:usage/%29s}{P:   }\n", "user time",
	    format_time(&kipp->ki_rusage.ru_utime));
	print_prefix(kipp);
	xo_emit("{d:resource/%-14s} {d:usage/%29s}{P:   }\n", "system time",
	    format_time(&kipp->ki_rusage.ru_stime));

	if ((procstat_opts & PS_OPT_PERTHREAD) != 0) {
		asprintf(&threadid, "%d", kipp->ki_tid);
		if (threadid == NULL)
			xo_errc(1, ENOMEM,
			    "Failed to allocate memory in print_rusage()");
		xo_open_container(threadid);
		xo_emit("{e:thread_id/%d}", kipp->ki_tid);
	} else {
		xo_emit("{e:process_id/%d}", kipp->ki_pid);
		xo_emit("{e:command/%s}", kipp->ki_comm);
	}
	xo_emit("{e:user time/%s}", format_time(&kipp->ki_rusage.ru_utime));
	xo_emit("{e:system time/%s}", format_time(&kipp->ki_rusage.ru_stime));

	lp = &kipp->ki_rusage.ru_maxrss;
	for (i = 0; i < nitems(rusage_info); i++) {
		print_prefix(kipp);
		asprintf(&field, "{e:%s/%%D}", rusage_info[i].ri_name);
		if (field == NULL)
			xo_errc(1, ENOMEM,
			    "Failed to allocate memory in print_rusage()");
		xo_emit(field, *lp);
		free(field);
		xo_emit("{d:resource/%-32s} {d:usage/%14s}\n",
		    rusage_info[i].ri_name,
		    format_value(*lp, rusage_info[i].ri_humanize,
		    rusage_info[i].ri_scale));
		lp++;
	}
	if ((procstat_opts & PS_OPT_PERTHREAD) != 0) {
		xo_close_container(threadid);
		free(threadid);
	}
}

void
procstat_rusage(struct procstat *procstat, struct kinfo_proc *kipp)
{
	struct kinfo_proc *kip;
	unsigned int count, i;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0) {
		xo_emit("{d:ta/%5s} ", "PID");
		if ((procstat_opts & PS_OPT_PERTHREAD) != 0)
			xo_emit("{d:tb/%6s} ", "TID");
		xo_emit("{d:tc/%-16s %-32s %14s}\n", "COMM", "RESOURCE",
		    "VALUE        ");
	}

	if ((procstat_opts & PS_OPT_PERTHREAD) == 0) {
		print_rusage(kipp);
		return;
	}

	xo_emit("{e:process_id/%d}", kipp->ki_pid);
	xo_emit("{e:command/%s}", kipp->ki_comm);
	xo_open_container("threads");

	kip = procstat_getprocs(procstat, KERN_PROC_PID | KERN_PROC_INC_THREAD,
	    kipp->ki_pid, &count);
	if (kip == NULL)
		return;
	kinfo_proc_sort(kip, count);
	for (i = 0; i < count; i++) {
		print_rusage(&kip[i]);
	}

	xo_close_container("threads");
	procstat_freeprocs(procstat, kip);
}
