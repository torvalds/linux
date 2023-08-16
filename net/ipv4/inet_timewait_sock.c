// SPDX-License-Identifier: GPL-2.0-only
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic TIME_WAIT sockets functions
 *
 *		From code orinally in TCP
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/inet_hashtables.h>
#include <net/inet_timewait_sock.h>
#include <net/ip.h>


/**
 *	inet_twsk_bind_unhash - unhash a timewait socket from bind hash
 *	@tw: timewait socket
 *	@hashinfo: hashinfo pointer
 *
 *	unhash a timewait socket from bind hash, if hashed.
 *	bind hash lock must be held by caller.
 *	Returns 1 if caller should call inet_twsk_put() after lock release.
 */
void inet_twsk_bind_unhash(struct inet_timewait_sock *tw,
			  struct inet_hashinfo *hashinfo)
{
	struct inet_bind2_bucket *tb2 = tw->tw_tb2;
	struct inet_bind_bucket *tb = tw->tw_tb;

	if (!tb)
		return;

	__hlist_del(&tw->tw_bind_node);
	tw->tw_tb = NULL;
	inet_bind_bucket_destroy(hashinfo->bind_bucket_cachep, tb);

	__hlist_del(&tw->tw_bind2_node);
	tw->tw_tb2 = NULL;
	inet_bind2_bucket_destroy(hashinfo->bind2_bucket_cachep, tb2);

	__sock_put((struct sock *)tw);
}

/* Must be called with locally disabled BHs. */
static void inet_twsk_kill(struct inet_timewait_sock *tw)
{
	struct inet_hashinfo *hashinfo = tw->tw_dr->hashinfo;
	spinlock_t *lock = inet_ehash_lockp(hashinfo, tw->tw_hash);
	struct inet_bind_hashbucket *bhead, *bhead2;

	spin_lock(lock);
	sk_nulls_del_node_init_rcu((struct sock *)tw);
	spin_unlock(lock);

	/* Disassociate with bind bucket. */
	bhead = &hashinfo->bhash[inet_bhashfn(twsk_net(tw), tw->tw_num,
			hashinfo->bhash_size)];
	bhead2 = inet_bhashfn_portaddr(hashinfo, (struct sock *)tw,
				       twsk_net(tw), tw->tw_num);

	spin_lock(&bhead->lock);
	spin_lock(&bhead2->lock);
	inet_twsk_bind_unhash(tw, hashinfo);
	spin_unlock(&bhead2->lock);
	spin_unlock(&bhead->lock);

	refcount_dec(&tw->tw_dr->tw_refcount);
	inet_twsk_put(tw);
}

void inet_twsk_free(struct inet_timewait_sock *tw)
{
	struct module *owner = tw->tw_prot->owner;
	twsk_destructor((struct sock *)tw);
	kmem_cache_free(tw->tw_prot->twsk_prot->twsk_slab, tw);
	module_put(owner);
}

void inet_twsk_put(struct inet_timewait_sock *tw)
{
	if (refcount_dec_and_test(&tw->tw_refcnt))
		inet_twsk_free(tw);
}
EXPORT_SYMBOL_GPL(inet_twsk_put);

static void inet_twsk_add_node_rcu(struct inet_timewait_sock *tw,
				   struct hlist_nulls_head *list)
{
	hlist_nulls_add_head_rcu(&tw->tw_node, list);
}

static void inet_twsk_add_bind_node(struct inet_timewait_sock *tw,
				    struct hlist_head *list)
{
	hlist_add_head(&tw->tw_bind_node, list);
}

static void inet_twsk_add_bind2_node(struct inet_timewait_sock *tw,
				     struct hlist_head *list)
{
	hlist_add_head(&tw->tw_bind2_node, list);
}

/*
 * Enter the time wait state. This is called with locally disabled BH.
 * Essentially we whip up a timewait bucket, copy the relevant info into it
 * from the SK, and mess with hash chains and list linkage.
 */
