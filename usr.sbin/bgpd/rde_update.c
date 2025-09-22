/*	$OpenBSD: rde_update.c,v 1.176 2025/06/04 09:12:34 claudio Exp $ */

/*
 * Copyright (c) 2004 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/tree.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"
#include "log.h"

enum up_state {
	UP_OK,
	UP_ERR_LIMIT,
	UP_FILTERED,
	UP_EXCLUDED,
};

static struct community	comm_no_advertise = {
	.flags = COMMUNITY_TYPE_BASIC,
	.data1 = COMMUNITY_WELLKNOWN,
	.data2 = COMMUNITY_NO_ADVERTISE
};
static struct community	comm_no_export = {
	.flags = COMMUNITY_TYPE_BASIC,
	.data1 = COMMUNITY_WELLKNOWN,
	.data2 = COMMUNITY_NO_EXPORT
};
static struct community	comm_no_expsubconfed = {
	.flags = COMMUNITY_TYPE_BASIC,
	.data1 = COMMUNITY_WELLKNOWN,
	.data2 = COMMUNITY_NO_EXPSUBCONFED
};

static void up_prep_adjout(struct rde_peer *, struct filterstate *, uint8_t);

static int
up_test_update(struct rde_peer *peer, struct prefix *p)
{
	struct rde_aspath	*asp;
	struct rde_community	*comm;
	struct rde_peer		*frompeer;

	frompeer = prefix_peer(p);
	asp = prefix_aspath(p);
	comm = prefix_communities(p);

	if (asp == NULL || asp->flags & F_ATTR_PARSE_ERR)
		fatalx("try to send out a botched path");
	if (asp->flags & (F_ATTR_LOOP | F_ATTR_OTC_LEAK))
		fatalx("try to send out a looped path");

	if (peer == frompeer)
		/* Do not send routes back to sender */
		return (0);

	if (!frompeer->conf.ebgp && !peer->conf.ebgp) {
		/*
		 * route reflector redistribution rules:
		 * 1. if announce is set                -> announce
		 * 2. from non-client, to non-client    -> no
		 * 3. from client, to non-client        -> yes
		 * 4. from non-client, to client        -> yes
		 * 5. from client, to client            -> yes
		 */
		if (frompeer->conf.reflector_client == 0 &&
		    peer->conf.reflector_client == 0 &&
		    (asp->flags & F_PREFIX_ANNOUNCED) == 0)
			/* Do not redistribute updates to ibgp peers */
			return (0);
	}

	/*
	 * With "transparent-as yes" set do not filter based on
	 * well-known communities. Instead pass them on to the client.
	 */
	if (peer->flags & PEERFLAG_TRANS_AS)
		return (1);

	/* well-known communities */
	if (community_match(comm, &comm_no_advertise, NULL))
		return (0);
	if (peer->conf.ebgp) {
		if (community_match(comm, &comm_no_export, NULL))
			return (0);
		if (community_match(comm, &comm_no_expsubconfed, NULL))
			return (0);
	}

	return (1);
}

/* RFC9234 open policy handling */
static int
up_enforce_open_policy(struct rde_peer *peer, struct filterstate *state,
    uint8_t aid)
{
	/* only for IPv4 and IPv6 unicast */
	if (aid != AID_INET && aid != AID_INET6)
		return 0;

	/*
	 * do not propagate (consider it filtered) if OTC is present and
	 * local role is peer, customer or rs-client.
	 */
	if (peer->role == ROLE_PEER || peer->role == ROLE_CUSTOMER ||
	    peer->role == ROLE_RS_CLIENT)
		if (state->aspath.flags & F_ATTR_OTC)
			return 1;

	/*
	 * add OTC attribute if not present towards peers, customers and
	 * rs-clients (local roles peer, provider, rs).
	 */
	if (peer->role == ROLE_PEER || peer->role == ROLE_PROVIDER ||
	    peer->role == ROLE_RS)
		if ((state->aspath.flags & F_ATTR_OTC) == 0) {
			uint32_t tmp;

			tmp = htonl(peer->conf.local_as);
			if (attr_optadd(&state->aspath,
			    ATTR_OPTIONAL|ATTR_TRANSITIVE, ATTR_OTC,
			    &tmp, sizeof(tmp)) == -1)
				log_peer_warnx(&peer->conf,
				    "failed to add OTC attribute");
			state->aspath.flags |= F_ATTR_OTC;
		}

	return 0;
}

/*
 * Process a single prefix by passing it through the various filter stages
 * and if not filtered out update the Adj-RIB-Out. Returns:
 * - UP_OK if prefix was added
 * - UP_ERR_LIMIT if the peer outbound prefix limit was reached
 * - UP_FILTERED if prefix was filtered out
 * - UP_EXCLUDED if prefix was excluded because of up_test_update()
 */
