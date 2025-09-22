/*	$OpenBSD: interface.c,v 1.52 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
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
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

static struct if_addr	*if_addr_new(struct kaddr *);
static struct if_addr	*if_addr_lookup(struct if_addr_head *, struct kaddr *);
static int		 if_start(struct iface *, int);
static int		 if_reset(struct iface *, int);
static void		 if_update_af(struct iface_af *, int);
static void		 if_hello_timer(int, short, void *);
static void		 if_start_hello_timer(struct iface_af *);
static void		 if_stop_hello_timer(struct iface_af *);
static int		 if_join_ipv4_group(struct iface *, struct in_addr *);
static int		 if_leave_ipv4_group(struct iface *, struct in_addr *);
static int		 if_join_ipv6_group(struct iface *, struct in6_addr *);
static int		 if_leave_ipv6_group(struct iface *, struct in6_addr *);

struct iface *
if_new(struct kif *kif)
{
	struct iface		*iface;

	if ((iface = calloc(1, sizeof(*iface))) == NULL)
		fatal("if_new: calloc");

	strlcpy(iface->name, kif->ifname, sizeof(iface->name));

	/* get type */
	if (kif->flags & IFF_POINTOPOINT)
		iface->type = IF_TYPE_POINTOPOINT;
	if (kif->flags & IFF_BROADCAST &&
	    kif->flags & IFF_MULTICAST)
		iface->type = IF_TYPE_BROADCAST;

	/* get index and flags */
	LIST_INIT(&iface->addr_list);
	iface->ifindex = kif->ifindex;
	iface->rdomain = kif->rdomain;
	iface->flags = kif->flags;
	iface->linkstate = kif->link_state;
	iface->if_type = kif->if_type;

	/* ipv4 */
	iface->ipv4.af = AF_INET;
	iface->ipv4.iface = iface;
	iface->ipv4.enabled = 0;
	iface->ipv4.state = IF_STA_DOWN;
	LIST_INIT(&iface->ipv4.adj_list);

	/* ipv6 */
	iface->ipv6.af = AF_INET6;
	iface->ipv6.iface = iface;
	iface->ipv6.enabled = 0;
	iface->ipv6.state = IF_STA_DOWN;
	LIST_INIT(&iface->ipv6.adj_list);

	return (iface);
}

void
if_exit(struct iface *iface)
{
	struct if_addr		*if_addr;

	log_debug("%s: interface %s", __func__, iface->name);

	if (iface->ipv4.state == IF_STA_ACTIVE)
		if_reset(iface, AF_INET);
	if (iface->ipv6.state == IF_STA_ACTIVE)
		if_reset(iface, AF_INET6);

	while ((if_addr = LIST_FIRST(&iface->addr_list)) != NULL) {
		LIST_REMOVE(if_addr, entry);
		free(if_addr);
	}
}

struct iface *
if_lookup(struct ldpd_conf *xconf, unsigned short ifindex)
{
	struct iface *iface;

	LIST_FOREACH(iface, &xconf->iface_list, entry)
		if (iface->ifindex == ifindex)
			return (iface);

	return (NULL);
}

struct iface_af *
iface_af_get(struct iface *iface, int af)
{
	switch (af) {
	case AF_INET:
		return (&iface->ipv4);
	case AF_INET6:
		return (&iface->ipv6);
	default:
		fatalx("iface_af_get: unknown af");
	}
}

static struct if_addr *
if_addr_new(struct kaddr *ka)
{
	struct if_addr	*if_addr;

	if ((if_addr = calloc(1, sizeof(*if_addr))) == NULL)
		fatal(__func__);

	if_addr->af = ka->af;
	if_addr->addr = ka->addr;
	if_addr->prefixlen = ka->prefixlen;
	if_addr->dstbrd = ka->dstbrd;

	return (if_addr);
}

