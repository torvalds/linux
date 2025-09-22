/*	$OpenBSD: rde_dual.c,v 1.31 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <stdlib.h>
#include <string.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "rde.h"
#include "log.h"

static int		 dual_fsm(struct rt_node *, enum dual_event);
static __inline int	 rt_compare(struct rt_node *, struct rt_node *);
static struct rt_node	*rt_find(struct eigrp *, struct rinfo *);
static struct rt_node	*rt_new(struct eigrp *, struct rinfo *);
static struct eigrp_route *route_find(struct rde_nbr *, struct rt_node *);
static struct eigrp_route *route_new(struct rt_node *, struct rde_nbr *,
			    struct rinfo *);
static void		 route_del(struct rt_node *, struct eigrp_route *);
static uint32_t		 safe_sum_uint32(uint32_t, uint32_t);
static uint32_t		 safe_mul_uint32(uint32_t, uint32_t);
static uint32_t		 route_composite_metric(uint8_t *, uint32_t, uint32_t,
			    uint8_t, uint8_t);
static void		 route_update_metrics(struct eigrp *,
			    struct eigrp_route *, struct rinfo *);
static void		 reply_outstanding_add(struct rt_node *,
			    struct rde_nbr *);
static struct reply_node *reply_outstanding_find(struct rt_node *,
			    struct rde_nbr *);
static void		 reply_outstanding_remove(struct reply_node *);
static void		 reply_active_timer(int, short, void *);
static void		 reply_active_start_timer(struct reply_node *);
static void		 reply_active_stop_timer(struct reply_node *);
static void		 reply_sia_timer(int, short, void *);
static void		 reply_sia_start_timer(struct reply_node *);
static void		 reply_sia_stop_timer(struct reply_node *);
static void		 rinfo_fill_infinite(struct rt_node *, enum route_type,
			    struct rinfo *);
static void		 rt_update_fib(struct rt_node *);
static void		 rt_set_successor(struct rt_node *,
			    struct eigrp_route *);
static struct eigrp_route *rt_get_successor_fc(struct rt_node *);
static void		 rde_send_update(struct eigrp_iface *, struct rinfo *);
static void		 rde_send_update_all(struct rt_node *, struct rinfo *);
static void		 rde_send_query(struct eigrp_iface *, struct rinfo *,
			    int);
static void		 rde_send_siaquery(struct rde_nbr *, struct rinfo *);
static void		 rde_send_query_all(struct eigrp *, struct rt_node *,
			    int);
static void		 rde_send_reply(struct rde_nbr *, struct rinfo *, int);
static void		 rde_last_reply(struct rt_node *);
static __inline int	 rde_nbr_compare(struct rde_nbr *, struct rde_nbr *);

RB_GENERATE(rt_tree, rt_node, entry, rt_compare)
RB_GENERATE(rde_nbr_head, rde_nbr, entry, rde_nbr_compare)

struct rde_nbr_head rde_nbrs = RB_INITIALIZER(&rde_nbrs);

/*
 * NOTE: events that don't cause a state transition aren't triggered to avoid
 * too much verbosity and are here mostly for illustration purposes.
 */
static struct {
	int		state;
	enum dual_event	event;
	int		new_state;
} dual_fsm_tbl[] = {
    /* current state		event		resulting state */
/* Passive */
    {DUAL_STA_PASSIVE,		DUAL_EVT_1,	0},
    {DUAL_STA_PASSIVE,		DUAL_EVT_2,	0},
    {DUAL_STA_PASSIVE,		DUAL_EVT_3,	DUAL_STA_ACTIVE3},
    {DUAL_STA_PASSIVE,		DUAL_EVT_4,	DUAL_STA_ACTIVE1},
/* Active Oij=0 */
    {DUAL_STA_ACTIVE0,		DUAL_EVT_5,	DUAL_STA_ACTIVE2},
    {DUAL_STA_ACTIVE0,		DUAL_EVT_11,	DUAL_STA_ACTIVE1},
    {DUAL_STA_ACTIVE0,		DUAL_EVT_14,	DUAL_STA_PASSIVE},
/* Active Oij=1 */
    {DUAL_STA_ACTIVE1,		DUAL_EVT_5,	DUAL_STA_ACTIVE2},
    {DUAL_STA_ACTIVE1,		DUAL_EVT_9,	DUAL_STA_ACTIVE0},
    {DUAL_STA_ACTIVE1,		DUAL_EVT_15,	DUAL_STA_PASSIVE},
/* Active Oij=2 */
    {DUAL_STA_ACTIVE2,		DUAL_EVT_12,	DUAL_STA_ACTIVE3},
    {DUAL_STA_ACTIVE2,		DUAL_EVT_16,	DUAL_STA_PASSIVE},
/* Active Oij=3 */
    {DUAL_STA_ACTIVE3,		DUAL_EVT_10,	DUAL_STA_ACTIVE2},
    {DUAL_STA_ACTIVE3,		DUAL_EVT_13,	DUAL_STA_PASSIVE},
/* Active (all) */
    {DUAL_STA_ACTIVE_ALL,	DUAL_EVT_6,	0},
    {DUAL_STA_ACTIVE_ALL,	DUAL_EVT_7,	0},
    {DUAL_STA_ACTIVE_ALL,	DUAL_EVT_8,	0},
/* sentinel */
    {-1,			0,		0},
};

