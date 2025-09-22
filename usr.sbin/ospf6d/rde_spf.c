/*	$OpenBSD: rde_spf.c,v 1.29 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2005 Esben Norby <norby@openbsd.org>
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "rde.h"

extern struct ospfd_conf	*rdeconf;
TAILQ_HEAD(, vertex)		 cand_list;
RB_HEAD(rt_tree, rt_node)	 rt;
RB_PROTOTYPE(rt_tree, rt_node, entry, rt_compare)
RB_GENERATE(rt_tree, rt_node, entry, rt_compare)
struct vertex			*spf_root = NULL;

struct in6_addr	*calc_nexthop_lladdr(struct vertex *, struct lsa_rtr_link *,
		     unsigned int);
void		 calc_nexthop_transit_nbr(struct vertex *, struct vertex *,
		     unsigned int);
void		 calc_nexthop(struct vertex *, struct vertex *,
		     struct area *, struct lsa_rtr_link *);
void		 rt_nexthop_clear(struct rt_node *);
void		 rt_nexthop_add(struct rt_node *, struct v_nexthead *,
		     u_int16_t, struct in_addr);
void		 rt_update(struct in6_addr *, u_int8_t, struct v_nexthead *,
		     u_int16_t, u_int32_t, u_int32_t, struct in_addr,
		     struct in_addr, enum path_type, enum dst_type, u_int8_t,
		     u_int32_t);
struct rt_node	*rt_lookup(enum dst_type, struct in6_addr *);
void		 rt_invalidate(struct area *);
int		 linked(struct vertex *, struct vertex *);

void
spf_calc(struct area *area)
{
	struct vertex		*v, *w;
	struct lsa_rtr_link	*rtr_link = NULL;
	struct lsa_net_link	*net_link;
	u_int32_t		 d;
	unsigned int		 i;

	/* clear SPF tree */
	spf_tree_clr(area);
	cand_list_clr();

	/* initialize SPF tree */
	if ((v = spf_root = lsa_find_rtr(area, rde_router_id())) == NULL)
		/* empty area because no interface is active */
		return;

	area->transit = 0;
	spf_root->cost = 0;
	w = NULL;

	/* calculate SPF tree */
	do {
		/* loop links */
		for (i = 0; i < lsa_num_links(v); i++) {
			switch (v->type) {
			case LSA_TYPE_ROUTER:
				rtr_link = get_rtr_link(v, i);
				switch (rtr_link->type) {
				case LINK_TYPE_POINTTOPOINT:
				case LINK_TYPE_VIRTUAL:
					/* find router LSA */
					w = lsa_find_rtr(area,
					    rtr_link->nbr_rtr_id);
					break;
				case LINK_TYPE_TRANSIT_NET:
					/* find network LSA */
					w = lsa_find_tree(&area->lsa_tree,
					    htons(LSA_TYPE_NETWORK),
					    rtr_link->nbr_iface_id,
					    rtr_link->nbr_rtr_id);
					break;
				default:
					fatalx("spf_calc: invalid link type");
				}
				break;
			case LSA_TYPE_NETWORK:
				net_link = get_net_link(v, i);
				/* find router LSA */
				w = lsa_find_rtr(area, net_link->att_rtr);
				break;
			default:
				fatalx("spf_calc: invalid LSA type");
			}

			if (w == NULL)
				continue;

			if (ntohs(w->lsa->hdr.age) == MAX_AGE)
				continue;

			if (lsa_num_links(w) == 0)
				continue;

			if (!linked(w, v)) {
				log_debug("spf_calc: w adv_rtr %s ls_id %s "
				    "type 0x%x numlinks %hu has no link to "
				    "v adv_rtr %s ls_id %s type 0x%x",
				    log_rtr_id(htonl(w->adv_rtr)),
				    log_rtr_id(htonl(w->ls_id)), w->type,
				    lsa_num_links(w),
				    log_rtr_id(htonl(v->adv_rtr)),
				    log_rtr_id(htonl(v->ls_id)), v->type);
				continue;
			}

			if (v->type == LSA_TYPE_ROUTER)
				d = v->cost + ntohs(rtr_link->metric);
			else
				d = v->cost;

			if (cand_list_present(w)) {
				if (d > w->cost)
					continue;
				if (d < w->cost) {
					w->cost = d;
					vertex_nexthop_clear(w);
					calc_nexthop(w, v, area, rtr_link);
					/*
					 * need to readd to candidate list
					 * because the list is sorted
					 */
					TAILQ_REMOVE(&cand_list, w, cand);
					cand_list_add(w);
				} else
					/* equal cost path */
					calc_nexthop(w, v, area, rtr_link);
			} else if (w->cost == LS_INFINITY && d < LS_INFINITY) {
				w->cost = d;

				vertex_nexthop_clear(w);
				calc_nexthop(w, v, area, rtr_link);
				cand_list_add(w);
			}
		}

		/* get next vertex */
		v = cand_list_pop();
		w = NULL;
	} while (v != NULL);

	/* spf_dump(area); */
	log_debug("spf_calc: area %s calculated", inet_ntoa(area->id));

	/* Dump SPF tree to log */
	RB_FOREACH(v, lsa_tree, &area->lsa_tree) {
		struct v_nexthop *vn;
		char hops[4096];
		struct iface *iface;

		bzero(hops, sizeof(hops));

		if (v->type != LSA_TYPE_ROUTER && v->type != LSA_TYPE_NETWORK)
			continue;

		TAILQ_FOREACH(vn, &v->nexthop, entry) {
			strlcat(hops, log_in6addr(&vn->nexthop), sizeof(hops));
			strlcat(hops, "%", sizeof(hops));
			if ((iface = if_find(vn->ifindex)) == NULL)
				fatalx("spf_calc: lost iface");
			strlcat(hops, iface->name, sizeof(hops));
			if (vn != TAILQ_LAST(&v->nexthop, v_nexthead))
				strlcat(hops, ", ", sizeof(hops));
		}
		log_debug("%s(%s, 0x%x, %s) cost %u has nexthops [%s]",
		    v == spf_root ? "*" : " ", log_rtr_id(htonl(v->adv_rtr)),
		    v->type, log_rtr_id(htonl(v->ls_id)), v->cost, hops);
	}

	area->num_spf_calc++;
	start_spf_timer();
}

