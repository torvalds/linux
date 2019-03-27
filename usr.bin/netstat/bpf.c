/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Christian S.J. Peron
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

#include <sys/types.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/user.h>

#include <net/if.h>
#include <net/bpf.h>
#include <net/bpfdesc.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>

#include "netstat.h"

/* print bpf stats */

static char *
bpf_pidname(pid_t pid)
{
	struct kinfo_proc newkp;
	int error, mib[4];
	size_t size;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = pid;
	size = sizeof(newkp);
	error = sysctl(mib, 4, &newkp, &size, NULL, 0);
	if (error < 0) {
		xo_warn("kern.proc.pid failed");
		return (strdup("??????"));
	}
	return (strdup(newkp.ki_comm));
}

static void
bpf_flags(struct xbpf_d *bd, char *flagbuf)
{

	*flagbuf++ = bd->bd_promisc ? 'p' : '-';
	*flagbuf++ = bd->bd_immediate ? 'i' : '-';
	*flagbuf++ = bd->bd_hdrcmplt ? '-' : 'f';
	*flagbuf++ = (bd->bd_direction == BPF_D_IN) ? '-' :
	    ((bd->bd_direction == BPF_D_OUT) ? 'o' : 's');
	*flagbuf++ = bd->bd_feedback ? 'b' : '-';
	*flagbuf++ = bd->bd_async ? 'a' : '-';
	*flagbuf++ = bd->bd_locked ? 'l' : '-';
	*flagbuf++ = '\0';

	if (bd->bd_promisc)
		xo_emit("{e:promiscuous/}");
	if (bd->bd_immediate)
		xo_emit("{e:immediate/}");
	if (bd->bd_hdrcmplt)
		xo_emit("{e:header-complete/}");
	xo_emit("{e:direction}", (bd->bd_direction == BPF_D_IN) ? "input" :
	    (bd->bd_direction == BPF_D_OUT) ? "output" : "bidirectional");
	if (bd->bd_feedback)
		xo_emit("{e:feedback/}");
	if (bd->bd_async)
		xo_emit("{e:async/}");
	if (bd->bd_locked)
		xo_emit("{e:locked/}");
}

void
bpf_stats(char *ifname)
{
	struct xbpf_d *d, *bd, zerostat;
	char *pname, flagbuf[12];
	size_t size;

	if (zflag) {
		bzero(&zerostat, sizeof(zerostat));
		if (sysctlbyname("net.bpf.stats", NULL, NULL,
		    &zerostat, sizeof(zerostat)) < 0)
			xo_warn("failed to zero bpf counters");
		return;
	}
	if (sysctlbyname("net.bpf.stats", NULL, &size,
	    NULL, 0) < 0) {
		xo_warn("net.bpf.stats");
		return;
	}
	if (size == 0)
		return;
	bd = malloc(size);
	if (bd == NULL) {
		xo_warn("malloc failed");
		return;
	}
	if (sysctlbyname("net.bpf.stats", bd, &size,
	    NULL, 0) < 0) {
		xo_warn("net.bpf.stats");
		free(bd);
		return;
	}
	xo_emit("{T:/%5s} {T:/%6s} {T:/%7s} {T:/%9s} {T:/%9s} {T:/%9s} "
	    "{T:/%5s} {T:/%5s} {T:/%s}\n",
	    "Pid", "Netif", "Flags", "Recv", "Drop", "Match",
	    "Sblen", "Hblen", "Command");
	xo_open_container("bpf-statistics");
	xo_open_list("bpf-entry");
	for (d = &bd[0]; d < &bd[size / sizeof(*d)]; d++) {
		if (d->bd_structsize != sizeof(*d)) {
			xo_warnx("bpf_stats_extended: version mismatch");
			return;
		}
		if (ifname && strcmp(ifname, d->bd_ifname) != 0)
			continue;
		xo_open_instance("bpf-entry");
		pname = bpf_pidname(d->bd_pid);
		xo_emit("{k:pid/%5d} {k:interface-name/%6s} ",
		    d->bd_pid, d->bd_ifname);
		bpf_flags(d, flagbuf);
		xo_emit("{d:flags/%7s} {:received-packets/%9ju} "
		    "{:dropped-packets/%9ju} {:filter-packets/%9ju} "
		    "{:store-buffer-length/%5d} {:hold-buffer-length/%5d} "
		    "{:process/%s}\n",
		    flagbuf, (uintmax_t)d->bd_rcount, (uintmax_t)d->bd_dcount,
		    (uintmax_t)d->bd_fcount, d->bd_slen, d->bd_hlen, pname);
		free(pname);
		xo_close_instance("bpf-entry");
	}
	xo_close_list("bpf-entry");
	xo_close_container("bpf-statistics");
	free(bd);
}
