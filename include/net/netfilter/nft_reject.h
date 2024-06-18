/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NFT_REJECT_H_
#define _NFT_REJECT_H_

#include <linux/types.h>
#include <net/netlink.h>
#include <net/netfilter/nf_tables.h>
#include <uapi/linux/netfilter/nf_tables.h>

struct nft_reject {
	enum nft_reject_types	type:8;
	u8			icmp_code;
};

extern const struct nla_policy nft_reject_policy[];

int nft_reject_validate(const struct nft_ctx *ctx,
			const struct nft_expr *expr,
			const struct nft_data **data);

int nft_reject_init(const struct nft_ctx *ctx,
		    const struct nft_expr *expr,
		    const struct nlattr * const tb[]);

int nft_reject_dump(struct sk_buff *skb,
		    const struct nft_expr *expr, bool reset);

int nft_reject_icmp_code(u8 code);
int nft_reject_icmpv6_code(u8 code);

#endif
