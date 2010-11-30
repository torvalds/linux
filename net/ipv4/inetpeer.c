/*
 *		INETPEER - A storage for permanent information about peers
 *
 *  This source is covered by the GNU GPL, the same as all kernel sources.
 *
 *  Authors:	Andrey V. Savochkin <saw@msu.ru>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <net/ip.h>
#include <net/inetpeer.h>

/*
 *  Theory of operations.
 *  We keep one entry for each peer IP address.  The nodes contains long-living
 *  information about the peer which doesn't depend on routes.
 *  At this moment this information consists only of ID field for the next
 *  outgoing IP packet.  This field is incremented with each packet as encoded
 *  in inet_getid() function (include/net/inetpeer.h).
 *  At the moment of writing this notes identifier of IP packets is generated
 *  to be unpredictable using this code only for packets subjected
 *  (actually or potentially) to defragmentation.  I.e. DF packets less than
 *  PMTU in size uses a constant ID and do not use this code (see
 *  ip_select_ident() in include/net/ip.h).
 *
 *  Route cache entries hold references to our nodes.
 *  New cache entries get references via lookup by destination IP address in
 *  the avl tree.  The reference is grabbed only when it's needed i.e. only
 *  when we try to output IP packet which needs an unpredictable ID (see
 *  __ip_select_ident() in net/ipv4/route.c).
 *  Nodes are removed only when reference counter goes to 0.
 *  When it's happened the node may be removed when a sufficient amount of
 *  time has been passed since its last use.  The less-recently-used entry can
 *  also be removed if the pool is overloaded i.e. if the total amount of
 *  entries is greater-or-equal than the threshold.
 *
 *  Node pool is organised as an AVL tree.
 *  Such an implementation has been chosen not just for fun.  It's a way to
 *  prevent easy and efficient DoS attacks by creating hash collisions.  A huge
 *  amount of long living nodes in a single hash slot would significantly delay
 *  lookups performed with disabled BHs.
 *
 *  Serialisation issues.
 *  1.  Nodes may appear in the tree only with the pool lock held.
 *  2.  Nodes may disappear from the tree only with the pool lock held
 *      AND reference count being 0.
 *  3.  Nodes appears and disappears from unused node list only under
 *      "inet_peer_unused_lock".
 *  4.  Global variable peer_total is modified under the pool lock.
 *  5.  struct inet_peer fields modification:
 *		avl_left, avl_right, avl_parent, avl_height: pool lock
 *		unused: unused node list lock
 *		refcnt: atomically against modifications on other CPU;
 *		   usually under some other lock to prevent node disappearing
 *		dtime: unused node list lock
 *		daddr: unchangeable
 *		ip_id_count: atomic value (no lock needed)
 */

static struct kmem_cache *peer_cachep __read_mostly;

#define node_height(x) x->avl_height

#define peer_avl_empty ((struct inet_peer *)&peer_fake_node)
#define peer_avl_empty_rcu ((struct inet_peer __rcu __force *)&peer_fake_node)
static const struct inet_peer peer_fake_node = {
	.avl_left	= peer_avl_empty_rcu,
	.avl_right	= peer_avl_empty_rcu,
	.avl_height	= 0
};

static struct inet_peer_base {
	struct inet_peer __rcu *root;
	spinlock_t	lock;
	int		total;
} v4_peers = {
	.root		= peer_avl_empty_rcu,
	.lock		= __SPIN_LOCK_UNLOCKED(v4_peers.lock),
	.total		= 0,
};
#define PEER_MAXDEPTH 40 /* sufficient for about 2^27 nodes */

/* Exported for sysctl_net_ipv4.  */
int inet_peer_threshold __read_mostly = 65536 + 128;	/* start to throw entries more
					 * aggressively at this stage */
int inet_peer_minttl __read_mostly = 120 * HZ;	/* TTL under high load: 120 sec */
int inet_peer_maxttl __read_mostly = 10 * 60 * HZ;	/* usual time to live: 10 min */
int inet_peer_gc_mintime __read_mostly = 10 * HZ;
int inet_peer_gc_maxtime __read_mostly = 120 * HZ;

static struct {
	struct list_head	list;
	spinlock_t		lock;
} unused_peers = {
	.list			= LIST_HEAD_INIT(unused_peers.list),
	.lock			= __SPIN_LOCK_UNLOCKED(unused_peers.lock),
};

