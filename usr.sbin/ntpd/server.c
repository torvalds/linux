/*	$OpenBSD: server.c,v 1.44 2016/09/03 11:52:06 reyk Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2004 Alexander Guy <alexander@openbsd.org>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ntpd.h"

int
setup_listeners(struct servent *se, struct ntpd_conf *lconf, u_int *cnt)
{
	struct listen_addr	*la, *nla, *lap;
	struct ifaddrs		*ifa, *ifap;
	struct sockaddr		*sa;
	struct if_data		*ifd;
	u_int8_t		*a6;
	size_t			 sa6len = sizeof(struct in6_addr);
	u_int			 new_cnt = 0;
	int			 tos = IPTOS_LOWDELAY, rdomain = 0;

	TAILQ_FOREACH(lap, &lconf->listen_addrs, entry) {
		switch (lap->sa.ss_family) {
		case AF_UNSPEC:
			if (getifaddrs(&ifa) == -1)
				fatal("getifaddrs");

			for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
				sa = ifap->ifa_addr;
				if (sa == NULL || SA_LEN(sa) == 0)
					continue;
				if (sa->sa_family == AF_LINK) {
					ifd = ifap->ifa_data;
					rdomain = ifd->ifi_rdomain;
				}
				if (sa->sa_family != AF_INET &&
				    sa->sa_family != AF_INET6)
					continue;
				if (lap->rtable != -1 && rdomain != lap->rtable)
					continue;

				if (sa->sa_family == AF_INET &&
				    ((struct sockaddr_in *)sa)->sin_addr.s_addr ==
				    INADDR_ANY)
					continue;

				if (sa->sa_family == AF_INET6) {
					a6 = ((struct sockaddr_in6 *)sa)->
					    sin6_addr.s6_addr;
					if (memcmp(a6, &in6addr_any, sa6len) == 0)
						continue;
				}

				if ((la = calloc(1, sizeof(struct listen_addr))) ==
				    NULL)
					fatal("setup_listeners calloc");

				memcpy(&la->sa, sa, SA_LEN(sa));
				la->rtable = rdomain;

				TAILQ_INSERT_TAIL(&lconf->listen_addrs, la, entry);
			}

			freeifaddrs(ifa);
		default:
			continue;
		}
	}


	for (la = TAILQ_FIRST(&lconf->listen_addrs); la; ) {
		switch (la->sa.ss_family) {
		case AF_INET:
			if (((struct sockaddr_in *)&la->sa)->sin_port == 0)
				((struct sockaddr_in *)&la->sa)->sin_port =
				    se->s_port;
			break;
		case AF_INET6:
			if (((struct sockaddr_in6 *)&la->sa)->sin6_port == 0)
				((struct sockaddr_in6 *)&la->sa)->sin6_port =
				    se->s_port;
			break;
		case AF_UNSPEC:
			nla = TAILQ_NEXT(la, entry);
			TAILQ_REMOVE(&lconf->listen_addrs, la, entry);
			free(la);
			la = nla;
			continue;
		default:
			fatalx("king bula sez: af borked");
		}

		log_info("listening on %s %s",
		    log_sockaddr((struct sockaddr *)&la->sa),
		    print_rtable(la->rtable));

		if ((la->fd = socket(la->sa.ss_family, SOCK_DGRAM, 0)) == -1)
			fatal("socket");

		if (la->sa.ss_family == AF_INET && setsockopt(la->fd,
		    IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == -1)
			log_warn("setsockopt IPTOS_LOWDELAY");

		if (la->rtable != -1 &&
		    setsockopt(la->fd, SOL_SOCKET, SO_RTABLE, &la->rtable,
		    sizeof(la->rtable)) == -1)
			fatal("setup_listeners setsockopt SO_RTABLE");

		if (bind(la->fd, (struct sockaddr *)&la->sa,
		    SA_LEN((struct sockaddr *)&la->sa)) == -1) {
			log_warn("bind on %s failed, skipping",
			    log_sockaddr((struct sockaddr *)&la->sa));
			close(la->fd);
			nla = TAILQ_NEXT(la, entry);
			TAILQ_REMOVE(&lconf->listen_addrs, la, entry);
			free(la);
			la = nla;
			continue;
		}
		new_cnt++;
		la = TAILQ_NEXT(la, entry);
	}

	*cnt = new_cnt;

	return (0);
}

int
server_dispatch(int fd, struct ntpd_conf *lconf)
{
	ssize_t			 size;
	double			 rectime;
	struct sockaddr_storage	 fsa;
	socklen_t		 fsa_len;
	struct ntp_msg		 query, reply;
	char			 buf[NTP_MSGSIZE];

	fsa_len = sizeof(fsa);
	if ((size = recvfrom(fd, &buf, sizeof(buf), 0,
	    (struct sockaddr *)&fsa, &fsa_len)) == -1) {
		if (errno == EHOSTUNREACH || errno == EHOSTDOWN ||
		    errno == ENETUNREACH || errno == ENETDOWN) {
			log_warn("recvfrom %s",
			    log_sockaddr((struct sockaddr *)&fsa));
			return (0);
		} else
			fatal("recvfrom");
	}

	rectime = gettime_corrected();

	if (ntp_getmsg((struct sockaddr *)&fsa, buf, size, &query) == -1)
		return (0);

	memset(&reply, 0, sizeof(reply));
	if (lconf->status.synced)
		reply.status = lconf->status.leap;
	else
		reply.status = LI_ALARM;
	reply.status |= (query.status & VERSIONMASK);
	if ((query.status & MODEMASK) == MODE_CLIENT)
		reply.status |= MODE_SERVER;
	else if ((query.status & MODEMASK) == MODE_SYM_ACT)
		reply.status |= MODE_SYM_PAS;
	else /* ignore packets of different type (e.g. bcast) */
		return (0);

	reply.stratum =	lconf->status.stratum;
	reply.ppoll = query.ppoll;
	reply.precision = lconf->status.precision;
	reply.rectime = d_to_lfp(rectime);
	reply.reftime = d_to_lfp(lconf->status.reftime);
	reply.xmttime = d_to_lfp(gettime_corrected());
	reply.orgtime = query.xmttime;
	reply.rootdelay = d_to_sfp(lconf->status.rootdelay);
	reply.refid = lconf->status.refid;

	ntp_sendmsg(fd, (struct sockaddr *)&fsa, &reply);
	return (0);
}