static const char * const dual_event_names[] = {
	"DUAL_EVT_1",
	"DUAL_EVT_2",
	"DUAL_EVT_3",
	"DUAL_EVT_4",
	"DUAL_EVT_5",
	"DUAL_EVT_6",
	"DUAL_EVT_7",
	"DUAL_EVT_8",
	"DUAL_EVT_9",
	"DUAL_EVT_10",
	"DUAL_EVT_11",
	"DUAL_EVT_12",
	"DUAL_EVT_13",
	"DUAL_EVT_14",
	"DUAL_EVT_15",
	"DUAL_EVT_16"
};

static int
dual_fsm(struct rt_node *rn, enum dual_event event)
{
	int		old_state;
	int		new_state = 0;
	int		i;

	old_state = rn->state;
	for (i = 0; dual_fsm_tbl[i].state != -1; i++)
		if ((dual_fsm_tbl[i].state & old_state) &&
		    (dual_fsm_tbl[i].event == event)) {
			new_state = dual_fsm_tbl[i].new_state;
			break;
		}

	if (dual_fsm_tbl[i].state == -1) {
		/* event outside of the defined fsm, ignore it. */
		log_warnx("%s: route %s, event %s not expected in state %s",
		    __func__, log_prefix(rn), dual_event_names[event],
		    dual_state_name(old_state));
		return (0);
	}

	if (new_state != 0)
		rn->state = new_state;

	if (old_state != rn->state) {
		log_debug("%s: event %s changing state for prefix %s "
		    "from %s to %s", __func__, dual_event_names[event],
		    log_prefix(rn), dual_state_name(old_state),
		    dual_state_name(rn->state));

		if (old_state == DUAL_STA_PASSIVE ||
		    new_state == DUAL_STA_PASSIVE)
			rt_update_fib(rn);
	}

	return (0);
}

static __inline int
rt_compare(struct rt_node *a, struct rt_node *b)
{
	int		 addrcmp;

	addrcmp = eigrp_addrcmp(a->eigrp->af, &a->prefix, &b->prefix);
	if (addrcmp != 0)
		return (addrcmp);

	if (a->prefixlen < b->prefixlen)
		return (-1);
	if (a->prefixlen > b->prefixlen)
		return (1);

	return (0);
}

static struct rt_node *
rt_find(struct eigrp *eigrp, struct rinfo *ri)
{
	struct rt_node	 rn;

	rn.eigrp = eigrp;
	rn.prefix = ri->prefix;
	rn.prefixlen = ri->prefixlen;

	return (RB_FIND(rt_tree, &eigrp->topology, &rn));
}

static struct rt_node *
rt_new(struct eigrp *eigrp, struct rinfo *ri)
{
	struct rt_node	*rn;

	if ((rn = calloc(1, sizeof(*rn))) == NULL)
		fatal("rt_new");

	rn->eigrp = eigrp;
	rn->prefix = ri->prefix;
	rn->prefixlen = ri->prefixlen;
	rn->state = DUAL_STA_PASSIVE;
	TAILQ_INIT(&rn->routes);
	TAILQ_INIT(&rn->rijk);
	rt_set_successor(rn, NULL);

	if (RB_INSERT(rt_tree, &eigrp->topology, rn) != NULL) {
		log_warnx("%s failed for %s", __func__, log_prefix(rn));
		free(rn);
		return (NULL);
	}

	log_debug("%s: prefix %s", __func__, log_prefix(rn));

	return (rn);
}

void
rt_del(struct rt_node *rn)
{
	struct eigrp_route	*route;
	struct reply_node	*reply;

	log_debug("%s: prefix %s", __func__, log_prefix(rn));

	while ((reply = TAILQ_FIRST(&rn->rijk)) != NULL)
		reply_outstanding_remove(reply);
	while ((route = TAILQ_FIRST(&rn->routes)) != NULL)
		route_del(rn, route);
	RB_REMOVE(rt_tree, &rn->eigrp->topology, rn);
	free(rn);
}

static struct eigrp_route *
route_find(struct rde_nbr *nbr, struct rt_node *rn)
{
	struct eigrp_route	*route;

	TAILQ_FOREACH(route, &rn->routes, entry)
		if (route->nbr == nbr)
			return (route);

	return (NULL);
}

static struct eigrp_route *
route_new(struct rt_node *rn, struct rde_nbr *nbr, struct rinfo *ri)
{
	struct eigrp		*eigrp = rn->eigrp;
	struct eigrp_route	*route, *tmp;

	if ((route = calloc(1, sizeof(*route))) == NULL)
		fatal("route_new");

	route->nbr = nbr;
	route->type = ri->type;
	if (eigrp_addrisset(eigrp->af, &ri->nexthop))
		route->nexthop = ri->nexthop;
	else
		route->nexthop = nbr->addr;
	route_update_metrics(eigrp, route, ri);

	/* order by nexthop */
	TAILQ_FOREACH(tmp, &rn->routes, entry)
		if (eigrp_addrcmp(eigrp->af, &tmp->nexthop,
		    &route->nexthop) > 0)
			break;
	if (tmp)
		TAILQ_INSERT_BEFORE(tmp, route, entry);
	else
		TAILQ_INSERT_TAIL(&rn->routes, route, entry);

	log_debug("%s: prefix %s via %s distance (%u/%u)", __func__,
	    log_prefix(rn), log_route_origin(eigrp->af, route->nbr),
	    route->distance, route->rdistance);

	return (route);
}

