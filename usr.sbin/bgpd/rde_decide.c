/*	$OpenBSD: rde_decide.c,v 1.104 2025/02/20 19:47:31 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <string.h>

#include "bgpd.h"
#include "rde.h"
#include "log.h"

int	prefix_cmp(struct prefix *, struct prefix *, int *);
void	prefix_set_dmetric(struct prefix *, struct prefix *);
void	prefix_insert(struct prefix *, struct prefix *, struct rib_entry *);
void	prefix_remove(struct prefix *, struct rib_entry *);
/*
 * Decision Engine RFC implementation:
 *  Phase 1:
 *   - calculate LOCAL_PREF if needed -- EBGP or IGP learnt routes
 *   - IBGP routes may either use LOCAL_PREF or the local system computes
 *     the degree of preference
 *   - If the route is ineligible, the route MAY NOT serve as an input to
 *     the next phase of route selection
 *   - if the route is eligible the computed value MUST be used as the
 *     LOCAL_PREF value in any IBGP readvertisement
 *
 *  Phase 2:
 *   - If the NEXT_HOP attribute of a BGP route depicts an address that is
 *     not resolvable the BGP route MUST be excluded from the Phase 2 decision
 *     function.
 *   - If the AS_PATH attribute of a BGP route contains an AS loop, the BGP
 *     route should be excluded from the Phase 2 decision function.
 *   - The local BGP speaker identifies the route that has:
 *     a) the highest degree of preference of any route to the same set
 *        of destinations
 *     b) is the only route to that destination
 *     c) is selected as a result of the Phase 2 tie breaking rules
 *   - The local speaker MUST determine the immediate next-hop address from
 *     the NEXT_HOP attribute of the selected route.
 *   - If either the immediate next hop or the IGP cost to the NEXT_HOP changes,
 *     Phase 2 Route Selection MUST be performed again.
 *
 *  Route Resolvability Condition
 *   - A route Rte1, referencing only the intermediate network address, is
 *     considered resolvable if the Routing Table contains at least one
 *     resolvable route Rte2 that matches Rte1's intermediate network address
 *     and is not recursively resolved through Rte1.
 *   - Routes referencing interfaces are considered resolvable if the state of
 *     the referenced interface is up and IP processing is enabled.
 *
 *  Breaking Ties (Phase 2)
 *   1. Remove from consideration all routes which are not tied for having the
 *      smallest number of AS numbers present in their AS_PATH attributes.
 *      Note, that when counting this number, an AS_SET counts as 1
 *   2. Remove from consideration all routes which are not tied for having the
 *      lowest Origin number in their Origin attribute.
 *   3. Remove from consideration routes with less-preferred MULTI_EXIT_DISC
 *      attributes. MULTI_EXIT_DISC is only comparable between routes learned
 *      from the same neighboring AS.
 *   4. If at least one of the candidate routes was received via EBGP,
 *      remove from consideration all routes which were received via IBGP.
 *   5. Remove from consideration any routes with less-preferred interior cost.
 *      If the NEXT_HOP hop for a route is reachable, but no cost can be
 *      determined, then this step should be skipped.
 *   6. Remove from consideration all routes other than the route that was
 *      advertised by the BGP speaker whose BGP Identifier has the lowest value.
 *   7. Prefer the route received from the lowest peer address.
 *
 * Phase 3: Route Dissemination
 *   - All routes in the Loc-RIB are processed into Adj-RIBs-Out according
 *     to configured policy. A route SHALL NOT be installed in the Adj-Rib-Out
 *     unless the destination and NEXT_HOP described by this route may be
 *     forwarded appropriately by the Routing Table.
 */

/*
 * Decision Engine OUR implementation:
 * The filtering is done first. The filtering calculates the preference and
 * stores it in LOCAL_PREF (Phase 1).
 * Ineligible routes are flagged as ineligible via nexthop_add().
 * Phase 3 is done together with Phase 2.
 * In following cases a prefix needs to be reevaluated:
 *  - update of a prefix (prefix_update)
 *  - withdraw of a prefix (prefix_withdraw)
 *  - state change of the nexthop (nexthop-{in}validate)
 *  - state change of session (session down)
 */

/*
 * Compare two prefixes with equal pt_entry. Returns an integer greater than or
 * less than 0, according to whether the prefix p1 is more or less preferred
 * than the prefix p2. p1 should be used for the new prefix and p2 for a
 * already added prefix. The absolute value returned specifies the similarity
 * of the prefixes.
 *   1: prefixes differ because of validity
 *   2: prefixes don't belong in any multipath set
 *   3: prefixes belong only in the as-wide multipath set
 *   4: prefixes belong in both the ecmp and as-wide multipath set
 *   TODO: maybe we also need a strict ecmp set that requires
 *   prefixes to e.g. equal ASPATH or equal neighbor-as (like for MED).
 */
