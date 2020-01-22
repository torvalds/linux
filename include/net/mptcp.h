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

/* MPTCP sk_buff extension data */
struct mptcp_ext {
	u64		data_ack;
	u64		data_seq;
	u32		subflow_seq;
	u16		data_len;
	u8		use_map:1,
			dsn64:1,
			data_fin:1,
			use_ack:1,
			ack64:1,
			__unused:3;
	/* one byte hole */
};

struct mptcp_out_options {
#if IS_ENABLED(CONFIG_MPTCP)
	u16 suboptions;
	u64 sndr_key;
	u64 rcvr_key;
#endif
};

#ifdef CONFIG_MPTCP

void mptcp_init(void);

void mptcp_parse_option(const unsigned char *ptr, int opsize,
			struct tcp_options_received *opt_rx);
void mptcp_write_options(__be32 *ptr, struct mptcp_out_options *opts);

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

#else

static inline void mptcp_init(void)
{
}

static inline void mptcp_parse_option(const unsigned char *ptr, int opsize,
				      struct tcp_options_received *opt_rx)
{
}

static inline void mptcp_skb_ext_move(struct sk_buff *to,
				      const struct sk_buff *from)
{
}

static inline bool mptcp_skb_can_collapse(const struct sk_buff *to,
					  const struct sk_buff *from)
{
	return true;
}

#endif /* CONFIG_MPTCP */

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int mptcpv6_init(void);
#elif IS_ENABLED(CONFIG_IPV6)
static inline int mptcpv6_init(void)
{
	return 0;
}
#endif

#endif /* __NET_MPTCP_H */
