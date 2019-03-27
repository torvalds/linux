/*	$FreeBSD$	*/
/*	$KAME: if.c,v 1.17 2001/01/21 15:27:30 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "pathnames.h"
#include "rtadvd.h"
#include "if.h"

#define ROUNDUP(a, size)					\
	(((a) & ((size)-1)) ? (1 + ((a) | ((size)-1))) : (a))

#define	NEXT_SA(ap)							\
	(ap) = (struct sockaddr *)((caddr_t)(ap) +			\
	    ((ap)->sa_len ? ROUNDUP((ap)->sa_len, sizeof(u_long)) :	\
	    sizeof(u_long)))

struct sockaddr_in6 sin6_linklocal_allnodes = {
        .sin6_len =     sizeof(sin6_linklocal_allnodes),
        .sin6_family =  AF_INET6,
        .sin6_addr =    IN6ADDR_LINKLOCAL_ALLNODES_INIT,
};

struct sockaddr_in6 sin6_linklocal_allrouters = {
        .sin6_len =     sizeof(sin6_linklocal_allrouters),
        .sin6_family =  AF_INET6,
        .sin6_addr =    IN6ADDR_LINKLOCAL_ALLROUTERS_INIT,
};

struct sockaddr_in6 sin6_sitelocal_allrouters = {
        .sin6_len =     sizeof(sin6_sitelocal_allrouters),
        .sin6_family =  AF_INET6,
        .sin6_addr =    IN6ADDR_SITELOCAL_ALLROUTERS_INIT,
};

struct sockinfo sock = { .si_fd = -1, .si_name = NULL };
struct sockinfo rtsock = { .si_fd = -1, .si_name = NULL };
struct sockinfo ctrlsock = { .si_fd = -1, .si_name = _PATH_CTRL_SOCK };

char *mcastif;

static void		get_rtaddrs(int, struct sockaddr *,
			    struct sockaddr **);
static struct if_msghdr	*get_next_msghdr(struct if_msghdr *,
			    struct if_msghdr *);

static void
get_rtaddrs(int addrs, struct sockaddr *sa, struct sockaddr **rti_info)
{
	int i;

	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rti_info[i] = sa;
			NEXT_SA(sa);
		}
		else
			rti_info[i] = NULL;
	}
}

#define ROUNDUP8(a) (1 + (((a) - 1) | 7))
int
lladdropt_length(struct sockaddr_dl *sdl)
{
	switch (sdl->sdl_type) {
	case IFT_ETHER:
	case IFT_L2VLAN:
	case IFT_BRIDGE:
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
	case IFT_L2VLAN:
	case IFT_BRIDGE:
		ndopt->nd_opt_len = (ROUNDUP8(ETHER_ADDR_LEN + 2)) >> 3;
		addr = (char *)(ndopt + 1);
		memcpy(addr, LLADDR(sdl), ETHER_ADDR_LEN);
		break;
	default:
		syslog(LOG_ERR, "<%s> unsupported link type(%d)",
		    __func__, sdl->sdl_type);
		exit(1);
	}

	return;
}

int
rtbuf_len(void)
{
	size_t len;
	int mib[6] = {CTL_NET, AF_ROUTE, 0, AF_INET6, NET_RT_DUMP, 0};

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0)
		return (-1);

	return (len);
}

#define FILTER_MATCH(type, filter) ((0x1 << type) & filter)
#define SIN6(s) ((struct sockaddr_in6 *)(s))
#define SDL(s) ((struct sockaddr_dl *)(s))
char *
get_next_msg(char *buf, char *lim, int ifindex, size_t *lenp, int filter)
{
	struct rt_msghdr *rtm;
	struct ifa_msghdr *ifam;
	struct sockaddr *sa, *dst, *gw, *ifa, *rti_info[RTAX_MAX];

	*lenp = 0;
	for (rtm = (struct rt_msghdr *)buf;
	     rtm < (struct rt_msghdr *)lim;
	     rtm = (struct rt_msghdr *)(((char *)rtm) + rtm->rtm_msglen)) {
		/* just for safety */
		if (!rtm->rtm_msglen) {
			syslog(LOG_WARNING, "<%s> rtm_msglen is 0 "
			    "(buf=%p lim=%p rtm=%p)", __func__,
			    buf, lim, rtm);
			break;
		}
		if (((struct rt_msghdr *)buf)->rtm_version != RTM_VERSION) {
			syslog(LOG_WARNING,
			    "<%s> routing message version mismatch "
			    "(buf=%p lim=%p rtm=%p)", __func__,
			    buf, lim, rtm);
			continue;
		}

		if (FILTER_MATCH(rtm->rtm_type, filter) == 0)
			continue;

		switch (rtm->rtm_type) {
		case RTM_GET:
		case RTM_ADD:
		case RTM_DELETE:
			/* address related checks */
			sa = (struct sockaddr *)(rtm + 1);
			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
			if ((dst = rti_info[RTAX_DST]) == NULL ||
			    dst->sa_family != AF_INET6)
				continue;

			if (IN6_IS_ADDR_LINKLOCAL(&SIN6(dst)->sin6_addr) ||
			    IN6_IS_ADDR_MULTICAST(&SIN6(dst)->sin6_addr))
				continue;

			if ((gw = rti_info[RTAX_GATEWAY]) == NULL ||
			    gw->sa_family != AF_LINK)
				continue;
			if (ifindex && SDL(gw)->sdl_index != ifindex)
				continue;

			if (rti_info[RTAX_NETMASK] == NULL)
				continue;

			/* found */
			*lenp = rtm->rtm_msglen;
			return (char *)rtm;
			/* NOTREACHED */
		case RTM_NEWADDR:
		case RTM_DELADDR:
			ifam = (struct ifa_msghdr *)rtm;

			/* address related checks */
			sa = (struct sockaddr *)(ifam + 1);
			get_rtaddrs(ifam->ifam_addrs, sa, rti_info);
			if ((ifa = rti_info[RTAX_IFA]) == NULL ||
			    (ifa->sa_family != AF_INET &&
			     ifa->sa_family != AF_INET6))
				continue;

			if (ifa->sa_family == AF_INET6 &&
			    (IN6_IS_ADDR_LINKLOCAL(&SIN6(ifa)->sin6_addr) ||
			     IN6_IS_ADDR_MULTICAST(&SIN6(ifa)->sin6_addr)))
				continue;

			if (ifindex && ifam->ifam_index != ifindex)
				continue;

			/* found */
			*lenp = ifam->ifam_msglen;
			return (char *)rtm;
			/* NOTREACHED */
		case RTM_IFINFO:
		case RTM_IFANNOUNCE:
			/* found */
			*lenp = rtm->rtm_msglen;
			return (char *)rtm;
			/* NOTREACHED */
		}
	}

	return ((char *)rtm);
}
#undef FILTER_MATCH