static enum up_state
up_process_prefix(struct rde_peer *peer, struct prefix *new, struct prefix *p)
{
	struct filterstate state;
	struct bgpd_addr addr;
	int excluded = 0;

	/*
	 * up_test_update() needs to run before the output filters
	 * else the well-known communities won't work properly.
	 * The output filters would not be able to add well-known
	 * communities.
	 */
	if (!up_test_update(peer, new))
		excluded = 1;

	rde_filterstate_prep(&state, new);
	pt_getaddr(new->pt, &addr);
	if (rde_filter(peer->out_rules, peer, prefix_peer(new), &addr,
	    new->pt->prefixlen, &state) == ACTION_DENY) {
		rde_filterstate_clean(&state);
		return UP_FILTERED;
	}

	/* Open Policy Check: acts like an output filter */
	if (up_enforce_open_policy(peer, &state, new->pt->aid)) {
		rde_filterstate_clean(&state);
		return UP_FILTERED;
	}

	if (excluded) {
		rde_filterstate_clean(&state);
		return UP_EXCLUDED;
	}

	/* from here on we know this is an update */
	if (p == (void *)-1)
		p = prefix_adjout_get(peer, new->path_id_tx, new->pt);

	up_prep_adjout(peer, &state, new->pt->aid);
	prefix_adjout_update(p, peer, &state, new->pt, new->path_id_tx);
	rde_filterstate_clean(&state);

	/* max prefix checker outbound */
	if (peer->conf.max_out_prefix &&
	    peer->stats.prefix_out_cnt > peer->conf.max_out_prefix) {
		log_peer_warnx(&peer->conf,
		    "outbound prefix limit reached (>%u/%u)",
		    peer->stats.prefix_out_cnt, peer->conf.max_out_prefix);
		rde_update_err(peer, ERR_CEASE,
		    ERR_CEASE_MAX_SENT_PREFIX, NULL);
		return UP_ERR_LIMIT;
	}

	return UP_OK;
}

void
up_generate_updates(struct rde_peer *peer, struct rib_entry *re)
{
	struct prefix		*new, *p;

	p = prefix_adjout_first(peer, re->prefix);

	new = prefix_best(re);
	while (new != NULL) {
		switch (up_process_prefix(peer, new, p)) {
		case UP_OK:
		case UP_ERR_LIMIT:
			return;
		case UP_FILTERED:
			if (peer->flags & PEERFLAG_EVALUATE_ALL) {
				new = TAILQ_NEXT(new, entry.list.rib);
				if (new != NULL && prefix_eligible(new))
					continue;
			}
			goto done;
		case UP_EXCLUDED:
			goto done;
		}
	}

done:
	/* withdraw prefix */
	if (p != NULL)
		prefix_adjout_withdraw(p);
}

/*
 * Generate updates for the add-path send case. Depending on the
 * peer eval settings prefixes are selected and distributed.
 * This highly depends on the Adj-RIB-Out to handle prefixes with no
 * changes gracefully. It may be possible to improve the API so that
 * less churn is needed.
 */
void
up_generate_addpath(struct rde_peer *peer, struct rib_entry *re)
{
	struct prefix		*head, *new, *p;
	int			maxpaths = 0, extrapaths = 0, extra;
	int			checkmode = 1;

	head = prefix_adjout_first(peer, re->prefix);

	/* mark all paths as stale */
	for (p = head; p != NULL; p = prefix_adjout_next(peer, p))
		p->flags |= PREFIX_FLAG_STALE;

	/* update paths */
	new = prefix_best(re);
	while (new != NULL) {
		/* check limits and stop when a limit is reached */
		if (peer->eval.maxpaths != 0 &&
		    maxpaths >= peer->eval.maxpaths)
			break;
		if (peer->eval.extrapaths != 0 &&
		    extrapaths >= peer->eval.extrapaths)
			break;

		extra = 1;
		if (checkmode) {
			switch (peer->eval.mode) {
			case ADDPATH_EVAL_BEST:
				if (new->dmetric == PREFIX_DMETRIC_BEST)
					extra = 0;
				else
					checkmode = 0;
				break;
			case ADDPATH_EVAL_ECMP:
				if (new->dmetric == PREFIX_DMETRIC_BEST ||
				    new->dmetric == PREFIX_DMETRIC_ECMP)
					extra = 0;
				else
					checkmode = 0;
				break;
			case ADDPATH_EVAL_AS_WIDE:
				if (new->dmetric == PREFIX_DMETRIC_BEST ||
				    new->dmetric == PREFIX_DMETRIC_ECMP ||
				    new->dmetric == PREFIX_DMETRIC_AS_WIDE)
					extra = 0;
				else
					checkmode = 0;
				break;
			case ADDPATH_EVAL_ALL:
				/* nothing to check */
				checkmode = 0;
				break;
			default:
				fatalx("unknown add-path eval mode");
			}
		}

		switch (up_process_prefix(peer, new, (void *)-1)) {
		case UP_OK:
			maxpaths++;
			extrapaths += extra;
			break;
		case UP_FILTERED:
		case UP_EXCLUDED:
			break;
		case UP_ERR_LIMIT:
			/* just give up */
			return;
		}

		/* only allow valid prefixes */
		new = TAILQ_NEXT(new, entry.list.rib);
		if (new == NULL || !prefix_eligible(new))
			break;
	}

	/* withdraw stale paths */
	for (p = head; p != NULL; p = prefix_adjout_next(peer, p)) {
		if (p->flags & PREFIX_FLAG_STALE)
			prefix_adjout_withdraw(p);
	}
}

/*
 * Generate updates for the add-path send all case. Since all prefixes
 * are distributed just remove old and add new.
 */