int
prefix_cmp(struct prefix *p1, struct prefix *p2, int *testall)
{
	struct rde_aspath	*asp1, *asp2;
	struct rde_peer		*peer1, *peer2;
	struct attr		*a;
	uint32_t		 p1id, p2id;
	int			 p1cnt, p2cnt, i;
	int			 rv = 1;

	/*
	 * If a match happens before the MED check then the list is
	 * correctly sorted. If a match happens after MED then further
	 * elements may need to be checked to ensure that all paths
	 * which could affect this path were considered. This only
	 * matters for strict MED evaluation and in that case testall
	 * is set to 1. If the check happens to be on the MED check
	 * itself testall is set to 2.
	 */
	*testall = 0;

	if (p1 == NULL)
		return -rv;
	if (p2 == NULL)
		return rv;

	asp1 = prefix_aspath(p1);
	asp2 = prefix_aspath(p2);
	peer1 = prefix_peer(p1);
	peer2 = prefix_peer(p2);

	/* 1. check if prefix is eligible a.k.a reachable */
	if (!prefix_eligible(p2))
		return rv;
	if (!prefix_eligible(p1))
		return -rv;

	/* bump rv, from here on prefix is considered valid */
	rv++;

	/* 2. local preference of prefix, bigger is better */
	if (asp1->lpref > asp2->lpref)
		return rv;
	if (asp1->lpref < asp2->lpref)
		return -rv;

	/* 3. aspath count, the shorter the better */
	if (asp1->aspath->ascnt < asp2->aspath->ascnt)
		return rv;
	if (asp1->aspath->ascnt > asp2->aspath->ascnt)
		return -rv;

	/* 4. origin, the lower the better */
	if (asp1->origin < asp2->origin)
		return rv;
	if (asp1->origin > asp2->origin)
		return -rv;

	/*
	 * 5. MED decision
	 * Only comparable between the same neighboring AS or if
	 * 'rde med compare always' is set. In the first case
	 * set the testall flag since further elements need to be
	 * evaluated as well.
	 */
	if ((rde_decisionflags() & BGPD_FLAG_DECISION_MED_ALWAYS) ||
	    aspath_neighbor(asp1->aspath) == aspath_neighbor(asp2->aspath)) {
		if (!(rde_decisionflags() & BGPD_FLAG_DECISION_MED_ALWAYS))
			*testall = 2;
		/* lowest value wins */
		if (asp1->med < asp2->med)
			return rv;
		if (asp1->med > asp2->med)
			return -rv;
	}

	if (!(rde_decisionflags() & BGPD_FLAG_DECISION_MED_ALWAYS))
		*testall = 1;

	/*
	 * 6. EBGP is cooler than IBGP
	 * It is absolutely important that the ebgp value in peer_config.ebgp
	 * is bigger than all other ones (IBGP, confederations)
	 */
	if (peer1->conf.ebgp != peer2->conf.ebgp) {
		if (peer1->conf.ebgp) /* peer1 is EBGP other is lower */
			return rv;
		else if (peer2->conf.ebgp) /* peer2 is EBGP */
			return -rv;
	}

	/* bump rv, as-wide multipath */
	rv++;

	/*
	 * 7. local tie-breaker, this weight is here to tip equal long AS
	 * paths in one or the other direction. It happens more and more
	 * that AS paths are equally long and so traffic engineering needs
	 * a metric that weights a prefix at a very late stage in the
	 * decision process.
	 */
	if (asp1->weight > asp2->weight)
		return rv;
	if (asp1->weight < asp2->weight)
		return -rv;

	/* 8. nexthop costs. NOT YET -> IGNORE */

	/* bump rv, equal cost multipath */
	rv++;

	/*
	 * 9. older route (more stable) wins but only if route-age
	 * evaluation is enabled.
	 */
	if (rde_decisionflags() & BGPD_FLAG_DECISION_ROUTEAGE) {
		switch (monotime_cmp(p1->lastchange, p2->lastchange)) {
		case -1:	/* p1 is older */
			return rv;
		case 1:		/* p2 is older */
			return -rv;
		}
	}

	/* 10. lowest BGP Id wins, use ORIGINATOR_ID if present */
	if ((a = attr_optget(asp1, ATTR_ORIGINATOR_ID)) != NULL) {
		memcpy(&p1id, a->data, sizeof(p1id));
		p1id = ntohl(p1id);
	} else
		p1id = peer1->remote_bgpid;
	if ((a = attr_optget(asp2, ATTR_ORIGINATOR_ID)) != NULL) {
		memcpy(&p2id, a->data, sizeof(p2id));
		p2id = ntohl(p2id);
	} else
		p2id = peer2->remote_bgpid;
	if (p1id < p2id)
		return rv;
	if (p1id > p2id)
		return -rv;

	/* 11. compare CLUSTER_LIST length, shorter is better */
	p1cnt = p2cnt = 0;
	if ((a = attr_optget(asp1, ATTR_CLUSTER_LIST)) != NULL)
		p1cnt = a->len / sizeof(uint32_t);
	if ((a = attr_optget(asp2, ATTR_CLUSTER_LIST)) != NULL)
		p2cnt = a->len / sizeof(uint32_t);
	if (p1cnt < p2cnt)
		return rv;
	if (p1cnt > p2cnt)
		return -rv;

	/* 12. lowest peer address wins (IPv4 is better than IPv6) */
	if (peer1->remote_addr.aid < peer2->remote_addr.aid)
		return rv;
	if (peer1->remote_addr.aid > peer2->remote_addr.aid)
		return -rv;
	switch (peer1->remote_addr.aid) {
	case AID_INET:
		i = memcmp(&peer1->remote_addr.v4, &peer2->remote_addr.v4,
		    sizeof(struct in_addr));
		break;
	case AID_INET6:
		i = memcmp(&peer1->remote_addr.v6, &peer2->remote_addr.v6,
		    sizeof(struct in6_addr));
		break;
	default:
		fatalx("%s: unknown af", __func__);
	}
	if (i < 0)
		return rv;
	if (i > 0)
		return -rv;

	/* RFC7911 does not specify this but something like this is needed. */
	/* 13. lowest path identifier wins */
	if (p1->path_id < p2->path_id)
		return rv;
	if (p1->path_id > p2->path_id)
		return -rv;

	fatalx("Uh, oh a politician in the decision process");
}

