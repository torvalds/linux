// SPDX-License-Identifier: GPL-2.0-only
/*
 * xfrm_policy.c
 *
 * Changes:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 		IPv6 support
 * 	Kazunori MIYAZAWA @USAGI
 * 	YOSHIFUJI Hideaki
 * 		Split up af-specific portion
 *	Derek Atkins <derek@ihtfp.com>		Add the post_input processor
 *
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/cache.h>
#include <linux/cpu.h>
#include <linux/audit.h>
#include <linux/rhashtable.h>
#include <linux/if_tunnel.h>
#include <net/dst.h>
#include <net/flow.h>
#ifndef __GENKSYMS__
#include <net/inet_ecn.h>
#endif
#include <net/xfrm.h>
#include <net/ip.h>
#ifndef __GENKSYMS__
#include <net/gre.h>
#endif
#if IS_ENABLED(CONFIG_IPV6_MIP6)
#include <net/mip6.h>
#endif
#ifdef CONFIG_XFRM_STATISTICS
#include <net/snmp.h>
#endif
#ifdef CONFIG_XFRM_ESPINTCP
#include <net/espintcp.h>
#endif

#include "xfrm_hash.h"

#define XFRM_QUEUE_TMO_MIN ((unsigned)(HZ/10))
#define XFRM_QUEUE_TMO_MAX ((unsigned)(60*HZ))
#define XFRM_MAX_QUEUE_LEN	100

struct xfrm_flo {
	struct dst_entry *dst_orig;
	u8 flags;
};

/* prefixes smaller than this are stored in lists, not trees. */
#define INEXACT_PREFIXLEN_IPV4	16
#define INEXACT_PREFIXLEN_IPV6	48

struct xfrm_pol_inexact_node {
	struct rb_node node;
	union {
		xfrm_address_t addr;
		struct rcu_head rcu;
	};
	u8 prefixlen;

	struct rb_root root;

	/* the policies matching this node, can be empty list */
	struct hlist_head hhead;
};

/* xfrm inexact policy search tree:
 * xfrm_pol_inexact_bin = hash(dir,type,family,if_id);
 *  |
 * +---- root_d: sorted by daddr:prefix
 * |                 |
 * |        xfrm_pol_inexact_node
 * |                 |
 * |                 +- root: sorted by saddr/prefix
 * |                 |              |
 * |                 |         xfrm_pol_inexact_node
 * |                 |              |
 * |                 |              + root: unused
 * |                 |              |
 * |                 |              + hhead: saddr:daddr policies
 * |                 |
 * |                 +- coarse policies and all any:daddr policies
 * |
 * +---- root_s: sorted by saddr:prefix
 * |                 |
 * |        xfrm_pol_inexact_node
 * |                 |
 * |                 + root: unused
 * |                 |
 * |                 + hhead: saddr:any policies
 * |
 * +---- coarse policies and all any:any policies
 *
 * Lookups return four candidate lists:
 * 1. any:any list from top-level xfrm_pol_inexact_bin
 * 2. any:daddr list from daddr tree
 * 3. saddr:daddr list from 2nd level daddr tree
 * 4. saddr:any list from saddr tree
 *
 * This result set then needs to be searched for the policy with
 * the lowest priority.  If two results have same prio, youngest one wins.
 */

struct xfrm_pol_inexact_key {
	possible_net_t net;
	u32 if_id;
	u16 family;
	u8 dir, type;
};

struct xfrm_pol_inexact_bin {
	struct xfrm_pol_inexact_key k;
	struct rhash_head head;
	/* list containing '*:*' policies */
	struct hlist_head hhead;

	seqcount_spinlock_t count;
	/* tree sorted by daddr/prefix */
	struct rb_root root_d;

	/* tree sorted by saddr/prefix */
	struct rb_root root_s;

	/* slow path below */
	struct list_head inexact_bins;
	struct rcu_head rcu;
};

enum xfrm_pol_inexact_candidate_type {
	XFRM_POL_CAND_BOTH,
	XFRM_POL_CAND_SADDR,
	XFRM_POL_CAND_DADDR,
	XFRM_POL_CAND_ANY,

	XFRM_POL_CAND_MAX,
};

struct xfrm_pol_inexact_candidates {
	struct hlist_head *res[XFRM_POL_CAND_MAX];
};

static DEFINE_SPINLOCK(xfrm_if_cb_lock);
static struct xfrm_if_cb const __rcu *xfrm_if_cb __read_mostly;

static DEFINE_SPINLOCK(xfrm_policy_afinfo_lock);
static struct xfrm_policy_afinfo const __rcu *xfrm_policy_afinfo[AF_INET6 + 1]
						__read_mostly;

static struct kmem_cache *xfrm_dst_cache __ro_after_init;
static __read_mostly seqcount_mutex_t xfrm_policy_hash_generation;

static struct rhashtable xfrm_policy_inexact_table;
static const struct rhashtable_params xfrm_pol_inexact_params;

static void xfrm_init_pmtu(struct xfrm_dst **bundle, int nr);
static int stale_bundle(struct dst_entry *dst);
static int xfrm_bundle_ok(struct xfrm_dst *xdst);
static void xfrm_policy_queue_process(struct timer_list *t);

static void __xfrm_policy_link(struct xfrm_policy *pol, int dir);
static struct xfrm_policy *__xfrm_policy_unlink(struct xfrm_policy *pol,
						int dir);

static struct xfrm_pol_inexact_bin *
xfrm_policy_inexact_lookup(struct net *net, u8 type, u16 family, u8 dir,
			   u32 if_id);

static struct xfrm_pol_inexact_bin *
xfrm_policy_inexact_lookup_rcu(struct net *net,
			       u8 type, u16 family, u8 dir, u32 if_id);
static struct xfrm_policy *
xfrm_policy_insert_list(struct hlist_head *chain, struct xfrm_policy *policy,
			bool excl);
static void xfrm_policy_insert_inexact_list(struct hlist_head *chain,
					    struct xfrm_policy *policy);

static bool
xfrm_policy_find_inexact_candidates(struct xfrm_pol_inexact_candidates *cand,
				    struct xfrm_pol_inexact_bin *b,
				    const xfrm_address_t *saddr,
				    const xfrm_address_t *daddr);

static inline bool xfrm_pol_hold_rcu(struct xfrm_policy *policy)
{
	return refcount_inc_not_zero(&policy->refcnt);
}

static inline bool
__xfrm4_selector_match(const struct xfrm_selector *sel, const struct flowi *fl)
{
	const struct flowi4 *fl4 = &fl->u.ip4;

	return  addr4_match(fl4->daddr, sel->daddr.a4, sel->prefixlen_d) &&
		addr4_match(fl4->saddr, sel->saddr.a4, sel->prefixlen_s) &&
		!((xfrm_flowi_dport(fl, &fl4->uli) ^ sel->dport) & sel->dport_mask) &&
		!((xfrm_flowi_sport(fl, &fl4->uli) ^ sel->sport) & sel->sport_mask) &&
		(fl4->flowi4_proto == sel->proto || !sel->proto) &&
		(fl4->flowi4_oif == sel->ifindex || !sel->ifindex);
}

static inline bool
__xfrm6_selector_match(const struct xfrm_selector *sel, const struct flowi *fl)
{
	const struct flowi6 *fl6 = &fl->u.ip6;

	return  addr_match(&fl6->daddr, &sel->daddr, sel->prefixlen_d) &&
		addr_match(&fl6->saddr, &sel->saddr, sel->prefixlen_s) &&
		!((xfrm_flowi_dport(fl, &fl6->uli) ^ sel->dport) & sel->dport_mask) &&
		!((xfrm_flowi_sport(fl, &fl6->uli) ^ sel->sport) & sel->sport_mask) &&
		(fl6->flowi6_proto == sel->proto || !sel->proto) &&
		(fl6->flowi6_oif == sel->ifindex || !sel->ifindex);
}

bool xfrm_selector_match(const struct xfrm_selector *sel, const struct flowi *fl,
			 unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_selector_match(sel, fl);
	case AF_INET6:
		return __xfrm6_selector_match(sel, fl);
	}
	return false;
}

static const struct xfrm_policy_afinfo *xfrm_policy_get_afinfo(unsigned short family)
{
	const struct xfrm_policy_afinfo *afinfo;

	if (unlikely(family >= ARRAY_SIZE(xfrm_policy_afinfo)))
		return NULL;
	rcu_read_lock();
	afinfo = rcu_dereference(xfrm_policy_afinfo[family]);
	if (unlikely(!afinfo))
		rcu_read_unlock();
	return afinfo;
}

/* Called with rcu_read_lock(). */
static const struct xfrm_if_cb *xfrm_if_get_cb(void)
{
	return rcu_dereference(xfrm_if_cb);
}

struct dst_entry *__xfrm_dst_lookup(struct net *net, int tos, int oif,
				    const xfrm_address_t *saddr,
				    const xfrm_address_t *daddr,
				    int family, u32 mark)
{
	const struct xfrm_policy_afinfo *afinfo;
	struct dst_entry *dst;

	afinfo = xfrm_policy_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return ERR_PTR(-EAFNOSUPPORT);

	dst = afinfo->dst_lookup(net, tos, oif, saddr, daddr, mark);

	rcu_read_unlock();

	return dst;
}
EXPORT_SYMBOL(__xfrm_dst_lookup);

static inline struct dst_entry *xfrm_dst_lookup(struct xfrm_state *x,
						int tos, int oif,
						xfrm_address_t *prev_saddr,
						xfrm_address_t *prev_daddr,
						int family, u32 mark)
{
	struct net *net = xs_net(x);
	xfrm_address_t *saddr = &x->props.saddr;
	xfrm_address_t *daddr = &x->id.daddr;
	struct dst_entry *dst;

	if (x->type->flags & XFRM_TYPE_LOCAL_COADDR) {
		saddr = x->coaddr;
		daddr = prev_daddr;
	}
	if (x->type->flags & XFRM_TYPE_REMOTE_COADDR) {
		saddr = prev_saddr;
		daddr = x->coaddr;
	}

	dst = __xfrm_dst_lookup(net, tos, oif, saddr, daddr, family, mark);

	if (!IS_ERR(dst)) {
		if (prev_saddr != saddr)
			memcpy(prev_saddr, saddr,  sizeof(*prev_saddr));
		if (prev_daddr != daddr)
			memcpy(prev_daddr, daddr,  sizeof(*prev_daddr));
	}

	return dst;
}

static inline unsigned long make_jiffies(long secs)
{
	if (secs >= (MAX_SCHEDULE_TIMEOUT-1)/HZ)
		return MAX_SCHEDULE_TIMEOUT-1;
	else
		return secs*HZ;
}

static void xfrm_policy_timer(struct timer_list *t)
{
	struct xfrm_policy *xp = from_timer(xp, t, timer);
	time64_t now = ktime_get_real_seconds();
	time64_t next = TIME64_MAX;
	int warn = 0;
	int dir;

	read_lock(&xp->lock);

	if (unlikely(xp->walk.dead))
		goto out;

	dir = xfrm_policy_id2dir(xp->index);

	if (xp->lft.hard_add_expires_seconds) {
		time64_t tmo = xp->lft.hard_add_expires_seconds +
			xp->curlft.add_time - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.hard_use_expires_seconds) {
		time64_t tmo = xp->lft.hard_use_expires_seconds +
			(xp->curlft.use_time ? : xp->curlft.add_time) - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.soft_add_expires_seconds) {
		time64_t tmo = xp->lft.soft_add_expires_seconds +
			xp->curlft.add_time - now;
		if (tmo <= 0) {
			warn = 1;
			tmo = XFRM_KM_TIMEOUT;
		}
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.soft_use_expires_seconds) {
		time64_t tmo = xp->lft.soft_use_expires_seconds +
			(xp->curlft.use_time ? : xp->curlft.add_time) - now;
		if (tmo <= 0) {
			warn = 1;
			tmo = XFRM_KM_TIMEOUT;
		}
		if (tmo < next)
			next = tmo;
	}

	if (warn)
		km_policy_expired(xp, dir, 0, 0);
	if (next != TIME64_MAX &&
	    !mod_timer(&xp->timer, jiffies + make_jiffies(next)))
		xfrm_pol_hold(xp);

out:
	read_unlock(&xp->lock);
	xfrm_pol_put(xp);
	return;

expired:
	read_unlock(&xp->lock);
	if (!xfrm_policy_delete(xp, dir))
		km_policy_expired(xp, dir, 1, 0);
	xfrm_pol_put(xp);
}

/* Allocate xfrm_policy. Not used here, it is supposed to be used by pfkeyv2
 * SPD calls.
 */

struct xfrm_policy *xfrm_policy_alloc(struct net *net, gfp_t gfp)
{
	struct xfrm_policy *policy;

	policy = kzalloc(sizeof(struct xfrm_policy), gfp);

	if (policy) {
		write_pnet(&policy->xp_net, net);
		INIT_LIST_HEAD(&policy->walk.all);
		INIT_HLIST_NODE(&policy->bydst_inexact_list);
		INIT_HLIST_NODE(&policy->bydst);
		INIT_HLIST_NODE(&policy->byidx);
		rwlock_init(&policy->lock);
		refcount_set(&policy->refcnt, 1);
		skb_queue_head_init(&policy->polq.hold_queue);
		timer_setup(&policy->timer, xfrm_policy_timer, 0);
		timer_setup(&policy->polq.hold_timer,
			    xfrm_policy_queue_process, 0);
	}
	return policy;
}
EXPORT_SYMBOL(xfrm_policy_alloc);

static void xfrm_policy_destroy_rcu(struct rcu_head *head)
{
	struct xfrm_policy *policy = container_of(head, struct xfrm_policy, rcu);

	security_xfrm_policy_free(policy->security);
	kfree(policy);
}

/* Destroy xfrm_policy: descendant resources must be released to this moment. */

void xfrm_policy_destroy(struct xfrm_policy *policy)
{
	BUG_ON(!policy->walk.dead);

	if (del_timer(&policy->timer) || del_timer(&policy->polq.hold_timer))
		BUG();

	call_rcu(&policy->rcu, xfrm_policy_destroy_rcu);
}
EXPORT_SYMBOL(xfrm_policy_destroy);

/* Rule must be locked. Release descendant resources, announce
 * entry dead. The rule must be unlinked from lists to the moment.
 */

static void xfrm_policy_kill(struct xfrm_policy *policy)
{
	write_lock_bh(&policy->lock);
	policy->walk.dead = 1;
	write_unlock_bh(&policy->lock);

	atomic_inc(&policy->genid);

	if (del_timer(&policy->polq.hold_timer))
		xfrm_pol_put(policy);
	skb_queue_purge(&policy->polq.hold_queue);

	if (del_timer(&policy->timer))
		xfrm_pol_put(policy);

	xfrm_pol_put(policy);
}

static unsigned int xfrm_policy_hashmax __read_mostly = 1 * 1024 * 1024;

static inline unsigned int idx_hash(struct net *net, u32 index)
{
	return __idx_hash(index, net->xfrm.policy_idx_hmask);
}

/* calculate policy hash thresholds */
static void __get_hash_thresh(struct net *net,
			      unsigned short family, int dir,
			      u8 *dbits, u8 *sbits)
{
	switch (family) {
	case AF_INET:
		*dbits = net->xfrm.policy_bydst[dir].dbits4;
		*sbits = net->xfrm.policy_bydst[dir].sbits4;
		break;

	case AF_INET6:
		*dbits = net->xfrm.policy_bydst[dir].dbits6;
		*sbits = net->xfrm.policy_bydst[dir].sbits6;
		break;

	default:
		*dbits = 0;
		*sbits = 0;
	}
}

static struct hlist_head *policy_hash_bysel(struct net *net,
					    const struct xfrm_selector *sel,
					    unsigned short family, int dir)
{
	unsigned int hmask = net->xfrm.policy_bydst[dir].hmask;
	unsigned int hash;
	u8 dbits;
	u8 sbits;

	__get_hash_thresh(net, family, dir, &dbits, &sbits);
	hash = __sel_hash(sel, family, hmask, dbits, sbits);

	if (hash == hmask + 1)
		return NULL;

	return rcu_dereference_check(net->xfrm.policy_bydst[dir].table,
		     lockdep_is_held(&net->xfrm.xfrm_policy_lock)) + hash;
}

static struct hlist_head *policy_hash_direct(struct net *net,
					     const xfrm_address_t *daddr,
					     const xfrm_address_t *saddr,
					     unsigned short family, int dir)
{
	unsigned int hmask = net->xfrm.policy_bydst[dir].hmask;
	unsigned int hash;
	u8 dbits;
	u8 sbits;

	__get_hash_thresh(net, family, dir, &dbits, &sbits);
	hash = __addr_hash(daddr, saddr, family, hmask, dbits, sbits);

	return rcu_dereference_check(net->xfrm.policy_bydst[dir].table,
		     lockdep_is_held(&net->xfrm.xfrm_policy_lock)) + hash;
}

