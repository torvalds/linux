/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Konstantin Belousov
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libprocstat.h>

#include "procstat.h"

static void
procstat_print_signame(int sig)
{
	char name[12];
	int i;

	if ((procstat_opts & PS_OPT_SIGNUM) == 0 && sig < sys_nsig) {
		strlcpy(name, sys_signame[sig], sizeof(name));
		for (i = 0; name[i] != 0; i++)
			name[i] = toupper(name[i]);
		xo_emit("{d:signal/%-7s/%s} ", name);
		xo_open_container(name);
	} else {
		xo_emit("{d:signal/%-7d/%d} ", sig);
		snprintf(name, 12, "%d", sig);
		xo_open_container(name);
	}
}

static void
procstat_close_signame(int sig)
{
	char name[12];
	int i;

	if ((procstat_opts & PS_OPT_SIGNUM) == 0 && sig < sys_nsig) {
		strlcpy(name, sys_signame[sig], sizeof(name));
		for (i = 0; name[i] != 0; i++)
			name[i] = toupper(name[i]);
		xo_close_container(name);
	} else
		snprintf(name, 12, "%d", sig);
		xo_close_container(name);
}

static void
procstat_print_sig(const sigset_t *set, int sig, char flag)
{
	xo_emit("{d:sigmember/%c}", sigismember(set, sig) ? flag : '-');
	switch (flag) {
		case 'B':
			xo_emit("{en:mask/%s}", sigismember(set, sig) ?
			    "true" : "false");
			break;
		case 'C':
			xo_emit("{en:catch/%s}", sigismember(set, sig) ?
			    "true" : "false");
			break;
		case 'P':
			xo_emit("{en:list/%s}", sigismember(set, sig) ?
			    "true" : "false");
			break;
		case 'I':
			xo_emit("{en:ignore/%s}", sigismember(set, sig) ?
			    "true" : "false");
			break;
		default:
			xo_emit("{en:unknown/%s}", sigismember(set, sig) ?
			    "true" : "false");
			break;
	}
}

void
procstat_sigs(struct procstat *prstat __unused, struct kinfo_proc *kipp)
{
	int j;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %-16s %-7s %4s}\n", "PID", "COMM", "SIG",
		    "FLAGS");

	xo_emit("{ek:process_id/%5d/%d}", kipp->ki_pid);
	xo_emit("{e:command/%-16s/%s}", kipp->ki_comm);
	xo_open_container("signals");
	for (j = 1; j <= _SIG_MAXSIG; j++) {
		xo_emit("{dk:process_id/%5d/%d} ", kipp->ki_pid);
		xo_emit("{d:command/%-16s/%s} ", kipp->ki_comm);
		procstat_print_signame(j);
		xo_emit(" ");
		procstat_print_sig(&kipp->ki_siglist, j, 'P');
		procstat_print_sig(&kipp->ki_sigignore, j, 'I');
		procstat_print_sig(&kipp->ki_sigcatch, j, 'C');
		procstat_close_signame(j);
		xo_emit("\n");
	}
	xo_close_container("signals");
}

void
procstat_threads_sigs(struct procstat *procstat, struct kinfo_proc *kipp)
{
	struct kinfo_proc *kip;
	int j;
	unsigned int count, i;
	char *threadid;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %6s %-16s %-7s %4s}\n", "PID", "TID", "COMM",
		     "SIG", "FLAGS");

	kip = procstat_getprocs(procstat, KERN_PROC_PID | KERN_PROC_INC_THREAD,
	    kipp->ki_pid, &count);
	if (kip == NULL)
		return;
	xo_emit("{ek:process_id/%5d/%d}", kipp->ki_pid);
	xo_emit("{e:command/%-16s/%s}", kipp->ki_comm);
	xo_open_container("threads");
	kinfo_proc_sort(kip, count);
	for (i = 0; i < count; i++) {
		kipp = &kip[i];
		asprintf(&threadid, "%d", kipp->ki_tid);
		if (threadid == NULL)
			xo_errc(1, ENOMEM, "Failed to allocate memory in "
			    "procstat_threads_sigs()");
		xo_open_container(threadid);
		xo_emit("{e:thread_id/%6d/%d}", kipp->ki_tid);
		xo_open_container("signals");
		for (j = 1; j <= _SIG_MAXSIG; j++) {
			xo_emit("{dk:process_id/%5d/%d} ", kipp->ki_pid);
			xo_emit("{d:thread_id/%6d/%d} ", kipp->ki_tid);
			xo_emit("{d:command/%-16s/%s} ", kipp->ki_comm);
			procstat_print_signame(j);
			xo_emit(" ");
			procstat_print_sig(&kipp->ki_siglist, j, 'P');
			procstat_print_sig(&kipp->ki_sigmask, j, 'B');
			procstat_close_signame(j);
			xo_emit("\n");
		}
		xo_close_container("signals");
		xo_close_container(threadid);
		free(threadid);
	}
	xo_close_container("threads");
	procstat_freeprocs(procstat, kip);
}
