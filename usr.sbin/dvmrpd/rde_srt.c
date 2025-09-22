/*	$OpenBSD: rde_srt.c,v 1.27 2018/09/21 01:33:55 jsg Exp $ */

/*
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/socket.h>
#include <sys/tree.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "log.h"
#include "dvmrpe.h"
#include "rde.h"

/* source route tree */

void	 rt_invalidate(void);
void	 rt_expire_timer(int, short, void *);
int	 rt_start_expire_timer(struct rt_node *);
void	 rt_holddown_timer(int, short, void *);
void	 rt_start_holddown_timer(struct rt_node *);

void	 srt_set_upstream(struct rt_node *, u_int32_t);

/* Designated forwarder */
void	 srt_set_forwarder_self(struct rt_node *, struct iface *);
void	 srt_update_ds_forwarders(struct rt_node *, struct iface *,
	    u_int32_t);
void	 srt_current_forwarder(struct rt_node *, struct iface *,
	    u_int32_t, u_int32_t);

/* Downstream neighbors */
void		 srt_add_ds(struct rt_node *, u_int32_t, u_int32_t);
void		 srt_delete_ds(struct rt_node *, struct ds_nbr *,
		    struct iface *);

/* Flash updates */
void		 flash_update(struct rt_node *);
void		 flash_update_ds(struct rt_node *);

RB_HEAD(rt_tree, rt_node)	 rt;
RB_PROTOTYPE(rt_tree, rt_node, entry, rt_compare)
RB_GENERATE(rt_tree, rt_node, entry, rt_compare)

extern struct dvmrpd_conf	*rdeconf;

/* timers */
void
rt_expire_timer(int fd, short event, void *arg)
{
	struct rt_node	*rn = arg;
	struct timeval	 tv;

	log_debug("rt_expire_timer: route %s/%d", inet_ntoa(rn->prefix),
	    rn->prefixlen);

	timerclear(&tv);
	rn->old_cost = rn->cost;
	rn->cost = INFINITY_METRIC;
	tv.tv_sec = ROUTE_HOLD_DOWN;

	if (evtimer_add(&rn->holddown_timer, &tv) == -1)
		fatal("rt_expire_timer");
}

int
rt_start_expire_timer(struct rt_node *rn)
{
	struct timeval	tv;

	rn->old_cost = 0;

	if (evtimer_pending(&rn->holddown_timer, NULL))
		if (evtimer_del(&rn->holddown_timer) == -1)
			fatal("rt_start_expire_timer");

	timerclear(&tv);
	tv.tv_sec = ROUTE_EXPIRATION_TIME;
	return (evtimer_add(&rn->expiration_timer, &tv));
}

void
rt_holddown_timer(int fd, short event, void *arg)
{
	struct rt_node	*rn = arg;

	log_debug("rt_holddown_timer: route %s/%d", inet_ntoa(rn->prefix),
	    rn->prefixlen);

	rt_remove(rn);
}

void
rt_start_holddown_timer(struct rt_node *rn)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = ROUTE_HOLD_DOWN;
	if (evtimer_pending(&rn->expiration_timer, NULL)) {
		if (evtimer_del(&rn->expiration_timer) == -1)
			fatal("rt_start_holddown_timer");
		evtimer_add(&rn->holddown_timer, &tv);
	}
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
	/*
	 * sort route entries based on prefixlen since generating route
	 * reports rely on that.
	 */
	if (a->prefixlen < b->prefixlen)
		return (-1);
	if (a->prefixlen > b->prefixlen)
		return (1);
	if (ntohl(a->prefix.s_addr) < ntohl(b->prefix.s_addr))
		return (-1);
	if (ntohl(a->prefix.s_addr) > ntohl(b->prefix.s_addr))
		return (1);
	return (0);
}

struct rt_node *
rt_find(in_addr_t prefix, u_int8_t prefixlen)
{
	struct rt_node	s;

	s.prefix.s_addr = prefix;
	s.prefixlen = prefixlen;

	return (RB_FIND(rt_tree, &rt, &s));
}

struct rt_node *
rr_new_rt(struct route_report *rr, u_int32_t adj_metric, int connected)
{
	struct timespec	 now;
	struct rt_node  *rn;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &now);

	if ((rn = calloc(1, sizeof(*rn))) == NULL)
		fatal("rr_new_rt");

	rn->prefix.s_addr = rr->net.s_addr;
	rn->prefixlen = mask2prefixlen(rr->mask.s_addr);
	rn->nexthop.s_addr = rr->nexthop.s_addr;
	rn->cost = adj_metric;
	rn->ifindex = rr->ifindex;

	for (i = 0; i < MAXVIFS; i++) {
		rn->ttls[i] = 0;
		rn->ds_cnt[i] = 0;
		rn->adv_rtr[i].addr.s_addr = 0;
		rn->adv_rtr[i].metric = 0;
	}

	LIST_INIT(&rn->ds_list);

	rn->flags = F_DVMRPD_INSERTED;
	rn->connected = connected;
	rn->uptime = now.tv_sec;

	evtimer_set(&rn->expiration_timer, rt_expire_timer, rn);
	evtimer_set(&rn->holddown_timer, rt_holddown_timer, rn);

	return (rn);
}