static void xfrm_dst_hash_transfer(struct net *net,
				   struct hlist_head *list,
				   struct hlist_head *ndsttable,
				   unsigned int nhashmask,
				   int dir)
{
	struct hlist_node *tmp, *entry0 = NULL;
	struct xfrm_policy *pol;
	unsigned int h0 = 0;
	u8 dbits;
	u8 sbits;

redo:
	hlist_for_each_entry_safe(pol, tmp, list, bydst) {
		unsigned int h;

		__get_hash_thresh(net, pol->family, dir, &dbits, &sbits);
		h = __addr_hash(&pol->selector.daddr, &pol->selector.saddr,
				pol->family, nhashmask, dbits, sbits);
		if (!entry0) {
			hlist_del_rcu(&pol->bydst);
			hlist_add_head_rcu(&pol->bydst, ndsttable + h);
			h0 = h;
		} else {
			if (h != h0)
				continue;
			hlist_del_rcu(&pol->bydst);
			hlist_add_behind_rcu(&pol->bydst, entry0);
		}
		entry0 = &pol->bydst;
	}
	if (!hlist_empty(list)) {
		entry0 = NULL;
		goto redo;
	}
}

static void xfrm_idx_hash_transfer(struct hlist_head *list,
				   struct hlist_head *nidxtable,
				   unsigned int nhashmask)
{
	struct hlist_node *tmp;
	struct xfrm_policy *pol;

	hlist_for_each_entry_safe(pol, tmp, list, byidx) {
		unsigned int h;

		h = __idx_hash(pol->index, nhashmask);
		hlist_add_head(&pol->byidx, nidxtable+h);
	}
}

static unsigned long xfrm_new_hash_mask(unsigned int old_hmask)
{
	return ((old_hmask + 1) << 1) - 1;
}

static void xfrm_bydst_resize(struct net *net, int dir)
{
	unsigned int hmask = net->xfrm.policy_bydst[dir].hmask;
	unsigned int nhashmask = xfrm_new_hash_mask(hmask);
	unsigned int nsize = (nhashmask + 1) * sizeof(struct hlist_head);
	struct hlist_head *ndst = xfrm_hash_alloc(nsize);
	struct hlist_head *odst;
	int i;

	if (!ndst)
		return;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	write_seqcount_begin(&xfrm_policy_hash_generation);

	odst = rcu_dereference_protected(net->xfrm.policy_bydst[dir].table,
				lockdep_is_held(&net->xfrm.xfrm_policy_lock));

	for (i = hmask; i >= 0; i--)
		xfrm_dst_hash_transfer(net, odst + i, ndst, nhashmask, dir);

	rcu_assign_pointer(net->xfrm.policy_bydst[dir].table, ndst);
	net->xfrm.policy_bydst[dir].hmask = nhashmask;

	write_seqcount_end(&xfrm_policy_hash_generation);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	synchronize_rcu();

	xfrm_hash_free(odst, (hmask + 1) * sizeof(struct hlist_head));
}

static void xfrm_byidx_resize(struct net *net, int total)
{
	unsigned int hmask = net->xfrm.policy_idx_hmask;
	unsigned int nhashmask = xfrm_new_hash_mask(hmask);
	unsigned int nsize = (nhashmask + 1) * sizeof(struct hlist_head);
	struct hlist_head *oidx = net->xfrm.policy_byidx;
	struct hlist_head *nidx = xfrm_hash_alloc(nsize);
	int i;

	if (!nidx)
		return;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);

	for (i = hmask; i >= 0; i--)
		xfrm_idx_hash_transfer(oidx + i, nidx, nhashmask);

	net->xfrm.policy_byidx = nidx;
	net->xfrm.policy_idx_hmask = nhashmask;

	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	xfrm_hash_free(oidx, (hmask + 1) * sizeof(struct hlist_head));
}

static inline int xfrm_bydst_should_resize(struct net *net, int dir, int *total)
{
	unsigned int cnt = net->xfrm.policy_count[dir];
	unsigned int hmask = net->xfrm.policy_bydst[dir].hmask;

	if (total)
		*total += cnt;

	if ((hmask + 1) < xfrm_policy_hashmax &&
	    cnt > hmask)
		return 1;

	return 0;
}

static inline int xfrm_byidx_should_resize(struct net *net, int total)
{
	unsigned int hmask = net->xfrm.policy_idx_hmask;

	if ((hmask + 1) < xfrm_policy_hashmax &&
	    total > hmask)
		return 1;

	return 0;
}

void xfrm_spd_getinfo(struct net *net, struct xfrmk_spdinfo *si)
{
	si->incnt = net->xfrm.policy_count[XFRM_POLICY_IN];
	si->outcnt = net->xfrm.policy_count[XFRM_POLICY_OUT];
	si->fwdcnt = net->xfrm.policy_count[XFRM_POLICY_FWD];
	si->inscnt = net->xfrm.policy_count[XFRM_POLICY_IN+XFRM_POLICY_MAX];
	si->outscnt = net->xfrm.policy_count[XFRM_POLICY_OUT+XFRM_POLICY_MAX];
	si->fwdscnt = net->xfrm.policy_count[XFRM_POLICY_FWD+XFRM_POLICY_MAX];
	si->spdhcnt = net->xfrm.policy_idx_hmask;
	si->spdhmcnt = xfrm_policy_hashmax;
}
EXPORT_SYMBOL(xfrm_spd_getinfo);

static DEFINE_MUTEX(hash_resize_mutex);
static void xfrm_hash_resize(struct work_struct *work)
{
	struct net *net = container_of(work, struct net, xfrm.policy_hash_work);
	int dir, total;

	mutex_lock(&hash_resize_mutex);

	total = 0;
	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		if (xfrm_bydst_should_resize(net, dir, &total))
			xfrm_bydst_resize(net, dir);
	}
	if (xfrm_byidx_should_resize(net, total))
		xfrm_byidx_resize(net, total);

	mutex_unlock(&hash_resize_mutex);
}

/* Make sure *pol can be inserted into fastbin.
 * Useful to check that later insert requests will be sucessful
 * (provided xfrm_policy_lock is held throughout).
 */
static struct xfrm_pol_inexact_bin *
xfrm_policy_inexact_alloc_bin(const struct xfrm_policy *pol, u8 dir)
{
	struct xfrm_pol_inexact_bin *bin, *prev;
	struct xfrm_pol_inexact_key k = {
		.family = pol->family,
		.type = pol->type,
		.dir = dir,
		.if_id = pol->if_id,
	};
	struct net *net = xp_net(pol);

	lockdep_assert_held(&net->xfrm.xfrm_policy_lock);

	write_pnet(&k.net, net);
	bin = rhashtable_lookup_fast(&xfrm_policy_inexact_table, &k,
				     xfrm_pol_inexact_params);
	if (bin)
		return bin;

	bin = kzalloc(sizeof(*bin), GFP_ATOMIC);
	if (!bin)
		return NULL;

	bin->k = k;
	INIT_HLIST_HEAD(&bin->hhead);
	bin->root_d = RB_ROOT;
	bin->root_s = RB_ROOT;
	seqcount_spinlock_init(&bin->count, &net->xfrm.xfrm_policy_lock);

	prev = rhashtable_lookup_get_insert_key(&xfrm_policy_inexact_table,
						&bin->k, &bin->head,
						xfrm_pol_inexact_params);
	if (!prev) {
		list_add(&bin->inexact_bins, &net->xfrm.inexact_bins);
		return bin;
	}

	kfree(bin);

	return IS_ERR(prev) ? NULL : prev;
}

static bool xfrm_pol_inexact_addr_use_any_list(const xfrm_address_t *addr,
					       int family, u8 prefixlen)
{
	if (xfrm_addr_any(addr, family))
		return true;

	if (family == AF_INET6 && prefixlen < INEXACT_PREFIXLEN_IPV6)
		return true;

	if (family == AF_INET && prefixlen < INEXACT_PREFIXLEN_IPV4)
		return true;

	return false;
}

static bool
xfrm_policy_inexact_insert_use_any_list(const struct xfrm_policy *policy)
{
	const xfrm_address_t *addr;
	bool saddr_any, daddr_any;
	u8 prefixlen;

	addr = &policy->selector.saddr;
	prefixlen = policy->selector.prefixlen_s;

	saddr_any = xfrm_pol_inexact_addr_use_any_list(addr,
						       policy->family,
						       prefixlen);
	addr = &policy->selector.daddr;
	prefixlen = policy->selector.prefixlen_d;
	daddr_any = xfrm_pol_inexact_addr_use_any_list(addr,
						       policy->family,
						       prefixlen);
	return saddr_any && daddr_any;
}

static void xfrm_pol_inexact_node_init(struct xfrm_pol_inexact_node *node,
				       const xfrm_address_t *addr, u8 prefixlen)
{
	node->addr = *addr;
	node->prefixlen = prefixlen;
}

static struct xfrm_pol_inexact_node *
xfrm_pol_inexact_node_alloc(const xfrm_address_t *addr, u8 prefixlen)
{
	struct xfrm_pol_inexact_node *node;

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (node)
		xfrm_pol_inexact_node_init(node, addr, prefixlen);

	return node;
}

static int xfrm_policy_addr_delta(const xfrm_address_t *a,
				  const xfrm_address_t *b,
				  u8 prefixlen, u16 family)
{
	u32 ma, mb, mask;
	unsigned int pdw, pbi;
	int delta = 0;

	switch (family) {
	case AF_INET:
		if (prefixlen == 0)
			return 0;
		mask = ~0U << (32 - prefixlen);
		ma = ntohl(a->a4) & mask;
		mb = ntohl(b->a4) & mask;
		if (ma < mb)
			delta = -1;
		else if (ma > mb)
			delta = 1;
		break;
	case AF_INET6:
		pdw = prefixlen >> 5;
		pbi = prefixlen & 0x1f;

		if (pdw) {
			delta = memcmp(a->a6, b->a6, pdw << 2);
			if (delta)
				return delta;
		}
		if (pbi) {
			mask = ~0U << (32 - pbi);
			ma = ntohl(a->a6[pdw]) & mask;
			mb = ntohl(b->a6[pdw]) & mask;
			if (ma < mb)
				delta = -1;
			else if (ma > mb)
				delta = 1;
		}
		break;
	default:
		break;
	}

	return delta;
}

static void xfrm_policy_inexact_list_reinsert(struct net *net,
					      struct xfrm_pol_inexact_node *n,
					      u16 family)
{
	unsigned int matched_s, matched_d;
	struct xfrm_policy *policy, *p;

	matched_s = 0;
	matched_d = 0;

	list_for_each_entry_reverse(policy, &net->xfrm.policy_all, walk.all) {
		struct hlist_node *newpos = NULL;
		bool matches_s, matches_d;

		if (!policy->bydst_reinsert)
			continue;

		WARN_ON_ONCE(policy->family != family);

		policy->bydst_reinsert = false;
		hlist_for_each_entry(p, &n->hhead, bydst) {
			if (policy->priority > p->priority)
				newpos = &p->bydst;
			else if (policy->priority == p->priority &&
				 policy->pos > p->pos)
				newpos = &p->bydst;
			else
				break;
		}

		if (newpos)
			hlist_add_behind_rcu(&policy->bydst, newpos);
		else
			hlist_add_head_rcu(&policy->bydst, &n->hhead);

		/* paranoia checks follow.
		 * Check that the reinserted policy matches at least
		 * saddr or daddr for current node prefix.
		 *
		 * Matching both is fine, matching saddr in one policy
		 * (but not daddr) and then matching only daddr in another
		 * is a bug.
		 */
		matches_s = xfrm_policy_addr_delta(&policy->selector.saddr,
						   &n->addr,
						   n->prefixlen,
						   family) == 0;
		matches_d = xfrm_policy_addr_delta(&policy->selector.daddr,
						   &n->addr,
						   n->prefixlen,
						   family) == 0;
		if (matches_s && matches_d)
			continue;

		WARN_ON_ONCE(!matches_s && !matches_d);
		if (matches_s)
			matched_s++;
		if (matches_d)
			matched_d++;
		WARN_ON_ONCE(matched_s && matched_d);
	}
}

static void xfrm_policy_inexact_node_reinsert(struct net *net,
					      struct xfrm_pol_inexact_node *n,
					      struct rb_root *new,
					      u16 family)
{
	struct xfrm_pol_inexact_node *node;
	struct rb_node **p, *parent;

	/* we should not have another subtree here */
	WARN_ON_ONCE(!RB_EMPTY_ROOT(&n->root));
restart:
	parent = NULL;
	p = &new->rb_node;
	while (*p) {
		u8 prefixlen;
		int delta;

		parent = *p;
		node = rb_entry(*p, struct xfrm_pol_inexact_node, node);

		prefixlen = min(node->prefixlen, n->prefixlen);

		delta = xfrm_policy_addr_delta(&n->addr, &node->addr,
					       prefixlen, family);
		if (delta < 0) {
			p = &parent->rb_left;
		} else if (delta > 0) {
			p = &parent->rb_right;
		} else {
			bool same_prefixlen = node->prefixlen == n->prefixlen;
			struct xfrm_policy *tmp;

			hlist_for_each_entry(tmp, &n->hhead, bydst) {
				tmp->bydst_reinsert = true;
				hlist_del_rcu(&tmp->bydst);
			}

			node->prefixlen = prefixlen;

			xfrm_policy_inexact_list_reinsert(net, node, family);

			if (same_prefixlen) {
				kfree_rcu(n, rcu);
				return;
			}

			rb_erase(*p, new);
			kfree_rcu(n, rcu);
			n = node;
			goto restart;
		}
	}

	rb_link_node_rcu(&n->node, parent, p);
	rb_insert_color(&n->node, new);
}

/* merge nodes v and n */
static void xfrm_policy_inexact_node_merge(struct net *net,
					   struct xfrm_pol_inexact_node *v,
					   struct xfrm_pol_inexact_node *n,
					   u16 family)
{
	struct xfrm_pol_inexact_node *node;
	struct xfrm_policy *tmp;
	struct rb_node *rnode;

	/* To-be-merged node v has a subtree.
	 *
	 * Dismantle it and insert its nodes to n->root.
	 */
	while ((rnode = rb_first(&v->root)) != NULL) {
		node = rb_entry(rnode, struct xfrm_pol_inexact_node, node);
		rb_erase(&node->node, &v->root);
		xfrm_policy_inexact_node_reinsert(net, node, &n->root,
						  family);
	}

	hlist_for_each_entry(tmp, &v->hhead, bydst) {
		tmp->bydst_reinsert = true;
		hlist_del_rcu(&tmp->bydst);
	}

	xfrm_policy_inexact_list_reinsert(net, n, family);
}

static struct xfrm_pol_inexact_node *
xfrm_policy_inexact_insert_node(struct net *net,
				struct rb_root *root,
				xfrm_address_t *addr,
				u16 family, u8 prefixlen, u8 dir)
{
	struct xfrm_pol_inexact_node *cached = NULL;
	struct rb_node **p, *parent = NULL;
	struct xfrm_pol_inexact_node *node;

	p = &root->rb_node;
	while (*p) {
		int delta;

		parent = *p;
		node = rb_entry(*p, struct xfrm_pol_inexact_node, node);

		delta = xfrm_policy_addr_delta(addr, &node->addr,
					       node->prefixlen,
					       family);
		if (delta == 0 && prefixlen >= node->prefixlen) {
			WARN_ON_ONCE(cached); /* ipsec policies got lost */
			return node;
		}

		if (delta < 0)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;

		if (prefixlen < node->prefixlen) {
			delta = xfrm_policy_addr_delta(addr, &node->addr,
						       prefixlen,
						       family);
			if (delta)
				continue;

			/* This node is a subnet of the new prefix. It needs
			 * to be removed and re-inserted with the smaller
			 * prefix and all nodes that are now also covered
			 * by the reduced prefixlen.
			 */
			rb_erase(&node->node, root);

			if (!cached) {
				xfrm_pol_inexact_node_init(node, addr,
							   prefixlen);
				cached = node;
			} else {
				/* This node also falls within the new
				 * prefixlen. Merge the to-be-reinserted
				 * node and this one.
				 */
				xfrm_policy_inexact_node_merge(net, node,
							       cached, family);
				kfree_rcu(node, rcu);
			}

			/* restart */
			p = &root->rb_node;
			parent = NULL;
		}
	}

	node = cached;
	if (!node) {
		node = xfrm_pol_inexact_node_alloc(addr, prefixlen);
		if (!node)
			return NULL;
	}

	rb_link_node_rcu(&node->node, parent, p);
	rb_insert_color(&node->node, root);

	return node;
}

static void xfrm_policy_inexact_gc_tree(struct rb_root *r, bool rm)
{
	struct xfrm_pol_inexact_node *node;
	struct rb_node *rn = rb_first(r);

	while (rn) {
		node = rb_entry(rn, struct xfrm_pol_inexact_node, node);

		xfrm_policy_inexact_gc_tree(&node->root, rm);
		rn = rb_next(rn);

		if (!hlist_empty(&node->hhead) || !RB_EMPTY_ROOT(&node->root)) {
			WARN_ON_ONCE(rm);
			continue;
		}

		rb_erase(&node->node, r);
		kfree_rcu(node, rcu);
	}
}

static void __xfrm_policy_inexact_prune_bin(struct xfrm_pol_inexact_bin *b, bool net_exit)
{
	write_seqcount_begin(&b->count);
	xfrm_policy_inexact_gc_tree(&b->root_d, net_exit);
	xfrm_policy_inexact_gc_tree(&b->root_s, net_exit);
	write_seqcount_end(&b->count);

	if (!RB_EMPTY_ROOT(&b->root_d) || !RB_EMPTY_ROOT(&b->root_s) ||
	    !hlist_empty(&b->hhead)) {
		WARN_ON_ONCE(net_exit);
		return;
	}

	if (rhashtable_remove_fast(&xfrm_policy_inexact_table, &b->head,
				   xfrm_pol_inexact_params) == 0) {
		list_del(&b->inexact_bins);
		kfree_rcu(b, rcu);
	}
}

