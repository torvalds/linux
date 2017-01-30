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
#include <linux/audit.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/xfrm.h>
#include <net/ip.h>
#ifdef CONFIG_XFRM_STATISTICS
#include <net/snmp.h>
#endif

#include "xfrm_hash.h"

#define XFRM_QUEUE_TMO_MIN ((unsigned)(HZ/10))
#define XFRM_QUEUE_TMO_MAX ((unsigned)(60*HZ))
#define XFRM_MAX_QUEUE_LEN	100

struct xfrm_flo {
	struct dst_entry *dst_orig;
	u8 flags;
};

static DEFINE_SPINLOCK(xfrm_policy_afinfo_lock);
static struct xfrm_policy_afinfo __rcu *xfrm_policy_afinfo[NPROTO]
						__read_mostly;

static struct kmem_cache *xfrm_dst_cache __read_mostly;
static __read_mostly seqcount_t xfrm_policy_hash_generation;

static void xfrm_init_pmtu(struct dst_entry *dst);
static int stale_bundle(struct dst_entry *dst);
static int xfrm_bundle_ok(struct xfrm_dst *xdst);
static void xfrm_policy_queue_process(unsigned long arg);

static void __xfrm_policy_link(struct xfrm_policy *pol, int dir);
static struct xfrm_policy *__xfrm_policy_unlink(struct xfrm_policy *pol,
						int dir);

static inline bool xfrm_pol_hold_rcu(struct xfrm_policy *policy)
{
	return atomic_inc_not_zero(&policy->refcnt);
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

static struct xfrm_policy_afinfo *xfrm_policy_get_afinfo(unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo;

	if (unlikely(family >= NPROTO))
		return NULL;
	rcu_read_lock();
	afinfo = rcu_dereference(xfrm_policy_afinfo[family]);
	if (unlikely(!afinfo))
		rcu_read_unlock();
	return afinfo;
}

static void xfrm_policy_put_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	rcu_read_unlock();
}

static inline struct dst_entry *__xfrm_dst_lookup(struct net *net,
						  int tos, int oif,
						  const xfrm_address_t *saddr,
						  const xfrm_address_t *daddr,
						  int family)
{
	struct xfrm_policy_afinfo *afinfo;
	struct dst_entry *dst;

	afinfo = xfrm_policy_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return ERR_PTR(-EAFNOSUPPORT);

	dst = afinfo->dst_lookup(net, tos, oif, saddr, daddr);

	xfrm_policy_put_afinfo(afinfo);

	return dst;
}

static inline struct dst_entry *xfrm_dst_lookup(struct xfrm_state *x,
						int tos, int oif,
						xfrm_address_t *prev_saddr,
						xfrm_address_t *prev_daddr,
						int family)
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

	dst = __xfrm_dst_lookup(net, tos, oif, saddr, daddr, family);

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