void
rt_calc(struct vertex *v, struct area *area, struct ospfd_conf *conf)
{
	struct vertex		*w;
	struct lsa_intra_prefix	*iap;
	struct lsa_prefix	*prefix;
	struct in_addr		 adv_rtr;
	struct in6_addr		 ia6;
	u_int16_t		 i, off;
	u_int8_t		 flags;

	lsa_age(v);
	if (ntohs(v->lsa->hdr.age) == MAX_AGE)
		return;

	switch (v->type) {
	case LSA_TYPE_ROUTER:
		if (v->cost >= LS_INFINITY || TAILQ_EMPTY(&v->nexthop))
			return;

		/* router, only add border and as-external routers */
		flags = LSA_24_GETHI(ntohl(v->lsa->data.rtr.opts));
		if ((flags & (OSPF_RTR_B | OSPF_RTR_E)) == 0)
			return;

		bzero(&ia6, sizeof(ia6));
		adv_rtr.s_addr = htonl(v->adv_rtr);
		bcopy(&adv_rtr, &ia6.s6_addr[12], sizeof(adv_rtr));

		rt_update(&ia6, 128, &v->nexthop, v->type, v->cost, 0, area->id,
		    adv_rtr, PT_INTER_AREA, DT_RTR, flags, 0);
		break;
	case LSA_TYPE_INTRA_A_PREFIX:
		/* Find referenced LSA */
		iap = &v->lsa->data.pref_intra;
		switch (ntohs(iap->ref_type)) {
		case LSA_TYPE_ROUTER:
			w = lsa_find_rtr(area, iap->ref_adv_rtr);
			if (w == NULL) {
				warnx("rt_calc: Intra-Area-Prefix LSA (%s, %u) "
				    "references non-existent router %s",
				    log_rtr_id(htonl(v->adv_rtr)),
				    v->ls_id, log_rtr_id(iap->ref_adv_rtr));
				return;
			}
			flags = LSA_24_GETHI(ntohl(w->lsa->data.rtr.opts));
			break;
		case LSA_TYPE_NETWORK:
			w = lsa_find_tree(&area->lsa_tree, iap->ref_type,
			    iap->ref_ls_id, iap->ref_adv_rtr);
			if (w == NULL) {
				warnx("rt_calc: Intra-Area-Prefix LSA (%s, %u) "
				    "references non-existent Network LSA (%s, "
				    "%u)", log_rtr_id(htonl(v->adv_rtr)),
				    v->ls_id, log_rtr_id(iap->ref_adv_rtr),
				    ntohl(iap->ref_ls_id));
				return;
			}
			flags = 0;
			break;
		default:
			warnx("rt_calc: Intra-Area-Prefix LSA (%s, %u) has "
			    "invalid ref_type 0x%hx", log_rtr_id(v->adv_rtr),
			    v->ls_id, ntohs(iap->ref_type));
			return;
		}

		if (w->cost >= LS_INFINITY || TAILQ_EMPTY(&w->nexthop))
			return;

		/* Add prefixes listed in Intra-Area-Prefix LSA to routing
		 * table, using w as destination. */
		off = sizeof(v->lsa->hdr) + sizeof(struct lsa_intra_prefix);
		for (i = 0; i < ntohs(v->lsa->data.pref_intra.numprefix); i++) {
			prefix = (struct lsa_prefix *)((char *)(v->lsa) + off);
			if (!(prefix->options & OSPF_PREFIX_NU)) {
				bzero(&ia6, sizeof(ia6));
				bcopy(prefix + 1, &ia6,
				    LSA_PREFIXSIZE(prefix->prefixlen));

				adv_rtr.s_addr = htonl(w->adv_rtr);

				rt_update(&ia6, prefix->prefixlen, &w->nexthop,
				    v->type, w->cost + ntohs(prefix->metric), 0,
				    area->id, adv_rtr, PT_INTRA_AREA, DT_NET,
				    flags, 0);
			}
			off += sizeof(struct lsa_prefix)
			    + LSA_PREFIXSIZE(prefix->prefixlen);
		}
		break;
	case LSA_TYPE_INTER_A_PREFIX:
		/* XXX if ABR only look at area 0.0.0.0 LSA */
		/* ignore self-originated stuff */
		if (v->self)
			return;

		adv_rtr.s_addr = htonl(v->adv_rtr);
		w = lsa_find_rtr(area, adv_rtr.s_addr);
		if (w == NULL) {
			warnx("rt_calc: Inter-Area-Router LSA (%s, %u) "
			    "originated from non-existent router",
			    log_rtr_id(htonl(v->adv_rtr)),
			    v->ls_id);
			return;
		}
		if (w->cost >= LS_INFINITY || TAILQ_EMPTY(&w->nexthop))
			return;

		/* Add prefix listed in Inter-Area-Prefix LSA to routing
		 * table, using w as destination. */
		off = sizeof(v->lsa->hdr) + sizeof(struct lsa_prefix_sum);
		prefix = (struct lsa_prefix *)((char *)(v->lsa) + off);
		if (prefix->options & OSPF_PREFIX_NU)
			return;

		bzero(&ia6, sizeof(ia6));
		bcopy(prefix + 1, &ia6, LSA_PREFIXSIZE(prefix->prefixlen));

		rt_update(&ia6, prefix->prefixlen, &w->nexthop, v->type,
		    w->cost + (ntohs(v->lsa->data.rtr_sum.metric) &
		    LSA_METRIC_MASK), 0, area->id, adv_rtr, PT_INTER_AREA,
		    DT_NET, 0, 0);
		break;
	case LSA_TYPE_INTER_A_ROUTER:
		/* XXX if ABR only look at area 0.0.0.0 LSA */
		/* ignore self-originated stuff */
		if (v->self)
			return;

		adv_rtr.s_addr = htonl(v->adv_rtr);
		w = lsa_find_rtr(area, adv_rtr.s_addr);
		if (w == NULL) {
			warnx("rt_calc: Inter-Area-Router LSA (%s, %u) "
			    "originated from non-existent router",
			    log_rtr_id(htonl(v->adv_rtr)),
			    v->ls_id);
			return;
		}
		if (w->cost >= LS_INFINITY || TAILQ_EMPTY(&w->nexthop))
			return;

		/* Add router listed in Inter-Area-Router LSA to routing
		 * table, using w as destination. */
		bzero(&ia6, sizeof(ia6));
		bcopy(&v->lsa->data.rtr_sum.dest_rtr_id, &ia6.s6_addr[12],
		    4);

		rt_update(&ia6, 128, &w->nexthop, v->type, w->cost +
		    (ntohs(v->lsa->data.rtr_sum.metric) & LSA_METRIC_MASK), 0,
		    area->id, adv_rtr, PT_INTER_AREA, DT_RTR, 0, 0);
		break;
	default:
		break;
	}
}