void
up_generate_addpath_all(struct rde_peer *peer, struct rib_entry *re,
    struct prefix *new, struct prefix *old)
{
	struct prefix		*p, *head = NULL;
	int			all = 0;

	/*
	 * if old and new are NULL then insert all prefixes from best,
	 * clearing old routes in the process
	 */
	if (old == NULL && new == NULL) {
		/* mark all paths as stale */
		head = prefix_adjout_first(peer, re->prefix);
		for (p = head; p != NULL; p = prefix_adjout_next(peer, p))
			p->flags |= PREFIX_FLAG_STALE;

		new = prefix_best(re);
		all = 1;
	}

	if (new != NULL && !prefix_eligible(new)) {
		/* only allow valid prefixes */
		new = NULL;
	}

	if (old != NULL) {
		/* withdraw stale paths */
		p = prefix_adjout_get(peer, old->path_id_tx, old->pt);
		if (p != NULL)
			prefix_adjout_withdraw(p);
	}

	/* add new path (or multiple if all is set) */
	while (new != NULL) {
		switch (up_process_prefix(peer, new, (void *)-1)) {
		case UP_OK:
		case UP_FILTERED:
		case UP_EXCLUDED:
			break;
		case UP_ERR_LIMIT:
			/* just give up */
			return;
		}

		if (!all)
			break;

		/* only allow valid prefixes */
		new = TAILQ_NEXT(new, entry.list.rib);
		if (new == NULL || !prefix_eligible(new))
			break;
	}

	if (all) {
		/* withdraw stale paths */
		for (p = head; p != NULL; p = prefix_adjout_next(peer, p)) {
			if (p->flags & PREFIX_FLAG_STALE)
				prefix_adjout_withdraw(p);
		}
	}
}

/* send a default route to the specified peer */
void
up_generate_default(struct rde_peer *peer, uint8_t aid)
{
	extern struct rde_peer	*peerself;
	struct filterstate	 state;
	struct rde_aspath	*asp;
	struct prefix		*p;
	struct pt_entry		*pte;
	struct bgpd_addr	 addr;

	if (peer->capa.mp[aid] == 0)
		return;

	rde_filterstate_init(&state);
	asp = &state.aspath;
	asp->aspath = aspath_get(NULL, 0);
	asp->origin = ORIGIN_IGP;
	rde_filterstate_set_vstate(&state, ROA_NOTFOUND, ASPA_NEVER_KNOWN);
	/* the other default values are OK, nexthop is once again NULL */

	/*
	 * XXX apply default overrides. Not yet possible, mainly a parse.y
	 * problem.
	 */
	/* rde_apply_set(asp, peerself, peerself, set, af); */

	memset(&addr, 0, sizeof(addr));
	addr.aid = aid;
	p = prefix_adjout_lookup(peer, &addr, 0);

	/* outbound filter as usual */
	if (rde_filter(peer->out_rules, peer, peerself, &addr, 0, &state) ==
	    ACTION_DENY) {
		rde_filterstate_clean(&state);
		return;
	}

	up_prep_adjout(peer, &state, addr.aid);
	/* can't use pt_fill here since prefix_adjout_update keeps a ref */
	pte = pt_get(&addr, 0);
	if (pte == NULL)
		pte = pt_add(&addr, 0);
	prefix_adjout_update(p, peer, &state, pte, 0);
	rde_filterstate_clean(&state);

	/* max prefix checker outbound */
	if (peer->conf.max_out_prefix &&
	    peer->stats.prefix_out_cnt > peer->conf.max_out_prefix) {
		log_peer_warnx(&peer->conf,
		    "outbound prefix limit reached (>%u/%u)",
		    peer->stats.prefix_out_cnt, peer->conf.max_out_prefix);
		rde_update_err(peer, ERR_CEASE,
		    ERR_CEASE_MAX_SENT_PREFIX, NULL);
	}
}

