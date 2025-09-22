/*	$OpenBSD: rde_peer.c,v 1.50 2025/08/22 11:41:56 claudio Exp $ */

/*
 * Copyright (c) 2019 Claudio Jeker <claudio@openbsd.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bgpd.h"
#include "rde.h"

struct peer_tree	 peertable = RB_INITIALIZER(&peertable);
struct peer_tree	 zombietable = RB_INITIALIZER(&zombietable);
struct rde_peer		*peerself;

CTASSERT(sizeof(peerself->recv_eor) * 8 >= AID_MAX);
CTASSERT(sizeof(peerself->sent_eor) * 8 >= AID_MAX);

struct iq {
	SIMPLEQ_ENTRY(iq)	entry;
	struct imsg		imsg;
};

int
peer_has_as4byte(struct rde_peer *peer)
{
	return peer->capa.as4byte;
}

/*
 * Check if ADD_PATH is enabled for aid and mode (rx / tx). If aid is
 * AID_UNSPEC then the function returns true if any aid has mode enabled.
 */
int
peer_has_add_path(struct rde_peer *peer, uint8_t aid, int mode)
{
	if (aid >= AID_MAX)
		return 0;
	return peer->capa.add_path[aid] & mode;
}

int
peer_has_ext_msg(struct rde_peer *peer)
{
	return peer->capa.ext_msg;
}

int
peer_has_ext_nexthop(struct rde_peer *peer, uint8_t aid)
{
	if (aid >= AID_MAX)
		return 0;
	return peer->capa.ext_nh[aid];
}

int
peer_permit_as_set(struct rde_peer *peer)
{
	return peer->flags & PEERFLAG_PERMIT_AS_SET;
}

void
peer_init(struct filter_head *rules)
{
	struct peer_config pc;

	memset(&pc, 0, sizeof(pc));
	snprintf(pc.descr, sizeof(pc.descr), "LOCAL");
	pc.id = PEER_ID_SELF;

	peerself = peer_add(PEER_ID_SELF, &pc, rules);
	peerself->state = PEER_UP;
}

void
peer_shutdown(void)
{
	struct rde_peer *peer, *np;

	RB_FOREACH_SAFE(peer, peer_tree, &peertable, np)
		peer_delete(peer);

	while (!RB_EMPTY(&zombietable))
		peer_reaper(NULL);

	if (!RB_EMPTY(&peertable))
		log_warnx("%s: free non-free table", __func__);
}

/*
 * Traverse all peers calling callback for each peer.
 */
void
peer_foreach(void (*callback)(struct rde_peer *, void *), void *arg)
{
	struct rde_peer *peer, *np;

	RB_FOREACH_SAFE(peer, peer_tree, &peertable, np)
		callback(peer, arg);
}

/*
 * Lookup a peer by peer_id, return NULL if not found.
 */
struct rde_peer *
peer_get(uint32_t id)
{
	struct rde_peer	needle;

	needle.conf.id = id;
	return RB_FIND(peer_tree, &peertable, &needle);
}

/*
 * Find next peer that matches neighbor options in *n.
 * If peerid was set then pickup the lookup after that peer.
 * Returns NULL if no more peers match.
 */
struct rde_peer *
peer_match(struct ctl_neighbor *n, uint32_t peerid)
{
	struct rde_peer		*peer;

	if (peerid != 0) {
		peer = peer_get(peerid);
		if (peer)
			peer = RB_NEXT(peer_tree, &peertable, peer);
	} else
		peer = RB_MIN(peer_tree, &peertable);

	for (; peer != NULL; peer = RB_NEXT(peer_tree, &peertable, peer)) {
		if (rde_match_peer(peer, n))
			return peer;
	}
	return NULL;
}

