/*	$OpenBSD: rde_rib.c,v 1.6 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <event.h>

#include "ripd.h"
#include "rip.h"
#include "log.h"
#include "rde.h"

extern struct ripd_conf		*rdeconf;
RB_HEAD(rt_tree, rt_node)	 rt;
RB_PROTOTYPE(rt_tree, rt_node, entry, rt_compare)
RB_GENERATE(rt_tree, rt_node, entry, rt_compare)

void	 route_action_timeout(int, short, void *);
void	 route_action_garbage(int, short, void *);

/* timers */
int
route_start_timeout(struct rt_node *rn)
{
	struct timeval	 tv;

	timerclear(&tv);
	tv.tv_sec = ROUTE_TIMEOUT;

	return (evtimer_add(&rn->timeout_timer, &tv));
}

void
route_start_garbage(struct rt_node *rn)
{
	struct timeval	 tv;

	timerclear(&tv);
	tv.tv_sec = ROUTE_GARBAGE;

	if (evtimer_pending(&rn->timeout_timer, NULL)) {
		if (evtimer_del(&rn->timeout_timer) == -1)
			fatal("route_start_garbage");
		evtimer_add(&rn->garbage_timer, &tv);
	}
}

void
route_action_timeout(int fd, short event, void *arg)
{
	struct rt_node	*r = arg;
	struct timeval	 tv;

	timerclear(&tv);
	r->metric = INFINITY;
	tv.tv_sec = ROUTE_GARBAGE;

	if (evtimer_add(&r->garbage_timer, &tv) == -1)
		fatal("route_action_timeout");

	rde_send_change_kroute(r);
}

void
route_action_garbage(int fd, short event, void *arg)
{
	struct rt_node	*r = arg;

	rde_send_delete_kroute(r);
	rt_remove(r);
}

void
route_reset_timers(struct rt_node *r)
{
	struct timeval	 tv;

	timerclear(&tv);
	tv.tv_sec = ROUTE_TIMEOUT;
	evtimer_del(&r->timeout_timer);
	evtimer_del(&r->garbage_timer);

	evtimer_add(&r->timeout_timer, &tv);
}

/* route table */
void
rt_init(void)
{
	RB_INIT(&rt);
}

int
rt_compare(struct rt_node *a, struct rt_node *b)
{
	if (ntohl(a->prefix.s_addr) < ntohl(b->prefix.s_addr))
		return (-1);
	if (ntohl(a->prefix.s_addr) > ntohl(b->prefix.s_addr))
		return (1);
	if (ntohl(a->netmask.s_addr) < ntohl(b->netmask.s_addr))
		return (-1);
	if (ntohl(a->netmask.s_addr) > ntohl(b->netmask.s_addr))
		return (1);

	return (0);
}

struct rt_node *
rt_find(in_addr_t prefix, in_addr_t netmask)
{
	struct rt_node	 s;

	s.prefix.s_addr = prefix;
	s.netmask.s_addr = netmask;

	return (RB_FIND(rt_tree, &rt, &s));
}

struct rt_node *
rt_new_kr(struct kroute *kr)
{
	struct rt_node	*rn;

	if ((rn = calloc(1, sizeof(*rn))) == NULL)
		fatal("rt_new_kr");

	evtimer_set(&rn->timeout_timer, route_action_timeout, rn);
	evtimer_set(&rn->garbage_timer, route_action_garbage, rn);

	rn->prefix.s_addr = kr->prefix.s_addr;
	rn->netmask.s_addr = kr->netmask.s_addr;
	rn->nexthop.s_addr = kr->nexthop.s_addr;
	rn->metric = kr->metric;
	rn->ifindex = kr->ifindex;
	rn->flags = F_KERNEL;

	return (rn);
}

struct rt_node *
rt_new_rr(struct rip_route *e, u_int8_t metric)
{
	struct rt_node	*rn;

	if ((rn = calloc(1, sizeof(*rn))) == NULL)
		fatal("rt_new_rr");

	evtimer_set(&rn->timeout_timer, route_action_timeout, rn);
	evtimer_set(&rn->garbage_timer, route_action_garbage, rn);

	rn->prefix.s_addr = e->address.s_addr;
	rn->netmask.s_addr = e->mask.s_addr;
	rn->nexthop.s_addr = e->nexthop.s_addr;
	rn->metric = metric;
	rn->ifindex = e->ifindex;
	rn->flags = F_RIPD_INSERTED;

	return (rn);
}

int
rt_insert(struct rt_node *r)
{
	if (RB_INSERT(rt_tree, &rt, r) != NULL) {
		log_warnx("rt_insert failed for %s/%u",
		    inet_ntoa(r->prefix), mask2prefixlen(r->netmask.s_addr));
		free(r);
		return (-1);
	}

	return (0);
}

int
rt_remove(struct rt_node *r)
{
	if (RB_REMOVE(rt_tree, &rt, r) == NULL) {
		log_warnx("rt_remove failed for %s/%u",
		    inet_ntoa(r->prefix), mask2prefixlen(r->netmask.s_addr));
		return (-1);
	}

	free(r);
	return (0);
}

void
rt_snap(u_int32_t peerid)
{
	struct rt_node		*r;
	struct rip_route	 rr;

	bzero(&rr, sizeof(rr));

	RB_FOREACH(r, rt_tree, &rt) {
		rr.address = r->prefix;
		rr.mask = r->netmask;
		rr.nexthop = r->nexthop;
		rr.metric = r->metric;
		rr.ifindex = r->ifindex;

		rde_imsg_compose_ripe(IMSG_RESPONSE_ADD, peerid, 0, &rr,
		    sizeof(rr));
	}
}

void
rt_dump(pid_t pid)
{
	struct rt_node		*r;
	static struct ctl_rt	 rtctl;

	RB_FOREACH(r, rt_tree, &rt) {
		rtctl.prefix.s_addr = r->prefix.s_addr;
		rtctl.netmask.s_addr = r->netmask.s_addr;
		rtctl.nexthop.s_addr = r->nexthop.s_addr;
		rtctl.metric = r->metric;
		rtctl.flags = r->flags;

		rde_imsg_compose_ripe(IMSG_CTL_SHOW_RIB, 0, pid, &rtctl,
		    sizeof(rtctl));
	}
}

void
rt_complete(struct rip_route *rr)
{
	struct rt_node	*rn;

	if ((rn = rt_find(rr->address.s_addr, rr->mask.s_addr)) == NULL)
		rr->metric = INFINITY;
	else
		rr->metric = rn->metric;
}

void
rt_clear(void)
{
	struct rt_node	*r;

	while ((r = RB_MIN(rt_tree, &rt)) != NULL)
		rt_remove(r);
}