static void xfrm_policy_inexact_prune_bin(struct xfrm_pol_inexact_bin *b)
{
	struct net *net = read_pnet(&b->k.net);

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	__xfrm_policy_inexact_prune_bin(b, false);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
}

static void __xfrm_policy_inexact_flush(struct net *net)
{
	struct xfrm_pol_inexact_bin *bin, *t;

	lockdep_assert_held(&net->xfrm.xfrm_policy_lock);

	list_for_each_entry_safe(bin, t, &net->xfrm.inexact_bins, inexact_bins)
		__xfrm_policy_inexact_prune_bin(bin, false);
}

static struct hlist_head *
xfrm_policy_inexact_alloc_chain(struct xfrm_pol_inexact_bin *bin,
				struct xfrm_policy *policy, u8 dir)
{
	struct xfrm_pol_inexact_node *n;
	struct net *net;

	net = xp_net(policy);
	lockdep_assert_held(&net->xfrm.xfrm_policy_lock);

	if (xfrm_policy_inexact_insert_use_any_list(policy))
		return &bin->hhead;

	if (xfrm_pol_inexact_addr_use_any_list(&policy->selector.daddr,
					       policy->family,
					       policy->selector.prefixlen_d)) {
		write_seqcount_begin(&bin->count);
		n = xfrm_policy_inexact_insert_node(net,
						    &bin->root_s,
						    &policy->selector.saddr,
						    policy->family,
						    policy->selector.prefixlen_s,
						    dir);
		write_seqcount_end(&bin->count);
		if (!n)
			return NULL;

		return &n->hhead;
	}

	/* daddr is fixed */
	write_seqcount_begin(&bin->count);
	n = xfrm_policy_inexact_insert_node(net,
					    &bin->root_d,
					    &policy->selector.daddr,
					    policy->family,
					    policy->selector.prefixlen_d, dir);
	write_seqcount_end(&bin->count);
	if (!n)
		return NULL;

	/* saddr is wildcard */
	if (xfrm_pol_inexact_addr_use_any_list(&policy->selector.saddr,
					       policy->family,
					       policy->selector.prefixlen_s))
		return &n->hhead;

	write_seqcount_begin(&bin->count);
	n = xfrm_policy_inexact_insert_node(net,
					    &n->root,
					    &policy->selector.saddr,
					    policy->family,
					    policy->selector.prefixlen_s, dir);
	write_seqcount_end(&bin->count);
	if (!n)
		return NULL;

	return &n->hhead;
}

static struct xfrm_policy *
xfrm_policy_inexact_insert(struct xfrm_policy *policy, u8 dir, int excl)
{
	struct xfrm_pol_inexact_bin *bin;
	struct xfrm_policy *delpol;
	struct hlist_head *chain;
	struct net *net;

	bin = xfrm_policy_inexact_alloc_bin(policy, dir);
	if (!bin)
		return ERR_PTR(-ENOMEM);

	net = xp_net(policy);
	lockdep_assert_held(&net->xfrm.xfrm_policy_lock);

	chain = xfrm_policy_inexact_alloc_chain(bin, policy, dir);
	if (!chain) {
		__xfrm_policy_inexact_prune_bin(bin, false);
		return ERR_PTR(-ENOMEM);
	}

	delpol = xfrm_policy_insert_list(chain, policy, excl);
	if (delpol && excl) {
		__xfrm_policy_inexact_prune_bin(bin, false);
		return ERR_PTR(-EEXIST);
	}

	chain = &net->xfrm.policy_inexact[dir];
	xfrm_policy_insert_inexact_list(chain, policy);

	if (delpol)
		__xfrm_policy_inexact_prune_bin(bin, false);

	return delpol;
}

static void xfrm_hash_rebuild(struct work_struct *work)
{
	struct net *net = container_of(work, struct net,
				       xfrm.policy_hthresh.work);
	unsigned int hmask;
	struct xfrm_policy *pol;
	struct xfrm_policy *policy;
	struct hlist_head *chain;
	struct hlist_head *odst;
	struct hlist_node *newpos;
	int i;
	int dir;
	unsigned seq;
	u8 lbits4, rbits4, lbits6, rbits6;

	mutex_lock(&hash_resize_mutex);

	/* read selector prefixlen thresholds */
	do {
		seq = read_seqbegin(&net->xfrm.policy_hthresh.lock);

		lbits4 = net->xfrm.policy_hthresh.lbits4;
		rbits4 = net->xfrm.policy_hthresh.rbits4;
		lbits6 = net->xfrm.policy_hthresh.lbits6;
		rbits6 = net->xfrm.policy_hthresh.rbits6;
	} while (read_seqretry(&net->xfrm.policy_hthresh.lock, seq));

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	write_seqcount_begin(&xfrm_policy_hash_generation);

	/* make sure that we can insert the indirect policies again before
	 * we start with destructive action.
	 */
	list_for_each_entry(policy, &net->xfrm.policy_all, walk.all) {
		struct xfrm_pol_inexact_bin *bin;
		u8 dbits, sbits;

		dir = xfrm_policy_id2dir(policy->index);
		if (policy->walk.dead || dir >= XFRM_POLICY_MAX)
			continue;

		if ((dir & XFRM_POLICY_MASK) == XFRM_POLICY_OUT) {
			if (policy->family == AF_INET) {
				dbits = rbits4;
				sbits = lbits4;
			} else {
				dbits = rbits6;
				sbits = lbits6;
			}
		} else {
			if (policy->family == AF_INET) {
				dbits = lbits4;
				sbits = rbits4;
			} else {
				dbits = lbits6;
				sbits = rbits6;
			}
		}

		if (policy->selector.prefixlen_d < dbits ||
		    policy->selector.prefixlen_s < sbits)
			continue;

		bin = xfrm_policy_inexact_alloc_bin(policy, dir);
		if (!bin)
			goto out_unlock;

		if (!xfrm_policy_inexact_alloc_chain(bin, policy, dir))
			goto out_unlock;
	}

	/* reset the bydst and inexact table in all directions */
	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		struct hlist_node *n;

		hlist_for_each_entry_safe(policy, n,
					  &net->xfrm.policy_inexact[dir],
					  bydst_inexact_list) {
			hlist_del_rcu(&policy->bydst);
			hlist_del_init(&policy->bydst_inexact_list);
		}

		hmask = net->xfrm.policy_bydst[dir].hmask;
		odst = net->xfrm.policy_bydst[dir].table;
		for (i = hmask; i >= 0; i--) {
			hlist_for_each_entry_safe(policy, n, odst + i, bydst)
				hlist_del_rcu(&policy->bydst);
		}
		if ((dir & XFRM_POLICY_MASK) == XFRM_POLICY_OUT) {
			/* dir out => dst = remote, src = local */
			net->xfrm.policy_bydst[dir].dbits4 = rbits4;
			net->xfrm.policy_bydst[dir].sbits4 = lbits4;
			net->xfrm.policy_bydst[dir].dbits6 = rbits6;
			net->xfrm.policy_bydst[dir].sbits6 = lbits6;
		} else {
			/* dir in/fwd => dst = local, src = remote */
			net->xfrm.policy_bydst[dir].dbits4 = lbits4;
			net->xfrm.policy_bydst[dir].sbits4 = rbits4;
			net->xfrm.policy_bydst[dir].dbits6 = lbits6;
			net->xfrm.policy_bydst[dir].sbits6 = rbits6;
		}
	}

	/* re-insert all policies by order of creation */
	list_for_each_entry_reverse(policy, &net->xfrm.policy_all, walk.all) {
		if (policy->walk.dead)
			continue;
		dir = xfrm_policy_id2dir(policy->index);
		if (dir >= XFRM_POLICY_MAX) {
			/* skip socket policies */
			continue;
		}
		newpos = NULL;
		chain = policy_hash_bysel(net, &policy->selector,
					  policy->family, dir);

		if (!chain) {
			void *p = xfrm_policy_inexact_insert(policy, dir, 0);

			WARN_ONCE(IS_ERR(p), "reinsert: %ld\n", PTR_ERR(p));
			continue;
		}

		hlist_for_each_entry(pol, chain, bydst) {
			if (policy->priority >= pol->priority)
				newpos = &pol->bydst;
			else
				break;
		}
		if (newpos)
			hlist_add_behind_rcu(&policy->bydst, newpos);
		else
			hlist_add_head_rcu(&policy->bydst, chain);
	}

out_unlock:
	__xfrm_policy_inexact_flush(net);
	write_seqcount_end(&xfrm_policy_hash_generation);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	mutex_unlock(&hash_resize_mutex);
}

void xfrm_policy_hash_rebuild(struct net *net)
{
	schedule_work(&net->xfrm.policy_hthresh.work);
}
EXPORT_SYMBOL(xfrm_policy_hash_rebuild);

/* Generate new index... KAME seems to generate them ordered by cost
 * of an absolute inpredictability of ordering of rules. This will not pass. */
static u32 xfrm_gen_index(struct net *net, int dir, u32 index)
{
	static u32 idx_generator;

	for (;;) {
		struct hlist_head *list;
		struct xfrm_policy *p;
		u32 idx;
		int found;

		if (!index) {
			idx = (idx_generator | dir);
			idx_generator += 8;
		} else {
			idx = index;
			index = 0;
		}

		if (idx == 0)
			idx = 8;
		list = net->xfrm.policy_byidx + idx_hash(net, idx);
		found = 0;
		hlist_for_each_entry(p, list, byidx) {
			if (p->index == idx) {
				found = 1;
				break;
			}
		}
		if (!found)
			return idx;
	}
}

static inline int selector_cmp(struct xfrm_selector *s1, struct xfrm_selector *s2)
{
	u32 *p1 = (u32 *) s1;
	u32 *p2 = (u32 *) s2;
	int len = sizeof(struct xfrm_selector) / sizeof(u32);
	int i;

	for (i = 0; i < len; i++) {
		if (p1[i] != p2[i])
			return 1;
	}

	return 0;
}

static void xfrm_policy_requeue(struct xfrm_policy *old,
				struct xfrm_policy *new)
{
	struct xfrm_policy_queue *pq = &old->polq;
	struct sk_buff_head list;

	if (skb_queue_empty(&pq->hold_queue))
		return;

	__skb_queue_head_init(&list);

	spin_lock_bh(&pq->hold_queue.lock);
	skb_queue_splice_init(&pq->hold_queue, &list);
	if (del_timer(&pq->hold_timer))
		xfrm_pol_put(old);
	spin_unlock_bh(&pq->hold_queue.lock);

	pq = &new->polq;

	spin_lock_bh(&pq->hold_queue.lock);
	skb_queue_splice(&list, &pq->hold_queue);
	pq->timeout = XFRM_QUEUE_TMO_MIN;
	if (!mod_timer(&pq->hold_timer, jiffies))
		xfrm_pol_hold(new);
	spin_unlock_bh(&pq->hold_queue.lock);
}

static inline bool xfrm_policy_mark_match(const struct xfrm_mark *mark,
					  struct xfrm_policy *pol)
{
	return mark->v == pol->mark.v && mark->m == pol->mark.m;
}

static u32 xfrm_pol_bin_key(const void *data, u32 len, u32 seed)
{
	const struct xfrm_pol_inexact_key *k = data;
	u32 a = k->type << 24 | k->dir << 16 | k->family;

	return jhash_3words(a, k->if_id, net_hash_mix(read_pnet(&k->net)),
			    seed);
}

static u32 xfrm_pol_bin_obj(const void *data, u32 len, u32 seed)
{
	const struct xfrm_pol_inexact_bin *b = data;

	return xfrm_pol_bin_key(&b->k, 0, seed);
}

static int xfrm_pol_bin_cmp(struct rhashtable_compare_arg *arg,
			    const void *ptr)
{
	const struct xfrm_pol_inexact_key *key = arg->key;
	const struct xfrm_pol_inexact_bin *b = ptr;
	int ret;

	if (!net_eq(read_pnet(&b->k.net), read_pnet(&key->net)))
		return -1;

	ret = b->k.dir ^ key->dir;
	if (ret)
		return ret;

	ret = b->k.type ^ key->type;
	if (ret)
		return ret;

	ret = b->k.family ^ key->family;
	if (ret)
		return ret;

	return b->k.if_id ^ key->if_id;
}

static const struct rhashtable_params xfrm_pol_inexact_params = {
	.head_offset		= offsetof(struct xfrm_pol_inexact_bin, head),
	.hashfn			= xfrm_pol_bin_key,
	.obj_hashfn		= xfrm_pol_bin_obj,
	.obj_cmpfn		= xfrm_pol_bin_cmp,
	.automatic_shrinking	= true,
};

static void xfrm_policy_insert_inexact_list(struct hlist_head *chain,
					    struct xfrm_policy *policy)
{
	struct xfrm_policy *pol, *delpol = NULL;
	struct hlist_node *newpos = NULL;
	int i = 0;

	hlist_for_each_entry(pol, chain, bydst_inexact_list) {
		if (pol->type == policy->type &&
		    pol->if_id == policy->if_id &&
		    !selector_cmp(&pol->selector, &policy->selector) &&
		    xfrm_policy_mark_match(&policy->mark, pol) &&
		    xfrm_sec_ctx_match(pol->security, policy->security) &&
		    !WARN_ON(delpol)) {
			delpol = pol;
			if (policy->priority > pol->priority)
				continue;
		} else if (policy->priority >= pol->priority) {
			newpos = &pol->bydst_inexact_list;
			continue;
		}
		if (delpol)
			break;
	}

	if (newpos)
		hlist_add_behind_rcu(&policy->bydst_inexact_list, newpos);
	else
		hlist_add_head_rcu(&policy->bydst_inexact_list, chain);

	hlist_for_each_entry(pol, chain, bydst_inexact_list) {
		pol->pos = i;
		i++;
	}
}

static struct xfrm_policy *xfrm_policy_insert_list(struct hlist_head *chain,
						   struct xfrm_policy *policy,
						   bool excl)
{
	struct xfrm_policy *pol, *newpos = NULL, *delpol = NULL;

	hlist_for_each_entry(pol, chain, bydst) {
		if (pol->type == policy->type &&
		    pol->if_id == policy->if_id &&
		    !selector_cmp(&pol->selector, &policy->selector) &&
		    xfrm_policy_mark_match(&policy->mark, pol) &&
		    xfrm_sec_ctx_match(pol->security, policy->security) &&
		    !WARN_ON(delpol)) {
			if (excl)
				return ERR_PTR(-EEXIST);
			delpol = pol;
			if (policy->priority > pol->priority)
				continue;
		} else if (policy->priority >= pol->priority) {
			newpos = pol;
			continue;
		}
		if (delpol)
			break;
	}

	if (newpos)
		hlist_add_behind_rcu(&policy->bydst, &newpos->bydst);
	else
		hlist_add_head_rcu(&policy->bydst, chain);

	return delpol;
}

int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl)
{
	struct net *net = xp_net(policy);
	struct xfrm_policy *delpol;
	struct hlist_head *chain;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_bysel(net, &policy->selector, policy->family, dir);
	if (chain)
		delpol = xfrm_policy_insert_list(chain, policy, excl);
	else
		delpol = xfrm_policy_inexact_insert(policy, dir, excl);

	if (IS_ERR(delpol)) {
		spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
		return PTR_ERR(delpol);
	}

	__xfrm_policy_link(policy, dir);

	/* After previous checking, family can either be AF_INET or AF_INET6 */
	if (policy->family == AF_INET)
		rt_genid_bump_ipv4(net);
	else
		rt_genid_bump_ipv6(net);

	if (delpol) {
		xfrm_policy_requeue(delpol, policy);
		__xfrm_policy_unlink(delpol, dir);
	}
	policy->index = delpol ? delpol->index : xfrm_gen_index(net, dir, policy->index);
	hlist_add_head(&policy->byidx, net->xfrm.policy_byidx+idx_hash(net, policy->index));
	policy->curlft.add_time = ktime_get_real_seconds();
	policy->curlft.use_time = 0;
	if (!mod_timer(&policy->timer, jiffies + HZ))
		xfrm_pol_hold(policy);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	if (delpol)
		xfrm_policy_kill(delpol);
	else if (xfrm_bydst_should_resize(net, dir, NULL))
		schedule_work(&net->xfrm.policy_hash_work);

	return 0;
}
EXPORT_SYMBOL(xfrm_policy_insert);

static struct xfrm_policy *
__xfrm_policy_bysel_ctx(struct hlist_head *chain, const struct xfrm_mark *mark,
			u32 if_id, u8 type, int dir, struct xfrm_selector *sel,
			struct xfrm_sec_ctx *ctx)
{
	struct xfrm_policy *pol;

	if (!chain)
		return NULL;

	hlist_for_each_entry(pol, chain, bydst) {
		if (pol->type == type &&
		    pol->if_id == if_id &&
		    xfrm_policy_mark_match(mark, pol) &&
		    !selector_cmp(sel, &pol->selector) &&
		    xfrm_sec_ctx_match(ctx, pol->security))
			return pol;
	}

	return NULL;
}

struct xfrm_policy *
xfrm_policy_bysel_ctx(struct net *net, const struct xfrm_mark *mark, u32 if_id,
		      u8 type, int dir, struct xfrm_selector *sel,
		      struct xfrm_sec_ctx *ctx, int delete, int *err)
{
	struct xfrm_pol_inexact_bin *bin = NULL;
	struct xfrm_policy *pol, *ret = NULL;
	struct hlist_head *chain;

