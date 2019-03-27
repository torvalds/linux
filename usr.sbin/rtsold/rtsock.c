/*	$KAME: rtsock.c,v 1.3 2000/10/10 08:46:45 itojun Exp $	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2000 WIDE Project.
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
 */

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include <capsicum_helpers.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include "rtsold.h"

static int rtsock_input_ifannounce(int, struct rt_msghdr *, char *);

static struct {
	u_char type;
	size_t minlen;
	int (*func)(int, struct rt_msghdr *, char *);
} rtsock_dispatch[] = {
	{ RTM_IFANNOUNCE, sizeof(struct if_announcemsghdr),
	  rtsock_input_ifannounce },
	{ 0, 0, NULL },
};

int
rtsock_open(void)
{
	cap_rights_t rights;
	int error, s;

	s = socket(PF_ROUTE, SOCK_RAW, 0);
	if (s < 0)
		return (s);
	cap_rights_init(&rights, CAP_EVENT, CAP_READ);
	if (caph_rights_limit(s, &rights) != 0) {
		error = errno;
		(void)close(s);
		errno = errno;
		return (-1);
	}
	return (s);
}

int
rtsock_input(int s)
{
	ssize_t n;
	char msg[2048];
	char *lim, *next;
	struct rt_msghdr *rtm;
	int idx;
	ssize_t len;
	int ret = 0;
	const ssize_t lenlim =
	    offsetof(struct rt_msghdr, rtm_msglen) + sizeof(rtm->rtm_msglen);

	n = read(s, msg, sizeof(msg));

	lim = msg + n;
	for (next = msg; next < lim; next += len) {
		rtm = (struct rt_msghdr *)(void *)next;
		if (lim - next < lenlim)
			break;
		len = rtm->rtm_msglen;
		if (len < lenlim)
			break;

		if (dflag > 1) {
			warnmsg(LOG_INFO, __func__,
			    "rtmsg type %d, len=%lu", rtm->rtm_type,
			    (u_long)len);
		}

		for (idx = 0; rtsock_dispatch[idx].func; idx++) {
			if (rtm->rtm_type != rtsock_dispatch[idx].type)
				continue;
			if (rtm->rtm_msglen < rtsock_dispatch[idx].minlen) {
				warnmsg(LOG_INFO, __func__,
				    "rtmsg type %d too short!", rtm->rtm_type);
				continue;
			}

			ret = (*rtsock_dispatch[idx].func)(s, rtm, lim);
			break;
		}
	}

	return (ret);
}

static int
rtsock_input_ifannounce(int s __unused, struct rt_msghdr *rtm, char *lim)
{
	struct if_announcemsghdr *ifan;
	struct ifinfo *ifi;

	ifan = (struct if_announcemsghdr *)rtm;
	if ((char *)(ifan + 1) > lim)
		return (-1);

	switch (ifan->ifan_what) {
	case IFAN_ARRIVAL:
		/*
		 * XXX for NetBSD 1.5, interface index will monotonically be
		 * increased as new pcmcia card gets inserted.
		 * we may be able to do a name-based interface match,
		 * and call ifreconfig() to enable the interface again.
		 */
		warnmsg(LOG_INFO, __func__,
		    "interface %s inserted", ifan->ifan_name);
		break;
	case IFAN_DEPARTURE:
		warnmsg(LOG_WARNING, __func__,
		    "interface %s removed", ifan->ifan_name);
		ifi = find_ifinfo(ifan->ifan_index);
		if (ifi) {
			if (dflag > 1) {
				warnmsg(LOG_INFO, __func__,
				    "bring interface %s to DOWN state",
				    ifan->ifan_name);
			}
			ifi->state = IFS_DOWN;
		}
		break;
	}

	return (0);
}