struct rde_peer *
peer_add(uint32_t id, struct peer_config *p_conf, struct filter_head *rules)
{
	struct rde_peer		*peer;
	int			 conflict;

	if ((peer = peer_get(id))) {
		memcpy(&peer->conf, p_conf, sizeof(struct peer_config));
		return peer;
	}

	peer = calloc(1, sizeof(struct rde_peer));
	if (peer == NULL)
		fatal("peer_add");

	memcpy(&peer->conf, p_conf, sizeof(struct peer_config));
	peer->remote_bgpid = 0;
	peer->loc_rib_id = rib_find(peer->conf.rib);
	if (peer->loc_rib_id == RIB_NOTFOUND)
		fatalx("King Bula's new peer met an unknown RIB");
	peer->state = PEER_NONE;
	peer->eval = peer->conf.eval;
	peer->role = peer->conf.role;
	peer->export_type = peer->conf.export_type;
	peer->flags = peer->conf.flags;
	SIMPLEQ_INIT(&peer->imsg_queue);
	if ((peer->ibufq = ibufq_new()) == NULL)
		fatal(NULL);

	peer_apply_out_filter(peer, rules);

	/*
	 * Assign an even random unique transmit path id.
	 * Odd path_id_tx numbers are for peers using add-path recv.
	 */
	do {
		struct rde_peer *p;

		conflict = 0;
		peer->path_id_tx = arc4random() << 1;
		RB_FOREACH(p, peer_tree, &peertable) {
			if (p->path_id_tx == peer->path_id_tx) {
				conflict = 1;
				break;
			}
		}
	} while (conflict);

	if (RB_INSERT(peer_tree, &peertable, peer) != NULL)
		fatalx("rde peer table corrupted");

	return peer;
}

struct filter_head *
peer_apply_out_filter(struct rde_peer *peer, struct filter_head *rules)
{
	struct filter_head *old;
	struct filter_rule *fr, *new;

	old = peer->out_rules;
	if ((peer->out_rules = malloc(sizeof(*peer->out_rules))) == NULL)
		fatal(NULL);
	TAILQ_INIT(peer->out_rules);

	TAILQ_FOREACH(fr, rules, entry) {
		if (rde_filter_skip_rule(peer, fr))
			continue;

		if ((new = malloc(sizeof(*new))) == NULL)
			fatal(NULL);
		memcpy(new, fr, sizeof(*new));
		filterset_copy(&fr->set, &new->set);

		TAILQ_INSERT_TAIL(peer->out_rules, new, entry);
	}

	return old;
}

static inline int
peer_cmp(struct rde_peer *a, struct rde_peer *b)
{
	if (a->conf.id > b->conf.id)
		return 1;
	if (a->conf.id < b->conf.id)
		return -1;
	return 0;
}

RB_GENERATE(peer_tree, rde_peer, entry, peer_cmp);

static void
peer_generate_update(struct rde_peer *peer, struct rib_entry *re,
    struct prefix *newpath, struct prefix *oldpath,
    enum eval_mode mode)
{
	uint8_t		 aid;

	aid = re->prefix->aid;

	/* skip ourself */
	if (peer == peerself)
		return;
	/* skip peers that never had a session open */
	if (peer->state == PEER_NONE)
		return;
	/* skip peers using a different rib */
	if (peer->loc_rib_id != re->rib_id)
		return;
	/* check if peer actually supports the address family */
	if (peer->capa.mp[aid] == 0)
		return;
	/* skip peers with special export types */
	if (peer->export_type == EXPORT_NONE ||
	    peer->export_type == EXPORT_DEFAULT_ROUTE)
		return;

	/* if reconf skip peers which don't need to reconfigure */
	if (mode == EVAL_RECONF && peer->reconf_out == 0)
		return;

	/* handle peers with add-path */
	if (peer_has_add_path(peer, aid, CAPA_AP_SEND)) {
		if (peer->eval.mode == ADDPATH_EVAL_ALL)
			up_generate_addpath_all(peer, re, newpath, oldpath);
		else
			up_generate_addpath(peer, re);
		return;
	}

	/* skip regular peers if the best path didn't change */
	if (mode == EVAL_ALL && (peer->flags & PEERFLAG_EVALUATE_ALL) == 0)
		return;
	up_generate_updates(peer, re);
}

void
rde_generate_updates(struct rib_entry *re, struct prefix *newpath,
    struct prefix *oldpath, enum eval_mode mode)
{
	struct rde_peer	*peer;

	RB_FOREACH(peer, peer_tree, &peertable)
		peer_generate_update(peer, re, newpath, oldpath, mode);
}

/*
 * Various RIB walker callbacks.
 */
struct peer_flush {
	struct rde_peer *peer;
	monotime_t	 staletime;
};