static void xfrm_policy_timer(unsigned long data)
{
	struct xfrm_policy *xp = (struct xfrm_policy *)data;
	unsigned long now = get_seconds();
	long next = LONG_MAX;
	int warn = 0;
	int dir;

	read_lock(&xp->lock);

	if (unlikely(xp->walk.dead))
		goto out;

	dir = xfrm_policy_id2dir(xp->index);

	if (xp->lft.hard_add_expires_seconds) {
		long tmo = xp->lft.hard_add_expires_seconds +
			xp->curlft.add_time - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.hard_use_expires_seconds) {
		long tmo = xp->lft.hard_use_expires_seconds +
			(xp->curlft.use_time ? : xp->curlft.add_time) - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.soft_add_expires_seconds) {
		long tmo = xp->lft.soft_add_expires_seconds +
			xp->curlft.add_time - now;
		if (tmo <= 0) {
			warn = 1;
			tmo = XFRM_KM_TIMEOUT;
		}
		if (tmo < next)
			next = tmo;
	}
	if (xp->lft.soft_use_expires_seconds) {
		long tmo = xp->lft.soft_use_expires_seconds +
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
	if (next != LONG_MAX &&
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

static struct flow_cache_object *xfrm_policy_flo_get(struct flow_cache_object *flo)
{
	struct xfrm_policy *pol = container_of(flo, struct xfrm_policy, flo);

	if (unlikely(pol->walk.dead))
		flo = NULL;
	else
		xfrm_pol_hold(pol);

	return flo;
}

static int xfrm_policy_flo_check(struct flow_cache_object *flo)
{
	struct xfrm_policy *pol = container_of(flo, struct xfrm_policy, flo);

	return !pol->walk.dead;
}

static void xfrm_policy_flo_delete(struct flow_cache_object *flo)
{
	xfrm_pol_put(container_of(flo, struct xfrm_policy, flo));
}

static const struct flow_cache_ops xfrm_policy_fc_ops = {
	.get = xfrm_policy_flo_get,
	.check = xfrm_policy_flo_check,
	.delete = xfrm_policy_flo_delete,
};

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
		INIT_HLIST_NODE(&policy->bydst);
		INIT_HLIST_NODE(&policy->byidx);
		rwlock_init(&policy->lock);
		atomic_set(&policy->refcnt, 1);
		skb_queue_head_init(&policy->polq.hold_queue);
		setup_timer(&policy->timer, xfrm_policy_timer,
				(unsigned long)policy);
		setup_timer(&policy->polq.hold_timer, xfrm_policy_queue_process,
			    (unsigned long)policy);
		policy->flo.ops = &xfrm_policy_fc_ops;
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

/* Rule must be locked. Release descentant resources, announce
 * entry dead. The rule must be unlinked from lists to the moment.
 */

static void xfrm_policy_kill(struct xfrm_policy *policy)
{
	policy->walk.dead = 1;

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
		return &net->xfrm.policy_inexact[dir];

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

	/* reset the bydst and inexact table in all directions */
	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		INIT_HLIST_HEAD(&net->xfrm.policy_inexact[dir]);
		hmask = net->xfrm.policy_bydst[dir].hmask;
		odst = net->xfrm.policy_bydst[dir].table;
		for (i = hmask; i >= 0; i--)
			INIT_HLIST_HEAD(odst + i);
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
		if (xfrm_policy_id2dir(policy->index) >= XFRM_POLICY_MAX) {
			/* skip socket policies */
			continue;
		}
		newpos = NULL;
		chain = policy_hash_bysel(net, &policy->selector,
					  policy->family,
					  xfrm_policy_id2dir(policy->index));
		hlist_for_each_entry(pol, chain, bydst) {
			if (policy->priority >= pol->priority)
				newpos = &pol->bydst;
			else
				break;
		}
		if (newpos)
			hlist_add_behind(&policy->bydst, newpos);
		else
			hlist_add_head(&policy->bydst, chain);
	}

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

static bool xfrm_policy_mark_match(struct xfrm_policy *policy,
				   struct xfrm_policy *pol)
{
	u32 mark = policy->mark.v & policy->mark.m;

	if (policy->mark.v == pol->mark.v && policy->mark.m == pol->mark.m)
		return true;

	if ((mark & pol->mark.m) == pol->mark.v &&
	    policy->priority == pol->priority)
		return true;

	return false;
}

int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl)
{
	struct net *net = xp_net(policy);
	struct xfrm_policy *pol;
	struct xfrm_policy *delpol;
	struct hlist_head *chain;
	struct hlist_node *newpos;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_bysel(net, &policy->selector, policy->family, dir);
	delpol = NULL;
	newpos = NULL;
	hlist_for_each_entry(pol, chain, bydst) {
		if (pol->type == policy->type &&
		    !selector_cmp(&pol->selector, &policy->selector) &&
		    xfrm_policy_mark_match(policy, pol) &&
		    xfrm_sec_ctx_match(pol->security, policy->security) &&
		    !WARN_ON(delpol)) {
			if (excl) {
				spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
				return -EEXIST;
			}
			delpol = pol;
			if (policy->priority > pol->priority)
				continue;
		} else if (policy->priority >= pol->priority) {
			newpos = &pol->bydst;
			continue;
		}
		if (delpol)
			break;
	}
	if (newpos)
		hlist_add_behind(&policy->bydst, newpos);
	else
		hlist_add_head(&policy->bydst, chain);
	__xfrm_policy_link(policy, dir);
	atomic_inc(&net->xfrm.flow_cache_genid);

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
	policy->curlft.add_time = get_seconds();
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

struct xfrm_policy *xfrm_policy_bysel_ctx(struct net *net, u32 mark, u8 type,
					  int dir, struct xfrm_selector *sel,
					  struct xfrm_sec_ctx *ctx, int delete,
					  int *err)
{
	struct xfrm_policy *pol, *ret;
	struct hlist_head *chain;

	*err = 0;
	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_bysel(net, sel, sel->family, dir);
	ret = NULL;
	hlist_for_each_entry(pol, chain, bydst) {
		if (pol->type == type &&
		    (mark & pol->mark.m) == pol->mark.v &&
		    !selector_cmp(sel, &pol->selector) &&
		    xfrm_sec_ctx_match(ctx, pol->security)) {
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
EXPORT_SYMBOL(xfrm_policy_bysel_ctx);

struct xfrm_policy *xfrm_policy_byid(struct net *net, u32 mark, u8 type,
				     int dir, u32 id, int delete, int *err)
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
		    (mark & pol->mark.m) == pol->mark.v) {
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
	int dir, err = 0;

	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		struct xfrm_policy *pol;
		int i;

		hlist_for_each_entry(pol,
				     &net->xfrm.policy_inexact[dir], bydst) {
			if (pol->type != type)
				continue;
			err = security_xfrm_policy_delete(pol->security);
			if (err) {
				xfrm_audit_policy_delete(pol, 0, task_valid);
				return err;
			}
		}
		for (i = net->xfrm.policy_bydst[dir].hmask; i >= 0; i--) {
			hlist_for_each_entry(pol,
					     net->xfrm.policy_bydst[dir].table + i,
					     bydst) {
				if (pol->type != type)
					continue;
				err = security_xfrm_policy_delete(
								pol->security);
				if (err) {
					xfrm_audit_policy_delete(pol, 0,
								 task_valid);
					return err;
				}
			}
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

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);

	err = xfrm_policy_flush_secctx_check(net, type, task_valid);
	if (err)
		goto out;

	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		struct xfrm_policy *pol;
		int i;

	again1:
		hlist_for_each_entry(pol,
				     &net->xfrm.policy_inexact[dir], bydst) {
			if (pol->type != type)
				continue;
			__xfrm_policy_unlink(pol, dir);
			spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
			cnt++;

			xfrm_audit_policy_delete(pol, 1, task_valid);

			xfrm_policy_kill(pol);

			spin_lock_bh(&net->xfrm.xfrm_policy_lock);
			goto again1;
		}

		for (i = net->xfrm.policy_bydst[dir].hmask; i >= 0; i--) {
	again2:
			hlist_for_each_entry(pol,
					     net->xfrm.policy_bydst[dir].table + i,
					     bydst) {
				if (pol->type != type)
					continue;
				__xfrm_policy_unlink(pol, dir);
				spin_unlock_bh(&net->xfrm.xfrm_policy_lock);
				cnt++;

				xfrm_audit_policy_delete(pol, 1, task_valid);
				xfrm_policy_kill(pol);

				spin_lock_bh(&net->xfrm.xfrm_policy_lock);
				goto again2;
			}
		}

	}
	if (!cnt)
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
			     u8 type, u16 family, int dir)
{
	const struct xfrm_selector *sel = &pol->selector;
	int ret = -ESRCH;
	bool match;

	if (pol->family != family ||
	    (fl->flowi_mark & pol->mark.m) != pol->mark.v ||
	    pol->type != type)
		return ret;

	match = xfrm_selector_match(sel, fl, family);
	if (match)
		ret = security_xfrm_policy_lookup(pol->security, fl->flowi_secid,
						  dir);

	return ret;
}

static struct xfrm_policy *xfrm_policy_lookup_bytype(struct net *net, u8 type,
						     const struct flowi *fl,
						     u16 family, u8 dir)
{
	int err;
	struct xfrm_policy *pol, *ret;
	const xfrm_address_t *daddr, *saddr;
	struct hlist_head *chain;
	unsigned int sequence;
	u32 priority;

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

	priority = ~0U;
	ret = NULL;
	hlist_for_each_entry_rcu(pol, chain, bydst) {
		err = xfrm_policy_match(pol, fl, type, family, dir);
		if (err) {
			if (err == -ESRCH)
				continue;
			else {
				ret = ERR_PTR(err);
				goto fail;
			}
		} else {
			ret = pol;
			priority = ret->priority;
			break;
		}
	}
	chain = &net->xfrm.policy_inexact[dir];
	hlist_for_each_entry_rcu(pol, chain, bydst) {
		if ((pol->priority >= priority) && ret)
			break;

		err = xfrm_policy_match(pol, fl, type, family, dir);
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

	if (read_seqcount_retry(&xfrm_policy_hash_generation, sequence))
		goto retry;

	if (ret && !xfrm_pol_hold_rcu(ret))
		goto retry;
fail:
	rcu_read_unlock();

	return ret;
}

static struct xfrm_policy *
__xfrm_policy_lookup(struct net *net, const struct flowi *fl, u16 family, u8 dir)
{
#ifdef CONFIG_XFRM_SUB_POLICY
	struct xfrm_policy *pol;

	pol = xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_SUB, fl, family, dir);
	if (pol != NULL)
		return pol;
#endif
	return xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_MAIN, fl, family, dir);
}

static int flow_to_policy_dir(int dir)
{
	if (XFRM_POLICY_IN == FLOW_DIR_IN &&
	    XFRM_POLICY_OUT == FLOW_DIR_OUT &&
	    XFRM_POLICY_FWD == FLOW_DIR_FWD)
		return dir;

	switch (dir) {
	default:
	case FLOW_DIR_IN:
		return XFRM_POLICY_IN;
	case FLOW_DIR_OUT:
		return XFRM_POLICY_OUT;
	case FLOW_DIR_FWD:
		return XFRM_POLICY_FWD;
	}
}

static struct flow_cache_object *
xfrm_policy_lookup(struct net *net, const struct flowi *fl, u16 family,
		   u8 dir, struct flow_cache_object *old_obj, void *ctx)
{
	struct xfrm_policy *pol;

	if (old_obj)
		xfrm_pol_put(container_of(old_obj, struct xfrm_policy, flo));

	pol = __xfrm_policy_lookup(net, fl, family, flow_to_policy_dir(dir));
	if (IS_ERR_OR_NULL(pol))
		return ERR_CAST(pol);

	/* Resolver returns two references:
	 * one for cache and one for caller of flow_cache_lookup() */
	xfrm_pol_hold(pol);

	return &pol->flo;
}

static inline int policy_to_flow_dir(int dir)
{
	if (XFRM_POLICY_IN == FLOW_DIR_IN &&
	    XFRM_POLICY_OUT == FLOW_DIR_OUT &&
	    XFRM_POLICY_FWD == FLOW_DIR_FWD)
		return dir;
	switch (dir) {
	default:
	case XFRM_POLICY_IN:
		return FLOW_DIR_IN;
	case XFRM_POLICY_OUT:
		return FLOW_DIR_OUT;
	case XFRM_POLICY_FWD:
		return FLOW_DIR_FWD;
	}
}

static struct xfrm_policy *xfrm_sk_policy_lookup(const struct sock *sk, int dir,
						 const struct flowi *fl)
{
	struct xfrm_policy *pol;

	rcu_read_lock();
 again:
	pol = rcu_dereference(sk->sk_policy[dir]);
	if (pol != NULL) {
		bool match = xfrm_selector_match(&pol->selector, fl,
						 sk->sk_family);
		int err = 0;

		if (match) {
			if ((sk->sk_mark & pol->mark.m) != pol->mark.v) {
				pol = NULL;
				goto out;
			}
			err = security_xfrm_policy_lookup(pol->security,
						      fl->flowi_secid,
						      policy_to_flow_dir(dir));
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
	struct net *net = xp_net(pol);
	struct xfrm_policy *old_pol;

#ifdef CONFIG_XFRM_SUB_POLICY
	if (pol && pol->type != XFRM_POLICY_TYPE_MAIN)
		return -EINVAL;
#endif

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	old_pol = rcu_dereference_protected(sk->sk_policy[dir],
				lockdep_is_held(&net->xfrm.xfrm_policy_lock));
	if (pol) {
		pol->curlft.add_time = get_seconds();
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
		newp->action = old->action;
		newp->flags = old->flags;
		newp->xfrm_nr = old->xfrm_nr;
		newp->index = old->index;
		newp->type = old->type;
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
	       xfrm_address_t *remote, unsigned short family)
{
	int err;
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);

	if (unlikely(afinfo == NULL))
		return -EINVAL;
	err = afinfo->get_saddr(net, oif, local, remote);
	xfrm_policy_put_afinfo(afinfo);
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
						       tmpl->encap_family);
				if (error)
					goto fail;
				local = &tmp;
			}
		}

		x = xfrm_state_find(remote, local, fl, tmpl, policy, &error, family);

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

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static inline int xfrm_get_tos(const struct flowi *fl, int family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	int tos;

	if (!afinfo)
		return -EINVAL;

	tos = afinfo->get_tos(fl);

	xfrm_policy_put_afinfo(afinfo);

	return tos;
}

static struct flow_cache_object *xfrm_bundle_flo_get(struct flow_cache_object *flo)
{
	struct xfrm_dst *xdst = container_of(flo, struct xfrm_dst, flo);
	struct dst_entry *dst = &xdst->u.dst;

	if (xdst->route == NULL) {
		/* Dummy bundle - if it has xfrms we were not
		 * able to build bundle as template resolution failed.
		 * It means we need to try again resolving. */
		if (xdst->num_xfrms > 0)
			return NULL;
	} else if (dst->flags & DST_XFRM_QUEUE) {
		return NULL;
	} else {
		/* Real bundle */
		if (stale_bundle(dst))
			return NULL;
	}

	dst_hold(dst);
	return flo;
}

static int xfrm_bundle_flo_check(struct flow_cache_object *flo)
{
	struct xfrm_dst *xdst = container_of(flo, struct xfrm_dst, flo);
	struct dst_entry *dst = &xdst->u.dst;

	if (!xdst->route)
		return 0;
	if (stale_bundle(dst))
		return 0;

	return 1;
}

static void xfrm_bundle_flo_delete(struct flow_cache_object *flo)
{
	struct xfrm_dst *xdst = container_of(flo, struct xfrm_dst, flo);
	struct dst_entry *dst = &xdst->u.dst;

	dst_free(dst);
}

static const struct flow_cache_ops xfrm_bundle_fc_ops = {
	.get = xfrm_bundle_flo_get,
	.check = xfrm_bundle_flo_check,
	.delete = xfrm_bundle_flo_delete,
};

static inline struct xfrm_dst *xfrm_alloc_dst(struct net *net, int family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
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
	xdst = dst_alloc(dst_ops, NULL, 0, DST_OBSOLETE_NONE, 0);

	if (likely(xdst)) {
		struct dst_entry *dst = &xdst->u.dst;

		memset(dst + 1, 0, sizeof(*xdst) - sizeof(*dst));
		xdst->flo.ops = &xfrm_bundle_fc_ops;
	} else
		xdst = ERR_PTR(-ENOBUFS);

	xfrm_policy_put_afinfo(afinfo);

	return xdst;
}

static inline int xfrm_init_path(struct xfrm_dst *path, struct dst_entry *dst,
				 int nfheader_len)
{
	struct xfrm_policy_afinfo *afinfo =
		xfrm_policy_get_afinfo(dst->ops->family);
	int err;

	if (!afinfo)
		return -EINVAL;

	err = afinfo->init_path(path, dst, nfheader_len);

	xfrm_policy_put_afinfo(afinfo);

	return err;
}

static inline int xfrm_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
				const struct flowi *fl)
{
	struct xfrm_policy_afinfo *afinfo =
		xfrm_policy_get_afinfo(xdst->u.dst.ops->family);
	int err;

	if (!afinfo)
		return -EINVAL;

	err = afinfo->fill_dst(xdst, dev, fl);

	xfrm_policy_put_afinfo(afinfo);

	return err;
}


/* Allocate chain of dst_entry's, attach known xfrm's, calculate
 * all the metrics... Shortly, bundle a bundle.
 */

static struct dst_entry *xfrm_bundle_create(struct xfrm_policy *policy,
					    struct xfrm_state **xfrm, int nx,
					    const struct flowi *fl,
					    struct dst_entry *dst)
{
	struct net *net = xp_net(policy);
	unsigned long now = jiffies;
	struct net_device *dev;
	struct xfrm_mode *inner_mode;
	struct dst_entry *dst_prev = NULL;
	struct dst_entry *dst0 = NULL;
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
	err = tos;
	if (tos < 0)
		goto put_states;

	dst_hold(dst);

	for (; i < nx; i++) {
		struct xfrm_dst *xdst = xfrm_alloc_dst(net, family);
		struct dst_entry *dst1 = &xdst->u.dst;

		err = PTR_ERR(xdst);
		if (IS_ERR(xdst)) {
			dst_release(dst);
			goto put_states;
		}

		if (xfrm[i]->sel.family == AF_UNSPEC) {
			inner_mode = xfrm_ip2inner_mode(xfrm[i],
							xfrm_af2proto(family));
			if (!inner_mode) {
				err = -EAFNOSUPPORT;
				dst_release(dst);
				goto put_states;
			}
		} else
			inner_mode = xfrm[i]->inner_mode;

		if (!dst_prev)
			dst0 = dst1;
		else {
			dst_prev->child = dst_clone(dst1);
			dst1->flags |= DST_NOHASH;
		}

		xdst->route = dst;
		dst_copy_metrics(dst1, dst);

		if (xfrm[i]->props.mode != XFRM_MODE_TRANSPORT) {
			family = xfrm[i]->props.family;
			dst = xfrm_dst_lookup(xfrm[i], tos, fl->flowi_oif,
					      &saddr, &daddr, family);
			err = PTR_ERR(dst);
			if (IS_ERR(dst))
				goto put_states;
		} else
			dst_hold(dst);

		dst1->xfrm = xfrm[i];
		xdst->xfrm_genid = xfrm[i]->genid;

		dst1->obsolete = DST_OBSOLETE_FORCE_CHK;
		dst1->flags |= DST_HOST;
		dst1->lastuse = now;

		dst1->input = dst_discard;
		dst1->output = inner_mode->afinfo->output;

		dst1->next = dst_prev;
		dst_prev = dst1;

		header_len += xfrm[i]->props.header_len;
		if (xfrm[i]->type->flags & XFRM_TYPE_NON_FRAGMENT)
			nfheader_len += xfrm[i]->props.header_len;
		trailer_len += xfrm[i]->props.trailer_len;
	}

	dst_prev->child = dst;
	dst0->path = dst;

	err = -ENODEV;
	dev = dst->dev;
	if (!dev)
		goto free_dst;

	xfrm_init_path((struct xfrm_dst *)dst0, dst, nfheader_len);
	xfrm_init_pmtu(dst_prev);

	for (dst_prev = dst0; dst_prev != dst; dst_prev = dst_prev->child) {
		struct xfrm_dst *xdst = (struct xfrm_dst *)dst_prev;

		err = xfrm_fill_dst(xdst, dev, fl);
		if (err)
			goto free_dst;

		dst_prev->header_len = header_len;
		dst_prev->trailer_len = trailer_len;
		header_len -= xdst->u.dst.xfrm->props.header_len;
		trailer_len -= xdst->u.dst.xfrm->props.trailer_len;
	}

out:
	return dst0;

put_states:
	for (; i < nx; i++)
		xfrm_state_put(xfrm[i]);
free_dst:
	if (dst0)
		dst_free(dst0);
	dst0 = ERR_PTR(err);
	goto out;
}

#ifdef CONFIG_XFRM_SUB_POLICY
static int xfrm_dst_alloc_copy(void **target, const void *src, int size)
{
	if (!*target) {
		*target = kmalloc(size, GFP_ATOMIC);
		if (!*target)
			return -ENOMEM;
	}

	memcpy(*target, src, size);
	return 0;
}
#endif

static int xfrm_dst_update_parent(struct dst_entry *dst,
				  const struct xfrm_selector *sel)
{
#ifdef CONFIG_XFRM_SUB_POLICY
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	return xfrm_dst_alloc_copy((void **)&(xdst->partner),
				   sel, sizeof(*sel));
#else
	return 0;
#endif
}

static int xfrm_dst_update_origin(struct dst_entry *dst,
				  const struct flowi *fl)
{
#ifdef CONFIG_XFRM_SUB_POLICY
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	return xfrm_dst_alloc_copy((void **)&(xdst->origin), fl, sizeof(*fl));
#else
	return 0;
#endif
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
	if (IS_ERR(pols[0]))
		return PTR_ERR(pols[0]);

	*num_xfrms = pols[0]->xfrm_nr;

#ifdef CONFIG_XFRM_SUB_POLICY
	if (pols[0] && pols[0]->action == XFRM_POLICY_ALLOW &&
	    pols[0]->type != XFRM_POLICY_TYPE_MAIN) {
		pols[1] = xfrm_policy_lookup_bytype(xp_net(pols[0]),
						    XFRM_POLICY_TYPE_MAIN,
						    fl, family,
						    XFRM_POLICY_OUT);
		if (pols[1]) {
			if (IS_ERR(pols[1])) {
				xfrm_pols_put(pols, *num_pols);
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
	struct dst_entry *dst;
	struct xfrm_dst *xdst;
	int err;

	/* Try to instantiate a bundle */
	err = xfrm_tmpl_resolve(pols, num_pols, fl, xfrm, family);
	if (err <= 0) {
		if (err != 0 && err != -EAGAIN)
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTPOLERROR);
		return ERR_PTR(err);
	}

	dst = xfrm_bundle_create(pols[0], xfrm, err, fl, dst_orig);
	if (IS_ERR(dst)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTBUNDLEGENERROR);
		return ERR_CAST(dst);
	}

	xdst = (struct xfrm_dst *)dst;
	xdst->num_xfrms = err;
	if (num_pols > 1)
		err = xfrm_dst_update_parent(dst, &pols[1]->selector);
	else
		err = xfrm_dst_update_origin(dst, fl);
	if (unlikely(err)) {
		dst_free(dst);
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTBUNDLECHECKERROR);
		return ERR_PTR(err);
	}

	xdst->num_pols = num_pols;
	memcpy(xdst->pols, pols, sizeof(struct xfrm_policy *) * num_pols);
	xdst->policy_genid = atomic_read(&pols[0]->genid);

	return xdst;
}

static void xfrm_policy_queue_process(unsigned long arg)
{
	struct sk_buff *skb;
	struct sock *sk;
	struct dst_entry *dst;
	struct xfrm_policy *pol = (struct xfrm_policy *)arg;
	struct net *net = xp_net(pol);
	struct xfrm_policy_queue *pq = &pol->polq;
	struct flowi fl;
	struct sk_buff_head list;

	spin_lock(&pq->hold_queue.lock);
	skb = skb_peek(&pq->hold_queue);
	if (!skb) {
		spin_unlock(&pq->hold_queue.lock);
		goto out;
	}
	dst = skb_dst(skb);
	sk = skb->sk;
	xfrm_decode_session(skb, &fl, dst->ops->family);
	spin_unlock(&pq->hold_queue.lock);

	dst_hold(dst->path);
	dst = xfrm_lookup(net, dst->path, &fl, sk, 0);
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

		xfrm_decode_session(skb, &fl, skb_dst(skb)->ops->family);
		dst_hold(skb_dst(skb)->path);
		dst = xfrm_lookup(net, skb_dst(skb)->path, &fl, skb->sk, 0);
		if (IS_ERR(dst)) {
			kfree_skb(skb);
			continue;
		}

		nf_reset(skb);
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
	dst1->flags |= DST_HOST | DST_XFRM_QUEUE;
	dst1->lastuse = jiffies;

	dst1->input = dst_discard;
	dst1->output = xdst_queue_output;

	dst_hold(dst);
	dst1->child = dst;
	dst1->path = dst;

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

static struct flow_cache_object *
xfrm_bundle_lookup(struct net *net, const struct flowi *fl, u16 family, u8 dir,
		   struct flow_cache_object *oldflo, void *ctx)
{
	struct xfrm_flo *xflo = (struct xfrm_flo *)ctx;
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	struct xfrm_dst *xdst, *new_xdst;
	int num_pols = 0, num_xfrms = 0, i, err, pol_dead;

	/* Check if the policies from old bundle are usable */
	xdst = NULL;
	if (oldflo) {
		xdst = container_of(oldflo, struct xfrm_dst, flo);
		num_pols = xdst->num_pols;
		num_xfrms = xdst->num_xfrms;
		pol_dead = 0;
		for (i = 0; i < num_pols; i++) {
			pols[i] = xdst->pols[i];
			pol_dead |= pols[i]->walk.dead;
		}
		if (pol_dead) {
			dst_free(&xdst->u.dst);
			xdst = NULL;
			num_pols = 0;
			num_xfrms = 0;
			oldflo = NULL;
		}
	}

	/* Resolve policies to use if we couldn't get them from
	 * previous cache entry */
	if (xdst == NULL) {
		num_pols = 1;
		pols[0] = __xfrm_policy_lookup(net, fl, family,
					       flow_to_policy_dir(dir));
		err = xfrm_expand_policies(fl, family, pols,
					   &num_pols, &num_xfrms);
		if (err < 0)
			goto inc_error;
		if (num_pols == 0)
			return NULL;
		if (num_xfrms <= 0)
			goto make_dummy_bundle;
	}

	new_xdst = xfrm_resolve_and_create_bundle(pols, num_pols, fl, family,
						  xflo->dst_orig);
	if (IS_ERR(new_xdst)) {
		err = PTR_ERR(new_xdst);
		if (err != -EAGAIN)
			goto error;
		if (oldflo == NULL)
			goto make_dummy_bundle;
		dst_hold(&xdst->u.dst);
		return oldflo;
	} else if (new_xdst == NULL) {
		num_xfrms = 0;
		if (oldflo == NULL)
			goto make_dummy_bundle;
		xdst->num_xfrms = 0;
		dst_hold(&xdst->u.dst);
		return oldflo;
	}

	/* Kill the previous bundle */
	if (xdst) {
		/* The policies were stolen for newly generated bundle */
		xdst->num_pols = 0;
		dst_free(&xdst->u.dst);
	}

	/* Flow cache does not have reference, it dst_free()'s,
	 * but we do need to return one reference for original caller */
	dst_hold(&new_xdst->u.dst);
	return &new_xdst->flo;

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

	dst_hold(&xdst->u.dst);
	return &xdst->flo;

inc_error:
	XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTPOLERROR);
error:
	if (xdst != NULL)
		dst_free(&xdst->u.dst);
	else
		xfrm_pols_put(pols, num_pols);
	return ERR_PTR(err);
}

static struct dst_entry *make_blackhole(struct net *net, u16 family,
					struct dst_entry *dst_orig)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct dst_entry *ret;

	if (!afinfo) {
		dst_release(dst_orig);
		return ERR_PTR(-EINVAL);
	} else {
		ret = afinfo->blackhole_route(net, dst_orig);
	}
	xfrm_policy_put_afinfo(afinfo);

	return ret;
}

/* Main function: finds/creates a bundle for given flow.
 *
 * At the moment we eat a raw IP route. Mostly to speed up lookups
 * on interfaces with disabled IPsec.
 */
struct dst_entry *xfrm_lookup(struct net *net, struct dst_entry *dst_orig,
			      const struct flowi *fl,
			      const struct sock *sk, int flags)
{
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	struct flow_cache_object *flo;
	struct xfrm_dst *xdst;
	struct dst_entry *dst, *route;
	u16 family = dst_orig->ops->family;
	u8 dir = policy_to_flow_dir(XFRM_POLICY_OUT);
	int i, err, num_pols, num_xfrms = 0, drop_pols = 0;

	dst = NULL;
	xdst = NULL;
	route = NULL;

	sk = sk_const_to_full_sk(sk);
	if (sk && sk->sk_policy[XFRM_POLICY_OUT]) {
		num_pols = 1;
		pols[0] = xfrm_sk_policy_lookup(sk, XFRM_POLICY_OUT, fl);
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
				goto dropdst;
			} else if (xdst == NULL) {
				num_xfrms = 0;
				drop_pols = num_pols;
				goto no_transform;
			}

			dst_hold(&xdst->u.dst);
			xdst->u.dst.flags |= DST_NOCACHE;
			route = xdst->route;
		}
	}

	if (xdst == NULL) {
		struct xfrm_flo xflo;

		xflo.dst_orig = dst_orig;
		xflo.flags = flags;

		/* To accelerate a bit...  */
		if ((dst_orig->flags & DST_NOXFRM) ||
		    !net->xfrm.policy_count[XFRM_POLICY_OUT])
			goto nopol;

		flo = flow_cache_lookup(net, fl, family, dir,
					xfrm_bundle_lookup, &xflo);
		if (flo == NULL)
			goto nopol;
		if (IS_ERR(flo)) {
			err = PTR_ERR(flo);
			goto dropdst;
		}
		xdst = container_of(flo, struct xfrm_dst, flo);

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
		pols[i]->curlft.use_time = get_seconds();

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

	if (IS_ERR(dst) && PTR_ERR(dst) == -EREMOTE)
		return make_blackhole(net, dst_orig->ops->family, dst_orig);

	return dst;
}
EXPORT_SYMBOL(xfrm_lookup_route);

