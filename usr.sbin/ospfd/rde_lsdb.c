/*	$OpenBSD: rde_lsdb.c,v 1.52 2023/03/08 04:43:14 guenther Exp $ */

/*
 * Copyright (c) 2004, 2005 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/tree.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ospf.h"
#include "ospfd.h"
#include "rde.h"
#include "log.h"

struct vertex	*vertex_get(struct lsa *, struct rde_nbr *, struct lsa_tree *);

int		 lsa_router_check(struct lsa *, u_int16_t);
struct vertex	*lsa_find_tree(struct lsa_tree *, u_int16_t, u_int32_t,
		    u_int32_t);
void		 lsa_timeout(int, short, void *);
void		 lsa_refresh(struct vertex *);
int		 lsa_equal(struct lsa *, struct lsa *);

RB_GENERATE(lsa_tree, vertex, entry, lsa_compare)

void
lsa_init(struct lsa_tree *t)
{
	RB_INIT(t);
}

int
lsa_compare(struct vertex *a, struct vertex *b)
{
	if (a->type < b->type)
		return (-1);
	if (a->type > b->type)
		return (1);
	if (a->adv_rtr < b->adv_rtr)
		return (-1);
	if (a->adv_rtr > b->adv_rtr)
		return (1);
	if (a->ls_id < b->ls_id)
		return (-1);
	if (a->ls_id > b->ls_id)
		return (1);
	return (0);
}


struct vertex *
vertex_get(struct lsa *lsa, struct rde_nbr *nbr, struct lsa_tree *tree)
{
	struct vertex	*v;
	struct timespec	 tp;

	if ((v = calloc(1, sizeof(struct vertex))) == NULL)
		fatal(NULL);
	TAILQ_INIT(&v->nexthop);
	v->area = nbr->area;
	v->peerid = nbr->peerid;
	v->lsa = lsa;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	v->changed = v->stamp = tp.tv_sec;
	v->cost = LS_INFINITY;
	v->ls_id = ntohl(lsa->hdr.ls_id);
	v->adv_rtr = ntohl(lsa->hdr.adv_rtr);
	v->type = lsa->hdr.type;
	v->lsa_tree = tree;

	if (!nbr->self)
		v->flooded = 1; /* XXX fix me */
	v->self = nbr->self;

	evtimer_set(&v->ev, lsa_timeout, v);

	return (v);
}

void
vertex_free(struct vertex *v)
{
	RB_REMOVE(lsa_tree, v->lsa_tree, v);

	(void)evtimer_del(&v->ev);
	vertex_nexthop_clear(v);
	free(v->lsa);
	free(v);
}

void
vertex_nexthop_clear(struct vertex *v)
{
	struct v_nexthop	*vn;

	while ((vn = TAILQ_FIRST(&v->nexthop))) {
		TAILQ_REMOVE(&v->nexthop, vn, entry);
		free(vn);
	}
}

void
vertex_nexthop_add(struct vertex *dst, struct vertex *parent, u_int32_t nexthop)
{
	struct v_nexthop	*vn;

	if ((vn = calloc(1, sizeof(*vn))) == NULL)
		fatal("vertex_nexthop_add");

	vn->prev = parent;
	vn->nexthop.s_addr = nexthop;

	TAILQ_INSERT_TAIL(&dst->nexthop, vn, entry);
}