static struct bgpd_addr *
up_get_nexthop(struct rde_peer *peer, struct filterstate *state, uint8_t aid)
{
	struct bgpd_addr *peer_local = NULL;

	switch (aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		if (peer_has_ext_nexthop(peer, aid) &&
		    peer->remote_addr.aid == AID_INET6)
			peer_local = &peer->local_v6_addr;
		else if (peer->local_v4_addr.aid == AID_INET)
			peer_local = &peer->local_v4_addr;
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		if (peer->local_v6_addr.aid == AID_INET6)
			peer_local = &peer->local_v6_addr;
		break;
	case AID_EVPN:
		if (peer->local_v4_addr.aid == AID_INET)
			peer_local = &peer->local_v4_addr;
		else if (peer->local_v6_addr.aid == AID_INET6)
			peer_local = &peer->local_v6_addr;
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		/* flowspec has no nexthop */
		return (NULL);
	default:
		fatalx("%s, bad AID %s", __func__, aid2str(aid));
	}

	if (state->nhflags & NEXTHOP_SELF) {
		/*
		 * Forcing the nexthop to self is always possible
		 * and has precedence over other flags.
		 */
		return (peer_local);
	} else if (!peer->conf.ebgp) {
		/*
		 * in the ibgp case the nexthop is normally not
		 * modified unless it points at the peer itself.
		 */
		if (state->nexthop == NULL) {
			/* announced networks without explicit nexthop set */
			return (peer_local);
		}
		/*
		 * per RFC: if remote peer address is equal to the nexthop set
		 * the nexthop to our local address. This reduces the risk of
		 * routing loops. This overrides NEXTHOP_NOMODIFY.
		 */
		if (memcmp(&state->nexthop->exit_nexthop,
		    &peer->remote_addr, sizeof(peer->remote_addr)) == 0) {
			return (peer_local);
		}
		return (&state->nexthop->exit_nexthop);
	} else if (peer->conf.distance == 1) {
		/*
		 * In the ebgp directly connected case never send
		 * out a nexthop that is outside of the connected
		 * network of the peer. No matter what flags are
		 * set. This follows section 5.1.3 of RFC 4271.
		 * So just check if the nexthop is in the same net
		 * is enough here.
		 */
		if (state->nexthop != NULL &&
		    state->nexthop->flags & NEXTHOP_CONNECTED &&
		    prefix_compare(&peer->remote_addr,
		    &state->nexthop->nexthop_net,
		    state->nexthop->nexthop_netlen) == 0) {
			/* nexthop and peer are in the same net */
			return (&state->nexthop->exit_nexthop);
		}
		return (peer_local);
	} else {
		/*
		 * For ebgp multihop make it possible to overrule
		 * the sent nexthop by setting NEXTHOP_NOMODIFY.
		 * Similar to the ibgp case there is no same net check
		 * needed but still ensure that the nexthop is not
		 * pointing to the peer itself.
		 */
		if (state->nhflags & NEXTHOP_NOMODIFY &&
		    state->nexthop != NULL &&
		    memcmp(&state->nexthop->exit_nexthop,
		    &peer->remote_addr, sizeof(peer->remote_addr)) != 0) {
			/* no modify flag set and nexthop not peer addr */
			return (&state->nexthop->exit_nexthop);
		}
		return (peer_local);
	}
}

static void
up_prep_adjout(struct rde_peer *peer, struct filterstate *state, uint8_t aid)
{
	struct bgpd_addr *nexthop;
	struct nexthop *nh = NULL;
	u_char *np;
	uint16_t nl;

	/* prepend local AS number for eBGP sessions. */
	if (peer->conf.ebgp && (peer->flags & PEERFLAG_TRANS_AS) == 0) {
		uint32_t prep_as = peer->conf.local_as;
		np = aspath_prepend(state->aspath.aspath, prep_as, 1, &nl);
		aspath_put(state->aspath.aspath);
		state->aspath.aspath = aspath_get(np, nl);
		free(np);
	}

	/* update nexthop */
	nexthop = up_get_nexthop(peer, state, aid);
	if (nexthop != NULL)
		nh = nexthop_get(nexthop);
	nexthop_unref(state->nexthop);
	state->nexthop = nh;
	state->nhflags = 0;
}


