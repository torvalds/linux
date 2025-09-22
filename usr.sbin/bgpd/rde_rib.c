/*	$OpenBSD: rde_rib.c,v 1.269 2025/04/24 20:17:49 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Claudio Jeker <claudio@openbsd.org>
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bgpd.h"
#include "rde.h"
#include "log.h"

/*
 * BGP RIB -- Routing Information Base
 *
 * The RIB is build with one aspect in mind. Speed -- actually update speed.
 * Therefore one thing needs to be absolutely avoided, long table walks.
 * This is achieved by heavily linking the different parts together.
 */
uint16_t rib_size;
struct rib **ribs;
struct rib flowrib = { .id = 1, .tree = RB_INITIALIZER(&flowrib.tree) };

struct rib_entry *rib_add(struct rib *, struct pt_entry *);
static inline int rib_compare(const struct rib_entry *,
			const struct rib_entry *);
void rib_remove(struct rib_entry *);
int rib_empty(struct rib_entry *);
static void rib_dump_abort(uint16_t);

RB_PROTOTYPE(rib_tree, rib_entry, rib_e, rib_compare);
RB_GENERATE(rib_tree, rib_entry, rib_e, rib_compare);

struct rib_context {
	LIST_ENTRY(rib_context)		 entry;
	struct rib_entry		*ctx_re;
	struct prefix			*ctx_p;
	uint32_t			 ctx_id;
	void		(*ctx_rib_call)(struct rib_entry *, void *);
	void		(*ctx_prefix_call)(struct prefix *, void *);
	void		(*ctx_done)(void *, uint8_t);
	int		(*ctx_throttle)(void *);
	void				*ctx_arg;
	struct bgpd_addr		 ctx_subtree;
	unsigned int			 ctx_count;
	uint8_t				 ctx_aid;
	uint8_t				 ctx_subtreelen;
};
LIST_HEAD(, rib_context) rib_dumps = LIST_HEAD_INITIALIZER(rib_dumps);

static void	prefix_dump_r(struct rib_context *);

static inline struct rib_entry *
re_lock(struct rib_entry *re)
{
	if (re->lock != 0)
		log_warnx("%s: entry already locked", __func__);
	re->lock = 1;
	return re;
}

static inline struct rib_entry *
re_unlock(struct rib_entry *re)
{
	if (re->lock == 0)
		log_warnx("%s: entry already unlocked", __func__);
	re->lock = 0;
	return re;
}

static inline int
re_is_locked(struct rib_entry *re)
{
	return (re->lock != 0);
}

static inline struct prefix *
prefix_lock(struct prefix *p)
{
	if (p->flags & PREFIX_FLAG_LOCKED)
		fatalx("%s: locking locked prefix", __func__);
	p->flags |= PREFIX_FLAG_LOCKED;
	return p;
}

static inline struct prefix *
prefix_unlock(struct prefix *p)
{
	if ((p->flags & PREFIX_FLAG_LOCKED) == 0)
		fatalx("%s: unlocking unlocked prefix", __func__);
	p->flags &= ~PREFIX_FLAG_LOCKED;
	return p;
}

static inline int
prefix_is_locked(struct prefix *p)
{
	return (p->flags & PREFIX_FLAG_LOCKED) != 0;
}

static inline int
prefix_is_dead(struct prefix *p)
{
	return (p->flags & PREFIX_FLAG_DEAD) != 0;
}

static inline struct rib_tree *
rib_tree(struct rib *rib)
{
	return (&rib->tree);
}

static inline int
rib_compare(const struct rib_entry *a, const struct rib_entry *b)
{
	return (pt_prefix_cmp(a->prefix, b->prefix));
}

/* RIB specific functions */
struct rib *
rib_new(char *name, u_int rtableid, uint16_t flags)
{
	struct rib *new;
	uint16_t id;

	for (id = 0; id < rib_size; id++) {
		if (ribs[id] == NULL)
			break;
	}

	if (id >= rib_size) {
		if ((ribs = recallocarray(ribs, id, id + 8,
		    sizeof(struct rib *))) == NULL)
			fatal(NULL);
		rib_size = id + 8;
	}

	if ((new = calloc(1, sizeof(*new))) == NULL)
		fatal(NULL);

	strlcpy(new->name, name, sizeof(new->name));
	RB_INIT(rib_tree(new));
	new->state = RECONF_REINIT;
	new->id = id;
	new->flags = flags;
	new->rtableid = rtableid;

	new->in_rules = calloc(1, sizeof(struct filter_head));
	if (new->in_rules == NULL)
		fatal(NULL);
	TAILQ_INIT(new->in_rules);

	ribs[id] = new;

	log_debug("%s: %s -> %u", __func__, name, id);
	return (new);
}

/*
 * This function is only called when the FIB information of a RIB changed.
 * It will flush the FIB if there was one previously and change the fibstate
 * from RECONF_NONE (nothing to do) to either RECONF_RELOAD (reload the FIB)
 * or RECONF_REINIT (rerun the route decision process for every element)
 * depending on the new flags.
 */
int
rib_update(struct rib *rib)
{
	/* flush fib first if there was one */
	if ((rib->flags & (F_RIB_NOFIB | F_RIB_NOEVALUATE)) == 0)
		rde_send_kroute_flush(rib);

	/* if no evaluate changes then a full reinit is needed */
	if ((rib->flags & F_RIB_NOEVALUATE) !=
	    (rib->flags_tmp & F_RIB_NOEVALUATE))
		rib->fibstate = RECONF_REINIT;

	rib->flags = rib->flags_tmp;
	rib->rtableid = rib->rtableid_tmp;

	/* reload fib if there is no reinit pending and there will be a fib */
	if (rib->fibstate != RECONF_REINIT &&
	    (rib->flags & (F_RIB_NOFIB | F_RIB_NOEVALUATE)) == 0)
		rib->fibstate = RECONF_RELOAD;

	return (rib->fibstate == RECONF_REINIT);
}

struct rib *
rib_byid(uint16_t id)
{
	if (id == RIB_NOTFOUND || id >= rib_size || ribs[id] == NULL)
		return NULL;
	return ribs[id];
}

uint16_t
rib_find(char *name)
{
	uint16_t id;

	/* no name returns the first Loc-RIB */
	if (name == NULL || *name == '\0')
		return RIB_LOC_START;

	for (id = 0; id < rib_size; id++) {
		/* cannot trust name to be properly terminated */
		if (ribs[id] != NULL &&
		    !strncmp(ribs[id]->name, name, sizeof(ribs[id]->name)))
			return id;
	}

	return RIB_NOTFOUND;
}

void
rib_free(struct rib *rib)
{
	struct rib_entry *re, *xre;
	struct prefix *p;

	rib_dump_abort(rib->id);

	/*
	 * flush the rib, disable route evaluation and fib sync to speed up
	 * the prefix removal. Nothing depends on this data anymore.
	 */
	if ((rib->flags & (F_RIB_NOFIB | F_RIB_NOEVALUATE)) == 0)
		rde_send_kroute_flush(rib);
	rib->flags |= F_RIB_NOEVALUATE | F_RIB_NOFIB;

	for (re = RB_MIN(rib_tree, rib_tree(rib)); re != NULL; re = xre) {
		xre = RB_NEXT(rib_tree, rib_tree(rib), re);

		/*
		 * Removing the prefixes is tricky because the last one
		 * will remove the rib_entry as well and because we do
		 * an empty check in prefix_destroy() it is not possible to
		 * use the default for loop.
		 */
		while ((p = TAILQ_FIRST(&re->prefix_h))) {
			struct rde_aspath *asp = prefix_aspath(p);
			if (asp && asp->pftableid)
				rde_pftable_del(asp->pftableid, p);
			prefix_destroy(p);
		}
	}
	if (rib->id <= RIB_LOC_START)
		return; /* never remove the default ribs */
	filterlist_free(rib->in_rules_tmp);
	filterlist_free(rib->in_rules);
	ribs[rib->id] = NULL;
	free(rib);
}