/* returns -1 if a is older, 1 if newer and 0 if equal to b */
int
lsa_newer(struct lsa_hdr *a, struct lsa_hdr *b)
{
	int32_t		 a32, b32;
	u_int16_t	 a16, b16;
	int		 i;

	if (a == NULL)
		return (-1);
	if (b == NULL)
		return (1);

	/*
	 * The sequence number is defined as signed 32-bit integer,
	 * no idea how IETF came up with such a stupid idea.
	 */
	a32 = (int32_t)ntohl(a->seq_num);
	b32 = (int32_t)ntohl(b->seq_num);

	if (a32 > b32)
		return (1);
	if (a32 < b32)
		return (-1);

	a16 = ntohs(a->ls_chksum);
	b16 = ntohs(b->ls_chksum);

	if (a16 > b16)
		return (1);
	if (a16 < b16)
		return (-1);

	a16 = ntohs(a->age);
	b16 = ntohs(b->age);

	if (a16 >= MAX_AGE && b16 >= MAX_AGE)
		return (0);
	if (b16 >= MAX_AGE)
		return (-1);
	if (a16 >= MAX_AGE)
		return (1);

	i = b16 - a16;
	if (abs(i) > MAX_AGE_DIFF)
		return (i > 0 ? 1 : -1);

	return (0);
}

int
lsa_check(struct rde_nbr *nbr, struct lsa *lsa, u_int16_t len)
{
	struct area	*area = nbr->area;
	u_int32_t	 metric;

	if (len < sizeof(lsa->hdr)) {
		log_warnx("lsa_check: bad packet size");
		return (0);
	}
	if (ntohs(lsa->hdr.len) != len) {
		log_warnx("lsa_check: bad packet size");
		return (0);
	}

	if (iso_cksum(lsa, len, 0)) {
		log_warnx("lsa_check: bad packet checksum");
		return (0);
	}

	/* invalid ages */
	if ((ntohs(lsa->hdr.age) < 1 && !nbr->self) ||
	    ntohs(lsa->hdr.age) > MAX_AGE) {
		log_warnx("lsa_check: bad age");
		return (0);
	}

	/* invalid sequence number */
	if (ntohl(lsa->hdr.seq_num) == RESV_SEQ_NUM) {
		log_warnx("ls_check: bad seq num");
		return (0);
	}

	switch (lsa->hdr.type) {
	case LSA_TYPE_ROUTER:
		if (!lsa_router_check(lsa, len))
			return (0);
		break;
	case LSA_TYPE_NETWORK:
		if ((len % sizeof(u_int32_t)) ||
		    len < sizeof(lsa->hdr) + sizeof(u_int32_t)) {
			log_warnx("lsa_check: bad LSA network packet");
			return (0);
		}
		break;
	case LSA_TYPE_SUM_NETWORK:
	case LSA_TYPE_SUM_ROUTER:
		if ((len % sizeof(u_int32_t)) ||
		    len < sizeof(lsa->hdr) + sizeof(lsa->data.sum)) {
			log_warnx("lsa_check: bad LSA summary packet");
			return (0);
		}
		metric = ntohl(lsa->data.sum.metric);
		if (metric & ~LSA_METRIC_MASK) {
			log_warnx("lsa_check: bad LSA summary metric");
			return (0);
		}
		break;
	case LSA_TYPE_EXTERNAL:
		if ((len % (3 * sizeof(u_int32_t))) ||
		    len < sizeof(lsa->hdr) + sizeof(lsa->data.asext)) {
			log_warnx("lsa_check: bad LSA as-external packet");
			return (0);
		}
		metric = ntohl(lsa->data.asext.metric);
		if (metric & ~(LSA_METRIC_MASK | LSA_ASEXT_E_FLAG)) {
			log_warnx("lsa_check: bad LSA as-external metric");
			return (0);
		}
		/* AS-external-LSA are silently discarded in stub areas */
		if (area->stub)
			return (0);
		break;
	case LSA_TYPE_LINK_OPAQ:
	case LSA_TYPE_AREA_OPAQ:
	case LSA_TYPE_AS_OPAQ:
		if (len % sizeof(u_int32_t)) {
			log_warnx("lsa_check: bad opaque LSA packet");
			return (0);
		}
		/* Type-11 Opaque-LSA are silently discarded in stub areas */
		if (lsa->hdr.type == LSA_TYPE_AS_OPAQ && area->stub)
			return (0);
		break;
	default:
		log_warnx("lsa_check: unknown type %u", lsa->hdr.type);
		return (0);
	}

	/* MaxAge handling */
	if (lsa->hdr.age == htons(MAX_AGE) && !nbr->self && lsa_find(nbr->iface,
	    lsa->hdr.type, lsa->hdr.ls_id, lsa->hdr.adv_rtr) == NULL &&
	    !rde_nbr_loading(area)) {
		/*
		 * if no neighbor in state Exchange or Loading
		 * ack LSA but don't add it. Needs to be a direct ack.
		 */
		rde_imsg_compose_ospfe(IMSG_LS_ACK, nbr->peerid, 0, &lsa->hdr,
		    sizeof(struct lsa_hdr));
		return (0);
	}

	return (1);
}