static inline int
xfrm_secpath_reject(int idx, struct sk_buff *skb, const struct flowi *fl)
{
	struct xfrm_state *x;

	if (!skb->sp || idx < 0 || idx >= skb->sp->len)
		return 0;
	x = skb->sp->xvec[idx];
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

int __xfrm_decode_session(struct sk_buff *skb, struct flowi *fl,
			  unsigned int family, int reverse)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	int err;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	afinfo->decode_session(skb, fl, reverse);
	err = security_xfrm_decode_session(skb, &fl->flowi_secid);
	xfrm_policy_put_afinfo(afinfo);
	return err;
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
	u8 fl_dir;
	int xerr_idx = -1;

	reverse = dir & ~XFRM_POLICY_MASK;
	dir &= XFRM_POLICY_MASK;
	fl_dir = policy_to_flow_dir(dir);

	if (__xfrm_decode_session(skb, &fl, family, reverse) < 0) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINHDRERROR);
		return 0;
	}

	nf_nat_decode_session(skb, &fl, family);

	/* First, check used SA against their selectors. */
	if (skb->sp) {
		int i;

		for (i = skb->sp->len-1; i >= 0; i--) {
			struct xfrm_state *x = skb->sp->xvec[i];
			if (!xfrm_selector_match(&x->sel, &fl, family)) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINSTATEMISMATCH);
				return 0;
			}
		}
	}

	pol = NULL;
	sk = sk_to_full_sk(sk);
	if (sk && sk->sk_policy[dir]) {
		pol = xfrm_sk_policy_lookup(sk, dir, &fl);
		if (IS_ERR(pol)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
			return 0;
		}
	}

	if (!pol) {
		struct flow_cache_object *flo;

		flo = flow_cache_lookup(net, &fl, family, fl_dir,
					xfrm_policy_lookup, NULL);
		if (IS_ERR_OR_NULL(flo))
			pol = ERR_CAST(flo);
		else
			pol = container_of(flo, struct xfrm_policy, flo);
	}

	if (IS_ERR(pol)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
		return 0;
	}

	if (!pol) {
		if (skb->sp && secpath_has_nontransport(skb->sp, 0, &xerr_idx)) {
			xfrm_secpath_reject(xerr_idx, skb, &fl);
			XFRM_INC_STATS(net, LINUX_MIB_XFRMINNOPOLS);
			return 0;
		}
		return 1;
	}

	pol->curlft.use_time = get_seconds();

	pols[0] = pol;
	npols++;