static void
peer_flush_upcall(struct rib_entry *re, void *arg)
{
	struct rde_peer *peer = ((struct peer_flush *)arg)->peer;
	struct rde_aspath *asp;
	struct bgpd_addr addr;
	struct prefix *p, *np, *rp;
	monotime_t staletime = ((struct peer_flush *)arg)->staletime;
	uint32_t i;
	uint8_t prefixlen;

	pt_getaddr(re->prefix, &addr);
	prefixlen = re->prefix->prefixlen;
	TAILQ_FOREACH_SAFE(p, &re->prefix_h, entry.list.rib, np) {
		if (peer != prefix_peer(p))
			continue;
		if (monotime_valid(staletime) &&
		    monotime_cmp(p->lastchange, staletime) > 0)
			continue;

		for (i = RIB_LOC_START; i < rib_size; i++) {
			struct rib *rib = rib_byid(i);
			if (rib == NULL)
				continue;
			rp = prefix_get(rib, peer, p->path_id,
			    &addr, prefixlen);
			if (rp) {
				asp = prefix_aspath(rp);
				if (asp && asp->pftableid)
					rde_pftable_del(asp->pftableid, rp);

				prefix_destroy(rp);
				rde_update_log("flush", i, peer, NULL,
				    &addr, prefixlen);
			}
		}

		prefix_destroy(p);
		peer->stats.prefix_cnt--;
	}
}

/*
 * Session got established, bring peer up, load RIBs do initial table dump.
 */
void
peer_up(struct rde_peer *peer, struct session_up *sup)
{
	uint8_t	 i;
	int force_sync = 1;

	if (peer->state == PEER_ERR) {
		/*
		 * There is a race condition when doing PEER_ERR -> PEER_DOWN.
		 * So just do a full reset of the peer here.
		 */
		rib_dump_terminate(peer);
		peer_imsg_flush(peer);
		peer_flush(peer, AID_UNSPEC, monotime_clear());
		peer->stats.prefix_cnt = 0;
		peer->state = PEER_DOWN;
	}

	/*
	 * Check if no value changed during flap to decide if the RIB
	 * is in sync. The capa check is maybe too strict but it should
	 * not matter for normal operation.
	 */
	if (memcmp(&peer->remote_addr, &sup->remote_addr,
	    sizeof(sup->remote_addr)) == 0 &&
	    memcmp(&peer->local_v4_addr, &sup->local_v4_addr,
	    sizeof(sup->local_v4_addr)) == 0 &&
	    memcmp(&peer->local_v6_addr, &sup->local_v6_addr,
	    sizeof(sup->local_v6_addr)) == 0 &&
	    memcmp(&peer->capa, &sup->capa, sizeof(sup->capa)) == 0)
		force_sync = 0;

	peer->remote_addr = sup->remote_addr;
	peer->local_v4_addr = sup->local_v4_addr;
	peer->local_v6_addr = sup->local_v6_addr;
	memcpy(&peer->capa, &sup->capa, sizeof(sup->capa));
	/* the Adj-RIB-Out does not depend on those */
	peer->remote_bgpid = sup->remote_bgpid;
	peer->local_if_scope = sup->if_scope;
	peer->short_as = sup->short_as;

	/* clear eor markers depending on GR flags */
	if (peer->capa.grestart.restart) {
		peer->sent_eor = 0;
		peer->recv_eor = 0;
	} else {
		/* no EOR expected */
		peer->sent_eor = ~0;
		peer->recv_eor = ~0;
	}
	peer->state = PEER_UP;

	if (!force_sync) {
		for (i = AID_MIN; i < AID_MAX; i++) {
			if (peer->capa.mp[i])
				peer_blast(peer, i);
		}
	} else {
		for (i = AID_MIN; i < AID_MAX; i++) {
			if (peer->capa.mp[i])
				peer_dump(peer, i);
		}
	}
}

/*
 * Session dropped and no graceful restart is done. Stop everything for
 * this peer and clean up.
 */