int
lsa_router_check(struct lsa *lsa, u_int16_t len)
{
	struct lsa_rtr_link	*rtr_link;
	char			*buf = (char *)lsa;
	u_int16_t		 i, off, nlinks;

	off = sizeof(lsa->hdr) + sizeof(struct lsa_rtr);
	if (off > len) {
		log_warnx("lsa_check: invalid LSA router packet");
		return (0);
	}

	if (lsa->hdr.ls_id != lsa->hdr.adv_rtr) {
		log_warnx("lsa_check: invalid LSA router packet, bad adv_rtr");
		return (0);
	}

	nlinks = ntohs(lsa->data.rtr.nlinks);
	if (nlinks == 0) {
		log_warnx("lsa_check: invalid LSA router packet");
		return (0);
	}
	for (i = 0; i < nlinks; i++) {
		rtr_link = (struct lsa_rtr_link *)(buf + off);
		off += sizeof(struct lsa_rtr_link);
		if (off > len) {
			log_warnx("lsa_check: invalid LSA router packet");
			return (0);
		}
		off += rtr_link->num_tos * sizeof(u_int32_t);
		if (off > len) {
			log_warnx("lsa_check: invalid LSA router packet");
			return (0);
		}
	}

	if (i != nlinks) {
		log_warnx("lsa_check: invalid LSA router packet");
		return (0);
	}
	return (1);
}

int
lsa_self(struct rde_nbr *nbr, struct lsa *new, struct vertex *v)
{
	struct iface	*iface;
	struct lsa	*dummy;

	if (nbr->self)
		return (0);

	if (rde_router_id() == new->hdr.adv_rtr)
		goto self;

	if (new->hdr.type == LSA_TYPE_NETWORK)
		LIST_FOREACH(iface, &nbr->area->iface_list, entry)
		    if (iface->addr.s_addr == new->hdr.ls_id)
			    goto self;

	return (0);

self:
	if (v == NULL) {
		/*
		 * LSA is no longer announced, remove by premature aging.
		 * The problem is that new may not be altered so a copy
		 * needs to be added to the LSA DB first.
		 */
		if ((dummy = malloc(ntohs(new->hdr.len))) == NULL)
			fatal("lsa_self");
		memcpy(dummy, new, ntohs(new->hdr.len));
		dummy->hdr.age = htons(MAX_AGE);
		/*
		 * The clue is that by using the remote nbr as originator
		 * the dummy LSA will be reflooded via the default timeout
		 * handler.
		 */
		(void)lsa_add(rde_nbr_self(nbr->area), dummy);
		return (1);
	}

	/*
	 * LSA is still originated, just reflood it. But we need to create
	 * a new instance by setting the LSA sequence number equal to the
	 * one of new and calling lsa_refresh(). Flooding will be done by the
	 * caller.
	 */
	v->lsa->hdr.seq_num = new->hdr.seq_num;
	lsa_refresh(v);
	return (1);
}

