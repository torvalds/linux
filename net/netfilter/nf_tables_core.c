/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

static void nft_cmp_fast_eval(const struct nft_expr *expr,
			      struct nft_data data[NFT_REG_MAX + 1])
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	u32 mask;

	mask = ~0U >> (sizeof(priv->data) * BITS_PER_BYTE - priv->len);
	if ((data[priv->sreg].data[0] & mask) == priv->data)
		return;
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}

unsigned int nft_do_chain(const struct nf_hook_ops *ops,
			  struct sk_buff *skb,
			  const struct net_device *in,
			  const struct net_device *out,
			  int (*okfn)(struct sk_buff *))
{
	const struct nft_chain *chain = ops->priv;
	const struct nft_rule *rule;
	const struct nft_expr *expr, *last;
	struct nft_data data[NFT_REG_MAX + 1];
	const struct nft_pktinfo pkt = {
		.skb		= skb,
		.in		= in,
		.out		= out,
		.hooknum	= ops->hooknum,
	};
	unsigned int stackptr = 0;
	struct {
		const struct nft_chain	*chain;
		const struct nft_rule	*rule;
	} jumpstack[NFT_JUMP_STACK_SIZE];

do_chain:
	rule = list_entry(&chain->rules, struct nft_rule, list);
next_rule:
	data[NFT_REG_VERDICT].verdict = NFT_CONTINUE;
	list_for_each_entry_continue_rcu(rule, &chain->rules, list) {
		nft_rule_for_each_expr(expr, last, rule) {
			if (expr->ops == &nft_cmp_fast_ops)
				nft_cmp_fast_eval(expr, data);
			else
				expr->ops->eval(expr, data, &pkt);

			if (data[NFT_REG_VERDICT].verdict != NFT_CONTINUE)
				break;
		}

		switch (data[NFT_REG_VERDICT].verdict) {
		case NFT_BREAK:
			data[NFT_REG_VERDICT].verdict = NFT_CONTINUE;
			/* fall through */
		case NFT_CONTINUE:
			continue;
		}
		break;
	}

	switch (data[NFT_REG_VERDICT].verdict) {
	case NF_ACCEPT:
	case NF_DROP:
	case NF_QUEUE:
		return data[NFT_REG_VERDICT].verdict;
	case NFT_JUMP:
		BUG_ON(stackptr >= NFT_JUMP_STACK_SIZE);
		jumpstack[stackptr].chain = chain;
		jumpstack[stackptr].rule  = rule;
		stackptr++;
		/* fall through */
	case NFT_GOTO:
		chain = data[NFT_REG_VERDICT].chain;
		goto do_chain;
	case NFT_RETURN:
	case NFT_CONTINUE:
		break;
	default:
		WARN_ON(1);
	}

	if (stackptr > 0) {
		stackptr--;
		chain = jumpstack[stackptr].chain;
		rule  = jumpstack[stackptr].rule;
		goto next_rule;
	}

	return NF_ACCEPT;
}
EXPORT_SYMBOL_GPL(nft_do_chain);

int __init nf_tables_core_module_init(void)
{
	int err;

	err = nft_immediate_module_init();
	if (err < 0)
		goto err1;

	err = nft_cmp_module_init();
	if (err < 0)
		goto err2;

	err = nft_lookup_module_init();
	if (err < 0)
		goto err3;

	err = nft_bitwise_module_init();
	if (err < 0)
		goto err4;

	err = nft_byteorder_module_init();
	if (err < 0)
		goto err5;

	err = nft_payload_module_init();
	if (err < 0)
		goto err6;

	return 0;

err6:
	nft_byteorder_module_exit();
err5:
	nft_bitwise_module_exit();
err4:
	nft_lookup_module_exit();
err3:
	nft_cmp_module_exit();
err2:
	nft_immediate_module_exit();
err1:
	return err;
}

void nf_tables_core_module_exit(void)
{
	nft_payload_module_exit();
	nft_byteorder_module_exit();
	nft_bitwise_module_exit();
	nft_lookup_module_exit();
	nft_cmp_module_exit();
	nft_immediate_module_exit();
}