static void
route_del(struct rt_node *rn, struct eigrp_route *route)
{
	struct eigrp		*eigrp = rn->eigrp;

	log_debug("%s: prefix %s via %s", __func__, log_prefix(rn),
	    log_route_origin(eigrp->af, route->nbr));

	if (route->flags & F_EIGRP_ROUTE_INSTALLED)
		rde_send_delete_kroute(rn, route);

	TAILQ_REMOVE(&rn->routes, route, entry);
	free(route);
}

static uint32_t
safe_sum_uint32(uint32_t a, uint32_t b)
{
	uint64_t	total;

	total = (uint64_t) a + (uint64_t) b;

	if (total >> 32)
		return ((uint32_t )(~0));

	return ((uint32_t) total);
}

static uint32_t
safe_mul_uint32(uint32_t a, uint32_t b)
{
	uint64_t	total;

	total = (uint64_t) a * (uint64_t) b;

	if (total >> 32)
		return ((uint32_t )(~0));

	return ((uint32_t) total);
}

uint32_t
eigrp_composite_delay(uint32_t delay)
{
	/* cheap overflow protection */
	delay = min(delay, (1 << 24) - 1);
	return (delay * EIGRP_SCALING_FACTOR);
}

uint32_t
eigrp_real_delay(uint32_t delay)
{
	return (delay / EIGRP_SCALING_FACTOR);
}

uint32_t
eigrp_composite_bandwidth(uint32_t bandwidth)
{
	/* truncate before applying the scaling factor */
	bandwidth = 10000000 / bandwidth;
	return (EIGRP_SCALING_FACTOR * bandwidth);
}

uint32_t
eigrp_real_bandwidth(uint32_t bandwidth)
{
	/*
	 * apply the scaling factor before the division and only then truncate.
	 * this is to keep consistent with what cisco does.
	 */
	return ((EIGRP_SCALING_FACTOR * (uint32_t)10000000) / bandwidth);
}

static uint32_t
route_composite_metric(uint8_t *kvalues, uint32_t delay, uint32_t bandwidth,
    uint8_t load, uint8_t reliability)
{
	uint64_t	 distance;
	uint32_t	 operand1, operand2, operand3;
	double		 operand4;

	/*
	 * Need to apply the scaling factor before any division to avoid
	 * losing information from truncation.
	 */
	operand1 = safe_mul_uint32(kvalues[0] * EIGRP_SCALING_FACTOR,
	    10000000 / bandwidth);
	operand2 = safe_mul_uint32(kvalues[1] * EIGRP_SCALING_FACTOR,
	    10000000 / bandwidth) / (256 - load);
	operand3 = safe_mul_uint32(kvalues[2] * EIGRP_SCALING_FACTOR, delay);

	distance = (uint64_t) operand1 + (uint64_t) operand2 +
	    (uint64_t) operand3;

	/* if K5 is set to zero, the last term of the formula is not used */
	if (kvalues[4] != 0) {
		operand4 = (double) kvalues[4] / (reliability + kvalues[3]);
		/* no risk of overflow (64 bits), operand4 can be at most 255 */
		distance *= operand4;
	}

	/* overflow protection */
	if (distance >> 32)
		distance = ((uint32_t )(~0));

	return ((uint32_t) distance);
}

static void
route_update_metrics(struct eigrp *eigrp, struct eigrp_route *route,
    struct rinfo *ri)
{
	struct eigrp_iface	*ei = route->nbr->ei;
	uint32_t		 delay, bandwidth;
	int			 mtu;

	route->metric = ri->metric;
	route->emetric = ri->emetric;
	route->flags |= F_EIGRP_ROUTE_M_CHANGED;

	delay = eigrp_real_delay(route->metric.delay);
	bandwidth = eigrp_real_bandwidth(route->metric.bandwidth);

	if (route->nbr->flags & F_RDE_NBR_SELF)
		route->rdistance = 0;
	else {
		route->rdistance = route_composite_metric(eigrp->kvalues,
		    delay, bandwidth, route->metric.load,
		    route->metric.reliability);

		/* update the delay */
		delay = safe_sum_uint32(delay, ei->delay);
		route->metric.delay = eigrp_composite_delay(delay);

		/* update the bandwidth */
		bandwidth = min(bandwidth, ei->bandwidth);
		route->metric.bandwidth = eigrp_composite_bandwidth(bandwidth);

		/* update the mtu */
		mtu = min(metric_decode_mtu(route->metric.mtu), ei->iface->mtu);
		metric_encode_mtu(route->metric.mtu, mtu);

		/* update the hop count */
		if (route->metric.hop_count < UINT8_MAX)
			route->metric.hop_count++;
	}

	route->distance = route_composite_metric(eigrp->kvalues, delay,
	    bandwidth, DEFAULT_LOAD, DEFAULT_RELIABILITY);
}

static void
reply_outstanding_add(struct rt_node *rn, struct rde_nbr *nbr)
{
	struct reply_node	*reply;

	if ((reply = calloc(1, sizeof(*reply))) == NULL)
		fatal("reply_outstanding_add");

	evtimer_set(&reply->ev_active_timeout, reply_active_timer, reply);
	evtimer_set(&reply->ev_sia_timeout, reply_sia_timer, reply);
	reply->siaquery_sent = 0;
	reply->siareply_recv = 0;
	reply->rn = rn;
	reply->nbr = nbr;
	TAILQ_INSERT_TAIL(&rn->rijk, reply, rn_entry);
	TAILQ_INSERT_TAIL(&nbr->rijk, reply, nbr_entry);

	if (rn->eigrp->active_timeout > 0) {
		reply_active_start_timer(reply);
		reply_sia_start_timer(reply);
	}
}

