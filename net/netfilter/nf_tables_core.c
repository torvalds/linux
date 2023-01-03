// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
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
#include <linux/static_key.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_log.h>
#include <net/netfilter/nft_meta.h>

#if defined(CONFIG_RETPOLINE) && defined(CONFIG_X86)

static struct static_key_false nf_tables_skip_direct_calls;

static bool nf_skip_indirect_calls(void)
{
	return static_branch_likely(&nf_tables_skip_direct_calls);
}

static void __init nf_skip_indirect_calls_enable(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_RETPOLINE))
		static_branch_enable(&nf_tables_skip_direct_calls);
}
#else
static inline bool nf_skip_indirect_calls(void) { return false; }

static inline void nf_skip_indirect_calls_enable(void) { }
#endif

static noinline void __nft_trace_packet(struct nft_traceinfo *info,
					const struct nft_chain *chain,
					enum nft_trace_types type)
{
	if (!info->trace || !info->nf_trace)
		return;

	info->chain = chain;
	info->type = type;

	nft_trace_notify(info);
}

static inline void nft_trace_packet(const struct nft_pktinfo *pkt,
				    struct nft_traceinfo *info,
				    const struct nft_chain *chain,
				    const struct nft_rule_dp *rule,
				    enum nft_trace_types type)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		info->nf_trace = pkt->skb->nf_trace;
		info->rule = rule;
		__nft_trace_packet(info, chain, type);
	}
}

static inline void nft_trace_copy_nftrace(const struct nft_pktinfo *pkt,
					  struct nft_traceinfo *info)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		if (info->trace)
			info->nf_trace = pkt->skb->nf_trace;
	}
}

static void nft_bitwise_fast_eval(const struct nft_expr *expr,
				  struct nft_regs *regs)
{
	const struct nft_bitwise_fast_expr *priv = nft_expr_priv(expr);
	u32 *src = &regs->data[priv->sreg];
	u32 *dst = &regs->data[priv->dreg];

	*dst = (*src & priv->mask) ^ priv->xor;
}

static void nft_cmp_fast_eval(const struct nft_expr *expr,
			      struct nft_regs *regs)
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);

	if (((regs->data[priv->sreg] & priv->mask) == priv->data) ^ priv->inv)
		return;
	regs->verdict.code = NFT_BREAK;
}

static void nft_cmp16_fast_eval(const struct nft_expr *expr,
				struct nft_regs *regs)
{
	const struct nft_cmp16_fast_expr *priv = nft_expr_priv(expr);
	const u64 *reg_data = (const u64 *)&regs->data[priv->sreg];
	const u64 *mask = (const u64 *)&priv->mask;
	const u64 *data = (const u64 *)&priv->data;

	if (((reg_data[0] & mask[0]) == data[0] &&
	    ((reg_data[1] & mask[1]) == data[1])) ^ priv->inv)
		return;
	regs->verdict.code = NFT_BREAK;
}

static noinline void __nft_trace_verdict(struct nft_traceinfo *info,
					 const struct nft_chain *chain,
					 const struct nft_regs *regs)
{
	enum nft_trace_types type;

	switch (regs->verdict.code) {
	case NFT_CONTINUE:
	case NFT_RETURN:
		type = NFT_TRACETYPE_RETURN;
		break;
	case NF_STOLEN:
		type = NFT_TRACETYPE_RULE;
		/* can't access skb->nf_trace; use copy */
		break;
	default:
		type = NFT_TRACETYPE_RULE;

		if (info->trace)
			info->nf_trace = info->pkt->skb->nf_trace;
		break;
	}

	__nft_trace_packet(info, chain, type);
}

static inline void nft_trace_verdict(struct nft_traceinfo *info,
				     const struct nft_chain *chain,
				     const struct nft_rule_dp *rule,
				     const struct nft_regs *regs)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		info->rule = rule;
		__nft_trace_verdict(info, chain, regs);
	}
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
	else {
		if (!(pkt->flags & NFT_PKTINFO_L4PROTO))
			return false;
		ptr = skb_network_header(skb) + nft_thoff(pkt);
	}

	ptr += priv->offset;

	if (unlikely(ptr + priv->len > skb_tail_pointer(skb)))
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