void
rib_shutdown(void)
{
	struct rib *rib;
	uint16_t id;

	for (id = 0; id < rib_size; id++) {
		rib = rib_byid(id);
		if (rib == NULL)
			continue;
		if (!RB_EMPTY(rib_tree(ribs[id]))) {
			log_warnx("%s: rib %s is not empty", __func__,
			    ribs[id]->name);
		}
		rib_free(ribs[id]);
	}
	for (id = 0; id <= RIB_LOC_START; id++) {
		rib = rib_byid(id);
		if (rib == NULL)
			continue;
		filterlist_free(rib->in_rules_tmp);
		filterlist_free(rib->in_rules);
		ribs[id] = NULL;
		free(rib);
	}
	free(ribs);
}

struct rib_entry *
rib_get(struct rib *rib, struct pt_entry *pte)
{
	struct rib_entry xre, *re;

	memset(&xre, 0, sizeof(xre));
	xre.prefix = pte;

	re = RB_FIND(rib_tree, rib_tree(rib), &xre);
	if (re && re->rib_id != rib->id)
		fatalx("%s: Unexpected RIB %u != %u.", __func__,
		    re->rib_id, rib->id);
	return re;
}

struct rib_entry *
rib_get_addr(struct rib *rib, struct bgpd_addr *prefix, int prefixlen)
{
	return rib_get(rib, pt_fill(prefix, prefixlen));
}

struct rib_entry *
rib_match(struct rib *rib, struct bgpd_addr *addr)
{
	struct rib_entry *re;
	int		 i;

	switch (addr->aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		for (i = 32; i >= 0; i--) {
			re = rib_get_addr(rib, addr, i);
			if (re != NULL)
				return (re);
		}
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		for (i = 128; i >= 0; i--) {
			re = rib_get_addr(rib, addr, i);
			if (re != NULL)
				return (re);
		}
		break;
	default:
		fatalx("%s: unknown af", __func__);
	}
	return (NULL);
}


struct rib_entry *
rib_add(struct rib *rib, struct pt_entry *pte)
{
	struct rib_entry *re;

	if ((re = calloc(1, sizeof(*re))) == NULL)
		fatal("rib_add");

	TAILQ_INIT(&re->prefix_h);
	re->prefix = pt_ref(pte);
	re->rib_id = rib->id;

	if (RB_INSERT(rib_tree, rib_tree(rib), re) != NULL) {
		log_warnx("rib_add: insert failed");
		free(re);
		return (NULL);
	}


	rdemem.rib_cnt++;

	return (re);
}

void
rib_remove(struct rib_entry *re)
{
	if (!rib_empty(re))
		fatalx("rib_remove: entry not empty");

	if (re_is_locked(re))
		/* entry is locked, don't free it. */
		return;

	pt_unref(re->prefix);

	if (RB_REMOVE(rib_tree, rib_tree(re_rib(re)), re) == NULL)
		log_warnx("rib_remove: remove failed.");

	free(re);
	rdemem.rib_cnt--;
}

int
rib_empty(struct rib_entry *re)
{
	return TAILQ_EMPTY(&re->prefix_h);
}

static struct rib_entry *
rib_restart(struct rib_context *ctx)
{
	struct rib_entry *re = NULL;

	if (ctx->ctx_re)
		re = re_unlock(ctx->ctx_re);

	/* find first non empty element */
	while (re && rib_empty(re))
		re = RB_NEXT(rib_tree, unused, re);

	/* free the previously locked rib element if empty */
	if (ctx->ctx_re && rib_empty(ctx->ctx_re))
		rib_remove(ctx->ctx_re);
	ctx->ctx_re = NULL;
	return (re);
}

static void
rib_dump_r(struct rib_context *ctx)
{
	struct rib_entry	*re, *next;
	struct rib		*rib;
	unsigned int		 i;

	rib = rib_byid(ctx->ctx_id);
	if (rib == NULL)
		fatalx("%s: rib id %u gone", __func__, ctx->ctx_id);

	if (ctx->ctx_re == NULL && ctx->ctx_subtree.aid == AID_UNSPEC)
		re = RB_MIN(rib_tree, rib_tree(rib));
	else
		re = rib_restart(ctx);

	for (i = 0; re != NULL; re = next) {
		next = RB_NEXT(rib_tree, unused, re);
		if (re->rib_id != ctx->ctx_id)
			fatalx("%s: Unexpected RIB %u != %u.", __func__,
			    re->rib_id, ctx->ctx_id);
		if (ctx->ctx_aid != AID_UNSPEC &&
		    ctx->ctx_aid != re->prefix->aid)
			continue;
		if (ctx->ctx_subtree.aid != AID_UNSPEC) {
			struct bgpd_addr addr;
			pt_getaddr(re->prefix, &addr);
			if (prefix_compare(&ctx->ctx_subtree, &addr,
			    ctx->ctx_subtreelen) != 0)
				/* left subtree, walk is done */
				break;
		}
		if (ctx->ctx_count && i++ >= ctx->ctx_count &&
		    !re_is_locked(re)) {
			/* store and lock last element */
			ctx->ctx_re = re_lock(re);
			return;
		}
		ctx->ctx_rib_call(re, ctx->ctx_arg);
	}

	if (ctx->ctx_done)
		ctx->ctx_done(ctx->ctx_arg, ctx->ctx_aid);
	LIST_REMOVE(ctx, entry);
	free(ctx);
}

int
rib_dump_pending(void)
{
	struct rib_context *ctx;

	/* return true if at least one context is not throttled */
	LIST_FOREACH(ctx, &rib_dumps, entry) {
		if (ctx->ctx_throttle && ctx->ctx_throttle(ctx->ctx_arg))
			continue;
		return 1;
	}
	return 0;
}

void
rib_dump_runner(void)
{
	struct rib_context *ctx, *next;

	LIST_FOREACH_SAFE(ctx, &rib_dumps, entry, next) {
		if (ctx->ctx_throttle && ctx->ctx_throttle(ctx->ctx_arg))
			continue;
		if (ctx->ctx_rib_call != NULL)
			rib_dump_r(ctx);
		else
			prefix_dump_r(ctx);
	}
}

static void
rib_dump_abort(uint16_t id)
{
	struct rib_context *ctx, *next;

	LIST_FOREACH_SAFE(ctx, &rib_dumps, entry, next) {
		if (id != ctx->ctx_id)
			continue;
		if (ctx->ctx_done)
			ctx->ctx_done(ctx->ctx_arg, ctx->ctx_aid);
		if (ctx->ctx_re && rib_empty(re_unlock(ctx->ctx_re)))
			rib_remove(ctx->ctx_re);
		if (ctx->ctx_p && prefix_is_dead(prefix_unlock(ctx->ctx_p)))
			prefix_adjout_destroy(ctx->ctx_p);
		LIST_REMOVE(ctx, entry);
		free(ctx);
	}
}

void
rib_dump_terminate(void *arg)
{
	struct rib_context *ctx, *next;

	LIST_FOREACH_SAFE(ctx, &rib_dumps, entry, next) {
		if (ctx->ctx_arg != arg)
			continue;
		if (ctx->ctx_done)
			ctx->ctx_done(ctx->ctx_arg, ctx->ctx_aid);
		if (ctx->ctx_re && rib_empty(re_unlock(ctx->ctx_re)))
			rib_remove(ctx->ctx_re);
		if (ctx->ctx_p && prefix_is_dead(prefix_unlock(ctx->ctx_p)))
			prefix_adjout_destroy(ctx->ctx_p);
		LIST_REMOVE(ctx, entry);
		free(ctx);
	}
}