static struct reply_node *
reply_outstanding_find(struct rt_node *rn, struct rde_nbr *nbr)
{
	struct reply_node	*reply;

	TAILQ_FOREACH(reply, &rn->rijk, rn_entry)
		if (reply->nbr == nbr)
			return (reply);

	return (NULL);
}

static void
reply_outstanding_remove(struct reply_node *reply)
{
	reply_active_stop_timer(reply);
	reply_sia_stop_timer(reply);
	TAILQ_REMOVE(&reply->rn->rijk, reply, rn_entry);
	TAILQ_REMOVE(&reply->nbr->rijk, reply, nbr_entry);
	free(reply);
}

static void
reply_active_timer(int fd, short event, void *arg)
{
	struct reply_node	*reply = arg;
	struct rde_nbr		*nbr = reply->nbr;

	log_debug("%s: neighbor %s is stuck in active", __func__,
	    log_addr(nbr->eigrp->af, &nbr->addr));

	rde_nbr_del(reply->nbr, 1);
}

static void
reply_active_start_timer(struct reply_node *reply)
{
	struct eigrp		*eigrp = reply->nbr->eigrp;
	struct timeval		 tv;

	timerclear(&tv);
	tv.tv_sec = eigrp->active_timeout * 60;
	if (evtimer_add(&reply->ev_active_timeout, &tv) == -1)
		fatal("reply_active_start_timer");
}

static void
reply_active_stop_timer(struct reply_node *reply)
{
	if (evtimer_pending(&reply->ev_active_timeout, NULL) &&
	    evtimer_del(&reply->ev_active_timeout) == -1)
		fatal("reply_active_stop_timer");
}

static void
reply_sia_timer(int fd, short event, void *arg)
{
	struct reply_node	*reply = arg;
	struct rde_nbr		*nbr = reply->nbr;
	struct rt_node		*rn = reply->rn;
	struct rinfo		 ri;

	log_debug("%s: nbr %s prefix %s", __func__, log_addr(nbr->eigrp->af,
	    &nbr->addr), log_prefix(rn));

	if (reply->siaquery_sent > 0 && reply->siareply_recv == 0) {
		log_debug("%s: neighbor %s is stuck in active", __func__,
		    log_addr(nbr->eigrp->af, &nbr->addr));
		rde_nbr_del(nbr, 1);
		return;
	}

	/*
	 * draft-savage-eigrp-04 - Section 4.4.1.1:
	 * "Up to three SIA-QUERY packets for a specific destination may
	 * be sent, each at a value of one-half the ACTIVE time, so long
	 * as each are successfully acknowledged and met with an SIA-REPLY".
	 */
	if (reply->siaquery_sent >= 3)
		return;

	reply->siaquery_sent++;
	reply->siareply_recv = 0;

	/* restart sia and active timeouts */
	reply_sia_start_timer(reply);
	reply_active_start_timer(reply);

	/* send an sia-query */
	rinfo_fill_successor(rn, &ri);
	ri.metric.flags |= F_METRIC_ACTIVE;
	rde_send_siaquery(nbr, &ri);
}

static void
reply_sia_start_timer(struct reply_node *reply)
{
	struct eigrp		*eigrp = reply->nbr->eigrp;
	struct timeval		 tv;

	/*
	 * draft-savage-eigrp-04 - Section 4.4.1.1:
	 * "The SIA-QUERY packet SHOULD be sent on a per-destination basis
	 * at one-half of the ACTIVE timeout period."
	 */
	timerclear(&tv);
	tv.tv_sec = (eigrp->active_timeout * 60) / 2;
	if (evtimer_add(&reply->ev_sia_timeout, &tv) == -1)
		fatal("reply_sia_start_timer");
}

static void
reply_sia_stop_timer(struct reply_node *reply)
{
	if (evtimer_pending(&reply->ev_sia_timeout, NULL) &&
	    evtimer_del(&reply->ev_sia_timeout) == -1)
		fatal("reply_sia_stop_timer");
}

void
rinfo_fill_successor(struct rt_node *rn, struct rinfo *ri)
{
	if (rn->successor.nbr == NULL) {
		rinfo_fill_infinite(rn, EIGRP_ROUTE_INTERNAL, ri);
		return;
	}

	memset(ri, 0, sizeof(*ri));
	ri->af = rn->eigrp->af;
	ri->type = rn->successor.type;
	ri->prefix = rn->prefix;
	ri->prefixlen = rn->prefixlen;
	ri->metric = rn->successor.metric;
	if (ri->type == EIGRP_ROUTE_EXTERNAL)
		ri->emetric = rn->successor.emetric;
}

static void
rinfo_fill_infinite(struct rt_node *rn, enum route_type type, struct rinfo *ri)
{
	memset(ri, 0, sizeof(*ri));
	ri->af = rn->eigrp->af;
	ri->type = type;
	ri->prefix = rn->prefix;
	ri->prefixlen = rn->prefixlen;
	ri->metric.delay = EIGRP_INFINITE_METRIC;
}

