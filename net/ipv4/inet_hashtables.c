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
struct inet_bind_bucket *inet_bind_bucket_create(struct kmem_cache *cachep,
						 struct net *net,
						 struct inet_bind_hashbucket *head,
						 const unsigned short snum)
{
	struct inet_bind_bucket *tb = kmem_cache_alloc(cachep, GFP_ATOMIC);

	if (tb != NULL) {
		tb->ib_net       = hold_net(net);
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
void inet_bind_bucket_destroy(struct kmem_cache *cachep, struct inet_bind_bucket *tb)
{
	if (hlist_empty(&tb->owners)) {
		__hlist_del(&tb->node);
		release_net(tb->ib_net);
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
static void __inet_put_port(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
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

void inet_put_port(struct sock *sk)
{
	local_bh_disable();
	__inet_put_port(sk);
	local_bh_enable();
}

EXPORT_SYMBOL(inet_put_port);

void __inet_inherit_port(struct sock *sk, struct sock *child)
{
	struct inet_hashinfo *table = sk->sk_prot->h.hashinfo;
	const int bhash = inet_bhashfn(inet_sk(child)->num, table->bhash_size);
	struct inet_bind_hashbucket *head = &table->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	sk_add_bind_node(child, &tb->owners);
	inet_csk(child)->icsk_bind_hash = tb;
	spin_unlock(&head->lock);
}

EXPORT_SYMBOL_GPL(__inet_inherit_port);

/*
 * This lock without WQ_FLAG_EXCLUSIVE is good on UP and it can be very bad on SMP.
 * Look, when several writers sleep and reader wakes them up, all but one
 * immediately hit write lock and grab all the cpus. Exclusive sleep solves
 * this, _but_ remember, it adds useless work on UP machines (wake up each
 * exclusive lock release). It should be ifdefed really.
 */
void inet_listen_wlock(struct inet_hashinfo *hashinfo)
	__acquires(hashinfo->lhash_lock)
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

/*
 * Don't inline this cruft. Here are some nice properties to exploit here. The
 * BSD API does not allow a listening sock to specify the remote port nor the
 * remote address for the connection. So always assume those are both
 * wildcarded during the search since they can never be otherwise.
 */
static struct sock *inet_lookup_listener_slow(struct net *net,
					      const struct hlist_head *head,
					      const __be32 daddr,
					      const unsigned short hnum,
					      const int dif)
{
	struct sock *result = NULL, *sk;
	const struct hlist_node *node;
	int hiscore = -1;

	sk_for_each(sk, node, head) {
		const struct inet_sock *inet = inet_sk(sk);

		if (net_eq(sock_net(sk), net) && inet->num == hnum &&
				!ipv6_only_sock(sk)) {
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
struct sock *__inet_lookup_listener(struct net *net,
				    struct inet_hashinfo *hashinfo,
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
		    !sk->sk_bound_dev_if && net_eq(sock_net(sk), net))
			goto sherry_cache;
		sk = inet_lookup_listener_slow(net, head, daddr, hnum, dif);
	}
	if (sk) {
sherry_cache:
		sock_hold(sk);
	}
	read_unlock(&hashinfo->lhash_lock);
	return sk;
}
EXPORT_SYMBOL_GPL(__inet_lookup_listener);

struct sock * __inet_lookup_established(struct net *net,
				  struct inet_hashinfo *hashinfo,
				  const __be32 saddr, const __be16 sport,
				  const __be32 daddr, const u16 hnum,
				  const int dif)
{
	INET_ADDR_COOKIE(acookie, saddr, daddr)
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	struct sock *sk;
	const struct hlist_node *node;
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet_ehashfn(daddr, hnum, saddr, sport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hashinfo, hash);
	rwlock_t *lock = inet_ehash_lockp(hashinfo, hash);

	prefetch(head->chain.first);
	read_lock(lock);
	sk_for_each(sk, node, &head->chain) {
		if (INET_MATCH(sk, net, hash, acookie,
					saddr, daddr, ports, dif))
			goto hit; /* You sunk my battleship! */
	}

	/* Must check for a TIME_WAIT'er before going to listener hash. */
	sk_for_each(sk, node, &head->twchain) {
		if (INET_TW_MATCH(sk, net, hash, acookie,
					saddr, daddr, ports, dif))
			goto hit;
	}
	sk = NULL;
out:
	read_unlock(lock);
	return sk;
hit:
	sock_hold(sk);
	goto out;
}
EXPORT_SYMBOL_GPL(__inet_lookup_established);

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
	rwlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;
	struct net *net = sock_net(sk);

	prefetch(head->chain.first);
	write_lock(lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &head->twchain) {
		tw = inet_twsk(sk2);

		if (INET_TW_MATCH(sk2, net, hash, acookie,
					saddr, daddr, ports, dif)) {
			if (twsk_unique(sk, sk2, twp))
				goto unique;
			else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET_MATCH(sk2, net, hash, acookie,
					saddr, daddr, ports, dif))
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
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock(lock);

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
	write_unlock(lock);
	return -EADDRNOTAVAIL;
}

static inline u32 inet_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	return secure_ipv4_port_ephemeral(inet->rcv_saddr, inet->daddr,
					  inet->dport);
}

void __inet_hash_nolisten(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct hlist_head *list;
	rwlock_t *lock;
	struct inet_ehash_bucket *head;

	BUG_TRAP(sk_unhashed(sk));

	sk->sk_hash = inet_sk_ehashfn(sk);
	head = inet_ehash_bucket(hashinfo, sk->sk_hash);
	list = &head->chain;
	lock = inet_ehash_lockp(hashinfo, sk->sk_hash);

	write_lock(lock);
	__sk_add_node(sk, list);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock(lock);
}
EXPORT_SYMBOL_GPL(__inet_hash_nolisten);

static void __inet_hash(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct hlist_head *list;
	rwlock_t *lock;

	if (sk->sk_state != TCP_LISTEN) {
		__inet_hash_nolisten(sk);
		return;
	}

	BUG_TRAP(sk_unhashed(sk));
	list = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
	lock = &hashinfo->lhash_lock;

	inet_listen_wlock(hashinfo);
	__sk_add_node(sk, list);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock(lock);
	wake_up(&hashinfo->lhash_wait);
}

void inet_hash(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		local_bh_disable();
		__inet_hash(sk);
		local_bh_enable();
	}
}
EXPORT_SYMBOL_GPL(inet_hash);

void inet_unhash(struct sock *sk)
{
	rwlock_t *lock;
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;

	if (sk_unhashed(sk))
		goto out;

	if (sk->sk_state == TCP_LISTEN) {
		local_bh_disable();
		inet_listen_wlock(hashinfo);
		lock = &hashinfo->lhash_lock;
	} else {
		lock = inet_ehash_lockp(hashinfo, sk->sk_hash);
		write_lock_bh(lock);
	}

	if (__sk_del_node_init(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	write_unlock_bh(lock);
out:
	if (sk->sk_state == TCP_LISTEN)
		wake_up(&hashinfo->lhash_wait);
}
EXPORT_SYMBOL_GPL(inet_unhash);

int __inet_hash_connect(struct inet_timewait_death_row *death_row,
		struct sock *sk, u32 port_offset,
		int (*check_established)(struct inet_timewait_death_row *,
			struct sock *, __u16, struct inet_timewait_sock **),
		void (*hash)(struct sock *sk))
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	const unsigned short snum = inet_sk(sk)->num;
	struct inet_bind_hashbucket *head;
	struct inet_bind_bucket *tb;
	int ret;
	struct net *net = sock_net(sk);

	if (!snum) {
		int i, remaining, low, high, port;
		static u32 hint;
		u32 offset = hint + port_offset;
		struct hlist_node *node;
		struct inet_timewait_sock *tw = NULL;

		inet_get_local_port_range(&low, &high);
		remaining = (high - low) + 1;

		local_bh_disable();
		for (i = 1; i <= remaining; i++) {
			port = low + (i + offset) % remaining;
			head = &hinfo->bhash[inet_bhashfn(port, hinfo->bhash_size)];
			spin_lock(&head->lock);

			/* Does not bother with rcv_saddr checks,
			 * because the established check is already
			 * unique enough.
			 */
			inet_bind_bucket_for_each(tb, node, &head->chain) {
				if (tb->ib_net == net && tb->port == port) {
					BUG_TRAP(!hlist_empty(&tb->owners));
					if (tb->fastreuse >= 0)
						goto next_port;
					if (!check_established(death_row, sk,
								port, &tw))
						goto ok;
					goto next_port;
				}
			}

			tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
					net, head, port);
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
			hash(sk);
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
		hash(sk);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = check_established(death_row, sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

/*
 * Bind a port for a connect operation and hash it.
 */
int inet_hash_connect(struct inet_timewait_death_row *death_row,
		      struct sock *sk)
{
	return __inet_hash_connect(death_row, sk, inet_sk_port_offset(sk),
			__inet_check_established, __inet_hash_nolisten);
}

EXPORT_SYMBOL_GPL(inet_hash_connect);
