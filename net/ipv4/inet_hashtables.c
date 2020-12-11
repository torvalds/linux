// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>

#include <net/addrconf.h>
#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/secure_seq.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/sock_reuseport.h>

static u32 inet_ehashfn(const struct net *net, const __be32 laddr,
			const __u16 lport, const __be32 faddr,
			const __be16 fport)
{
	static u32 inet_ehash_secret __read_mostly;

	net_get_random_once(&inet_ehash_secret, sizeof(inet_ehash_secret));

	return __inet_ehashfn(laddr, lport, faddr, fport,
			      inet_ehash_secret + net_hash_mix(net));
}

/* This function handles inet_sock, but also timewait and request sockets
 * for IPv4/IPv6.
 */
static u32 sk_ehashfn(const struct sock *sk)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6 &&
	    !ipv6_addr_v4mapped(&sk->sk_v6_daddr))
		return inet6_ehashfn(sock_net(sk),
				     &sk->sk_v6_rcv_saddr, sk->sk_num,
				     &sk->sk_v6_daddr, sk->sk_dport);
#endif
	return inet_ehashfn(sock_net(sk),
			    sk->sk_rcv_saddr, sk->sk_num,
			    sk->sk_daddr, sk->sk_dport);
}

/*
 * Allocate and initialize a new local port bind bucket.
 * The bindhash mutex for snum's hash chain must be held here.
 */
struct inet_bind_bucket *inet_bind_bucket_create(struct kmem_cache *cachep,
						 struct net *net,
						 struct inet_bind_hashbucket *head,
						 const unsigned short snum,
						 int l3mdev)
{
	struct inet_bind_bucket *tb = kmem_cache_alloc(cachep, GFP_ATOMIC);

	if (tb) {
		write_pnet(&tb->ib_net, net);
		tb->l3mdev    = l3mdev;
		tb->port      = snum;
		tb->fastreuse = 0;
		tb->fastreuseport = 0;
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
		kmem_cache_free(cachep, tb);
	}
}

void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
		    const unsigned short snum)
{
	inet_sk(sk)->inet_num = snum;
	sk_add_bind_node(sk, &tb->owners);
	inet_csk(sk)->icsk_bind_hash = tb;
}

/*
 * Get rid of any references to a local port held by the given sock.
 */