void
asext_calc(struct vertex *v)
{
	struct in6_addr		 addr, fw_addr;
	struct rt_node		*r;
	struct rt_nexthop	*rn;
	struct lsa_prefix	*prefix;
	struct in_addr		 adv_rtr, area;
	char			*p;
	u_int32_t		 metric, cost2, ext_tag = 0;
	enum path_type		 type;

	lsa_age(v);
	if (ntohs(v->lsa->hdr.age) == MAX_AGE ||
	    (ntohl(v->lsa->data.asext.metric) & LSA_METRIC_MASK) >=
	    LS_INFINITY)
		return;

	switch (v->type) {
	case LSA_TYPE_EXTERNAL:
		/* ignore self-originated stuff */
		if (v->self)
			return;

		adv_rtr.s_addr = htonl(v->adv_rtr);
		bzero(&addr, sizeof(addr));
		bcopy(&adv_rtr, &addr.s6_addr[12], sizeof(adv_rtr));
		if ((r = rt_lookup(DT_RTR, &addr)) == NULL)
			return;

		prefix = &v->lsa->data.asext.prefix;
		if (prefix->options & OSPF_PREFIX_NU)
			break;
		bzero(&addr, sizeof(addr));
		bcopy(prefix + 1, &addr,
		    LSA_PREFIXSIZE(prefix->prefixlen));

		p = (char *)(prefix + 1) + LSA_PREFIXSIZE(prefix->prefixlen);
		metric = ntohl(v->lsa->data.asext.metric);

		if (metric & LSA_ASEXT_F_FLAG) {
			bcopy(p, &fw_addr, sizeof(fw_addr));
			p += sizeof(fw_addr);

			/* lookup forwarding address */
			if ((r = rt_lookup(DT_NET, &fw_addr)) == NULL ||
			    (r->p_type != PT_INTRA_AREA &&
			    r->p_type != PT_INTER_AREA))
				return;
		}
		if (metric & LSA_ASEXT_T_FLAG) {
			bcopy(p, &ext_tag, sizeof(ext_tag));
			p += sizeof(ext_tag);
		}
		if (metric & LSA_ASEXT_E_FLAG) {
			v->cost = r->cost;
			cost2 = metric & LSA_METRIC_MASK;
			type = PT_TYPE2_EXT;
		} else {
			v->cost = r->cost + (metric & LSA_METRIC_MASK);
			cost2 = 0;
			type = PT_TYPE1_EXT;
		}

		area.s_addr = 0;
		vertex_nexthop_clear(v);
		TAILQ_FOREACH(rn, &r->nexthop, entry) {
			if (rn->invalid)
				continue;

			if (rn->connected && r->d_type == DT_NET) {
				if (metric & LSA_ASEXT_F_FLAG)
					vertex_nexthop_add(v, NULL, &fw_addr,
					    rn->ifindex);
				else
					fatalx("asext_calc: I'm sorry Dave, "
					    "I'm afraid I can't do that.");
			} else
				vertex_nexthop_add(v, NULL, &rn->nexthop,
				    rn->ifindex);
		}

		rt_update(&addr, prefix->prefixlen, &v->nexthop, v->type,
		    v->cost, cost2, area, adv_rtr, type, DT_NET, 0, ext_tag);
		break;
	default:
		fatalx("asext_calc: invalid LSA type");
	}
}

