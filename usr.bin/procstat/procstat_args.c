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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

void
procstat_args(struct procstat *procstat, struct kinfo_proc *kipp)
{
	int i;
	char **args;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0) {
		xo_emit("{T:/%5s %-16s %-53s}\n", "PID", "COMM", "ARGS");
	}

	args = procstat_getargv(procstat, kipp, 0);

	xo_emit("{k:process_id/%5d/%d} {:command/%-16s/%s}", kipp->ki_pid,
	    kipp->ki_comm);

	if (args == NULL) {
		xo_emit(" {d:args/-}\n");
		return;
	}

	xo_open_list("arguments");
	for (i = 0; args[i] != NULL; i++)
		xo_emit(" {l:args/%s}", args[i]);
	xo_close_list("arguments");
	xo_emit("\n");
}

void
procstat_env(struct procstat *procstat, struct kinfo_proc *kipp)
{
	int i;
	char **envs;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0) {
		xo_emit("{T:/%5s %-16s %-53s}\n", "PID", "COMM", "ENVIRONMENT");
	}

	envs = procstat_getenvv(procstat, kipp, 0);

	xo_emit("{k:process_id/%5d/%d} {:command/%-16s/%s}", kipp->ki_pid,
	    kipp->ki_comm);

	if (envs == NULL) {
		xo_emit(" {d:env/-}\n");
		return;
	}

	xo_open_list("environment");
	for (i = 0; envs[i] != NULL; i++)
		xo_emit(" {l:env/%s}", envs[i]);
	xo_close_list("environment");
	xo_emit("\n");
}
