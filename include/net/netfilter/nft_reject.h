#ifndef _NFT_REJECT_H_
#define _NFT_REJECT_H_

struct nft_reject {
	enum nft_reject_types	type:8;
	u8			icmp_code;
};

extern const struct nla_policy nft_reject_policy[];

int nft_reject_init(const struct nft_ctx *ctx,
		    const struct nft_expr *expr,
		    const struct nlattr * const tb[]);

int nft_reject_dump(struct sk_buff *skb, const struct nft_expr *expr);

#endif
