// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/seqlock.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables_offload.h>

struct nft_counter {
	s64		bytes;
	s64		packets;
};

struct nft_counter_percpu_priv {
	struct nft_counter __percpu *counter;
};

static DEFINE_PER_CPU(seqcount_t, nft_counter_seq);

static inline void nft_counter_do_eval(struct nft_counter_percpu_priv *priv,
				       struct nft_regs *regs,
				       const struct nft_pktinfo *pkt)
{
	struct nft_counter *this_cpu;
	seqcount_t *myseq;

	local_bh_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	myseq = this_cpu_ptr(&nft_counter_seq);

	write_seqcount_begin(myseq);

	this_cpu->bytes += pkt->skb->len;
	this_cpu->packets++;

	write_seqcount_end(myseq);
	local_bh_enable();
}

static inline void nft_counter_obj_eval(struct nft_object *obj,
					struct nft_regs *regs,
					const struct nft_pktinfo *pkt)
{
	struct nft_counter_percpu_priv *priv = nft_obj_data(obj);

	nft_counter_do_eval(priv, regs, pkt);
}

static int nft_counter_do_init(const struct nlattr * const tb[],
			       struct nft_counter_percpu_priv *priv)
{
	struct nft_counter __percpu *cpu_stats;
	struct nft_counter *this_cpu;

	cpu_stats = alloc_percpu(struct nft_counter);
	if (cpu_stats == NULL)
		return -ENOMEM;

	preempt_disable();
	this_cpu = this_cpu_ptr(cpu_stats);
	if (tb[NFTA_COUNTER_PACKETS]) {
	        this_cpu->packets =
			be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_PACKETS]));
	}
	if (tb[NFTA_COUNTER_BYTES]) {
		this_cpu->bytes =
			be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_BYTES]));
	}
	preempt_enable();
	priv->counter = cpu_stats;
	return 0;
}

static int nft_counter_obj_init(const struct nft_ctx *ctx,
				const struct nlattr * const tb[],
				struct nft_object *obj)
{
	struct nft_counter_percpu_priv *priv = nft_obj_data(obj);

	return nft_counter_do_init(tb, priv);
}

static void nft_counter_do_destroy(struct nft_counter_percpu_priv *priv)
{
	free_percpu(priv->counter);
}

static void nft_counter_obj_destroy(const struct nft_ctx *ctx,
				    struct nft_object *obj)
{
	struct nft_counter_percpu_priv *priv = nft_obj_data(obj);

	nft_counter_do_destroy(priv);
}

static void nft_counter_reset(struct nft_counter_percpu_priv *priv,
			      struct nft_counter *total)
{
	struct nft_counter *this_cpu;

	local_bh_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	this_cpu->packets -= total->packets;
	this_cpu->bytes -= total->bytes;
	local_bh_enable();
}

static void nft_counter_fetch(struct nft_counter_percpu_priv *priv,
			      struct nft_counter *total)
{
	struct nft_counter *this_cpu;
	const seqcount_t *myseq;
	u64 bytes, packets;
	unsigned int seq;
	int cpu;

	memset(total, 0, sizeof(*total));
	for_each_possible_cpu(cpu) {
		myseq = per_cpu_ptr(&nft_counter_seq, cpu);
		this_cpu = per_cpu_ptr(priv->counter, cpu);
		do {
			seq	= read_seqcount_begin(myseq);
			bytes	= this_cpu->bytes;
			packets	= this_cpu->packets;
		} while (read_seqcount_retry(myseq, seq));

		total->bytes	+= bytes;
		total->packets	+= packets;
	}
}

static int nft_counter_do_dump(struct sk_buff *skb,
			       struct nft_counter_percpu_priv *priv,
			       bool reset)
{
	struct nft_counter total;

	nft_counter_fetch(priv, &total);

	if (nla_put_be64(skb, NFTA_COUNTER_BYTES, cpu_to_be64(total.bytes),
			 NFTA_COUNTER_PAD) ||
	    nla_put_be64(skb, NFTA_COUNTER_PACKETS, cpu_to_be64(total.packets),
			 NFTA_COUNTER_PAD))
		goto nla_put_failure;

	if (reset)
		nft_counter_reset(priv, &total);

	return 0;

nla_put_failure:
	return -1;
}