static void
rt_update_fib(struct rt_node *rn)
{
	struct eigrp		*eigrp = rn->eigrp;
	uint8_t			 maximum_paths = eigrp->maximum_paths;
	uint8_t			 variance = eigrp->variance;
	int			 installed = 0;
	struct eigrp_route	*route;

	if (rn->state == DUAL_STA_PASSIVE) {
		/* no multipath for attached networks. */
		if (rn->successor.nbr &&
		    (rn->successor.nbr->flags & F_RDE_NBR_LOCAL))
			return;

		TAILQ_FOREACH(route, &rn->routes, entry) {
			/* skip redistributed routes */
			if (route->nbr->flags & F_RDE_NBR_REDIST)
				continue;

			/*
			 * Only feasible successors and the successor itself
			 * are eligible to be installed.
			 */
			if (route->rdistance >= rn->successor.fdistance)
				goto uninstall;

			if (route->distance >
			    (rn->successor.fdistance * variance))
				goto uninstall;

			if (installed >= maximum_paths)
				goto uninstall;

			installed++;

			if ((route->flags & F_EIGRP_ROUTE_INSTALLED) &&
			    !(route->flags & F_EIGRP_ROUTE_M_CHANGED))
				continue;

			rde_send_change_kroute(rn, route);
			continue;

uninstall:
			if (route->flags & F_EIGRP_ROUTE_INSTALLED)
				rde_send_delete_kroute(rn, route);
		}
	} else {
		TAILQ_FOREACH(route, &rn->routes, entry)
			if (route->flags & F_EIGRP_ROUTE_INSTALLED)
				rde_send_delete_kroute(rn, route);
	}
}

static void
rt_set_successor(struct rt_node *rn, struct eigrp_route *successor)
{
	struct eigrp		*eigrp = rn->eigrp;
	struct eigrp_iface	*ei;
	struct summary_addr	*summary;

	if (successor == NULL) {
		rn->successor.nbr = NULL;
		rn->successor.type = 0;
		rn->successor.fdistance = EIGRP_INFINITE_METRIC;
		rn->successor.rdistance = EIGRP_INFINITE_METRIC;
		memset(&rn->successor.metric, 0,
		    sizeof(rn->successor.metric));
		rn->successor.metric.delay = EIGRP_INFINITE_METRIC;
		memset(&rn->successor.emetric, 0,
		    sizeof(rn->successor.emetric));
	} else {
		rn->successor.nbr = successor->nbr;
		rn->successor.type = successor->type;
		rn->successor.fdistance = successor->distance;
		rn->successor.rdistance = successor->rdistance;
		rn->successor.metric = successor->metric;
		rn->successor.emetric = successor->emetric;
	}

	TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry) {
		summary = rde_summary_check(ei, &rn->prefix, rn->prefixlen);
		if (summary)
			rt_summary_set(eigrp, summary, &rn->successor.metric);
	}
}

static struct eigrp_route *
rt_get_successor_fc(struct rt_node *rn)
{
	struct eigrp_route	*route, *successor = NULL;
	uint32_t		 distance = EIGRP_INFINITE_METRIC;
	int			 external_only = 1;

	TAILQ_FOREACH(route, &rn->routes, entry)
		if (route->type == EIGRP_ROUTE_INTERNAL) {
			/*
			 * connected routes should always be preferred over
			 * received routes independent of the metric.
			 */
			if (route->nbr->flags & F_RDE_NBR_LOCAL)
				return (route);

			external_only = 0;
		}

	TAILQ_FOREACH(route, &rn->routes, entry) {
		/*
		 * draft-savage-eigrp-04 - Section 5.4.7:
		 * "Internal routes MUST be preferred over external routes
		 * independent of the metric."
		 */
		if (route->type == EIGRP_ROUTE_EXTERNAL && !external_only)
			continue;

		/* pick the best route that meets the feasibility condition */
		if (route->rdistance < rn->successor.fdistance &&
		    route->distance < distance) {
			distance = route->distance;
			successor = route;
		}
	}

	return (successor);
}

struct summary_addr *
rde_summary_check(struct eigrp_iface *ei, union eigrpd_addr *prefix,
    uint8_t prefixlen)
{
	struct summary_addr	*summary;

	TAILQ_FOREACH(summary, &ei->summary_list, entry) {
		/* do not filter the summary itself */
		if (summary->prefixlen == prefixlen &&
		    !eigrp_addrcmp(ei->eigrp->af, prefix, &summary->prefix))
			return (NULL);

		if (summary->prefixlen <= prefixlen &&
		    !eigrp_prefixcmp(ei->eigrp->af, prefix, &summary->prefix,
		    summary->prefixlen))
			return (summary);
	}

	return (NULL);
}

static void
rde_send_update(struct eigrp_iface *ei, struct rinfo *ri)
{
	if (ri->metric.hop_count >= ei->eigrp->maximum_hops ||
	    rde_summary_check(ei, &ri->prefix, ri->prefixlen))
		ri->metric.delay = EIGRP_INFINITE_METRIC;

	rde_imsg_compose_eigrpe(IMSG_SEND_MUPDATE, ei->ifaceid, 0,
	    ri, sizeof(*ri));
	rde_imsg_compose_eigrpe(IMSG_SEND_MUPDATE_END, ei->ifaceid, 0,
	    NULL, 0);
}

static void
rde_send_update_all(struct rt_node *rn, struct rinfo *ri)
{
	struct eigrp		*eigrp = rn->eigrp;
	struct eigrp_iface	*ei;

	TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry) {
		/* respect split-horizon configuration */
		if (rn->successor.nbr && rn->successor.nbr->ei == ei &&
		    ei->splithorizon)
			continue;
		rde_send_update(ei, ri);
	}
}