void
peer_down(struct rde_peer *peer)
{
	peer->remote_bgpid = 0;
	peer->state = PEER_DOWN;
	/*
	 * stop all pending dumps which may depend on this peer
	 * and flush all pending imsg from the SE.
	 */
	rib_dump_terminate(peer);
	prefix_adjout_flush_pending(peer);
	peer_imsg_flush(peer);

	/* flush Adj-RIB-In */
	peer_flush(peer, AID_UNSPEC, monotime_clear());
	peer->stats.prefix_cnt = 0;
}

void
peer_delete(struct rde_peer *peer)
{
	if (peer->state != PEER_DOWN)
		peer_down(peer);

	/* free filters */
	filterlist_free(peer->out_rules);

	RB_REMOVE(peer_tree, &peertable, peer);
	while (RB_INSERT(peer_tree, &zombietable, peer) != NULL) {
		log_warnx("zombie peer conflict");
		peer->conf.id = arc4random();
	}

	/* start reaping the zombie */
	peer_reaper(peer);
}

/*
 * Flush all routes older then staletime. If staletime is 0 all routes will
 * be flushed.
 */
void
peer_flush(struct rde_peer *peer, uint8_t aid, monotime_t staletime)
{
	struct peer_flush pf = { peer, staletime };

	/* this dump must run synchronous, too much depends on that right now */
	if (rib_dump_new(RIB_ADJ_IN, aid, 0, &pf, peer_flush_upcall,
	    NULL, NULL) == -1)
		fatal("%s: rib_dump_new", __func__);

	/* every route is gone so reset staletime */
	if (aid == AID_UNSPEC) {
		uint8_t i;
		for (i = AID_MIN; i < AID_MAX; i++)
			peer->staletime[i] = monotime_clear();
	} else {
		peer->staletime[aid] = monotime_clear();
	}
}

/*
 * During graceful restart mark a peer as stale if the session goes down.
 * For the specified AID the Adj-RIB-Out is marked stale and the staletime
 * is set to the current timestamp for identifying stale routes in Adj-RIB-In.
 */