DEFINE_STATIC_KEY_FALSE(nft_counters_enabled);

static noinline void nft_update_chain_stats(const struct nft_chain *chain,
					    const struct nft_pktinfo *pkt)
{
	struct nft_base_chain *base_chain;
	struct nft_stats __percpu *pstats;
	struct nft_stats *stats;

	base_chain = nft_base_chain(chain);

	pstats = READ_ONCE(base_chain->stats);
	if (pstats) {
		local_bh_disable();
		stats = this_cpu_ptr(pstats);
		u64_stats_update_begin(&stats->syncp);
		stats->pkts++;
		stats->bytes += pkt->skb->len;
		u64_stats_update_end(&stats->syncp);
		local_bh_enable();
	}
}

struct nft_jumpstack {
	const struct nft_chain *chain;
	const struct nft_rule_dp *rule;
	const struct nft_rule_dp *last_rule;
};

static void expr_call_ops_eval(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       struct nft_pktinfo *pkt)
{
#ifdef CONFIG_RETPOLINE
	unsigned long e;

	if (nf_skip_indirect_calls())
		goto indirect_call;

	e = (unsigned long)expr->ops->eval;
#define X(e, fun) \
	do { if ((e) == (unsigned long)(fun)) \
		return fun(expr, regs, pkt); } while (0)

	X(e, nft_payload_eval);
	X(e, nft_cmp_eval);
	X(e, nft_counter_eval);
	X(e, nft_meta_get_eval);
	X(e, nft_lookup_eval);
#if IS_ENABLED(CONFIG_NFT_CT)
	X(e, nft_ct_get_fast_eval);
#endif
	X(e, nft_range_eval);
	X(e, nft_immediate_eval);
	X(e, nft_byteorder_eval);
	X(e, nft_dynset_eval);
	X(e, nft_rt_get_eval);
	X(e, nft_bitwise_eval);
	X(e, nft_objref_eval);
	X(e, nft_objref_map_eval);
#undef  X
indirect_call:
#endif /* CONFIG_RETPOLINE */
	expr->ops->eval(expr, regs, pkt);
}

#define nft_rule_expr_first(rule)	(struct nft_expr *)&rule->data[0]
#define nft_rule_expr_next(expr)	((void *)expr) + expr->ops->size
#define nft_rule_expr_last(rule)	(struct nft_expr *)&rule->data[rule->dlen]
#define nft_rule_next(rule)		(void *)rule + sizeof(*rule) + rule->dlen

#define nft_rule_dp_for_each_expr(expr, last, rule) \
        for ((expr) = nft_rule_expr_first(rule), (last) = nft_rule_expr_last(rule); \
             (expr) != (last); \
             (expr) = nft_rule_expr_next(expr))