static void
rde_send_query(struct eigrp_iface *ei, struct rinfo *ri, int push)
{
	rde_imsg_compose_eigrpe(IMSG_SEND_MQUERY, ei->ifaceid, 0,
	    ri, sizeof(*ri));
	if (push)
		rde_imsg_compose_eigrpe(IMSG_SEND_MQUERY_END, ei->ifaceid,
		    0, NULL, 0);
}

static void
rde_send_siaquery(struct rde_nbr *nbr, struct rinfo *ri)
{
	rde_imsg_compose_eigrpe(IMSG_SEND_QUERY, nbr->peerid, 0,
	    ri, sizeof(*ri));
	rde_imsg_compose_eigrpe(IMSG_SEND_SIAQUERY_END, nbr->peerid, 0,
	    NULL, 0);
}

static void
rde_send_query_all(struct eigrp *eigrp, struct rt_node *rn, int push)
{
	struct eigrp_iface	*ei;
	struct rde_nbr		*nbr;
	struct rinfo		 ri;

	rinfo_fill_successor(rn, &ri);
	ri.metric.flags |= F_METRIC_ACTIVE;

	TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry) {
		/* respect split-horizon configuration */
		if (rn->successor.nbr && rn->successor.nbr->ei == ei &&
		    ei->splithorizon)
			continue;

		rde_send_query(ei, &ri, push);
	}

	RB_FOREACH(nbr, rde_nbr_head, &rde_nbrs)
		if (nbr->ei->eigrp == eigrp && !(nbr->flags & F_RDE_NBR_SELF)) {
			/* respect split-horizon configuration */
			if (rn->successor.nbr &&
			    rn->successor.nbr->ei == nbr->ei &&
			    nbr->ei->splithorizon)
				continue;

			reply_outstanding_add(rn, nbr);
		}
}

void
rde_flush_queries(void)
{
	struct eigrp		*eigrp;
	struct eigrp_iface	*ei;

	TAILQ_FOREACH(eigrp, &rdeconf->instances, entry)
		TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry)
			rde_imsg_compose_eigrpe(IMSG_SEND_MQUERY_END,
			    ei->ifaceid, 0, NULL, 0);
}

static void
rde_send_reply(struct rde_nbr *nbr, struct rinfo *ri, int siareply)
{
	int	 type;

	if (ri->metric.hop_count >= nbr->eigrp->maximum_hops ||
	    rde_summary_check(nbr->ei, &ri->prefix, ri->prefixlen))
		ri->metric.delay = EIGRP_INFINITE_METRIC;

	if (!siareply)
		type = IMSG_SEND_REPLY_END;
	else
		type = IMSG_SEND_SIAREPLY_END;

	rde_imsg_compose_eigrpe(IMSG_SEND_REPLY, nbr->peerid, 0,
	    ri, sizeof(*ri));
	rde_imsg_compose_eigrpe(type, nbr->peerid, 0, NULL, 0);
}

void
rde_check_update(struct rde_nbr *nbr, struct rinfo *ri)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct rt_node		*rn;
	struct eigrp_route	*route, *successor;
	uint32_t		 old_fdistance;
	struct rinfo		 sri;

	rn = rt_find(eigrp, ri);
	if (rn == NULL) {
		if (ri->metric.delay == EIGRP_INFINITE_METRIC)
			return;

		rn = rt_new(eigrp, ri);
		route = route_new(rn, nbr, ri);

		old_fdistance = EIGRP_INFINITE_METRIC;
	} else {
		old_fdistance = rn->successor.fdistance;

		if (ri->metric.delay == EIGRP_INFINITE_METRIC) {
			route = route_find(nbr, rn);
			if (route)
				route_del(rn, route);
		} else {
			route = route_find(nbr, rn);
			if (route == NULL)
				route = route_new(rn, nbr, ri);
			else
				route_update_metrics(eigrp, route, ri);
		}
	}

	switch (rn->state) {
	case DUAL_STA_PASSIVE:
		successor = rt_get_successor_fc(rn);

		/*
		 * go active if the successor was affected and no feasible
		 * successor exist.
		 */
		if (successor == NULL) {
			rde_send_query_all(eigrp, rn, 1);

			dual_fsm(rn, DUAL_EVT_4);
		} else {
			rt_set_successor(rn, successor);
			rt_update_fib(rn);

			/* send update with new metric if necessary */
			rinfo_fill_successor(rn, &sri);
			if (rn->successor.fdistance != old_fdistance)
				rde_send_update_all(rn, &sri);
		}
		break;
	case DUAL_STA_ACTIVE1:
		/* XXX event 9 if cost increase? */
		break;
	case DUAL_STA_ACTIVE3:
		/* XXX event 10 if cost increase? */
		break;
	}

	if ((rn->state & DUAL_STA_ACTIVE_ALL) && TAILQ_EMPTY(&rn->rijk))
		rde_last_reply(rn);
}

