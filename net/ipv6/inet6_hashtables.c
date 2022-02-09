// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET6 transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp, generalised here
 *		by Arnaldo Carvalho de Melo <acme@mandriva.com>
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

extern struct inet_hashinfo tcp_hashinfo;

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
				const int dif, const int sdif)
{
	int score = -1;

	if (net_eq(sock_net(sk), net) && inet_sk(sk)->inet_num == hnum &&
	    sk->sk_family == PF_INET6) {
		if (!ipv6_addr_equal(&sk->sk_v6_rcv_saddr, daddr))
			return -1;

		if (!inet_sk_bound_dev_eq(net, sk->sk_bound_dev_if, dif, sdif))
			return -1;

		score =  sk->sk_bound_dev_if ? 2 : 1;
		if (READ_ONCE(sk->sk_incoming_cpu) == raw_smp_processor_id())
			score++;
	}
	return score;
}

static inline struct sock *lookup_reuseport(struct net *net, struct sock *sk,
					    struct sk_buff *skb, int doff,
					    const struct in6_addr *saddr,
					    __be16 sport,
					    const struct in6_addr *daddr,
					    unsigned short hnum)
{
	struct sock *reuse_sk = NULL;
	u32 phash;

	if (sk->sk_reuseport) {
		phash = inet6_ehashfn(net, daddr, hnum, saddr, sport);
		reuse_sk = reuseport_select_sock(sk, phash, skb, doff);
	}
	return reuse_sk;
}

/* called with rcu_read_lock() */
static struct sock *inet6_lhash2_lookup(struct net *net,
		struct inet_listen_hashbucket *ilb2,
		struct sk_buff *skb, int doff,
		const struct in6_addr *saddr,
		const __be16 sport, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif, const int sdif)
{
	struct inet_connection_sock *icsk;
	struct sock *sk, *result = NULL;
	int score, hiscore = 0;

	inet_lhash2_for_each_icsk_rcu(icsk, &ilb2->head) {
		sk = (struct sock *)icsk;
		score = compute_score(sk, net, hnum, daddr, dif, sdif);
		if (score > hiscore) {
			result = lookup_reuseport(net, sk, skb, doff,
						  saddr, sport, daddr, hnum);
			if (result)
				return result;

			result = sk;
			hiscore = score;
		}
	}

	return result;
}

static inline struct sock *inet6_lookup_run_bpf(struct net *net,
						struct inet_hashinfo *hashinfo,
						struct sk_buff *skb, int doff,
						const struct in6_addr *saddr,
						const __be16 sport,
						const struct in6_addr *daddr,
						const u16 hnum)
{
	struct sock *sk, *reuse_sk;
	bool no_reuseport;

	if (hashinfo != &tcp_hashinfo)
		return NULL; /* only TCP is supported */

	no_reuseport = bpf_sk_lookup_run_v6(net, IPPROTO_TCP,
					    saddr, sport, daddr, hnum, &sk);
	if (no_reuseport || IS_ERR_OR_NULL(sk))
		return sk;

	reuse_sk = lookup_reuseport(net, sk, skb, doff, saddr, sport, daddr, hnum);
	if (reuse_sk)
		sk = reuse_sk;
	return sk;
}

struct sock *inet6_lookup_listener(struct net *net,
		struct inet_hashinfo *hashinfo,
		struct sk_buff *skb, int doff,
		const struct in6_addr *saddr,
		const __be16 sport, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif, const int sdif)
{
	struct inet_listen_hashbucket *ilb2;
	struct sock *result = NULL;
	unsigned int hash2;

	/* Lookup redirect from BPF */
	if (static_branch_unlikely(&bpf_sk_lookup_enabled)) {
		result = inet6_lookup_run_bpf(net, hashinfo, skb, doff,
					      saddr, sport, daddr, hnum);
		if (result)
			goto done;
	}

	hash2 = ipv6_portaddr_hash(net, daddr, hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);

	result = inet6_lhash2_lookup(net, ilb2, skb, doff,
				     saddr, sport, daddr, hnum,
				     dif, sdif);
	if (result)
		goto done;

	/* Lookup lhash2 with in6addr_any */
	hash2 = ipv6_portaddr_hash(net, &in6addr_any, hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);

	result = inet6_lhash2_lookup(net, ilb2, skb, doff,
				     saddr, sport, &in6addr_any, hnum,
				     dif, sdif);
done:
	if (IS_ERR(result))
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

	if (sk->sk_state != TCP_CLOSE)
		err = __inet_hash(sk, NULL);

	return err;
}
EXPORT_SYMBOL_GPL(inet6_hash);
