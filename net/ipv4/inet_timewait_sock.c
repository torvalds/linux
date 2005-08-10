/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic TIME_WAIT sockets functions
 *
 *		From code orinally in TCP
 */

#include <linux/config.h>

#include <net/inet_hashtables.h>
#include <net/inet_timewait_sock.h>

/* Must be called with locally disabled BHs. */
void __inet_twsk_kill(struct inet_timewait_sock *tw, struct inet_hashinfo *hashinfo)
{
	struct inet_bind_hashbucket *bhead;
	struct inet_bind_bucket *tb;
	/* Unlink from established hashes. */
	struct inet_ehash_bucket *ehead = &hashinfo->ehash[tw->tw_hashent];

	write_lock(&ehead->lock);
	if (hlist_unhashed(&tw->tw_node)) {
		write_unlock(&ehead->lock);
		return;
	}
	__hlist_del(&tw->tw_node);
	sk_node_init(&tw->tw_node);
	write_unlock(&ehead->lock);

	/* Disassociate with bind bucket. */
	bhead = &hashinfo->bhash[inet_bhashfn(tw->tw_num, hashinfo->bhash_size)];
	spin_lock(&bhead->lock);
	tb = tw->tw_tb;
	__hlist_del(&tw->tw_bind_node);
	tw->tw_tb = NULL;
	inet_bind_bucket_destroy(hashinfo->bind_bucket_cachep, tb);
	spin_unlock(&bhead->lock);
#ifdef SOCK_REFCNT_DEBUG
	if (atomic_read(&tw->tw_refcnt) != 1) {
		printk(KERN_DEBUG "%s timewait_sock %p refcnt=%d\n",
		       tw->tw_prot->name, tw, atomic_read(&tw->tw_refcnt));
	}
#endif
	inet_twsk_put(tw);
}

/*
 * Enter the time wait state. This is called with locally disabled BH.
 * Essentially we whip up a timewait bucket, copy the relevant info into it
 * from the SK, and mess with hash chains and list linkage.
 */
void __inet_twsk_hashdance(struct inet_timewait_sock *tw, struct sock *sk,
			   struct inet_hashinfo *hashinfo)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct inet_ehash_bucket *ehead = &hashinfo->ehash[sk->sk_hashent];
	struct inet_bind_hashbucket *bhead;
	/* Step 1: Put TW into bind hash. Original socket stays there too.
	   Note, that any socket with inet->num != 0 MUST be bound in
	   binding cache, even if it is closed.
	 */
	bhead = &hashinfo->bhash[inet_bhashfn(inet->num, hashinfo->bhash_size)];
	spin_lock(&bhead->lock);
	tw->tw_tb = inet->bind_hash;
	BUG_TRAP(inet->bind_hash);
	inet_twsk_add_bind_node(tw, &tw->tw_tb->owners);
	spin_unlock(&bhead->lock);

	write_lock(&ehead->lock);

	/* Step 2: Remove SK from established hash. */
	if (__sk_del_node_init(sk))
		sock_prot_dec_use(sk->sk_prot);

	/* Step 3: Hash TW into TIMEWAIT half of established hash table. */
	inet_twsk_add_node(tw, &(ehead + hashinfo->ehash_size)->chain);
	atomic_inc(&tw->tw_refcnt);

	write_unlock(&ehead->lock);
}

struct inet_timewait_sock *inet_twsk_alloc(const struct sock *sk, const int state)
{
	struct inet_timewait_sock *tw = kmem_cache_alloc(sk->sk_prot_creator->twsk_slab,
							 SLAB_ATOMIC);
	if (tw != NULL) {
		const struct inet_sock *inet = inet_sk(sk);

		/* Give us an identity. */
		tw->tw_daddr	    = inet->daddr;
		tw->tw_rcv_saddr    = inet->rcv_saddr;
		tw->tw_bound_dev_if = sk->sk_bound_dev_if;
		tw->tw_num	    = inet->num;
		tw->tw_state	    = TCP_TIME_WAIT;
		tw->tw_substate	    = state;
		tw->tw_sport	    = inet->sport;
		tw->tw_dport	    = inet->dport;
		tw->tw_family	    = sk->sk_family;
		tw->tw_reuse	    = sk->sk_reuse;
		tw->tw_hashent	    = sk->sk_hashent;
		tw->tw_ipv6only	    = 0;
		tw->tw_prot	    = sk->sk_prot_creator;
		atomic_set(&tw->tw_refcnt, 1);
		inet_twsk_dead_node_init(tw);
	}

	return tw;
}
