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
#include <libprocstat.h>
#include <stdio.h>
#include <string.h>

#include "procstat.h"

void
procstat_basic(struct procstat *procstat __unused, struct kinfo_proc *kipp)
{

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %5s %5s %5s %5s %3s %-8s %-9s %-13s %-12s}\n",
		    "PID", "PPID", "PGID", "SID", "TSID", "THR", "LOGIN",
		    "WCHAN", "EMUL", "COMM");

	xo_emit("{k:process_id/%5d/%d} ", kipp->ki_pid);
	xo_emit("{:parent_process_id/%5d/%d} ", kipp->ki_ppid);
	xo_emit("{:process_group_id/%5d/%d} ", kipp->ki_pgid);
	xo_emit("{:session_id/%5d/%d} ", kipp->ki_sid);
	xo_emit("{:terminal_session_id/%5d/%d} ", kipp->ki_tsid);
	xo_emit("{:threads/%3d/%d} ", kipp->ki_numthreads);
	xo_emit("{:login/%-8s/%s} ", strlen(kipp->ki_login) ?
	    kipp->ki_login : "-");
	if (kipp->ki_kiflag & KI_LOCKBLOCK) {
		xo_emit("{:lockname/*%-8s/%s} ", strlen(kipp->ki_lockname) ?
		    kipp->ki_lockname : "-");
	} else {
		xo_emit("{:wait_channel/%-9s/%s} ", strlen(kipp->ki_wmesg) ?
		    kipp->ki_wmesg : "-");
	}
	xo_emit("{:emulation/%-13s/%s} ", strcmp(kipp->ki_emul, "null") ?
	    kipp->ki_emul : "-");
	xo_emit("{:command/%-12s/%s}\n", kipp->ki_comm);
}