static void peer_check_expire(unsigned long dummy);
static DEFINE_TIMER(peer_periodic_timer, peer_check_expire, 0, 0);


/* Called from ip_output.c:ip_init  */
void __init inet_initpeers(void)
{
	struct sysinfo si;

	/* Use the straight interface to information about memory. */
	si_meminfo(&si);
	/* The values below were suggested by Alexey Kuznetsov
	 * <kuznet@ms2.inr.ac.ru>.  I don't have any opinion about the values
	 * myself.  --SAW
	 */
	if (si.totalram <= (32768*1024)/PAGE_SIZE)
		inet_peer_threshold >>= 1; /* max pool size about 1MB on IA32 */
	if (si.totalram <= (16384*1024)/PAGE_SIZE)
		inet_peer_threshold >>= 1; /* about 512KB */
	if (si.totalram <= (8192*1024)/PAGE_SIZE)
		inet_peer_threshold >>= 2; /* about 128KB */

	peer_cachep = kmem_cache_create("inet_peer_cache",
			sizeof(struct inet_peer),
			0, SLAB_HWCACHE_ALIGN | SLAB_PANIC,
			NULL);

	/* All the timers, started at system startup tend
	   to synchronize. Perturb it a bit.
	 */
	peer_periodic_timer.expires = jiffies
		+ net_random() % inet_peer_gc_maxtime
		+ inet_peer_gc_maxtime;
	add_timer(&peer_periodic_timer);
}

/* Called with or without local BH being disabled. */
static void unlink_from_unused(struct inet_peer *p)
{
	if (!list_empty(&p->unused)) {
		spin_lock_bh(&unused_peers.lock);
		list_del_init(&p->unused);
		spin_unlock_bh(&unused_peers.lock);
	}
}

static int addr_compare(const inet_peer_address_t *a,
			const inet_peer_address_t *b)
{
	int i, n = (a->family == AF_INET ? 1 : 4);

	for (i = 0; i < n; i++) {
		if (a->a6[i] == b->a6[i])
			continue;
		if (a->a6[i] < b->a6[i])
			return -1;
		return 1;
	}

	return 0;
}

/*
 * Called with local BH disabled and the pool lock held.
 */
#define lookup(_daddr, _stack, _base)				\
({								\
	struct inet_peer *u;					\
	struct inet_peer __rcu **v;				\
								\
	stackptr = _stack;					\
	*stackptr++ = &_base->root;				\
	for (u = rcu_dereference_protected(_base->root,		\
			lockdep_is_held(&_base->lock));		\
	     u != peer_avl_empty; ) {				\
		int cmp = addr_compare(_daddr, &u->daddr);	\
		if (cmp == 0)					\
			break;					\
		if (cmp == -1)					\
			v = &u->avl_left;			\
		else						\
			v = &u->avl_right;			\
		*stackptr++ = v;				\
		u = rcu_dereference_protected(*v,		\
			lockdep_is_held(&_base->lock));		\
	}							\
	u;							\
})

/*
 * Called with rcu_read_lock_bh()
 * Because we hold no lock against a writer, its quite possible we fall
 * in an endless loop.
 * But every pointer we follow is guaranteed to be valid thanks to RCU.
 * We exit from this function if number of links exceeds PEER_MAXDEPTH
 */
static struct inet_peer *lookup_rcu_bh(const inet_peer_address_t *daddr,
				       struct inet_peer_base *base)
{
	struct inet_peer *u = rcu_dereference_bh(base->root);
	int count = 0;

	while (u != peer_avl_empty) {
		int cmp = addr_compare(daddr, &u->daddr);
		if (cmp == 0) {
			/* Before taking a reference, check if this entry was
			 * deleted, unlink_from_pool() sets refcnt=-1 to make
			 * distinction between an unused entry (refcnt=0) and
			 * a freed one.
			 */
			if (unlikely(!atomic_add_unless(&u->refcnt, 1, -1)))
				u = NULL;
			return u;
		}
		if (cmp == -1)
			u = rcu_dereference_bh(u->avl_left);
		else
			u = rcu_dereference_bh(u->avl_right);
		if (unlikely(++count == PEER_MAXDEPTH))
			break;
	}
	return NULL;
}