/*
 * set the dmetric value of np based on the return value of
 * prefix_evaluate(pp, np) or set it to either PREFIX_DMETRIC_BEST
 * or PREFIX_DMETRIC_INVALID for the first element.
 */
void
prefix_set_dmetric(struct prefix *pp, struct prefix *np)
{
	int testall;

	if (np != NULL) {
		if (pp == NULL)
			np->dmetric = prefix_eligible(np) ?
			    PREFIX_DMETRIC_BEST : PREFIX_DMETRIC_INVALID;
		else
			np->dmetric = prefix_cmp(pp, np, &testall);
		if (np->dmetric < 0) {
			struct bgpd_addr addr;
			pt_getaddr(np->pt, &addr);
			log_debug("bad dmetric in decision process: %s/%u",
			    log_addr(&addr), np->pt->prefixlen);
		}
	}
}

/*
 * Insert a prefix keeping the total order of the list. For routes
 * that may depend on a MED selection the set is scanned until the
 * condition is cleared. If a MED inversion is detected the respective
 * prefix is taken of the rib list and put onto a redo queue. All
 * prefixes on the redo queue are re-inserted at the end.
 */
void
prefix_insert(struct prefix *new, struct prefix *ep, struct rib_entry *re)
{
	struct prefix_queue redo = TAILQ_HEAD_INITIALIZER(redo);
	struct prefix *xp, *np, *insertp = ep;
	int testall, preferred, selected = 0, removed = 0;

	/* start scan at the entry point (ep) or the head if ep == NULL */
	if (ep == NULL)
		ep = TAILQ_FIRST(&re->prefix_h);

	for (xp = ep; xp != NULL; xp = np) {
		np = TAILQ_NEXT(xp, entry.list.rib);

		if ((preferred = (prefix_cmp(new, xp, &testall) > 0))) {
			/* new is preferred over xp */
			if (testall == 2) {
				/*
				 * MED inversion, take out prefix and
				 * put it onto redo queue.
				 */
				TAILQ_REMOVE(&re->prefix_h, xp, entry.list.rib);
				TAILQ_INSERT_TAIL(&redo, xp, entry.list.rib);
				removed = 1;
				continue;
			}

			if (testall == 1) {
				/*
				 * lock insertion point and
				 * continue on with scan
				 */
				selected = 1;
			}
		} else {
			/*
			 * xp is preferred over new.
			 * Remember insertion point for later unless the
			 * traverse is just looking for a possible MED
			 * inversion (selected == 1).
			 * If the last comparison's tie-breaker was the MED
			 * check reset selected and with it insertp since
			 * this was an actual MED priority inversion.
			 */
			if (testall == 2)
				selected = 0;
			if (!selected)
				insertp = xp;
		}

		/*
		 * If previous element(s) got removed, fixup the
		 * dmetric, now that it is clear that this element
		 * is on the list.
		 */
		if (removed) {
			prefix_set_dmetric(TAILQ_PREV(xp, prefix_queue,
			    entry.list.rib), xp);
			removed = 0;
		}

		if (preferred && testall == 0)
			break;			/* we're done */
	}

	if (insertp == NULL) {
		TAILQ_INSERT_HEAD(&re->prefix_h, new, entry.list.rib);
	} else {
		TAILQ_INSERT_AFTER(&re->prefix_h, insertp, new, entry.list.rib);
	}

	prefix_set_dmetric(insertp, new);
	prefix_set_dmetric(new, TAILQ_NEXT(new, entry.list.rib));

	/* Fixup MED order again. All elements are < new */
	while (!TAILQ_EMPTY(&redo)) {
		xp = TAILQ_FIRST(&redo);
		TAILQ_REMOVE(&redo, xp, entry.list.rib);

		prefix_insert(xp, new, re);
	}
}