	*err = 0;
	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_bysel(net, sel, sel->family, dir);
	if (!chain) {
		struct xfrm_pol_inexact_candidates cand;
		int i;

		bin = xfrm_policy_inexact_lookup(net, type,
						 sel->family, dir, if_id);
		if (!bin) {
			spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
			return NULL;
		}

		if (!xfrm_policy_find_inexact_candidates(&cand, bin,
							 &sel->saddr,
							 &sel->daddr)) {
			spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
			return NULL;
		}

		pol = NULL;
		for (i = 0; i < ARRAY_SIZE(cand.res); i++) {
			struct xfrm_policy *tmp;

			tmp = __xfrm_policy_bysel_ctx(cand.res[i], mark,
						      if_id, type, dir,
						      sel, ctx);
			if (!tmp)
				continue;

			if (!pol || tmp->pos < pol->pos)
				pol = tmp;
		}
	} else {
		pol = __xfrm_policy_bysel_ctx(chain, mark, if_id, type, dir,
					      sel, ctx);
	}

	if (pol) {
		xfrm_pol_hold(pol);
		if (delete) {
			*err = security_xfrm_policy_delete(pol->security);
			if (*err) {
				spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
				return pol;
			}
			__xfrm_policy_unlink(pol, dir);
		}
		ret = pol;
	}
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	if (ret && delete)
		xfrm_policy_kill(ret);
	if (bin && delete)
		xfrm_policy_inexact_prune_bin(bin);
	return ret;
}
EXPORT_SYMBOL(xfrm_policy_bysel_ctx);

struct xfrm_policy *
xfrm_policy_byid(struct net *net, const struct xfrm_mark *mark, u32 if_id,
		 u8 type, int dir, u32 id, int delete, int *err)
{
	struct xfrm_policy *pol, *ret;
	struct hlist_head *chain;

	*err = -ENOENT;
	if (xfrm_policy_id2dir(id) != dir)
		return NULL;

	*err = 0;
	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = net->xfrm.policy_byidx + idx_hash(net, id);
	ret = NULL;
	hlist_for_each_entry(pol, chain, byidx) {
		if (pol->type == type && pol->index == id &&
		    pol->if_id == if_id && xfrm_policy_mark_match(mark, pol)) {
			xfrm_pol_hold(pol);
			if (delete) {
				*err = security_xfrm_policy_delete(
								pol->security);
				if (*err) {
					spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
					return pol;
				}
				__xfrm_policy_unlink(pol, dir);
			}
			ret = pol;
			break;
		}
	}
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	if (ret && delete)
		xfrm_policy_kill(ret);
	return ret;
}
EXPORT_SYMBOL(xfrm_policy_byid);

#ifdef CONFIG_SECURITY_NETWORK_XFRM
static inline int
xfrm_policy_flush_secctx_check(struct net *net, u8 type, bool task_valid)
{
	struct xfrm_policy *pol;
	int err = 0;

	list_for_each_entry(pol, &net->xfrm.policy_all, walk.all) {
		if (pol->walk.dead ||
		    xfrm_policy_id2dir(pol->index) >= XFRM_POLICY_MAX ||
		    pol->type != type)
			continue;

		err = security_xfrm_policy_delete(pol->security);
		if (err) {
			xfrm_audit_policy_delete(pol, 0, task_valid);
			return err;
		}
	}
	return err;
}
#else
static inline int
xfrm_policy_flush_secctx_check(struct net *net, u8 type, bool task_valid)
{
	return 0;
}
#endif

int xfrm_policy_flush(struct net *net, u8 type, bool task_valid)
{
	int dir, err = 0, cnt = 0;
	struct xfrm_policy *pol;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);

	err = xfrm_policy_flush_secctx_check(net, type, task_valid);
	if (err)
		goto out;

again:
	list_for_each_entry(pol, &net->xfrm.policy_all, walk.all) {
		dir = xfrm_policy_id2dir(pol->index);
		if (pol->walk.dead ||
		    dir >= XFRM_POLICY_MAX ||
		    pol->type != type)
			continue;

		__xfrm_policy_unlink(pol, dir);
		spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
		cnt++;
		xfrm_audit_policy_delete(pol, 1, task_valid);
		xfrm_policy_kill(pol);
		spin_lock_bh(&net->xfrm.xfrm_policy_lock);
		goto again;
	}
	if (cnt)
		__xfrm_policy_inexact_flush(net);
	else
		err = -ESRCH;
out:
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_policy_flush);

int xfrm_policy_walk(struct net *net, struct xfrm_policy_walk *walk,
		     int (*func)(struct xfrm_policy *, int, int, void*),
		     void *data)
{
	struct xfrm_policy *pol;
	struct xfrm_policy_walk_entry *x;
	int error = 0;

	if (walk->type >= XFRM_POLICY_TYPE_MAX &&
	    walk->type != XFRM_POLICY_TYPE_ANY)
		return -EINVAL;

	if (list_empty(&walk->walk.all) && walk->seq != 0)
		return 0;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	if (list_empty(&walk->walk.all))
		x = list_first_entry(&net->xfrm.policy_all, struct xfrm_policy_walk_entry, all);
	else
		x = list_first_entry(&walk->walk.all,
				     struct xfrm_policy_walk_entry, all);

	list_for_each_entry_from(x, &net->xfrm.policy_all, all) {
		if (x->dead)
			continue;
		pol = container_of(x, struct xfrm_policy, walk);
		if (walk->type != XFRM_POLICY_TYPE_ANY &&
		    walk->type != pol->type)
			continue;
		error = func(pol, xfrm_policy_id2dir(pol->index),
			     walk->seq, data);
		if (error) {
			list_move_tail(&walk->walk.all, &x->all);
			goto out;
		}
		walk->seq++;
	}
	if (walk->seq == 0) {
		error = -ENOENT;
		goto out;
	}
	list_del_init(&walk->walk.all);
out:
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
	return error;
}
EXPORT_SYMBOL(xfrm_policy_walk);

void xfrm_policy_walk_init(struct xfrm_policy_walk *walk, u8 type)
{
	INIT_LIST_HEAD(&walk->walk.all);
	walk->walk.dead = 1;
	walk->type = type;
	walk->seq = 0;
}
EXPORT_SYMBOL(xfrm_policy_walk_init);

void xfrm_policy_walk_done(struct xfrm_policy_walk *walk, struct net *net)
{
	if (list_empty(&walk->walk.all))
		return;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock); /*FIXME where is net? */
	list_del(&walk->walk.all);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
}
EXPORT_SYMBOL(xfrm_policy_walk_done);

/*
 * Find policy to apply to this flow.
 *
 * Returns 0 if policy found, else an -errno.
 */
static int xfrm_policy_match(const struct xfrm_policy *pol,
			     const struct flowi *fl,
			     u8 type, u16 family, int dir, u32 if_id)
{
	const struct xfrm_selector *sel = &pol->selector;
	int ret = -ESRCH;
	bool match;

	if (pol->family != family ||
	    pol->if_id != if_id ||
	    (fl->flowi_mark & pol->mark.m) != pol->mark.v ||
	    pol->type != type)
		return ret;

	match = xfrm_selector_match(sel, fl, family);
	if (match)
		ret = security_xfrm_policy_lookup(pol->security, fl->flowi_secid,
						  dir);
	return ret;
}

static struct xfrm_pol_inexact_node *
xfrm_policy_lookup_inexact_addr(const struct rb_root *r,
				seqcount_spinlock_t *count,
				const xfrm_address_t *addr, u16 family)
{
	const struct rb_node *parent;
	int seq;

again:
	seq = read_seqcount_begin(count);

	parent = rcu_dereference_raw(r->rb_node);
	while (parent) {
		struct xfrm_pol_inexact_node *node;
		int delta;

		node = rb_entry(parent, struct xfrm_pol_inexact_node, node);

		delta = xfrm_policy_addr_delta(addr, &node->addr,
					       node->prefixlen, family);
		if (delta < 0) {
			parent = rcu_dereference_raw(parent->rb_left);
			continue;
		} else if (delta > 0) {
			parent = rcu_dereference_raw(parent->rb_right);
			continue;
		}

		return node;
	}

	if (read_seqcount_retry(count, seq))
		goto again;

	return NULL;
}

static bool
xfrm_policy_find_inexact_candidates(struct xfrm_pol_inexact_candidates *cand,
				    struct xfrm_pol_inexact_bin *b,
				    const xfrm_address_t *saddr,
				    const xfrm_address_t *daddr)
{
	struct xfrm_pol_inexact_node *n;
	u16 family;

	if (!b)
		return false;

	family = b->k.family;
	memset(cand, 0, sizeof(*cand));
	cand->res[XFRM_POL_CAND_ANY] = &b->hhead;

	n = xfrm_policy_lookup_inexact_addr(&b->root_d, &b->count, daddr,
					    family);
	if (n) {
		cand->res[XFRM_POL_CAND_DADDR] = &n->hhead;
		n = xfrm_policy_lookup_inexact_addr(&n->root, &b->count, saddr,
						    family);
		if (n)
			cand->res[XFRM_POL_CAND_BOTH] = &n->hhead;
	}

	n = xfrm_policy_lookup_inexact_addr(&b->root_s, &b->count, saddr,
					    family);
	if (n)
		cand->res[XFRM_POL_CAND_SADDR] = &n->hhead;

	return true;
}

static struct xfrm_pol_inexact_bin *
xfrm_policy_inexact_lookup_rcu(struct net *net, u8 type, u16 family,
			       u8 dir, u32 if_id)
{
	struct xfrm_pol_inexact_key k = {
		.family = family,
		.type = type,
		.dir = dir,
		.if_id = if_id,
	};

	write_pnet(&k.net, net);

	return rhashtable_lookup(&xfrm_policy_inexact_table, &k,
				 xfrm_pol_inexact_params);
}

static struct xfrm_pol_inexact_bin *
xfrm_policy_inexact_lookup(struct net *net, u8 type, u16 family,
			   u8 dir, u32 if_id)
{
	struct xfrm_pol_inexact_bin *bin;

	lockdep_assert_held(&net->xfrm.xfrm_policy_lock);

	rcu_read_lock();
	bin = xfrm_policy_inexact_lookup_rcu(net, type, family, dir, if_id);
	rcu_read_unlock();

	return bin;
}

static struct xfrm_policy *
__xfrm_policy_eval_candidates(struct hlist_head *chain,
			      struct xfrm_policy *prefer,
			      const struct flowi *fl,
			      u8 type, u16 family, int dir, u32 if_id)
{
	u32 priority = prefer ? prefer->priority : ~0u;
	struct xfrm_policy *pol;

	if (!chain)
		return NULL;

	hlist_for_each_entry_rcu(pol, chain, bydst) {
		int err;

		if (pol->priority > priority)
			break;

		err = xfrm_policy_match(pol, fl, type, family, dir, if_id);
		if (err) {
			if (err != -ESRCH)
				return ERR_PTR(err);

			continue;
		}

		if (prefer) {
			/* matches.  Is it older than *prefer? */
			if (pol->priority == priority &&
			    prefer->pos < pol->pos)
				return prefer;
		}

		return pol;
	}

	return NULL;
}

static struct xfrm_policy *
xfrm_policy_eval_candidates(struct xfrm_pol_inexact_candidates *cand,
			    struct xfrm_policy *prefer,
			    const struct flowi *fl,
			    u8 type, u16 family, int dir, u32 if_id)
{
	struct xfrm_policy *tmp;
	int i;

	for (i = 0; i < ARRAY_SIZE(cand->res); i++) {
		tmp = __xfrm_policy_eval_candidates(cand->res[i],
						    prefer,
						    fl, type, family, dir,
						    if_id);
		if (!tmp)
			continue;

		if (IS_ERR(tmp))
			return tmp;
		prefer = tmp;
	}

	return prefer;
}

static struct xfrm_policy *xfrm_policy_lookup_bytype(struct net *net, u8 type,
						     const struct flowi *fl,
						     u16 family, u8 dir,
						     u32 if_id)
{
	struct xfrm_pol_inexact_candidates cand;
	const xfrm_address_t *daddr, *saddr;
	struct xfrm_pol_inexact_bin *bin;
	struct xfrm_policy *pol, *ret;
	struct hlist_head *chain;
	unsigned int sequence;
	int err;

	daddr = xfrm_flowi_daddr(fl, family);
	saddr = xfrm_flowi_saddr(fl, family);
	if (unlikely(!daddr || !saddr))
		return NULL;

	rcu_read_lock();
 retry:
	do {
		sequence = read_seqcount_begin(&xfrm_policy_hash_generation);
		chain = policy_hash_direct(net, daddr, saddr, family, dir);
	} while (read_seqcount_retry(&xfrm_policy_hash_generation, sequence));

	ret = NULL;
	hlist_for_each_entry_rcu(pol, chain, bydst) {
		err = xfrm_policy_match(pol, fl, type, family, dir, if_id);
		if (err) {
			if (err == -ESRCH)
				continue;
			else {
				ret = ERR_PTR(err);
				goto fail;
			}
		} else {
			ret = pol;
			break;
		}
	}
	bin = xfrm_policy_inexact_lookup_rcu(net, type, family, dir, if_id);
	if (!bin || !xfrm_policy_find_inexact_candidates(&cand, bin, saddr,
							 daddr))
		goto skip_inexact;

	pol = xfrm_policy_eval_candidates(&cand, ret, fl, type,
					  family, dir, if_id);
	if (pol) {
		ret = pol;
		if (IS_ERR(pol))
			goto fail;
	}

skip_inexact:
	if (read_seqcount_retry(&xfrm_policy_hash_generation, sequence))
		goto retry;

	if (ret && !xfrm_pol_hold_rcu(ret))
		goto retry;
fail:
	rcu_read_unlock();

	return ret;
}

static struct xfrm_policy *xfrm_policy_lookup(struct net *net,
					      const struct flowi *fl,
					      u16 family, u8 dir, u32 if_id)
{
#ifdef CONFIG_XFRM_SUB_POLICY
	struct xfrm_policy *pol;

	pol = xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_SUB, fl, family,
					dir, if_id);
	if (pol != NULL)
		return pol;
#endif
	return xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_MAIN, fl, family,
					 dir, if_id);
}

static struct xfrm_policy *xfrm_sk_policy_lookup(const struct sock *sk, int dir,
						 const struct flowi *fl,
						 u16 family, u32 if_id)
{
	struct xfrm_policy *pol;

	rcu_read_lock();
 again:
	pol = rcu_dereference(sk->sk_policy[dir]);
	if (pol != NULL) {
		bool match;
		int err = 0;

		if (pol->family != family) {
			pol = NULL;
			goto out;
		}

		match = xfrm_selector_match(&pol->selector, fl, family);
		if (match) {
			if ((sk->sk_mark & pol->mark.m) != pol->mark.v ||
			    pol->if_id != if_id) {
				pol = NULL;
				goto out;
			}
			err = security_xfrm_policy_lookup(pol->security,
						      fl->flowi_secid,
						      dir);
			if (!err) {
				if (!xfrm_pol_hold_rcu(pol))
					goto again;
			} else if (err == -ESRCH) {
				pol = NULL;
			} else {
				pol = ERR_PTR(err);
			}
		} else
			pol = NULL;
	}
out:
	rcu_read_unlock();
	return pol;
}

static void __xfrm_policy_link(struct xfrm_policy *pol, int dir)
{
	struct net *net = xp_net(pol);

	list_add(&pol->walk.all, &net->xfrm.policy_all);
	net->xfrm.policy_count[dir]++;
	xfrm_pol_hold(pol);
}

static struct xfrm_policy *__xfrm_policy_unlink(struct xfrm_policy *pol,
						int dir)
{
	struct net *net = xp_net(pol);

	if (list_empty(&pol->walk.all))
		return NULL;

	/* Socket policies are not hashed. */
	if (!hlist_unhashed(&pol->bydst)) {
		hlist_del_rcu(&pol->bydst);
		hlist_del_init(&pol->bydst_inexact_list);
		hlist_del(&pol->byidx);
	}

	list_del_init(&pol->walk.all);
	net->xfrm.policy_count[dir]--;

	return pol;
}

static void xfrm_sk_policy_link(struct xfrm_policy *pol, int dir)
{
	__xfrm_policy_link(pol, XFRM_POLICY_MAX + dir);
}

static void xfrm_sk_policy_unlink(struct xfrm_policy *pol, int dir)
{
	__xfrm_policy_unlink(pol, XFRM_POLICY_MAX + dir);
}

int xfrm_policy_delete(struct xfrm_policy *pol, int dir)
{
	struct net *net = xp_net(pol);

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	pol = __xfrm_policy_unlink(pol, dir);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
	if (pol) {
		xfrm_policy_kill(pol);
		return 0;
	}
	return -ENOENT;
}
EXPORT_SYMBOL(xfrm_policy_delete);

int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol)
{
	struct net *net = sock_net(sk);
	struct xfrm_policy *old_pol;

#ifdef CONFIG_XFRM_SUB_POLICY
	if (pol && pol->type != XFRM_POLICY_TYPE_MAIN)
		return -EINVAL;
#endif

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	old_pol = rcu_dereference_protected(sk->sk_policy[dir],
				lockdep_is_held(&net->xfrm.xfrm_policy_lock));
	if (pol) {
		pol->curlft.add_time = ktime_get_real_seconds();
		pol->index = xfrm_gen_index(net, XFRM_POLICY_MAX+dir, 0);
		xfrm_sk_policy_link(pol, dir);
	}
	rcu_assign_pointer(sk->sk_policy[dir], pol);
	if (old_pol) {
		if (pol)
			xfrm_policy_requeue(old_pol, pol);

		/* Unlinking succeeds always. This is the only function
		 * allowed to delete or replace socket policy.
		 */
		xfrm_sk_policy_unlink(old_pol, dir);
	}
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	if (old_pol) {
		xfrm_policy_kill(old_pol);
	}
	return 0;
}

