/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Robert N. M. Watson
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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "procstat.h"

static const char *get_umask(struct procstat *procstat,
    struct kinfo_proc *kipp);

void
procstat_cred(struct procstat *procstat, struct kinfo_proc *kipp)
{
	unsigned int i, ngroups;
	gid_t *groups;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %-16s %5s %5s %5s %5s %5s %5s %5s %5s %-15s}\n",
		    "PID", "COMM", "EUID", "RUID", "SVUID", "EGID", "RGID",
		    "SVGID", "UMASK", "FLAGS", "GROUPS");

	xo_emit("{k:process_id/%5d/%d} ", kipp->ki_pid);
	xo_emit("{:command/%-16s/%s} ", kipp->ki_comm);
	xo_emit("{:uid/%5d} ", kipp->ki_uid);
	xo_emit("{:ruid/%5d} ", kipp->ki_ruid);
	xo_emit("{:svuid/%5d} ", kipp->ki_svuid);
	xo_emit("{:group/%5d} ", kipp->ki_groups[0]);
	xo_emit("{:rgid/%5d} ", kipp->ki_rgid);
	xo_emit("{:svgid/%5d} ", kipp->ki_svgid);
	xo_emit("{:umask/%5s} ", get_umask(procstat, kipp));
	xo_emit("{:cr_flags/%s}", kipp->ki_cr_flags & CRED_FLAG_CAPMODE ?
	    "C" : "-");
	xo_emit("{P:     }");

	groups = NULL;
	/*
	 * We may have too many groups to fit in kinfo_proc's statically
	 * sized storage.  If that occurs, attempt to retrieve them using
	 * libprocstat.
	 */
	if (kipp->ki_cr_flags & KI_CRF_GRP_OVERFLOW)
		groups = procstat_getgroups(procstat, kipp, &ngroups);
	if (groups == NULL) {
		ngroups = kipp->ki_ngroups;
		groups = kipp->ki_groups;
	}
	xo_open_list("groups");
	for (i = 0; i < ngroups; i++)
		xo_emit("{D:/%s}{l:groups/%d}", (i > 0) ? "," : "", groups[i]);
	if (groups != kipp->ki_groups)
		procstat_freegroups(procstat, groups);

	xo_close_list("groups");
	xo_emit("\n");
}

static const char *
get_umask(struct procstat *procstat, struct kinfo_proc *kipp)
{
	u_short fd_cmask;
	static char umask[4];

	if (procstat_getumask(procstat, kipp, &fd_cmask) == 0) {
		snprintf(umask, 4, "%03o", fd_cmask);
		return (umask);
	} else {
		return ("-");
	}
}