int
lsa_add(struct rde_nbr *nbr, struct lsa *lsa)
{
	struct lsa_tree	*tree;
	struct vertex	*new, *old;
	struct timeval	 tv, now, res;
	int		 update = 1;

	if (lsa->hdr.type == LSA_TYPE_EXTERNAL ||
	    lsa->hdr.type == LSA_TYPE_AS_OPAQ)
		tree = &asext_tree;
	else if (lsa->hdr.type == LSA_TYPE_LINK_OPAQ)
		tree = &nbr->iface->lsa_tree;
	else
		tree = &nbr->area->lsa_tree;

	new = vertex_get(lsa, nbr, tree);
	old = RB_INSERT(lsa_tree, tree, new);

	if (old != NULL) {
		if (old->deleted && evtimer_pending(&old->ev, &tv)) {
			/* new update added before hold time expired */
			gettimeofday(&now, NULL);
			timersub(&tv, &now, &res);

			/* remove old LSA and insert new LSA with delay */
			vertex_free(old);
			RB_INSERT(lsa_tree, tree, new);
			new->deleted = 1;

			if (evtimer_add(&new->ev, &res) != 0)
				fatal("lsa_add");
			return (1);
		}
		if (lsa_equal(new->lsa, old->lsa))
			update = 0;
		vertex_free(old);
		RB_INSERT(lsa_tree, tree, new);
	}

	if (update) {
		if (lsa->hdr.type != LSA_TYPE_EXTERNAL &&
		    lsa->hdr.type != LSA_TYPE_AS_OPAQ)
			nbr->area->dirty = 1;
		start_spf_timer();
	}

	/* timeout handling either MAX_AGE or LS_REFRESH_TIME */
	timerclear(&tv);

	if (nbr->self && ntohs(new->lsa->hdr.age) == DEFAULT_AGE)
		tv.tv_sec = LS_REFRESH_TIME;
	else
		tv.tv_sec = MAX_AGE - ntohs(new->lsa->hdr.age);

	if (evtimer_add(&new->ev, &tv) != 0)
		fatal("lsa_add");
	return (0);
}

void
lsa_del(struct rde_nbr *nbr, struct lsa_hdr *lsa)
{
	struct vertex	*v;
	struct timeval	 tv;

	v = lsa_find(nbr->iface, lsa->type, lsa->ls_id, lsa->adv_rtr);
	if (v == NULL)
		return;

	v->deleted = 1;
	/* hold time to make sure that a new lsa is not added premature */
	timerclear(&tv);
	tv.tv_sec = MIN_LS_INTERVAL;
	if (evtimer_add(&v->ev, &tv) == -1)
		fatal("lsa_del");
}

void
lsa_age(struct vertex *v)
{
	struct timespec	tp;
	time_t		now;
	int		d;
	u_int16_t	age;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	now = tp.tv_sec;

	d = now - v->stamp;
	/* set stamp so that at least new calls work */
	v->stamp = now;

	if (d < 0) {
		log_warnx("lsa_age: time went backwards");
		return;
	}

	age = ntohs(v->lsa->hdr.age);
	if (age + d > MAX_AGE)
		age = MAX_AGE;
	else
		age += d;

	v->lsa->hdr.age = htons(age);
}

struct vertex *
lsa_find(struct iface *iface, u_int8_t type, u_int32_t ls_id, u_int32_t adv_rtr)
{
	struct lsa_tree	*tree;

	if (type == LSA_TYPE_EXTERNAL ||
	    type == LSA_TYPE_AS_OPAQ)
		tree = &asext_tree;
	else if (type == LSA_TYPE_LINK_OPAQ)
		tree = &iface->lsa_tree;
	else
		tree = &iface->area->lsa_tree;

	return lsa_find_tree(tree, type, ls_id, adv_rtr);
}

struct vertex *
lsa_find_area(struct area *area, u_int8_t type, u_int32_t ls_id,
    u_int32_t adv_rtr)
{
	return lsa_find_tree(&area->lsa_tree, type, ls_id, adv_rtr);
}

