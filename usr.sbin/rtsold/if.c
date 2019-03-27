/*	$KAME: if.c,v 1.27 2003/10/05 00:09:36 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>

#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <capsicum_helpers.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <ifaddrs.h>
#include "rtsold.h"

static int ifsock;
static void get_rtaddrs(int, struct sockaddr *, struct sockaddr **);

int
ifinit(void)
{
	cap_rights_t rights;
	int sock;

	sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (sock < 0) {
		warnmsg(LOG_ERR, __func__, "socket(): %s",
		    strerror(errno));
		return (-1);
	}
	if (caph_rights_limit(sock, cap_rights_init(&rights, CAP_IOCTL)) < 0) {
		warnmsg(LOG_ERR, __func__, "caph_rights_limit(): %s",
		    strerror(errno));
		(void)close(sock);
		return (-1);
	}
	ifsock = sock;
	return (0);
}

int
interface_up(char *name)
{
	struct ifreq ifr;
	struct in6_ndireq nd;
	int llflag;
	int s;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	memset(&nd, 0, sizeof(nd));
	strlcpy(nd.ifname, name, sizeof(nd.ifname));

	if (ioctl(ifsock, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		warnmsg(LOG_WARNING, __func__, "ioctl(SIOCGIFFLAGS): %s",
		    strerror(errno));
		return (-1);
	}
	if (!(ifr.ifr_flags & IFF_UP)) {
		ifr.ifr_flags |= IFF_UP;
		if (ioctl(ifsock, SIOCSIFFLAGS, (caddr_t)&ifr) < 0)
			warnmsg(LOG_ERR, __func__,
			    "ioctl(SIOCSIFFLAGS): %s", strerror(errno));
		return (-1);
	}
	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		warnmsg(LOG_WARNING, __func__, "socket(AF_INET6, SOCK_DGRAM): %s",
		    strerror(errno));
		return (-1);
	}
	if (ioctl(s, SIOCGIFINFO_IN6, (caddr_t)&nd) < 0) {
		warnmsg(LOG_WARNING, __func__, "ioctl(SIOCGIFINFO_IN6): %s",
		    strerror(errno));
		close(s);
		return (-1);
	}

	warnmsg(LOG_DEBUG, __func__, "checking if %s is ready...", name);

	if (nd.ndi.flags & ND6_IFF_IFDISABLED) {
		if (Fflag) {
			nd.ndi.flags &= ~ND6_IFF_IFDISABLED;
			if (ioctl(s, SIOCSIFINFO_IN6, (caddr_t)&nd)) {
				warnmsg(LOG_WARNING, __func__,
				    "ioctl(SIOCSIFINFO_IN6): %s",
		    		    strerror(errno));
				close(s);
				return (-1);
			}
		} else {
			warnmsg(LOG_WARNING, __func__,
			    "%s is disabled.", name);
			close(s);
			return (-1);
		}
	}
	if (!(nd.ndi.flags & ND6_IFF_ACCEPT_RTADV)) {
		if (Fflag) {
			nd.ndi.flags |= ND6_IFF_ACCEPT_RTADV;
			if (ioctl(s, SIOCSIFINFO_IN6, (caddr_t)&nd)) {
				warnmsg(LOG_WARNING, __func__,
				    "ioctl(SIOCSIFINFO_IN6): %s",
		    		    strerror(errno));
				close(s);
				return (-1);
			}
		} else {
			warnmsg(LOG_WARNING, __func__,
			    "%s does not accept Router Advertisement.", name);
			close(s);
			return (-1);
		}
	}
	close(s);

	if (cap_llflags_get(capllflags, name, &llflag) != 0) {
		warnmsg(LOG_WARNING, __func__,
		    "cap_llflags_get() failed, anyway I'll try");
		return (0);
	}

	if (!(llflag & IN6_IFF_NOTREADY)) {
		warnmsg(LOG_DEBUG, __func__, "%s is ready", name);
		return (0);
	} else {
		if (llflag & IN6_IFF_TENTATIVE) {
			warnmsg(LOG_DEBUG, __func__, "%s is tentative",
			    name);
			return (IFS_TENTATIVE);
		}
		if (llflag & IN6_IFF_DUPLICATED)
			warnmsg(LOG_DEBUG, __func__, "%s is duplicated",
			    name);
		return (-1);
	}
}

int
interface_status(struct ifinfo *ifinfo)
{
	char *ifname = ifinfo->ifname;
	struct ifreq ifr;
	struct ifmediareq ifmr;

	/* get interface flags */
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(ifsock, SIOCGIFFLAGS, &ifr) < 0) {
		warnmsg(LOG_ERR, __func__, "ioctl(SIOCGIFFLAGS) on %s: %s",
		    ifname, strerror(errno));
		return (-1);
	}
	/*
	 * if one of UP and RUNNING flags is dropped,
	 * the interface is not active.
	 */
	if ((ifr.ifr_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
		goto inactive;
	/* Next, check carrier on the interface, if possible */
	if (!ifinfo->mediareqok)
		goto active;
	memset(&ifmr, 0, sizeof(ifmr));
	strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));

	if (ioctl(ifsock, SIOCGIFMEDIA, (caddr_t)&ifmr) < 0) {
		if (errno != EINVAL) {
			warnmsg(LOG_DEBUG, __func__,
			    "ioctl(SIOCGIFMEDIA) on %s: %s",
			    ifname, strerror(errno));
			return(-1);
		}
		/*
		 * EINVAL simply means that the interface does not support
		 * the SIOCGIFMEDIA ioctl. We regard it alive.
		 */
		ifinfo->mediareqok = 0;
		goto active;
	}

	if (ifmr.ifm_status & IFM_AVALID) {
		switch (ifmr.ifm_active & IFM_NMASK) {
		case IFM_ETHER:
		case IFM_IEEE80211:
			if (ifmr.ifm_status & IFM_ACTIVE)
				goto active;
			else
				goto inactive;
			break;
		default:
			goto inactive;
		}
	}

  inactive:
	return (0);

  active:
	return (1);
}