int
rt_insert(struct rt_node *r)
{
	log_debug("rt_insert: inserting route %s/%d", inet_ntoa(r->prefix),
	    r->prefixlen);

	if (RB_INSERT(rt_tree, &rt, r) != NULL) {
		log_warnx("rt_insert failed for %s/%u", inet_ntoa(r->prefix),
		    r->prefixlen);
		free(r);
		return (-1);
	}

	return (0);
}

int
rt_remove(struct rt_node *r)
{
	struct ds_nbr	*ds_nbr;

	if (RB_REMOVE(rt_tree, &rt, r) == NULL) {
		log_warnx("rt_remove failed for %s/%u",
		    inet_ntoa(r->prefix), r->prefixlen);
		return (-1);
	}

	while ((ds_nbr = LIST_FIRST(&r->ds_list)) != NULL) {
		LIST_REMOVE(ds_nbr, entry);
		free(ds_nbr);
	}

	free(r);
	return (0);
}

void
rt_invalidate(void)
{
	struct rt_node	*r, *nr;

	for (r = RB_MIN(rt_tree, &rt); r != NULL; r = nr) {
		nr = RB_NEXT(rt_tree, &rt, r);
		if (r->invalid)
			rt_remove(r);
		else
			r->invalid = 1;
	}
}

void
rt_clear(void)
{
	struct rt_node	*r;

	while ((r = RB_MIN(rt_tree, &rt)) != NULL)
		rt_remove(r);
}

void
rt_snap(u_int32_t peerid)
{
	struct rt_node		*r;
	struct route_report	 rr;

	RB_FOREACH(r, rt_tree, &rt) {
		if (r->invalid)
			continue;

		rr.net = r->prefix;
		rr.mask.s_addr = ntohl(prefixlen2mask(r->prefixlen));
		rr.metric = r->cost;
		rr.ifindex = r->ifindex;
		rde_imsg_compose_dvmrpe(IMSG_FULL_ROUTE_REPORT, peerid, 0, &rr,
		    sizeof(rr));
	}
}

void
rt_dump(pid_t pid)
{
	static struct ctl_rt	 rtctl;
	struct timespec		 now;
	struct timeval		 tv, now2, res;
	struct rt_node		*r;

	clock_gettime(CLOCK_MONOTONIC, &now);

	RB_FOREACH(r, rt_tree, &rt) {
		if (r->invalid)
			continue;

		rtctl.prefix.s_addr = r->prefix.s_addr;
		rtctl.nexthop.s_addr = r->nexthop.s_addr;
		rtctl.cost = r->cost;
		rtctl.flags = r->flags;
		rtctl.prefixlen = r->prefixlen;
		rtctl.uptime = now.tv_sec - r->uptime;

		gettimeofday(&now2, NULL);
		if (evtimer_pending(&r->expiration_timer, &tv)) {
			timersub(&tv, &now2, &res);
			rtctl.expire = res.tv_sec;
		} else
			rtctl.expire = -1;

		rde_imsg_compose_dvmrpe(IMSG_CTL_SHOW_RIB, 0, pid, &rtctl,
		    sizeof(rtctl));
	}
}

void
rt_update(struct rt_node *rn)
{
	if (!rn->connected)
		rt_start_expire_timer(rn);
}

struct rt_node *
rt_match_origin(in_addr_t src)
{
	struct rt_node	*r;

	RB_FOREACH(r, rt_tree, &rt) {
		if (r->prefix.s_addr == (src &
		    htonl(prefixlen2mask(r->prefixlen))))
			return (r);
	}

	return (NULL);
}