void inet_twsk_hashdance(struct inet_timewait_sock *tw, struct sock *sk,
			   struct inet_hashinfo *hashinfo)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_ehash_bucket *ehead = inet_ehash_bucket(hashinfo, sk->sk_hash);
	spinlock_t *lock = inet_ehash_lockp(hashinfo, sk->sk_hash);
	struct inet_bind_hashbucket *bhead, *bhead2;

	/* Step 1: Put TW into bind hash. Original socket stays there too.
	   Note, that any socket with inet->num != 0 MUST be bound in
	   binding cache, even if it is closed.
	 */
	bhead = &hashinfo->bhash[inet_bhashfn(twsk_net(tw), inet->inet_num,
			hashinfo->bhash_size)];
	bhead2 = inet_bhashfn_portaddr(hashinfo, sk, twsk_net(tw), inet->inet_num);

	spin_lock(&bhead->lock);
	spin_lock(&bhead2->lock);

	tw->tw_tb = icsk->icsk_bind_hash;
	WARN_ON(!icsk->icsk_bind_hash);
	inet_twsk_add_bind_node(tw, &tw->tw_tb->owners);

	tw->tw_tb2 = icsk->icsk_bind2_hash;
	WARN_ON(!icsk->icsk_bind2_hash);
	inet_twsk_add_bind2_node(tw, &tw->tw_tb2->deathrow);

	spin_unlock(&bhead2->lock);
	spin_unlock(&bhead->lock);

	spin_lock(lock);

	inet_twsk_add_node_rcu(tw, &ehead->chain);

	/* Step 3: Remove SK from hash chain */
	if (__sk_nulls_del_node_init_rcu(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);

	spin_unlock(lock);

	/* tw_refcnt is set to 3 because we have :
	 * - one reference for bhash chain.
	 * - one reference for ehash chain.
	 * - one reference for timer.
	 * We can use atomic_set() because prior spin_lock()/spin_unlock()
	 * committed into memory all tw fields.
	 * Also note that after this point, we lost our implicit reference
	 * so we are not allowed to use tw anymore.
	 */
	refcount_set(&tw->tw_refcnt, 3);
}
EXPORT_SYMBOL_GPL(inet_twsk_hashdance);

static void tw_timer_handler(struct timer_list *t)
{
	struct inet_timewait_sock *tw = from_timer(tw, t, tw_timer);

	inet_twsk_kill(tw);
}

struct inet_timewait_sock *inet_twsk_alloc(const struct sock *sk,
					   struct inet_timewait_death_row *dr,
					   const int state)
{
	struct inet_timewait_sock *tw;

	if (refcount_read(&dr->tw_refcount) - 1 >=
	    READ_ONCE(dr->sysctl_max_tw_buckets))
		return NULL;

	tw = kmem_cache_alloc(sk->sk_prot_creator->twsk_prot->twsk_slab,
			      GFP_ATOMIC);
	if (tw) {
		const struct inet_sock *inet = inet_sk(sk);

		tw->tw_dr	    = dr;
		/* Give us an identity. */
		tw->tw_daddr	    = inet->inet_daddr;
		tw->tw_rcv_saddr    = inet->inet_rcv_saddr;
		tw->tw_bound_dev_if = sk->sk_bound_dev_if;
		tw->tw_tos	    = inet->tos;
		tw->tw_num	    = inet->inet_num;
		tw->tw_state	    = TCP_TIME_WAIT;
		tw->tw_substate	    = state;
		tw->tw_sport	    = inet->inet_sport;
		tw->tw_dport	    = inet->inet_dport;
		tw->tw_family	    = sk->sk_family;
		tw->tw_reuse	    = sk->sk_reuse;
		tw->tw_reuseport    = sk->sk_reuseport;
		tw->tw_hash	    = sk->sk_hash;
		tw->tw_ipv6only	    = 0;
		tw->tw_transparent  = inet_test_bit(TRANSPARENT, sk);
		tw->tw_prot	    = sk->sk_prot_creator;
		atomic64_set(&tw->tw_cookie, atomic64_read(&sk->sk_cookie));
		twsk_net_set(tw, sock_net(sk));
		timer_setup(&tw->tw_timer, tw_timer_handler, TIMER_PINNED);
		/*
		 * Because we use RCU lookups, we should not set tw_refcnt
		 * to a non null value before everything is setup for this
		 * timewait socket.
		 */
		refcount_set(&tw->tw_refcnt, 0);

		__module_get(tw->tw_prot->owner);
	}

	return tw;
}
EXPORT_SYMBOL_GPL(inet_twsk_alloc);

/* These are always called from BH context.  See callers in
 * tcp_input.c to verify this.
 */