int
rib_dump_new(uint16_t id, uint8_t aid, unsigned int count, void *arg,
    void (*upcall)(struct rib_entry *, void *), void (*done)(void *, uint8_t),
    int (*throttle)(void *))
{
	struct rib_context *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return -1;
	ctx->ctx_id = id;
	ctx->ctx_aid = aid;
	ctx->ctx_count = count;
	ctx->ctx_arg = arg;
	ctx->ctx_rib_call = upcall;
	ctx->ctx_done = done;
	ctx->ctx_throttle = throttle;

	LIST_INSERT_HEAD(&rib_dumps, ctx, entry);

	/* requested a sync traversal */
	if (count == 0)
		rib_dump_r(ctx);

	return 0;
}

int
rib_dump_subtree(uint16_t id, struct bgpd_addr *subtree, uint8_t subtreelen,
    unsigned int count, void *arg, void (*upcall)(struct rib_entry *, void *),
    void (*done)(void *, uint8_t), int (*throttle)(void *))
{
	struct rib_context *ctx;
	struct rib_entry xre;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return -1;
	ctx->ctx_id = id;
	ctx->ctx_aid = subtree->aid;
	ctx->ctx_count = count;
	ctx->ctx_arg = arg;
	ctx->ctx_rib_call = upcall;
	ctx->ctx_done = done;
	ctx->ctx_throttle = throttle;
	ctx->ctx_subtree = *subtree;
	ctx->ctx_subtreelen = subtreelen;

	LIST_INSERT_HEAD(&rib_dumps, ctx, entry);

	/* lookup start of subtree */
	memset(&xre, 0, sizeof(xre));
	xre.prefix = pt_fill(subtree, subtreelen);
	ctx->ctx_re = RB_NFIND(rib_tree, rib_tree(rib_byid(id)), &xre);
	if (ctx->ctx_re)
		re_lock(ctx->ctx_re);

	/* requested a sync traversal */
	if (count == 0)
		rib_dump_r(ctx);

	return 0;
}

/* path specific functions */

static struct rde_aspath *path_lookup(struct rde_aspath *);
static void path_link(struct rde_aspath *);
static void path_unlink(struct rde_aspath *);

static inline int
path_compare(struct rde_aspath *a, struct rde_aspath *b)
{
	int		 r;

	if (a == NULL && b == NULL)
		return (0);
	else if (b == NULL)
		return (1);
	else if (a == NULL)
		return (-1);
	if ((a->flags & ~F_ATTR_LINKED) > (b->flags & ~F_ATTR_LINKED))
		return (1);
	if ((a->flags & ~F_ATTR_LINKED) < (b->flags & ~F_ATTR_LINKED))
		return (-1);
	if (a->origin > b->origin)
		return (1);
	if (a->origin < b->origin)
		return (-1);
	if (a->med > b->med)
		return (1);
	if (a->med < b->med)
		return (-1);
	if (a->lpref > b->lpref)
		return (1);
	if (a->lpref < b->lpref)
		return (-1);
	if (a->weight > b->weight)
		return (1);
	if (a->weight < b->weight)
		return (-1);
	if (a->rtlabelid > b->rtlabelid)
		return (1);
	if (a->rtlabelid < b->rtlabelid)
		return (-1);
	if (a->pftableid > b->pftableid)
		return (1);
	if (a->pftableid < b->pftableid)
		return (-1);

	/* no need to check aspa_state or aspa_generation */

	r = aspath_compare(a->aspath, b->aspath);
	if (r > 0)
		return (1);
	if (r < 0)
		return (-1);

	return (attr_compare(a, b));
}

RB_HEAD(path_tree, rde_aspath)	pathtable = RB_INITIALIZER(&pathtable);
RB_GENERATE_STATIC(path_tree, rde_aspath, entry, path_compare);

static inline struct rde_aspath *
path_ref(struct rde_aspath *asp)
{
	if ((asp->flags & F_ATTR_LINKED) == 0)
		fatalx("%s: unlinked object", __func__);
	asp->refcnt++;
	rdemem.path_refs++;

	return asp;
}

static inline void
path_unref(struct rde_aspath *asp)
{
	if (asp == NULL)
		return;
	if ((asp->flags & F_ATTR_LINKED) == 0)
		fatalx("%s: unlinked object", __func__);
	asp->refcnt--;
	rdemem.path_refs--;
	if (asp->refcnt <= 0)
		path_unlink(asp);
}

void
path_shutdown(void)
{
	if (!RB_EMPTY(&pathtable))
		log_warnx("path_free: free non-free table");
}

static struct rde_aspath *
path_lookup(struct rde_aspath *aspath)
{
	return (RB_FIND(path_tree, &pathtable, aspath));
}

/*
 * Link this aspath into the global table.
 * The asp had to be alloced with path_get.
 */
static void
path_link(struct rde_aspath *asp)
{
	if (RB_INSERT(path_tree, &pathtable, asp) != NULL)
		fatalx("%s: already linked object", __func__);
	asp->flags |= F_ATTR_LINKED;
}

/*
 * This function can only be called when all prefix have been removed first.
 * Normally this happens directly out of the prefix removal functions.
 */
static void
path_unlink(struct rde_aspath *asp)
{
	if (asp == NULL)
		return;

	/* make sure no reference is hold for this rde_aspath */
	if (asp->refcnt != 0)
		fatalx("%s: still holds references", __func__);

	RB_REMOVE(path_tree, &pathtable, asp);
	asp->flags &= ~F_ATTR_LINKED;

	path_put(asp);
}

/*
 * Copy asp to a new UNLINKED aspath.
 * On dst either path_get() or path_prep() had to be called before.
 */
struct rde_aspath *
path_copy(struct rde_aspath *dst, const struct rde_aspath *src)
{
	dst->aspath = aspath_copy(src->aspath);
	dst->refcnt = 0;
	dst->flags = src->flags & ~F_ATTR_LINKED;

	dst->med = src->med;
	dst->lpref = src->lpref;
	dst->weight = src->weight;
	dst->rtlabelid = rtlabel_ref(src->rtlabelid);
	dst->pftableid = pftable_ref(src->pftableid);
	dst->origin = src->origin;

	attr_copy(dst, src);

	return (dst);
}

/* initialize or prepare an aspath for use */
struct rde_aspath *
path_prep(struct rde_aspath *asp)
{
	memset(asp, 0, sizeof(*asp));
	asp->origin = ORIGIN_INCOMPLETE;
	asp->lpref = DEFAULT_LPREF;

	return (asp);
}

/* alloc and initialize new entry. May not fail. */
struct rde_aspath *
path_get(void)
{
	struct rde_aspath *asp;

	asp = malloc(sizeof(*asp));
	if (asp == NULL)
		fatal("path_get");
	rdemem.path_cnt++;

	return (path_prep(asp));
}

/* clean up an asp after use (frees all references to sub-objects) */
void
path_clean(struct rde_aspath *asp)
{
	if (asp->flags & F_ATTR_LINKED)
		fatalx("%s: linked object", __func__);

	rtlabel_unref(asp->rtlabelid);
	pftable_unref(asp->pftableid);
	aspath_put(asp->aspath);
	asp->aspath = NULL;
	attr_freeall(asp);
}

/* free an unlinked element */
void
path_put(struct rde_aspath *asp)
{
	if (asp == NULL)
		return;

	path_clean(asp);

	rdemem.path_cnt--;
	free(asp);
}

/* prefix specific functions */

static int	prefix_add(struct bgpd_addr *, int, struct rib *,
		    struct rde_peer *, uint32_t, uint32_t, struct rde_aspath *,
		    struct rde_community *, struct nexthop *,
		    uint8_t, uint8_t, int);
