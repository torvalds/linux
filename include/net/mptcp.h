/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#ifndef __NET_MPTCP_H
#define __NET_MPTCP_H

#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/types.h>

struct seq_file;

/* MPTCP sk_buff extension data */
struct mptcp_ext {
	union {
		u64	data_ack;
		u32	data_ack32;
	};
	u64		data_seq;
	u32		subflow_seq;
	u16		data_len;
	u8		use_map:1,
			dsn64:1,
			data_fin:1,
			use_ack:1,
			ack64:1,
			mpc_map:1,
			frozen:1,
			reset_transient:1;
	u8		reset_reason:4;
};

#define MPTCP_RM_IDS_MAX	8

struct mptcp_rm_list {
	u8 ids[MPTCP_RM_IDS_MAX];
	u8 nr;
};

struct mptcp_addr_info {
	u8			id;
	sa_family_t		family;
	__be16			port;
	union {
		struct in_addr	addr;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		struct in6_addr	addr6;
#endif
	};
};

struct mptcp_out_options {
#if IS_ENABLED(CONFIG_MPTCP)
	u16 suboptions;
	u64 sndr_key;
	u64 rcvr_key;
	u64 ahmac;
	struct mptcp_addr_info addr;
	struct mptcp_rm_list rm_list;
	u8 join_id;
	u8 backup;
	u8 reset_reason:4;
	u8 reset_transient:1;
	u32 nonce;
	u64 thmac;
	u32 token;
	u8 hmac[20];
	struct mptcp_ext ext_copy;
#endif
};

#ifdef CONFIG_MPTCP
extern struct request_sock_ops mptcp_subflow_request_sock_ops;

void mptcp_init(void);

static inline bool sk_is_mptcp(const struct sock *sk)
{
	return tcp_sk(sk)->is_mptcp;
}

static inline bool rsk_is_mptcp(const struct request_sock *req)
{
	return tcp_rsk(req)->is_mptcp;
}

static inline bool rsk_drop_req(const struct request_sock *req)
{
	return tcp_rsk(req)->is_mptcp && tcp_rsk(req)->drop_req;
}

void mptcp_space(const struct sock *ssk, int *space, int *full_space);
bool mptcp_syn_options(struct sock *sk, const struct sk_buff *skb,
		       unsigned int *size, struct mptcp_out_options *opts);
bool mptcp_synack_options(const struct request_sock *req, unsigned int *size,
			  struct mptcp_out_options *opts);
bool mptcp_established_options(struct sock *sk, struct sk_buff *skb,
			       unsigned int *size, unsigned int remaining,
			       struct mptcp_out_options *opts);
void mptcp_incoming_options(struct sock *sk, struct sk_buff *skb);

void mptcp_write_options(__be32 *ptr, const struct tcp_sock *tp,
			 struct mptcp_out_options *opts);

/* move the skb extension owership, with the assumption that 'to' is
 * newly allocated
 */
static inline void mptcp_skb_ext_move(struct sk_buff *to,
				      struct sk_buff *from)
{
	if (!skb_ext_exist(from, SKB_EXT_MPTCP))
		return;

	if (WARN_ON_ONCE(to->active_extensions))
		skb_ext_put(to);

	to->active_extensions = from->active_extensions;
	to->extensions = from->extensions;
	from->active_extensions = 0;
}

static inline void mptcp_skb_ext_copy(struct sk_buff *to,
				      struct sk_buff *from)
{
	struct mptcp_ext *from_ext;

	from_ext = skb_ext_find(from, SKB_EXT_MPTCP);
	if (!from_ext)
		return;

	from_ext->frozen = 1;
	skb_ext_copy(to, from);
}

static inline bool mptcp_ext_matches(const struct mptcp_ext *to_ext,
				     const struct mptcp_ext *from_ext)
{
	/* MPTCP always clears the ext when adding it to the skb, so
	 * holes do not bother us here
	 */
	return !from_ext ||
	       (to_ext && from_ext &&
	        !memcmp(from_ext, to_ext, sizeof(struct mptcp_ext)));
}

/* check if skbs can be collapsed.
 * MPTCP collapse is allowed if neither @to or @from carry an mptcp data
 * mapping, or if the extension of @to is the same as @from.
 * Collapsing is not possible if @to lacks an extension, but @from carries one.
 */
static inline bool mptcp_skb_can_collapse(const struct sk_buff *to,
					  const struct sk_buff *from)
{
	return mptcp_ext_matches(skb_ext_find(to, SKB_EXT_MPTCP),
				 skb_ext_find(from, SKB_EXT_MPTCP));
}

void mptcp_seq_show(struct seq_file *seq);
int mptcp_subflow_init_cookie_req(struct request_sock *req,
				  const struct sock *sk_listener,
				  struct sk_buff *skb);

__be32 mptcp_get_reset_option(const struct sk_buff *skb);

static inline __be32 mptcp_reset_option(const struct sk_buff *skb)
{
	if (skb_ext_exist(skb, SKB_EXT_MPTCP))
		return mptcp_get_reset_option(skb);

	return htonl(0u);
}
#else

static inline void mptcp_init(void)
{
}

static inline bool sk_is_mptcp(const struct sock *sk)
{
	return false;
}

static inline bool rsk_is_mptcp(const struct request_sock *req)
{
	return false;
}

static inline bool rsk_drop_req(const struct request_sock *req)
{
	return false;
}

static inline void mptcp_parse_option(const struct sk_buff *skb,
				      const unsigned char *ptr, int opsize,
				      struct tcp_options_received *opt_rx)
{
}

static inline bool mptcp_syn_options(struct sock *sk, const struct sk_buff *skb,
				     unsigned int *size,
				     struct mptcp_out_options *opts)
{
	return false;
}

static inline bool mptcp_synack_options(const struct request_sock *req,
					unsigned int *size,
					struct mptcp_out_options *opts)
{
	return false;
}

static inline bool mptcp_established_options(struct sock *sk,
					     struct sk_buff *skb,
					     unsigned int *size,
					     unsigned int remaining,
					     struct mptcp_out_options *opts)
{
	return false;
}

static inline void mptcp_incoming_options(struct sock *sk,
					  struct sk_buff *skb)
{
}

static inline void mptcp_skb_ext_move(struct sk_buff *to,
				      const struct sk_buff *from)
{
}

static inline void mptcp_skb_ext_copy(struct sk_buff *to,
				      struct sk_buff *from)
{
}

static inline bool mptcp_skb_can_collapse(const struct sk_buff *to,
					  const struct sk_buff *from)
{
	return true;
}

static inline void mptcp_space(const struct sock *ssk, int *s, int *fs) { }
static inline void mptcp_seq_show(struct seq_file *seq) { }

static inline int mptcp_subflow_init_cookie_req(struct request_sock *req,
						const struct sock *sk_listener,
						struct sk_buff *skb)
{
	return 0; /* TCP fallback */
}

static inline __be32 mptcp_reset_option(const struct sk_buff *skb)  { return htonl(0u); }
#endif /* CONFIG_MPTCP */

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int mptcpv6_init(void);
void mptcpv6_handle_mapped(struct sock *sk, bool mapped);
#elif IS_ENABLED(CONFIG_IPV6)
static inline int mptcpv6_init(void) { return 0; }
static inline void mptcpv6_handle_mapped(struct sock *sk, bool mapped) { }
#endif

#endif /* __NET_MPTCP_H */