void
peer_stale(struct rde_peer *peer, uint8_t aid, int flushall)
{
	monotime_t now;

	/* flush the now even staler routes out */
	if (monotime_valid(peer->staletime[aid]))
		peer_flush(peer, aid, peer->staletime[aid]);

	peer->staletime[aid] = now = getmonotime();
	peer->state = PEER_DOWN;

	/*
	 * stop all pending dumps which may depend on this peer
	 * and flush all pending imsg from the SE.
	 */
	rib_dump_terminate(peer);
	prefix_adjout_flush_pending(peer);
	peer_imsg_flush(peer);

	if (flushall)
		peer_flush(peer, aid, monotime_clear());

	/* make sure new prefixes start on a higher timestamp */
	while (monotime_cmp(now, getmonotime()) >= 0) {
		struct timespec ts = { .tv_nsec = 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
}

/*
 * RIB walker callback for peer_blast.
 * Enqueue a prefix onto the update queue so it can be sent out.
 */
static void
peer_blast_upcall(struct prefix *p, void *ptr)
{
	struct rde_peer		*peer;

	if (p->flags & PREFIX_FLAG_DEAD) {
		/* ignore dead prefixes, they will go away soon */
	} else if ((p->flags & PREFIX_FLAG_MASK) == 0) {
		peer = prefix_peer(p);
		/* put entries on the update queue if not already on a queue */
		p->flags |= PREFIX_FLAG_UPDATE;
		if (RB_INSERT(prefix_tree, &peer->updates[p->pt->aid],
		    p) != NULL)
			fatalx("%s: RB tree invariant violated", __func__);
		peer->stats.pending_update++;
	}
}

/*
 * Called after all prefixes are put onto the update queue and we are
 * ready to blast out updates to the peer.
 */
static void
peer_blast_done(void *ptr, uint8_t aid)
{
	struct rde_peer		*peer = ptr;

	/* Adj-RIB-Out ready, unthrottle peer and inject EOR */
	peer->throttled = 0;
	if (peer->capa.grestart.restart)
		prefix_add_eor(peer, aid);
}

/*
 * Send out the full Adj-RIB-Out by putting all prefixes onto the update
 * queue.
 */
void
peer_blast(struct rde_peer *peer, uint8_t aid)
{
	if (peer->capa.enhanced_rr && (peer->sent_eor & (1 << aid)))
		rde_peer_send_rrefresh(peer, aid, ROUTE_REFRESH_BEGIN_RR);

	/* force out all updates from the Adj-RIB-Out */
	if (prefix_dump_new(peer, aid, 0, peer, peer_blast_upcall,
	    peer_blast_done, NULL) == -1)
		fatal("%s: prefix_dump_new", __func__);
}

/* RIB walker callbacks for peer_dump. */
static void
peer_dump_upcall(struct rib_entry *re, void *ptr)
{
	struct rde_peer		*peer = ptr;
	struct prefix		*p;

	if ((p = prefix_best(re)) == NULL)
		/* no eligible prefix, not even for 'evaluate all' */
		return;

	peer_generate_update(peer, re, NULL, NULL, 0);
}

static void
peer_dump_done(void *ptr, uint8_t aid)
{
	struct rde_peer		*peer = ptr;

	/* Adj-RIB-Out is ready, blast it out */
	peer_blast(peer, aid);
}

/*
 * Load the Adj-RIB-Out of a peer normally called when a session comes up
 * for the first time. Once the Adj-RIB-Out is ready it will blast the
 * updates out.
 */
void
peer_dump(struct rde_peer *peer, uint8_t aid)
{
	/* throttle peer until dump is done */
	peer->throttled = 1;

	if (peer->export_type == EXPORT_NONE) {
		peer_blast(peer, aid);
	} else if (peer->export_type == EXPORT_DEFAULT_ROUTE) {
		up_generate_default(peer, aid);
		peer_blast(peer, aid);
	} else if (aid == AID_FLOWSPECv4 || aid == AID_FLOWSPECv6) {
		prefix_flowspec_dump(aid, peer, peer_dump_upcall,
		    peer_dump_done);
	} else {
		if (rib_dump_new(peer->loc_rib_id, aid, RDE_RUNNER_ROUNDS, peer,
		    peer_dump_upcall, peer_dump_done, NULL) == -1)
			fatal("%s: rib_dump_new", __func__);
	}
}

/*
 * Start of an enhanced route refresh. Mark all routes as stale.
 * Once the route refresh ends a End of Route Refresh message is sent
 * which calls peer_flush() to remove all stale routes.
 */
void
peer_begin_rrefresh(struct rde_peer *peer, uint8_t aid)
{
	monotime_t now;

	/* flush the now even staler routes out */
	if (monotime_valid(peer->staletime[aid]))
		peer_flush(peer, aid, peer->staletime[aid]);

	peer->staletime[aid] = now = getmonotime();

	/* make sure new prefixes start on a higher timestamp */
	while (monotime_cmp(now, getmonotime()) >= 0) {
		struct timespec ts = { .tv_nsec = 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
}

void
peer_reaper(struct rde_peer *peer)
{
	if (peer == NULL)
		peer = RB_ROOT(&zombietable);
	if (peer == NULL)
		return;

	if (!prefix_adjout_reaper(peer))
		return;

	ibufq_free(peer->ibufq);
	RB_REMOVE(peer_tree, &zombietable, peer);
	free(peer);
}

/*
 * Check if any imsg are pending or any zombie peers are around.
 * Return 0 if no work is pending.
 */
int
peer_work_pending(void)
{
	struct rde_peer *p;

	if (!RB_EMPTY(&zombietable))
		return 1;

	RB_FOREACH(p, peer_tree, &peertable) {
		if (ibufq_queuelen(p->ibufq) != 0)
			return 1;
	}

	return 0;
}

/*
 * push an imsg onto the peer imsg queue.
 */
void
peer_imsg_push(struct rde_peer *peer, struct imsg *imsg)
{
	imsg_ibufq_push(peer->ibufq, imsg);
}

/*
 * pop first imsg from peer imsg queue and move it into imsg argument.
 * Returns 1 if an element is returned else 0.
 */
int
peer_imsg_pop(struct rde_peer *peer, struct imsg *imsg)
{
	switch (imsg_ibufq_pop(peer->ibufq, imsg)) {
	case 0:
		return 0;
	case 1:
		return 1;
	default:
		fatal("imsg_ibufq_pop");
	}
}

/*
 * flush all imsg queued for a peer.
 */
void
peer_imsg_flush(struct rde_peer *peer)
{
	ibufq_flush(peer->ibufq);
}