static int	prefix_move(struct prefix *, struct rde_peer *,
		    struct rde_aspath *, struct rde_community *,
		    struct nexthop *, uint8_t, uint8_t, int);

static void	prefix_link(struct prefix *, struct rib_entry *,
		    struct pt_entry *, struct rde_peer *, uint32_t, uint32_t,
		    struct rde_aspath *, struct rde_community *,
		    struct nexthop *, uint8_t, uint8_t);
static void	prefix_unlink(struct prefix *);

static struct prefix	*prefix_alloc(void);
static void		 prefix_free(struct prefix *);

/* RB tree comparison function */
static inline int
prefix_index_cmp(struct prefix *a, struct prefix *b)
{
	int r;
	r = pt_prefix_cmp(a->pt, b->pt);
	if (r != 0)
		return r;

	if (a->path_id_tx > b->path_id_tx)
		return 1;
	if (a->path_id_tx < b->path_id_tx)
		return -1;
	return 0;
}

static inline int
prefix_cmp(struct prefix *a, struct prefix *b)
{
	if ((a->flags & PREFIX_FLAG_EOR) != (b->flags & PREFIX_FLAG_EOR))
		return (a->flags & PREFIX_FLAG_EOR) ? 1 : -1;
	/* if EOR marker no need to check the rest */
	if (a->flags & PREFIX_FLAG_EOR)
		return 0;

	if (a->aspath != b->aspath)
		return (a->aspath > b->aspath ? 1 : -1);
	if (a->communities != b->communities)
		return (a->communities > b->communities ? 1 : -1);
	if (a->nexthop != b->nexthop)
		return (a->nexthop > b->nexthop ? 1 : -1);
	if (a->nhflags != b->nhflags)
		return (a->nhflags > b->nhflags ? 1 : -1);
	return prefix_index_cmp(a, b);
}

RB_GENERATE(prefix_tree, prefix, entry.tree.update, prefix_cmp)
RB_GENERATE_STATIC(prefix_index, prefix, entry.tree.index, prefix_index_cmp)

/*
 * Search for specified prefix of a peer. Returns NULL if not found.
 */
struct prefix *
prefix_get(struct rib *rib, struct rde_peer *peer, uint32_t path_id,
    struct bgpd_addr *prefix, int prefixlen)
{
	struct rib_entry	*re;

	re = rib_get_addr(rib, prefix, prefixlen);
	if (re == NULL)
		return (NULL);
	return (prefix_bypeer(re, peer, path_id));
}

/*
 * Search for specified prefix in the peer prefix_index.
 * Returns NULL if not found.
 */
struct prefix *
prefix_adjout_get(struct rde_peer *peer, uint32_t path_id_tx,
    struct pt_entry *pte)
{
	struct prefix xp;

	memset(&xp, 0, sizeof(xp));
	xp.pt = pte;
	xp.path_id_tx = path_id_tx;

	return RB_FIND(prefix_index, &peer->adj_rib_out, &xp);
}

/*
 * Lookup a prefix without considering path_id in the peer prefix_index.
 * Returns NULL if not found.
 */
struct prefix *
prefix_adjout_first(struct rde_peer *peer, struct pt_entry *pte)
{
	struct prefix xp, *np;

	memset(&xp, 0, sizeof(xp));
	xp.pt = pte;

	np = RB_NFIND(prefix_index, &peer->adj_rib_out, &xp);
	if (np == NULL || pt_prefix_cmp(np->pt, xp.pt) != 0)
		return NULL;
	return np;
}

/*
 * Return next prefix after a lookup that is actually an update.
 */
struct prefix *
prefix_adjout_next(struct rde_peer *peer, struct prefix *p)
{
	struct prefix *np;

	np = RB_NEXT(prefix_index, &peer->adj_rib_out, p);
	if (np == NULL || np->pt != p->pt)
		return NULL;
	return np;
}

/*
 * Lookup addr/prefixlen in the peer prefix_index. Returns first match.
 * Returns NULL if not found.
 */
struct prefix *
prefix_adjout_lookup(struct rde_peer *peer, struct bgpd_addr *addr, int plen)
{
	return prefix_adjout_first(peer, pt_fill(addr, plen));
}

/*
 * Lookup addr in the peer prefix_index. Returns first match.
 * Returns NULL if not found.
 */
struct prefix *
prefix_adjout_match(struct rde_peer *peer, struct bgpd_addr *addr)
{
	struct prefix *p;
	int i;

	switch (addr->aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		for (i = 32; i >= 0; i--) {
			p = prefix_adjout_lookup(peer, addr, i);
			if (p != NULL)
				return p;
		}
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		for (i = 128; i >= 0; i--) {
			p = prefix_adjout_lookup(peer, addr, i);
			if (p != NULL)
				return p;
		}
		break;
	default:
		fatalx("%s: unknown af", __func__);
	}
	return NULL;
}

/*
 * Update a prefix.
 * Return 1 if prefix was newly added, 0 if it was just changed.
 */
int
prefix_update(struct rib *rib, struct rde_peer *peer, uint32_t path_id,
    uint32_t path_id_tx, struct filterstate *state, int filtered,
    struct bgpd_addr *prefix, int prefixlen)
{
	struct rde_aspath	*asp, *nasp = &state->aspath;
	struct rde_community	*comm, *ncomm = &state->communities;
	struct prefix		*p;

	/*
	 * First try to find a prefix in the specified RIB.
	 */
	if ((p = prefix_get(rib, peer, path_id, prefix, prefixlen)) != NULL) {
		if (path_id_tx != p->path_id_tx)
			fatalx("path_id mismatch");
		if (prefix_nexthop(p) == state->nexthop &&
		    prefix_nhflags(p) == state->nhflags &&
		    communities_equal(ncomm, prefix_communities(p)) &&
		    path_compare(nasp, prefix_aspath(p)) == 0) {
			/* no change, update last change */
			p->lastchange = getmonotime();
			p->validation_state = state->vstate;
			if (filtered)
				p->flags |= PREFIX_FLAG_FILTERED;
			else
				p->flags &= ~PREFIX_FLAG_FILTERED;
			return (0);
		}
	}

	/*
	 * Either the prefix does not exist or the path changed.
	 * In both cases lookup the new aspath to make sure it is not
	 * already in the RIB.
	 */
	if ((asp = path_lookup(nasp)) == NULL) {
		/* Path not available, create and link a new one. */
		asp = path_copy(path_get(), nasp);
		path_link(asp);
	}

	if ((comm = communities_lookup(ncomm)) == NULL) {
		/* Communities not available, create and link a new one. */
		comm = communities_link(ncomm);
	}

	/* If the prefix was found move it else add it to the RIB. */
	if (p != NULL)
		return (prefix_move(p, peer, asp, comm, state->nexthop,
		    state->nhflags, state->vstate, filtered));
	else
		return (prefix_add(prefix, prefixlen, rib, peer, path_id,
		    path_id_tx, asp, comm, state->nexthop, state->nhflags,
		    state->vstate, filtered));
}

/*
 * Adds or updates a prefix.
 */
static int
prefix_add(struct bgpd_addr *prefix, int prefixlen, struct rib *rib,
    struct rde_peer *peer, uint32_t path_id, uint32_t path_id_tx,
    struct rde_aspath *asp, struct rde_community *comm,
    struct nexthop *nexthop, uint8_t nhflags, uint8_t vstate, int filtered)
{
	struct pt_entry	*pte;
	struct prefix		*p;
	struct rib_entry	*re;

	pte = pt_get(prefix, prefixlen);
	if (pte == NULL)
		pte = pt_add(prefix, prefixlen);
	re = rib_get(rib, pte);
	if (re == NULL)
		re = rib_add(rib, pte);

	p = prefix_alloc();
	prefix_link(p, re, re->prefix, peer, path_id, path_id_tx, asp, comm,
	    nexthop, nhflags, vstate);

