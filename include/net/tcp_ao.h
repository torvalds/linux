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
	refcount_t		refcnt;		/* Protects twsk destruction */
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
#define TCP_AO_ESTABLISHED (TCPF_ESTABLISHED | TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2 | \
			    TCPF_CLOSE | TCPF_CLOSE_WAIT | \
			    TCPF_LAST_ACK | TCPF_CLOSING)

int tcp_ao_transmit_skb(struct sock *sk, struct sk_buff *skb,
			struct tcp_ao_key *key, struct tcphdr *th,
			__u8 *hash_location);
int tcp_ao_hash_skb(unsigned short int family,
		    char *ao_hash, struct tcp_ao_key *key,
		    const struct sock *sk, const struct sk_buff *skb,
		    const u8 *tkey, int hash_offset, u32 sne);
int tcp_parse_ao(struct sock *sk, int cmd, unsigned short int family,
		 sockptr_t optval, int optlen);
struct tcp_ao_key *tcp_ao_established_key(struct tcp_ao_info *ao,
					  int sndid, int rcvid);
int tcp_ao_copy_all_matching(const struct sock *sk, struct sock *newsk,
			     struct request_sock *req, struct sk_buff *skb,
			     int family);
int tcp_ao_calc_traffic_key(struct tcp_ao_key *mkt, u8 *key, void *ctx,
			    unsigned int len, struct tcp_sigpool *hp);
void tcp_ao_destroy_sock(struct sock *sk, bool twsk);
void tcp_ao_time_wait(struct tcp_timewait_sock *tcptw, struct tcp_sock *tp);
enum skb_drop_reason tcp_inbound_ao_hash(struct sock *sk,
			const struct sk_buff *skb, unsigned short int family,
			const struct request_sock *req,
			const struct tcp_ao_hdr *aoh);
struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
				    const union tcp_ao_addr *addr,
				    int family, int sndid, int rcvid);
int tcp_ao_hash_hdr(unsigned short family, char *ao_hash,
		    struct tcp_ao_key *key, const u8 *tkey,
		    const union tcp_ao_addr *daddr,
		    const union tcp_ao_addr *saddr,
		    const struct tcphdr *th, u32 sne);
int tcp_ao_prepare_reset(const struct sock *sk, struct sk_buff *skb,
			 const struct tcp_ao_hdr *aoh, int l3index,
			 struct tcp_ao_key **key, char **traffic_key,
			 bool *allocated_traffic_key, u8 *keyid, u32 *sne);

/* ipv4 specific functions */
int tcp_v4_parse_ao(struct sock *sk, int cmd, sockptr_t optval, int optlen);
struct tcp_ao_key *tcp_v4_ao_lookup(const struct sock *sk, struct sock *addr_sk,
				    int sndid, int rcvid);
int tcp_v4_ao_synack_hash(char *ao_hash, struct tcp_ao_key *mkt,
			  struct request_sock *req, const struct sk_buff *skb,
			  int hash_offset, u32 sne);
int tcp_v4_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk,
			  __be32 sisn, __be32 disn, bool send);
int tcp_v4_ao_calc_key_rsk(struct tcp_ao_key *mkt, u8 *key,
			   struct request_sock *req);
struct tcp_ao_key *tcp_v4_ao_lookup_rsk(const struct sock *sk,
					struct request_sock *req,
					int sndid, int rcvid);
int tcp_v4_ao_hash_skb(char *ao_hash, struct tcp_ao_key *key,
		       const struct sock *sk, const struct sk_buff *skb,
		       const u8 *tkey, int hash_offset, u32 sne);
/* ipv6 specific functions */
int tcp_v6_ao_hash_pseudoheader(struct tcp_sigpool *hp,
				const struct in6_addr *daddr,
				const struct in6_addr *saddr, int nbytes);
int tcp_v6_ao_calc_key_skb(struct tcp_ao_key *mkt, u8 *key,
			   const struct sk_buff *skb, __be32 sisn, __be32 disn);
int tcp_v6_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk, __be32 sisn,
			  __be32 disn, bool send);
int tcp_v6_ao_calc_key_rsk(struct tcp_ao_key *mkt, u8 *key,
			   struct request_sock *req);
struct tcp_ao_key *tcp_v6_ao_do_lookup(const struct sock *sk,
				       const struct in6_addr *addr,
				       int sndid, int rcvid);
struct tcp_ao_key *tcp_v6_ao_lookup(const struct sock *sk,
				    struct sock *addr_sk, int sndid, int rcvid);
struct tcp_ao_key *tcp_v6_ao_lookup_rsk(const struct sock *sk,
					struct request_sock *req,
					int sndid, int rcvid);
int tcp_v6_ao_hash_skb(char *ao_hash, struct tcp_ao_key *key,
		       const struct sock *sk, const struct sk_buff *skb,
		       const u8 *tkey, int hash_offset, u32 sne);
int tcp_v6_parse_ao(struct sock *sk, int cmd, sockptr_t optval, int optlen);
int tcp_v6_ao_synack_hash(char *ao_hash, struct tcp_ao_key *ao_key,
			  struct request_sock *req, const struct sk_buff *skb,
			  int hash_offset, u32 sne);
void tcp_ao_established(struct sock *sk);
void tcp_ao_finish_connect(struct sock *sk, struct sk_buff *skb);
void tcp_ao_connect_init(struct sock *sk);
void tcp_ao_syncookie(struct sock *sk, const struct sk_buff *skb,
		      struct tcp_request_sock *treq,
		      unsigned short int family);
#else /* CONFIG_TCP_AO */

static inline int tcp_ao_transmit_skb(struct sock *sk, struct sk_buff *skb,
				      struct tcp_ao_key *key, struct tcphdr *th,
				      __u8 *hash_location)
{
	return 0;
}

static inline void tcp_ao_syncookie(struct sock *sk, const struct sk_buff *skb,
				    struct tcp_request_sock *treq,
				    unsigned short int family)
{
}

static inline enum skb_drop_reason tcp_inbound_ao_hash(struct sock *sk,
		const struct sk_buff *skb, unsigned short int family,
		const struct request_sock *req, const struct tcp_ao_hdr *aoh)
{
	return SKB_NOT_DROPPED_YET;
}

static inline struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
		const union tcp_ao_addr *addr, int family, int sndid, int rcvid)
{
	return NULL;
}

static inline void tcp_ao_destroy_sock(struct sock *sk, bool twsk)
{
}

static inline void tcp_ao_established(struct sock *sk)
{
}

static inline void tcp_ao_finish_connect(struct sock *sk, struct sk_buff *skb)
{
}

static inline void tcp_ao_time_wait(struct tcp_timewait_sock *tcptw,
				    struct tcp_sock *tp)
{
}

static inline void tcp_ao_connect_init(struct sock *sk)
{
}
#endif

#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
int tcp_do_parse_auth_options(const struct tcphdr *th,
			      const u8 **md5_hash, const u8 **ao_hash);
#else
static inline int tcp_do_parse_auth_options(const struct tcphdr *th,
		const u8 **md5_hash, const u8 **ao_hash)
{
	*md5_hash = NULL;
	*ao_hash = NULL;
	return 0;
}
#endif

#endif /* _TCP_AO_H */