void
spf_tree_clr(struct area *area)
{
	struct lsa_tree	*tree = &area->lsa_tree;
	struct vertex	*v;

	RB_FOREACH(v, lsa_tree, tree) {
		v->cost = LS_INFINITY;
		vertex_nexthop_clear(v);
	}
}

struct in6_addr *
calc_nexthop_lladdr(struct vertex *dst, struct lsa_rtr_link *rtr_link,
    unsigned int ifindex)
{
	struct iface		*iface;
	struct vertex		*link;
	struct rde_nbr		*nbr;

	/* Find outgoing interface, we need its LSA tree */
	LIST_FOREACH(iface, &dst->area->iface_list, entry) {
		if (ifindex == iface->ifindex)
			break;
	}
	if (!iface) {
		log_warnx("calc_nexthop_lladdr: no interface found for "
		    "ifindex %d", ntohl(rtr_link->iface_id));
		return (NULL);
	}

	/* Determine neighbor's link-local address.
	 * Try to get it from link LSA first. */
	link = lsa_find_tree(&iface->lsa_tree,
		htons(LSA_TYPE_LINK), rtr_link->iface_id,
		htonl(dst->adv_rtr));
	if (link)
		return &link->lsa->data.link.lladdr;

	/* Not found, so fall back to source address
	 * advertised in hello packet. */
	if ((nbr = rde_nbr_find(dst->peerid)) == NULL)
		fatalx("next hop is not a neighbor");
	return &nbr->addr;
}

void
calc_nexthop_transit_nbr(struct vertex *dst, struct vertex *parent,
    unsigned int ifindex)
{
	struct lsa_rtr_link	*rtr_link;
	unsigned int		 i;
	struct in6_addr		*lladdr;

	if (dst->type != LSA_TYPE_ROUTER)
		fatalx("calc_nexthop_transit_nbr: dst is not a router");
	if (parent->type != LSA_TYPE_NETWORK)
		fatalx("calc_nexthop_transit_nbr: parent is not a network");

	/* dst is a neighbor on a directly attached transit network.
	 * Figure out dst's link local address and add it as nexthop. */
	for (i = 0; i < lsa_num_links(dst); i++) {
		rtr_link = get_rtr_link(dst, i);
		if (rtr_link->type == LINK_TYPE_TRANSIT_NET &&
		    rtr_link->nbr_rtr_id == parent->lsa->hdr.adv_rtr &&
		    rtr_link->nbr_iface_id == parent->lsa->hdr.ls_id) {
			lladdr = calc_nexthop_lladdr(dst, rtr_link, ifindex);
			vertex_nexthop_add(dst, parent, lladdr, ifindex);
		}
	}
}

void
calc_nexthop(struct vertex *dst, struct vertex *parent,
    struct area *area, struct lsa_rtr_link *rtr_link)
{
	struct v_nexthop	*vn;
	struct in6_addr		*nexthop;