	if (filtered)
		p->flags |= PREFIX_FLAG_FILTERED;

	/* add possible pftable reference form aspath */
	if (asp && asp->pftableid)
		rde_pftable_add(asp->pftableid, p);
	/* make route decision */
	prefix_evaluate(re, p, NULL);
	return (1);
}

/*
 * Move the prefix to the specified as path, removes the old asp if needed.
 */
static int
prefix_move(struct prefix *p, struct rde_peer *peer,
    struct rde_aspath *asp, struct rde_community *comm,
    struct nexthop *nexthop, uint8_t nhflags, uint8_t vstate, int filtered)
{
	struct prefix		*np;

	if (p->flags & PREFIX_FLAG_ADJOUT)
		fatalx("%s: prefix with PREFIX_FLAG_ADJOUT hit", __func__);

	if (peer != prefix_peer(p))
		fatalx("prefix_move: cross peer move");

	/* add new prefix node */
	np = prefix_alloc();
	prefix_link(np, prefix_re(p), p->pt, peer, p->path_id, p->path_id_tx,
	    asp, comm, nexthop, nhflags, vstate);

	if (filtered)
		np->flags |= PREFIX_FLAG_FILTERED;

	/* add possible pftable reference from new aspath */
	if (asp && asp->pftableid)
		rde_pftable_add(asp->pftableid, np);

	/*
	 * no need to update the peer prefix count because we are only moving
	 * the prefix without changing the peer.
	 */
	prefix_evaluate(prefix_re(np), np, p);

	/* remove possible pftable reference from old path */
	if (p->aspath && p->aspath->pftableid)
		rde_pftable_del(p->aspath->pftableid, p);

	/* remove old prefix node */
	prefix_unlink(p);
	prefix_free(p);

	return (0);
}

/*
 * Removes a prefix from the specified RIB. If the parent objects -- rib_entry
 * or pt_entry -- become empty remove them too.
 */
int
prefix_withdraw(struct rib *rib, struct rde_peer *peer, uint32_t path_id,
    struct bgpd_addr *prefix, int prefixlen)
{
	struct prefix		*p;
	struct rde_aspath	*asp;

	p = prefix_get(rib, peer, path_id, prefix, prefixlen);
	if (p == NULL)		/* Got a dummy withdrawn request. */
		return (0);

	if (p->flags & PREFIX_FLAG_ADJOUT)
		fatalx("%s: prefix with PREFIX_FLAG_ADJOUT hit", __func__);
	asp = prefix_aspath(p);
	if (asp && asp->pftableid)
		/* only prefixes in the local RIB were pushed into pf */
		rde_pftable_del(asp->pftableid, p);

	prefix_destroy(p);

	return (1);
}

/*
 * Special functions for flowspec until full integration is available.
 * This just directly feeds the prefixes into the Adj-RIB-Out bypassing
 * Adj-RIB-In and Loc-RIB for now.
 */
int
prefix_flowspec_update(struct rde_peer *peer, struct filterstate *state,
    struct pt_entry *pte, uint32_t path_id_tx)
{
	struct rde_aspath *asp, *nasp;
	struct rde_community *comm, *ncomm;
	struct rib_entry *re;
	struct prefix *new, *old;

	re = rib_get(&flowrib, pte);
	if (re == NULL)
		re = rib_add(&flowrib, pte);

	old = prefix_bypeer(re, peer, 0);
	new = prefix_alloc();

	nasp = &state->aspath;
	ncomm = &state->communities;
	if ((asp = path_lookup(nasp)) == NULL) {
		/* Path not available, create and link a new one. */
		asp = path_copy(path_get(), nasp);
		path_link(asp);
	}
	if ((comm = communities_lookup(ncomm)) == NULL) {
		/* Communities not available, create and link a new one. */
		comm = communities_link(ncomm);
	}

	prefix_link(new, re, re->prefix, peer, 0, path_id_tx, asp, comm,
	    NULL, 0, 0);
	TAILQ_INSERT_HEAD(&re->prefix_h, new, entry.list.rib);

	rde_generate_updates(re, new, old, EVAL_DEFAULT);

	if (old != NULL) {
		TAILQ_REMOVE(&re->prefix_h, old, entry.list.rib);
		prefix_unlink(old);
		prefix_free(old);
		return 0;
	}
	return 1;
}

/*
 * Remove a possible flowspec prefix from all Adj-RIB-Outs.
 */
int
prefix_flowspec_withdraw(struct rde_peer *peer, struct pt_entry *pte)
{
	struct rib_entry *re;
	struct prefix *p;

	re = rib_get(&flowrib, pte);
	if (re == NULL)
		return 0;
	p = prefix_bypeer(re, peer, 0);
	if (p == NULL)
		return 0;
	rde_generate_updates(re, NULL, p, EVAL_DEFAULT);
	TAILQ_REMOVE(&re->prefix_h, p, entry.list.rib);
	prefix_unlink(p);
	prefix_free(p);
	return 1;
}

/*
 * Push all flowspec rules into a newly available Adj-RIB-Out.
 */
void
prefix_flowspec_dump(uint8_t aid, void *arg,
    void (*call)(struct rib_entry *, void *), void (*done)(void *, uint8_t))
{
	struct rib_entry *re, *next;

	RB_FOREACH_SAFE(re, rib_tree, rib_tree(&flowrib), next) {
		if (aid != AID_UNSPEC && aid != re->prefix->aid)
			continue;
		call(re, arg);
	}
	if (done != NULL)
		done(arg, aid);
}

/*
 * Insert an End-of-RIB marker into the update queue.
 */
void
prefix_add_eor(struct rde_peer *peer, uint8_t aid)
{
	struct prefix *p;

	p = prefix_alloc();
	p->flags = PREFIX_FLAG_ADJOUT | PREFIX_FLAG_UPDATE | PREFIX_FLAG_EOR;
	if (RB_INSERT(prefix_tree, &peer->updates[aid], p) != NULL)
		/* no need to add if EoR marker already present */
		prefix_free(p);
	/* EOR marker is not inserted into the adj_rib_out index */
}

/*
 * Put a prefix from the Adj-RIB-Out onto the update queue.
 */
void
prefix_adjout_update(struct prefix *p, struct rde_peer *peer,
    struct filterstate *state, struct pt_entry *pte, uint32_t path_id_tx)
{
	struct rde_aspath *asp;
	struct rde_community *comm;

	if (p == NULL) {
		p = prefix_alloc();
		/* initially mark DEAD so code below is skipped */
		p->flags |= PREFIX_FLAG_ADJOUT | PREFIX_FLAG_DEAD;

		p->pt = pt_ref(pte);
		p->peer = peer;
		p->path_id_tx = path_id_tx;

		if (RB_INSERT(prefix_index, &peer->adj_rib_out, p) != NULL)
			fatalx("%s: RB index invariant violated", __func__);
	}

	if ((p->flags & PREFIX_FLAG_ADJOUT) == 0)
		fatalx("%s: prefix without PREFIX_FLAG_ADJOUT hit", __func__);
	if ((p->flags & (PREFIX_FLAG_WITHDRAW | PREFIX_FLAG_DEAD)) == 0) {
		/*
		 * XXX for now treat a different path_id_tx like different
		 * attributes and force out an update. It is unclear how
		 * common it is to have equivalent updates from alternative
		 * paths.
		 */
		if (p->path_id_tx == path_id_tx &&
		    prefix_nhflags(p) == state->nhflags &&
		    prefix_nexthop(p) == state->nexthop &&
		    communities_equal(&state->communities,
		    prefix_communities(p)) &&
		    path_compare(&state->aspath, prefix_aspath(p)) == 0) {
			/* nothing changed */
			p->validation_state = state->vstate;
			p->lastchange = getmonotime();
			p->flags &= ~PREFIX_FLAG_STALE;
			return;
		}

		/* if pending update unhook it before it is unlinked */
		if (p->flags & PREFIX_FLAG_UPDATE) {
			RB_REMOVE(prefix_tree, &peer->updates[pte->aid], p);
			peer->stats.pending_update--;
		}

		/* unlink prefix so it can be relinked below */
		prefix_unlink(p);
		peer->stats.prefix_out_cnt--;
	}
	if (p->flags & PREFIX_FLAG_WITHDRAW) {
		RB_REMOVE(prefix_tree, &peer->withdraws[pte->aid], p);
		peer->stats.pending_withdraw--;
	}