/*
 * Remove a prefix from the RIB list ensuring that the total order of the
 * list remains intact. All routes that differ in the MED are taken of the
 * list and put on the redo list. To figure out if a route could cause a
 * resort because of a MED check the next prefix of the to-remove prefix
 * is compared with the old prefix. A full scan is only done if the next
 * route differs because of the MED or later checks.
 * Again at the end all routes on the redo queue are reinserted.
 */
void
prefix_remove(struct prefix *old, struct rib_entry *re)
{
	struct prefix_queue redo = TAILQ_HEAD_INITIALIZER(redo);
	struct prefix *xp, *np, *pp;
	int testall, removed = 0;

	xp = TAILQ_NEXT(old, entry.list.rib);
	pp = TAILQ_PREV(old, prefix_queue, entry.list.rib);
	TAILQ_REMOVE(&re->prefix_h, old, entry.list.rib);

	/* check if a MED inversion could be possible */
	prefix_cmp(old, xp, &testall);
	if (testall > 0) {
		/* maybe MED route, scan tail for other possible routes */
		for (; xp != NULL; xp = np) {
			np = TAILQ_NEXT(xp, entry.list.rib);

			/* only interested in the testall result */
			prefix_cmp(old, xp, &testall);
			if (testall == 2) {
				/*
				 * possible MED inversion, take out prefix and
				 * put it onto redo queue.
				 */
				TAILQ_REMOVE(&re->prefix_h, xp, entry.list.rib);
				TAILQ_INSERT_TAIL(&redo, xp, entry.list.rib);
				removed = 1;
				continue;
			}
			/*
			 * If previous element(s) got removed, fixup the
			 * dmetric, now that it is clear that this element
			 * is on the list.
			 */
			if (removed) {
				prefix_set_dmetric(TAILQ_PREV(xp, prefix_queue,
				    entry.list.rib), xp);
				removed = 0;
			}
			if (testall == 0)
				break;		/* we're done */
		}
	}

	if (pp)
		prefix_set_dmetric(pp, TAILQ_NEXT(pp, entry.list.rib));
	else
		prefix_set_dmetric(NULL, TAILQ_FIRST(&re->prefix_h));

	/* Fixup MED order again, reinsert prefixes from the start */
	while (!TAILQ_EMPTY(&redo)) {
		xp = TAILQ_FIRST(&redo);
		TAILQ_REMOVE(&redo, xp, entry.list.rib);

		prefix_insert(xp, NULL, re);
	}
}

/* helper function to check if a prefix is valid to be selected */
int
prefix_eligible(struct prefix *p)
{
	struct rde_aspath *asp = prefix_aspath(p);

	/* prefix itself is marked ineligible */
	if (prefix_filtered(p))
		return 0;

	/* The aspath needs to be loop and error free */
	if (asp == NULL ||
	    asp->flags & (F_ATTR_LOOP|F_ATTR_OTC_LEAK|F_ATTR_PARSE_ERR))
		return 0;

	/* The nexthop must be valid. */
	if (!prefix_nhvalid(p))
		return 0;

	return 1;
}

struct prefix *
prefix_best(struct rib_entry *re)
{
	struct prefix	*xp;
	struct rib	*rib;

	rib = re_rib(re);
	if (rib->flags & F_RIB_NOEVALUATE)
		/* decision process is turned off */
		return NULL;

	xp = TAILQ_FIRST(&re->prefix_h);
	if (xp != NULL && !prefix_eligible(xp))
		xp = NULL;
	return xp;
}