static struct xfrm_policy *clone_policy(const struct xfrm_policy *old, int dir)
{
	struct xfrm_policy *newp = xfrm_policy_alloc(xp_net(old), GFP_ATOMIC);
	struct net *net = xp_net(old);

	if (newp) {
		newp->selector = old->selector;
		if (security_xfrm_policy_clone(old->security,
					       &newp->security)) {
			kfree(newp);
			return NULL;  /* ENOMEM */
		}
		newp->lft = old->lft;
		newp->curlft = old->curlft;
		newp->mark = old->mark;
		newp->if_id = old->if_id;
		newp->action = old->action;
		newp->flags = old->flags;
		newp->xfrm_nr = old->xfrm_nr;
		newp->index = old->index;
		newp->type = old->type;
		newp->family = old->family;
		memcpy(newp->xfrm_vec, old->xfrm_vec,
		       newp->xfrm_nr*sizeof(struct xfrm_tmpl));
		spin_lock_bh(&net->xfrm.xfrm_policy_lock);
		xfrm_sk_policy_link(newp, dir);
		spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
		xfrm_pol_put(newp);
	}
	return newp;
}

int __xfrm_sk_clone_policy(struct sock *sk, const struct sock *osk)
{
	const struct xfrm_policy *p;
	struct xfrm_policy *np;
	int i, ret = 0;

	rcu_read_lock();
	for (i = 0; i < 2; i++) {
		p = rcu_dereference(osk->sk_policy[i]);
		if (p) {
			np = clone_policy(p, i);
			if (unlikely(!np)) {
				ret = -ENOMEM;
				break;
			}
			rcu_assign_pointer(sk->sk_policy[i], np);
		}
	}
	rcu_read_unlock();
	return ret;
}

static int
xfrm_get_saddr(struct net *net, int oif, xfrm_address_t *local,
	       xfrm_address_t *remote, unsigned short family, u32 mark)
{
	int err;
	const struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);

	if (unlikely(afinfo == NULL))
		return -EINVAL;
	err = afinfo->get_saddr(net, oif, local, remote, mark);
	rcu_read_unlock();
	return err;
}

/* Resolve list of templates for the flow, given policy. */

static int
xfrm_tmpl_resolve_one(struct xfrm_policy *policy, const struct flowi *fl,
		      struct xfrm_state **xfrm, unsigned short family)
{
	struct net *net = xp_net(policy);
	int nx;
	int i, error;
	xfrm_address_t *daddr = xfrm_flowi_daddr(fl, family);
	xfrm_address_t *saddr = xfrm_flowi_saddr(fl, family);
	xfrm_address_t tmp;

	for (nx = 0, i = 0; i < policy->xfrm_nr; i++) {
		struct xfrm_state *x;
		xfrm_address_t *remote = daddr;
		xfrm_address_t *local  = saddr;
		struct xfrm_tmpl *tmpl = &policy->xfrm_vec[i];

		if (tmpl->mode == XFRM_MODE_TUNNEL ||
		    tmpl->mode == XFRM_MODE_BEET) {
			remote = &tmpl->id.daddr;
			local = &tmpl->saddr;
			if (xfrm_addr_any(local, tmpl->encap_family)) {
				error = xfrm_get_saddr(net, fl->flowi_oif,
						       &tmp, remote,
						       tmpl->encap_family, 0);
				if (error)
					goto fail;
				local = &tmp;
			}
		}

		x = xfrm_state_find(remote, local, fl, tmpl, policy, &error,
				    family, policy->if_id);

		if (x && x->km.state == XFRM_STATE_VALID) {
			xfrm[nx++] = x;
			daddr = remote;
			saddr = local;
			continue;
		}
		if (x) {
			error = (x->km.state == XFRM_STATE_ERROR ?
				 -EINVAL : -EAGAIN);
			xfrm_state_put(x);
		} else if (error == -ESRCH) {
			error = -EAGAIN;
		}

		if (!tmpl->optional)
			goto fail;
	}
	return nx;

fail:
	for (nx--; nx >= 0; nx--)
		xfrm_state_put(xfrm[nx]);
	return error;
}

static int
xfrm_tmpl_resolve(struct xfrm_policy **pols, int npols, const struct flowi *fl,
		  struct xfrm_state **xfrm, unsigned short family)
{
	struct xfrm_state *tp[XFRM_MAX_DEPTH];
	struct xfrm_state **tpp = (npols > 1) ? tp : xfrm;
	int cnx = 0;
	int error;
	int ret;
	int i;

	for (i = 0; i < npols; i++) {
		if (cnx + pols[i]->xfrm_nr >= XFRM_MAX_DEPTH) {
			error = -ENOBUFS;
			goto fail;
		}

		ret = xfrm_tmpl_resolve_one(pols[i], fl, &tpp[cnx], family);
		if (ret < 0) {
			error = ret;
			goto fail;
		} else
			cnx += ret;
	}

	/* found states are sorted for outbound processing */
	if (npols > 1)
		xfrm_state_sort(xfrm, tpp, cnx, family);

	return cnx;

 fail:
	for (cnx--; cnx >= 0; cnx--)
		xfrm_state_put(tpp[cnx]);
	return error;

}

static int xfrm_get_tos(const struct flowi *fl, int family)
{
	if (family == AF_INET)
		return IPTOS_RT_MASK & fl->u.ip4.flowi4_tos;

	return 0;
}

static inline struct xfrm_dst *xfrm_alloc_dst(struct net *net, int family)
{
	const struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct dst_ops *dst_ops;
	struct xfrm_dst *xdst;

	if (!afinfo)
		return ERR_PTR(-EINVAL);

	switch (family) {
	case AF_INET:
		dst_ops = &net->xfrm.xfrm4_dst_ops;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		dst_ops = &net->xfrm.xfrm6_dst_ops;
		break;
#endif
	default:
		BUG();
	}
	xdst = dst_alloc(dst_ops, NULL, 1, DST_OBSOLETE_NONE, 0);

	if (likely(xdst)) {
		struct dst_entry *dst = &xdst->u.dst;

		memset(dst + 1, 0, sizeof(*xdst) - sizeof(*dst));
	} else
		xdst = ERR_PTR(-ENOBUFS);

	rcu_read_unlock();

	return xdst;
}

static void xfrm_init_path(struct xfrm_dst *path, struct dst_entry *dst,
			   int nfheader_len)
{
	if (dst->ops->family == AF_INET6) {
		struct rt6_info *rt = (struct rt6_info *)dst;
		path->path_cookie = rt6_get_cookie(rt);
		path->u.rt6.rt6i_nfheader_len = nfheader_len;
	}
}

static inline int xfrm_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
				const struct flowi *fl)
{
	const struct xfrm_policy_afinfo *afinfo =
		xfrm_policy_get_afinfo(xdst->u.dst.ops->family);
	int err;

	if (!afinfo)
		return -EINVAL;

	err = afinfo->fill_dst(xdst, dev, fl);

	rcu_read_unlock();

	return err;
}


/* Allocate chain of dst_entry's, attach known xfrm's, calculate
 * all the metrics... Shortly, bundle a bundle.
 */

static struct dst_entry *xfrm_bundle_create(struct xfrm_policy *policy,
					    struct xfrm_state **xfrm,
					    struct xfrm_dst **bundle,
					    int nx,
					    const struct flowi *fl,
					    struct dst_entry *dst)
{
	const struct xfrm_state_afinfo *afinfo;
	const struct xfrm_mode *inner_mode;
	struct net *net = xp_net(policy);
	unsigned long now = jiffies;
	struct net_device *dev;
	struct xfrm_dst *xdst_prev = NULL;
	struct xfrm_dst *xdst0 = NULL;
	int i = 0;
	int err;
	int header_len = 0;
	int nfheader_len = 0;
	int trailer_len = 0;
	int tos;
	int family = policy->selector.family;
	xfrm_address_t saddr, daddr;

	xfrm_flowi_addr_get(fl, &saddr, &daddr, family);

	tos = xfrm_get_tos(fl, family);

	dst_hold(dst);

	for (; i < nx; i++) {
		struct xfrm_dst *xdst = xfrm_alloc_dst(net, family);
		struct dst_entry *dst1 = &xdst->u.dst;

		err = PTR_ERR(xdst);
		if (IS_ERR(xdst)) {
			dst_release(dst);
			goto put_states;
		}

		bundle[i] = xdst;
		if (!xdst_prev)
			xdst0 = xdst;
		else
			/* Ref count is taken during xfrm_alloc_dst()
			 * No need to do dst_clone() on dst1
			 */
			xfrm_dst_set_child(xdst_prev, &xdst->u.dst);

		if (xfrm[i]->sel.family == AF_UNSPEC) {
			inner_mode = xfrm_ip2inner_mode(xfrm[i],
							xfrm_af2proto(family));
			if (!inner_mode) {
				err = -EAFNOSUPPORT;
				dst_release(dst);
				goto put_states;
			}
		} else
			inner_mode = &xfrm[i]->inner_mode;

		xdst->route = dst;
		dst_copy_metrics(dst1, dst);

		if (xfrm[i]->props.mode != XFRM_MODE_TRANSPORT) {
			__u32 mark = 0;

			if (xfrm[i]->props.smark.v || xfrm[i]->props.smark.m)
				mark = xfrm_smark_get(fl->flowi_mark, xfrm[i]);

			family = xfrm[i]->props.family;
			dst = xfrm_dst_lookup(xfrm[i], tos, fl->flowi_oif,
					      &saddr, &daddr, family, mark);
			err = PTR_ERR(dst);
			if (IS_ERR(dst))
				goto put_states;
		} else
			dst_hold(dst);

		dst1->xfrm = xfrm[i];
		xdst->xfrm_genid = xfrm[i]->genid;

		dst1->obsolete = DST_OBSOLETE_FORCE_CHK;
		dst1->lastuse = now;

		dst1->input = dst_discard;

		rcu_read_lock();
		afinfo = xfrm_state_afinfo_get_rcu(inner_mode->family);
		if (likely(afinfo))
			dst1->output = afinfo->output;
		else
			dst1->output = dst_discard_out;
		rcu_read_unlock();

		xdst_prev = xdst;

		header_len += xfrm[i]->props.header_len;
		if (xfrm[i]->type->flags & XFRM_TYPE_NON_FRAGMENT)
			nfheader_len += xfrm[i]->props.header_len;
		trailer_len += xfrm[i]->props.trailer_len;
	}

	xfrm_dst_set_child(xdst_prev, dst);
	xdst0->path = dst;

	err = -ENODEV;
	dev = dst->dev;
	if (!dev)
		goto free_dst;

	xfrm_init_path(xdst0, dst, nfheader_len);
	xfrm_init_pmtu(bundle, nx);

	for (xdst_prev = xdst0; xdst_prev != (struct xfrm_dst *)dst;
	     xdst_prev = (struct xfrm_dst *) xfrm_dst_child(&xdst_prev->u.dst)) {
		err = xfrm_fill_dst(xdst_prev, dev, fl);
		if (err)
			goto free_dst;

		xdst_prev->u.dst.header_len = header_len;
		xdst_prev->u.dst.trailer_len = trailer_len;
		header_len -= xdst_prev->u.dst.xfrm->props.header_len;
		trailer_len -= xdst_prev->u.dst.xfrm->props.trailer_len;
	}

	return &xdst0->u.dst;

put_states:
	for (; i < nx; i++)
		xfrm_state_put(xfrm[i]);
free_dst:
	if (xdst0)
		dst_release_immediate(&xdst0->u.dst);

	return ERR_PTR(err);
}

static int xfrm_expand_policies(const struct flowi *fl, u16 family,
				struct xfrm_policy **pols,
				int *num_pols, int *num_xfrms)
{
	int i;

	if (*num_pols == 0 || !pols[0]) {
		*num_pols = 0;
		*num_xfrms = 0;
		return 0;
	}
	if (IS_ERR(pols[0])) {
		*num_pols = 0;
		return PTR_ERR(pols[0]);
	}

	*num_xfrms = pols[0]->xfrm_nr;

#ifdef CONFIG_XFRM_SUB_POLICY
	if (pols[0] && pols[0]->action == XFRM_POLICY_ALLOW &&
	    pols[0]->type != XFRM_POLICY_TYPE_MAIN) {
		pols[1] = xfrm_policy_lookup_bytype(xp_net(pols[0]),
						    XFRM_POLICY_TYPE_MAIN,
						    fl, family,
						    XFRM_POLICY_OUT,
						    pols[0]->if_id);
		if (pols[1]) {
			if (IS_ERR(pols[1])) {
				xfrm_pols_put(pols, *num_pols);
				*num_pols = 0;
				return PTR_ERR(pols[1]);
			}
			(*num_pols)++;
			(*num_xfrms) += pols[1]->xfrm_nr;
		}
	}
#endif
	for (i = 0; i < *num_pols; i++) {
		if (pols[i]->action != XFRM_POLICY_ALLOW) {
			*num_xfrms = -1;
			break;
		}
	}

	return 0;

}

static struct xfrm_dst *
xfrm_resolve_and_create_bundle(struct xfrm_policy **pols, int num_pols,
			       const struct flowi *fl, u16 family,
			       struct dst_entry *dst_orig)
{
	struct net *net = xp_net(pols[0]);
	struct xfrm_state *xfrm[XFRM_MAX_DEPTH];
	struct xfrm_dst *bundle[XFRM_MAX_DEPTH];
	struct xfrm_dst *xdst;
	struct dst_entry *dst;
	int err;

	/* Try to instantiate a bundle */
	err = xfrm_tmpl_resolve(pols, num_pols, fl, xfrm, family);
	if (err <= 0) {
		if (err == 0)
			return NULL;

		if (err != -EAGAIN)
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTPOLERROR);
		return ERR_PTR(err);
	}

	dst = xfrm_bundle_create(pols[0], xfrm, bundle, err, fl, dst_orig);
	if (IS_ERR(dst)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTBUNDLEGENERROR);
		return ERR_CAST(dst);
	}

	xdst = (struct xfrm_dst *)dst;
	xdst->num_xfrms = err;
	xdst->num_pols = num_pols;
	memcpy(xdst->pols, pols, sizeof(struct xfrm_policy *) * num_pols);
	xdst->policy_genid = atomic_read(&pols[0]->genid);

	return xdst;
}

static void xfrm_policy_queue_process(struct timer_list *t)
{
	struct sk_buff *skb;
	struct sock *sk;
	struct dst_entry *dst;
	struct xfrm_policy *pol = from_timer(pol, t, polq.hold_timer);
	struct net *net = xp_net(pol);
	struct xfrm_policy_queue *pq = &pol->polq;
	struct flowi fl;
	struct sk_buff_head list;
	__u32 skb_mark;

	spin_lock(&pq->hold_queue.lock);
	skb = skb_peek(&pq->hold_queue);
	if (!skb) {
		spin_unlock(&pq->hold_queue.lock);
		goto out;
	}
	dst = skb_dst(skb);
	sk = skb->sk;

	/* Fixup the mark to support VTI. */
	skb_mark = skb->mark;
	skb->mark = pol->mark.v;
	xfrm_decode_session(skb, &fl, dst->ops->family);
	skb->mark = skb_mark;
	spin_unlock(&pq->hold_queue.lock);

	dst_hold(xfrm_dst_path(dst));
	dst = xfrm_lookup(net, xfrm_dst_path(dst), &fl, sk, XFRM_LOOKUP_QUEUE);
	if (IS_ERR(dst))
		goto purge_queue;

	if (dst->flags & DST_XFRM_QUEUE) {
		dst_release(dst);

		if (pq->timeout >= XFRM_QUEUE_TMO_MAX)
			goto purge_queue;

		pq->timeout = pq->timeout << 1;
		if (!mod_timer(&pq->hold_timer, jiffies + pq->timeout))
			xfrm_pol_hold(pol);
		goto out;
	}

	dst_release(dst);

	__skb_queue_head_init(&list);

	spin_lock(&pq->hold_queue.lock);
	pq->timeout = 0;
	skb_queue_splice_init(&pq->hold_queue, &list);
	spin_unlock(&pq->hold_queue.lock);

	while (!skb_queue_empty(&list)) {
		skb = __skb_dequeue(&list);

		/* Fixup the mark to support VTI. */
		skb_mark = skb->mark;
		skb->mark = pol->mark.v;
		xfrm_decode_session(skb, &fl, skb_dst(skb)->ops->family);
		skb->mark = skb_mark;

		dst_hold(xfrm_dst_path(skb_dst(skb)));
		dst = xfrm_lookup(net, xfrm_dst_path(skb_dst(skb)), &fl, skb->sk, 0);
		if (IS_ERR(dst)) {
			kfree_skb(skb);
			continue;
		}

		nf_reset_ct(skb);
		skb_dst_drop(skb);
		skb_dst_set(skb, dst);

		dst_output(net, skb->sk, skb);
	}

out:
	xfrm_pol_put(pol);
	return;

purge_queue:
	pq->timeout = 0;
	skb_queue_purge(&pq->hold_queue);
	xfrm_pol_put(pol);
}