struct vertex *
lsa_find_tree(struct lsa_tree *tree, u_int16_t type, u_int32_t ls_id,
    u_int32_t adv_rtr)
{
	struct vertex	 key;
	struct vertex	*v;

	key.ls_id = ntohl(ls_id);
	key.adv_rtr = ntohl(adv_rtr);
	key.type = type;

	v = RB_FIND(lsa_tree, tree, &key);

	/* LSA that are deleted are not findable */
	if (v && v->deleted)
		return (NULL);

	if (v)
		lsa_age(v);

	return (v);
}

struct vertex *
lsa_find_net(struct area *area, u_int32_t ls_id)
{
	struct lsa_tree	*tree = &area->lsa_tree;
	struct vertex	*v;

	/* XXX speed me up */
	RB_FOREACH(v, lsa_tree, tree) {
		if (v->lsa->hdr.type == LSA_TYPE_NETWORK &&
		    v->lsa->hdr.ls_id == ls_id) {
			/* LSA that are deleted are not findable */
			if (v->deleted)
				return (NULL);
			lsa_age(v);
			return (v);
		}
	}

	return (NULL);
}

u_int16_t
lsa_num_links(struct vertex *v)
{
	switch (v->type) {
	case LSA_TYPE_ROUTER:
		return (ntohs(v->lsa->data.rtr.nlinks));
	case LSA_TYPE_NETWORK:
		return ((ntohs(v->lsa->hdr.len) - sizeof(struct lsa_hdr)
		    - sizeof(u_int32_t)) / sizeof(struct lsa_net_link));
	default:
		fatalx("lsa_num_links: invalid LSA type");
	}
}

void
lsa_snap(struct rde_nbr *nbr)
{
	struct lsa_tree	*tree = &nbr->area->lsa_tree;
	struct vertex	*v;

	do {
		RB_FOREACH(v, lsa_tree, tree) {
			if (v->deleted)
				continue;
			switch (v->type) {
			case LSA_TYPE_LINK_OPAQ:
			case LSA_TYPE_AREA_OPAQ:
			case LSA_TYPE_AS_OPAQ:
				if (nbr->capa_options & OSPF_OPTION_O)
					break;
				continue;
			}
			lsa_age(v);
			if (ntohs(v->lsa->hdr.age) >= MAX_AGE)
				rde_imsg_compose_ospfe(IMSG_LS_SNAP, nbr->peerid,
				    0, &v->lsa->hdr, ntohs(v->lsa->hdr.len));
			else
				rde_imsg_compose_ospfe(IMSG_DB_SNAPSHOT,
				    nbr->peerid, 0, &v->lsa->hdr,
				    sizeof(struct lsa_hdr));
		}
		if (tree == &asext_tree)
			break;
		if (tree == &nbr->area->lsa_tree)
			tree = &nbr->iface->lsa_tree;
		else if (nbr->area->stub)
			break;
		else
			tree = &asext_tree;
	} while (1);
}

void
lsa_dump(struct lsa_tree *tree, int imsg_type, pid_t pid)
{
	struct vertex	*v;

	RB_FOREACH(v, lsa_tree, tree) {
		if (v->deleted)
			continue;
		lsa_age(v);
		switch (imsg_type) {
		case IMSG_CTL_SHOW_DATABASE:
			break;
		case IMSG_CTL_SHOW_DB_SELF:
			if (v->lsa->hdr.adv_rtr == rde_router_id())
				break;
			continue;
		case IMSG_CTL_SHOW_DB_EXT:
			if (v->type == LSA_TYPE_EXTERNAL)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_NET:
			if (v->type == LSA_TYPE_NETWORK)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_RTR:
			if (v->type == LSA_TYPE_ROUTER)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_SUM:
			if (v->type == LSA_TYPE_SUM_NETWORK)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_ASBR:
			if (v->type == LSA_TYPE_SUM_ROUTER)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_OPAQ:
			if (v->type == LSA_TYPE_LINK_OPAQ ||
			    v->type == LSA_TYPE_AREA_OPAQ ||
			    v->type == LSA_TYPE_AS_OPAQ)
				break;
			continue;
		default:
			log_warnx("lsa_dump: unknown imsg type");
			return;
		}
		rde_imsg_compose_ospfe(imsg_type, 0, pid, &v->lsa->hdr,
		    ntohs(v->lsa->hdr.len));
	}
}

