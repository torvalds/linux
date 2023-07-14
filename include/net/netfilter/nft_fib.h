/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NFT_FIB_H_
#define _NFT_FIB_H_

#include <net/netfilter/nf_tables.h>

struct nft_fib {
	u8			dreg;
	u8			result;
	u32			flags;
};

extern const struct nla_policy nft_fib_policy[];

static inline bool
nft_fib_is_loopback(const struct sk_buff *skb, const struct net_device *in)
{
	return skb->pkt_type == PACKET_LOOPBACK || in->flags & IFF_LOOPBACK;
}

int nft_fib_dump(struct sk_buff *skb, const struct nft_expr *expr, bool reset);
int nft_fib_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
		 const struct nlattr * const tb[]);
int nft_fib_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
		     const struct nft_data **data);


void nft_fib4_eval_type(const struct nft_expr *expr, struct nft_regs *regs,
			const struct nft_pktinfo *pkt);
void nft_fib4_eval(const struct nft_expr *expr, struct nft_regs *regs,
		   const struct nft_pktinfo *pkt);

void nft_fib6_eval_type(const struct nft_expr *expr, struct nft_regs *regs,
			const struct nft_pktinfo *pkt);
void nft_fib6_eval(const struct nft_expr *expr, struct nft_regs *regs,
		   const struct nft_pktinfo *pkt);

void nft_fib_store_result(void *reg, const struct nft_fib *priv,
			  const struct net_device *dev);

bool nft_fib_reduce(struct nft_regs_track *track,
		    const struct nft_expr *expr);
#endif
