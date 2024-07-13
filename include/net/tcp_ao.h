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

struct tcp_ao_counters {
	atomic64_t	pkt_good;
	atomic64_t	pkt_bad;
	atomic64_t	key_not_found;
	atomic64_t	ao_required;
	atomic64_t	dropped_icmp;
};

struct tcp_ao_key {
	struct hlist_node	node;
	union tcp_ao_addr	addr;
	u8			key[TCP_AO_MAXKEYLEN] __tcp_ao_key_align;
	unsigned int		tcp_sigpool_id;
	unsigned int		digest_size;
	int			l3index;
	u8			prefixlen;
	u8			family;
	u8			keylen;
	u8			keyflags;
	u8			sndid;
	u8			rcvid;
	u8			maclen;
	struct rcu_head		rcu;
	atomic64_t		pkt_good;
	atomic64_t		pkt_bad;
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

/* Use tcp_ao_len_aligned() for TCP header calculations */
static inline int tcp_ao_len(const struct tcp_ao_key *key)
{
	return tcp_ao_maclen(key) + sizeof(struct tcp_ao_hdr);
}

static inline int tcp_ao_len_aligned(const struct tcp_ao_key *key)
{
	return round_up(tcp_ao_len(key), 4);
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
	/* current_key and rnext_key are maintained on sockets
	 * in TCP_AO_ESTABLISHED states.
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
	struct tcp_ao_counters	counters;
	u32			ao_required	:1,
				accept_icmps	:1,
				__unused	:30;
	__be32			lisn;
	__be32			risn;
	/* Sequence Number Extension (SNE) are upper 4 bytes for SEQ,
	 * that protect TCP-AO connection from replayed old TCP segments.
	 * See RFC5925 (6.2).
	 * In order to get correct SNE, there's a helper tcp_ao_compute_sne().
	 * It needs SEQ basis to understand whereabouts are lower SEQ numbers.
	 * According to that basis vector, it can provide incremented SNE
	 * when SEQ rolls over or provide decremented SNE when there's
	 * a retransmitted segment from before-rolling over.
	 * - for request sockets such basis is rcv_isn/snt_isn, which seems
	 *   good enough as it's unexpected to receive 4 Gbytes on reqsk.
	 * - for full sockets the basis is rcv_nxt/snd_una. snd_una is
	 *   taken instead of snd_nxt as currently it's easier to track
	 *   in tcp_snd_una_update(), rather than updating SNE in all
	 *   WRITE_ONCE(tp->snd_nxt, ...)
	 * - for time-wait sockets the basis is tw_rcv_nxt/tw_snd_nxt.
	 *   tw_snd_nxt is not expected to change, while tw_rcv_nxt may.
	 */
	u32			snd_sne;
	u32			rcv_sne;
	refcount_t		refcnt;		/* Protects twsk destruction */
	struct rcu_head		rcu;
};

#ifdef CONFIG_TCP_MD5SIG
#include <linux/jump_label.h>
extern struct static_key_false_deferred tcp_md5_needed;
#define static_branch_tcp_md5()	static_branch_unlikely(&tcp_md5_needed.key)
#else
#define static_branch_tcp_md5()	false
#endif
#ifdef CONFIG_TCP_AO
/* TCP-AO structures and functions */
#include <linux/jump_label.h>
extern struct static_key_false_deferred tcp_ao_needed;
#define static_branch_tcp_ao()	static_branch_unlikely(&tcp_ao_needed.key)
#else
#define static_branch_tcp_ao()	false
#endif

static inline bool tcp_hash_should_produce_warnings(void)
{
	return static_branch_tcp_md5() || static_branch_tcp_ao();
}

#define tcp_hash_fail(msg, family, skb, fmt, ...)			\
do {									\
	const struct tcphdr *th = tcp_hdr(skb);				\
	char hdr_flags[6];						\
	char *f = hdr_flags;						\
									\
	if (!tcp_hash_should_produce_warnings())			\
		break;							\
	if (th->fin)							\
		*f++ = 'F';						\
	if (th->syn)							\
		*f++ = 'S';						\
	if (th->rst)							\
		*f++ = 'R';						\
	if (th->psh)							\
		*f++ = 'P';						\
	if (th->ack)							\
		*f++ = '.';						\
	*f = 0;								\
	if ((family) == AF_INET) {					\
		net_info_ratelimited("%s for %pI4.%d->%pI4.%d [%s] " fmt "\n", \
				msg, &ip_hdr(skb)->saddr, ntohs(th->source), \
				&ip_hdr(skb)->daddr, ntohs(th->dest),	\
				hdr_flags, ##__VA_ARGS__);		\
	} else {							\
		net_info_ratelimited("%s for [%pI6c].%d->[%pI6c].%d [%s]" fmt "\n", \
				msg, &ipv6_hdr(skb)->saddr, ntohs(th->source), \
				&ipv6_hdr(skb)->daddr, ntohs(th->dest),	\
				hdr_flags, ##__VA_ARGS__);		\
	}								\
} while (0)

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
/* Established states are fast-path and there always is current_key/rnext_key */
#define TCP_AO_ESTABLISHED (TCPF_ESTABLISHED | TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2 | \
			    TCPF_CLOSE_WAIT | TCPF_LAST_ACK | TCPF_CLOSING)

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
bool tcp_ao_ignore_icmp(const struct sock *sk, int family, int type, int code);
int tcp_ao_get_mkts(struct sock *sk, sockptr_t optval, sockptr_t optlen);
int tcp_ao_get_sock_info(struct sock *sk, sockptr_t optval, sockptr_t optlen);
int tcp_ao_get_repair(struct sock *sk, sockptr_t optval, sockptr_t optlen);
int tcp_ao_set_repair(struct sock *sk, sockptr_t optval, unsigned int optlen);
enum skb_drop_reason tcp_inbound_ao_hash(struct sock *sk,
			const struct sk_buff *skb, unsigned short int family,
			const struct request_sock *req, int l3index,
			const struct tcp_ao_hdr *aoh);
u32 tcp_ao_compute_sne(u32 next_sne, u32 next_seq, u32 seq);
struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk, int l3index,
				    const union tcp_ao_addr *addr,
				    int family, int sndid, int rcvid);
int tcp_ao_hash_hdr(unsigned short family, char *ao_hash,
		    struct tcp_ao_key *key, const u8 *tkey,
		    const union tcp_ao_addr *daddr,
		    const union tcp_ao_addr *saddr,
		    const struct tcphdr *th, u32 sne);
int tcp_ao_prepare_reset(const struct sock *sk, struct sk_buff *skb,
			 const struct tcp_ao_hdr *aoh, int l3index, u32 seq,
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
		      struct request_sock *req, unsigned short int family);
#else /* CONFIG_TCP_AO */

static inline int tcp_ao_transmit_skb(struct sock *sk, struct sk_buff *skb,
				      struct tcp_ao_key *key, struct tcphdr *th,
				      __u8 *hash_location)
{
	return 0;
}

static inline void tcp_ao_syncookie(struct sock *sk, const struct sk_buff *skb,
				    struct request_sock *req, unsigned short int family)
{
}

static inline bool tcp_ao_ignore_icmp(const struct sock *sk, int family,
				      int type, int code)
{
	return false;
}

static inline enum skb_drop_reason tcp_inbound_ao_hash(struct sock *sk,
		const struct sk_buff *skb, unsigned short int family,
		const struct request_sock *req, int l3index,
		const struct tcp_ao_hdr *aoh)
{
	return SKB_NOT_DROPPED_YET;
}

static inline struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
		int l3index, const union tcp_ao_addr *addr,
		int family, int sndid, int rcvid)
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

static inline int tcp_ao_get_mkts(struct sock *sk, sockptr_t optval, sockptr_t optlen)
{
	return -ENOPROTOOPT;
}

static inline int tcp_ao_get_sock_info(struct sock *sk, sockptr_t optval, sockptr_t optlen)
{
	return -ENOPROTOOPT;
}

static inline int tcp_ao_get_repair(struct sock *sk,
				    sockptr_t optval, sockptr_t optlen)
{
	return -ENOPROTOOPT;
}

static inline int tcp_ao_set_repair(struct sock *sk,
				    sockptr_t optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
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
