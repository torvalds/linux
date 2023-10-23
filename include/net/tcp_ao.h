/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _TCP_AO_H
#define _TCP_AO_H

#define TCP_AO_KEY_ALIGN	1
#define __tcp_ao_key_align __aligned(TCP_AO_KEY_ALIGN)

union tcp_ao_addr {
	struct in_addr  a4;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr	a6;
#endif
};

struct tcp_ao_hdr {
	u8	kind;
	u8	length;
	u8	keyid;
	u8	rnext_keyid;
};

struct tcp_ao_key {
	struct hlist_node	node;
	union tcp_ao_addr	addr;
	u8			key[TCP_AO_MAXKEYLEN] __tcp_ao_key_align;
	unsigned int		tcp_sigpool_id;
	unsigned int		digest_size;
	u8			prefixlen;
	u8			family;
	u8			keylen;
	u8			keyflags;
	u8			sndid;
	u8			rcvid;
	u8			maclen;
	struct rcu_head		rcu;
	u8			traffic_keys[];
};

static inline u8 *rcv_other_key(struct tcp_ao_key *key)
{
	return key->traffic_keys;
}

static inline u8 *snd_other_key(struct tcp_ao_key *key)
{
	return key->traffic_keys + key->digest_size;
}

static inline int tcp_ao_maclen(const struct tcp_ao_key *key)
{
	return key->maclen;
}

static inline int tcp_ao_len(const struct tcp_ao_key *key)
{
	return tcp_ao_maclen(key) + sizeof(struct tcp_ao_hdr);
}

static inline unsigned int tcp_ao_digest_size(struct tcp_ao_key *key)
{
	return key->digest_size;
}

static inline int tcp_ao_sizeof_key(const struct tcp_ao_key *key)
{
	return sizeof(struct tcp_ao_key) + (key->digest_size << 1);
}

struct tcp_ao_info {
	/* List of tcp_ao_key's */
	struct hlist_head	head;
	/* current_key and rnext_key aren't maintained on listen sockets.
	 * Their purpose is to cache keys on established connections,
	 * saving needless lookups. Never dereference any of them from
	 * listen sockets.
	 * ::current_key may change in RX to the key that was requested by
	 * the peer, please use READ_ONCE()/WRITE_ONCE() in order to avoid
	 * load/store tearing.
	 * Do the same for ::rnext_key, if you don't hold socket lock
	 * (it's changed only by userspace request in setsockopt()).
	 */
	struct tcp_ao_key	*current_key;
	struct tcp_ao_key	*rnext_key;
	u32			ao_required	:1,
				__unused	:31;
	__be32			lisn;
	__be32			risn;
	struct rcu_head		rcu;
};

#ifdef CONFIG_TCP_AO
/* TCP-AO structures and functions */

struct tcp4_ao_context {
	__be32		saddr;
	__be32		daddr;
	__be16		sport;
	__be16		dport;
	__be32		sisn;
	__be32		disn;
};

struct tcp6_ao_context {
	struct in6_addr	saddr;
	struct in6_addr	daddr;
	__be16		sport;
	__be16		dport;
	__be32		sisn;
	__be32		disn;
};

struct tcp_sigpool;

int tcp_parse_ao(struct sock *sk, int cmd, unsigned short int family,
		 sockptr_t optval, int optlen);
int tcp_ao_calc_traffic_key(struct tcp_ao_key *mkt, u8 *key, void *ctx,
			    unsigned int len, struct tcp_sigpool *hp);
void tcp_ao_destroy_sock(struct sock *sk);
struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
				    const union tcp_ao_addr *addr,
				    int family, int sndid, int rcvid);
/* ipv4 specific functions */
int tcp_v4_parse_ao(struct sock *sk, int cmd, sockptr_t optval, int optlen);
struct tcp_ao_key *tcp_v4_ao_lookup(const struct sock *sk, struct sock *addr_sk,
				    int sndid, int rcvid);
int tcp_v4_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk,
			  __be32 sisn, __be32 disn, bool send);
/* ipv6 specific functions */
int tcp_v6_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk, __be32 sisn,
			  __be32 disn, bool send);
struct tcp_ao_key *tcp_v6_ao_lookup(const struct sock *sk,
				    struct sock *addr_sk, int sndid, int rcvid);
int tcp_v6_parse_ao(struct sock *sk, int cmd, sockptr_t optval, int optlen);
void tcp_ao_established(struct sock *sk);
void tcp_ao_finish_connect(struct sock *sk, struct sk_buff *skb);
void tcp_ao_connect_init(struct sock *sk);

#else /* CONFIG_TCP_AO */

static inline struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
		const union tcp_ao_addr *addr, int family, int sndid, int rcvid)
{
	return NULL;
}

static inline void tcp_ao_destroy_sock(struct sock *sk)
{
}

static inline void tcp_ao_established(struct sock *sk)
{
}

static inline void tcp_ao_finish_connect(struct sock *sk, struct sk_buff *skb)
{
}

static inline void tcp_ao_connect_init(struct sock *sk)
{
}
#endif

#endif /* _TCP_AO_H */
