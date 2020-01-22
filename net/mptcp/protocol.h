/* SPDX-License-Identifier: GPL-2.0 */
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#ifndef __MPTCP_PROTOCOL_H
#define __MPTCP_PROTOCOL_H

#include <linux/random.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>

#define MPTCP_SUPPORTED_VERSION	0

/* MPTCP option bits */
#define OPTION_MPTCP_MPC_SYN	BIT(0)
#define OPTION_MPTCP_MPC_SYNACK	BIT(1)
#define OPTION_MPTCP_MPC_ACK	BIT(2)

/* MPTCP option subtypes */
#define MPTCPOPT_MP_CAPABLE	0
#define MPTCPOPT_MP_JOIN	1
#define MPTCPOPT_DSS		2
#define MPTCPOPT_ADD_ADDR	3
#define MPTCPOPT_RM_ADDR	4
#define MPTCPOPT_MP_PRIO	5
#define MPTCPOPT_MP_FAIL	6
#define MPTCPOPT_MP_FASTCLOSE	7

/* MPTCP suboption lengths */
#define TCPOLEN_MPTCP_MPC_SYN		12
#define TCPOLEN_MPTCP_MPC_SYNACK	12
#define TCPOLEN_MPTCP_MPC_ACK		20
#define TCPOLEN_MPTCP_DSS_BASE		4
#define TCPOLEN_MPTCP_DSS_ACK32		4
#define TCPOLEN_MPTCP_DSS_ACK64		8
#define TCPOLEN_MPTCP_DSS_MAP32		10
#define TCPOLEN_MPTCP_DSS_MAP64		14
#define TCPOLEN_MPTCP_DSS_CHECKSUM	2

/* MPTCP MP_CAPABLE flags */
#define MPTCP_VERSION_MASK	(0x0F)
#define MPTCP_CAP_CHECKSUM_REQD	BIT(7)
#define MPTCP_CAP_EXTENSIBILITY	BIT(6)
#define MPTCP_CAP_HMAC_SHA1	BIT(0)
#define MPTCP_CAP_FLAG_MASK	(0x3F)

/* MPTCP DSS flags */
#define MPTCP_DSS_DATA_FIN	BIT(4)
#define MPTCP_DSS_DSN64		BIT(3)
#define MPTCP_DSS_HAS_MAP	BIT(2)
#define MPTCP_DSS_ACK64		BIT(1)
#define MPTCP_DSS_HAS_ACK	BIT(0)
#define MPTCP_DSS_FLAG_MASK	(0x1F)

/* MPTCP socket flags */
#define MPTCP_DATA_READY	BIT(0)
#define MPTCP_SEND_SPACE	BIT(1)

/* MPTCP connection sock */
struct mptcp_sock {
	/* inet_connection_sock must be the first member */
	struct inet_connection_sock sk;
	u64		local_key;
	u64		remote_key;
	u64		write_seq;
	u64		ack_seq;
	u32		token;
	unsigned long	flags;
	struct list_head conn_list;
	struct skb_ext	*cached_ext;	/* for the next sendmsg */
	struct socket	*subflow; /* outgoing connect/listener/!mp_capable */
};

#define mptcp_for_each_subflow(__msk, __subflow)			\
	list_for_each_entry(__subflow, &((__msk)->conn_list), node)

static inline struct mptcp_sock *mptcp_sk(const struct sock *sk)
{
	return (struct mptcp_sock *)sk;
}

struct mptcp_subflow_request_sock {
	struct	tcp_request_sock sk;
	u8	mp_capable : 1,
		mp_join : 1,
		backup : 1;
	u64	local_key;
	u64	remote_key;
	u64	idsn;
	u32	token;
	u32	ssn_offset;
};

static inline struct mptcp_subflow_request_sock *
mptcp_subflow_rsk(const struct request_sock *rsk)
{
	return (struct mptcp_subflow_request_sock *)rsk;
}