static int
up_generate_attr(struct ibuf *buf, struct rde_peer *peer,
    struct rde_aspath *asp, struct rde_community *comm, struct nexthop *nh,
    uint8_t aid)
{
	struct attr	*oa = NULL, *newaggr = NULL;
	u_char		*pdata;
	uint32_t	 tmp32;
	int		 flags, neednewpath = 0, rv;
	uint16_t	 plen;
	uint8_t		 oalen = 0, type;

	if (asp->others_len > 0)
		oa = asp->others[oalen++];

	/* dump attributes in ascending order */
	for (type = ATTR_ORIGIN; type < 255; type++) {
		while (oa && oa->type < type) {
			if (oalen < asp->others_len)
				oa = asp->others[oalen++];
			else
				oa = NULL;
		}

		switch (type) {
		/*
		 * Attributes stored in rde_aspath
		 */
		case ATTR_ORIGIN:
			if (attr_writebuf(buf, ATTR_WELL_KNOWN,
			    ATTR_ORIGIN, &asp->origin, 1) == -1)
				return -1;
			break;
		case ATTR_ASPATH:
			plen = aspath_length(asp->aspath);
			pdata = aspath_dump(asp->aspath);

			if (!peer_has_as4byte(peer))
				pdata = aspath_deflate(pdata, &plen,
				    &neednewpath);
			rv = attr_writebuf(buf, ATTR_WELL_KNOWN,
			    ATTR_ASPATH, pdata, plen);
			if (!peer_has_as4byte(peer))
				free(pdata);

			if (rv == -1)
				return -1;
			break;
		case ATTR_NEXTHOP:
			switch (aid) {
			case AID_INET:
				if (nh == NULL)
					return -1;
				if (nh->exit_nexthop.aid != AID_INET) {
					if (peer_has_ext_nexthop(peer, aid))
						break;
					return -1;
				}
				if (attr_writebuf(buf, ATTR_WELL_KNOWN,
				    ATTR_NEXTHOP, &nh->exit_nexthop.v4,
				    sizeof(nh->exit_nexthop.v4)) == -1)
					return -1;
				break;
			default:
				break;
			}
			break;
		case ATTR_MED:
			/*
			 * The old MED from other peers MUST not be announced
			 * to others unless the MED is originating from us or
			 * the peer is an IBGP one. Only exception are routers
			 * with "transparent-as yes" set.
			 */
			if (asp->flags & F_ATTR_MED && (!peer->conf.ebgp ||
			    asp->flags & F_ATTR_MED_ANNOUNCE ||
			    peer->flags & PEERFLAG_TRANS_AS)) {
				tmp32 = htonl(asp->med);
				if (attr_writebuf(buf, ATTR_OPTIONAL,
				    ATTR_MED, &tmp32, 4) == -1)
					return -1;
			}
			break;
		case ATTR_LOCALPREF:
			if (!peer->conf.ebgp) {
				/* local preference, only valid for ibgp */
				tmp32 = htonl(asp->lpref);
				if (attr_writebuf(buf, ATTR_WELL_KNOWN,
				    ATTR_LOCALPREF, &tmp32, 4) == -1)
					return -1;
			}
			break;
		/*
		 * Communities are stored in struct rde_community
		 */
		case ATTR_COMMUNITIES:
		case ATTR_EXT_COMMUNITIES:
		case ATTR_LARGE_COMMUNITIES:
			if (community_writebuf(comm, type, peer->conf.ebgp,
			    buf) == -1)
				return -1;
			break;
		/*
		 * NEW to OLD conversion when sending stuff to a 2byte AS peer
		 */
		case ATTR_AS4_PATH:
			if (neednewpath) {
				plen = aspath_length(asp->aspath);
				pdata = aspath_dump(asp->aspath);

				flags = ATTR_OPTIONAL|ATTR_TRANSITIVE;
				if (!(asp->flags & F_PREFIX_ANNOUNCED))
					flags |= ATTR_PARTIAL;
				if (plen != 0)
					if (attr_writebuf(buf, flags,
					    ATTR_AS4_PATH, pdata, plen) == -1)
						return -1;
			}
			break;
		case ATTR_AS4_AGGREGATOR:
			if (newaggr) {
				flags = ATTR_OPTIONAL|ATTR_TRANSITIVE;
				if (!(asp->flags & F_PREFIX_ANNOUNCED))
					flags |= ATTR_PARTIAL;
				if (attr_writebuf(buf, flags,
				    ATTR_AS4_AGGREGATOR, newaggr->data,
				    newaggr->len) == -1)
					return -1;
			}
			break;
		/*
		 * multiprotocol attributes are handled elsewhere
		 */
		case ATTR_MP_REACH_NLRI:
		case ATTR_MP_UNREACH_NLRI:
			break;
		/*
		 * dump all other path attributes. Following rules apply:
		 *  1. well-known attrs: ATTR_ATOMIC_AGGREGATE and
		 *     ATTR_AGGREGATOR pass unmodified (enforce flags
		 *     to correct values). Actually ATTR_AGGREGATOR may be
		 *     deflated for OLD 2-byte peers.
		 *  2. non-transitive attrs: don't re-announce to ebgp peers
		 *  3. transitive known attrs: announce unmodified
		 *  4. transitive unknown attrs: set partial bit and re-announce
		 */
		case ATTR_ATOMIC_AGGREGATE:
			if (oa == NULL || oa->type != type)
				break;
			if (attr_writebuf(buf, ATTR_WELL_KNOWN,
			    ATTR_ATOMIC_AGGREGATE, NULL, 0) == -1)
				return -1;
			break;
		case ATTR_AGGREGATOR:
			if (oa == NULL || oa->type != type)
				break;
			if ((!(oa->flags & ATTR_TRANSITIVE)) &&
			    peer->conf.ebgp)
				break;
			if (!peer_has_as4byte(peer)) {
				/* need to deflate the aggregator */
				uint8_t		t[6];
				uint16_t	tas;

				if ((!(oa->flags & ATTR_TRANSITIVE)) &&
				    peer->conf.ebgp)
					break;

				memcpy(&tmp32, oa->data, sizeof(tmp32));
				if (ntohl(tmp32) > USHRT_MAX) {
					tas = htons(AS_TRANS);
					newaggr = oa;
				} else
					tas = htons(ntohl(tmp32));

				memcpy(t, &tas, sizeof(tas));
				memcpy(t + sizeof(tas),
				    oa->data + sizeof(tmp32),
				    oa->len - sizeof(tmp32));
				if (attr_writebuf(buf, oa->flags,
				    oa->type, &t, sizeof(t)) == -1)
					return -1;
			} else {
				if (attr_writebuf(buf, oa->flags, oa->type,
				    oa->data, oa->len) == -1)
					return -1;
			}
			break;
		case ATTR_ORIGINATOR_ID:
		case ATTR_CLUSTER_LIST:
		case ATTR_OTC:
			if (oa == NULL || oa->type != type)
				break;
			if ((!(oa->flags & ATTR_TRANSITIVE)) &&
			    peer->conf.ebgp)
				break;
			if (attr_writebuf(buf, oa->flags, oa->type,
			    oa->data, oa->len) == -1)
				return -1;
			break;
		default:
			if (oa == NULL && type >= ATTR_FIRST_UNKNOWN)
				/* there is no attribute left to dump */
				return (0);

			if (oa == NULL || oa->type != type)
				break;
			/* unknown attribute */
			if (!(oa->flags & ATTR_TRANSITIVE)) {
				/*
				 * RFC 1771:
				 * Unrecognized non-transitive optional
				 * attributes must be quietly ignored and
				 * not passed along to other BGP peers.
				 */
				break;
			}
			if (attr_writebuf(buf, oa->flags | ATTR_PARTIAL,
			    oa->type, oa->data, oa->len) == -1)
				return -1;
		}
	}
	return 0;
}