/* Called with local BH disabled and the pool lock held. */
#define lookup_rightempty(start, base)				\
({								\
	struct inet_peer *u;					\
	struct inet_peer __rcu **v;				\
	*stackptr++ = &start->avl_left;				\
	v = &start->avl_left;					\
	for (u = rcu_dereference_protected(*v,			\
			lockdep_is_held(&base->lock));		\
	     u->avl_right != peer_avl_empty_rcu; ) {		\
		v = &u->avl_right;				\
		*stackptr++ = v;				\
		u = rcu_dereference_protected(*v,		\
			lockdep_is_held(&base->lock));		\
	}							\
	u;							\
})

/* Called with local BH disabled and the pool lock held.
 * Variable names are the proof of operation correctness.
 * Look into mm/map_avl.c for more detail description of the ideas.
 */
static void peer_avl_rebalance(struct inet_peer __rcu **stack[],
			       struct inet_peer __rcu ***stackend,
			       struct inet_peer_base *base)
{
	struct inet_peer __rcu **nodep;
	struct inet_peer *node, *l, *r;
	int lh, rh;

	while (stackend > stack) {
		nodep = *--stackend;
		node = rcu_dereference_protected(*nodep,
				lockdep_is_held(&base->lock));
		l = rcu_dereference_protected(node->avl_left,
				lockdep_is_held(&base->lock));
		r = rcu_dereference_protected(node->avl_right,
				lockdep_is_held(&base->lock));
		lh = node_height(l);
		rh = node_height(r);
		if (lh > rh + 1) { /* l: RH+2 */
			struct inet_peer *ll, *lr, *lrl, *lrr;
			int lrh;
			ll = rcu_dereference_protected(l->avl_left,
				lockdep_is_held(&base->lock));
			lr = rcu_dereference_protected(l->avl_right,
				lockdep_is_held(&base->lock));
			lrh = node_height(lr);
			if (lrh <= node_height(ll)) {	/* ll: RH+1 */
				RCU_INIT_POINTER(node->avl_left, lr);	/* lr: RH or RH+1 */
				RCU_INIT_POINTER(node->avl_right, r);	/* r: RH */
				node->avl_height = lrh + 1; /* RH+1 or RH+2 */
				RCU_INIT_POINTER(l->avl_left, ll);       /* ll: RH+1 */
				RCU_INIT_POINTER(l->avl_right, node);	/* node: RH+1 or RH+2 */
				l->avl_height = node->avl_height + 1;
				RCU_INIT_POINTER(*nodep, l);
			} else { /* ll: RH, lr: RH+1 */
				lrl = rcu_dereference_protected(lr->avl_left,
					lockdep_is_held(&base->lock));	/* lrl: RH or RH-1 */
				lrr = rcu_dereference_protected(lr->avl_right,
					lockdep_is_held(&base->lock));	/* lrr: RH or RH-1 */
				RCU_INIT_POINTER(node->avl_left, lrr);	/* lrr: RH or RH-1 */
				RCU_INIT_POINTER(node->avl_right, r);	/* r: RH */
				node->avl_height = rh + 1; /* node: RH+1 */
				RCU_INIT_POINTER(l->avl_left, ll);	/* ll: RH */
				RCU_INIT_POINTER(l->avl_right, lrl);	/* lrl: RH or RH-1 */
				l->avl_height = rh + 1;	/* l: RH+1 */
				RCU_INIT_POINTER(lr->avl_left, l);	/* l: RH+1 */
				RCU_INIT_POINTER(lr->avl_right, node);	/* node: RH+1 */
				lr->avl_height = rh + 2;
				RCU_INIT_POINTER(*nodep, lr);
			}
		} else if (rh > lh + 1) { /* r: LH+2 */
			struct inet_peer *rr, *rl, *rlr, *rll;
			int rlh;
			rr = rcu_dereference_protected(r->avl_right,
				lockdep_is_held(&base->lock));
			rl = rcu_dereference_protected(r->avl_left,
				lockdep_is_held(&base->lock));
			rlh = node_height(rl);
			if (rlh <= node_height(rr)) {	/* rr: LH+1 */
				RCU_INIT_POINTER(node->avl_right, rl);	/* rl: LH or LH+1 */
				RCU_INIT_POINTER(node->avl_left, l);	/* l: LH */
				node->avl_height = rlh + 1; /* LH+1 or LH+2 */
				RCU_INIT_POINTER(r->avl_right, rr);	/* rr: LH+1 */
				RCU_INIT_POINTER(r->avl_left, node);	/* node: LH+1 or LH+2 */
				r->avl_height = node->avl_height + 1;
				RCU_INIT_POINTER(*nodep, r);
			} else { /* rr: RH, rl: RH+1 */
				rlr = rcu_dereference_protected(rl->avl_right,
					lockdep_is_held(&base->lock));	/* rlr: LH or LH-1 */
				rll = rcu_dereference_protected(rl->avl_left,
					lockdep_is_held(&base->lock));	/* rll: LH or LH-1 */
				RCU_INIT_POINTER(node->avl_right, rll);	/* rll: LH or LH-1 */
				RCU_INIT_POINTER(node->avl_left, l);	/* l: LH */
				node->avl_height = lh + 1; /* node: LH+1 */
				RCU_INIT_POINTER(r->avl_right, rr);	/* rr: LH */
				RCU_INIT_POINTER(r->avl_left, rlr);	/* rlr: LH or LH-1 */
				r->avl_height = lh + 1;	/* r: LH+1 */
				RCU_INIT_POINTER(rl->avl_right, r);	/* r: LH+1 */
				RCU_INIT_POINTER(rl->avl_left, node);	/* node: LH+1 */
				rl->avl_height = lh + 2;
				RCU_INIT_POINTER(*nodep, rl);
			}
		} else {
			node->avl_height = (lh > rh ? lh : rh) + 1;
		}
	}
}