void
lsa_timeout(int fd, short event, void *bula)
{
	struct vertex	*v = bula;
	struct timeval	 tv;

	lsa_age(v);

	if (v->deleted) {
		if (ntohs(v->lsa->hdr.age) >= MAX_AGE) {
			vertex_free(v);
		} else {
			v->deleted = 0;

			/* schedule recalculation of the RIB */
			if (v->type != LSA_TYPE_EXTERNAL &&
			    v->type != LSA_TYPE_AS_OPAQ)
				v->area->dirty = 1;
			start_spf_timer();

			rde_imsg_compose_ospfe(IMSG_LS_FLOOD, v->peerid, 0,
			    v->lsa, ntohs(v->lsa->hdr.len));

			/* timeout handling either MAX_AGE or LS_REFRESH_TIME */
			timerclear(&tv);
			if (v->self)
				tv.tv_sec = LS_REFRESH_TIME;
			else
				tv.tv_sec = MAX_AGE - ntohs(v->lsa->hdr.age);

			if (evtimer_add(&v->ev, &tv) != 0)
				fatal("lsa_timeout");
		}
		return;
	}

	if (v->self && ntohs(v->lsa->hdr.age) < MAX_AGE)
		lsa_refresh(v);

	rde_imsg_compose_ospfe(IMSG_LS_FLOOD, v->peerid, 0,
	    v->lsa, ntohs(v->lsa->hdr.len));
}

void
lsa_refresh(struct vertex *v)
{
	struct timeval	 tv;
	struct timespec	 tp;
	u_int32_t	 seqnum;
	u_int16_t	 len;

	/* refresh LSA by increasing sequence number by one */
	if (v->self && ntohs(v->lsa->hdr.age) >= MAX_AGE)
		/* self originated network that is currently beeing removed */
		v->lsa->hdr.age = htons(MAX_AGE);
	else
		v->lsa->hdr.age = htons(DEFAULT_AGE);
	seqnum = ntohl(v->lsa->hdr.seq_num);
	if (seqnum++ == MAX_SEQ_NUM)
		/* XXX fix me */
		fatalx("sequence number wrapping");
	v->lsa->hdr.seq_num = htonl(seqnum);

	/* recalculate checksum */
	len = ntohs(v->lsa->hdr.len);
	v->lsa->hdr.ls_chksum = 0;
	v->lsa->hdr.ls_chksum = htons(iso_cksum(v->lsa, len, LS_CKSUM_OFFSET));

	clock_gettime(CLOCK_MONOTONIC, &tp);
	v->changed = v->stamp = tp.tv_sec;

	timerclear(&tv);
	tv.tv_sec = LS_REFRESH_TIME;
	if (evtimer_add(&v->ev, &tv) == -1)
		fatal("lsa_refresh");
}

