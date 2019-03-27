/*-
 * Copyright (c) 2017 Dell EMC
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
 */
 #include <sys/cdefs.h>
 __FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <libprocstat.h>

#include "procstat.h"

void
procstat_ptlwpinfo(struct procstat *prstat, struct kinfo_proc *kipp __unused)
{
	struct ptrace_lwpinfo *pl;
	unsigned int count, i;

	pl = procstat_getptlwpinfo(prstat, &count);
	if (pl == NULL)
		return;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit(
	    "{T:/%6s %7s %5s %5s %5s %6s %5s} {[:/%d}{T:/%s}{]:} {T:/%s}\n",
		    "LWPID", "EVENT", "SIGNO", "CODE", "ERRNO", "PID", "UID",
		    2 * sizeof(void *) + 2, "ADDR", "TDNAME");

	xo_open_container("threads");
	for (i = 0; i < count; i++) {
		xo_open_container("thread");
		xo_emit("{:lwpid/%6d} ", pl[i].pl_lwpid);
		switch (pl[i].pl_event) {
		case PL_EVENT_NONE:
			xo_emit("{eq:event/none}{d:event/%7s} ", "none");
			break;
		case PL_EVENT_SIGNAL:
			xo_emit("{eq:event/signal}{d:event/%7s} ", "signal");
			break;
		default:
			xo_emit("{eq:event/unknown}{d:event/%7s} ", "?");
			break;
		}
		if ((pl[i].pl_flags & PL_FLAG_SI) != 0) {
			siginfo_t *si;

			si = &pl[i].pl_siginfo;
			xo_emit("{:signal_number/%5d} ", si->si_signo);
			xo_emit("{:code/%5d} ", si->si_code);
			xo_emit("{:signal_errno/%5d} ", si->si_errno);
			xo_emit("{:process_id/%6d} ", si->si_pid);
			xo_emit("{:user_id/%5d} ", si->si_uid);
			xo_emit("{[:/%d}{:address/%p}{]:} ",
			    2 * sizeof(void *) + 2, si->si_addr);
		} else {
			xo_emit("{:signal_number/%5s} ", "-");
			xo_emit("{:code/%5s} ", "-");
			xo_emit("{:signal_errno/%5s} ", "-");
			xo_emit("{:process_id/%6s} ", "-");
			xo_emit("{:user_id/%5s} ", "-");
			xo_emit("{[:/%d}{:address/%s}{]:} ",
			    2 * sizeof(void *) + 2, "-");
		}
		xo_emit("{:tdname/%s}\n", pl[i].pl_tdname);
		xo_close_container("thread");
	}
	xo_close_container("threads");

	procstat_freeptlwpinfo(prstat, pl);
}