#ifdef CONFIG_XFRM_SUB_POLICY
	if (pols[0]->type != XFRM_POLICY_TYPE_MAIN) {
		pols[1] = xfrm_policy_lookup_bytype(net, XFRM_POLICY_TYPE_MAIN,
						    &fl, family,
						    XFRM_POLICY_IN);
		if (pols[1]) {
			if (IS_ERR(pols[1])) {
				XFRM_INC_STATS(net, LINUX_MIB_XFRMINPOLERROR);
				return 0;
			}
			pols[1]->curlft.use_time = get_seconds();
			npols++;
		}
	}
#endif

	if (pol->action == XFRM_POLICY_ALLOW) {
		struct sec_path *sp;
		static struct sec_path dummy;
		struct xfrm_tmpl *tp[XFRM_MAX_DEPTH];
		struct xfrm_tmpl *stp[XFRM_MAX_DEPTH];
		struct xfrm_tmpl **tpp = tp;
		int ti = 0;
		int i, k;

		if ((sp = skb->sp) == NULL)
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
			xfrm_tmpl_sort(stp, tpp, xfrm_nr, family, net);
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
	 * When a policy's bundle is pruned, we dst_free() the XFRM
	 * dst which causes it's ->obsolete field to be set to
	 * DST_OBSOLETE_DEAD.  If an XFRM dst has been pruned like
	 * this, we want to force a new route lookup.
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
	while ((dst = dst->child) && dst->xfrm && dst->dev == dev) {
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

void xfrm_garbage_collect(struct net *net)
{
	flow_cache_flush(net);
}
EXPORT_SYMBOL(xfrm_garbage_collect);

static void xfrm_garbage_collect_deferred(struct net *net)
{
	flow_cache_flush_deferred(net);
}

static void xfrm_init_pmtu(struct dst_entry *dst)
{
	do {
		struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
		u32 pmtu, route_mtu_cached;

		pmtu = dst_mtu(dst->child);
		xdst->child_mtu_cached = pmtu;

		pmtu = xfrm_state_mtu(dst->xfrm, pmtu);

		route_mtu_cached = dst_mtu(xdst->route);
		xdst->route_mtu_cached = route_mtu_cached;

		if (pmtu > route_mtu_cached)
			pmtu = route_mtu_cached;

		dst_metric_set(dst, RTAX_MTU, pmtu);
	} while ((dst = dst->next));
}

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static int xfrm_bundle_ok(struct xfrm_dst *first)
{
	struct dst_entry *dst = &first->u.dst;
	struct xfrm_dst *last;
	u32 mtu;

	if (!dst_check(dst->path, ((struct xfrm_dst *)dst)->path_cookie) ||
	    (dst->dev && !netif_running(dst->dev)))
		return 0;

	if (dst->flags & DST_XFRM_QUEUE)
		return 1;

	last = NULL;

	do {
		struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

		if (dst->xfrm->km.state != XFRM_STATE_VALID)
			return 0;
		if (xdst->xfrm_genid != dst->xfrm->genid)
			return 0;
		if (xdst->num_pols > 0 &&
		    xdst->policy_genid != atomic_read(&xdst->pols[0]->genid))
			return 0;

		mtu = dst_mtu(dst->child);
		if (xdst->child_mtu_cached != mtu) {
			last = xdst;
			xdst->child_mtu_cached = mtu;
		}

		if (!dst_check(xdst->route, xdst->route_cookie))
			return 0;
		mtu = dst_mtu(xdst->route);
		if (xdst->route_mtu_cached != mtu) {
			last = xdst;
			xdst->route_mtu_cached = mtu;
		}

		dst = dst->child;
	} while (dst->xfrm);

	if (likely(!last))
		return 1;

	mtu = last->child_mtu_cached;
	for (;;) {
		dst = &last->u.dst;

		mtu = xfrm_state_mtu(dst->xfrm, mtu);
		if (mtu > last->route_mtu_cached)
			mtu = last->route_mtu_cached;
		dst_metric_set(dst, RTAX_MTU, mtu);

		if (last == first)
			break;

		last = (struct xfrm_dst *)last->u.dst.next;
		last->child_mtu_cached = mtu;
	}

	return 1;
}

static unsigned int xfrm_default_advmss(const struct dst_entry *dst)
{
	return dst_metric_advmss(dst->path);
}

static unsigned int xfrm_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst_mtu(dst->path);
}

static struct neighbour *xfrm_neigh_lookup(const struct dst_entry *dst,
					   struct sk_buff *skb,
					   const void *daddr)
{
	return dst->path->ops->neigh_lookup(dst, skb, daddr);
}

int xfrm_policy_register_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	spin_lock(&xfrm_policy_afinfo_lock);
	if (unlikely(xfrm_policy_afinfo[afinfo->family] != NULL))
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
		if (likely(afinfo->garbage_collect == NULL))
			afinfo->garbage_collect = xfrm_garbage_collect_deferred;
		rcu_assign_pointer(xfrm_policy_afinfo[afinfo->family], afinfo);
	}
	spin_unlock(&xfrm_policy_afinfo_lock);

	return err;
}
EXPORT_SYMBOL(xfrm_policy_register_afinfo);

