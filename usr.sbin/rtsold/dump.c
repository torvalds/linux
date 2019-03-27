/*	$KAME: dump.c,v 1.13 2003/10/05 00:09:36 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>

#include <capsicum_helpers.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "rtsold.h"

static const char * const ifstatstr[] =
    { "IDLE", "DELAY", "PROBE", "DOWN", "TENTATIVE" };

void
rtsold_dump(FILE *fp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct ra_opt *rao;
	struct timespec now;
	char ntopbuf[INET6_ADDRSTRLEN];

	if (fseek(fp, 0, SEEK_SET) != 0) {
		warnmsg(LOG_ERR, __func__, "fseek(): %s", strerror(errno));
		return;
	}
	(void)ftruncate(fileno(fp), 0);

	(void)clock_gettime(CLOCK_MONOTONIC_FAST, &now);

	TAILQ_FOREACH(ifi, &ifinfo_head, ifi_next) {
		fprintf(fp, "Interface %s\n", ifi->ifname);
		fprintf(fp, "  probe interval: ");
		if (ifi->probeinterval) {
			fprintf(fp, "%d\n", ifi->probeinterval);
			fprintf(fp, "  probe timer: %d\n", ifi->probetimer);
		} else {
			fprintf(fp, "infinity\n");
			fprintf(fp, "  no probe timer\n");
		}
		fprintf(fp, "  interface status: %s\n",
		    ifi->active > 0 ? "active" : "inactive");
		fprintf(fp, "  other config: %s\n",
		    ifi->otherconfig ? "on" : "off");
		fprintf(fp, "  rtsold status: %s\n", ifstatstr[ifi->state]);
		fprintf(fp, "  carrier detection: %s\n",
		    ifi->mediareqok ? "available" : "unavailable");
		fprintf(fp, "  probes: %d, dadcount = %d\n",
		    ifi->probes, ifi->dadcount);
		if (ifi->timer.tv_sec == tm_max.tv_sec &&
		    ifi->timer.tv_nsec == tm_max.tv_nsec)
			fprintf(fp, "  no timer\n");
		else {
			fprintf(fp, "  timer: interval=%d:%d, expire=%s\n",
			    (int)ifi->timer.tv_sec,
			    (int)ifi->timer.tv_nsec / 1000,
			    (ifi->expire.tv_sec < now.tv_sec) ? "expired"
			    : sec2str(&ifi->expire));
		}
		fprintf(fp, "  number of valid RAs: %d\n", ifi->racnt);

		TAILQ_FOREACH(rai, &ifi->ifi_rainfo, rai_next) {
			fprintf(fp, "   RA from %s\n",
			    inet_ntop(AF_INET6, &rai->rai_saddr.sin6_addr,
				ntopbuf, sizeof(ntopbuf)));
			TAILQ_FOREACH(rao, &rai->rai_ra_opt, rao_next) {
				fprintf(fp, "    option: ");
				switch (rao->rao_type) {
				case ND_OPT_RDNSS:
					fprintf(fp, "RDNSS: %s (expire: %s)\n",
					    (char *)rao->rao_msg,
					    sec2str(&rao->rao_expire));
					break;
				case ND_OPT_DNSSL:
					fprintf(fp, "DNSSL: %s (expire: %s)\n",
					    (char *)rao->rao_msg,
					    sec2str(&rao->rao_expire));
					break;
				default:
					break;
				}
			}
			fprintf(fp, "\n");
		}
	}
	fflush(fp);
}

FILE *
rtsold_init_dumpfile(const char *dumpfile)
{
	cap_rights_t rights;
	FILE *fp;

	if ((fp = fopen(dumpfile, "w")) == NULL) {
		warnmsg(LOG_WARNING, __func__, "opening a dump file(%s): %s",
		    dumpfile, strerror(errno));
		return (NULL);
	}

	cap_rights_init(&rights, CAP_FSTAT, CAP_FTRUNCATE, CAP_SEEK, CAP_WRITE);
	if (caph_rights_limit(fileno(fp), &rights) != 0) {
		warnmsg(LOG_WARNING, __func__, "caph_rights_limit(%s): %s",
		    dumpfile, strerror(errno));
		return (NULL);
	}
	return (fp);
}

const char *
sec2str(const struct timespec *total)
{
	static char result[256];
	int days, hours, mins, secs;
	int first = 1;
	char *p = result;
	char *ep = &result[sizeof(result)];
	int n;
	struct timespec now;
	time_t tsec;

	clock_gettime(CLOCK_MONOTONIC_FAST, &now);
	tsec  = total->tv_sec;
	tsec += total->tv_nsec / 1000 / 1000000;
	tsec -= now.tv_sec;
	tsec -= now.tv_nsec / 1000 / 1000000;

	days = tsec / 3600 / 24;
	hours = (tsec / 3600) % 24;
	mins = (tsec / 60) % 60;
	secs = tsec % 60;

	if (days) {
		first = 0;
		n = snprintf(p, ep - p, "%dd", days);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	if (!first || hours) {
		first = 0;
		n = snprintf(p, ep - p, "%dh", hours);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	if (!first || mins) {
		first = 0;
		n = snprintf(p, ep - p, "%dm", mins);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	snprintf(p, ep - p, "%ds", secs);

	return (result);
}