/*
 * Check if the pending element is a EoR marker. If so remove it from the
 * tree and return 1.
 */
int
up_is_eor(struct rde_peer *peer, uint8_t aid)
{
	struct prefix *p;

	p = RB_MIN(prefix_tree, &peer->updates[aid]);
	if (p != NULL && (p->flags & PREFIX_FLAG_EOR)) {
		/*
		 * Need to remove eor from update tree because
		 * prefix_adjout_destroy() can't handle that.
		 */
		RB_REMOVE(prefix_tree, &peer->updates[aid], p);
		p->flags &= ~PREFIX_FLAG_UPDATE;
		prefix_adjout_destroy(p);
		return 1;
	}
	return 0;
}

/* minimal buffer size > withdraw len + attr len + attr hdr + afi/safi */
#define MIN_UPDATE_LEN	16

static void
up_prefix_free(struct prefix_tree *prefix_head, struct prefix *p,
    struct rde_peer *peer, int withdraw)
{
	if (withdraw) {
		/* prefix no longer needed, remove it */
		prefix_adjout_destroy(p);
		peer->stats.prefix_sent_withdraw++;
	} else {
		/* prefix still in Adj-RIB-Out, keep it */
		RB_REMOVE(prefix_tree, prefix_head, p);
		p->flags &= ~PREFIX_FLAG_UPDATE;
		peer->stats.pending_update--;
		peer->stats.prefix_sent_update++;
	}
}

/*
 * Write prefixes to buffer until either there is no more space or
 * the next prefix has no longer the same ASPATH attributes.
 * Returns -1 if no prefix was written else 0.
 */
static int
up_dump_prefix(struct ibuf *buf, struct prefix_tree *prefix_head,
    struct rde_peer *peer, int withdraw)
{
	struct prefix	*p, *np;
	int		 done = 0, has_ap = -1, rv = -1;

	RB_FOREACH_SAFE(p, prefix_tree, prefix_head, np) {
		if (has_ap == -1)
			has_ap = peer_has_add_path(peer, p->pt->aid,
			    CAPA_AP_SEND);
		if (pt_writebuf(buf, p->pt, withdraw, has_ap, p->path_id_tx) ==
		    -1)
			break;

		/* make sure we only dump prefixes which belong together */
		if (np == NULL ||
		    np->aspath != p->aspath ||
		    np->communities != p->communities ||
		    np->nexthop != p->nexthop ||
		    np->nhflags != p->nhflags ||
		    (np->flags & PREFIX_FLAG_EOR))
			done = 1;

		rv = 0;
		up_prefix_free(prefix_head, p, peer, withdraw);
		if (done)
			break;
	}
	return rv;
}

static int
up_generate_mp_reach(struct ibuf *buf, struct rde_peer *peer,
    struct nexthop *nh, uint8_t aid)
{
	struct bgpd_addr *nexthop;
	size_t off, nhoff;
	uint16_t len, afi;
	uint8_t safi;

	/* attribute header, defaulting to extended length one */
	if (ibuf_add_n8(buf, ATTR_OPTIONAL | ATTR_EXTLEN) == -1)
		return -1;
	if (ibuf_add_n8(buf, ATTR_MP_REACH_NLRI) == -1)
		return -1;
	off = ibuf_size(buf);
	if (ibuf_add_zero(buf, sizeof(len)) == -1)
		return -1;

	if (aid2afi(aid, &afi, &safi))
		fatalx("up_generate_mp_reach: bad AID");

	/* AFI + SAFI + NH LEN + NH + Reserved */
	if (ibuf_add_n16(buf, afi) == -1)
		return -1;
	if (ibuf_add_n8(buf, safi) == -1)
		return -1;
	nhoff = ibuf_size(buf);
	if (ibuf_add_zero(buf, 1) == -1)
		return -1;

	if (aid == AID_VPN_IPv4 || aid == AID_VPN_IPv6) {
		/* write zero rd */
		if (ibuf_add_zero(buf, sizeof(uint64_t)) == -1)
			return -1;
	}

	switch (aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		if (nh == NULL)
			return -1;
		nexthop = &nh->exit_nexthop;
		/* AID_INET must only use this path with an IPv6 nexthop */
		if (nexthop->aid == AID_INET && aid != AID_INET) {
			if (ibuf_add(buf, &nexthop->v4,
			    sizeof(nexthop->v4)) == -1)
				return -1;
			break;
		} else if (nexthop->aid == AID_INET6 &&
		    peer_has_ext_nexthop(peer, aid)) {
			if (ibuf_add(buf, &nexthop->v6,
			    sizeof(nexthop->v6)) == -1)
				return -1;
		} else {
			/* can't encode nexthop, give up and withdraw prefix */
			return -1;
		}
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		if (nh == NULL)
			return -1;
		nexthop = &nh->exit_nexthop;
		if (ibuf_add(buf, &nexthop->v6, sizeof(nexthop->v6)) == -1)
			return -1;
		break;
	case AID_EVPN:
		if (nh == NULL)
			return -1;
		nexthop = &nh->exit_nexthop;
		if (nexthop->aid == AID_INET) {
			if (ibuf_add(buf, &nexthop->v4,
			    sizeof(nexthop->v4)) == -1)
				return -1;
			break;
		} else if (nexthop->aid == AID_INET6) {
			if (ibuf_add(buf, &nexthop->v6,
			    sizeof(nexthop->v6)) == -1)
				return -1;
		} else {
			/* can't encode nexthop, give up and withdraw prefix */
			return -1;
		}
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
		/* no NH */
		break;
	default:
		fatalx("up_generate_mp_reach: unknown AID");
	}