static struct if_addr *
if_addr_lookup(struct if_addr_head *addr_list, struct kaddr *ka)
{
	struct if_addr	*if_addr;
	int		 af = ka->af;

	LIST_FOREACH(if_addr, addr_list, entry)
		if (!ldp_addrcmp(af, &if_addr->addr, &ka->addr) &&
		    if_addr->prefixlen == ka->prefixlen &&
		    !ldp_addrcmp(af, &if_addr->dstbrd, &ka->dstbrd))
			return (if_addr);

	return (NULL);
}

void
if_addr_add(struct kaddr *ka)
{
	struct iface		*iface;
	struct if_addr		*if_addr;
	struct nbr		*nbr;

	if (if_addr_lookup(&global.addr_list, ka) == NULL) {
		if_addr = if_addr_new(ka);

		LIST_INSERT_HEAD(&global.addr_list, if_addr, entry);
		RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
			if (nbr->state != NBR_STA_OPER)
				continue;
			if (if_addr->af == AF_INET && !nbr->v4_enabled)
				continue;
			if (if_addr->af == AF_INET6 && !nbr->v6_enabled)
				continue;

			send_address_single(nbr, if_addr, 0);
		}
	}

	iface = if_lookup(leconf, ka->ifindex);
	if (iface) {
		if (ka->af == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&ka->addr.v6))
			iface->linklocal = ka->addr.v6;

		if (if_addr_lookup(&iface->addr_list, ka) == NULL) {
			if_addr = if_addr_new(ka);
			LIST_INSERT_HEAD(&iface->addr_list, if_addr, entry);
			if_update(iface, if_addr->af);
		}
	}
}

void
if_addr_del(struct kaddr *ka)
{
	struct iface		*iface;
	struct if_addr		*if_addr;
	struct nbr		*nbr;

	iface = if_lookup(leconf, ka->ifindex);
	if (iface) {
		if (ka->af == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&iface->linklocal, &ka->addr.v6))
			memset(&iface->linklocal, 0, sizeof(iface->linklocal));

		if_addr = if_addr_lookup(&iface->addr_list, ka);
		if (if_addr) {
			LIST_REMOVE(if_addr, entry);
			if_update(iface, if_addr->af);
			free(if_addr);
		}
	}

	if_addr = if_addr_lookup(&global.addr_list, ka);
	if (if_addr) {
		RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
			if (nbr->state != NBR_STA_OPER)
				continue;
			if (if_addr->af == AF_INET && !nbr->v4_enabled)
				continue;
			if (if_addr->af == AF_INET6 && !nbr->v6_enabled)
				continue;
			send_address_single(nbr, if_addr, 1);
		}
		LIST_REMOVE(if_addr, entry);
		free(if_addr);
	}
}

static int
if_start(struct iface *iface, int af)
{
	struct iface_af		*ia;
	struct timeval		 now;

	log_debug("%s: %s address-family %s", __func__, iface->name,
	    af_name(af));

	ia = iface_af_get(iface, af);

	gettimeofday(&now, NULL);
	ia->uptime = now.tv_sec;

	switch (af) {
	case AF_INET:
		if (if_join_ipv4_group(iface, &global.mcast_addr_v4))
			return (-1);
		break;
	case AF_INET6:
		if (if_join_ipv6_group(iface, &global.mcast_addr_v6))
			return (-1);
		break;
	default:
		fatalx("if_start: unknown af");
	}

	send_hello(HELLO_LINK, ia, NULL);

	evtimer_set(&ia->hello_timer, if_hello_timer, ia);
	if_start_hello_timer(ia);
	return (0);
}

