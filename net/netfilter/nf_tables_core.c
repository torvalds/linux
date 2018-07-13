/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
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
#include <net/netfilter/nf_log.h>

enum nft_trace {
	NFT_TRACE_RULE,
	NFT_TRACE_RETURN,
	NFT_TRACE_POLICY,
};

static const char *const comments[] = {
	[NFT_TRACE_RULE]	= "rule",
	[NFT_TRACE_RETURN]	= "return",
	[NFT_TRACE_POLICY]	= "policy",
};

static struct nf_loginfo trace_loginfo = {
	.type = NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level = LOGLEVEL_WARNING,
			.logflags = NF_LOG_MASK,
	        },
	},
};

static void __nft_trace_packet(const struct nft_pktinfo *pkt,
			       const struct nft_chain *chain,
			       int rulenum, enum nft_trace type)
{
	nf_log_trace(pkt->net, pkt->pf, pkt->hook, pkt->skb, pkt->in,
		     pkt->out, &trace_loginfo, "TRACE: %s:%s:%s:%u ",
		     chain->table->name, chain->name, comments[type],
		     rulenum);
}

static inline void nft_trace_packet(const struct nft_pktinfo *pkt,
				    const struct nft_chain *chain,
				    int rulenum, enum nft_trace type)
{
	if (unlikely(pkt->skb->nf_trace))
		__nft_trace_packet(pkt, chain, rulenum, type);
}

static void nft_cmp_fast_eval(const struct nft_expr *expr,
			      struct nft_regs *regs)
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	u32 mask = nft_cmp_fast_mask(priv->len);

	if ((regs->data[priv->sreg] & mask) == priv->data)
		return;
	regs->verdict.code = NFT_BREAK;
}

static bool nft_payload_fast_eval(const struct nft_expr *expr,
				  struct nft_regs *regs,
				  const struct nft_pktinfo *pkt)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	unsigned char *ptr;

	if (priv->base == NFT_PAYLOAD_NETWORK_HEADER)
		ptr = skb_network_header(skb);
	else
		ptr = skb_network_header(skb) + pkt->xt.thoff;

	ptr += priv->offset;

	if (unlikely(ptr + priv->len >= skb_tail_pointer(skb)))
		return false;

	*dest = 0;
	if (priv->len == 2)
		*(u16 *)dest = *(u16 *)ptr;
	else if (priv->len == 4)
		*(u32 *)dest = *(u32 *)ptr;
	else
		*(u8 *)dest = *(u8 *)ptr;
	return true;
}

struct nft_jumpstack {
	const struct nft_chain	*chain;
	const struct nft_rule	*rule;
	int			rulenum;
};

unsigned int
nft_do_chain(struct nft_pktinfo *pkt, void *priv)
{
	const struct nft_chain *chain = priv, *basechain = chain;
	const struct net *net = pkt->net;
	const struct nft_rule *rule;
	const struct nft_expr *expr, *last;
	struct nft_regs regs;
	unsigned int stackptr = 0;
	struct nft_jumpstack jumpstack[NFT_JUMP_STACK_SIZE];
	struct nft_stats *stats;
	int rulenum;
	unsigned int gencursor = nft_genmask_cur(net);

do_chain:
	rulenum = 0;
	rule = list_entry(&chain->rules, struct nft_rule, list);
next_rule:
	regs.verdict.code = NFT_CONTINUE;
	list_for_each_entry_continue_rcu(rule, &chain->rules, list) {

		/* This rule is not active, skip. */
		if (unlikely(rule->genmask & (1 << gencursor)))
			continue;

		rulenum++;

		nft_rule_for_each_expr(expr, last, rule) {
			if (expr->ops == &nft_cmp_fast_ops)
				nft_cmp_fast_eval(expr, &regs);
			else if (expr->ops != &nft_payload_fast_ops ||
				 !nft_payload_fast_eval(expr, &regs, pkt))
				expr->ops->eval(expr, &regs, pkt);

			if (regs.verdict.code != NFT_CONTINUE)
				break;
		}

		switch (regs.verdict.code) {
		case NFT_BREAK:
			regs.verdict.code = NFT_CONTINUE;
			continue;
		case NFT_CONTINUE:
			nft_trace_packet(pkt, chain, rulenum, NFT_TRACE_RULE);
			continue;
		}
		break;
	}

	switch (regs.verdict.code & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_DROP:
	case NF_QUEUE:
		nft_trace_packet(pkt, chain, rulenum, NFT_TRACE_RULE);
		return regs.verdict.code;
	}

	switch (regs.verdict.code) {
	case NFT_JUMP:
		if (WARN_ON_ONCE(stackptr >= NFT_JUMP_STACK_SIZE))
			return NF_DROP;
		jumpstack[stackptr].chain = chain;
		jumpstack[stackptr].rule  = rule;
		jumpstack[stackptr].rulenum = rulenum;
		stackptr++;
		/* fall through */
	case NFT_GOTO:
		nft_trace_packet(pkt, chain, rulenum, NFT_TRACE_RULE);

		chain = regs.verdict.chain;
		goto do_chain;
	case NFT_CONTINUE:
		rulenum++;
		/* fall through */
	case NFT_RETURN:
		nft_trace_packet(pkt, chain, rulenum, NFT_TRACE_RETURN);
		break;
	default:
		WARN_ON(1);
	}

	if (stackptr > 0) {
		stackptr--;
		chain = jumpstack[stackptr].chain;
		rule  = jumpstack[stackptr].rule;
		rulenum = jumpstack[stackptr].rulenum;
		goto next_rule;
	}

	nft_trace_packet(pkt, basechain, -1, NFT_TRACE_POLICY);

	rcu_read_lock_bh();
	stats = this_cpu_ptr(rcu_dereference(nft_base_chain(basechain)->stats));
	u64_stats_update_begin(&stats->syncp);
	stats->pkts++;
	stats->bytes += pkt->skb->len;
	u64_stats_update_end(&stats->syncp);
	rcu_read_unlock_bh();

	return nft_base_chain(basechain)->policy;
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

	err = nft_dynset_module_init();
	if (err < 0)
		goto err7;

	return 0;

err7:
	nft_payload_module_exit();
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
	nft_dynset_module_exit();
	nft_payload_module_exit();
	nft_byteorder_module_exit();
	nft_bitwise_module_exit();
	nft_lookup_module_exit();
	nft_cmp_module_exit();
	nft_immediate_module_exit();
}
