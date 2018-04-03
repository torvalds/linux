/*
 *		INETPEER - A storage for permanent information about peers
 *
 *  This source is covered by the GNU GPL, the same as all kernel sources.
 *
 *  Authors:	Andrey V. Savochkin <saw@msu.ru>
 */

#include <linux/cache.h>
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
#include <linux/workqueue.h>
#include <net/ip.h>
#include <net/inetpeer.h>
#include <net/secure_seq.h>

/*
 *  Theory of operations.
 *  We keep one entry for each peer IP address.  The nodes contains long-living
 *  information about the peer which doesn't depend on routes.
 *
 *  Nodes are removed only when reference counter goes to 0.
 *  When it's happened the node may be removed when a sufficient amount of
 *  time has been passed since its last use.  The less-recently-used entry can
 *  also be removed if the pool is overloaded i.e. if the total amount of
 *  entries is greater-or-equal than the threshold.
 *
 *  Node pool is organised as an RB tree.
 *  Such an implementation has been chosen not just for fun.  It's a way to
 *  prevent easy and efficient DoS attacks by creating hash collisions.  A huge
 *  amount of long living nodes in a single hash slot would significantly delay
 *  lookups performed with disabled BHs.
 *
 *  Serialisation issues.
 *  1.  Nodes may appear in the tree only with the pool lock held.
 *  2.  Nodes may disappear from the tree only with the pool lock held
 *      AND reference count being 0.
 *  3.  Global variable peer_total is modified under the pool lock.
 *  4.  struct inet_peer fields modification:
 *		rb_node: pool lock
 *		refcnt: atomically against modifications on other CPU;
 *		   usually under some other lock to prevent node disappearing
 *		daddr: unchangeable
 */

static struct kmem_cache *peer_cachep __ro_after_init;

void inet_peer_base_init(struct inet_peer_base *bp)
{
	bp->rb_root = RB_ROOT;
	seqlock_init(&bp->lock);
	bp->total = 0;
}
EXPORT_SYMBOL_GPL(inet_peer_base_init);

#define PEER_MAX_GC 32

/* Exported for sysctl_net_ipv4.  */
int inet_peer_threshold __read_mostly = 65536 + 128;	/* start to throw entries more
					 * aggressively at this stage */
int inet_peer_minttl __read_mostly = 120 * HZ;	/* TTL under high load: 120 sec */
int inet_peer_maxttl __read_mostly = 10 * 60 * HZ;	/* usual time to live: 10 min */

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
}

/* Called with rcu_read_lock() or base->lock held */
static struct inet_peer *lookup(const struct inetpeer_addr *daddr,
				struct inet_peer_base *base,
				unsigned int seq,
				struct inet_peer *gc_stack[],
				unsigned int *gc_cnt,
				struct rb_node **parent_p,
				struct rb_node ***pp_p)
{
	struct rb_node **pp, *parent, *next;
	struct inet_peer *p;

	pp = &base->rb_root.rb_node;
	parent = NULL;
	while (1) {
		int cmp;

		next = rcu_dereference_raw(*pp);
		if (!next)
			break;
		parent = next;
		p = rb_entry(parent, struct inet_peer, rb_node);
		cmp = inetpeer_addr_cmp(daddr, &p->daddr);
		if (cmp == 0) {
			if (!refcount_inc_not_zero(&p->refcnt))
				break;
			return p;
		}
		if (gc_stack) {
			if (*gc_cnt < PEER_MAX_GC)
				gc_stack[(*gc_cnt)++] = p;
		} else if (unlikely(read_seqretry(&base->lock, seq))) {
			break;
		}
		if (cmp == -1)
			pp = &next->rb_left;
		else
			pp = &next->rb_right;
	}
	*parent_p = parent;
	*pp_p = pp;
	return NULL;
}

static void inetpeer_free_rcu(struct rcu_head *head)
{
	kmem_cache_free(peer_cachep, container_of(head, struct inet_peer, rcu));
}

/* perform garbage collect on all items stacked during a lookup */
static void inet_peer_gc(struct inet_peer_base *base,
			 struct inet_peer *gc_stack[],
			 unsigned int gc_cnt)
{
	struct inet_peer *p;
	__u32 delta, ttl;
	int i;

	if (base->total >= inet_peer_threshold)
		ttl = 0; /* be aggressive */
	else
		ttl = inet_peer_maxttl
				- (inet_peer_maxttl - inet_peer_minttl) / HZ *
					base->total / inet_peer_threshold * HZ;
	for (i = 0; i < gc_cnt; i++) {
		p = gc_stack[i];
		delta = (__u32)jiffies - p->dtime;
		if (delta < ttl || !refcount_dec_if_one(&p->refcnt))
			gc_stack[i] = NULL;
	}
	for (i = 0; i < gc_cnt; i++) {
		p = gc_stack[i];
		if (p) {
			rb_erase(&p->rb_node, &base->rb_root);
			base->total--;
			call_rcu(&p->rcu, inetpeer_free_rcu);
		}
	}
}