	/* case 1 */
	if (parent == spf_root) {
		switch (dst->type) {
		case LSA_TYPE_ROUTER:
			if (rtr_link->type != LINK_TYPE_POINTTOPOINT)
				fatalx("inconsistent SPF tree");
			nexthop = calc_nexthop_lladdr(dst, rtr_link,
			    ntohl(rtr_link->iface_id));
			break;
		case LSA_TYPE_NETWORK:
			if (rtr_link->type != LINK_TYPE_TRANSIT_NET)
				fatalx("inconsistent SPF tree");

			/* Next hop address cannot be determined yet,
			 * we only know the outgoing interface. */
			nexthop = NULL;
			break;
		default:
			fatalx("calc_nexthop: invalid dst type");
		}

		vertex_nexthop_add(dst, parent, nexthop,
		    ntohl(rtr_link->iface_id));
		return;
	}

	/* case 2 */
	if (parent->type == LSA_TYPE_NETWORK && dst->type == LSA_TYPE_ROUTER) {
		TAILQ_FOREACH(vn, &parent->nexthop, entry) {
			if (vn->prev == spf_root)
				calc_nexthop_transit_nbr(dst, parent,
				    vn->ifindex);
			else
				/* dst is more than one transit net away */
				vertex_nexthop_add(dst, parent, &vn->nexthop,
				    vn->ifindex);
		}
		return;
	}

	/* case 3 */
	TAILQ_FOREACH(vn, &parent->nexthop, entry)
	    vertex_nexthop_add(dst, parent, &vn->nexthop, vn->ifindex);
}

/* candidate list */
void
cand_list_init(void)
{
	TAILQ_INIT(&cand_list);
}