struct in6_addr *
get_addr(char *buf)
{
	struct rt_msghdr *rtm = (struct rt_msghdr *)buf;
	struct sockaddr *sa, *rti_info[RTAX_MAX];

	sa = (struct sockaddr *)(rtm + 1);
	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

	return (&SIN6(rti_info[RTAX_DST])->sin6_addr);
}

int
get_rtm_ifindex(char *buf)
{
	struct rt_msghdr *rtm = (struct rt_msghdr *)buf;
	struct sockaddr *sa, *rti_info[RTAX_MAX];

	sa = (struct sockaddr *)(rtm + 1);
	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

	return (((struct sockaddr_dl *)rti_info[RTAX_GATEWAY])->sdl_index);
}

int
get_prefixlen(char *buf)
{
	struct rt_msghdr *rtm = (struct rt_msghdr *)buf;
	struct sockaddr *sa, *rti_info[RTAX_MAX];
	char *p, *lim;

	sa = (struct sockaddr *)(rtm + 1);
	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
	sa = rti_info[RTAX_NETMASK];

	p = (char *)(&SIN6(sa)->sin6_addr);
	lim = (char *)sa + sa->sa_len;
	return prefixlen(p, lim);
}

int
prefixlen(unsigned char *p, unsigned char *lim)
{
	int masklen;

	for (masklen = 0; p < lim; p++) {
		switch (*p) {
		case 0xff:
			masklen += 8;
			break;
		case 0xfe:
			masklen += 7;
			break;
		case 0xfc:
			masklen += 6;
			break;
		case 0xf8:
			masklen += 5;
			break;
		case 0xf0:
			masklen += 4;
			break;
		case 0xe0:
			masklen += 3;
			break;
		case 0xc0:
			masklen += 2;
			break;
		case 0x80:
			masklen += 1;
			break;
		case 0x00:
			break;
		default:
			return (-1);
		}
	}

	return (masklen);
}