static int xdst_queue_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	unsigned long sched_next;
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_dst *xdst = (struct xfrm_dst *) dst;
	struct xfrm_policy *pol = xdst->pols[0];
	struct xfrm_policy_queue *pq = &pol->polq;

	if (unlikely(skb_fclone_busy(sk, skb))) {
		kfree_skb(skb);
		return 0;
	}

	if (pq->hold_queue.qlen > XFRM_MAX_QUEUE_LEN) {
		kfree_skb(skb);
		return -EAGAIN;
	}

	skb_dst_force(skb);

	spin_lock_bh(&pq->hold_queue.lock);

	if (!pq->timeout)
		pq->timeout = XFRM_QUEUE_TMO_MIN;

	sched_next = jiffies + pq->timeout;

	if (del_timer(&pq->hold_timer)) {
		if (time_before(pq->hold_timer.expires, sched_next))
			sched_next = pq->hold_timer.expires;
		xfrm_pol_put(pol);
	}

	__skb_queue_tail(&pq->hold_queue, skb);
	if (!mod_timer(&pq->hold_timer, sched_next))
		xfrm_pol_hold(pol);

	spin_unlock_bh(&pq->hold_queue.lock);

	return 0;
}

static struct xfrm_dst *xfrm_create_dummy_bundle(struct net *net,
						 struct xfrm_flo *xflo,
						 const struct flowi *fl,
						 int num_xfrms,
						 u16 family)
{
	int err;
	struct net_device *dev;
	struct dst_entry *dst;
	struct dst_entry *dst1;
	struct xfrm_dst *xdst;

	xdst = xfrm_alloc_dst(net, family);
	if (IS_ERR(xdst))
		return xdst;

	if (!(xflo->flags & XFRM_LOOKUP_QUEUE) ||
	    net->xfrm.sysctl_larval_drop ||
	    num_xfrms <= 0)
		return xdst;

	dst = xflo->dst_orig;
	dst1 = &xdst->u.dst;
	dst_hold(dst);
	xdst->route = dst;

	dst_copy_metrics(dst1, dst);

	dst1->obsolete = DST_OBSOLETE_FORCE_CHK;
	dst1->flags |= DST_XFRM_QUEUE;
	dst1->lastuse = jiffies;

	dst1->input = dst_discard;
	dst1->output = xdst_queue_output;

	dst_hold(dst);
	xfrm_dst_set_child(xdst, dst);
	xdst->path = dst;

	xfrm_init_path((struct xfrm_dst *)dst1, dst, 0);

	err = -ENODEV;
	dev = dst->dev;
	if (!dev)
		goto free_dst;

	err = xfrm_fill_dst(xdst, dev, fl);
	if (err)
		goto free_dst;

out:
	return xdst;

free_dst:
	dst_release(dst1);
	xdst = ERR_PTR(err);
	goto out;
}

static struct xfrm_dst *xfrm_bundle_lookup(struct net *net,
					   const struct flowi *fl,
					   u16 family, u8 dir,
					   struct xfrm_flo *xflo, u32 if_id)
{
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	int num_pols = 0, num_xfrms = 0, err;
	struct xfrm_dst *xdst;

	/* Resolve policies to use if we couldn't get them from
	 * previous cache entry */
	num_pols = 1;
	pols[0] = xfrm_policy_lookup(net, fl, family, dir, if_id);
	err = xfrm_expand_policies(fl, family, pols,
					   &num_pols, &num_xfrms);
	if (err < 0)
		goto inc_error;
	if (num_pols == 0)
		return NULL;
	if (num_xfrms <= 0)
		goto make_dummy_bundle;

	xdst = xfrm_resolve_and_create_bundle(pols, num_pols, fl, family,
					      xflo->dst_orig);
	if (IS_ERR(xdst)) {
		err = PTR_ERR(xdst);
		if (err == -EREMOTE) {
			xfrm_pols_put(pols, num_pols);
			return NULL;
		}

		if (err != -EAGAIN)
			goto error;
		goto make_dummy_bundle;
	} else if (xdst == NULL) {
		num_xfrms = 0;
		goto make_dummy_bundle;
	}

	return xdst;

make_dummy_bundle:
	/* We found policies, but there's no bundles to instantiate:
	 * either because the policy blocks, has no transformations or
	 * we could not build template (no xfrm_states).*/
	xdst = xfrm_create_dummy_bundle(net, xflo, fl, num_xfrms, family);
	if (IS_ERR(xdst)) {
		xfrm_pols_put(pols, num_pols);
		return ERR_CAST(xdst);
	}
	xdst->num_pols = num_pols;
	xdst->num_xfrms = num_xfrms;
	memcpy(xdst->pols, pols, sizeof(struct xfrm_policy *) * num_pols);

	return xdst;

inc_error:
	XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTPOLERROR);
error:
	xfrm_pols_put(pols, num_pols);
	return ERR_PTR(err);
}

static struct dst_entry *make_blackhole(struct net *net, u16 family,
					struct dst_entry *dst_orig)
{
	const struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct dst_entry *ret;

	if (!afinfo) {
		dst_release(dst_orig);
		return ERR_PTR(-EINVAL);
	} else {
		ret = afinfo->blackhole_route(net, dst_orig);
	}
	rcu_read_unlock();

	return ret;
}

/* Finds/creates a bundle for given flow and if_id
 *
 * At the moment we eat a raw IP route. Mostly to speed up lookups
 * on interfaces with disabled IPsec.
 *
 * xfrm_lookup uses an if_id of 0 by default, and is provided for
 * compatibility
 */
struct dst_entry *xfrm_lookup_with_ifid(struct net *net,
					struct dst_entry *dst_orig,
					const struct flowi *fl,
					const struct sock *sk,
					int flags, u32 if_id)
{
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	struct xfrm_dst *xdst;
	struct dst_entry *dst, *route;
	u16 family = dst_orig->ops->family;
	u8 dir = XFRM_POLICY_OUT;
	int i, err, num_pols, num_xfrms = 0, drop_pols = 0;

	dst = NULL;
	xdst = NULL;
	route = NULL;

	sk = sk_const_to_full_sk(sk);
	if (sk && sk->sk_policy[XFRM_POLICY_OUT]) {
		num_pols = 1;
		pols[0] = xfrm_sk_policy_lookup(sk, XFRM_POLICY_OUT, fl, family,
						if_id);
		err = xfrm_expand_policies(fl, family, pols,
					   &num_pols, &num_xfrms);
		if (err < 0)
			goto dropdst;

		if (num_pols) {
			if (num_xfrms <= 0) {
				drop_pols = num_pols;
				goto no_transform;
			}

			xdst = xfrm_resolve_and_create_bundle(
					pols, num_pols, fl,
					family, dst_orig);

			if (IS_ERR(xdst)) {
				xfrm_pols_put(pols, num_pols);
				err = PTR_ERR(xdst);
				if (err == -EREMOTE)
					goto nopol;

				goto dropdst;
			} else if (xdst == NULL) {
				num_xfrms = 0;
				drop_pols = num_pols;
				goto no_transform;
			}

			route = xdst->route;
		}
	}

	if (xdst == NULL) {
		struct xfrm_flo xflo;

		xflo.dst_orig = dst_orig;
		xflo.flags = flags;

		/* To accelerate a bit...  */
		if (!if_id && ((dst_orig->flags & DST_NOXFRM) ||
			       !net->xfrm.policy_count[XFRM_POLICY_OUT]))
			goto nopol;

		xdst = xfrm_bundle_lookup(net, fl, family, dir, &xflo, if_id);
		if (xdst == NULL)
			goto nopol;
		if (IS_ERR(xdst)) {
			err = PTR_ERR(xdst);
			goto dropdst;
		}

		num_pols = xdst->num_pols;
		num_xfrms = xdst->num_xfrms;
		memcpy(pols, xdst->pols, sizeof(struct xfrm_policy *) * num_pols);
		route = xdst->route;
	}

	dst = &xdst->u.dst;
	if (route == NULL && num_xfrms > 0) {
		/* The only case when xfrm_bundle_lookup() returns a
		 * bundle with null route, is when the template could
		 * not be resolved. It means policies are there, but
		 * bundle could not be created, since we don't yet
		 * have the xfrm_state's. We need to wait for KM to
		 * negotiate new SA's or bail out with error.*/
		if (net->xfrm.sysctl_larval_drop) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTNOSTATES);
			err = -EREMOTE;
			goto error;
		}

		err = -EAGAIN;

		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTNOSTATES);
		goto error;
	}

no_transform:
	if (num_pols == 0)
		goto nopol;

	if ((flags & XFRM_LOOKUP_ICMP) &&
	    !(pols[0]->flags & XFRM_POLICY_ICMP)) {
		err = -ENOENT;
		goto error;
	}

	for (i = 0; i < num_pols; i++)
		pols[i]->curlft.use_time = ktime_get_real_seconds();

	if (num_xfrms < 0) {
		/* Prohibit the flow */
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTPOLBLOCK);
		err = -EPERM;
		goto error;
	} else if (num_xfrms > 0) {
		/* Flow transformed */
		dst_release(dst_orig);
	} else {
		/* Flow passes untransformed */
		dst_release(dst);
		dst = dst_orig;
	}
ok:
	xfrm_pols_put(pols, drop_pols);
	if (dst && dst->xfrm &&
	    dst->xfrm->props.mode == XFRM_MODE_TUNNEL)
		dst->flags |= DST_XFRM_TUNNEL;
	return dst;

nopol:
	if (!(flags & XFRM_LOOKUP_ICMP)) {
		dst = dst_orig;
		goto ok;
	}
	err = -ENOENT;
error:
	dst_release(dst);
dropdst:
	if (!(flags & XFRM_LOOKUP_KEEP_DST_REF))
		dst_release(dst_orig);
	xfrm_pols_put(pols, drop_pols);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(xfrm_lookup_with_ifid);

/* Main function: finds/creates a bundle for given flow.
 *
 * At the moment we eat a raw IP route. Mostly to speed up lookups
 * on interfaces with disabled IPsec.
 */
struct dst_entry *xfrm_lookup(struct net *net, struct dst_entry *dst_orig,
			      const struct flowi *fl, const struct sock *sk,
			      int flags)
{
	return xfrm_lookup_with_ifid(net, dst_orig, fl, sk, flags, 0);
}
EXPORT_SYMBOL(xfrm_lookup);

/* Callers of xfrm_lookup_route() must ensure a call to dst_output().
 * Otherwise we may send out blackholed packets.
 */
struct dst_entry *xfrm_lookup_route(struct net *net, struct dst_entry *dst_orig,
				    const struct flowi *fl,
				    const struct sock *sk, int flags)
{
	struct dst_entry *dst = xfrm_lookup(net, dst_orig, fl, sk,
					    flags | XFRM_LOOKUP_QUEUE |
					    XFRM_LOOKUP_KEEP_DST_REF);

	if (PTR_ERR(dst) == -EREMOTE)
		return make_blackhole(net, dst_orig->ops->family, dst_orig);

	if (IS_ERR(dst))
		dst_release(dst_orig);

	return dst;
}
EXPORT_SYMBOL(xfrm_lookup_route);

static inline int
xfrm_secpath_reject(int idx, struct sk_buff *skb, const struct flowi *fl)
{
	struct sec_path *sp = skb_sec_path(skb);
	struct xfrm_state *x;

	if (!sp || idx < 0 || idx >= sp->len)
		return 0;
	x = sp->xvec[idx];
	if (!x->type->reject)
		return 0;
	return x->type->reject(x, skb, fl);
}

/* When skb is transformed back to its "native" form, we have to
 * check policy restrictions. At the moment we make this in maximally
 * stupid way. Shame on me. :-) Of course, connected sockets must
 * have policy cached at them.
 */

static inline int
xfrm_state_ok(const struct xfrm_tmpl *tmpl, const struct xfrm_state *x,
	      unsigned short family)
{
	if (xfrm_state_kern(x))
		return tmpl->optional && !xfrm_state_addr_cmp(tmpl, x, tmpl->encap_family);
	return	x->id.proto == tmpl->id.proto &&
		(x->id.spi == tmpl->id.spi || !tmpl->id.spi) &&
		(x->props.reqid == tmpl->reqid || !tmpl->reqid) &&
		x->props.mode == tmpl->mode &&
		(tmpl->allalgs || (tmpl->aalgos & (1<<x->props.aalgo)) ||
		 !(xfrm_id_proto_match(tmpl->id.proto, IPSEC_PROTO_ANY))) &&
		!(x->props.mode != XFRM_MODE_TRANSPORT &&
		  xfrm_state_addr_cmp(tmpl, x, family));
}

/*
 * 0 or more than 0 is returned when validation is succeeded (either bypass
 * because of optional transport mode, or next index of the mathced secpath
 * state with the template.
 * -1 is returned when no matching template is found.
 * Otherwise "-2 - errored_index" is returned.
 */
static inline int
xfrm_policy_ok(const struct xfrm_tmpl *tmpl, const struct sec_path *sp, int start,
	       unsigned short family)
{
	int idx = start;

	if (tmpl->optional) {
		if (tmpl->mode == XFRM_MODE_TRANSPORT)
			return start;
	} else
		start = -1;
	for (; idx < sp->len; idx++) {
		if (xfrm_state_ok(tmpl, sp->xvec[idx], family))
			return ++idx;
		if (sp->xvec[idx]->props.mode != XFRM_MODE_TRANSPORT) {
			if (start == -1)
				start = -2-idx;
			break;
		}
	}
	return start;
}

static void
decode_session4(struct sk_buff *skb, struct flowi *fl, bool reverse)
{
	const struct iphdr *iph = ip_hdr(skb);
	int ihl = iph->ihl;
	u8 *xprth = skb_network_header(skb) + ihl * 4;
	struct flowi4 *fl4 = &fl->u.ip4;
	int oif = 0;

	if (skb_dst(skb) && skb_dst(skb)->dev)
		oif = skb_dst(skb)->dev->ifindex;

	memset(fl4, 0, sizeof(struct flowi4));
	fl4->flowi4_mark = skb->mark;
	fl4->flowi4_oif = reverse ? skb->skb_iif : oif;

	fl4->flowi4_proto = iph->protocol;
	fl4->daddr = reverse ? iph->saddr : iph->daddr;
	fl4->saddr = reverse ? iph->daddr : iph->saddr;
	fl4->flowi4_tos = iph->tos & ~INET_ECN_MASK;

	if (!ip_is_fragment(iph)) {
		switch (iph->protocol) {
		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
		case IPPROTO_DCCP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ports;

				xprth = skb_network_header(skb) + ihl * 4;
				ports = (__be16 *)xprth;

				fl4->fl4_sport = ports[!!reverse];
				fl4->fl4_dport = ports[!reverse];
			}
			break;
		case IPPROTO_ICMP:
			if (xprth + 2 < skb->data ||
			    pskb_may_pull(skb, xprth + 2 - skb->data)) {
				u8 *icmp;

				xprth = skb_network_header(skb) + ihl * 4;
				icmp = xprth;

				fl4->fl4_icmp_type = icmp[0];
				fl4->fl4_icmp_code = icmp[1];
			}
			break;
		case IPPROTO_ESP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be32 *ehdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ehdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ehdr[0];
			}
			break;
		case IPPROTO_AH:
			if (xprth + 8 < skb->data ||
			    pskb_may_pull(skb, xprth + 8 - skb->data)) {
				__be32 *ah_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ah_hdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ah_hdr[1];
			}
			break;
		case IPPROTO_COMP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ipcomp_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ipcomp_hdr = (__be16 *)xprth;

				fl4->fl4_ipsec_spi = htonl(ntohs(ipcomp_hdr[1]));
			}
			break;
		case IPPROTO_GRE:
			if (xprth + 12 < skb->data ||
			    pskb_may_pull(skb, xprth + 12 - skb->data)) {
				__be16 *greflags;
				__be32 *gre_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				greflags = (__be16 *)xprth;
				gre_hdr = (__be32 *)xprth;

				if (greflags[0] & GRE_KEY) {
					if (greflags[0] & GRE_CSUM)
						gre_hdr++;
					fl4->fl4_gre_key = gre_hdr[1];
				}
			}
			break;
		default:
			fl4->fl4_ipsec_spi = 0;
			break;
		}
	}
}