unsigned int
nft_do_chain(struct nft_pktinfo *pkt, void *priv)
{
	const struct nft_chain *chain = priv, *basechain = chain;
	const struct nft_rule_dp *rule, *last_rule;
	const struct net *net = nft_net(pkt);
	const struct nft_expr *expr, *last;
	struct nft_regs regs = {};
	unsigned int stackptr = 0;
	struct nft_jumpstack jumpstack[NFT_JUMP_STACK_SIZE];
	bool genbit = READ_ONCE(net->nft.gencursor);
	struct nft_rule_blob *blob;
	struct nft_traceinfo info;

	info.trace = false;
	if (static_branch_unlikely(&nft_trace_enabled))
		nft_trace_init(&info, pkt, &regs.verdict, basechain);
do_chain:
	if (genbit)
		blob = rcu_dereference(chain->blob_gen_1);
	else
		blob = rcu_dereference(chain->blob_gen_0);

	rule = (struct nft_rule_dp *)blob->data;
	last_rule = (void *)blob->data + blob->size;
next_rule:
	regs.verdict.code = NFT_CONTINUE;
	for (; rule < last_rule; rule = nft_rule_next(rule)) {
		nft_rule_dp_for_each_expr(expr, last, rule) {
			if (expr->ops == &nft_cmp_fast_ops)
				nft_cmp_fast_eval(expr, &regs);
			else if (expr->ops == &nft_cmp16_fast_ops)
				nft_cmp16_fast_eval(expr, &regs);
			else if (expr->ops == &nft_bitwise_fast_ops)
				nft_bitwise_fast_eval(expr, &regs);
			else if (expr->ops != &nft_payload_fast_ops ||
				 !nft_payload_fast_eval(expr, &regs, pkt))
				expr_call_ops_eval(expr, &regs, pkt);

			if (regs.verdict.code != NFT_CONTINUE)
				break;
		}

		switch (regs.verdict.code) {
		case NFT_BREAK:
			regs.verdict.code = NFT_CONTINUE;
			nft_trace_copy_nftrace(pkt, &info);
			continue;
		case NFT_CONTINUE:
			nft_trace_packet(pkt, &info, chain, rule,
					 NFT_TRACETYPE_RULE);
			continue;
		}
		break;
	}

	nft_trace_verdict(&info, chain, rule, &regs);

	switch (regs.verdict.code & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_DROP:
	case NF_QUEUE:
	case NF_STOLEN:
		return regs.verdict.code;
	}

	switch (regs.verdict.code) {
	case NFT_JUMP:
		if (WARN_ON_ONCE(stackptr >= NFT_JUMP_STACK_SIZE))
			return NF_DROP;
		jumpstack[stackptr].chain = chain;
		jumpstack[stackptr].rule = nft_rule_next(rule);
		jumpstack[stackptr].last_rule = last_rule;
		stackptr++;
		fallthrough;
	case NFT_GOTO:
		chain = regs.verdict.chain;
		goto do_chain;
	case NFT_CONTINUE:
	case NFT_RETURN:
		break;
	default:
		WARN_ON_ONCE(1);
	}

	if (stackptr > 0) {
		stackptr--;
		chain = jumpstack[stackptr].chain;
		rule = jumpstack[stackptr].rule;
		last_rule = jumpstack[stackptr].last_rule;
		goto next_rule;
	}

	nft_trace_packet(pkt, &info, basechain, NULL, NFT_TRACETYPE_POLICY);

	if (static_branch_unlikely(&nft_counters_enabled))
		nft_update_chain_stats(basechain, pkt);

	return nft_base_chain(basechain)->policy;
}
EXPORT_SYMBOL_GPL(nft_do_chain);

static struct nft_expr_type *nft_basic_types[] = {
	&nft_imm_type,
	&nft_cmp_type,
	&nft_lookup_type,
	&nft_bitwise_type,
	&nft_byteorder_type,
	&nft_payload_type,
	&nft_dynset_type,
	&nft_range_type,
	&nft_meta_type,
	&nft_rt_type,
	&nft_exthdr_type,
	&nft_last_type,
	&nft_counter_type,
	&nft_objref_type,
	&nft_inner_type,
};

static struct nft_object_type *nft_basic_objects[] = {
#ifdef CONFIG_NETWORK_SECMARK
	&nft_secmark_obj_type,
#endif
	&nft_counter_obj_type,
};

int __init nf_tables_core_module_init(void)
{
	int err, i, j = 0;

	nft_counter_init_seqcount();

	for (i = 0; i < ARRAY_SIZE(nft_basic_objects); i++) {
		err = nft_register_obj(nft_basic_objects[i]);
		if (err)
			goto err;
	}

	for (j = 0; j < ARRAY_SIZE(nft_basic_types); j++) {
		err = nft_register_expr(nft_basic_types[j]);
		if (err)
			goto err;
	}

	nf_skip_indirect_calls_enable();

	return 0;

err:
	while (j-- > 0)
		nft_unregister_expr(nft_basic_types[j]);

	while (i-- > 0)
		nft_unregister_obj(nft_basic_objects[i]);

	return err;
}

void nf_tables_core_module_exit(void)
{
	int i;

	i = ARRAY_SIZE(nft_basic_types);
	while (i-- > 0)
		nft_unregister_expr(nft_basic_types[i]);

	i = ARRAY_SIZE(nft_basic_objects);
	while (i-- > 0)
		nft_unregister_obj(nft_basic_objects[i]);
}
