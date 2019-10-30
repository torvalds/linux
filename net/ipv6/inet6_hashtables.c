/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET6 transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp, generalised here
 *		by Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>

#include <net/addrconf.h>
#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/secure_seq.h>
#include <net/ip.h>
#include <net/sock_reuseport.h>

u32 inet6_ehashfn(const struct net *net,
		  const struct in6_addr *laddr, const u16 lport,
		  const struct in6_addr *faddr, const __be16 fport)
{
	static u32 inet6_ehash_secret __read_mostly;
	static u32 ipv6_hash_secret __read_mostly;

	u32 lhash, fhash;

	net_get_random_once(&inet6_ehash_secret, sizeof(inet6_ehash_secret));
	net_get_random_once(&ipv6_hash_secret, sizeof(ipv6_hash_secret));

	lhash = (__force u32)laddr->s6_addr32[3];
	fhash = __ipv6_addr_jhash(faddr, ipv6_hash_secret);

	return __inet6_ehashfn(lhash, lport, fhash, fport,
			       inet6_ehash_secret + net_hash_mix(net));
}

/*
 * Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 *
 * The sockhash lock must be held as a reader here.
 */
struct sock *__inet6_lookup_established(struct net *net,
					struct inet_hashinfo *hashinfo,
					   const struct in6_addr *saddr,
					   const __be16 sport,
					   const struct in6_addr *daddr,
					   const u16 hnum,
					   const int dif, const int sdif)
{
	struct sock *sk;
	const struct hlist_nulls_node *node;
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet6_ehashfn(net, daddr, hnum, saddr, sport);
	unsigned int slot = hash & hashinfo->ehash_mask;
	struct inet_ehash_bucket *head = &hashinfo->ehash[slot];


begin:
	sk_nulls_for_each_rcu(sk, node, &head->chain) {
		if (sk->sk_hash != hash)
			continue;
		if (!INET6_MATCH(sk, net, saddr, daddr, ports, dif, sdif))
			continue;
		if (unlikely(!refcount_inc_not_zero(&sk->sk_refcnt)))
			goto out;

		if (unlikely(!INET6_MATCH(sk, net, saddr, daddr, ports, dif, sdif))) {
			sock_gen_put(sk);
			goto begin;
		}
		goto found;
	}
	if (get_nulls_value(node) != slot)
		goto begin;
out:
	sk = NULL;
found:
	return sk;
}
EXPORT_SYMBOL(__inet6_lookup_established);

static inline int compute_score(struct sock *sk, struct net *net,
				const unsigned short hnum,
				const struct in6_addr *daddr,
				const int dif, const int sdif, bool exact_dif)
{
	int score = -1;

	if (net_eq(sock_net(sk), net) && inet_sk(sk)->inet_num == hnum &&
	    sk->sk_family == PF_INET6) {

		score = 1;
		if (!ipv6_addr_any(&sk->sk_v6_rcv_saddr)) {
			if (!ipv6_addr_equal(&sk->sk_v6_rcv_saddr, daddr))
				return -1;
			score++;
		}
		if (sk->sk_bound_dev_if || exact_dif) {
			bool dev_match = (sk->sk_bound_dev_if == dif ||
					  sk->sk_bound_dev_if == sdif);

			if (!dev_match)
				return -1;
			if (sk->sk_bound_dev_if)
				score++;
		}
		if (READ_ONCE(sk->sk_incoming_cpu) == raw_smp_processor_id())
			score++;
	}
	return score;
}

/* called with rcu_read_lock() */
static struct sock *inet6_lhash2_lookup(struct net *net,
		struct inet_listen_hashbucket *ilb2,
		struct sk_buff *skb, int doff,
		const struct in6_addr *saddr,
		const __be16 sport, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif, const int sdif)
{
	bool exact_dif = inet6_exact_dif_match(net, skb);
	struct inet_connection_sock *icsk;
	struct sock *sk, *result = NULL;
	int score, hiscore = 0;
	u32 phash = 0;

	inet_lhash2_for_each_icsk_rcu(icsk, &ilb2->head) {
		sk = (struct sock *)icsk;
		score = compute_score(sk, net, hnum, daddr, dif, sdif,
				      exact_dif);
		if (score > hiscore) {
			if (sk->sk_reuseport) {
				phash = inet6_ehashfn(net, daddr, hnum,
						      saddr, sport);
				result = reuseport_select_sock(sk, phash,
							       skb, doff);
				if (result)
					return result;
			}
			result = sk;
			hiscore = score;
		}
	}

	return result;
}

