/*	$OpenBSD: arptab.c,v 1.31 2019/06/28 13:32:50 deraadt Exp $ */

/*
 * Copyright (c) 1984, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Sun Microsystems, Inc.
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
 * set arp table entries
 */


#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <err.h>

/* ROUNDUP() is nasty, but it is identical to what's in the kernel. */
#define ROUNDUP(a)					\
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

static pid_t pid;
static int s = -1;

int rtget(struct sockaddr_inarp **, struct sockaddr_dl **);

void
arptab_init(void)
{
	s = socket(AF_ROUTE, SOCK_RAW, 0);
	if (s == -1)
		err(1, "arp: socket");
}

struct	sockaddr_in so_mask = {8, 0, 0, { 0xffffffff}};
struct	sockaddr_inarp blank_sin = {sizeof(blank_sin), AF_INET }, sin_m;
struct	sockaddr_dl blank_sdl = {sizeof(blank_sdl), AF_LINK }, sdl_m;
struct	sockaddr_dl ifp_m = {sizeof(&ifp_m), AF_LINK};
time_t	expire_time;
int	flags, export_only, doing_proxy;

struct	{
	struct	rt_msghdr m_rtm;
	char	m_space[512];
} m_rtmsg;

int	arptab_set(u_char *, u_int32_t);
int	rtmsg(int);

/*
 * Set an individual arp entry
 */
int
arptab_set(u_char *eaddr, u_int32_t host)
{
	struct sockaddr_inarp *sin = &sin_m;
	struct rt_msghdr *rtm = &(m_rtmsg.m_rtm);
	struct sockaddr_dl *sdl;
	struct timeval now;
	int rt;

	pid = getpid();

	sdl_m = blank_sdl;
	sin_m = blank_sin;
	sin->sin_addr.s_addr = host;
	memcpy((u_char *)LLADDR(&sdl_m), (char *)eaddr, 6);
	sdl_m.sdl_alen = 6;
	expire_time = 0;
	doing_proxy = flags = export_only = 0;
	gettimeofday(&now, 0);
	expire_time = now.tv_sec + 20 * 60;

tryagain:
	if (rtget(&sin, &sdl)) {
		syslog(LOG_ERR,"%s: %m", inet_ntoa(sin->sin_addr));
		return (1);
	}

	if (sin->sin_addr.s_addr == sin_m.sin_addr.s_addr) {
		if (sdl->sdl_family == AF_LINK &&
		    (rtm->rtm_flags & RTF_LLINFO) &&
		    !(rtm->rtm_flags & RTF_GATEWAY))
			switch (sdl->sdl_type) {
			case IFT_ETHER:
			case IFT_FDDI:
			case IFT_ISO88023:
			case IFT_ISO88024:
			case IFT_ISO88025:
				goto overwrite;
			default:
				break;
		}
		if (doing_proxy == 0) {
			syslog(LOG_ERR, "arptab_set: can only proxy for %s",
			    inet_ntoa(sin->sin_addr));
			return (1);
		}
		if (sin_m.sin_other & SIN_PROXY) {
			syslog(LOG_ERR,
			    "arptab_set: proxy entry exists for non 802 device");
			return(1);
		}
		sin_m.sin_other = SIN_PROXY;
		export_only = 1;
		goto tryagain;
	}
overwrite:
	if (sdl->sdl_family != AF_LINK) {
		syslog(LOG_ERR,
		    "arptab_set: cannot intuit interface index and type for %s",
		    inet_ntoa(sin->sin_addr));
		return (1);
	}
	sdl_m.sdl_type = sdl->sdl_type;
	sdl_m.sdl_index = sdl->sdl_index;
	rt = rtmsg(RTM_ADD);
	return (rt);
}

int
rtmsg(int cmd)
{
	static int seq;
	struct rt_msghdr *rtm = &m_rtmsg.m_rtm;
	char *cp = m_rtmsg.m_space;
	int l;

retry:
	errno = 0;
	if (cmd == RTM_DELETE)
		goto doit;
	memset((char *)&m_rtmsg, 0, sizeof(m_rtmsg));
	rtm->rtm_flags = flags;
	rtm->rtm_version = RTM_VERSION;

	switch (cmd) {
	default:
		syslog(LOG_ERR, "arptab_set: internal wrong cmd");
		exit(1);
	case RTM_ADD:
		rtm->rtm_addrs |= RTA_GATEWAY;
		rtm->rtm_rmx.rmx_expire = expire_time;
		rtm->rtm_inits = RTV_EXPIRE;
		rtm->rtm_flags |= (RTF_HOST | RTF_STATIC);
		sin_m.sin_other = 0;
		if (doing_proxy) {
			if (export_only)
				sin_m.sin_other = SIN_PROXY;
			else {
				rtm->rtm_addrs |= RTA_NETMASK;
				rtm->rtm_flags &= ~RTF_HOST;
			}
		}
		/* FALLTHROUGH */
	case RTM_GET:
		rtm->rtm_addrs |= (RTA_DST | RTA_IFP);
	}
#define NEXTADDR(w, s) \
	if (rtm->rtm_addrs & (w)) { \
		memcpy(cp, (char *)&s, sizeof(s)); \
		cp += sizeof(s); \
	}

	NEXTADDR(RTA_DST, sin_m);
	NEXTADDR(RTA_GATEWAY, sdl_m);
	NEXTADDR(RTA_NETMASK, so_mask);
	NEXTADDR(RTA_IFP, ifp_m);

	rtm->rtm_msglen = cp - (char *)&m_rtmsg;
doit:
	l = rtm->rtm_msglen;
	rtm->rtm_seq = ++seq;
	rtm->rtm_type = cmd;
	if (write(s, (char *)&m_rtmsg, l) == -1) {
		if (errno != ESRCH && errno != EEXIST) {
			syslog(LOG_ERR, "writing to routing socket: %m");
			return (-1);
		}
	}
	do {
		l = recv(s, (char *)&m_rtmsg, sizeof(m_rtmsg), MSG_DONTWAIT);
	} while (l > 0 && (rtm->rtm_version != RTM_VERSION ||
	    rtm->rtm_seq != seq || rtm->rtm_pid != pid));
	if (l == -1) {
		if (errno == EAGAIN || errno == EINTR)
			goto retry;
		syslog(LOG_ERR, "arptab_set: read from routing socket: %m");
	}
	return (0);
}

int
rtget(struct sockaddr_inarp **sinp, struct sockaddr_dl **sdlp)
{
	struct rt_msghdr *rtm = &(m_rtmsg.m_rtm);
	struct sockaddr_inarp *sin = NULL;
	struct sockaddr_dl *sdl = NULL;
	struct sockaddr *sa;
	char *cp;
	unsigned int i;

	if (rtmsg(RTM_GET) < 0)
		return (1);

	if (rtm->rtm_addrs) {
		cp = ((char *)rtm + rtm->rtm_hdrlen);
		for (i = 1; i; i <<= 1) {
			if (i & rtm->rtm_addrs) {
				sa = (struct sockaddr *)cp;
				switch (i) {
				case RTA_DST:
					sin = (struct sockaddr_inarp *)sa;
					break;
				case RTA_IFP:
					sdl = (struct sockaddr_dl *)sa;
					break;
				default:
					break;
				}
				cp += ROUNDUP(sa->sa_len);
			}
		}
	}

	if (sin == NULL || sdl == NULL)
		return (1);

	*sinp = sin;
	*sdlp = sdl;

	return (0);
}