#if IS_ENABLED(CONFIG_IPV6)
static void
decode_session6(struct sk_buff *skb, struct flowi *fl, bool reverse)
{
	struct flowi6 *fl6 = &fl->u.ip6;
	int onlyproto = 0;
	const struct ipv6hdr *hdr = ipv6_hdr(skb);
	u32 offset = sizeof(*hdr);
	struct ipv6_opt_hdr *exthdr;
	const unsigned char *nh = skb_network_header(skb);
	u16 nhoff = IP6CB(skb)->nhoff;
	int oif = 0;
	u8 nexthdr;

	if (!nhoff)
		nhoff = offsetof(struct ipv6hdr, nexthdr);

	nexthdr = nh[nhoff];

	if (skb_dst(skb) && skb_dst(skb)->dev)
		oif = skb_dst(skb)->dev->ifindex;

	memset(fl6, 0, sizeof(struct flowi6));
	fl6->flowi6_mark = skb->mark;
	fl6->flowi6_oif = reverse ? skb->skb_iif : oif;

	fl6->daddr = reverse ? hdr->saddr : hdr->daddr;
	fl6->saddr = reverse ? hdr->daddr : hdr->saddr;

	while (nh + offset + sizeof(*exthdr) < skb->data ||
	       pskb_may_pull(skb, nh + offset + sizeof(*exthdr) - skb->data)) {
		nh = skb_network_header(skb);
		exthdr = (struct ipv6_opt_hdr *)(nh + offset);

		switch (nexthdr) {
		case NEXTHDR_FRAGMENT:
			onlyproto = 1;
			fallthrough;
		case NEXTHDR_ROUTING:
		case NEXTHDR_HOP:
		case NEXTHDR_DEST:
			offset += ipv6_optlen(exthdr);
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr *)(nh + offset);
			break;
		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
		case IPPROTO_DCCP:
			if (!onlyproto && (nh + offset + 4 < skb->data ||
			     pskb_may_pull(skb, nh + offset + 4 - skb->data))) {
				__be16 *ports;

				nh = skb_network_header(skb);
				ports = (__be16 *)(nh + offset);
				fl6->fl6_sport = ports[!!reverse];
				fl6->fl6_dport = ports[!reverse];
			}
			fl6->flowi6_proto = nexthdr;
			return;
		case IPPROTO_ICMPV6:
			if (!onlyproto && (nh + offset + 2 < skb->data ||
			    pskb_may_pull(skb, nh + offset + 2 - skb->data))) {
				u8 *icmp;

				nh = skb_network_header(skb);
				icmp = (u8 *)(nh + offset);
				fl6->fl6_icmp_type = icmp[0];
				fl6->fl6_icmp_code = icmp[1];
			}
			fl6->flowi6_proto = nexthdr;
			return;
		case IPPROTO_GRE:
			if (!onlyproto &&
			    (nh + offset + 12 < skb->data ||
			     pskb_may_pull(skb, nh + offset + 12 - skb->data))) {
				struct gre_base_hdr *gre_hdr;
				__be32 *gre_key;

				nh = skb_network_header(skb);
				gre_hdr = (struct gre_base_hdr *)(nh + offset);
				gre_key = (__be32 *)(gre_hdr + 1);

				if (gre_hdr->flags & GRE_KEY) {
					if (gre_hdr->flags & GRE_CSUM)
						gre_key++;
					fl6->fl6_gre_key = *gre_key;
				}
			}
			fl6->flowi6_proto = nexthdr;
			return;

#if IS_ENABLED(CONFIG_IPV6_MIP6)
		case IPPROTO_MH:
			offset += ipv6_optlen(exthdr);
			if (!onlyproto && (nh + offset + 3 < skb->data ||
			    pskb_may_pull(skb, nh + offset + 3 - skb->data))) {
				struct ip6_mh *mh;

				nh = skb_network_header(skb);
				mh = (struct ip6_mh *)(nh + offset);
				fl6->fl6_mh_type = mh->ip6mh_type;
			}
			fl6->flowi6_proto = nexthdr;
			return;
#endif
		/* XXX Why are there these headers? */
		case IPPROTO_AH:
		case IPPROTO_ESP:
		case IPPROTO_COMP:
		default:
			fl6->fl6_ipsec_spi = 0;
			fl6->flowi6_proto = nexthdr;
			return;
		}
	}
}
#endif

int __xfrm_decode_session(struct sk_buff *skb, struct flowi *fl,
			  unsigned int family, int reverse)
{
	switch (family) {
	case AF_INET:
		decode_session4(skb, fl, reverse);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		decode_session6(skb, fl, reverse);
		break;
#endif
	default:
		return -EAFNOSUPPORT;
	}

	return security_xfrm_decode_session(skb, &fl->flowi_secid);
}
EXPORT_SYMBOL(__xfrm_decode_session);

static inline int secpath_has_nontransport(const struct sec_path *sp, int k, int *idxp)
{
	for (; k < sp->len; k++) {
		if (sp->xvec[k]->props.mode != XFRM_MODE_TRANSPORT) {
			*idxp = k;
			return 1;
		}
	}

	return 0;
}

int __xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb,
			unsigned short family)
{
	struct net *net = dev_net(skb->dev);
	struct xfrm_policy *pol;
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	int npols = 0;
	int xfrm_nr;
	int pi;
	int reverse;
	struct flowi fl;
	int xerr_idx = -1;
	const struct xfrm_if_cb *ifcb;
	struct sec_path *sp;
	struct xfrm_if *xi;
	u32 if_id = 0;

	rcu_read_lock();
	ifcb = xfrm_if_get_cb();

	if (ifcb) {
		xi = ifcb->decode_session(skb, family);
		if (xi) {
			if_id = xi->p.if_id;
			net = xi->net;
		}
	}
	rcu_read_unlock();

	reverse = dir & ~XFRM_POLICY_MASK;
	dir &= XFRM_POLICY_MASK;

	if (__xfrm_decode_session(skb, &fl, family, reverse) < 0) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
		return 0;
	}

	nf_nat_decode_session(skb, &fl, family);

	/* First, check used SA against their selectors. */
	sp = skb_sec_path(skb);
	if (sp) {
		int i;

		for (i = sp->len - 1; i >= 0; i--) {
			struct xfrm_state *x = sp->xvec[i];
			if (!xfrm_selector_match(&x->sel, &fl, family)) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMISMATCH);
				return 0;
			}
		}
	}

	pol = NULL;
	sk = sk_to_full_sk(sk);
	if (sk && sk->sk_policy[dir]) {
		pol = xfrm_sk_policy_lookup(sk, dir, &fl, family, if_id);
		if (IS_ERR(pol)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
			return 0;
		}
	}

	if (!pol)
		pol = xfrm_policy_lookup(net, &fl, family, dir, if_id);

	if (IS_ERR(pol)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
		return 0;
	}

	if (!pol) {
		if (sp && secpath_has_nontransport(sp, 0, &xerr_idx)) {
			xfrm_secpath_reject(xerr_idx, skb, &fl);
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINNOPOLS);
			return 0;
		}
		return 1;
	}

	pol->curlft.use_time = ktime_get_real_seconds();

	pols[0] = pol;
	npols++;
#ifdef CONFIG_XFRM_SUB_POLICY
	if (pols[0]->type != XFRM_POLICY_TYPE_MAIN) {
		pols[1] = xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_MAIN,
						    &fl, family,
						    XFRM_POLICY_IN, if_id);
		if (pols[1]) {
			if (IS_ERR(pols[1])) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
				return 0;
			}
			pols[1]->curlft.use_time = ktime_get_real_seconds();
			npols++;
		}
	}
#endif

	if (pol->action == XFRM_POLICY_ALLOW) {
		static struct sec_path dummy;
		struct xfrm_tmpl *tp[XFRM_MAX_DEPTH];
		struct xfrm_tmpl *stp[XFRM_MAX_DEPTH];
		struct xfrm_tmpl **tpp = tp;
		int ti = 0;
		int i, k;

		sp = skb_sec_path(skb);
		if (!sp)
			sp = &dummy;

		for (pi = 0; pi < npols; pi++) {
			if (pols[pi] != pol &&
			    pols[pi]->action != XFRM_POLICY_ALLOW) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLBLOCK);
				goto reject;
			}
			if (ti + pols[pi]->xfrm_nr >= XFRM_MAX_DEPTH) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINBUFFERERROR);
				goto reject_error;
			}
			for (i = 0; i < pols[pi]->xfrm_nr; i++)
				tpp[ti++] = &pols[pi]->xfrm_vec[i];
		}
		xfrm_nr = ti;
		if (npols > 1) {
			xfrm_tmpl_sort(stp, tpp, xfrm_nr, family);
			tpp = stp;
		}

		/* For each tunnel xfrm, find the first matching tmpl.
		 * For each tmpl before that, find corresponding xfrm.
		 * Order is _important_. Later we will implement
		 * some barriers, but at the moment barriers
		 * are implied between each two transformations.
		 */
		for (i = xfrm_nr-1, k = 0; i >= 0; i--) {
			k = xfrm_policy_ok(tpp[i], sp, k, family);
			if (k < 0) {
				if (k < -1)
					/* "-2 - errored_index" returned */
					xerr_idx = -(2+k);
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINTMPLMISMATCH);
				goto reject;
			}
		}

		if (secpath_has_nontransport(sp, k, &xerr_idx)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINTMPLMISMATCH);
			goto reject;
		}

		xfrm_pols_put(pols, npols);
		return 1;
	}
	XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLBLOCK);

reject:
	xfrm_secpath_reject(xerr_idx, skb, &fl);
reject_error:
	xfrm_pols_put(pols, npols);
	return 0;
}
EXPORT_SYMBOL(__xfrm_policy_check);

int __xfrm_route_forward(struct sk_buff *skb, unsigned short family)
{
	struct net *net = dev_net(skb->dev);
	struct flowi fl;
	struct dst_entry *dst;
	int res = 1;

	if (xfrm_decode_session(skb, &fl, family) < 0) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMFWDHDRERROR);
		return 0;
	}

	skb_dst_force(skb);
	if (!skb_dst(skb)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMFWDHDRERROR);
		return 0;
	}

	dst = xfrm_lookup(net, skb_dst(skb), &fl, NULL, XFRM_LOOKUP_QUEUE);
	if (IS_ERR(dst)) {
		res = 0;
		dst = NULL;
	}
	skb_dst_set(skb, dst);
	return res;
}
EXPORT_SYMBOL(__xfrm_route_forward);

/* Optimize later using cookies and generation ids. */

static struct dst_entry *xfrm_dst_check(struct dst_entry *dst, u32 cookie)
{
	/* Code (such as __xfrm4_bundle_create()) sets dst->obsolete
	 * to DST_OBSOLETE_FORCE_CHK to force all XFRM destinations to
	 * get validated by dst_ops->check on every use.  We do this
	 * because when a normal route referenced by an XFRM dst is
	 * obsoleted we do not go looking around for all parent
	 * referencing XFRM dsts so that we can invalidate them.  It
	 * is just too much work.  Instead we make the checks here on
	 * every use.  For example:
	 *
	 *	XFRM dst A --> IPv4 dst X
	 *
	 * X is the "xdst->route" of A (X is also the "dst->path" of A
	 * in this example).  If X is marked obsolete, "A" will not
	 * notice.  That's what we are validating here via the
	 * stale_bundle() check.
	 *
	 * When a dst is removed from the fib tree, DST_OBSOLETE_DEAD will
	 * be marked on it.
	 * This will force stale_bundle() to fail on any xdst bundle with
	 * this dst linked in it.
	 */
	if (dst->obsolete < 0 && !stale_bundle(dst))
		return dst;

	return NULL;
}

static int stale_bundle(struct dst_entry *dst)
{
	return !xfrm_bundle_ok((struct xfrm_dst *)dst);
}

void xfrm_dst_ifdown(struct dst_entry *dst, struct net_device *dev)
{
	while ((dst = xfrm_dst_child(dst)) && dst->xfrm && dst->dev == dev) {
		dst->dev = dev_net(dev)->loopback_dev;
		dev_hold(dst->dev);
		dev_put(dev);
	}
}
EXPORT_SYMBOL(xfrm_dst_ifdown);

static void xfrm_link_failure(struct sk_buff *skb)
{
	/* Impossible. Such dst must be popped before reaches point of failure. */
}

static struct dst_entry *xfrm_negative_advice(struct dst_entry *dst)
{
	if (dst) {
		if (dst->obsolete) {
			dst_release(dst);
			dst = NULL;
		}
	}
	return dst;
}

static void xfrm_init_pmtu(struct xfrm_dst **bundle, int nr)
{
	while (nr--) {
		struct xfrm_dst *xdst = bundle[nr];
		u32 pmtu, route_mtu_cached;
		struct dst_entry *dst;

		dst = &xdst->u.dst;
		pmtu = dst_mtu(xfrm_dst_child(dst));
		xdst->child_mtu_cached = pmtu;

		pmtu = xfrm_state_mtu(dst->xfrm, pmtu);

		route_mtu_cached = dst_mtu(xdst->route);
		xdst->route_mtu_cached = route_mtu_cached;

		if (pmtu > route_mtu_cached)
			pmtu = route_mtu_cached;

		dst_metric_set(dst, RTAX_MTU, pmtu);
	}
}

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static int xfrm_bundle_ok(struct xfrm_dst *first)
{
	struct xfrm_dst *bundle[XFRM_MAX_DEPTH];
	struct dst_entry *dst = &first->u.dst;
	struct xfrm_dst *xdst;
	int start_from, nr;
	u32 mtu;

	if (!dst_check(xfrm_dst_path(dst), ((struct xfrm_dst *)dst)->path_cookie) ||
	    (dst->dev && !netif_running(dst->dev)))
		return 0;

	if (dst->flags & DST_XFRM_QUEUE)
		return 1;

	start_from = nr = 0;
	do {
		struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

		if (dst->xfrm->km.state != XFRM_STATE_VALID)
			return 0;
		if (xdst->xfrm_genid != dst->xfrm->genid)
			return 0;
		if (xdst->num_pols > 0 &&
		    xdst->policy_genid != atomic_read(&xdst->pols[0]->genid))
			return 0;

		bundle[nr++] = xdst;

		mtu = dst_mtu(xfrm_dst_child(dst));
		if (xdst->child_mtu_cached != mtu) {
			start_from = nr;
			xdst->child_mtu_cached = mtu;
		}

		if (!dst_check(xdst->route, xdst->route_cookie))
			return 0;
		mtu = dst_mtu(xdst->route);
		if (xdst->route_mtu_cached != mtu) {
			start_from = nr;
			xdst->route_mtu_cached = mtu;
		}

		dst = xfrm_dst_child(dst);
	} while (dst->xfrm);

	if (likely(!start_from))
		return 1;

	xdst = bundle[start_from - 1];
	mtu = xdst->child_mtu_cached;
	while (start_from--) {
		dst = &xdst->u.dst;

		mtu = xfrm_state_mtu(dst->xfrm, mtu);
		if (mtu > xdst->route_mtu_cached)
			mtu = xdst->route_mtu_cached;
		dst_metric_set(dst, RTAX_MTU, mtu);
		if (!start_from)
			break;

		xdst = bundle[start_from - 1];
		xdst->child_mtu_cached = mtu;
	}

	return 1;
}

static unsigned int xfrm_default_advmss(const struct dst_entry *dst)
{
	return dst_metric_advmss(xfrm_dst_path(dst));
}

static unsigned int xfrm_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst_mtu(xfrm_dst_path(dst));
}

static const void *xfrm_get_dst_nexthop(const struct dst_entry *dst,
					const void *daddr)
{
	while (dst->xfrm) {
		const struct xfrm_state *xfrm = dst->xfrm;

		dst = xfrm_dst_child(dst);

		if (xfrm->props.mode == XFRM_MODE_TRANSPORT)
			continue;
		if (xfrm->type->flags & XFRM_TYPE_REMOTE_COADDR)
			daddr = xfrm->coaddr;
		else if (!(xfrm->type->flags & XFRM_TYPE_LOCAL_COADDR))
			daddr = &xfrm->id.daddr;
	}
	return daddr;
}

static struct neighbour *xfrm_neigh_lookup(const struct dst_entry *dst,
					   struct sk_buff *skb,
					   const void *daddr)
{
	const struct dst_entry *path = xfrm_dst_path(dst);

	if (!skb)
		daddr = xfrm_get_dst_nexthop(dst, daddr);
	return path->ops->neigh_lookup(path, skb, daddr);
}

static void xfrm_confirm_neigh(const struct dst_entry *dst, const void *daddr)
{
	const struct dst_entry *path = xfrm_dst_path(dst);

	daddr = xfrm_get_dst_nexthop(dst, daddr);
	path->ops->confirm_neigh(path, daddr);
}

int xfrm_policy_register_afinfo(const struct xfrm_policy_afinfo *afinfo, int family)
{
	int err = 0;

	if (WARN_ON(family >= ARRAY_SIZE(xfrm_policy_afinfo)))
		return -EAFNOSUPPORT;

	spin_lock(&xfrm_policy_afinfo_lock);
	if (unlikely(xfrm_policy_afinfo[family] != NULL))
		err = -EEXIST;
	else {
		struct dst_ops *dst_ops = afinfo->dst_ops;
		if (likely(dst_ops->kmem_cachep == NULL))
			dst_ops->kmem_cachep = xfrm_dst_cache;
		if (likely(dst_ops->check == NULL))
			dst_ops->check = xfrm_dst_check;
		if (likely(dst_ops->default_advmss == NULL))
			dst_ops->default_advmss = xfrm_default_advmss;
		if (likely(dst_ops->mtu == NULL))
			dst_ops->mtu = xfrm_mtu;
		if (likely(dst_ops->negative_advice == NULL))
			dst_ops->negative_advice = xfrm_negative_advice;
		if (likely(dst_ops->link_failure == NULL))
			dst_ops->link_failure = xfrm_link_failure;
		if (likely(dst_ops->neigh_lookup == NULL))
			dst_ops->neigh_lookup = xfrm_neigh_lookup;
		if (likely(!dst_ops->confirm_neigh))
			dst_ops->confirm_neigh = xfrm_confirm_neigh;
		rcu_assign_pointer(xfrm_policy_afinfo[family], afinfo);
	}
	spin_unlock(&xfrm_policy_afinfo_lock);

	return err;
}
EXPORT_SYMBOL(xfrm_policy_register_afinfo);

void xfrm_policy_unregister_afinfo(const struct xfrm_policy_afinfo *afinfo)
{
	struct dst_ops *dst_ops = afinfo->dst_ops;
	int i;

	for (i = 0; i < ARRAY_SIZE(xfrm_policy_afinfo); i++) {
		if (xfrm_policy_afinfo[i] != afinfo)
			continue;
		RCU_INIT_POINTER(xfrm_policy_afinfo[i], NULL);
		break;
	}

	synchronize_rcu();

	dst_ops->kmem_cachep = NULL;
	dst_ops->check = NULL;
	dst_ops->negative_advice = NULL;
	dst_ops->link_failure = NULL;
}
EXPORT_SYMBOL(xfrm_policy_unregister_afinfo);