	/* nothing needs to be done for PREFIX_FLAG_DEAD and STALE */
	p->flags &= ~PREFIX_FLAG_MASK;

	/* update path_id_tx now that the prefix is unlinked */
	if (p->path_id_tx != path_id_tx) {
		/* path_id_tx is part of the index so remove and re-insert p */
		RB_REMOVE(prefix_index, &peer->adj_rib_out, p);
		p->path_id_tx = path_id_tx;
		if (RB_INSERT(prefix_index, &peer->adj_rib_out, p) != NULL)
			fatalx("%s: RB index invariant violated", __func__);
	}

	if ((asp = path_lookup(&state->aspath)) == NULL) {
		/* Path not available, create and link a new one. */
		asp = path_copy(path_get(), &state->aspath);
		path_link(asp);
	}

	if ((comm = communities_lookup(&state->communities)) == NULL) {
		/* Communities not available, create and link a new one. */
		comm = communities_link(&state->communities);
	}

	prefix_link(p, NULL, p->pt, peer, 0, p->path_id_tx, asp, comm,
	    state->nexthop, state->nhflags, state->vstate);
	peer->stats.prefix_out_cnt++;

	if (p->flags & PREFIX_FLAG_MASK)
		fatalx("%s: bad flags %x", __func__, p->flags);
	if (peer_is_up(peer)) {
		p->flags |= PREFIX_FLAG_UPDATE;
		if (RB_INSERT(prefix_tree, &peer->updates[pte->aid], p) != NULL)
			fatalx("%s: RB tree invariant violated", __func__);
		peer->stats.pending_update++;
	}
}

/*
 * Withdraw a prefix from the Adj-RIB-Out, this unlinks the aspath but leaves
 * the prefix in the RIB linked to the peer withdraw list.
 */
void
prefix_adjout_withdraw(struct prefix *p)
{
	struct rde_peer *peer = prefix_peer(p);

	if ((p->flags & PREFIX_FLAG_ADJOUT) == 0)
		fatalx("%s: prefix without PREFIX_FLAG_ADJOUT hit", __func__);

	/* already a withdraw, shortcut */
	if (p->flags & PREFIX_FLAG_WITHDRAW) {
		p->lastchange = getmonotime();
		p->flags &= ~PREFIX_FLAG_STALE;
		return;
	}
	/* pending update just got withdrawn */
	if (p->flags & PREFIX_FLAG_UPDATE) {
		RB_REMOVE(prefix_tree, &peer->updates[p->pt->aid], p);
		peer->stats.pending_update--;
	}
	/* unlink prefix if it was linked (not a withdraw or dead) */
	if ((p->flags & (PREFIX_FLAG_WITHDRAW | PREFIX_FLAG_DEAD)) == 0) {
		prefix_unlink(p);
		peer->stats.prefix_out_cnt--;
	}

	/* nothing needs to be done for PREFIX_FLAG_DEAD and STALE */
	p->flags &= ~PREFIX_FLAG_MASK;
	p->lastchange = getmonotime();

	if (peer_is_up(peer)) {
		p->flags |= PREFIX_FLAG_WITHDRAW;
		if (RB_INSERT(prefix_tree, &peer->withdraws[p->pt->aid],
		    p) != NULL)
			fatalx("%s: RB tree invariant violated", __func__);
		peer->stats.pending_withdraw++;
	} else {
		/* mark prefix dead to skip unlink on destroy */
		p->flags |= PREFIX_FLAG_DEAD;
		prefix_adjout_destroy(p);
	}
}

void
prefix_adjout_destroy(struct prefix *p)
{
	struct rde_peer *peer = prefix_peer(p);

	if ((p->flags & PREFIX_FLAG_ADJOUT) == 0)
		fatalx("%s: prefix without PREFIX_FLAG_ADJOUT hit", __func__);

	if (p->flags & PREFIX_FLAG_EOR) {
		/* EOR marker is not linked in the index */
		prefix_free(p);
		return;
	}

	if (p->flags & PREFIX_FLAG_WITHDRAW) {
		RB_REMOVE(prefix_tree, &peer->withdraws[p->pt->aid], p);
		peer->stats.pending_withdraw--;
	}
	if (p->flags & PREFIX_FLAG_UPDATE) {
		RB_REMOVE(prefix_tree, &peer->updates[p->pt->aid], p);
		peer->stats.pending_update--;
	}
	/* unlink prefix if it was linked (not a withdraw or dead) */
	if ((p->flags & (PREFIX_FLAG_WITHDRAW | PREFIX_FLAG_DEAD)) == 0) {
		prefix_unlink(p);
		peer->stats.prefix_out_cnt--;
	}

	/* nothing needs to be done for PREFIX_FLAG_DEAD and STALE */
	p->flags &= ~PREFIX_FLAG_MASK;

	if (prefix_is_locked(p)) {
		/* mark prefix dead but leave it for prefix_restart */
		p->flags |= PREFIX_FLAG_DEAD;
	} else {
		RB_REMOVE(prefix_index, &peer->adj_rib_out, p);
		/* remove the last prefix reference before free */
		pt_unref(p->pt);
		prefix_free(p);
	}
}

void
prefix_adjout_flush_pending(struct rde_peer *peer)
{
	struct prefix *p, *np;
	uint8_t aid;

	for (aid = AID_MIN; aid < AID_MAX; aid++) {
		RB_FOREACH_SAFE(p, prefix_tree, &peer->withdraws[aid], np) {
			prefix_adjout_destroy(p);
		}
		RB_FOREACH_SAFE(p, prefix_tree, &peer->updates[aid], np) {
			p->flags &= ~PREFIX_FLAG_UPDATE;
			RB_REMOVE(prefix_tree, &peer->updates[aid], p);
			if (p->flags & PREFIX_FLAG_EOR) {
				prefix_adjout_destroy(p);
			} else {
				peer->stats.pending_update--;
			}
		}
	}
}

int
prefix_adjout_reaper(struct rde_peer *peer)
{
	struct prefix *p, *np;
	int count = RDE_REAPER_ROUNDS;

	RB_FOREACH_SAFE(p, prefix_index, &peer->adj_rib_out, np) {
		prefix_adjout_destroy(p);
		if (count-- <= 0)
			return 0;
	}
	return 1;
}

static struct prefix *
prefix_restart(struct rib_context *ctx)
{
	struct prefix *p = NULL;

	if (ctx->ctx_p)
		p = prefix_unlock(ctx->ctx_p);

	if (p && prefix_is_dead(p)) {
		struct prefix *next;

		next = RB_NEXT(prefix_index, unused, p);
		prefix_adjout_destroy(p);
		p = next;
	}
	ctx->ctx_p = NULL;
	return p;
}