static int
if_reset(struct iface *iface, int af)
{
	struct iface_af		*ia;
	struct adj		*adj;

	log_debug("%s: %s address-family %s", __func__, iface->name,
	    af_name(af));

	ia = iface_af_get(iface, af);
	if_stop_hello_timer(ia);

	while ((adj = LIST_FIRST(&ia->adj_list)) != NULL)
		adj_del(adj, S_SHUTDOWN);

	/* try to cleanup */
	switch (af) {
	case AF_INET:
		if (global.ipv4.ldp_disc_socket != -1)
			if_leave_ipv4_group(iface, &global.mcast_addr_v4);
		break;
	case AF_INET6:
		if (global.ipv6.ldp_disc_socket != -1)
			if_leave_ipv6_group(iface, &global.mcast_addr_v6);
		break;
	default:
		fatalx("if_start: unknown af");
	}

	return (0);
}

static void
if_update_af(struct iface_af *ia, int link_ok)
{
	int			 addr_ok = 0, socket_ok, rtr_id_ok;
	struct if_addr		*if_addr;

	switch (ia->af) {
	case AF_INET:
		/*
		 * NOTE: for LDPv4, each interface should have at least one
		 * valid IP address otherwise they can not be enabled.
		 */
		LIST_FOREACH(if_addr, &ia->iface->addr_list, entry) {
			if (if_addr->af == AF_INET) {
				addr_ok = 1;
				break;
			}
		}
		break;
	case AF_INET6:
		/* for IPv6 the link-local address is enough. */
		if (IN6_IS_ADDR_LINKLOCAL(&ia->iface->linklocal))
			addr_ok = 1;
		break;
	default:
		fatalx("if_update_af: unknown af");
	}

	if ((ldp_af_global_get(&global, ia->af))->ldp_disc_socket != -1)
		socket_ok = 1;
	else
		socket_ok = 0;

	if (leconf->rtr_id.s_addr != INADDR_ANY)
		rtr_id_ok = 1;
	else
		rtr_id_ok = 0;

	if (ia->state == IF_STA_DOWN) {
		if (!ia->enabled || !link_ok || !addr_ok || !socket_ok ||
		    !rtr_id_ok)
			return;

		ia->state = IF_STA_ACTIVE;
		if_start(ia->iface, ia->af);
	} else if (ia->state == IF_STA_ACTIVE) {
		if (ia->enabled && link_ok && addr_ok && socket_ok && rtr_id_ok)
			return;

		ia->state = IF_STA_DOWN;
		if_reset(ia->iface, ia->af);
	}
}

void
if_update(struct iface *iface, int af)
{
	int			 link_ok;

	link_ok = (iface->flags & IFF_UP) &&
	    LINK_STATE_IS_UP(iface->linkstate);

	if (af == AF_INET || af == AF_UNSPEC)
		if_update_af(&iface->ipv4, link_ok);
	if (af == AF_INET6 || af == AF_UNSPEC)
		if_update_af(&iface->ipv6, link_ok);
}

void
if_update_all(int af)
{
	struct iface		*iface;

	LIST_FOREACH(iface, &leconf->iface_list, entry)
		if_update(iface, af);
}

/* timers */
static void
if_hello_timer(int fd, short event, void *arg)
{
	struct iface_af		*ia = arg;

	send_hello(HELLO_LINK, ia, NULL);
	if_start_hello_timer(ia);
}

static void
if_start_hello_timer(struct iface_af *ia)
{
	struct timeval		 tv;

	timerclear(&tv);
	tv.tv_sec = ia->hello_interval;
	if (evtimer_add(&ia->hello_timer, &tv) == -1)
		fatal(__func__);
}

static void
if_stop_hello_timer(struct iface_af *ia)
{
	if (evtimer_pending(&ia->hello_timer, NULL) &&
	    evtimer_del(&ia->hello_timer) == -1)
		fatal(__func__);
}