#define ROUNDUP(a, size) \
	(((a) & ((size)-1)) ? (1 + ((a) | ((size)-1))) : (a))

#define NEXT_SA(ap) (ap) = (struct sockaddr *) \
	((caddr_t)(ap) + ((ap)->sa_len ? ROUNDUP((ap)->sa_len,\
	sizeof(u_long)) : sizeof(u_long)))
#define ROUNDUP8(a) (1 + (((a) - 1) | 7))

int
lladdropt_length(struct sockaddr_dl *sdl)
{
	switch (sdl->sdl_type) {
	case IFT_ETHER:
		return (ROUNDUP8(ETHER_ADDR_LEN + 2));
	default:
		return (0);
	}
}

void
lladdropt_fill(struct sockaddr_dl *sdl, struct nd_opt_hdr *ndopt)
{
	char *addr;

	ndopt->nd_opt_type = ND_OPT_SOURCE_LINKADDR; /* fixed */

	switch (sdl->sdl_type) {
	case IFT_ETHER:
		ndopt->nd_opt_len = (ROUNDUP8(ETHER_ADDR_LEN + 2)) >> 3;
		addr = (char *)(ndopt + 1);
		memcpy(addr, LLADDR(sdl), ETHER_ADDR_LEN);
		break;
	default:
		warnmsg(LOG_ERR, __func__,
		    "unsupported link type(%d)", sdl->sdl_type);
		exit(1);
	}
}

struct sockaddr_dl *
if_nametosdl(char *name)
{
	int mib[] = {CTL_NET, AF_ROUTE, 0, 0, NET_RT_IFLIST, 0};
	char *buf, *next, *lim;
	size_t len;
	struct if_msghdr *ifm;
	struct sockaddr *sa, *rti_info[RTAX_MAX];
	struct sockaddr_dl *sdl = NULL, *ret_sdl;

	if (sysctl(mib, nitems(mib), NULL, &len, NULL, 0) < 0)
		return(NULL);
	if ((buf = malloc(len)) == NULL)
		return(NULL);
	if (sysctl(mib, nitems(mib), buf, &len, NULL, 0) < 0) {
		free(buf);
		return (NULL);
	}

	lim = buf + len;
	for (next = buf; next < lim; next += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)(void *)next;
		if (ifm->ifm_type == RTM_IFINFO) {
			sa = (struct sockaddr *)(ifm + 1);
			get_rtaddrs(ifm->ifm_addrs, sa, rti_info);
			if ((sa = rti_info[RTAX_IFP]) != NULL) {
				if (sa->sa_family == AF_LINK) {
					sdl = (struct sockaddr_dl *)(void *)sa;
					if (strlen(name) != sdl->sdl_nlen)
						continue; /* not same len */
					if (strncmp(&sdl->sdl_data[0],
						    name,
						    sdl->sdl_nlen) == 0) {
						break;
					}
				}
			}
		}
	}
	if (next == lim) {
		/* search failed */
		free(buf);
		return (NULL);
	}

	if ((ret_sdl = malloc(sdl->sdl_len)) == NULL) {
		free(buf);
		return (NULL);
	}
	memcpy((caddr_t)ret_sdl, (caddr_t)sdl, sdl->sdl_len);

	free(buf);
	return (ret_sdl);
}

static void
get_rtaddrs(int addrs, struct sockaddr *sa, struct sockaddr **rti_info)
{
	int i;

	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rti_info[i] = sa;
			NEXT_SA(sa);
		} else
			rti_info[i] = NULL;
	}
}