struct ifinfo *
update_persist_ifinfo(struct ifilist_head_t *ifi_head, const char *ifname)
{
	struct ifinfo *ifi;
	int ifindex;

	ifi = NULL;
	ifindex = if_nametoindex(ifname);
	TAILQ_FOREACH(ifi, ifi_head, ifi_next) {
		if (ifindex != 0) {
			if (ifindex == ifi->ifi_ifindex)
				break;
		} else {
			if (strncmp(ifname, ifi->ifi_ifname,
				sizeof(ifi->ifi_ifname)) == 0)
				break;
		}
	}

	if (ifi == NULL) {
		/* A new ifinfo element is needed. */
		syslog(LOG_DEBUG, "<%s> new entry: %s", __func__,
		    ifname);

		ELM_MALLOC(ifi, exit(1));
		ifi->ifi_ifindex = 0;
		strlcpy(ifi->ifi_ifname, ifname, sizeof(ifi->ifi_ifname));
		ifi->ifi_rainfo = NULL;
		ifi->ifi_state = IFI_STATE_UNCONFIGURED;
		TAILQ_INSERT_TAIL(ifi_head, ifi, ifi_next);
	}

	ifi->ifi_persist = 1;

	syslog(LOG_DEBUG, "<%s> %s is marked PERSIST", __func__,
	    ifi->ifi_ifname);
	syslog(LOG_DEBUG, "<%s> %s is state = %d", __func__,
	    ifi->ifi_ifname, ifi->ifi_state);
	return (ifi);
}

int
update_ifinfo_nd_flags(struct ifinfo *ifi)
{
	struct in6_ndireq nd;
	int s;
	int error;

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR,
		    "<%s> socket() failed.", __func__);
		return (1);
	}
	/* ND flags */
	memset(&nd, 0, sizeof(nd));
	strlcpy(nd.ifname, ifi->ifi_ifname,
	    sizeof(nd.ifname));
	error = ioctl(s, SIOCGIFINFO_IN6, (caddr_t)&nd);
	if (error) {
		close(s);
		if (errno != EPFNOSUPPORT)
			syslog(LOG_ERR, "<%s> ioctl() failed.", __func__);
		return (1);
	}
	ifi->ifi_nd_flags = nd.ndi.flags;
	close(s);

	return (0);
}

