#ifndef _NFT_FIB_H_
#define _NFT_FIB_H_

struct nft_fib {
	enum nft_registers	dreg:8;
	u8			result;
	u32			flags;
};

extern const struct nla_policy nft_fib_policy[];

int nft_fib_dump(struct sk_buff *skb, const struct nft_expr *expr);
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

void nft_fib_store_result(void *reg, enum nft_fib_result r,
			  const struct nft_pktinfo *pkt, int index);
#endif
