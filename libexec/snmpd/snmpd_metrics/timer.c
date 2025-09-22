/*	$OpenBSD: timer.c,v 1.1.1.1 2022/09/01 14:20:33 martijn Exp $	*/

/*
 * Copyright (c) 2008 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sched.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include "snmpd.h"
#include "mib.h"

void	 timer_cpu(int, short, void *);
int	 percentages(int, int64_t *, int64_t *, int64_t *, int64_t *);

static int64_t	**cp_time;
static int64_t	**cp_old;
static int64_t	**cp_diff;
struct event	  cpu_ev;

void
timer_cpu(int fd, short event, void *arg)
{
	struct event	*ev = (struct event *)arg;
	struct timeval	 tv = { 60, 0 };	/* every 60 seconds */
	int		 mib[3] = { CTL_KERN, KERN_CPTIME2, 0 }, n;
	size_t		 len;
	int64_t		*cptime2;

	len = CPUSTATES * sizeof(int64_t);
	for (n = 0; n < snmpd_env->sc_ncpu; n++) {
		mib[2] = n;
		cptime2 = snmpd_env->sc_cpustates + (CPUSTATES * n);
		if (sysctl(mib, 3, cp_time[n], &len, NULL, 0) == -1)
			continue;
		(void)percentages(CPUSTATES, cptime2, cp_time[n],
		    cp_old[n], cp_diff[n]);
#ifdef DEBUG
		log_debug("timer_cpu: cpu%d %lld%% idle in %llds", n,
		    (cptime2[CP_IDLE] > 1000 ?
		    1000 : (cptime2[CP_IDLE] / 10)), (long long) tv.tv_sec);
#endif
	}

	evtimer_add(ev, &tv);
}

void
timer_init(void)
{
	int	 mib[] = { CTL_HW, HW_NCPU }, i;
	size_t	 len;

	len = sizeof(snmpd_env->sc_ncpu);
	if (sysctl(mib, 2, &snmpd_env->sc_ncpu, &len, NULL, 0) == -1)
		fatal("sysctl");

	snmpd_env->sc_cpustates = calloc(snmpd_env->sc_ncpu,
	    CPUSTATES * sizeof(int64_t));
	cp_time = calloc(snmpd_env->sc_ncpu, sizeof(int64_t *));
	cp_old = calloc(snmpd_env->sc_ncpu, sizeof(int64_t *));
	cp_diff = calloc(snmpd_env->sc_ncpu, sizeof(int64_t *));
	if (snmpd_env->sc_cpustates == NULL ||
	    cp_time == NULL || cp_old == NULL || cp_diff == NULL)
		fatal("calloc");
	for (i = 0; i < snmpd_env->sc_ncpu; i++) {
		cp_time[i] = calloc(CPUSTATES, sizeof(int64_t));
		cp_old[i] = calloc(CPUSTATES, sizeof(int64_t));
		cp_diff[i] = calloc(CPUSTATES, sizeof(int64_t));
		if (cp_time[i] == NULL || cp_old[i] == NULL ||
		    cp_diff[i] == NULL)
			fatal("calloc");
	}

	evtimer_set(&cpu_ev, timer_cpu, &cpu_ev);
	timer_cpu(0, EV_TIMEOUT, &cpu_ev);
}

/*
 * percentages() function to calculate CPU utilization.
 * Source code derived from the top(1) utility:
 *
 * Copyright (c) 1984, 1989, William LeFebvre, Rice University
 * Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS EMPLOYER BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
int
percentages(int cnt, int64_t *out, int64_t *new, int64_t *old, int64_t *diffs)
{
	int64_t change, total_change, *dp, half_total;
	int i;

	/* initialization */
	total_change = 0;
	dp = diffs;

	/* calculate changes for each state and the overall change */
	for (i = 0; i < cnt; i++) {
		if ((change = *new - *old) < 0) {
			/* this only happens when the counter wraps */
			change = (*new - *old);
		}
		total_change += (*dp++ = change);
		*old++ = *new++;
	}

	/* avoid divide by zero potential */
	if (total_change == 0)
		total_change = 1;

	/* calculate percentages based on overall change, rounding up */
	half_total = total_change / 2l;
	for (i = 0; i < cnt; i++)
		*out++ = ((*diffs++ * 1000 + half_total) / total_change);

	/* return the total in case the caller wants to use it */
	return (total_change);
}