struct ifinfo *
update_ifinfo(struct ifilist_head_t *ifi_head, int ifindex)
{
	struct if_msghdr *ifm;
	struct ifinfo *ifi = NULL;
	struct sockaddr *sa;
	struct sockaddr *rti_info[RTAX_MAX];
	char *msg;
	size_t len;
	char *lim;
	int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET6, NET_RT_IFLIST, 0 };
	int error;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), NULL, &len, NULL, 0) <
	    0) {
		syslog(LOG_ERR,
		    "<%s> sysctl: NET_RT_IFLIST size get failed", __func__);
		exit(1);
	}
	if ((msg = malloc(len)) == NULL) {
		syslog(LOG_ERR, "<%s> malloc failed", __func__);
		exit(1);
	}
	if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), msg, &len, NULL, 0) <
	    0) {
		syslog(LOG_ERR,
		    "<%s> sysctl: NET_RT_IFLIST get failed", __func__);
		exit(1);
	}

	lim = msg + len;
	for (ifm = (struct if_msghdr *)msg;
	     ifm != NULL && ifm < (struct if_msghdr *)lim;
	     ifm = get_next_msghdr(ifm,(struct if_msghdr *)lim)) {
		int ifi_new;

		syslog(LOG_DEBUG, "<%s> ifm = %p, lim = %p, diff = %zu",
		    __func__, ifm, lim, (char *)lim - (char *)ifm);

		if (ifm->ifm_version != RTM_VERSION) {
			syslog(LOG_ERR,
			    "<%s> ifm_vesrion mismatch", __func__);
			exit(1);
		}
		if (ifm->ifm_msglen == 0) {
			syslog(LOG_WARNING,
			    "<%s> ifm_msglen is 0", __func__);
			free(msg);
			return (NULL);
		}

		ifi_new = 0;
		if (ifm->ifm_type == RTM_IFINFO) {
			struct ifreq ifr;
			int s;
			char ifname[IFNAMSIZ];

			syslog(LOG_DEBUG, "<%s> RTM_IFINFO found. "
			    "ifm_index = %d, ifindex = %d",
			    __func__, ifm->ifm_index, ifindex);

			/* when ifindex is specified */
			if (ifindex != UPDATE_IFINFO_ALL &&
			    ifindex != ifm->ifm_index)
				continue;

			/* ifname */
			if (if_indextoname(ifm->ifm_index, ifname) == NULL) {
				syslog(LOG_WARNING,
				    "<%s> ifname not found (idx=%d)",
				    __func__, ifm->ifm_index);
				continue;
			}

			/* lookup an entry with the same ifindex */
			TAILQ_FOREACH(ifi, ifi_head, ifi_next) {
				if (ifm->ifm_index == ifi->ifi_ifindex)
					break;
				if (strncmp(ifname, ifi->ifi_ifname,
					sizeof(ifname)) == 0)
					break;
			}
			if (ifi == NULL) {
				syslog(LOG_DEBUG,
				    "<%s> new entry for idx=%d",
				    __func__, ifm->ifm_index);
				ELM_MALLOC(ifi, exit(1));
				ifi->ifi_rainfo = NULL;
				ifi->ifi_state = IFI_STATE_UNCONFIGURED;
				ifi->ifi_persist = 0;
				ifi_new = 1;
			}
			/* ifindex */
			ifi->ifi_ifindex = ifm->ifm_index;

			/* ifname */
			strlcpy(ifi->ifi_ifname, ifname, IFNAMSIZ);

			if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
				syslog(LOG_ERR,
				    "<%s> socket() failed.", __func__);
				if (ifi_new)
					free(ifi);
				continue;
			}

			/* MTU  */
			ifi->ifi_phymtu = ifm->ifm_data.ifi_mtu;
			if (ifi->ifi_phymtu == 0) {
				memset(&ifr, 0, sizeof(ifr));
				ifr.ifr_addr.sa_family = AF_INET6;
				strlcpy(ifr.ifr_name, ifi->ifi_ifname,
				    sizeof(ifr.ifr_name));
				error = ioctl(s, SIOCGIFMTU, (caddr_t)&ifr);
				if (error) {
					close(s);
					syslog(LOG_ERR,
					    "<%s> ioctl() failed.",
					    __func__);
					if (ifi_new)
						free(ifi);
					continue;
				}
				ifi->ifi_phymtu = ifr.ifr_mtu;
				if (ifi->ifi_phymtu == 0) {
					syslog(LOG_WARNING,
					    "<%s> no interface mtu info"
					    " on %s.  %d will be used.",
					    __func__, ifi->ifi_ifname,
					    IPV6_MMTU);
					ifi->ifi_phymtu = IPV6_MMTU;
				}
			}
			close(s);

			/* ND flags */
			error = update_ifinfo_nd_flags(ifi);
			if (error) {
				if (ifi_new)
					free(ifi);
				continue;
			}

			/* SDL */
			sa = (struct sockaddr *)(ifm + 1);
			get_rtaddrs(ifm->ifm_addrs, sa, rti_info);
			if ((sa = rti_info[RTAX_IFP]) != NULL) {
				if (sa->sa_family == AF_LINK) {
					memcpy(&ifi->ifi_sdl,
					    (struct sockaddr_dl *)sa,
					    sizeof(ifi->ifi_sdl));
				}
			} else
				memset(&ifi->ifi_sdl, 0,
				    sizeof(ifi->ifi_sdl));

			/* flags */
			ifi->ifi_flags = ifm->ifm_flags;

			/* type */
			ifi->ifi_type = ifm->ifm_type;
		} else {
			syslog(LOG_ERR,
			    "out of sync parsing NET_RT_IFLIST\n"
			    "expected %d, got %d\n msglen = %d\n",
			    RTM_IFINFO, ifm->ifm_type, ifm->ifm_msglen);
			exit(1);
		}

		if (ifi_new) {
			syslog(LOG_DEBUG,
			    "<%s> adding %s(idx=%d) to ifilist",
			    __func__, ifi->ifi_ifname, ifi->ifi_ifindex);
			TAILQ_INSERT_TAIL(ifi_head, ifi, ifi_next);
		}
	}
	free(msg);

	if (mcastif != NULL) {
		error = sock_mc_rr_update(&sock, mcastif);
		if (error)
			exit(1);
	}

	return (ifi);
}