struct inet_peer *inet_getpeer(struct inet_peer_base *base,
			       const struct inetpeer_addr *daddr,
			       int create)
{
	struct inet_peer *p, *gc_stack[PEER_MAX_GC];
	struct rb_node **pp, *parent;
	unsigned int gc_cnt, seq;
	int invalidated;

	/* Attempt a lockless lookup first.
	 * Because of a concurrent writer, we might not find an existing entry.
	 */
	rcu_read_lock();
	seq = read_seqbegin(&base->lock);
	p = lookup(daddr, base, seq, NULL, &gc_cnt, &parent, &pp);
	invalidated = read_seqretry(&base->lock, seq);
	rcu_read_unlock();

	if (p)
		return p;

	/* If no writer did a change during our lookup, we can return early. */
	if (!create && !invalidated)
		return NULL;

	/* retry an exact lookup, taking the lock before.
	 * At least, nodes should be hot in our cache.
	 */
	parent = NULL;
	write_seqlock_bh(&base->lock);

	gc_cnt = 0;
	p = lookup(daddr, base, seq, gc_stack, &gc_cnt, &parent, &pp);
	if (!p && create) {
		p = kmem_cache_alloc(peer_cachep, GFP_ATOMIC);
		if (p) {
			p->daddr = *daddr;
			refcount_set(&p->refcnt, 2);
			atomic_set(&p->rid, 0);
			p->metrics[RTAX_LOCK-1] = INETPEER_METRICS_NEW;
			p->rate_tokens = 0;
			/* 60*HZ is arbitrary, but chosen enough high so that the first
			 * calculation of tokens is at its maximum.
			 */
			p->rate_last = jiffies - 60*HZ;

			rb_link_node(&p->rb_node, parent, pp);
			rb_insert_color(&p->rb_node, &base->rb_root);
			base->total++;
		}
	}
	if (gc_cnt)
		inet_peer_gc(base, gc_stack, gc_cnt);
	write_sequnlock_bh(&base->lock);

	return p;
}
EXPORT_SYMBOL_GPL(inet_getpeer);

void inet_putpeer(struct inet_peer *p)
{
	p->dtime = (__u32)jiffies;

	if (refcount_dec_and_test(&p->refcnt))
		call_rcu(&p->rcu, inetpeer_free_rcu);
}
EXPORT_SYMBOL_GPL(inet_putpeer);

/*
 *	Check transmit rate limitation for given message.
 *	The rate information is held in the inet_peer entries now.
 *	This function is generic and could be used for other purposes
 *	too. It uses a Token bucket filter as suggested by Alexey Kuznetsov.
 *
 *	Note that the same inet_peer fields are modified by functions in
 *	route.c too, but these work for packet destinations while xrlim_allow
 *	works for icmp destinations. This means the rate limiting information
 *	for one "ip object" is shared - and these ICMPs are twice limited:
 *	by source and by destination.
 *
 *	RFC 1812: 4.3.2.8 SHOULD be able to limit error message rate
 *			  SHOULD allow setting of rate limits
 *
 * 	Shared between ICMPv4 and ICMPv6.
 */
#define XRLIM_BURST_FACTOR 6
bool inet_peer_xrlim_allow(struct inet_peer *peer, int timeout)
{
	unsigned long now, token;
	bool rc = false;

	if (!peer)
		return true;

	token = peer->rate_tokens;
	now = jiffies;
	token += now - peer->rate_last;
	peer->rate_last = now;
	if (token > XRLIM_BURST_FACTOR * timeout)
		token = XRLIM_BURST_FACTOR * timeout;
	if (token >= timeout) {
		token -= timeout;
		rc = true;
	}
	peer->rate_tokens = token;
	return rc;
}
EXPORT_SYMBOL(inet_peer_xrlim_allow);

void inetpeer_invalidate_tree(struct inet_peer_base *base)
{
	struct rb_node *p = rb_first(&base->rb_root);

	while (p) {
		struct inet_peer *peer = rb_entry(p, struct inet_peer, rb_node);

		p = rb_next(p);
		rb_erase(&peer->rb_node, &base->rb_root);
		inet_putpeer(peer);
		cond_resched();
	}

	base->total = 0;
}
EXPORT_SYMBOL(inetpeer_invalidate_tree);
