/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/ip.h>

/*
 * Allocate and initialize a new local port bind bucket.
 * The bindhash mutex for snum's hash chain must be held here.
 */
struct inet_bind_bucket *inet_bind_bucket_create(kmem_cache_t *cachep,
						 struct inet_bind_hashbucket *head,
						 const unsigned short snum)
{
	struct inet_bind_bucket *tb = kmem_cache_alloc(cachep, GFP_ATOMIC);

	if (tb != NULL) {
		tb->port      = snum;
		tb->fastreuse = 0;
		INIT_HLIST_HEAD(&tb->owners);
		hlist_add_head(&tb->node, &head->chain);
	}
	return tb;
}

/*
 * Caller must hold hashbucket lock for this tb with local BH disabled
 */
void inet_bind_bucket_destroy(kmem_cache_t *cachep, struct inet_bind_bucket *tb)
{
	if (hlist_empty(&tb->owners)) {
		__hlist_del(&tb->node);
		kmem_cache_free(cachep, tb);
	}
}

void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
		    const unsigned short snum)
{
	inet_sk(sk)->num = snum;
	sk_add_bind_node(sk, &tb->owners);
	inet_csk(sk)->icsk_bind_hash = tb;
}

/*
 * Get rid of any references to a local port held by the given sock.
 */
static void __inet_put_port(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	const int bhash = inet_bhashfn(inet_sk(sk)->num, hashinfo->bhash_size);
	struct inet_bind_hashbucket *head = &hashinfo->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	__sk_del_bind_node(sk);
	inet_csk(sk)->icsk_bind_hash = NULL;
	inet_sk(sk)->num = 0;
	inet_bind_bucket_destroy(hashinfo->bind_bucket_cachep, tb);
	spin_unlock(&head->lock);
}

void inet_put_port(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	local_bh_disable();
	__inet_put_port(hashinfo, sk);
	local_bh_enable();
}

EXPORT_SYMBOL(inet_put_port);

/*
 * This lock without WQ_FLAG_EXCLUSIVE is good on UP and it can be very bad on SMP.
 * Look, when several writers sleep and reader wakes them up, all but one
 * immediately hit write lock and grab all the cpus. Exclusive sleep solves
 * this, _but_ remember, it adds useless work on UP machines (wake up each
 * exclusive lock release). It should be ifdefed really.
 */
void inet_listen_wlock(struct inet_hashinfo *hashinfo)
{
	write_lock(&hashinfo->lhash_lock);

	if (atomic_read(&hashinfo->lhash_users)) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait_exclusive(&hashinfo->lhash_wait,
						  &wait, TASK_UNINTERRUPTIBLE);
			if (!atomic_read(&hashinfo->lhash_users))
				break;
			write_unlock_bh(&hashinfo->lhash_lock);
			schedule();
			write_lock_bh(&hashinfo->lhash_lock);
		}

		finish_wait(&hashinfo->lhash_wait, &wait);
	}
}

EXPORT_SYMBOL(inet_listen_wlock);

/*
 * Don't inline this cruft. Here are some nice properties to exploit here. The
 * BSD API does not allow a listening sock to specify the remote port nor the
 * remote address for the connection. So always assume those are both
 * wildcarded during the search since they can never be otherwise.
 */
static struct sock *inet_lookup_listener_slow(const struct hlist_head *head,
					      const __be32 daddr,
					      const unsigned short hnum,
					      const int dif)
{
	struct sock *result = NULL, *sk;
	const struct hlist_node *node;
	int hiscore = -1;

	sk_for_each(sk, node, head) {
		const struct inet_sock *inet = inet_sk(sk);

		if (inet->num == hnum && !ipv6_only_sock(sk)) {
			const __be32 rcv_saddr = inet->rcv_saddr;
			int score = sk->sk_family == PF_INET ? 1 : 0;

			if (rcv_saddr) {
				if (rcv_saddr != daddr)
					continue;
				score += 2;
			}
			if (sk->sk_bound_dev_if) {
				if (sk->sk_bound_dev_if != dif)
					continue;
				score += 2;
			}
			if (score == 5)
				return sk;
			if (score > hiscore) {
				hiscore	= score;
				result	= sk;
			}
		}
	}
	return result;
}

/* Optimize the common listener case. */
struct sock *__inet_lookup_listener(struct inet_hashinfo *hashinfo,
				    const __be32 daddr, const unsigned short hnum,
				    const int dif)
{
	struct sock *sk = NULL;
	const struct hlist_head *head;

	read_lock(&hashinfo->lhash_lock);
	head = &hashinfo->listening_hash[inet_lhashfn(hnum)];
	if (!hlist_empty(head)) {
		const struct inet_sock *inet = inet_sk((sk = __sk_head(head)));

		if (inet->num == hnum && !sk->sk_node.next &&
		    (!inet->rcv_saddr || inet->rcv_saddr == daddr) &&
		    (sk->sk_family == PF_INET || !ipv6_only_sock(sk)) &&
		    !sk->sk_bound_dev_if)
			goto sherry_cache;
		sk = inet_lookup_listener_slow(head, daddr, hnum, dif);
	}
	if (sk) {
sherry_cache:
		sock_hold(sk);
	}
	read_unlock(&hashinfo->lhash_lock);
	return sk;
}
EXPORT_SYMBOL_GPL(__inet_lookup_listener);

