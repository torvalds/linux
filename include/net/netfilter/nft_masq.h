#ifndef _NFT_MASQ_H_
#define _NFT_MASQ_H_

struct nft_masq {
	u32	flags;
};

extern const struct nla_policy nft_masq_policy[];

int nft_masq_init(const struct nft_ctx *ctx,
		  const struct nft_expr *expr,
		  const struct nlattr * const tb[]);

int nft_masq_dump(struct sk_buff *skb, const struct nft_expr *expr);

int nft_masq_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
		      const struct nft_data **data);

#endif /* _NFT_MASQ_H_ */