/* Called with local BH disabled and the pool lock held. */
#define link_to_pool(n, base)					\
do {								\
	n->avl_height = 1;					\
	n->avl_left = peer_avl_empty_rcu;			\
	n->avl_right = peer_avl_empty_rcu;			\
	/* lockless readers can catch us now */			\
	rcu_assign_pointer(**--stackptr, n);			\
	peer_avl_rebalance(stack, stackptr, base);		\
} while (0)

static void inetpeer_free_rcu(struct rcu_head *head)
{
	kmem_cache_free(peer_cachep, container_of(head, struct inet_peer, rcu));
}

/* May be called with local BH enabled. */
static void unlink_from_pool(struct inet_peer *p, struct inet_peer_base *base)
{
	int do_free;

	do_free = 0;

	spin_lock_bh(&base->lock);
	/* Check the reference counter.  It was artificially incremented by 1
	 * in cleanup() function to prevent sudden disappearing.  If we can
	 * atomically (because of lockless readers) take this last reference,
	 * it's safe to remove the node and free it later.
	 * We use refcnt=-1 to alert lockless readers this entry is deleted.
	 */
	if (atomic_cmpxchg(&p->refcnt, 1, -1) == 1) {
		struct inet_peer __rcu **stack[PEER_MAXDEPTH];
		struct inet_peer __rcu ***stackptr, ***delp;
		if (lookup(&p->daddr, stack, base) != p)
			BUG();
		delp = stackptr - 1; /* *delp[0] == p */
		if (p->avl_left == peer_avl_empty_rcu) {
			*delp[0] = p->avl_right;
			--stackptr;
		} else {
			/* look for a node to insert instead of p */
			struct inet_peer *t;
			t = lookup_rightempty(p, base);
			BUG_ON(rcu_dereference_protected(*stackptr[-1],
					lockdep_is_held(&base->lock)) != t);
			**--stackptr = t->avl_left;
			/* t is removed, t->daddr > x->daddr for any
			 * x in p->avl_left subtree.
			 * Put t in the old place of p. */
			RCU_INIT_POINTER(*delp[0], t);
			t->avl_left = p->avl_left;
			t->avl_right = p->avl_right;
			t->avl_height = p->avl_height;
			BUG_ON(delp[1] != &p->avl_left);
			delp[1] = &t->avl_left; /* was &p->avl_left */
		}
		peer_avl_rebalance(stack, stackptr, base);
		base->total--;
		do_free = 1;
	}
	spin_unlock_bh(&base->lock);

	if (do_free)
		call_rcu_bh(&p->rcu, inetpeer_free_rcu);
	else
		/* The node is used again.  Decrease the reference counter
		 * back.  The loop "cleanup -> unlink_from_unused
		 *   -> unlink_from_pool -> putpeer -> link_to_unused
		 *   -> cleanup (for the same node)"
		 * doesn't really exist because the entry will have a
		 * recent deletion time and will not be cleaned again soon.
		 */
		inet_putpeer(p);
}

static struct inet_peer_base *peer_to_base(struct inet_peer *p)
{
	return &v4_peers;
}