int
srt_check_route(struct route_report *rr, int connected)
{
	struct rt_node		*rn;
	struct iface		*iface;
	struct ds_nbr		*ds_nbr;
	u_int32_t		 adj_metric;
	u_int32_t		 nbr_ip, nbr_report, ifindex;

	if ((iface = if_find_index(rr->ifindex)) == NULL)
		return (-1);

	ifindex = iface->ifindex;

	/* Interpret special case 0.0.0.0/8 as 0.0.0.0/0 */
	if (rr->net.s_addr == 0)
		rr->mask.s_addr = 0;

	if (connected)
		adj_metric = rr->metric;
	else
		adj_metric = rr->metric + iface->metric;

	if (adj_metric > INFINITY_METRIC)
		adj_metric = INFINITY_METRIC;

	/* If the route is new and the Adjusted Metric is less than infinity,
	   the route should be added. */
	rn = rt_find(rr->net.s_addr, mask2prefixlen(rr->mask.s_addr));
	if (rn == NULL) {
		if (adj_metric < INFINITY_METRIC) {
			rn = rr_new_rt(rr, adj_metric, connected);
			rt_insert(rn);
		}
		return (0);
	}

	/* If the route is connected accept only downstream neighbors reports */
	if (rn->connected && rr->metric <= INFINITY_METRIC)
		return (0);

	nbr_ip = rn->nexthop.s_addr;
	nbr_report = rr->nexthop.s_addr;

	if (rr->metric < INFINITY_METRIC) {
		/* If it is our current nexthop it cannot be a
		 * downstream router */
		if (nbr_ip != nbr_report)
			if ((ds_nbr = srt_find_ds(rn, nbr_report)))
				srt_delete_ds(rn, ds_nbr, iface);

		if (adj_metric > rn->cost) {
			if (nbr_ip == nbr_report) {
				rn->cost = adj_metric;
				flash_update_ds(rn);
			}
		} else if (adj_metric < rn->cost) {
			rn->cost = adj_metric;
			if (nbr_ip != nbr_report) {
				rn->nexthop.s_addr = nbr_report;
				srt_set_upstream(rn, ifindex);
				flash_update(rn);
			}
			/* We have a new best route to source, update the
			 * designated forwarders on downstream interfaces to
			 * reflect the new metric */
			srt_update_ds_forwarders(rn, iface, nbr_report);
		} else {
			if (nbr_report < nbr_ip) {
				rn->nexthop.s_addr = nbr_report;
				srt_set_upstream(rn, ifindex);
				flash_update(rn);
			} else if (nbr_report == nbr_ip &&
			    adj_metric == rn->old_cost) {
				rt_update(rn);
				flash_update_ds(rn);
			}
		}
		/* Update forwarder of current interface if necessary and
		 * refresh the route */
		srt_current_forwarder(rn, iface, rr->metric, nbr_report);
		rt_update(rn);
	} else if (rr->metric == INFINITY_METRIC) {
		if (nbr_report == rn->adv_rtr[ifindex].addr.s_addr)
			srt_set_forwarder_self(rn, iface);
infinity:
		if (nbr_ip == nbr_report) {
			if (rn->cost < INFINITY_METRIC)
				rt_start_holddown_timer(rn);
		} else
			if ((ds_nbr = srt_find_ds(rn, nbr_report)))
				srt_delete_ds(rn, ds_nbr, iface);
	} else if (INFINITY_METRIC < rr->metric &&
	    rr->metric < 2 * INFINITY_METRIC) {
		/* Neighbor is reporting his dependency for this source */
		if (nbr_report == rn->adv_rtr[ifindex].addr.s_addr)
			srt_set_forwarder_self(rn, iface);

		if (rn->ifindex == ifindex)
			goto infinity;
		else
			if (srt_find_ds(rn, nbr_report) == NULL)
				srt_add_ds(rn, nbr_report, ifindex);
	}

	return (0);
}

void
srt_current_forwarder(struct rt_node *rn, struct iface *iface,
    u_int32_t metric, u_int32_t nbr_report)
{
	struct adv_rtr *adv = &rn->adv_rtr[iface->ifindex];

	if (nbr_report == adv->addr.s_addr) {
		if (metric > rn->cost || (metric == rn->cost &&
		    iface->addr.s_addr < nbr_report))
			srt_set_forwarder_self(rn, iface);

	} else {
		if (metric < adv->metric ||
		    (metric == adv->metric && nbr_report < adv->addr.s_addr))
			if (adv->addr.s_addr == iface->addr.s_addr)
				rn->ttls[iface->ifindex] = 0;

		adv->addr.s_addr = nbr_report;
		adv->metric = metric;

		mfc_update_source(rn);
	}

}

void
srt_update_ds_forwarders(struct rt_node *rn, struct iface *iface,
    u_int32_t nbr_report)
{
	struct iface	*ifa;
	int		 i;

	for (i = 0; i < MAXVIFS; i++) {
		if (rn->adv_rtr[i].addr.s_addr &&
		    (rn->cost < rn->adv_rtr[i].metric ||
		    (rn->cost == rn->adv_rtr[i].metric &&
		    iface->addr.s_addr < nbr_report))) {
			ifa = if_find_index(i);
			srt_set_forwarder_self(rn, ifa);
		}
	}
}