	/* update nexthop len */
	len = ibuf_size(buf) - nhoff - 1;
	if (ibuf_set_n8(buf, nhoff, len) == -1)
		return -1;

	if (ibuf_add_zero(buf, 1) == -1) /* Reserved must be 0 */
		return -1;

	if (up_dump_prefix(buf, &peer->updates[aid], peer, 0) == -1)
		/* no prefixes written, fail update  */
		return -1;

	/* update MP_REACH attribute length field */
	len = ibuf_size(buf) - off - sizeof(len);
	if (ibuf_set_n16(buf, off, len) == -1)
		return -1;

	return 0;
}

/*
 * Generate UPDATE message containing either just withdraws or updates.
 * UPDATE messages are contructed like this:
 *
 *    +-----------------------------------------------------+
 *    |   Withdrawn Routes Length (2 octets)                |
 *    +-----------------------------------------------------+
 *    |   Withdrawn Routes (variable)                       |
 *    +-----------------------------------------------------+
 *    |   Total Path Attribute Length (2 octets)            |
 *    +-----------------------------------------------------+
 *    |   Path Attributes (variable)                        |
 *    +-----------------------------------------------------+
 *    |   Network Layer Reachability Information (variable) |
 *    +-----------------------------------------------------+
 *
 * Multiprotocol messages use MP_REACH_NLRI and MP_UNREACH_NLRI
 * the latter will be the only path attribute in a message.
 */

/*
 * Write UPDATE message for withdrawn routes. The size of buf limits
 * how may routes can be added. Return 0 on success -1 on error which
 * includes generating an empty withdraw message.
 */
void
up_dump_withdraws(struct imsgbuf *imsg, struct rde_peer *peer, uint8_t aid)
{
	struct ibuf *buf;
	size_t off, pkgsize = MAX_PKTSIZE;
	uint16_t afi, len;
	uint8_t safi;

	if ((buf = imsg_create(imsg, IMSG_UPDATE, peer->conf.id, 0, 64)) ==
	    NULL)
		goto fail;

	/* enforce correct ibuf size */
	if (peer_has_ext_msg(peer))
		pkgsize = MAX_EXT_PKTSIZE;
	imsg_set_maxsize(buf, pkgsize - MSGSIZE_HEADER);

	/* reserve space for the withdrawn routes length field */
	off = ibuf_size(buf);
	if (ibuf_add_zero(buf, sizeof(len)) == -1)
		goto fail;

	if (aid != AID_INET) {
		/* reserve space for 2-byte path attribute length */
		off = ibuf_size(buf);
		if (ibuf_add_zero(buf, sizeof(len)) == -1)
			goto fail;

		/* attribute header, defaulting to extended length one */
		if (ibuf_add_n8(buf, ATTR_OPTIONAL | ATTR_EXTLEN) == -1)
			goto fail;
		if (ibuf_add_n8(buf, ATTR_MP_UNREACH_NLRI) == -1)
			goto fail;
		if (ibuf_add_zero(buf, sizeof(len)) == -1)
			goto fail;

		/* afi & safi */
		if (aid2afi(aid, &afi, &safi))
			fatalx("%s: bad AID", __func__);
		if (ibuf_add_n16(buf, afi) == -1)
			goto fail;
		if (ibuf_add_n8(buf, safi) == -1)
			goto fail;
	}

	if (up_dump_prefix(buf, &peer->withdraws[aid], peer, 1) == -1)
		goto fail;

	/* update length field (either withdrawn routes or attribute length) */
	len = ibuf_size(buf) - off - sizeof(len);
	if (ibuf_set_n16(buf, off, len) == -1)
		goto fail;

	if (aid != AID_INET) {
		/* write MP_UNREACH_NLRI attribute length (always extended) */
		len -= 4; /* skip attribute header */
		if (ibuf_set_n16(buf, off + sizeof(len) + 2, len) == -1)
			goto fail;
	} else {
		/* no extra attributes so set attribute len to 0 */
		if (ibuf_add_zero(buf, sizeof(len)) == -1) {
			goto fail;
		}
	}

	imsg_close(imsg, buf);
	return;

 fail:
	/* something went horribly wrong */
	log_peer_warn(&peer->conf, "generating withdraw failed, peer desynced");
	ibuf_free(buf);
}

/*
 * Withdraw a single prefix after an error.
 */