/* May be called with local BH enabled. */
static int cleanup_once(unsigned long ttl)
{
	struct inet_peer *p = NULL;

	/* Remove the first entry from the list of unused nodes. */
	spin_lock_bh(&unused_peers.lock);
	if (!list_empty(&unused_peers.list)) {
		__u32 delta;

		p = list_first_entry(&unused_peers.list, struct inet_peer, unused);
		delta = (__u32)jiffies - p->dtime;

		if (delta < ttl) {
			/* Do not prune fresh entries. */
			spin_unlock_bh(&unused_peers.lock);
			return -1;
		}

		list_del_init(&p->unused);

		/* Grab an extra reference to prevent node disappearing
		 * before unlink_from_pool() call. */
		atomic_inc(&p->refcnt);
	}
	spin_unlock_bh(&unused_peers.lock);

	if (p == NULL)
		/* It means that the total number of USED entries has
		 * grown over inet_peer_threshold.  It shouldn't really
		 * happen because of entry limits in route cache. */
		return -1;

	unlink_from_pool(p, peer_to_base(p));
	return 0;
}

static struct inet_peer_base *family_to_base(int family)
{
	return &v4_peers;
}

/* Called with or without local BH being disabled. */
struct inet_peer *inet_getpeer(inet_peer_address_t *daddr, int create)
{
	struct inet_peer __rcu **stack[PEER_MAXDEPTH], ***stackptr;
	struct inet_peer_base *base = family_to_base(AF_INET);
	struct inet_peer *p;

	/* Look up for the address quickly, lockless.
	 * Because of a concurrent writer, we might not find an existing entry.
	 */
	rcu_read_lock_bh();
	p = lookup_rcu_bh(daddr, base);
	rcu_read_unlock_bh();

	if (p) {
		/* The existing node has been found.
		 * Remove the entry from unused list if it was there.
		 */
		unlink_from_unused(p);
		return p;
	}

	/* retry an exact lookup, taking the lock before.
	 * At least, nodes should be hot in our cache.
	 */
	spin_lock_bh(&base->lock);
	p = lookup(daddr, stack, base);
	if (p != peer_avl_empty) {
		atomic_inc(&p->refcnt);
		spin_unlock_bh(&base->lock);
		/* Remove the entry from unused list if it was there. */
		unlink_from_unused(p);
		return p;
	}
	p = create ? kmem_cache_alloc(peer_cachep, GFP_ATOMIC) : NULL;
	if (p) {
		p->daddr = *daddr;
		atomic_set(&p->refcnt, 1);
		atomic_set(&p->rid, 0);
		atomic_set(&p->ip_id_count, secure_ip_id(daddr->a4));
		p->tcp_ts_stamp = 0;
		INIT_LIST_HEAD(&p->unused);


		/* Link the node. */
		link_to_pool(p, base);
		base->total++;
	}
	spin_unlock_bh(&base->lock);

	if (base->total >= inet_peer_threshold)
		/* Remove one less-recently-used entry. */
		cleanup_once(0);

	return p;
}

static int compute_total(void)
{
	return v4_peers.total;
}

/* Called with local BH disabled. */
static void peer_check_expire(unsigned long dummy)
{
	unsigned long now = jiffies;
	int ttl, total;

	total = compute_total();
	if (total >= inet_peer_threshold)
		ttl = inet_peer_minttl;
	else
		ttl = inet_peer_maxttl
				- (inet_peer_maxttl - inet_peer_minttl) / HZ *
					total / inet_peer_threshold * HZ;
	while (!cleanup_once(ttl)) {
		if (jiffies != now)
			break;
	}

	/* Trigger the timer after inet_peer_gc_mintime .. inet_peer_gc_maxtime
	 * interval depending on the total number of entries (more entries,
	 * less interval). */
	total = compute_total();
	if (total >= inet_peer_threshold)
		peer_periodic_timer.expires = jiffies + inet_peer_gc_mintime;
	else
		peer_periodic_timer.expires = jiffies
			+ inet_peer_gc_maxtime
			- (inet_peer_gc_maxtime - inet_peer_gc_mintime) / HZ *
				total / inet_peer_threshold * HZ;
	add_timer(&peer_periodic_timer);
}

void inet_putpeer(struct inet_peer *p)
{
	local_bh_disable();

	if (atomic_dec_and_lock(&p->refcnt, &unused_peers.lock)) {
		list_add_tail(&p->unused, &unused_peers.list);
		p->dtime = (__u32)jiffies;
		spin_unlock(&unused_peers.lock);
	}

	local_bh_enable();
}