static struct if_msghdr *
get_next_msghdr(struct if_msghdr *ifm, struct if_msghdr *lim)
{
	struct ifa_msghdr *ifam;

	for (ifam = (struct ifa_msghdr *)((char *)ifm + ifm->ifm_msglen);
	     ifam < (struct ifa_msghdr *)lim;
	     ifam = (struct ifa_msghdr *)((char *)ifam + ifam->ifam_msglen)) {
		if (!ifam->ifam_msglen) {
			syslog(LOG_WARNING,
			    "<%s> ifa_msglen is 0", __func__);
			return (NULL);
		}
		if (ifam->ifam_type != RTM_NEWADDR)
			break;
	}

	return ((struct if_msghdr *)ifam);
}

int
getinet6sysctl(int code)
{
	int mib[] = { CTL_NET, PF_INET6, IPPROTO_IPV6, 0 };
	int value;
	size_t size;

	mib[3] = code;
	size = sizeof(value);
	if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), &value, &size, NULL, 0)
	    < 0) {
		syslog(LOG_ERR, "<%s>: failed to get ip6 sysctl(%d): %s",
		    __func__, code,
		    strerror(errno));
		return (-1);
	}
	else
		return (value);
}


int
sock_mc_join(struct sockinfo *s, int ifindex)
{
	struct ipv6_mreq mreq;
	char ifname[IFNAMSIZ];

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (ifindex == 0)
		return (1);

	/*
	 * join all routers multicast address on each advertising
	 * interface.
	 */
	memset(&mreq, 0, sizeof(mreq));
	/* XXX */
	memcpy(&mreq.ipv6mr_multiaddr.s6_addr,
	    &sin6_linklocal_allrouters.sin6_addr,
	    sizeof(mreq.ipv6mr_multiaddr.s6_addr));

	mreq.ipv6mr_interface = ifindex;
	if (setsockopt(s->si_fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq,
		sizeof(mreq)) < 0) {
		syslog(LOG_ERR,
		    "<%s> IPV6_JOIN_GROUP(link) on %s: %s",
		    __func__, if_indextoname(ifindex, ifname),
		    strerror(errno));
		return (1);
	}
	syslog(LOG_DEBUG,
	    "<%s> %s: join link-local all-routers MC group",
	    __func__, if_indextoname(ifindex, ifname));

	return (0);
}

int
sock_mc_leave(struct sockinfo *s, int ifindex)
{
	struct ipv6_mreq mreq;
	char ifname[IFNAMSIZ];

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (ifindex == 0)
		return (1);

	/*
	 * join all routers multicast address on each advertising
	 * interface.
	 */

	memset(&mreq, 0, sizeof(mreq));
	/* XXX */
	memcpy(&mreq.ipv6mr_multiaddr.s6_addr,
	    &sin6_linklocal_allrouters.sin6_addr,
	    sizeof(mreq.ipv6mr_multiaddr.s6_addr));

	mreq.ipv6mr_interface = ifindex;
	if (setsockopt(s->si_fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq,
		sizeof(mreq)) < 0) {
		syslog(LOG_ERR,
		    "<%s> IPV6_JOIN_LEAVE(link) on %s: %s",
		    __func__, if_indextoname(ifindex, ifname),
		    strerror(errno));
		return (1);
	}
	syslog(LOG_DEBUG,
	    "<%s> %s: leave link-local all-routers MC group",
	    __func__, if_indextoname(ifindex, ifname));

	return (0);
}

int
sock_mc_rr_update(struct sockinfo *s, char *mif)
{
	struct ipv6_mreq mreq;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (mif == NULL)
		return (1);
	/*
	 * When attending router renumbering, join all-routers site-local
	 * multicast group.
	 */
	/* XXX */
	memcpy(&mreq.ipv6mr_multiaddr.s6_addr,
	    &sin6_sitelocal_allrouters.sin6_addr,
	    sizeof(mreq.ipv6mr_multiaddr.s6_addr));
	if ((mreq.ipv6mr_interface = if_nametoindex(mif)) == 0) {
		syslog(LOG_ERR,
		    "<%s> invalid interface: %s",
		    __func__, mif);
		return (1);
	}

	if (setsockopt(s->si_fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		&mreq, sizeof(mreq)) < 0) {
		syslog(LOG_ERR,
		    "<%s> IPV6_JOIN_GROUP(site) on %s: %s",
		    __func__, mif, strerror(errno));
		return (1);
	}

	syslog(LOG_DEBUG,
	    "<%s> %s: join site-local all-routers MC group",
	    __func__, mif);

	return (0);
}