void xfrm_if_register_cb(const struct xfrm_if_cb *ifcb)
{
	spin_lock(&xfrm_if_cb_lock);
	rcu_assign_pointer(xfrm_if_cb, ifcb);
	spin_unlock(&xfrm_if_cb_lock);
}
EXPORT_SYMBOL(xfrm_if_register_cb);

void xfrm_if_unregister_cb(void)
{
	RCU_INIT_POINTER(xfrm_if_cb, NULL);
	synchronize_rcu();
}
EXPORT_SYMBOL(xfrm_if_unregister_cb);

#ifdef CONFIG_XFRM_STATISTICS
static int __net_init xfrm_statistics_init(struct net *net)
{
	int rv;
	net->mib.xfrm_statistics = alloc_percpu(struct linux_xfrm_mib);
	if (!net->mib.xfrm_statistics)
		return -ENOMEM;
	rv = xfrm_proc_init(net);
	if (rv < 0)
		free_percpu(net->mib.xfrm_statistics);
	return rv;
}

static void xfrm_statistics_fini(struct net *net)
{
	xfrm_proc_fini(net);
	free_percpu(net->mib.xfrm_statistics);
}
#else
static int __net_init xfrm_statistics_init(struct net *net)
{
	return 0;
}

static void xfrm_statistics_fini(struct net *net)
{
}
#endif

static int __net_init xfrm_policy_init(struct net *net)
{
	unsigned int hmask, sz;
	int dir, err;

	if (net_eq(net, &init_net)) {
		xfrm_dst_cache = kmem_cache_create("xfrm_dst_cache",
					   sizeof(struct xfrm_dst),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL);
		err = rhashtable_init(&xfrm_policy_inexact_table,
				      &xfrm_pol_inexact_params);
		BUG_ON(err);
	}

	hmask = 8 - 1;
	sz = (hmask+1) * sizeof(struct hlist_head);

	net->xfrm.policy_byidx = xfrm_hash_alloc(sz);
	if (!net->xfrm.policy_byidx)
		goto out_byidx;
	net->xfrm.policy_idx_hmask = hmask;

	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		struct xfrm_policy_hash *htab;

		net->xfrm.policy_count[dir] = 0;
		net->xfrm.policy_count[XFRM_POLICY_MAX + dir] = 0;
		INIT_HLIST_HEAD(&net->xfrm.policy_inexact[dir]);

		htab = &net->xfrm.policy_bydst[dir];
		htab->table = xfrm_hash_alloc(sz);
		if (!htab->table)
			goto out_bydst;
		htab->hmask = hmask;
		htab->dbits4 = 32;
		htab->sbits4 = 32;
		htab->dbits6 = 128;
		htab->sbits6 = 128;
	}
	net->xfrm.policy_hthresh.lbits4 = 32;
	net->xfrm.policy_hthresh.rbits4 = 32;
	net->xfrm.policy_hthresh.lbits6 = 128;
	net->xfrm.policy_hthresh.rbits6 = 128;

	seqlock_init(&net->xfrm.policy_hthresh.lock);

	INIT_LIST_HEAD(&net->xfrm.policy_all);
	INIT_LIST_HEAD(&net->xfrm.inexact_bins);
	INIT_WORK(&net->xfrm.policy_hash_work, xfrm_hash_resize);
	INIT_WORK(&net->xfrm.policy_hthresh.work, xfrm_hash_rebuild);
	return 0;

out_bydst:
	for (dir--; dir >= 0; dir--) {
		struct xfrm_policy_hash *htab;

		htab = &net->xfrm.policy_bydst[dir];
		xfrm_hash_free(htab->table, sz);
	}
	xfrm_hash_free(net->xfrm.policy_byidx, sz);
out_byidx:
	return -ENOMEM;
}

static void xfrm_policy_fini(struct net *net)
{
	struct xfrm_pol_inexact_bin *b, *t;
	unsigned int sz;
	int dir;

	flush_work(&net->xfrm.policy_hash_work);
#ifdef CONFIG_XFRM_SUB_POLICY
	xfrm_policy_flush(net, XFRM_POLICY_TYPE_SUB, false);
#endif
	xfrm_policy_flush(net, XFRM_POLICY_TYPE_MAIN, false);

	WARN_ON(!list_empty(&net->xfrm.policy_all));

	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		struct xfrm_policy_hash *htab;

		WARN_ON(!hlist_empty(&net->xfrm.policy_inexact[dir]));

		htab = &net->xfrm.policy_bydst[dir];
		sz = (htab->hmask + 1) * sizeof(struct hlist_head);
		WARN_ON(!hlist_empty(htab->table));
		xfrm_hash_free(htab->table, sz);
	}

	sz = (net->xfrm.policy_idx_hmask + 1) * sizeof(struct hlist_head);
	WARN_ON(!hlist_empty(net->xfrm.policy_byidx));
	xfrm_hash_free(net->xfrm.policy_byidx, sz);

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	list_for_each_entry_safe(b, t, &net->xfrm.inexact_bins, inexact_bins)
		__xfrm_policy_inexact_prune_bin(b, true);
	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
}

static int __net_init xfrm_net_init(struct net *net)
{
	int rv;

	/* Initialize the per-net locks here */
	spin_lock_init(&net->xfrm.xfrm_state_lock);
	spin_lock_init(&net->xfrm.xfrm_policy_lock);
	mutex_init(&net->xfrm.xfrm_cfg_mutex);

	rv = xfrm_statistics_init(net);
	if (rv < 0)
		goto out_statistics;
	rv = xfrm_state_init(net);
	if (rv < 0)
		goto out_state;
	rv = xfrm_policy_init(net);
	if (rv < 0)
		goto out_policy;
	rv = xfrm_sysctl_init(net);
	if (rv < 0)
		goto out_sysctl;

	return 0;

out_sysctl:
	xfrm_policy_fini(net);
out_policy:
	xfrm_state_fini(net);
out_state:
	xfrm_statistics_fini(net);
out_statistics:
	return rv;
}

static void __net_exit xfrm_net_exit(struct net *net)
{
	xfrm_sysctl_fini(net);
	xfrm_policy_fini(net);
	xfrm_state_fini(net);
	xfrm_statistics_fini(net);
}

static struct pernet_operations __net_initdata xfrm_net_ops = {
	.init = xfrm_net_init,
	.exit = xfrm_net_exit,
};

void __init xfrm_init(void)
{
	register_pernet_subsys(&xfrm_net_ops);
	xfrm_dev_init();
	seqcount_mutex_init(&xfrm_policy_hash_generation, &hash_resize_mutex);
	xfrm_input_init();

#ifdef CONFIG_XFRM_ESPINTCP
	espintcp_init();
#endif

	RCU_INIT_POINTER(xfrm_if_cb, NULL);
	synchronize_rcu();
}

#ifdef CONFIG_AUDITSYSCALL
static void xfrm_audit_common_policyinfo(struct xfrm_policy *xp,
					 struct audit_buffer *audit_buf)
{
	struct xfrm_sec_ctx *ctx = xp->security;
	struct xfrm_selector *sel = &xp->selector;

	if (ctx)
		audit_log_format(audit_buf, " sec_alg=%u sec_doi=%u sec_obj=%s",
				 ctx->ctx_alg, ctx->ctx_doi, ctx->ctx_str);

	switch (sel->family) {
	case AF_INET:
		audit_log_format(audit_buf, " src=%pI4", &sel->saddr.a4);
		if (sel->prefixlen_s != 32)
			audit_log_format(audit_buf, " src_prefixlen=%d",
					 sel->prefixlen_s);
		audit_log_format(audit_buf, " dst=%pI4", &sel->daddr.a4);
		if (sel->prefixlen_d != 32)
			audit_log_format(audit_buf, " dst_prefixlen=%d",
					 sel->prefixlen_d);
		break;
	case AF_INET6:
		audit_log_format(audit_buf, " src=%pI6", sel->saddr.a6);
		if (sel->prefixlen_s != 128)
			audit_log_format(audit_buf, " src_prefixlen=%d",
					 sel->prefixlen_s);
		audit_log_format(audit_buf, " dst=%pI6", sel->daddr.a6);
		if (sel->prefixlen_d != 128)
			audit_log_format(audit_buf, " dst_prefixlen=%d",
					 sel->prefixlen_d);
		break;
	}
}

void xfrm_audit_policy_add(struct xfrm_policy *xp, int result, bool task_valid)
{
	struct audit_buffer *audit_buf;

	audit_buf = xfrm_audit_start("SPD-add");
	if (audit_buf == NULL)
		return;
	xfrm_audit_helper_usrinfo(task_valid, audit_buf);
	audit_log_format(audit_buf, " res=%u", result);
	xfrm_audit_common_policyinfo(xp, audit_buf);
	audit_log_end(audit_buf);
}
EXPORT_SYMBOL_GPL(xfrm_audit_policy_add);

void xfrm_audit_policy_delete(struct xfrm_policy *xp, int result,
			      bool task_valid)
{
	struct audit_buffer *audit_buf;

	audit_buf = xfrm_audit_start("SPD-delete");
	if (audit_buf == NULL)
		return;
	xfrm_audit_helper_usrinfo(task_valid, audit_buf);
	audit_log_format(audit_buf, " res=%u", result);
	xfrm_audit_common_policyinfo(xp, audit_buf);
	audit_log_end(audit_buf);
}
EXPORT_SYMBOL_GPL(xfrm_audit_policy_delete);
#endif

#ifdef CONFIG_XFRM_MIGRATE
static bool xfrm_migrate_selector_match(const struct xfrm_selector *sel_cmp,
					const struct xfrm_selector *sel_tgt)
{
	if (sel_cmp->proto == IPSEC_ULPROTO_ANY) {
		if (sel_tgt->family == sel_cmp->family &&
		    xfrm_addr_equal(&sel_tgt->daddr, &sel_cmp->daddr,
				    sel_cmp->family) &&
		    xfrm_addr_equal(&sel_tgt->saddr, &sel_cmp->saddr,
				    sel_cmp->family) &&
		    sel_tgt->prefixlen_d == sel_cmp->prefixlen_d &&
		    sel_tgt->prefixlen_s == sel_cmp->prefixlen_s) {
			return true;
		}
	} else {
		if (memcmp(sel_tgt, sel_cmp, sizeof(*sel_tgt)) == 0) {
			return true;
		}
	}
	return false;
}

static struct xfrm_policy *xfrm_migrate_policy_find(const struct xfrm_selector *sel,
						    u8 dir, u8 type, struct net *net, u32 if_id)
{
	struct xfrm_policy *pol, *ret = NULL;
	struct hlist_head *chain;
	u32 priority = ~0U;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_direct(net, &sel->daddr, &sel->saddr, sel->family, dir);
	hlist_for_each_entry(pol, chain, bydst) {
		if ((if_id == 0 || pol->if_id == if_id) &&
		    xfrm_migrate_selector_match(sel, &pol->selector) &&
		    pol->type == type) {
			ret = pol;
			priority = ret->priority;
			break;
		}
	}
	chain = &net->xfrm.policy_inexact[dir];
	hlist_for_each_entry(pol, chain, bydst_inexact_list) {
		if ((pol->priority >= priority) && ret)
			break;

		if ((if_id == 0 || pol->if_id == if_id) &&
		    xfrm_migrate_selector_match(sel, &pol->selector) &&
		    pol->type == type) {
			ret = pol;
			break;
		}
	}

	xfrm_pol_hold(ret);

	spin_unlock_bh(&net->xfrm.xfrm_policy_lock);

	return ret;
}

static int migrate_tmpl_match(const struct xfrm_migrate *m, const struct xfrm_tmpl *t)
{
	int match = 0;

	if (t->mode == m->mode && t->id.proto == m->proto &&
	    (m->reqid == 0 || t->reqid == m->reqid)) {
		switch (t->mode) {
		case XFRM_MODE_TUNNEL:
		case XFRM_MODE_BEET:
			if (xfrm_addr_equal(&t->id.daddr, &m->old_daddr,
					    m->old_family) &&
			    xfrm_addr_equal(&t->saddr, &m->old_saddr,
					    m->old_family)) {
				match = 1;
			}
			break;
		case XFRM_MODE_TRANSPORT:
			/* in case of transport mode, template does not store
			   any IP addresses, hence we just compare mode and
			   protocol */
			match = 1;
			break;
		default:
			break;
		}
	}
	return match;
}

/* update endpoint address(es) of template(s) */
static int xfrm_policy_migrate(struct xfrm_policy *pol,
			       struct xfrm_migrate *m, int num_migrate)
{
	struct xfrm_migrate *mp;
	int i, j, n = 0;

	write_lock_bh(&pol->lock);
	if (unlikely(pol->walk.dead)) {
		/* target policy has been deleted */
		write_unlock_bh(&pol->lock);
		return -ENOENT;
	}

	for (i = 0; i < pol->xfrm_nr; i++) {
		for (j = 0, mp = m; j < num_migrate; j++, mp++) {
			if (!migrate_tmpl_match(mp, &pol->xfrm_vec[i]))
				continue;
			n++;
			if (pol->xfrm_vec[i].mode != XFRM_MODE_TUNNEL &&
			    pol->xfrm_vec[i].mode != XFRM_MODE_BEET)
				continue;
			/* update endpoints */
			memcpy(&pol->xfrm_vec[i].id.daddr, &mp->new_daddr,
			       sizeof(pol->xfrm_vec[i].id.daddr));
			memcpy(&pol->xfrm_vec[i].saddr, &mp->new_saddr,
			       sizeof(pol->xfrm_vec[i].saddr));
			pol->xfrm_vec[i].encap_family = mp->new_family;
			/* flush bundles */
			atomic_inc(&pol->genid);
		}
	}

	write_unlock_bh(&pol->lock);

	if (!n)
		return -ENODATA;

	return 0;
}

static int xfrm_migrate_check(const struct xfrm_migrate *m, int num_migrate)
{
	int i, j;

	if (num_migrate < 1 || num_migrate > XFRM_MAX_DEPTH)
		return -EINVAL;

	for (i = 0; i < num_migrate; i++) {
		if (xfrm_addr_any(&m[i].new_daddr, m[i].new_family) ||
		    xfrm_addr_any(&m[i].new_saddr, m[i].new_family))
			return -EINVAL;

		/* check if there is any duplicated entry */
		for (j = i + 1; j < num_migrate; j++) {
			if (!memcmp(&m[i].old_daddr, &m[j].old_daddr,
				    sizeof(m[i].old_daddr)) &&
			    !memcmp(&m[i].old_saddr, &m[j].old_saddr,
				    sizeof(m[i].old_saddr)) &&
			    m[i].proto == m[j].proto &&
			    m[i].mode == m[j].mode &&
			    m[i].reqid == m[j].reqid &&
			    m[i].old_family == m[j].old_family)
				return -EINVAL;
		}
	}

	return 0;
}

int xfrm_migrate(const struct xfrm_selector *sel, u8 dir, u8 type,
		 struct xfrm_migrate *m, int num_migrate,
		 struct xfrm_kmaddress *k, struct net *net,
		 struct xfrm_encap_tmpl *encap, u32 if_id)
{
	int i, err, nx_cur = 0, nx_new = 0;
	struct xfrm_policy *pol = NULL;
	struct xfrm_state *x, *xc;
	struct xfrm_state *x_cur[XFRM_MAX_DEPTH];
	struct xfrm_state *x_new[XFRM_MAX_DEPTH];
	struct xfrm_migrate *mp;

	/* Stage 0 - sanity checks */
	if ((err = xfrm_migrate_check(m, num_migrate)) < 0)
		goto out;

	if (dir >= XFRM_POLICY_MAX) {
		err = -EINVAL;
		goto out;
	}

	/* Stage 1 - find policy */
	if ((pol = xfrm_migrate_policy_find(sel, dir, type, net, if_id)) == NULL) {
		err = -ENOENT;
		goto out;
	}

	/* Stage 2 - find and update state(s) */
	for (i = 0, mp = m; i < num_migrate; i++, mp++) {
		if ((x = xfrm_migrate_state_find(mp, net, if_id))) {
			x_cur[nx_cur] = x;
			nx_cur++;
			xc = xfrm_state_migrate(x, mp, encap);
			if (xc) {
				x_new[nx_new] = xc;
				nx_new++;
			} else {
				err = -ENODATA;
				goto restore_state;
			}
		}
	}

	/* Stage 3 - update policy */
	if ((err = xfrm_policy_migrate(pol, m, num_migrate)) < 0)
		goto restore_state;

	/* Stage 4 - delete old state(s) */
	if (nx_cur) {
		xfrm_states_put(x_cur, nx_cur);
		xfrm_states_delete(x_cur, nx_cur);
	}

	/* Stage 5 - announce */
	km_migrate(sel, dir, type, m, num_migrate, k, encap);

	xfrm_pol_put(pol);

	return 0;
out:
	return err;

restore_state:
	if (pol)
		xfrm_pol_put(pol);
	if (nx_cur)
		xfrm_states_put(x_cur, nx_cur);
	if (nx_new)
		xfrm_states_delete(x_new, nx_new);

	return err;
}
EXPORT_SYMBOL(xfrm_migrate);
#endif