void
srt_set_forwarder_self(struct rt_node *rn, struct iface *iface)
{
	rn->adv_rtr[iface->ifindex].addr.s_addr = iface->addr.s_addr;
	rn->adv_rtr[iface->ifindex].metric = rn->cost;
	rn->ttls[iface->ifindex] = 1;

	mfc_update_source(rn);
}

void
srt_set_upstream(struct rt_node *rn, u_int32_t ifindex)
{
	if (rn->ifindex != ifindex) {
		rn->ttls[rn->ifindex] = 1;
		rn->ifindex = ifindex;
	}

	mfc_update_source(rn);
}

void
srt_add_ds(struct rt_node *rn, u_int32_t nbr_report, u_int32_t ifindex)
{
	struct ds_nbr	*ds_nbr;

	log_debug("srt_add_ds: adding downstream router for source %s/%d",
	    inet_ntoa(rn->prefix), rn->prefixlen);

	if ((ds_nbr = malloc(sizeof(struct ds_nbr))) == NULL)
		fatal("srt_add_ds");

	ds_nbr->addr.s_addr = nbr_report;

	LIST_INSERT_HEAD(&rn->ds_list, ds_nbr, entry);
	rn->ds_cnt[ifindex]++;
	rn->ttls[ifindex] = 1;

	mfc_update_source(rn);
}

struct ds_nbr *
srt_find_ds(struct rt_node *rn, u_int32_t nbr_report)
{
	struct ds_nbr	*ds_nbr;

	LIST_FOREACH(ds_nbr, &rn->ds_list, entry)
		if (ds_nbr->addr.s_addr == nbr_report)
			return (ds_nbr);

	return (NULL);
}

void
srt_delete_ds(struct rt_node *rn, struct ds_nbr *ds_nbr, struct iface *iface)
{
	log_debug("srt_delete_ds: deleting downstream router for source %s/%d",
	    inet_ntoa(rn->prefix), rn->prefixlen);

	LIST_REMOVE(ds_nbr, entry);
	free(ds_nbr);
	rn->ds_cnt[iface->ifindex]--;

	srt_check_downstream_ifaces(rn, iface);
}

void
srt_check_downstream_ifaces(struct rt_node *rn, struct iface *iface)
{
	/* We are not the designated forwarder for this source on this
	   interface. Keep things as they currently are */
	if (rn->adv_rtr[iface->ifindex].addr.s_addr != iface->addr.s_addr)
		return;

	/* There are still downstream dependent router for this source
	   Keep things as they currently are */
	if (rn->ds_cnt[iface->ifindex])
		return;

	/* There are still group members for this source on this iface
	   Keep things as they currently are */
	if (mfc_check_members(rn, iface))
		return;

	/* Remove interface from the downstream list */
	rn->ttls[iface->ifindex] = 0;
	mfc_update_source(rn);
}

void
srt_expire_nbr(struct in_addr addr, unsigned int ifindex)
{
	struct ds_nbr		*ds;
	struct rt_node		*rn;
	struct iface		*iface;

	iface = if_find_index(ifindex);
	if (iface == NULL)
		fatal("srt_expire_nbr: interface not found");

	RB_FOREACH(rn, rt_tree, &rt) {
		if (rn->adv_rtr[ifindex].addr.s_addr == addr.s_addr) {
			rn->adv_rtr[ifindex].addr.s_addr =
			    iface->addr.s_addr;
			rn->adv_rtr[ifindex].metric = rn->cost;
			/* XXX: delete all routes learned from this nbr */
		} else if (rn->adv_rtr[ifindex].addr.s_addr ==
		    iface->addr.s_addr) {
			ds = srt_find_ds(rn, addr.s_addr);
			if (ds) {
				srt_delete_ds(rn, ds, iface);
				srt_check_downstream_ifaces(rn, iface);
			}
		}
	}
}

void
flash_update(struct rt_node *rn) {
	struct route_report	rr;

	rr.net = rn->prefix;
	rr.mask.s_addr = ntohl(prefixlen2mask(rn->prefixlen));
	rr.metric = rn->cost;
	rr.ifindex = rn->ifindex;
	rde_imsg_compose_dvmrpe(IMSG_FLASH_UPDATE, 0, 0, &rr, sizeof(rr));
}

void
flash_update_ds(struct rt_node *rn) {
	struct route_report	rr;

	rr.net = rn->prefix;
	rr.mask.s_addr = ntohl(prefixlen2mask(rn->prefixlen));
	rr.metric = rn->cost;
	rr.ifindex = rn->ifindex;
	rde_imsg_compose_dvmrpe(IMSG_FLASH_UPDATE_DS, 0, 0, &rr, sizeof(rr));
}