void
lsa_merge(struct rde_nbr *nbr, struct lsa *lsa, struct vertex *v)
{
	struct timeval	tv;
	struct timespec	tp;
	time_t		now;
	u_int16_t	len;

	if (v == NULL) {
		if (lsa_add(nbr, lsa))
			/* delayed update */
			return;
		rde_imsg_compose_ospfe(IMSG_LS_FLOOD, nbr->peerid, 0,
		    lsa, ntohs(lsa->hdr.len));
		return;
	}

	/* set the seq_num to the current one. lsa_refresh() will do the ++ */
	lsa->hdr.seq_num = v->lsa->hdr.seq_num;
	/* recalculate checksum */
	len = ntohs(lsa->hdr.len);
	lsa->hdr.ls_chksum = 0;
	lsa->hdr.ls_chksum = htons(iso_cksum(lsa, len, LS_CKSUM_OFFSET));

	/* compare LSA most header fields are equal so don't check them */
	if (lsa_equal(lsa, v->lsa)) {
		free(lsa);
		return;
	}

	/* overwrite the lsa all other fields are unaffected */
	free(v->lsa);
	v->lsa = lsa;
	start_spf_timer();
	if (v->type != LSA_TYPE_EXTERNAL &&
	    v->type != LSA_TYPE_AS_OPAQ)
		nbr->area->dirty = 1;

	/* set correct timeout for reflooding the LSA */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	now = tp.tv_sec;
	timerclear(&tv);
	if (v->changed + MIN_LS_INTERVAL >= now)
		tv.tv_sec = MIN_LS_INTERVAL;
	if (evtimer_add(&v->ev, &tv) == -1)
		fatal("lsa_merge");
}

void
lsa_remove_invalid_sums(struct area *area)
{
	struct lsa_tree	*tree = &area->lsa_tree;
	struct vertex	*v, *nv;

	/* XXX speed me up */
	for (v = RB_MIN(lsa_tree, tree); v != NULL; v = nv) {
		nv = RB_NEXT(lsa_tree, tree, v);
		if ((v->type == LSA_TYPE_SUM_NETWORK ||
		    v->type == LSA_TYPE_SUM_ROUTER) &&
		    v->self && v->cost == LS_INFINITY &&
		    v->deleted == 0) {
			/*
			 * age the lsa and call lsa_timeout() which will
			 * actually remove it from the database.
			 */
			v->lsa->hdr.age = htons(MAX_AGE);
			lsa_timeout(0, 0, v);
		}
	}
}

void
lsa_generate_stub_sums(struct area *area)
{
	struct rt_node rn;
	struct redistribute *r;
	struct vertex *v;
	struct lsa *lsa;
	struct area *back;

	if (!area->stub)
		return;

	back = rde_backbone_area();
	if (!back || !back->active)
		return;

	SIMPLEQ_FOREACH(r, &area->redist_list, entry) {
		bzero(&rn, sizeof(rn));
		if (r->type == REDIST_DEFAULT) {
			/* setup fake rt_node */
			rn.prefixlen = 0;
			rn.prefix.s_addr = INADDR_ANY;
			rn.cost = r->metric & LSA_METRIC_MASK;

			/* update lsa but only if it was changed */
			v = lsa_find_area(area, LSA_TYPE_SUM_NETWORK,
			    rn.prefix.s_addr, rde_router_id());
			lsa = orig_sum_lsa(&rn, area, LSA_TYPE_SUM_NETWORK, 0);
			lsa_merge(rde_nbr_self(area), lsa, v);

			if (v == NULL)
				v = lsa_find_area(area, LSA_TYPE_SUM_NETWORK,
				    rn.prefix.s_addr, rde_router_id());

			/*
			 * suppressed/deleted routes are not found in the
			 * second lsa_find
			 */
			if (v)
				v->cost = rn.cost;
			return;
		} else if (r->type == (REDIST_DEFAULT | REDIST_NO))
			return;
	}
}

int
lsa_equal(struct lsa *a, struct lsa *b)
{
	/*
	 * compare LSA that already have same type, adv_rtr and ls_id
	 * so not all header need to be compared
	 */
	if (a == NULL || b == NULL)
		return (0);
	if (a->hdr.len != b->hdr.len)
		return (0);
	if (a->hdr.opts != b->hdr.opts)
		return (0);
	/* LSAs with age MAX_AGE are never equal */
	if (a->hdr.age == htons(MAX_AGE) || b->hdr.age == htons(MAX_AGE))
		return (0);
	if (memcmp(&a->data, &b->data, ntohs(a->hdr.len) -
	    sizeof(struct lsa_hdr)))
		return (0);

	return (1);
}