struct ctl_iface *
if_to_ctl(struct iface_af *ia)
{
	static struct ctl_iface	 ictl;
	struct timeval		 now;
	struct adj		*adj;

	ictl.af = ia->af;
	memcpy(ictl.name, ia->iface->name, sizeof(ictl.name));
	ictl.ifindex = ia->iface->ifindex;
	ictl.state = ia->state;
	ictl.flags = ia->iface->flags;
	ictl.linkstate = ia->iface->linkstate;
	ictl.type = ia->iface->type;
	ictl.if_type = ia->iface->if_type;
	ictl.hello_holdtime = ia->hello_holdtime;
	ictl.hello_interval = ia->hello_interval;

	gettimeofday(&now, NULL);
	if (ia->state != IF_STA_DOWN &&
	    ia->uptime != 0) {
		ictl.uptime = now.tv_sec - ia->uptime;
	} else
		ictl.uptime = 0;

	ictl.adj_cnt = 0;
	LIST_FOREACH(adj, &ia->adj_list, ia_entry)
		ictl.adj_cnt++;

	return (&ictl);
}

/* multicast membership sockopts */
in_addr_t
if_get_ipv4_addr(struct iface *iface)
{
	struct if_addr		*if_addr;

	LIST_FOREACH(if_addr, &iface->addr_list, entry)
		if (if_addr->af == AF_INET)
			return (if_addr->addr.v4.s_addr);

	return (INADDR_ANY);
}

static int
if_join_ipv4_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreq		 mreq;

	log_debug("%s: interface %s addr %s", __func__, iface->name,
	    inet_ntoa(*addr));

	mreq.imr_multiaddr = *addr;
	mreq.imr_interface.s_addr = if_get_ipv4_addr(iface);

	if (setsockopt(global.ipv4.ldp_disc_socket, IPPROTO_IP,
	    IP_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) == -1) {
		log_warn("%s: error IP_ADD_MEMBERSHIP, interface %s address %s",
		     __func__, iface->name, inet_ntoa(*addr));
		return (-1);
	}
	return (0);
}

static int
if_leave_ipv4_group(struct iface *iface, struct in_addr *addr)
{
	struct ip_mreq		 mreq;

	log_debug("%s: interface %s addr %s", __func__, iface->name,
	    inet_ntoa(*addr));

	mreq.imr_multiaddr = *addr;
	mreq.imr_interface.s_addr = if_get_ipv4_addr(iface);

	if (setsockopt(global.ipv4.ldp_disc_socket, IPPROTO_IP,
	    IP_DROP_MEMBERSHIP, (void *)&mreq, sizeof(mreq)) == -1) {
		log_warn("%s: error IP_DROP_MEMBERSHIP, interface %s "
		    "address %s", __func__, iface->name, inet_ntoa(*addr));
		return (-1);
	}

	return (0);
}

static int
if_join_ipv6_group(struct iface *iface, struct in6_addr *addr)
{
	struct ipv6_mreq	 mreq;

	log_debug("%s: interface %s addr %s", __func__, iface->name,
	    log_in6addr(addr));

	mreq.ipv6mr_multiaddr = *addr;
	mreq.ipv6mr_interface = iface->ifindex;

	if (setsockopt(global.ipv6.ldp_disc_socket, IPPROTO_IPV6,
	    IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) == -1) {
		log_warn("%s: error IPV6_JOIN_GROUP, interface %s address %s",
		    __func__, iface->name, log_in6addr(addr));
		return (-1);
	}

	return (0);
}

static int
if_leave_ipv6_group(struct iface *iface, struct in6_addr *addr)
{
	struct ipv6_mreq	 mreq;

	log_debug("%s: interface %s addr %s", __func__, iface->name,
	    log_in6addr(addr));

	mreq.ipv6mr_multiaddr = *addr;
	mreq.ipv6mr_interface = iface->ifindex;

	if (setsockopt(global.ipv6.ldp_disc_socket, IPPROTO_IPV6,
	    IPV6_LEAVE_GROUP, (void *)&mreq, sizeof(mreq)) == -1) {
		log_warn("%s: error IPV6_LEAVE_GROUP, interface %s address %s",
		    __func__, iface->name, log_in6addr(addr));
		return (-1);
	}

	return (0);
}