struct sock *inet6_lookup_listener(struct net *net,
		struct inet_hashinfo *hashinfo,
		struct sk_buff *skb, int doff,
		const struct in6_addr *saddr,
		const __be16 sport, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif, const int sdif)
{
	unsigned int hash = inet_lhashfn(net, hnum);
	struct inet_listen_hashbucket *ilb = &hashinfo->listening_hash[hash];
	bool exact_dif = inet6_exact_dif_match(net, skb);
	struct inet_listen_hashbucket *ilb2;
	struct sock *sk, *result = NULL;
	int score, hiscore = 0;
	unsigned int hash2;
	u32 phash = 0;

	if (ilb->count <= 10 || !hashinfo->lhash2)
		goto port_lookup;

	/* Too many sk in the ilb bucket (which is hashed by port alone).
	 * Try lhash2 (which is hashed by port and addr) instead.
	 */

	hash2 = ipv6_portaddr_hash(net, daddr, hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);
	if (ilb2->count > ilb->count)
		goto port_lookup;

	result = inet6_lhash2_lookup(net, ilb2, skb, doff,
				     saddr, sport, daddr, hnum,
				     dif, sdif);
	if (result)
		goto done;

	/* Lookup lhash2 with in6addr_any */

	hash2 = ipv6_portaddr_hash(net, &in6addr_any, hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);
	if (ilb2->count > ilb->count)
		goto port_lookup;

	result = inet6_lhash2_lookup(net, ilb2, skb, doff,
				     saddr, sport, daddr, hnum,
				     dif, sdif);
	goto done;

port_lookup:
	sk_for_each(sk, &ilb->head) {
		score = compute_score(sk, net, hnum, daddr, dif, sdif, exact_dif);
		if (score > hiscore) {
			if (sk->sk_reuseport) {
				phash = inet6_ehashfn(net, daddr, hnum,
						      saddr, sport);
				result = reuseport_select_sock(sk, phash,
							       skb, doff);
				if (result)
					goto done;
			}
			result = sk;
			hiscore = score;
		}
	}
done:
	if (unlikely(IS_ERR(result)))
		return NULL;
	return result;
}
EXPORT_SYMBOL_GPL(inet6_lookup_listener);

struct sock *inet6_lookup(struct net *net, struct inet_hashinfo *hashinfo,
			  struct sk_buff *skb, int doff,
			  const struct in6_addr *saddr, const __be16 sport,
			  const struct in6_addr *daddr, const __be16 dport,
			  const int dif)
{
	struct sock *sk;
	bool refcounted;

	sk = __inet6_lookup(net, hashinfo, skb, doff, saddr, sport, daddr,
			    ntohs(dport), dif, 0, &refcounted);
	if (sk && !refcounted && !refcount_inc_not_zero(&sk->sk_refcnt))
		sk = NULL;
	return sk;
}
EXPORT_SYMBOL_GPL(inet6_lookup);

static int __inet6_check_established(struct inet_timewait_death_row *death_row,
				     struct sock *sk, const __u16 lport,
				     struct inet_timewait_sock **twp)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_sock *inet = inet_sk(sk);
	const struct in6_addr *daddr = &sk->sk_v6_rcv_saddr;
	const struct in6_addr *saddr = &sk->sk_v6_daddr;
	const int dif = sk->sk_bound_dev_if;
	struct net *net = sock_net(sk);
	const int sdif = l3mdev_master_ifindex_by_index(net, dif);
	const __portpair ports = INET_COMBINED_PORTS(inet->inet_dport, lport);
	const unsigned int hash = inet6_ehashfn(net, daddr, lport, saddr,
						inet->inet_dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	spinlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_nulls_node *node;
	struct inet_timewait_sock *tw = NULL;

	spin_lock(lock);

	sk_nulls_for_each(sk2, node, &head->chain) {
		if (sk2->sk_hash != hash)
			continue;

		if (likely(INET6_MATCH(sk2, net, saddr, daddr, ports,
				       dif, sdif))) {
			if (sk2->sk_state == TCP_TIME_WAIT) {
				tw = inet_twsk(sk2);
				if (twsk_unique(sk, sk2, twp))
					break;
			}
			goto not_unique;
		}
	}

	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity.
	 */
	inet->inet_num = lport;
	inet->inet_sport = htons(lport);
	sk->sk_hash = hash;
	WARN_ON(!sk_unhashed(sk));
	__sk_nulls_add_node_rcu(sk, &head->chain);
	if (tw) {
		sk_nulls_del_node_init_rcu((struct sock *)tw);
		__NET_INC_STATS(net, LINUX_MIB_TIMEWAITRECYCLED);
	}
	spin_unlock(lock);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);

	if (twp) {
		*twp = tw;
	} else if (tw) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule_put(tw);
	}
	return 0;

not_unique:
	spin_unlock(lock);
	return -EADDRNOTAVAIL;
}

static u32 inet6_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);

	return secure_ipv6_port_ephemeral(sk->sk_v6_rcv_saddr.s6_addr32,
					  sk->sk_v6_daddr.s6_addr32,
					  inet->inet_dport);
}

int inet6_hash_connect(struct inet_timewait_death_row *death_row,
		       struct sock *sk)
{
	u32 port_offset = 0;

	if (!inet_sk(sk)->inet_num)
		port_offset = inet6_sk_port_offset(sk);
	return __inet_hash_connect(death_row, sk, port_offset,
				   __inet6_check_established);
}
EXPORT_SYMBOL_GPL(inet6_hash_connect);

int inet6_hash(struct sock *sk)
{
	int err = 0;

	if (sk->sk_state != TCP_CLOSE) {
		local_bh_disable();
		err = __inet_hash(sk, NULL);
		local_bh_enable();
	}

	return err;
}
EXPORT_SYMBOL_GPL(inet6_hash);
