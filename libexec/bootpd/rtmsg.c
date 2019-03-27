/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1984, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1994
 *	Geoffrey M. Rehmet, All rights reserved.
 *
 * This code is derived from software which forms part of the 4.4-Lite
 * Berkeley software distribution, which was in derived from software
 * contributed to Berkeley by Sun Microsystems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * from arp.c	8.2 (Berkeley) 1/2/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
/*
 * Verify that we are at least 4.4 BSD
 */
#if defined(BSD)
#if BSD >= 199306

#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "report.h"


static int rtmsg(int);

static int s = -1; 	/* routing socket */


/*
 * Open the routing socket
 */
static void getsocket () {
	if (s < 0) {
		s = socket(PF_ROUTE, SOCK_RAW, 0);
		if (s < 0) {
			report(LOG_ERR, "socket %s", strerror(errno));
			exit(1);
		}
	} else {
		/*
		 * Drain the socket of any unwanted routing messages.
		 */
		int n;
		char buf[512];

		ioctl(s, FIONREAD, &n);
		while (n > 0) {
			read(s, buf, sizeof buf);
			ioctl(s, FIONREAD, &n);
		}
	}
}

static struct	sockaddr_in so_mask = {8, 0, 0, { 0xffffffff}};
static struct	sockaddr_in blank_sin = {sizeof(blank_sin), AF_INET }, sin_m;
static struct	sockaddr_dl blank_sdl = {sizeof(blank_sdl), AF_LINK }, sdl_m;
static int	expire_time, flags, doing_proxy;
static struct	{
	struct	rt_msghdr m_rtm;
	char	m_space[512];
}	m_rtmsg;

/*
 * Set an individual arp entry
 */
int bsd_arp_set(ia, eaddr, len)
	struct in_addr *ia;
	char *eaddr;
	int len;
{
	struct sockaddr_in *sin = &sin_m;
	struct sockaddr_dl *sdl;
	struct rt_msghdr *rtm = &(m_rtmsg.m_rtm);
	u_char *ea;
	struct timespec tp;
	int op = RTM_ADD;

	getsocket();
	sdl_m = blank_sdl;
	sin_m = blank_sin;
	sin->sin_addr = *ia;

	ea = (u_char *)LLADDR(&sdl_m);
	bcopy(eaddr, ea, len);
	sdl_m.sdl_alen = len;
	doing_proxy = flags = expire_time = 0;

	/* make arp entry temporary */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	expire_time = tp.tv_sec + 20 * 60;

tryagain:
	if (rtmsg(RTM_GET) < 0) {
		report(LOG_WARNING, "rtmget: %s", strerror(errno));
		return (1);
	}
	sin = (struct sockaddr_in *)(rtm + 1);
	sdl = (struct sockaddr_dl *)(sin->sin_len + (char *)sin);
	if (sin->sin_addr.s_addr == sin_m.sin_addr.s_addr) {
		if (sdl->sdl_family == AF_LINK &&
		    !(rtm->rtm_flags & RTF_GATEWAY)) switch (sdl->sdl_type) {
		case IFT_ETHER: case IFT_FDDI: case IFT_ISO88023:
		case IFT_ISO88024: case IFT_ISO88025:
			op = RTM_CHANGE;
			goto overwrite;
		}
		if (doing_proxy == 0) {
			report(LOG_WARNING, "set: can only proxy for %s\n",
				inet_ntoa(sin->sin_addr));
			return (1);
		}
		goto tryagain;
	}
overwrite:
	if (sdl->sdl_family != AF_LINK) {
		report(LOG_WARNING,
			"cannot intuit interface index and type for %s\n",
			inet_ntoa(sin->sin_addr));
		return (1);
	}
	sdl_m.sdl_type = sdl->sdl_type;
	sdl_m.sdl_index = sdl->sdl_index;
	return (rtmsg(op));
}


static int rtmsg(cmd)
	int cmd;
{
	static int seq;
	int rlen;
	struct rt_msghdr *rtm = &m_rtmsg.m_rtm;
	char *cp = m_rtmsg.m_space;
	int l;

	errno = 0;
	bzero((char *)&m_rtmsg, sizeof(m_rtmsg));
	rtm->rtm_flags = flags;
	rtm->rtm_version = RTM_VERSION;

	switch (cmd) {
	default:
		report(LOG_ERR, "set_arp: internal wrong cmd - exiting");
		exit(1);
	case RTM_ADD:
	case RTM_CHANGE:
		rtm->rtm_addrs |= RTA_GATEWAY;
		rtm->rtm_rmx.rmx_expire = expire_time;
		rtm->rtm_inits = RTV_EXPIRE;
		rtm->rtm_flags |= (RTF_HOST | RTF_STATIC | RTF_LLDATA);
		if (doing_proxy) {
			rtm->rtm_addrs |= RTA_NETMASK;
			rtm->rtm_flags &= ~RTF_HOST;
		}
		/* FALLTHROUGH */
	case RTM_GET:
		rtm->rtm_addrs |= RTA_DST;
	}
#define NEXTADDR(w, s) \
	if (rtm->rtm_addrs & (w)) { \
		bcopy((char *)&s, cp, sizeof(s)); cp += sizeof(s);}

	NEXTADDR(RTA_DST, sin_m);
	NEXTADDR(RTA_GATEWAY, sdl_m);
	NEXTADDR(RTA_NETMASK, so_mask);

	rtm->rtm_msglen = cp - (char *)&m_rtmsg;

	l = rtm->rtm_msglen;
	rtm->rtm_seq = ++seq;
	rtm->rtm_type = cmd;
	if ((rlen = write(s, (char *)&m_rtmsg, l)) < 0) {
		if ((errno != ESRCH) && !(errno == EEXIST && cmd == RTM_ADD)){
			report(LOG_WARNING, "writing to routing socket: %s",
				strerror(errno));
			return (-1);
		}
	}
	do {
		l = read(s, (char *)&m_rtmsg, sizeof(m_rtmsg));
	} while (l > 0 && (rtm->rtm_type != cmd || rtm->rtm_seq != seq || rtm->rtm_pid != getpid()));
	if (l < 0)
		report(LOG_WARNING, "arp: read from routing socket: %s\n",
		    strerror(errno));
	return (0);
}

#endif /* BSD */
#endif /* BSD >= 199306 */