static void __inet_put_port(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	const int bhash = inet_bhashfn(sock_net(sk), inet_sk(sk)->inet_num,
			hashinfo->bhash_size);
	struct inet_bind_hashbucket *head = &hashinfo->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	__sk_del_bind_node(sk);
	inet_csk(sk)->icsk_bind_hash = NULL;
	inet_sk(sk)->inet_num = 0;
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

int __inet_inherit_port(const struct sock *sk, struct sock *child)
{
	struct inet_hashinfo *table = sk->sk_prot->h.hashinfo;
	unsigned short port = inet_sk(child)->inet_num;
	const int bhash = inet_bhashfn(sock_net(sk), port,
			table->bhash_size);
	struct inet_bind_hashbucket *head = &table->bhash[bhash];
	struct inet_bind_bucket *tb;
	int l3mdev;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	if (unlikely(!tb)) {
		spin_unlock(&head->lock);
		return -ENOENT;
	}
	if (tb->port != port) {
		l3mdev = inet_sk_bound_l3mdev(sk);

		/* NOTE: using tproxy and redirecting skbs to a proxy
		 * on a different listener port breaks the assumption
		 * that the listener socket's icsk_bind_hash is the same
		 * as that of the child socket. We have to look up or
		 * create a new bind bucket for the child here. */
		inet_bind_bucket_for_each(tb, &head->chain) {
			if (net_eq(ib_net(tb), sock_net(sk)) &&
			    tb->l3mdev == l3mdev && tb->port == port)
				break;
		}
		if (!tb) {
			tb = inet_bind_bucket_create(table->bind_bucket_cachep,
						     sock_net(sk), head, port,
						     l3mdev);
			if (!tb) {
				spin_unlock(&head->lock);
				return -ENOMEM;
			}
		}
		inet_csk_update_fastreuse(tb, child);
	}
	inet_bind_hash(child, tb, port);
	spin_unlock(&head->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(__inet_inherit_port);

static struct inet_listen_hashbucket *
inet_lhash2_bucket_sk(struct inet_hashinfo *h, struct sock *sk)
{
	u32 hash;

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		hash = ipv6_portaddr_hash(sock_net(sk),
					  &sk->sk_v6_rcv_saddr,
					  inet_sk(sk)->inet_num);
	else
#endif
		hash = ipv4_portaddr_hash(sock_net(sk),
					  inet_sk(sk)->inet_rcv_saddr,
					  inet_sk(sk)->inet_num);
	return inet_lhash2_bucket(h, hash);
}

static void inet_hash2(struct inet_hashinfo *h, struct sock *sk)
{
	struct inet_listen_hashbucket *ilb2;

	if (!h->lhash2)
		return;

	ilb2 = inet_lhash2_bucket_sk(h, sk);

	spin_lock(&ilb2->lock);
	if (sk->sk_reuseport && sk->sk_family == AF_INET6)
		hlist_add_tail_rcu(&inet_csk(sk)->icsk_listen_portaddr_node,
				   &ilb2->head);
	else
		hlist_add_head_rcu(&inet_csk(sk)->icsk_listen_portaddr_node,
				   &ilb2->head);
	ilb2->count++;
	spin_unlock(&ilb2->lock);
}

static void inet_unhash2(struct inet_hashinfo *h, struct sock *sk)
{
	struct inet_listen_hashbucket *ilb2;

	if (!h->lhash2 ||
	    WARN_ON_ONCE(hlist_unhashed(&inet_csk(sk)->icsk_listen_portaddr_node)))
		return;

	ilb2 = inet_lhash2_bucket_sk(h, sk);

	spin_lock(&ilb2->lock);
	hlist_del_init_rcu(&inet_csk(sk)->icsk_listen_portaddr_node);
	ilb2->count--;
	spin_unlock(&ilb2->lock);
}

static inline int compute_score(struct sock *sk, struct net *net,
				const unsigned short hnum, const __be32 daddr,
				const int dif, const int sdif)
{
	int score = -1;

	if (net_eq(sock_net(sk), net) && sk->sk_num == hnum &&
			!ipv6_only_sock(sk)) {
		if (sk->sk_rcv_saddr != daddr)
			return -1;

		if (!inet_sk_bound_dev_eq(net, sk->sk_bound_dev_if, dif, sdif))
			return -1;

		score = sk->sk_family == PF_INET ? 2 : 1;
		if (READ_ONCE(sk->sk_incoming_cpu) == raw_smp_processor_id())
			score++;
	}
	return score;
}

static inline struct sock *lookup_reuseport(struct net *net, struct sock *sk,
					    struct sk_buff *skb, int doff,
					    __be32 saddr, __be16 sport,
					    __be32 daddr, unsigned short hnum)
{
	struct sock *reuse_sk = NULL;
	u32 phash;

	if (sk->sk_reuseport) {
		phash = inet_ehashfn(net, daddr, hnum, saddr, sport);
		reuse_sk = reuseport_select_sock(sk, phash, skb, doff);
	}
	return reuse_sk;
}

/*
 * Here are some nice properties to exploit here. The BSD API
 * does not allow a listening sock to specify the remote port nor the
 * remote address for the connection. So always assume those are both
 * wildcarded during the search since they can never be otherwise.
 */

/* called with rcu_read_lock() : No refcount taken on the socket */
static struct sock *inet_lhash2_lookup(struct net *net,
				struct inet_listen_hashbucket *ilb2,
				struct sk_buff *skb, int doff,
				const __be32 saddr, __be16 sport,
				const __be32 daddr, const unsigned short hnum,
				const int dif, const int sdif)
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

static inline struct sock *inet_lookup_run_bpf(struct net *net,
					       struct inet_hashinfo *hashinfo,
					       struct sk_buff *skb, int doff,
					       __be32 saddr, __be16 sport,
					       __be32 daddr, u16 hnum)
{
	struct sock *sk, *reuse_sk;
	bool no_reuseport;

	if (hashinfo != &tcp_hashinfo)
		return NULL; /* only TCP is supported */

	no_reuseport = bpf_sk_lookup_run_v4(net, IPPROTO_TCP,
					    saddr, sport, daddr, hnum, &sk);
	if (no_reuseport || IS_ERR_OR_NULL(sk))
		return sk;

	reuse_sk = lookup_reuseport(net, sk, skb, doff, saddr, sport, daddr, hnum);
	if (reuse_sk)
		sk = reuse_sk;
	return sk;
}

struct sock *__inet_lookup_listener(struct net *net,
				    struct inet_hashinfo *hashinfo,
				    struct sk_buff *skb, int doff,
				    const __be32 saddr, __be16 sport,
				    const __be32 daddr, const unsigned short hnum,
				    const int dif, const int sdif)
{
	struct inet_listen_hashbucket *ilb2;
	struct sock *result = NULL;
	unsigned int hash2;

	/* Lookup redirect from BPF */
	if (static_branch_unlikely(&bpf_sk_lookup_enabled)) {
		result = inet_lookup_run_bpf(net, hashinfo, skb, doff,
					     saddr, sport, daddr, hnum);
		if (result)
			goto done;
	}

	hash2 = ipv4_portaddr_hash(net, daddr, hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);

	result = inet_lhash2_lookup(net, ilb2, skb, doff,
				    saddr, sport, daddr, hnum,
				    dif, sdif);
	if (result)
		goto done;

	/* Lookup lhash2 with INADDR_ANY */
	hash2 = ipv4_portaddr_hash(net, htonl(INADDR_ANY), hnum);
	ilb2 = inet_lhash2_bucket(hashinfo, hash2);

	result = inet_lhash2_lookup(net, ilb2, skb, doff,
				    saddr, sport, htonl(INADDR_ANY), hnum,
				    dif, sdif);
done:
	if (IS_ERR(result))
		return NULL;
	return result;
}
EXPORT_SYMBOL_GPL(__inet_lookup_listener);

/* All sockets share common refcount, but have different destructors */
void sock_gen_put(struct sock *sk)
{
	if (!refcount_dec_and_test(&sk->sk_refcnt))
		return;

	if (sk->sk_state == TCP_TIME_WAIT)
		inet_twsk_free(inet_twsk(sk));
	else if (sk->sk_state == TCP_NEW_SYN_RECV)
		reqsk_free(inet_reqsk(sk));
	else
		sk_free(sk);
}
EXPORT_SYMBOL_GPL(sock_gen_put);

void sock_edemux(struct sk_buff *skb)
{
	sock_gen_put(skb->sk);
}
EXPORT_SYMBOL(sock_edemux);

struct sock *__inet_lookup_established(struct net *net,
				  struct inet_hashinfo *hashinfo,
				  const __be32 saddr, const __be16 sport,
				  const __be32 daddr, const u16 hnum,
				  const int dif, const int sdif)
{
	INET_ADDR_COOKIE(acookie, saddr, daddr);
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	struct sock *sk;
	const struct hlist_nulls_node *node;
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet_ehashfn(net, daddr, hnum, saddr, sport);
	unsigned int slot = hash & hashinfo->ehash_mask;
	struct inet_ehash_bucket *head = &hashinfo->ehash[slot];

begin:
	sk_nulls_for_each_rcu(sk, node, &head->chain) {
		if (sk->sk_hash != hash)
			continue;
		if (likely(INET_MATCH(sk, net, acookie,
				      saddr, daddr, ports, dif, sdif))) {
			if (unlikely(!refcount_inc_not_zero(&sk->sk_refcnt)))
				goto out;
			if (unlikely(!INET_MATCH(sk, net, acookie,
						 saddr, daddr, ports,
						 dif, sdif))) {
				sock_gen_put(sk);
				goto begin;
			}
			goto found;
		}
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(node) != slot)
		goto begin;
out:
	sk = NULL;
found:
	return sk;
}
EXPORT_SYMBOL_GPL(__inet_lookup_established);

/* called with local bh disabled */
static int __inet_check_established(struct inet_timewait_death_row *death_row,
				    struct sock *sk, __u16 lport,
				    struct inet_timewait_sock **twp)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_sock *inet = inet_sk(sk);
	__be32 daddr = inet->inet_rcv_saddr;
	__be32 saddr = inet->inet_daddr;
	int dif = sk->sk_bound_dev_if;
	struct net *net = sock_net(sk);
	int sdif = l3mdev_master_ifindex_by_index(net, dif);
	INET_ADDR_COOKIE(acookie, saddr, daddr);
	const __portpair ports = INET_COMBINED_PORTS(inet->inet_dport, lport);
	unsigned int hash = inet_ehashfn(net, daddr, lport,
					 saddr, inet->inet_dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	spinlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_nulls_node *node;
	struct inet_timewait_sock *tw = NULL;

	spin_lock(lock);

	sk_nulls_for_each(sk2, node, &head->chain) {
		if (sk2->sk_hash != hash)
			continue;

		if (likely(INET_MATCH(sk2, net, acookie,
					 saddr, daddr, ports, dif, sdif))) {
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

static u32 inet_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);

	return secure_ipv4_port_ephemeral(inet->inet_rcv_saddr,
					  inet->inet_daddr,
					  inet->inet_dport);
}

/* insert a socket into ehash, and eventually remove another one
 * (The another one can be a SYN_RECV or TIMEWAIT
 */
bool inet_ehash_insert(struct sock *sk, struct sock *osk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct hlist_nulls_head *list;
	struct inet_ehash_bucket *head;
	spinlock_t *lock;
	bool ret = true;

	WARN_ON_ONCE(!sk_unhashed(sk));

	sk->sk_hash = sk_ehashfn(sk);
	head = inet_ehash_bucket(hashinfo, sk->sk_hash);
	list = &head->chain;
	lock = inet_ehash_lockp(hashinfo, sk->sk_hash);

	spin_lock(lock);
	if (osk) {
		WARN_ON_ONCE(sk->sk_hash != osk->sk_hash);
		ret = sk_nulls_del_node_init_rcu(osk);
	}
	if (ret)
		__sk_nulls_add_node_rcu(sk, list);
	spin_unlock(lock);
	return ret;
}

bool inet_ehash_nolisten(struct sock *sk, struct sock *osk)
{
	bool ok = inet_ehash_insert(sk, osk);

	if (ok) {
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	} else {
		percpu_counter_inc(sk->sk_prot->orphan_count);
		inet_sk_set_state(sk, TCP_CLOSE);
		sock_set_flag(sk, SOCK_DEAD);
		inet_csk_destroy_sock(sk);
	}
	return ok;
}
EXPORT_SYMBOL_GPL(inet_ehash_nolisten);

static int inet_reuseport_add_sock(struct sock *sk,
				   struct inet_listen_hashbucket *ilb)
{
	struct inet_bind_bucket *tb = inet_csk(sk)->icsk_bind_hash;
	const struct hlist_nulls_node *node;
	struct sock *sk2;
	kuid_t uid = sock_i_uid(sk);

	sk_nulls_for_each_rcu(sk2, node, &ilb->nulls_head) {
		if (sk2 != sk &&
		    sk2->sk_family == sk->sk_family &&
		    ipv6_only_sock(sk2) == ipv6_only_sock(sk) &&
		    sk2->sk_bound_dev_if == sk->sk_bound_dev_if &&
		    inet_csk(sk2)->icsk_bind_hash == tb &&
		    sk2->sk_reuseport && uid_eq(uid, sock_i_uid(sk2)) &&
		    inet_rcv_saddr_equal(sk, sk2, false))
			return reuseport_add_sock(sk, sk2,
						  inet_rcv_saddr_any(sk));
	}

	return reuseport_alloc(sk, inet_rcv_saddr_any(sk));
}

int __inet_hash(struct sock *sk, struct sock *osk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct inet_listen_hashbucket *ilb;
	int err = 0;

	if (sk->sk_state != TCP_LISTEN) {
		inet_ehash_nolisten(sk, osk);
		return 0;
	}
	WARN_ON(!sk_unhashed(sk));
	ilb = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];

	spin_lock(&ilb->lock);
	if (sk->sk_reuseport) {
		err = inet_reuseport_add_sock(sk, ilb);
		if (err)
			goto unlock;
	}
	if (IS_ENABLED(CONFIG_IPV6) && sk->sk_reuseport &&
		sk->sk_family == AF_INET6)
		__sk_nulls_add_node_tail_rcu(sk, &ilb->nulls_head);
	else
		__sk_nulls_add_node_rcu(sk, &ilb->nulls_head);
	inet_hash2(hashinfo, sk);
	ilb->count++;
	sock_set_flag(sk, SOCK_RCU_FREE);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
unlock:
	spin_unlock(&ilb->lock);

	return err;
}
EXPORT_SYMBOL(__inet_hash);

int inet_hash(struct sock *sk)
{
	int err = 0;

	if (sk->sk_state != TCP_CLOSE) {
		local_bh_disable();
		err = __inet_hash(sk, NULL);
		local_bh_enable();
	}

	return err;
}
EXPORT_SYMBOL_GPL(inet_hash);

void inet_unhash(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct inet_listen_hashbucket *ilb = NULL;
	spinlock_t *lock;

	if (sk_unhashed(sk))
		return;

	if (sk->sk_state == TCP_LISTEN) {
		ilb = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
		lock = &ilb->lock;
	} else {
		lock = inet_ehash_lockp(hashinfo, sk->sk_hash);
	}
	spin_lock_bh(lock);
	if (sk_unhashed(sk))
		goto unlock;

	if (rcu_access_pointer(sk->sk_reuseport_cb))
		reuseport_detach_sock(sk);
	if (ilb) {
		inet_unhash2(hashinfo, sk);
		ilb->count--;
	}
	__sk_nulls_del_node_init_rcu(sk);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
unlock:
	spin_unlock_bh(lock);
}
EXPORT_SYMBOL_GPL(inet_unhash);

int __inet_hash_connect(struct inet_timewait_death_row *death_row,
		struct sock *sk, u32 port_offset,
		int (*check_established)(struct inet_timewait_death_row *,
			struct sock *, __u16, struct inet_timewait_sock **))
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_timewait_sock *tw = NULL;
	struct inet_bind_hashbucket *head;
	int port = inet_sk(sk)->inet_num;
	struct net *net = sock_net(sk);
	struct inet_bind_bucket *tb;
	u32 remaining, offset;
	int ret, i, low, high;
	static u32 hint;
	int l3mdev;

	if (port) {
		head = &hinfo->bhash[inet_bhashfn(net, port,
						  hinfo->bhash_size)];
		tb = inet_csk(sk)->icsk_bind_hash;
		spin_lock_bh(&head->lock);
		if (sk_head(&tb->owners) == sk && !sk->sk_bind_node.next) {
			inet_ehash_nolisten(sk, NULL);
			spin_unlock_bh(&head->lock);
			return 0;
		}
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = check_established(death_row, sk, port, NULL);
		local_bh_enable();
		return ret;
	}

	l3mdev = inet_sk_bound_l3mdev(sk);

	inet_get_local_port_range(net, &low, &high);
	high++; /* [32768, 60999] -> [32768, 61000[ */
	remaining = high - low;
	if (likely(remaining > 1))
		remaining &= ~1U;

	offset = (hint + port_offset) % remaining;
	/* In first pass we try ports of @low parity.
	 * inet_csk_get_port() does the opposite choice.
	 */
	offset &= ~1U;
other_parity_scan:
	port = low + offset;
	for (i = 0; i < remaining; i += 2, port += 2) {
		if (unlikely(port >= high))
			port -= remaining;
		if (inet_is_local_reserved_port(net, port))
			continue;
		head = &hinfo->bhash[inet_bhashfn(net, port,
						  hinfo->bhash_size)];
		spin_lock_bh(&head->lock);

		/* Does not bother with rcv_saddr checks, because
		 * the established check is already unique enough.
		 */
		inet_bind_bucket_for_each(tb, &head->chain) {
			if (net_eq(ib_net(tb), net) && tb->l3mdev == l3mdev &&
			    tb->port == port) {
				if (tb->fastreuse >= 0 ||
				    tb->fastreuseport >= 0)
					goto next_port;
				WARN_ON(hlist_empty(&tb->owners));
				if (!check_established(death_row, sk,
						       port, &tw))
					goto ok;
				goto next_port;
			}
		}

		tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
					     net, head, port, l3mdev);
		if (!tb) {
			spin_unlock_bh(&head->lock);
			return -ENOMEM;
		}
		tb->fastreuse = -1;
		tb->fastreuseport = -1;
		goto ok;
next_port:
		spin_unlock_bh(&head->lock);
		cond_resched();
	}

	offset++;
	if ((offset & 1) && remaining > 1)
		goto other_parity_scan;

	return -EADDRNOTAVAIL;

ok:
	hint += i + 2;

	/* Head lock still held and bh's disabled */
	inet_bind_hash(sk, tb, port);
	if (sk_unhashed(sk)) {
		inet_sk(sk)->inet_sport = htons(port);
		inet_ehash_nolisten(sk, (struct sock *)tw);
	}
	if (tw)
		inet_twsk_bind_unhash(tw, hinfo);
	spin_unlock(&head->lock);
	if (tw)
		inet_twsk_deschedule_put(tw);
	local_bh_enable();
	return 0;
}

/*
 * Bind a port for a connect operation and hash it.
 */
int inet_hash_connect(struct inet_timewait_death_row *death_row,
		      struct sock *sk)
{
	u32 port_offset = 0;

	if (!inet_sk(sk)->inet_num)
		port_offset = inet_sk_port_offset(sk);
	return __inet_hash_connect(death_row, sk, port_offset,
				   __inet_check_established);
}
EXPORT_SYMBOL_GPL(inet_hash_connect);

void inet_hashinfo_init(struct inet_hashinfo *h)
{
	int i;

	for (i = 0; i < INET_LHTABLE_SIZE; i++) {
		spin_lock_init(&h->listening_hash[i].lock);
		INIT_HLIST_NULLS_HEAD(&h->listening_hash[i].nulls_head,
				      i + LISTENING_NULLS_BASE);
		h->listening_hash[i].count = 0;
	}

	h->lhash2 = NULL;
}
EXPORT_SYMBOL_GPL(inet_hashinfo_init);

static void init_hashinfo_lhash2(struct inet_hashinfo *h)
{
	int i;

	for (i = 0; i <= h->lhash2_mask; i++) {
		spin_lock_init(&h->lhash2[i].lock);
		INIT_HLIST_HEAD(&h->lhash2[i].head);
		h->lhash2[i].count = 0;
	}
}

void __init inet_hashinfo2_init(struct inet_hashinfo *h, const char *name,
				unsigned long numentries, int scale,
				unsigned long low_limit,
				unsigned long high_limit)
{
	h->lhash2 = alloc_large_system_hash(name,
					    sizeof(*h->lhash2),
					    numentries,
					    scale,
					    0,
					    NULL,
					    &h->lhash2_mask,
					    low_limit,
					    high_limit);
	init_hashinfo_lhash2(h);
}

int inet_hashinfo2_init_mod(struct inet_hashinfo *h)
{
	h->lhash2 = kmalloc_array(INET_LHTABLE_SIZE, sizeof(*h->lhash2), GFP_KERNEL);
	if (!h->lhash2)
		return -ENOMEM;

	h->lhash2_mask = INET_LHTABLE_SIZE - 1;
	/* INET_LHTABLE_SIZE must be a power of 2 */
	BUG_ON(INET_LHTABLE_SIZE & h->lhash2_mask);

	init_hashinfo_lhash2(h);
	return 0;
}
EXPORT_SYMBOL_GPL(inet_hashinfo2_init_mod);

int inet_ehash_locks_alloc(struct inet_hashinfo *hashinfo)
{
	unsigned int locksz = sizeof(spinlock_t);
	unsigned int i, nblocks = 1;

	if (locksz != 0) {
		/* allocate 2 cache lines or at least one spinlock per cpu */
		nblocks = max(2U * L1_CACHE_BYTES / locksz, 1U);
		nblocks = roundup_pow_of_two(nblocks * num_possible_cpus());

		/* no more locks than number of hash buckets */
		nblocks = min(nblocks, hashinfo->ehash_mask + 1);

		hashinfo->ehash_locks = kvmalloc_array(nblocks, locksz, GFP_KERNEL);
		if (!hashinfo->ehash_locks)
			return -ENOMEM;

		for (i = 0; i < nblocks; i++)
			spin_lock_init(&hashinfo->ehash_locks[i]);
	}
	hashinfo->ehash_locks_mask = nblocks - 1;
	return 0;
}
EXPORT_SYMBOL_GPL(inet_ehash_locks_alloc);