void
cand_list_add(struct vertex *v)
{
	struct vertex	*c = NULL;

	TAILQ_FOREACH(c, &cand_list, cand) {
		if (c->cost > v->cost) {
			TAILQ_INSERT_BEFORE(c, v, cand);
			return;
		} else if (c->cost == v->cost && c->type == LSA_TYPE_ROUTER &&
		    v->type == LSA_TYPE_NETWORK) {
			TAILQ_INSERT_BEFORE(c, v, cand);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&cand_list, v, cand);
}

struct vertex *
cand_list_pop(void)
{
	struct vertex	*c;

	if ((c = TAILQ_FIRST(&cand_list)) != NULL) {
		TAILQ_REMOVE(&cand_list, c, cand);
	}

	return (c);
}

int
cand_list_present(struct vertex *v)
{
	struct vertex	*c;

	TAILQ_FOREACH(c, &cand_list, cand) {
		if (c == v)
			return (1);
	}

	return (0);
}

void
cand_list_clr(void)
{
	struct vertex *c;

	while ((c = TAILQ_FIRST(&cand_list)) != NULL) {
		TAILQ_REMOVE(&cand_list, c, cand);
	}
}

/* timers */
void
spf_timer(int fd, short event, void *arg)
{
	struct vertex		*v;
	struct ospfd_conf	*conf = arg;
	struct area		*area;
	struct rt_node		*r;

	switch (conf->spf_state) {
	case SPF_IDLE:
		fatalx("spf_timer: invalid state IDLE");
	case SPF_HOLDQUEUE:
		conf->spf_state = SPF_DELAY;
		/* FALLTHROUGH */
	case SPF_DELAY:
		LIST_FOREACH(area, &conf->area_list, entry) {
			if (area->dirty) {
				/* invalidate RIB entries of this area */
				rt_invalidate(area);

				/* calculate SPF tree */
				spf_calc(area);

				/* calculate route table */
				RB_FOREACH(v, lsa_tree, &area->lsa_tree) {
					rt_calc(v, area, conf);
				}

				area->dirty = 0;
			}
		}

		/* calculate as-external routes, first invalidate them */
		rt_invalidate(NULL);
		RB_FOREACH(v, lsa_tree, &asext_tree) {
			asext_calc(v);
		}

		RB_FOREACH(r, rt_tree, &rt) {
			LIST_FOREACH(area, &conf->area_list, entry)
				rde_summary_update(r, area);

			if (r->d_type != DT_NET)
				continue;

			if (r->invalid)
				rde_send_delete_kroute(r);
			else
				rde_send_change_kroute(r);
		}

		LIST_FOREACH(area, &conf->area_list, entry)
			lsa_remove_invalid_sums(area);

		start_spf_holdtimer(conf);
		break;
	case SPF_HOLD:
		conf->spf_state = SPF_IDLE;
		break;
	default:
		fatalx("spf_timer: unknown state");
	}
}

void
start_spf_timer(void)
{
	struct timeval	tv;

	switch (rdeconf->spf_state) {
	case SPF_IDLE:
		timerclear(&tv);
		tv.tv_sec = rdeconf->spf_delay;
		rdeconf->spf_state = SPF_DELAY;
		if (evtimer_add(&rdeconf->ev, &tv) == -1)
			fatal("start_spf_timer");
		break;
	case SPF_DELAY:
		/* ignore */
		break;
	case SPF_HOLD:
		rdeconf->spf_state = SPF_HOLDQUEUE;
		break;
	case SPF_HOLDQUEUE:
		/* ignore */
		break;
	default:
		fatalx("start_spf_timer: invalid spf_state");
	}
}

void
stop_spf_timer(struct ospfd_conf *conf)
{
	if (evtimer_del(&conf->ev) == -1)
		fatal("stop_spf_timer");
}

void
start_spf_holdtimer(struct ospfd_conf *conf)
{
	struct timeval	tv;

	switch (conf->spf_state) {
	case SPF_DELAY:
		timerclear(&tv);
		tv.tv_sec = conf->spf_hold_time;
		conf->spf_state = SPF_HOLD;
		if (evtimer_add(&conf->ev, &tv) == -1)
			fatal("start_spf_holdtimer");
		break;
	case SPF_IDLE:
	case SPF_HOLD:
	case SPF_HOLDQUEUE:
		fatalx("start_spf_holdtimer: invalid state");
	default:
		fatalx("start_spf_holdtimer: unknown state");
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
	int	i;

	/* XXX maybe a & b need to be switched */
	i = memcmp(&a->prefix, &b->prefix, sizeof(a->prefix));
	if (i)
		return (i);
	if (a->prefixlen < b->prefixlen)
		return (-1);
	if (a->prefixlen > b->prefixlen)
		return (1);
	if (a->d_type > b->d_type)
		return (-1);
	if (a->d_type < b->d_type)
		return (1);
	return (0);
}

struct rt_node *
rt_find(struct in6_addr *prefix, u_int8_t prefixlen, enum dst_type d_type)
{
	struct rt_node	s;

	s.prefix = *prefix;
	s.prefixlen = prefixlen;
	s.d_type = d_type;

	return (RB_FIND(rt_tree, &rt, &s));
}

int
rt_insert(struct rt_node *r)
{
	if (RB_INSERT(rt_tree, &rt, r) != NULL) {
		log_warnx("rt_insert failed for %s/%u",
		    log_in6addr(&r->prefix), r->prefixlen);
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
		    log_in6addr(&r->prefix), r->prefixlen);
		return (-1);
	}

	rt_nexthop_clear(r);
	free(r);
	return (0);
}

void
rt_invalidate(struct area *area)
{
	struct rt_node		*r, *nr;
	struct rt_nexthop	*rn, *nrn;

	for (r = RB_MIN(rt_tree, &rt); r != NULL; r = nr) {
		nr = RB_NEXT(rt_tree, &rt, r);
		if (area == NULL) {
			/* look only at as_ext routes */
			if (r->p_type != PT_TYPE1_EXT &&
			    r->p_type != PT_TYPE2_EXT)
				continue;
		} else {
			/* ignore all as_ext routes */
			if (r->p_type == PT_TYPE1_EXT ||
			    r->p_type == PT_TYPE2_EXT)
				continue;

			/* look only at routes matching the area */
			if (r->area.s_addr != area->id.s_addr)
				continue;
		}
		r->invalid = 1;
		for (rn = TAILQ_FIRST(&r->nexthop); rn != NULL; rn = nrn) {
			nrn = TAILQ_NEXT(rn, entry);
			if (rn->invalid) {
				TAILQ_REMOVE(&r->nexthop, rn, entry);
				free(rn);
			} else
				rn->invalid = 1;
		}
		if (TAILQ_EMPTY(&r->nexthop))
			rt_remove(r);
	}
}

void
rt_nexthop_clear(struct rt_node *r)
{
	struct rt_nexthop	*rn;

	while ((rn = TAILQ_FIRST(&r->nexthop)) != NULL) {
		TAILQ_REMOVE(&r->nexthop, rn, entry);
		free(rn);
	}
}

void
rt_nexthop_add(struct rt_node *r, struct v_nexthead *vnh, u_int16_t type,
    struct in_addr adv_rtr)
{
	struct v_nexthop	*vn;
	struct rt_nexthop	*rn;
	struct timespec		 now;

	TAILQ_FOREACH(vn, vnh, entry) {
		TAILQ_FOREACH(rn, &r->nexthop, entry) {
			if (!IN6_ARE_ADDR_EQUAL(&rn->nexthop, &vn->nexthop))
				continue;

			rn->adv_rtr.s_addr = adv_rtr.s_addr;
			rn->connected = (type == LSA_TYPE_NETWORK &&
			    vn->prev == spf_root) ||
			    (IN6_IS_ADDR_UNSPECIFIED(&vn->nexthop));
			rn->invalid = 0;

			r->invalid = 0;
			break;
		}
		if (rn)
			continue;

		if ((rn = calloc(1, sizeof(struct rt_nexthop))) == NULL)
			fatal("rt_nexthop_add");

		clock_gettime(CLOCK_MONOTONIC, &now);
		rn->nexthop = vn->nexthop;
		rn->ifindex = vn->ifindex;
		rn->adv_rtr.s_addr = adv_rtr.s_addr;
		rn->uptime = now.tv_sec;
		rn->connected = (type == LSA_TYPE_NETWORK &&
		    vn->prev == spf_root) ||
		    (IN6_IS_ADDR_UNSPECIFIED(&vn->nexthop));
		rn->invalid = 0;

		r->invalid = 0;
		TAILQ_INSERT_TAIL(&r->nexthop, rn, entry);
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
rt_dump(struct in_addr area, pid_t pid, u_int8_t r_type)
{
	static struct ctl_rt	 rtctl;
	struct timespec		 now;
	struct rt_node		*r;
	struct rt_nexthop	*rn;

	clock_gettime(CLOCK_MONOTONIC, &now);

	RB_FOREACH(r, rt_tree, &rt) {
		if (r->invalid)
			continue;

		if (r->area.s_addr != area.s_addr)
			continue;

		switch (r_type) {
		case RIB_RTR:
			if (r->d_type != DT_RTR)
				continue;
			break;
		case RIB_NET:
			if (r->d_type != DT_NET)
				continue;
			if (r->p_type == PT_TYPE1_EXT ||
			    r->p_type == PT_TYPE2_EXT)
				continue;
			break;
		case RIB_EXT:
			if (r->p_type != PT_TYPE1_EXT &&
			    r->p_type != PT_TYPE2_EXT)
				continue;
			break;
		default:
			fatalx("rt_dump: invalid RIB type");
		}

		memset(&rtctl, 0, sizeof(rtctl));
		rtctl.prefix = r->prefix;
		rtctl.area.s_addr = r->area.s_addr;
		rtctl.cost = r->cost;
		rtctl.cost2 = r->cost2;
		rtctl.p_type = r->p_type;
		rtctl.d_type = r->d_type;
		rtctl.flags = r->flags;
		rtctl.prefixlen = r->prefixlen;

		TAILQ_FOREACH(rn, &r->nexthop, entry) {
			if (rn->invalid)
				continue;

			rtctl.connected = rn->connected;
			rtctl.nexthop = rn->nexthop;
			rtctl.ifindex = rn->ifindex;
			rtctl.adv_rtr.s_addr = rn->adv_rtr.s_addr;
			rtctl.uptime = now.tv_sec - rn->uptime;

			rde_imsg_compose_ospfe(IMSG_CTL_SHOW_RIB, 0, pid,
			    &rtctl, sizeof(rtctl));
		}
	}
}

void
rt_update(struct in6_addr *prefix, u_int8_t prefixlen, struct v_nexthead *vnh,
     u_int16_t v_type, u_int32_t cost, u_int32_t cost2, struct in_addr area,
     struct in_addr adv_rtr, enum path_type p_type, enum dst_type d_type,
     u_int8_t flags, u_int32_t tag)
{
	struct rt_node		*rte;
	struct rt_nexthop	*rn;
	int			 better = 0, equal = 0;

	if (vnh == NULL || TAILQ_EMPTY(vnh))	/* XXX remove */
		fatalx("rt_update: invalid nexthop");

	if ((rte = rt_find(prefix, prefixlen, d_type)) == NULL) {
		if ((rte = calloc(1, sizeof(struct rt_node))) == NULL)
			fatal("rt_update");

		TAILQ_INIT(&rte->nexthop);
		rte->prefix = *prefix;
		rte->prefixlen = prefixlen;
		rte->cost = cost;
		rte->cost2 = cost2;
		rte->area = area;
		rte->p_type = p_type;
		rte->d_type = d_type;
		rte->flags = flags;
		rte->ext_tag = tag;

		rt_nexthop_add(rte, vnh, v_type, adv_rtr);

		rt_insert(rte);
	} else {
		/* order:
		 * 1. intra-area
		 * 2. inter-area
		 * 3. type 1 as ext
		 * 4. type 2 as ext
		 */
		if (rte->invalid)	/* everything is better than invalid */
			better = 1;
		else if (p_type < rte->p_type)
			better = 1;
		else if (p_type == rte->p_type)
			switch (p_type) {
			case PT_INTRA_AREA:
			case PT_INTER_AREA:
				if (cost < rte->cost)
					better = 1;
				else if (cost == rte->cost &&
				    rte->area.s_addr == area.s_addr)
					equal = 1;
				break;
			case PT_TYPE1_EXT:
				if (cost < rte->cost)
					better = 1;
				else if (cost == rte->cost)
					equal = 1;
				break;
			case PT_TYPE2_EXT:
				if (cost2 < rte->cost2)
					better = 1;
				else if (cost2 == rte->cost2 &&
				    cost < rte->cost)
					better = 1;
				else if (cost2 == rte->cost2 &&
				    cost == rte->cost)
					equal = 1;
				break;
			}

		if (better) {
			TAILQ_FOREACH(rn, &rte->nexthop, entry)
				rn->invalid = 1;

			rte->area = area;
			rte->cost = cost;
			rte->cost2 = cost2;
			rte->p_type = p_type;
			rte->flags = flags;
			rte->ext_tag = tag;
		}

		if (equal || better)
			rt_nexthop_add(rte, vnh, v_type, adv_rtr);
	}
}

struct rt_node *
rt_lookup(enum dst_type type, struct in6_addr *addr)
{
	struct rt_node	*rn;
	struct in6_addr	 ina;
	u_int8_t	 i = 128;

	if (type == DT_RTR) {
		rn = rt_find(addr, 128, type);
		if (rn && rn->invalid == 0)
			return (rn);
		return (NULL);
	}

	/* type == DT_NET */
	do {
		inet6applymask(&ina, addr, i);
		if ((rn = rt_find(&ina, i, type)) && rn->invalid == 0)
			return (rn);
	} while (i-- != 0);

	return (NULL);
}

/* router LSA links */
struct lsa_rtr_link *
get_rtr_link(struct vertex *v, unsigned int idx)
{
	struct lsa_rtr_link	*rtr_link = NULL;
	unsigned int		 frag = 1;
	unsigned int		 frag_nlinks;
	unsigned int		 nlinks = 0;
	unsigned int		 i;

	if (v->type != LSA_TYPE_ROUTER)
		fatalx("get_rtr_link: invalid LSA type");

	/* Treat multiple Router-LSAs originated by the same router
	 * as an aggregate. */
	do {
		/* number of links validated earlier by lsa_check() */
		rtr_link = (struct lsa_rtr_link *)((char *)v->lsa +
		    sizeof(v->lsa->hdr) + sizeof(struct lsa_rtr));
		frag_nlinks = ((ntohs(v->lsa->hdr.len) -
		    sizeof(struct lsa_hdr) - sizeof(struct lsa_rtr)) /
		    sizeof(struct lsa_rtr_link));
		if (nlinks + frag_nlinks > idx) {
			for (i = 0; i < frag_nlinks; i++) {
				if (i + nlinks == idx)
					return (rtr_link);
				rtr_link++;
			}
		}
		nlinks += frag_nlinks;
		v = lsa_find_rtr_frag(v->area, htonl(v->adv_rtr), frag++);
	} while (v);

	fatalx("get_rtr_link: index not found");
}

/* network LSA links */
struct lsa_net_link *
get_net_link(struct vertex *v, unsigned int idx)
{
	struct lsa_net_link	*net_link = NULL;
	char			*buf = (char *)v->lsa;
	unsigned int		 i, nlinks;

	if (v->type != LSA_TYPE_NETWORK)
		fatalx("get_net_link: invalid LSA type");

	net_link = (struct lsa_net_link *)(buf + sizeof(v->lsa->hdr) +
	    sizeof(struct lsa_net));

	/* number of links validated earlier by lsa_check() */
	nlinks = lsa_num_links(v);
	for (i = 0; i < nlinks; i++) {
		if (i == idx)
			return (net_link);
		net_link++;
	}

	fatalx("get_net_link: index not found");
}

/* misc */
int
linked(struct vertex *w, struct vertex *v)
{
	struct lsa_rtr_link	*rtr_link = NULL;
	struct lsa_net_link	*net_link = NULL;
	unsigned int		 i;

	switch (w->type) {
	case LSA_TYPE_ROUTER:
		for (i = 0; i < lsa_num_links(w); i++) {
			rtr_link = get_rtr_link(w, i);
			switch (v->type) {
			case LSA_TYPE_ROUTER:
				if (rtr_link->type == LINK_TYPE_POINTTOPOINT &&
				    rtr_link->nbr_rtr_id == htonl(v->adv_rtr))
					return (1);
				break;
			case LSA_TYPE_NETWORK:
				if (rtr_link->type == LINK_TYPE_TRANSIT_NET &&
				    rtr_link->nbr_rtr_id == htonl(v->adv_rtr) &&
				    rtr_link->nbr_iface_id == htonl(v->ls_id))
					return (1);
				break;
			default:
				fatalx("linked: invalid type");
			}
		}
		return (0);
	case LSA_TYPE_NETWORK:
		for (i = 0; i < lsa_num_links(w); i++) {
			net_link = get_net_link(w, i);
			switch (v->type) {
			case LSA_TYPE_ROUTER:
				if (net_link->att_rtr == htonl(v->adv_rtr))
					return (1);
				break;
			default:
				fatalx("linked: invalid type");
			}
		}
		return (0);
	default:
		fatalx("linked: invalid LSA type");
	}

	return (0);
}