/*
 * Find the correct place to insert the prefix in the prefix list.
 * If the active prefix has changed we need to send an update also special
 * treatment is needed if 'rde evaluate all' is used on some peers.
 * To re-evaluate a prefix just call prefix_evaluate with old and new pointing
 * to the same prefix.
 */
void
prefix_evaluate(struct rib_entry *re, struct prefix *new, struct prefix *old)
{
	struct prefix	*newbest, *oldbest;
	struct rib	*rib;

	rib = re_rib(re);
	if (rib->flags & F_RIB_NOEVALUATE) {
		/* decision process is turned off */
		if (old != NULL)
			TAILQ_REMOVE(&re->prefix_h, old, entry.list.rib);
		if (new != NULL) {
			TAILQ_INSERT_HEAD(&re->prefix_h, new, entry.list.rib);
			new->dmetric = PREFIX_DMETRIC_INVALID;
		}
		return;
	}

	oldbest = prefix_best(re);
	if (old != NULL)
		prefix_remove(old, re);
	if (new != NULL)
		prefix_insert(new, NULL, re);
	newbest = prefix_best(re);

	/*
	 * If the active prefix changed or the active prefix was removed
	 * and added again then generate an update.
	 */
	if (oldbest != newbest || (old != NULL && newbest == old)) {
		/*
		 * Send update withdrawing oldbest and adding newbest
		 * but remember that newbest may be NULL aka ineligible.
		 * Additional decision may be made by the called functions.
		 */
		if ((rib->flags & F_RIB_NOFIB) == 0)
			rde_send_kroute(rib, newbest, oldbest);
		rde_generate_updates(re, new, old, EVAL_DEFAULT);
		return;
	}

	/*
	 * If there are peers with 'rde evaluate all' every update needs
	 * to be passed on (not only a change of the best prefix).
	 * rde_generate_updates() will then take care of distribution.
	 */
	if (rde_evaluate_all()) {
		if (new != NULL && !prefix_eligible(new))
			new = NULL;
		if (new != NULL || old != NULL)
			rde_generate_updates(re, new, old, EVAL_ALL);
	}
}

void
prefix_evaluate_nexthop(struct prefix *p, enum nexthop_state state,
    enum nexthop_state oldstate)
{
	struct rib_entry *re = prefix_re(p);
	struct prefix	*newbest, *oldbest, *new, *old;
	struct rib	*rib;

	/* Skip non local-RIBs or RIBs that are flagged as noeval. */
	rib = re_rib(re);
	if (rib->flags & F_RIB_NOEVALUATE) {
		log_warnx("%s: prefix with F_RIB_NOEVALUATE hit", __func__);
		return;
	}

	if (oldstate == state) {
		/*
		 * The state of the nexthop did not change. The only
		 * thing that may have changed is the true_nexthop
		 * or other internal infos. This will not change
		 * the routing decision so shortcut here.
		 * XXX needs to be changed for ECMP
		 */
		if (state == NEXTHOP_REACH) {
			if ((rib->flags & F_RIB_NOFIB) == 0 &&
			    p == prefix_best(re))
				rde_send_kroute(rib, p, NULL);
		}
		return;
	}

	/*
	 * Re-evaluate the prefix by removing the prefix then updating the
	 * nexthop state and reinserting the prefix again.
	 */
	old = p;
	oldbest = prefix_best(re);
	prefix_remove(p, re);

	if (state == NEXTHOP_REACH)
		p->nhflags |= NEXTHOP_VALID;
	else
		p->nhflags &= ~NEXTHOP_VALID;

	prefix_insert(p, NULL, re);
	newbest = prefix_best(re);
	new = p;
	if (!prefix_eligible(new))
		new = NULL;

	/*
	 * If the active prefix changed or the active prefix was removed
	 * and added again then generate an update.
	 */
	if (oldbest != newbest || newbest == p) {
		/*
		 * Send update withdrawing oldbest and adding newbest
		 * but remember that newbest may be NULL aka ineligible.
		 * Additional decision may be made by the called functions.
		 */
		if ((rib->flags & F_RIB_NOFIB) == 0)
			rde_send_kroute(rib, newbest, oldbest);
		rde_generate_updates(re, new, old, EVAL_DEFAULT);
		return;
	}

	/*
	 * If there are peers with 'rde evaluate all' every update needs
	 * to be passed on (not only a change of the best prefix).
	 * rde_generate_updates() will then take care of distribution.
	 */
	if (rde_evaluate_all())
		rde_generate_updates(re, new, old, EVAL_ALL);
}