static void
prefix_dump_r(struct rib_context *ctx)
{
	struct prefix *p, *next;
	struct rde_peer *peer;
	unsigned int i;

	if ((peer = peer_get(ctx->ctx_id)) == NULL)
		goto done;

	if (ctx->ctx_p == NULL && ctx->ctx_subtree.aid == AID_UNSPEC)
		p = RB_MIN(prefix_index, &peer->adj_rib_out);
	else
		p = prefix_restart(ctx);

	for (i = 0; p != NULL; p = next) {
		next = RB_NEXT(prefix_index, unused, p);
		if (prefix_is_dead(p))
			continue;
		if (ctx->ctx_aid != AID_UNSPEC &&
		    ctx->ctx_aid != p->pt->aid)
			continue;
		if (ctx->ctx_subtree.aid != AID_UNSPEC) {
			struct bgpd_addr addr;
			pt_getaddr(p->pt, &addr);
			if (prefix_compare(&ctx->ctx_subtree, &addr,
			    ctx->ctx_subtreelen) != 0)
				/* left subtree, walk is done */
				break;
		}
		if (ctx->ctx_count && i++ >= ctx->ctx_count &&
		    !prefix_is_locked(p)) {
			/* store and lock last element */
			ctx->ctx_p = prefix_lock(p);
			return;
		}
		ctx->ctx_prefix_call(p, ctx->ctx_arg);
	}

done:
	if (ctx->ctx_done)
		ctx->ctx_done(ctx->ctx_arg, ctx->ctx_aid);
	LIST_REMOVE(ctx, entry);
	free(ctx);
}

int
prefix_dump_new(struct rde_peer *peer, uint8_t aid, unsigned int count,
    void *arg, void (*upcall)(struct prefix *, void *),
    void (*done)(void *, uint8_t), int (*throttle)(void *))
{
	struct rib_context *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return -1;
	ctx->ctx_id = peer->conf.id;
	ctx->ctx_aid = aid;
	ctx->ctx_count = count;
	ctx->ctx_arg = arg;
	ctx->ctx_prefix_call = upcall;
	ctx->ctx_done = done;
	ctx->ctx_throttle = throttle;

	LIST_INSERT_HEAD(&rib_dumps, ctx, entry);

	/* requested a sync traversal */
	if (count == 0)
		prefix_dump_r(ctx);

	return 0;
}

int
prefix_dump_subtree(struct rde_peer *peer, struct bgpd_addr *subtree,
    uint8_t subtreelen, unsigned int count, void *arg,
    void (*upcall)(struct prefix *, void *), void (*done)(void *, uint8_t),
    int (*throttle)(void *))
{
	struct rib_context *ctx;
	struct prefix xp;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return -1;
	ctx->ctx_id = peer->conf.id;
	ctx->ctx_aid = subtree->aid;
	ctx->ctx_count = count;
	ctx->ctx_arg = arg;
	ctx->ctx_prefix_call = upcall;
	ctx->ctx_done = done;
	ctx->ctx_throttle = throttle;
	ctx->ctx_subtree = *subtree;
	ctx->ctx_subtreelen = subtreelen;

	LIST_INSERT_HEAD(&rib_dumps, ctx, entry);

	/* lookup start of subtree */
	memset(&xp, 0, sizeof(xp));
	xp.pt = pt_fill(subtree, subtreelen);
	ctx->ctx_p = RB_NFIND(prefix_index, &peer->adj_rib_out, &xp);
	if (ctx->ctx_p)
		prefix_lock(ctx->ctx_p);

	/* requested a sync traversal */
	if (count == 0)
		prefix_dump_r(ctx);

	return 0;
}

/*
 * Searches in the prefix list of specified rib_entry for a prefix entry
 * belonging to the peer peer. Returns NULL if no match found.
 */
struct prefix *
prefix_bypeer(struct rib_entry *re, struct rde_peer *peer, uint32_t path_id)
{
	struct prefix	*p;

	TAILQ_FOREACH(p, &re->prefix_h, entry.list.rib)
		if (prefix_peer(p) == peer && p->path_id == path_id)
			return (p);
	return (NULL);
}

/* kill a prefix. */
void
prefix_destroy(struct prefix *p)
{
	/* make route decision */
	prefix_evaluate(prefix_re(p), NULL, p);

	prefix_unlink(p);
	prefix_free(p);
}

/*
 * Link a prefix into the different parent objects.
 */
static void
prefix_link(struct prefix *p, struct rib_entry *re, struct pt_entry *pt,
    struct rde_peer *peer, uint32_t path_id, uint32_t path_id_tx,
    struct rde_aspath *asp, struct rde_community *comm,
    struct nexthop *nexthop, uint8_t nhflags, uint8_t vstate)
{
	if (re)
		p->entry.list.re = re;
	p->aspath = path_ref(asp);
	p->communities = communities_ref(comm);
	p->peer = peer;
	p->pt = pt_ref(pt);
	p->path_id = path_id;
	p->path_id_tx = path_id_tx;
	p->validation_state = vstate;
	p->nhflags = nhflags;
	p->nexthop = nexthop_ref(nexthop);
	nexthop_link(p);
	p->lastchange = getmonotime();
}

/*
 * Unlink a prefix from the different parent objects.
 */
static void
prefix_unlink(struct prefix *p)
{
	struct rib_entry	*re = prefix_re(p);

	/* destroy all references to other objects */
	/* remove nexthop ref ... */
	nexthop_unlink(p);
	nexthop_unref(p->nexthop);
	p->nexthop = NULL;
	p->nhflags = 0;
	/* ... communities ... */
	communities_unref(p->communities);
	p->communities = NULL;
	/* and unlink from aspath */
	path_unref(p->aspath);
	p->aspath = NULL;

	if (re && rib_empty(re))
		rib_remove(re);

	pt_unref(p->pt);
}

/* alloc and zero new entry. May not fail. */
static struct prefix *
prefix_alloc(void)
{
	struct prefix *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL)
		fatal("prefix_alloc");
	rdemem.prefix_cnt++;
	return p;
}

/* free a unlinked entry */
static void
prefix_free(struct prefix *p)
{
	rdemem.prefix_cnt--;
	free(p);
}

/*
 * nexthop functions
 */
struct nexthop		*nexthop_lookup(struct bgpd_addr *);

/*
 * In BGP there exist two nexthops: the exit nexthop which was announced via
 * BGP and the true nexthop which is used in the FIB -- forward information
 * base a.k.a kernel routing table. When sending updates it is even more
 * confusing. In IBGP we pass the unmodified exit nexthop to the neighbors
 * while in EBGP normally the address of the router is sent. The exit nexthop
 * may be passed to the external neighbor if the neighbor and the exit nexthop
 * reside in the same subnet -- directly connected.
 */

TAILQ_HEAD(nexthop_queue, nexthop)	nexthop_runners =
				    TAILQ_HEAD_INITIALIZER(nexthop_runners);

RB_HEAD(nexthop_tree, nexthop)		nexthoptable =
					    RB_INITIALIZER(&nexthoptree);

static inline int nexthop_cmp(struct nexthop *, struct nexthop *);

RB_GENERATE_STATIC(nexthop_tree, nexthop, entry, nexthop_cmp);

void
nexthop_shutdown(void)
{
	struct nexthop		*nh, *nnh;

	RB_FOREACH_SAFE(nh, nexthop_tree, &nexthoptable, nnh) {
		nh->state = NEXTHOP_UNREACH;
		nexthop_unref(nh);
	}

	RB_FOREACH(nh, nexthop_tree, &nexthoptable) {
		log_warnx("%s: nexthop %s refcnt %d", __func__,
		    log_addr(&nh->exit_nexthop), nh->refcnt);
	}
}

int
nexthop_pending(void)
{
	return !TAILQ_EMPTY(&nexthop_runners);
}