void
rde_check_query(struct rde_nbr *nbr, struct rinfo *ri, int siaquery)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct rt_node		*rn;
	struct eigrp_route	*route, *successor;
	uint32_t		 old_fdistance;
	struct rinfo		 sri;
	int			 reply_sent = 0;

	/*
	 * draft-savage-eigrp-02 - Section 4.3:
	 * "When a query is received for a route that doesn't exist in our
	 * topology table, a reply with infinite metric is sent and an entry
	 * in the topology table is added with the metric in the QUERY if
	 * the metric is not an infinite value".
	 */
	rn = rt_find(eigrp, ri);
	if (rn == NULL) {
		sri = *ri;
		sri.metric.delay = EIGRP_INFINITE_METRIC;
		rde_send_reply(nbr, &sri, 0);

		if (ri->metric.delay == EIGRP_INFINITE_METRIC)
			return;

		rn = rt_new(eigrp, ri);
		route = route_new(rn, nbr, ri);
		rt_set_successor(rn, route);
		return;
	}

	old_fdistance = rn->successor.fdistance;

	if (ri->metric.delay == EIGRP_INFINITE_METRIC) {
		route = route_find(nbr, rn);
		if (route)
			route_del(rn, route);
	} else {
		route = route_find(nbr, rn);
		if (route == NULL)
			route = route_new(rn, nbr, ri);
		else
			route_update_metrics(eigrp, route, ri);
	}

	switch (rn->state) {
	case DUAL_STA_PASSIVE:
		successor = rt_get_successor_fc(rn);

		/*
		 * go active if the successor was affected and no feasible
		 * successor exist.
		 */
		if (successor == NULL) {
			rde_send_query_all(eigrp, rn, 1);
			dual_fsm(rn, DUAL_EVT_3);
		} else {
			rt_set_successor(rn, successor);
			rt_update_fib(rn);

			/* send reply */
			rinfo_fill_successor(rn, &sri);
			rde_send_reply(nbr, &sri, 0);
			reply_sent = 1;

			/* send update with new metric if necessary */
			if (rn->successor.fdistance != old_fdistance)
				rde_send_update_all(rn, &sri);
		}
		break;
	case DUAL_STA_ACTIVE0:
	case DUAL_STA_ACTIVE1:
		if (nbr == rn->successor.nbr)
			dual_fsm(rn, DUAL_EVT_5);
		else {
			dual_fsm(rn, DUAL_EVT_6);
			rinfo_fill_successor(rn, &sri);
			sri.metric.flags |= F_METRIC_ACTIVE;
			rde_send_reply(nbr, &sri, 0);
			reply_sent = 1;
		}
		break;
	case DUAL_STA_ACTIVE2:
	case DUAL_STA_ACTIVE3:
		if (nbr == rn->successor.nbr) {
			/* XXX not defined in the spec, do nothing? */
		} else {
			dual_fsm(rn, DUAL_EVT_6);
			rinfo_fill_successor(rn, &sri);
			sri.metric.flags |= F_METRIC_ACTIVE;
			rde_send_reply(nbr, &sri, 0);
			reply_sent = 1;
		}
		break;
	}

	if ((rn->state & DUAL_STA_ACTIVE_ALL) && TAILQ_EMPTY(&rn->rijk))
		rde_last_reply(rn);

	if (siaquery && !reply_sent) {
		rinfo_fill_successor(rn, &sri);
		sri.metric.flags |= F_METRIC_ACTIVE;
		rde_send_reply(nbr, &sri, 1);
	}
}

static void
rde_last_reply(struct rt_node *rn)
{
	struct eigrp		*eigrp = rn->eigrp;
	struct eigrp_route	*successor;
	struct rde_nbr		*old_successor;
	struct rinfo		 ri;

	old_successor = rn->successor.nbr;

	switch (rn->state) {
	case DUAL_STA_ACTIVE0:
		successor = rt_get_successor_fc(rn);
		if (successor == NULL) {
			/* feasibility condition is not met */
			rde_send_query_all(eigrp, rn, 1);
			dual_fsm(rn, DUAL_EVT_11);
			break;
		}

		/* update successor - feasibility condition is met */
		rt_set_successor(rn, successor);

		/* advertise new successor to neighbors */
		rinfo_fill_successor(rn, &ri);
		rde_send_update_all(rn, &ri);

		dual_fsm(rn, DUAL_EVT_14);
		break;
	case DUAL_STA_ACTIVE1:
		/* update successor */
		rn->successor.fdistance = EIGRP_INFINITE_METRIC;
		successor = rt_get_successor_fc(rn);
		rt_set_successor(rn, successor);

		/* advertise new successor to neighbors */
		rinfo_fill_successor(rn, &ri);
		rde_send_update_all(rn, &ri);

		dual_fsm(rn, DUAL_EVT_15);
		break;
	case DUAL_STA_ACTIVE2:
		successor = rt_get_successor_fc(rn);
		if (successor == NULL) {
			/* feasibility condition is not met */
			rde_send_query_all(eigrp, rn, 1);
			dual_fsm(rn, DUAL_EVT_12);
			break;
		}

		/* update successor - feasibility condition is met */
		rt_set_successor(rn, successor);

		/* send a reply to the old successor */
		rinfo_fill_successor(rn, &ri);
		ri.metric.flags |= F_METRIC_ACTIVE;
		if (old_successor)
			rde_send_reply(old_successor, &ri, 0);

		/* advertise new successor to neighbors */
		rde_send_update_all(rn, &ri);

		dual_fsm(rn, DUAL_EVT_16);
		break;
	case DUAL_STA_ACTIVE3:
		/* update successor */
		rn->successor.fdistance = EIGRP_INFINITE_METRIC;
		successor = rt_get_successor_fc(rn);
		rt_set_successor(rn, successor);

		/* send a reply to the old successor */
		rinfo_fill_successor(rn, &ri);
		ri.metric.flags |= F_METRIC_ACTIVE;
		if (old_successor)
			rde_send_reply(old_successor, &ri, 0);

		/* advertise new successor to neighbors */
		rde_send_update_all(rn, &ri);

		dual_fsm(rn, DUAL_EVT_13);
		break;
	}

	if (rn->state == DUAL_STA_PASSIVE && rn->successor.nbr == NULL)
		rt_del(rn);
}