/* This is for handling early-kills of TIME_WAIT sockets.
 * Warning : consume reference.
 * Caller should not access tw anymore.
 */
void inet_twsk_deschedule_put(struct inet_timewait_sock *tw)
{
	if (del_timer_sync(&tw->tw_timer))
		inet_twsk_kill(tw);
	inet_twsk_put(tw);
}
EXPORT_SYMBOL(inet_twsk_deschedule_put);

void __inet_twsk_schedule(struct inet_timewait_sock *tw, int timeo, bool rearm)
{
	/* timeout := RTO * 3.5
	 *
	 * 3.5 = 1+2+0.5 to wait for two retransmits.
	 *
	 * RATIONALE: if FIN arrived and we entered TIME-WAIT state,
	 * our ACK acking that FIN can be lost. If N subsequent retransmitted
	 * FINs (or previous seqments) are lost (probability of such event
	 * is p^(N+1), where p is probability to lose single packet and
	 * time to detect the loss is about RTO*(2^N - 1) with exponential
	 * backoff). Normal timewait length is calculated so, that we
	 * waited at least for one retransmitted FIN (maximal RTO is 120sec).
	 * [ BTW Linux. following BSD, violates this requirement waiting
	 *   only for 60sec, we should wait at least for 240 secs.
	 *   Well, 240 consumes too much of resources 8)
	 * ]
	 * This interval is not reduced to catch old duplicate and
	 * responces to our wandering segments living for two MSLs.
	 * However, if we use PAWS to detect
	 * old duplicates, we can reduce the interval to bounds required
	 * by RTO, rather than MSL. So, if peer understands PAWS, we
	 * kill tw bucket after 3.5*RTO (it is important that this number
	 * is greater than TS tick!) and detect old duplicates with help
	 * of PAWS.
	 */

	if (!rearm) {
		bool kill = timeo <= 4*HZ;

		__NET_INC_STATS(twsk_net(tw), kill ? LINUX_MIB_TIMEWAITKILLED :
						     LINUX_MIB_TIMEWAITED);
		BUG_ON(mod_timer(&tw->tw_timer, jiffies + timeo));
		refcount_inc(&tw->tw_dr->tw_refcount);
	} else {
		mod_timer_pending(&tw->tw_timer, jiffies + timeo);
	}
}
EXPORT_SYMBOL_GPL(__inet_twsk_schedule);

void inet_twsk_purge(struct inet_hashinfo *hashinfo, int family)
{
	struct inet_timewait_sock *tw;
	struct sock *sk;
	struct hlist_nulls_node *node;
	unsigned int slot;

	for (slot = 0; slot <= hashinfo->ehash_mask; slot++) {
		struct inet_ehash_bucket *head = &hashinfo->ehash[slot];
restart_rcu:
		cond_resched();
		rcu_read_lock();
restart:
		sk_nulls_for_each_rcu(sk, node, &head->chain) {
			if (sk->sk_state != TCP_TIME_WAIT) {
				/* A kernel listener socket might not hold refcnt for net,
				 * so reqsk_timer_handler() could be fired after net is
				 * freed.  Userspace listener and reqsk never exist here.
				 */
				if (unlikely(sk->sk_state == TCP_NEW_SYN_RECV &&
					     hashinfo->pernet)) {
					struct request_sock *req = inet_reqsk(sk);

					inet_csk_reqsk_queue_drop_and_put(req->rsk_listener, req);
				}

				continue;
			}

			tw = inet_twsk(sk);
			if ((tw->tw_family != family) ||
				refcount_read(&twsk_net(tw)->ns.count))
				continue;

			if (unlikely(!refcount_inc_not_zero(&tw->tw_refcnt)))
				continue;

			if (unlikely((tw->tw_family != family) ||
				     refcount_read(&twsk_net(tw)->ns.count))) {
				inet_twsk_put(tw);
				goto restart;
			}

			rcu_read_unlock();
			local_bh_disable();
			inet_twsk_deschedule_put(tw);
			local_bh_enable();
			goto restart_rcu;
		}
		/* If the nulls value we got at the end of this lookup is
		 * not the expected one, we must restart lookup.
		 * We probably met an item that was moved to another chain.
		 */
		if (get_nulls_value(node) != slot)
			goto restart;
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL_GPL(inet_twsk_purge);