static int
up_dump_withdraw_one(struct rde_peer *peer, struct prefix *p, struct ibuf *buf)
{
	size_t off;
	int has_ap;
	uint16_t afi, len;
	uint8_t safi;

	/* reset the buffer and start fresh */
	ibuf_truncate(buf, IMSG_HEADER_SIZE);

	/* reserve space for the withdrawn routes length field */
	off = ibuf_size(buf);
	if (ibuf_add_zero(buf, sizeof(len)) == -1)
		return -1;

	if (p->pt->aid != AID_INET) {
		/* reserve space for 2-byte path attribute length */
		off = ibuf_size(buf);
		if (ibuf_add_zero(buf, sizeof(len)) == -1)
			return -1;

		/* attribute header, defaulting to extended length one */
		if (ibuf_add_n8(buf, ATTR_OPTIONAL | ATTR_EXTLEN) == -1)
			return -1;
		if (ibuf_add_n8(buf, ATTR_MP_UNREACH_NLRI) == -1)
			return -1;
		if (ibuf_add_zero(buf, sizeof(len)) == -1)
			return -1;

		/* afi & safi */
		if (aid2afi(p->pt->aid, &afi, &safi))
			fatalx("%s: bad AID", __func__);
		if (ibuf_add_n16(buf, afi) == -1)
			return -1;
		if (ibuf_add_n8(buf, safi) == -1)
			return -1;
	}

	has_ap = peer_has_add_path(peer, p->pt->aid, CAPA_AP_SEND);
	if (pt_writebuf(buf, p->pt, 1, has_ap, p->path_id_tx) == -1)
		return -1;

	/* update length field (either withdrawn routes or attribute length) */
	len = ibuf_size(buf) - off - sizeof(len);
	if (ibuf_set_n16(buf, off, len) == -1)
		return -1;

	if (p->pt->aid != AID_INET) {
		/* write MP_UNREACH_NLRI attribute length (always extended) */
		len -= 4; /* skip attribute header */
		if (ibuf_set_n16(buf, off + sizeof(len) + 2, len) == -1)
			return -1;
	} else {
		/* no extra attributes so set attribute len to 0 */
		if (ibuf_add_zero(buf, sizeof(len)) == -1) {
			return -1;
		}
	}

	return 0;
}

/*
 * Write UPDATE message for changed and added routes. The size of buf limits
 * how may routes can be added. The function first dumps the path attributes
 * and then tries to add as many prefixes using these attributes.
 * Return 0 on success -1 on error which includes producing an empty message.
 */
void
up_dump_update(struct imsgbuf *imsg, struct rde_peer *peer, uint8_t aid)
{
	struct ibuf *buf;
	struct bgpd_addr addr;
	struct prefix *p;
	size_t off, pkgsize = MAX_PKTSIZE;
	uint16_t len;
	int force_ip4mp = 0;

	p = RB_MIN(prefix_tree, &peer->updates[aid]);
	if (p == NULL)
		return;

	if (aid == AID_INET && peer_has_ext_nexthop(peer, AID_INET)) {
		struct nexthop *nh = prefix_nexthop(p);
		if (nh != NULL && nh->exit_nexthop.aid == AID_INET6)
			force_ip4mp = 1;
	}

	if ((buf = imsg_create(imsg, IMSG_UPDATE, peer->conf.id, 0, 64)) ==
	    NULL)
		goto fail;

	/* enforce correct ibuf size */
	if (peer_has_ext_msg(peer))
		pkgsize = MAX_EXT_PKTSIZE;
	imsg_set_maxsize(buf, pkgsize - MSGSIZE_HEADER);

	/* withdrawn routes length field is 0 */
	if (ibuf_add_zero(buf, sizeof(len)) == -1)
		goto fail;

	/* reserve space for 2-byte path attribute length */
	off = ibuf_size(buf);
	if (ibuf_add_zero(buf, sizeof(len)) == -1)
		goto fail;

	if (up_generate_attr(buf, peer, prefix_aspath(p),
	    prefix_communities(p), prefix_nexthop(p), aid) == -1)
		goto drop;

	if (aid != AID_INET || force_ip4mp) {
		/* write mp attribute including nlri */

		/*
		 * RFC 7606 wants this to be first but then we need
		 * to use multiple buffers with adjusted length to
		 * merge the attributes together in reverse order of
		 * creation.
		 */
		if (up_generate_mp_reach(buf, peer, prefix_nexthop(p), aid) ==
		    -1)
			goto drop;
	}

	/* update attribute length field */
	len = ibuf_size(buf) - off - sizeof(len);
	if (ibuf_set_n16(buf, off, len) == -1)
		goto fail;

	if (aid == AID_INET && !force_ip4mp) {
		/* last but not least dump the IPv4 nlri */
		if (up_dump_prefix(buf, &peer->updates[aid], peer, 0) == -1)
			goto drop;
	}

	imsg_close(imsg, buf);
	return;

 drop:
	/* Not enough space. Drop current prefix, it will never fit. */
	p = RB_MIN(prefix_tree, &peer->updates[aid]);
	pt_getaddr(p->pt, &addr);
	log_peer_warnx(&peer->conf, "generating update failed, "
	    "prefix %s/%d dropped", log_addr(&addr), p->pt->prefixlen);

	up_prefix_free(&peer->updates[aid], p, peer, 0);
	if (up_dump_withdraw_one(peer, p, buf) == -1)
		goto fail;
	imsg_close(imsg, buf);
	return;

 fail:
	/* something went horribly wrong */
	log_peer_warn(&peer->conf, "generating update failed, peer desynced");
	ibuf_free(buf);
}
