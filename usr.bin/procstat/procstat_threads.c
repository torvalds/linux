/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

void
procstat_threads(struct procstat *procstat, struct kinfo_proc *kipp)
{
	struct kinfo_proc *kip;
	unsigned int count, i;
	const char *str;
	char *threadid;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %6s %-19s %-19s %2s %4s %-7s %-9s}\n", "PID",
		    "TID", "COMM", "TDNAME", "CPU", "PRI", "STATE", "WCHAN");

	xo_emit("{ek:process_id/%d}", kipp->ki_pid);
	xo_emit("{e:command/%s}", strlen(kipp->ki_comm) ?
		    kipp->ki_comm : "-");
	xo_open_container("threads");

	kip = procstat_getprocs(procstat, KERN_PROC_PID | KERN_PROC_INC_THREAD,
	    kipp->ki_pid, &count);
	if (kip == NULL)
		return;
	kinfo_proc_sort(kip, count);
	for (i = 0; i < count; i++) {
		kipp = &kip[i];
		asprintf(&threadid, "%d", kipp->ki_tid);
		if (threadid == NULL)
			xo_errc(1, ENOMEM, "Failed to allocate memory in "
			    "procstat_threads()");
		xo_open_container(threadid);
		xo_emit("{dk:process_id/%5d/%d} ", kipp->ki_pid);
		xo_emit("{:thread_id/%6d/%d} ", kipp->ki_tid);
		xo_emit("{d:command/%-19s/%s} ", strlen(kipp->ki_comm) ?
		    kipp->ki_comm : "-");
		xo_emit("{:thread_name/%-19s/%s} ",
		    kinfo_proc_thread_name(kipp));
		if (kipp->ki_oncpu != 255)
			xo_emit("{:cpu/%3d/%d} ", kipp->ki_oncpu);
		else if (kipp->ki_lastcpu != 255)
			xo_emit("{:cpu/%3d/%d} ", kipp->ki_lastcpu);
		else
			xo_emit("{:cpu/%3s/%s} ", "-");
		xo_emit("{:priority/%4d/%d} ", kipp->ki_pri.pri_level);
		switch (kipp->ki_stat) {
		case SRUN:
			str = "run";
			break;

		case SSTOP:
			str = "stop";
			break;

		case SSLEEP:
			str = "sleep";
			break;

		case SLOCK:
			str = "lock";
			break;

		case SWAIT:
			str = "wait";
			break;

		case SZOMB:
			str = "zomb";
			break;

		case SIDL:
			str = "idle";
			break;

		default:
			str = "??";
			break;
		}
		xo_emit("{:run_state/%-7s/%s} ", str);
		if (kipp->ki_kiflag & KI_LOCKBLOCK) {
			xo_emit("{:lock_name/*%-8s/%s} ",
			    strlen(kipp->ki_lockname) ?
			    kipp->ki_lockname : "-");
		} else {
			xo_emit("{:wait_channel/%-9s/%s} ",
			    strlen(kipp->ki_wmesg) ? kipp->ki_wmesg : "-");
		}
		xo_close_container(threadid);
		free(threadid);
		xo_emit("\n");
	}
	xo_close_container("threads");
	procstat_freeprocs(procstat, kip);
}