static int nft_counter_obj_dump(struct sk_buff *skb,
				struct nft_object *obj, bool reset)
{
	struct nft_counter_percpu_priv *priv = nft_obj_data(obj);

	return nft_counter_do_dump(skb, priv, reset);
}

static const struct nla_policy nft_counter_policy[NFTA_COUNTER_MAX + 1] = {
	[NFTA_COUNTER_PACKETS]	= { .type = NLA_U64 },
	[NFTA_COUNTER_BYTES]	= { .type = NLA_U64 },
};

struct nft_object_type nft_counter_obj_type;
static const struct nft_object_ops nft_counter_obj_ops = {
	.type		= &nft_counter_obj_type,
	.size		= sizeof(struct nft_counter_percpu_priv),
	.eval		= nft_counter_obj_eval,
	.init		= nft_counter_obj_init,
	.destroy	= nft_counter_obj_destroy,
	.dump		= nft_counter_obj_dump,
};

struct nft_object_type nft_counter_obj_type __read_mostly = {
	.type		= NFT_OBJECT_COUNTER,
	.ops		= &nft_counter_obj_ops,
	.maxattr	= NFTA_COUNTER_MAX,
	.policy		= nft_counter_policy,
	.owner		= THIS_MODULE,
};

void nft_counter_eval(const struct nft_expr *expr, struct nft_regs *regs,
		      const struct nft_pktinfo *pkt)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);

	nft_counter_do_eval(priv, regs, pkt);
}

static int nft_counter_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);

	return nft_counter_do_dump(skb, priv, false);
}

static int nft_counter_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr * const tb[])
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);

	return nft_counter_do_init(tb, priv);
}

static void nft_counter_destroy(const struct nft_ctx *ctx,
				const struct nft_expr *expr)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);

	nft_counter_do_destroy(priv);
}

static int nft_counter_clone(struct nft_expr *dst, const struct nft_expr *src)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(src);
	struct nft_counter_percpu_priv *priv_clone = nft_expr_priv(dst);
	struct nft_counter __percpu *cpu_stats;
	struct nft_counter *this_cpu;
	struct nft_counter total;

	nft_counter_fetch(priv, &total);

	cpu_stats = alloc_percpu_gfp(struct nft_counter, GFP_ATOMIC);
	if (cpu_stats == NULL)
		return -ENOMEM;

	preempt_disable();
	this_cpu = this_cpu_ptr(cpu_stats);
	this_cpu->packets = total.packets;
	this_cpu->bytes = total.bytes;
	preempt_enable();

	priv_clone->counter = cpu_stats;
	return 0;
}

static int nft_counter_offload(struct nft_offload_ctx *ctx,
			       struct nft_flow_rule *flow,
			       const struct nft_expr *expr)
{
	/* No specific offload action is needed, but report success. */
	return 0;
}

static void nft_counter_offload_stats(struct nft_expr *expr,
				      const struct flow_stats *stats)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);
	struct nft_counter *this_cpu;
	seqcount_t *myseq;

	preempt_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	myseq = this_cpu_ptr(&nft_counter_seq);

	write_seqcount_begin(myseq);
	this_cpu->packets += stats->pkts;
	this_cpu->bytes += stats->bytes;
	write_seqcount_end(myseq);
	preempt_enable();
}

void nft_counter_init_seqcount(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		seqcount_init(per_cpu_ptr(&nft_counter_seq, cpu));
}

struct nft_expr_type nft_counter_type;
static const struct nft_expr_ops nft_counter_ops = {
	.type		= &nft_counter_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_counter_percpu_priv)),
	.eval		= nft_counter_eval,
	.init		= nft_counter_init,
	.destroy	= nft_counter_destroy,
	.destroy_clone	= nft_counter_destroy,
	.dump		= nft_counter_dump,
	.clone		= nft_counter_clone,
	.offload	= nft_counter_offload,
	.offload_stats	= nft_counter_offload_stats,
};

struct nft_expr_type nft_counter_type __read_mostly = {
	.name		= "counter",
	.ops		= &nft_counter_ops,
	.policy		= nft_counter_policy,
	.maxattr	= NFTA_COUNTER_MAX,
	.flags		= NFT_EXPR_STATEFUL,
	.owner		= THIS_MODULE,
};
