// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/u64_stats_sync.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables_offload.h>

struct nft_counter {
	u64_stats_t	bytes;
	u64_stats_t	packets;
};

struct nft_counter_tot {
	s64		bytes;
	s64		packets;
};

struct nft_counter_percpu_priv {
	struct nft_counter __percpu *counter;
};

static DEFINE_PER_CPU(struct u64_stats_sync, nft_counter_sync);

static inline void nft_counter_do_eval(struct nft_counter_percpu_priv *priv,
				       struct nft_regs *regs,
				       const struct nft_pktinfo *pkt)
{
	struct u64_stats_sync *nft_sync;
	struct nft_counter *this_cpu;

	local_bh_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	nft_sync = this_cpu_ptr(&nft_counter_sync);

	u64_stats_update_begin(nft_sync);
	u64_stats_add(&this_cpu->bytes, pkt->skb->len);
	u64_stats_inc(&this_cpu->packets);
	u64_stats_update_end(nft_sync);

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

	cpu_stats = alloc_percpu_gfp(struct nft_counter, GFP_KERNEL_ACCOUNT);
	if (cpu_stats == NULL)
		return -ENOMEM;

	this_cpu = raw_cpu_ptr(cpu_stats);
	if (tb[NFTA_COUNTER_PACKETS]) {
		u64_stats_set(&this_cpu->packets,
			      be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_PACKETS])));
	}
	if (tb[NFTA_COUNTER_BYTES]) {
		u64_stats_set(&this_cpu->bytes,
			      be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_BYTES])));
	}

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
			      struct nft_counter_tot *total)
{
	struct u64_stats_sync *nft_sync;
	struct nft_counter *this_cpu;

	local_bh_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	nft_sync = this_cpu_ptr(&nft_counter_sync);

	u64_stats_update_begin(nft_sync);
	u64_stats_add(&this_cpu->packets, -total->packets);
	u64_stats_add(&this_cpu->bytes, -total->bytes);
	u64_stats_update_end(nft_sync);

	local_bh_enable();
}

static void nft_counter_fetch(struct nft_counter_percpu_priv *priv,
			      struct nft_counter_tot *total)
{
	struct nft_counter *this_cpu;
	u64 bytes, packets;
	unsigned int seq;
	int cpu;

	memset(total, 0, sizeof(*total));
	for_each_possible_cpu(cpu) {
		struct u64_stats_sync *nft_sync = per_cpu_ptr(&nft_counter_sync, cpu);

		this_cpu = per_cpu_ptr(priv->counter, cpu);
		do {
			seq	= u64_stats_fetch_begin(nft_sync);
			bytes	= u64_stats_read(&this_cpu->bytes);
			packets	= u64_stats_read(&this_cpu->packets);
		} while (u64_stats_fetch_retry(nft_sync, seq));

		total->bytes	+= bytes;
		total->packets	+= packets;
	}
}

static int nft_counter_do_dump(struct sk_buff *skb,
			       struct nft_counter_percpu_priv *priv,
			       bool reset)
{
	struct nft_counter_tot total;

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

static int nft_counter_dump(struct sk_buff *skb,
			    const struct nft_expr *expr, bool reset)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(expr);

	return nft_counter_do_dump(skb, priv, reset);
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

static int nft_counter_clone(struct nft_expr *dst, const struct nft_expr *src, gfp_t gfp)
{
	struct nft_counter_percpu_priv *priv = nft_expr_priv(src);
	struct nft_counter_percpu_priv *priv_clone = nft_expr_priv(dst);
	struct nft_counter __percpu *cpu_stats;
	struct nft_counter *this_cpu;
	struct nft_counter_tot total;

	nft_counter_fetch(priv, &total);

	cpu_stats = alloc_percpu_gfp(struct nft_counter, gfp);
	if (cpu_stats == NULL)
		return -ENOMEM;

	this_cpu = raw_cpu_ptr(cpu_stats);
	u64_stats_set(&this_cpu->packets, total.packets);
	u64_stats_set(&this_cpu->bytes, total.bytes);

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
	struct u64_stats_sync *nft_sync;
	struct nft_counter *this_cpu;

	local_bh_disable();
	this_cpu = this_cpu_ptr(priv->counter);
	nft_sync = this_cpu_ptr(&nft_counter_sync);

	u64_stats_update_begin(nft_sync);
	u64_stats_add(&this_cpu->packets, stats->pkts);
	u64_stats_add(&this_cpu->bytes, stats->bytes);
	u64_stats_update_end(nft_sync);
	local_bh_enable();
}

void nft_counter_init_seqcount(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		u64_stats_init(per_cpu_ptr(&nft_counter_sync, cpu));
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
	.reduce		= NFT_REDUCE_READONLY,
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