int xfrm_policy_unregister_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	spin_lock(&xfrm_policy_afinfo_lock);
	if (likely(xfrm_policy_afinfo[afinfo->family] != NULL)) {
		if (unlikely(xfrm_policy_afinfo[afinfo->family] != afinfo))
			err = -EINVAL;
		else
			RCU_INIT_POINTER(xfrm_policy_afinfo[afinfo->family],
					 NULL);
	}
	spin_unlock(&xfrm_policy_afinfo_lock);
	if (!err) {
		struct dst_ops *dst_ops = afinfo->dst_ops;

		synchronize_rcu();

		dst_ops->kmem_cachep = NULL;
		dst_ops->check = NULL;
		dst_ops->negative_advice = NULL;
		dst_ops->link_failure = NULL;
		afinfo->garbage_collect = NULL;
	}
	return err;
}
EXPORT_SYMBOL(xfrm_policy_unregister_afinfo);

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_DOWN:
		xfrm_garbage_collect(dev_net(dev));
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call	= xfrm_dev_event,
};

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
	int dir;

	if (net_eq(net, &init_net))
		xfrm_dst_cache = kmem_cache_create("xfrm_dst_cache",
					   sizeof(struct xfrm_dst),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL);

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
	INIT_WORK(&net->xfrm.policy_hash_work, xfrm_hash_resize);
	INIT_WORK(&net->xfrm.policy_hthresh.work, xfrm_hash_rebuild);
	if (net_eq(net, &init_net))
		register_netdevice_notifier(&xfrm_dev_notifier);
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
}