/* MPTCP subflow context */
struct mptcp_subflow_context {
	struct	list_head node;/* conn_list of subflows */
	u64	local_key;
	u64	remote_key;
	u64	idsn;
	u64	map_seq;
	u32	token;
	u32	rel_write_seq;
	u32	map_subflow_seq;
	u32	ssn_offset;
	u32	map_data_len;
	u32	request_mptcp : 1,  /* send MP_CAPABLE */
		mp_capable : 1,	    /* remote is MPTCP capable */
		fourth_ack : 1,	    /* send initial DSS */
		conn_finished : 1,
		map_valid : 1,
		data_avail : 1,
		rx_eof : 1;

	struct	sock *tcp_sock;	    /* tcp sk backpointer */
	struct	sock *conn;	    /* parent mptcp_sock */
	const	struct inet_connection_sock_af_ops *icsk_af_ops;
	void	(*tcp_data_ready)(struct sock *sk);
	void	(*tcp_state_change)(struct sock *sk);
	void	(*tcp_write_space)(struct sock *sk);

	struct	rcu_head rcu;
};

static inline struct mptcp_subflow_context *
mptcp_subflow_ctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	/* Use RCU on icsk_ulp_data only for sock diag code */
	return (__force struct mptcp_subflow_context *)icsk->icsk_ulp_data;
}

static inline struct sock *
mptcp_subflow_tcp_sock(const struct mptcp_subflow_context *subflow)
{
	return subflow->tcp_sock;
}

static inline u64
mptcp_subflow_get_map_offset(const struct mptcp_subflow_context *subflow)
{
	return tcp_sk(mptcp_subflow_tcp_sock(subflow))->copied_seq -
		      subflow->ssn_offset -
		      subflow->map_subflow_seq;
}

static inline u64
mptcp_subflow_get_mapped_dsn(const struct mptcp_subflow_context *subflow)
{
	return subflow->map_seq + mptcp_subflow_get_map_offset(subflow);
}

int mptcp_is_enabled(struct net *net);
bool mptcp_subflow_data_available(struct sock *sk);
void mptcp_subflow_init(void);
int mptcp_subflow_create_socket(struct sock *sk, struct socket **new_sock);

static inline void mptcp_subflow_tcp_fallback(struct sock *sk,
					      struct mptcp_subflow_context *ctx)
{
	sk->sk_data_ready = ctx->tcp_data_ready;
	sk->sk_state_change = ctx->tcp_state_change;
	sk->sk_write_space = ctx->tcp_write_space;

	inet_csk(sk)->icsk_af_ops = ctx->icsk_af_ops;
}

extern const struct inet_connection_sock_af_ops ipv4_specific;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
extern const struct inet_connection_sock_af_ops ipv6_specific;
#endif

void mptcp_proto_init(void);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int mptcp_proto_v6_init(void);
#endif

struct mptcp_read_arg {
	struct msghdr *msg;
};

int mptcp_read_actor(read_descriptor_t *desc, struct sk_buff *skb,
		     unsigned int offset, size_t len);

void mptcp_get_options(const struct sk_buff *skb,
		       struct tcp_options_received *opt_rx);

void mptcp_finish_connect(struct sock *sk);

int mptcp_token_new_request(struct request_sock *req);
void mptcp_token_destroy_request(u32 token);
int mptcp_token_new_connect(struct sock *sk);
int mptcp_token_new_accept(u32 token);
void mptcp_token_update_accept(struct sock *sk, struct sock *conn);
void mptcp_token_destroy(u32 token);

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn);
static inline void mptcp_crypto_key_gen_sha(u64 *key, u32 *token, u64 *idsn)
{
	/* we might consider a faster version that computes the key as a
	 * hash of some information available in the MPTCP socket. Use
	 * random data at the moment, as it's probably the safest option
	 * in case multiple sockets are opened in different namespaces at
	 * the same time.
	 */
	get_random_bytes(key, sizeof(u64));
	mptcp_crypto_key_sha(*key, token, idsn);
}

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u32 nonce1, u32 nonce2,
			   u32 *hash_out);

static inline struct mptcp_ext *mptcp_get_ext(struct sk_buff *skb)
{
	return (struct mptcp_ext *)skb_ext_find(skb, SKB_EXT_MPTCP);
}

static inline bool before64(__u64 seq1, __u64 seq2)
{
	return (__s64)(seq1 - seq2) < 0;
}

#define after64(seq2, seq1)	before64(seq1, seq2)

#endif /* __MPTCP_PROTOCOL_H */
