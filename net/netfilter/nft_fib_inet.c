/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

#include <net/netfilter/nft_fib.h>

static void nft_fib_inet_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	const struct nft_fib *priv = nft_expr_priv(expr);

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		switch (priv->result) {
		case NFT_FIB_RESULT_OIF:
		case NFT_FIB_RESULT_OIFNAME:
			return nft_fib4_eval(expr, regs, pkt);
		case NFT_FIB_RESULT_ADDRTYPE:
			return nft_fib4_eval_type(expr, regs, pkt);
		}
		break;
	case NFPROTO_IPV6:
		switch (priv->result) {
		case NFT_FIB_RESULT_OIF:
		case NFT_FIB_RESULT_OIFNAME:
			return nft_fib6_eval(expr, regs, pkt);
		case NFT_FIB_RESULT_ADDRTYPE:
			return nft_fib6_eval_type(expr, regs, pkt);
		}
		break;
	}

	regs->verdict.code = NF_DROP;
}

static struct nft_expr_type nft_fib_inet_type;
static const struct nft_expr_ops nft_fib_inet_ops = {
	.type		= &nft_fib_inet_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fib)),
	.eval		= nft_fib_inet_eval,
	.init		= nft_fib_init,
	.dump		= nft_fib_dump,
	.validate	= nft_fib_validate,
};

static struct nft_expr_type nft_fib_inet_type __read_mostly = {
	.family		= NFPROTO_INET,
	.name		= "fib",
	.ops		= &nft_fib_inet_ops,
	.policy		= nft_fib_policy,
	.maxattr	= NFTA_FIB_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_fib_inet_module_init(void)
{
	return nft_register_expr(&nft_fib_inet_type);
}

static void __exit nft_fib_inet_module_exit(void)
{
	nft_unregister_expr(&nft_fib_inet_type);
}

module_init(nft_fib_inet_module_init);
module_exit(nft_fib_inet_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_ALIAS_NFT_AF_EXPR(1, "fib");