static int __net_init xfrm_net_init(struct net *net)
{
	int rv;

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
	rv = flow_cache_init(net);
	if (rv < 0)
		goto out;

	/* Initialize the per-net locks here */
	spin_lock_init(&net->xfrm.xfrm_state_lock);
	spin_lock_init(&net->xfrm.xfrm_policy_lock);
	mutex_init(&net->xfrm.xfrm_cfg_mutex);

	return 0;

out:
	xfrm_sysctl_fini(net);
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
	flow_cache_fini(net);
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
	flow_cache_hp_init();
	register_pernet_subsys(&xfrm_net_ops);
	seqcount_init(&xfrm_policy_hash_generation);
	xfrm_input_init();
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
						    u8 dir, u8 type, struct net *net)
{
	struct xfrm_policy *pol, *ret = NULL;
	struct hlist_head *chain;
	u32 priority = ~0U;

	spin_lock_bh(&net->xfrm.xfrm_policy_lock);
	chain = policy_hash_direct(net, &sel->daddr, &sel->saddr, sel->family, dir);
	hlist_for_each_entry(pol, chain, bydst) {
		if (xfrm_migrate_selector_match(sel, &pol->selector) &&
		    pol->type == type) {
			ret = pol;
			priority = ret->priority;
			break;
		}
	}
	chain = &net->xfrm.policy_inexact[dir];
	hlist_for_each_entry(pol, chain, bydst) {
		if ((pol->priority >= priority) && ret)
			break;

		if (xfrm_migrate_selector_match(sel, &pol->selector) &&
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
		if (xfrm_addr_equal(&m[i].old_daddr, &m[i].new_daddr,
				    m[i].old_family) &&
		    xfrm_addr_equal(&m[i].old_saddr, &m[i].new_saddr,
				    m[i].old_family))
			return -EINVAL;
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
		 struct xfrm_kmaddress *k, struct net *net)
{
	int i, err, nx_cur = 0, nx_new = 0;
	struct xfrm_policy *pol = NULL;
	struct xfrm_state *x, *xc;
	struct xfrm_state *x_cur[XFRM_MAX_DEPTH];
	struct xfrm_state *x_new[XFRM_MAX_DEPTH];
	struct xfrm_migrate *mp;

	if ((err = xfrm_migrate_check(m, num_migrate)) < 0)
		goto out;

	/* Stage 1 - find policy */
	if ((pol = xfrm_migrate_policy_find(sel, dir, type, net)) == NULL) {
		err = -ENOENT;
		goto out;
	}

	/* Stage 2 - find and update state(s) */
	for (i = 0, mp = m; i < num_migrate; i++, mp++) {
		if ((x = xfrm_migrate_state_find(mp, net))) {
			x_cur[nx_cur] = x;
			nx_cur++;
			if ((xc = xfrm_state_migrate(x, mp))) {
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
	km_migrate(sel, dir, type, m, num_migrate, k);

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