void
rde_check_reply(struct rde_nbr *nbr, struct rinfo *ri, int siareply)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct rt_node		*rn;
	struct reply_node       *reply;
	struct eigrp_route	*route;

	rn = rt_find(eigrp, ri);
	if (rn == NULL)
		return;

	/* XXX ignore reply when the state is passive? */
	if (rn->state == DUAL_STA_PASSIVE)
		return;

	reply = reply_outstanding_find(rn, nbr);
	if (reply == NULL)
		return;

	if (siareply) {
		reply->siareply_recv = 1;
		reply_active_start_timer(reply);
		return;
	}

	if (ri->metric.delay == EIGRP_INFINITE_METRIC) {
		route = route_find(nbr, rn);
		if (route)
			route_del(rn, route);
	} else {
		route = route_find(nbr, rn);
		if (route == NULL)
			route = route_new(rn, nbr, ri);
		else
			route_update_metrics(eigrp, route, ri);
	}

	reply_outstanding_remove(reply);
	if (TAILQ_EMPTY(&rn->rijk))
		rde_last_reply(rn);
}

void
rde_check_link_down_rn(struct rde_nbr *nbr, struct rt_node *rn,
    struct eigrp_route *route)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct reply_node       *reply;
	struct eigrp_route	*successor;
	uint32_t		 old_fdistance;
	struct rinfo		 ri;
	enum route_type		 type;

	old_fdistance = rn->successor.fdistance;

	type = route->type;
	route_del(rn, route);

	switch (rn->state) {
	case DUAL_STA_PASSIVE:
		successor = rt_get_successor_fc(rn);

		/*
		 * go active if the successor was affected and no feasible
		 * successor exist.
		 */
		if (successor == NULL) {
			rde_send_query_all(eigrp, rn, 0);

			dual_fsm(rn, DUAL_EVT_4);
		} else {
			rt_set_successor(rn, successor);
			rt_update_fib(rn);

			/* send update with new metric if necessary */
			rinfo_fill_successor(rn, &ri);
			if (rn->successor.fdistance != old_fdistance)
				rde_send_update_all(rn, &ri);
		}
		break;
	case DUAL_STA_ACTIVE1:
		if (nbr == rn->successor.nbr)
			dual_fsm(rn, DUAL_EVT_9);
		break;
	case DUAL_STA_ACTIVE3:
		if (nbr == rn->successor.nbr)
			dual_fsm(rn, DUAL_EVT_10);
		break;
	}

	if (rn->state & DUAL_STA_ACTIVE_ALL) {
		reply = reply_outstanding_find(rn, nbr);
		if (reply) {
			rinfo_fill_infinite(rn, type, &ri);
			rde_check_reply(nbr, &ri, 0);
		}
	}
}

void
rde_check_link_down_nbr(struct rde_nbr *nbr)
{
	struct eigrp		*eigrp = nbr->eigrp;
	struct rt_node		*rn, *safe;
	struct eigrp_route	*route;

	RB_FOREACH_SAFE(rn, rt_tree, &eigrp->topology, safe) {
		route = route_find(nbr, rn);
		if (route) {
			rde_check_link_down_rn(nbr, rn, route);
			if (rn->successor.nbr == nbr)
				rn->successor.nbr = NULL;
		}
	}
}

void
rde_check_link_down(unsigned int ifindex)
{
	struct rde_nbr		*nbr;

	RB_FOREACH(nbr, rde_nbr_head, &rde_nbrs)
		if (nbr->ei->iface->ifindex == ifindex)
			rde_check_link_down_nbr(nbr);

	rde_flush_queries();
}

void
rde_check_link_cost_change(struct rde_nbr *nbr, struct eigrp_iface *ei)
{
}

static __inline int
rde_nbr_compare(struct rde_nbr *a, struct rde_nbr *b)
{
	return (a->peerid - b->peerid);
}

struct rde_nbr *
rde_nbr_find(uint32_t peerid)
{
	struct rde_nbr	n;

	n.peerid = peerid;

	return (RB_FIND(rde_nbr_head, &rde_nbrs, &n));
}

struct rde_nbr *
rde_nbr_new(uint32_t peerid, struct rde_nbr *new)
{
	struct rde_nbr		*nbr;

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("rde_nbr_new");

	nbr->peerid = peerid;
	nbr->ifaceid = new->ifaceid;
	nbr->addr = new->addr;
	nbr->ei = eigrp_if_lookup_id(nbr->ifaceid);
	if (nbr->ei)
		nbr->eigrp = nbr->ei->eigrp;
	TAILQ_INIT(&nbr->rijk);
	nbr->flags = new->flags;

	if (nbr->peerid != NBR_IDSELF &&
	    RB_INSERT(rde_nbr_head, &rde_nbrs, nbr) != NULL)
		fatalx("rde_nbr_new: RB_INSERT failed");

	return (nbr);
}

void
rde_nbr_del(struct rde_nbr *nbr, int peerterm)
{
	struct reply_node	*reply;

	if (peerterm)
		rde_imsg_compose_eigrpe(IMSG_NEIGHBOR_DOWN, nbr->peerid,
		    0, NULL, 0);

	while((reply = TAILQ_FIRST(&nbr->rijk)) != NULL)
		reply_outstanding_remove(reply);

	if (nbr->peerid != NBR_IDSELF)
		RB_REMOVE(rde_nbr_head, &rde_nbrs, nbr);
	free(nbr);
}
