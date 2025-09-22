/*	$OpenBSD: pfe_route.c,v 1.14 2023/06/29 16:24:53 claudio Exp $	*/

/*
 * Copyright (c) 2009 - 2011 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <net/route.h>
#include <arpa/inet.h>

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "relayd.h"

void
init_routes(struct relayd *env)
{
	u_int	 rtfilter;

	if (!(env->sc_conf.flags & F_NEEDRT))
		return;

	if ((env->sc_rtsock = socket(AF_ROUTE, SOCK_RAW, 0)) == -1)
		fatal("%s: failed to open routing socket", __func__);

	rtfilter = ROUTE_FILTER(0);
	if (setsockopt(env->sc_rtsock, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		fatal("%s: ROUTE_MSGFILTER", __func__);
}

void
sync_routes(struct relayd *env, struct router *rt)
{
	struct netroute		*nr;
	struct host		*host;
	char			 buf[HOST_NAME_MAX+1];
	struct ctl_netroute	 crt;

	if (!(env->sc_conf.flags & F_NEEDRT))
		return;

	TAILQ_FOREACH(nr, &rt->rt_netroutes, nr_entry) {
		print_host(&nr->nr_conf.ss, buf, sizeof(buf));
		TAILQ_FOREACH(host, &rt->rt_gwtable->hosts, entry) {
			if (host->up == HOST_UNKNOWN)
				continue;

			log_debug("%s: "
			    "router %s route %s/%d gateway %s %s priority %d",
			    __func__,
			    rt->rt_conf.name, buf, nr->nr_conf.prefixlen,
			    host->conf.name,
			    HOST_ISUP(host->up) ? "up" : "down",
			    host->conf.priority);

			crt.up = host->up;
			memcpy(&crt.nr, &nr->nr_conf, sizeof(nr->nr_conf));
			memcpy(&crt.host, &host->conf, sizeof(host->conf));
			memcpy(&crt.rt, &rt->rt_conf, sizeof(rt->rt_conf));

			proc_compose(env->sc_ps, PROC_PARENT,
			    IMSG_RTMSG, &crt, sizeof(crt));
		}
	}
}

static void
pfe_apply_prefixlen(struct sockaddr_storage *ss, int af, int len)
{
	int q, r, off;
	uint8_t *b = (uint8_t *)ss;

	q = len >> 3;
	r = len & 7;

	bzero(ss, sizeof(*ss));
	ss->ss_family = af;
	switch (af) {
	case AF_INET:
		ss->ss_len = sizeof(struct sockaddr_in);
		off = offsetof(struct sockaddr_in, sin_addr);
		break;
	case AF_INET6:
		ss->ss_len = sizeof(struct sockaddr_in6);
		off = offsetof(struct sockaddr_in6, sin6_addr);
		break;
	default:
		fatal("%s: invalid address family", __func__);
	}
	if (q > 0)
		memset(b + off, 0xff, q);
	if (r > 0)
		b[off + q] = (0xff00 >> r) & 0xff;
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int
pfe_route(struct relayd *env, struct ctl_netroute *crt)
{
	struct iovec			 iov[5];
	struct rt_msghdr		 hdr;
	struct sockaddr_storage		 dst, gw, mask, label;
	struct sockaddr_rtlabel		*sr = (struct sockaddr_rtlabel *)&label;
	int				 iovcnt = 0;
	char				*gwname;

	bzero(&hdr, sizeof(hdr));
	hdr.rtm_msglen = sizeof(hdr);
	hdr.rtm_version = RTM_VERSION;
	hdr.rtm_type = HOST_ISUP(crt->up) ? RTM_ADD : RTM_DELETE;
	hdr.rtm_flags = RTF_STATIC | RTF_GATEWAY | RTF_MPATH;
	hdr.rtm_seq = env->sc_rtseq++;
	hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	hdr.rtm_tableid = crt->rt.rtable;
	hdr.rtm_priority = crt->host.priority;

	iov[iovcnt].iov_base = &hdr;
	iov[iovcnt++].iov_len = sizeof(hdr);

	dst = crt->nr.ss;
	gw = crt->host.ss;
	gwname = crt->host.name;
	pfe_apply_prefixlen(&mask, dst.ss_family, crt->nr.prefixlen);

	iov[iovcnt].iov_base = &dst;
	iov[iovcnt++].iov_len = ROUNDUP(dst.ss_len);
	hdr.rtm_msglen += ROUNDUP(dst.ss_len);

	iov[iovcnt].iov_base = &gw;
	iov[iovcnt++].iov_len = ROUNDUP(gw.ss_len);
	hdr.rtm_msglen += ROUNDUP(gw.ss_len);

	iov[iovcnt].iov_base = &mask;
	iov[iovcnt++].iov_len = ROUNDUP(mask.ss_len);
	hdr.rtm_msglen += ROUNDUP(mask.ss_len);

	if (strlen(crt->rt.label)) {
		sr->sr_len = sizeof(*sr);
		strlcpy(sr->sr_label, crt->rt.label, sizeof(sr->sr_label));

		iov[iovcnt].iov_base = &label;
		iov[iovcnt++].iov_len = ROUNDUP(label.ss_len);
		hdr.rtm_msglen += ROUNDUP(label.ss_len);
		hdr.rtm_addrs |= RTA_LABEL;
	}

 retry:
	if (writev(env->sc_rtsock, iov, iovcnt) == -1) {
		switch (errno) {
		case EEXIST:
		case ESRCH:
			if (hdr.rtm_type == RTM_ADD) {
				hdr.rtm_type = RTM_CHANGE;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				/* Ignore */
				break;
			}
			/* FALLTHROUGH */
		default:
			goto bad;
		}
	}

	log_debug("%s: gateway %s %s", __func__, gwname,
	    HOST_ISUP(crt->up) ? "added" : "deleted");

	return (0);

 bad:
	log_debug("%s: failed to %s gateway %s: %d %s", __func__,
	    HOST_ISUP(crt->up) ? "add" : "delete", gwname,
	    errno, strerror(errno));

	return (-1);
}