void
nexthop_runner(void)
{
	struct nexthop *nh;
	struct prefix *p;
	uint32_t j;

	nh = TAILQ_FIRST(&nexthop_runners);
	if (nh == NULL)
		return;

	/* remove from runnner queue */
	TAILQ_REMOVE(&nexthop_runners, nh, runner_l);

	p = nh->next_prefix;
	for (j = 0; p != NULL && j < RDE_RUNNER_ROUNDS; j++) {
		prefix_evaluate_nexthop(p, nh->state, nh->oldstate);
		p = LIST_NEXT(p, entry.list.nexthop);
	}

	/* prep for next run, if not finished readd to tail of queue */
	nh->next_prefix = p;
	if (p != NULL)
		TAILQ_INSERT_TAIL(&nexthop_runners, nh, runner_l);
	else
		log_debug("nexthop %s update finished",
		    log_addr(&nh->exit_nexthop));
}

void
nexthop_update(struct kroute_nexthop *msg)
{
	struct nexthop		*nh;

	nh = nexthop_lookup(&msg->nexthop);
	if (nh == NULL) {
		log_warnx("nexthop_update: non-existent nexthop %s",
		    log_addr(&msg->nexthop));
		return;
	}

	nh->oldstate = nh->state;
	if (msg->valid)
		nh->state = NEXTHOP_REACH;
	else
		nh->state = NEXTHOP_UNREACH;

	if (nh->oldstate == NEXTHOP_LOOKUP)
		/* drop reference which was hold during the lookup */
		if (nexthop_unref(nh))
			return;		/* nh lost last ref, no work left */

	if (nh->next_prefix) {
		/*
		 * If nexthop_runner() is not finished with this nexthop
		 * then ensure that all prefixes are updated by setting
		 * the oldstate to NEXTHOP_FLAPPED.
		 */
		nh->oldstate = NEXTHOP_FLAPPED;
		TAILQ_REMOVE(&nexthop_runners, nh, runner_l);
	}

	if (msg->connected)
		nh->flags |= NEXTHOP_CONNECTED;

	nh->true_nexthop = msg->gateway;
	nh->nexthop_net = msg->net;
	nh->nexthop_netlen = msg->netlen;

	nh->next_prefix = LIST_FIRST(&nh->prefix_h);
	if (nh->next_prefix != NULL) {
		TAILQ_INSERT_HEAD(&nexthop_runners, nh, runner_l);
		log_debug("nexthop %s update starting",
		    log_addr(&nh->exit_nexthop));
	}
}

void
nexthop_modify(struct nexthop *setnh, enum action_types type, uint8_t aid,
    struct nexthop **nexthop, uint8_t *flags)
{
	switch (type) {
	case ACTION_SET_NEXTHOP_REJECT:
		*flags = NEXTHOP_REJECT;
		break;
	case ACTION_SET_NEXTHOP_BLACKHOLE:
		*flags = NEXTHOP_BLACKHOLE;
		break;
	case ACTION_SET_NEXTHOP_NOMODIFY:
		*flags = NEXTHOP_NOMODIFY;
		break;
	case ACTION_SET_NEXTHOP_SELF:
		*flags = NEXTHOP_SELF;
		break;
	case ACTION_SET_NEXTHOP_REF:
		/*
		 * it is possible that a prefix matches but has the wrong
		 * address family for the set nexthop. In this case ignore it.
		 */
		if (aid != setnh->exit_nexthop.aid)
			break;
		nexthop_unref(*nexthop);
		*nexthop = nexthop_ref(setnh);
		*flags = 0;
		break;
	default:
		break;
	}
}

void
nexthop_link(struct prefix *p)
{
	p->nhflags &= ~NEXTHOP_VALID;

	if (p->flags & PREFIX_FLAG_ADJOUT) {
		/* All nexthops are valid in Adj-RIB-Out */
		p->nhflags |= NEXTHOP_VALID;
		return;
	}

	/* no need to link prefixes in RIBs that have no decision process */
	if (re_rib(prefix_re(p))->flags & F_RIB_NOEVALUATE)
		return;

	/* self-announce networks use nexthop NULL and are valid as well */
	if (p->nexthop == NULL || p->nexthop->state == NEXTHOP_REACH)
		p->nhflags |= NEXTHOP_VALID;

	if (p->nexthop == NULL)
		return;
	p->flags |= PREFIX_NEXTHOP_LINKED;
	LIST_INSERT_HEAD(&p->nexthop->prefix_h, p, entry.list.nexthop);
}

void
nexthop_unlink(struct prefix *p)
{
	p->nhflags &= ~NEXTHOP_VALID;

	if (p->nexthop == NULL || (p->flags & PREFIX_NEXTHOP_LINKED) == 0)
		return;

	if (p == p->nexthop->next_prefix) {
		p->nexthop->next_prefix = LIST_NEXT(p, entry.list.nexthop);
		/* remove nexthop from list if no prefixes left to update */
		if (p->nexthop->next_prefix == NULL) {
			TAILQ_REMOVE(&nexthop_runners, p->nexthop, runner_l);
			log_debug("nexthop %s update finished",
			    log_addr(&p->nexthop->exit_nexthop));
		}
	}

	p->flags &= ~PREFIX_NEXTHOP_LINKED;
	LIST_REMOVE(p, entry.list.nexthop);
}

struct nexthop *
nexthop_get(struct bgpd_addr *nexthop)
{
	struct nexthop	*nh;

	nh = nexthop_lookup(nexthop);
	if (nh == NULL) {
		nh = calloc(1, sizeof(*nh));
		if (nh == NULL)
			fatal("nexthop_get");
		rdemem.nexthop_cnt++;

		LIST_INIT(&nh->prefix_h);
		nh->state = NEXTHOP_LOOKUP;
		nexthop_ref(nh);	/* take reference for lookup */
		nh->exit_nexthop = *nexthop;
		if (RB_INSERT(nexthop_tree, &nexthoptable, nh) != NULL)
			fatalx("nexthop tree inconsistency");

		rde_send_nexthop(&nh->exit_nexthop, 1);
	}

	return nexthop_ref(nh);
}

struct nexthop *
nexthop_ref(struct nexthop *nexthop)
{
	if (nexthop)
		nexthop->refcnt++;
	return (nexthop);
}

int
nexthop_unref(struct nexthop *nh)
{
	if (nh == NULL)
		return (0);
	if (--nh->refcnt > 0)
		return (0);

	/* sanity check */
	if (!LIST_EMPTY(&nh->prefix_h) || nh->state == NEXTHOP_LOOKUP)
		fatalx("%s: refcnt error", __func__);

	/* is nexthop update running? impossible, that is a refcnt error */
	if (nh->next_prefix)
		fatalx("%s: next_prefix not NULL", __func__);

	RB_REMOVE(nexthop_tree, &nexthoptable, nh);
	rde_send_nexthop(&nh->exit_nexthop, 0);

	rdemem.nexthop_cnt--;
	free(nh);
	return (1);
}

static inline int
nexthop_cmp(struct nexthop *na, struct nexthop *nb)
{
	struct bgpd_addr	*a, *b;

	if (na == nb)
		return (0);
	if (na == NULL)
		return (-1);
	if (nb == NULL)
		return (1);

	a = &na->exit_nexthop;
	b = &nb->exit_nexthop;

	if (a->aid != b->aid)
		return (a->aid - b->aid);

	switch (a->aid) {
	case AID_INET:
		if (ntohl(a->v4.s_addr) > ntohl(b->v4.s_addr))
			return (1);
		if (ntohl(a->v4.s_addr) < ntohl(b->v4.s_addr))
			return (-1);
		return (0);
	case AID_INET6:
		return (memcmp(&a->v6, &b->v6, sizeof(struct in6_addr)));
	default:
		fatalx("nexthop_cmp: %s is unsupported", aid2str(a->aid));
	}
	return (-1);
}

struct nexthop *
nexthop_lookup(struct bgpd_addr *nexthop)
{
	struct nexthop	needle = { 0 };

	needle.exit_nexthop = *nexthop;
	return RB_FIND(nexthop_tree, &nexthoptable, &needle);
}
