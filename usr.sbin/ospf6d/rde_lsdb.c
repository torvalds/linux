/*	$OpenBSD: rde_lsdb.c,v 1.48 2023/03/08 04:43:14 guenther Exp $ */

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

#include "ospf6.h"
#include "ospf6d.h"
#include "rde.h"
#include "log.h"

struct vertex	*vertex_get(struct lsa *, struct rde_nbr *, struct lsa_tree *);

int		 lsa_link_check(struct lsa *, u_int16_t);
int		 lsa_intra_a_pref_check(struct lsa *, u_int16_t);
int		 lsa_asext_check(struct lsa *, u_int16_t);
void		 lsa_timeout(int, short, void *);
void		 lsa_refresh(struct vertex *);
int		 lsa_equal(struct lsa *, struct lsa *);
int		 lsa_get_prefix(void *, u_int16_t, struct rt_prefix *);

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
	v->type = ntohs(lsa->hdr.type);
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
vertex_nexthop_add(struct vertex *dst, struct vertex *parent,
    const struct in6_addr *nexthop, u_int32_t ifindex)
{
	struct v_nexthop	*vn;

	if ((vn = calloc(1, sizeof(*vn))) == NULL)
		fatal("vertex_nexthop_add");

	vn->prev = parent;
	if (nexthop)
		vn->nexthop = *nexthop;
	vn->ifindex = ifindex;

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
	u_int32_t	 metric;

	if (len < sizeof(lsa->hdr)) {
		log_warnx("lsa_check: bad packet size");
		return (0);
	}
	if (ntohs(lsa->hdr.len) != len) {
		log_warnx("lsa_check: bad packet length");
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
		log_warnx("lsa_check: bad seq num");
		return (0);
	}

	switch (ntohs(lsa->hdr.type)) {
	case LSA_TYPE_LINK:
		if (!lsa_link_check(lsa, len))
			return (0);
		break;
	case LSA_TYPE_ROUTER:
		if (len < sizeof(lsa->hdr) + sizeof(struct lsa_rtr)) {
			log_warnx("lsa_check: bad LSA rtr packet");
			return (0);
		}
		len -= sizeof(lsa->hdr) + sizeof(struct lsa_rtr);
		if (len % sizeof(struct lsa_rtr_link)) {
			log_warnx("lsa_check: bad LSA rtr packet");
			return (0);
		}
		break;
	case LSA_TYPE_NETWORK:
		if ((len % sizeof(u_int32_t)) ||
		    len < sizeof(lsa->hdr) + sizeof(u_int32_t)) {
			log_warnx("lsa_check: bad LSA network packet");
			return (0);
		}
		break;
	case LSA_TYPE_INTER_A_PREFIX:
		if (len < sizeof(lsa->hdr) + sizeof(lsa->data.pref_sum)) {
			log_warnx("lsa_check: bad LSA prefix summary packet");
			return (0);
		}
		metric = ntohl(lsa->data.pref_sum.metric);
		if (metric & ~LSA_METRIC_MASK) {
			log_warnx("lsa_check: bad LSA prefix summary metric");
			return (0);
		}
		if (lsa_get_prefix(((char *)lsa) + sizeof(lsa->hdr) +
		    sizeof(lsa->data.pref_sum),
		    len - sizeof(lsa->hdr) + sizeof(lsa->data.pref_sum),
		    NULL) == -1) {
			log_warnx("lsa_check: "
			    "invalid LSA prefix summary packet");
			return (0);
		}
		break;
	case LSA_TYPE_INTER_A_ROUTER:
		if (len < sizeof(lsa->hdr) + sizeof(lsa->data.rtr_sum)) {
			log_warnx("lsa_check: bad LSA router summary packet");
			return (0);
		}
		metric = ntohl(lsa->data.rtr_sum.metric);
		if (metric & ~LSA_METRIC_MASK) {
			log_warnx("lsa_check: bad LSA router summary metric");
			return (0);
		}
		break;
	case LSA_TYPE_INTRA_A_PREFIX:
		if (!lsa_intra_a_pref_check(lsa, len))
			return (0);
		break;
	case LSA_TYPE_EXTERNAL:
		/* AS-external-LSA are silently discarded in stub areas */
		if (nbr->area->stub)
			return (0);
		if (!lsa_asext_check(lsa, len))
			return (0);
		break;
	default:
		log_warnx("lsa_check: unknown type %x", ntohs(lsa->hdr.type));
		return (0);
	}

	/* MaxAge handling */
	if (lsa->hdr.age == htons(MAX_AGE) && !nbr->self && lsa_find(nbr->iface,
	    lsa->hdr.type, lsa->hdr.ls_id, lsa->hdr.adv_rtr) == NULL &&
	    !rde_nbr_loading(nbr->area)) {
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
lsa_link_check(struct lsa *lsa, u_int16_t len)
{
	char			*buf = (char *)lsa;
	struct lsa_link		*llink;
	u_int32_t		 i, off, npref;
	int			 rv;

	llink = (struct lsa_link *)(buf + sizeof(lsa->hdr));
	off = sizeof(lsa->hdr) + sizeof(struct lsa_link);
	if (off > len) {
		log_warnx("lsa_link_check: invalid LSA link packet, "
		    "short header");
		return (0);
	}

	len -= off;
	npref = ntohl(llink->numprefix);

	for (i = 0; i < npref; i++) {
		rv = lsa_get_prefix(buf + off, len, NULL);
		if (rv == -1) {
			log_warnx("lsa_link_check: invalid LSA link packet");
			return (0);
		}
		off += rv;
		len -= rv;
	}

	return (1);
}

int
lsa_intra_a_pref_check(struct lsa *lsa, u_int16_t len)
{
	char			*buf = (char *)lsa;
	struct lsa_intra_prefix	*iap;
	u_int32_t		 i, off, npref;
	int			 rv;

	iap = (struct lsa_intra_prefix *)(buf + sizeof(lsa->hdr));
	off = sizeof(lsa->hdr) + sizeof(struct lsa_intra_prefix);
	if (off > len) {
		log_warnx("lsa_intra_a_pref_check: "
		    "invalid LSA intra area prefix packet, short header");
		return (0);
	}

	len -= off;
	npref = ntohs(iap->numprefix);

	for (i = 0; i < npref; i++) {
		rv = lsa_get_prefix(buf + off, len, NULL);
		if (rv == -1) {
			log_warnx("lsa_intra_a_pref_check: "
			    "invalid LSA intra area prefix packet");
			return (0);
		}
		off += rv;
		len -= rv;
	}

	return (1);
}

int
lsa_asext_check(struct lsa *lsa, u_int16_t len)
{
	char			*buf = (char *)lsa;
	struct lsa_asext	*asext;
	struct in6_addr		 fw_addr;
	u_int32_t		 metric;
	u_int16_t		 ref_ls_type;
	int			 rv;
	u_int16_t		 total_len;

	asext = (struct lsa_asext *)(buf + sizeof(lsa->hdr));

	if ((len % sizeof(u_int32_t)) ||
	    len < sizeof(lsa->hdr) + sizeof(*asext)) {
		log_warnx("lsa_asext_check: bad LSA as-external packet size");
		return (0);
	}

	total_len = sizeof(lsa->hdr) + sizeof(*asext);
	rv = lsa_get_prefix(&asext->prefix, len, NULL);
	if (rv == -1) {
		log_warnx("lsa_asext_check: bad LSA as-external packet");
		return (0);
	}
	total_len += rv - sizeof(struct lsa_prefix);

	metric = ntohl(asext->metric);
	if (metric & LSA_ASEXT_F_FLAG) {
		if (total_len + sizeof(fw_addr) < len) {
			bcopy(buf + total_len, &fw_addr, sizeof(fw_addr));
			if (IN6_IS_ADDR_UNSPECIFIED(&fw_addr) ||
			    IN6_IS_ADDR_LINKLOCAL(&fw_addr)) {
				log_warnx("lsa_asext_check: bad LSA "
				    "as-external forwarding address");
				return (0);
			}
		}
		total_len += sizeof(fw_addr);
	}

	if (metric & LSA_ASEXT_T_FLAG)
		total_len += sizeof(u_int32_t);

	ref_ls_type = asext->prefix.metric;
	if (ref_ls_type != 0)
		total_len += sizeof(u_int32_t);

	if (len != total_len) {
		log_warnx("lsa_asext_check: bad LSA as-external length");
		return (0);
	}

	return (1);
}

int
lsa_self(struct rde_nbr *nbr, struct lsa *lsa, struct vertex *v)
{
	struct lsa	*dummy;

	if (nbr->self)
		return (0);

	if (rde_router_id() != lsa->hdr.adv_rtr)
		return (0);

	if (v == NULL) {
		/* LSA is no longer announced, remove by premature aging.
	 	 * The LSA may not be altered because the caller may still
		 * use it, so a copy needs to be added to the LSDB.
		 * The copy will be reflooded via the default timeout handler.
		 */
		if ((dummy = malloc(ntohs(lsa->hdr.len))) == NULL)
			fatal("lsa_self");
		memcpy(dummy, lsa, ntohs(lsa->hdr.len));
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
	v->lsa->hdr.seq_num = lsa->hdr.seq_num;
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

	if (LSA_IS_SCOPE_AS(ntohs(lsa->hdr.type)))
		tree = &asext_tree;
	else if (LSA_IS_SCOPE_AREA(ntohs(lsa->hdr.type)))
		tree = &nbr->area->lsa_tree;
	else if (LSA_IS_SCOPE_LLOCAL(ntohs(lsa->hdr.type)))
		tree = &nbr->iface->lsa_tree;
	else
		fatalx("%s: unknown scope type", __func__);

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
		if (ntohs(lsa->hdr.type) == LSA_TYPE_LINK)
			orig_intra_area_prefix_lsas(nbr->area);
		if (ntohs(lsa->hdr.type) != LSA_TYPE_EXTERNAL)
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
		fatal("lsa_add: evtimer_add()");
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
lsa_find(struct iface *iface, u_int16_t type, u_int32_t ls_id,
    u_int32_t adv_rtr)
{
	struct lsa_tree	*tree;

	if (LSA_IS_SCOPE_AS(ntohs(type)))
		tree = &asext_tree;
	else if (LSA_IS_SCOPE_AREA(ntohs(type)))
		tree = &iface->area->lsa_tree;
	else if (LSA_IS_SCOPE_LLOCAL(ntohs(type)))
		tree = &iface->lsa_tree;
	else
		fatalx("unknown scope type");

	return lsa_find_tree(tree, type, ls_id, adv_rtr);

}

struct vertex *
lsa_find_tree(struct lsa_tree *tree, u_int16_t type, u_int32_t ls_id,
    u_int32_t adv_rtr)
{
	struct vertex	 key;
	struct vertex	*v;

	key.ls_id = ntohl(ls_id);
	key.adv_rtr = ntohl(adv_rtr);
	key.type = ntohs(type);

	v = RB_FIND(lsa_tree, tree, &key);

	/* LSA that are deleted are not findable */
	if (v && v->deleted)
		return (NULL);

	if (v)
		lsa_age(v);

	return (v);
}

struct vertex *
lsa_find_rtr(struct area *area, u_int32_t rtr_id)
{
	return lsa_find_rtr_frag(area, rtr_id, 0);
}

struct vertex *
lsa_find_rtr_frag(struct area *area, u_int32_t rtr_id, unsigned int n)
{
	struct vertex	*v;
	struct vertex	 key;
	unsigned int	 i;

	key.ls_id = 0;
	key.adv_rtr = ntohl(rtr_id);
	key.type = LSA_TYPE_ROUTER;

	i = 0;
	v = RB_NFIND(lsa_tree, &area->lsa_tree, &key);
	while (v) {
		if (v->type != LSA_TYPE_ROUTER ||
		    v->adv_rtr != ntohl(rtr_id)) {
			/* no more interesting LSAs */
			v = NULL;
			break;
		}
		if (!v->deleted) {
			if (i >= n)
				break;
			i++;
		}
		v = RB_NEXT(lsa_tree, &area->lsa_tree, v);
	}

	if (v) {
		if (i == n)
			lsa_age(v);
		else
			v = NULL;
	}

	return (v);
}

u_int32_t
lsa_find_lsid(struct lsa_tree *tree, int (*cmp)(struct lsa *, struct lsa *),
    struct lsa *lsa)
{
#define MIN(x, y)	((x) < (y) ? (x) : (y))
	struct vertex	*v;
	struct vertex	 key;
	u_int32_t	 min, cur;

	key.ls_id = 0;
	key.adv_rtr = ntohl(lsa->hdr.adv_rtr);
	key.type = ntohs(lsa->hdr.type);

	cur = 0;
	min = 0xffffffffU;
	v = RB_NFIND(lsa_tree, tree, &key);
	while (v) {
		if (v->type != key.type ||
		    v->adv_rtr != key.adv_rtr) {
			/* no more interesting LSAs */
			min = MIN(min, cur + 1);
			return (htonl(min));
		}
		if (cmp(lsa, v->lsa) == 0) {
			/* match, return this ls_id */
			return (htonl(v->ls_id));
		}
		if (v->ls_id > cur + 1)
			min = cur + 1;
		cur = v->ls_id;
		if (cur + 1 < cur)
			fatalx("King Bula sez: somebody got to many LSA");
		v = RB_NEXT(lsa_tree, tree, v);
	}
	min = MIN(min, cur + 1);
	return (htonl(min));
#undef MIN
}

u_int16_t
lsa_num_links(struct vertex *v)
{
	unsigned int	 n = 1;
	u_int16_t	 nlinks = 0;

	switch (v->type) {
	case LSA_TYPE_ROUTER:
		do {
			nlinks += ((ntohs(v->lsa->hdr.len) -
			    sizeof(struct lsa_hdr) - sizeof(struct lsa_rtr)) /
			    sizeof(struct lsa_rtr_link));
			v = lsa_find_rtr_frag(v->area, htonl(v->adv_rtr), n++);
		} while (v);
		return nlinks;
	case LSA_TYPE_NETWORK:
		return ((ntohs(v->lsa->hdr.len) - sizeof(struct lsa_hdr) -
		    sizeof(struct lsa_net)) / sizeof(struct lsa_net_link));
	default:
		fatalx("lsa_num_links: invalid LSA type");
	}

	return (0);
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
			lsa_age(v);
			if (ntohs(v->lsa->hdr.age) >= MAX_AGE) {
				rde_imsg_compose_ospfe(IMSG_LS_SNAP,
				    nbr->peerid, 0, &v->lsa->hdr,
				    ntohs(v->lsa->hdr.len));
			} else {
				rde_imsg_compose_ospfe(IMSG_DB_SNAPSHOT,
				    nbr->peerid, 0, &v->lsa->hdr,
				    sizeof(struct lsa_hdr));
			}
		}
		if (tree == &asext_tree)
			break;
		if (tree == &nbr->area->lsa_tree)
			tree = &nbr->iface->lsa_tree;
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
		case IMSG_CTL_SHOW_DB_LINK:
			if (v->type == LSA_TYPE_LINK)
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
		case IMSG_CTL_SHOW_DB_INTRA:
			if (v->type == LSA_TYPE_INTRA_A_PREFIX)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_SUM:
			if (v->type == LSA_TYPE_INTER_A_PREFIX)
				break;
			continue;
		case IMSG_CTL_SHOW_DB_ASBR:
			if (v->type == LSA_TYPE_INTER_A_ROUTER)
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
			if (ntohs(v->lsa->hdr.type) == LSA_TYPE_LINK)
				orig_intra_area_prefix_lsas(v->area);
			if (ntohs(v->lsa->hdr.type) != LSA_TYPE_EXTERNAL)
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
	if (v->type == LSA_TYPE_LINK)
		orig_intra_area_prefix_lsas(nbr->area);
	if (v->type != LSA_TYPE_EXTERNAL)
		nbr->area->dirty = 1;
	start_spf_timer();

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
		if ((v->type == LSA_TYPE_INTER_A_PREFIX ||
		    v->type == LSA_TYPE_INTER_A_ROUTER) &&
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
	/* LSAs with age MAX_AGE are never equal */
	if (a->hdr.age == htons(MAX_AGE) || b->hdr.age == htons(MAX_AGE))
		return (0);
	if (memcmp(&a->data, &b->data, ntohs(a->hdr.len) -
	    sizeof(struct lsa_hdr)))
		return (0);

	return (1);
}

int
lsa_get_prefix(void *buf, u_int16_t len, struct rt_prefix *p)
{
	struct lsa_prefix	*lp = buf;
	u_int32_t		*buf32, *addr = NULL;
	u_int8_t		 prefixlen;
	u_int16_t		 consumed;

	if (len < sizeof(*lp))
		return (-1);

	prefixlen = lp->prefixlen;

	if (p) {
		bzero(p, sizeof(*p));
		p->prefixlen = lp->prefixlen;
		p->options = lp->options;
		p->metric = ntohs(lp->metric);
		addr = (u_int32_t *)&p->prefix;
	}

	buf32 = (u_int32_t *)(lp + 1);
	consumed = sizeof(*lp);

	for (prefixlen = LSA_PREFIXSIZE(prefixlen) / sizeof(u_int32_t);
	    prefixlen > 0; prefixlen--) {
		if (len < consumed + sizeof(u_int32_t))
			return (-1);
		if (addr)
			*addr++ = *buf32++;
		consumed += sizeof(u_int32_t);
	}

	return (consumed);
}