/* called with local bh disabled */
static int __inet_check_established(struct inet_timewait_death_row *death_row,
				    struct sock *sk, __u16 lport,
				    struct inet_timewait_sock **twp)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_sock *inet = inet_sk(sk);
	__be32 daddr = inet->rcv_saddr;
	__be32 saddr = inet->daddr;
	int dif = sk->sk_bound_dev_if;
	INET_ADDR_COOKIE(acookie, saddr, daddr)
	const __portpair ports = INET_COMBINED_PORTS(inet->dport, lport);
	unsigned int hash = inet_ehashfn(daddr, lport, saddr, inet->dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;

	prefetch(head->chain.first);
	write_lock(&head->lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &(head + hinfo->ehash_size)->chain) {
		tw = inet_twsk(sk2);

		if (INET_TW_MATCH(sk2, hash, acookie, saddr, daddr, ports, dif)) {
			if (twsk_unique(sk, sk2, twp))
				goto unique;
			else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET_MATCH(sk2, hash, acookie, saddr, daddr, ports, dif))
			goto not_unique;
	}

unique:
	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity. */
	inet->num = lport;
	inet->sport = htons(lport);
	sk->sk_hash = hash;
	BUG_TRAP(sk_unhashed(sk));
	__sk_add_node(sk, &head->chain);
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(&head->lock);

	if (twp) {
		*twp = tw;
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);
	} else if (tw) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw, death_row);
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);

		inet_twsk_put(tw);
	}

	return 0;

not_unique:
	write_unlock(&head->lock);
	return -EADDRNOTAVAIL;
}

static inline u32 inet_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	return secure_ipv4_port_ephemeral(inet->rcv_saddr, inet->daddr, 
					  inet->dport);
}

/*
 * Bind a port for a connect operation and hash it.
 */
int inet_hash_connect(struct inet_timewait_death_row *death_row,
		      struct sock *sk)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	const unsigned short snum = inet_sk(sk)->num;
 	struct inet_bind_hashbucket *head;
 	struct inet_bind_bucket *tb;
	int ret;

 	if (!snum) {
 		int low = sysctl_local_port_range[0];
 		int high = sysctl_local_port_range[1];
		int range = high - low;
 		int i;
		int port;
		static u32 hint;
		u32 offset = hint + inet_sk_port_offset(sk);
		struct hlist_node *node;
 		struct inet_timewait_sock *tw = NULL;

 		local_bh_disable();
		for (i = 1; i <= range; i++) {
			port = low + (i + offset) % range;
 			head = &hinfo->bhash[inet_bhashfn(port, hinfo->bhash_size)];
 			spin_lock(&head->lock);

 			/* Does not bother with rcv_saddr checks,
 			 * because the established check is already
 			 * unique enough.
 			 */
			inet_bind_bucket_for_each(tb, node, &head->chain) {
 				if (tb->port == port) {
 					BUG_TRAP(!hlist_empty(&tb->owners));
 					if (tb->fastreuse >= 0)
 						goto next_port;
 					if (!__inet_check_established(death_row,
								      sk, port,
								      &tw))
 						goto ok;
 					goto next_port;
 				}
 			}

 			tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep, head, port);
 			if (!tb) {
 				spin_unlock(&head->lock);
 				break;
 			}
 			tb->fastreuse = -1;
 			goto ok;

 		next_port:
 			spin_unlock(&head->lock);
 		}
 		local_bh_enable();

 		return -EADDRNOTAVAIL;

ok:
		hint += i;

 		/* Head lock still held and bh's disabled */
 		inet_bind_hash(sk, tb, port);
		if (sk_unhashed(sk)) {
 			inet_sk(sk)->sport = htons(port);
 			__inet_hash(hinfo, sk, 0);
 		}
 		spin_unlock(&head->lock);

 		if (tw) {
 			inet_twsk_deschedule(tw, death_row);
 			inet_twsk_put(tw);
 		}

		ret = 0;
		goto out;
 	}

 	head = &hinfo->bhash[inet_bhashfn(snum, hinfo->bhash_size)];
 	tb  = inet_csk(sk)->icsk_bind_hash;
	spin_lock_bh(&head->lock);
	if (sk_head(&tb->owners) == sk && !sk->sk_bind_node.next) {
		__inet_hash(hinfo, sk, 0);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = __inet_check_established(death_row, sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

EXPORT_SYMBOL_GPL(inet_hash_connect);
