/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_count.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

struct nft_connlimit {
	struct nf_conncount_list	*list;
	u32				limit;
	bool				invert;
};

static inline void nft_connlimit_do_eval(struct nft_connlimit *priv,
					 struct nft_regs *regs,
					 const struct nft_pktinfo *pkt,
					 const struct nft_set_ext *ext)
{
	const struct nf_conntrack_zone *zone = &nf_ct_zone_dflt;
	const struct nf_conntrack_tuple *tuple_ptr;
	struct nf_conntrack_tuple tuple;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int count;

	tuple_ptr = &tuple;

	ct = nf_ct_get(pkt->skb, &ctinfo);
	if (ct != NULL) {
		tuple_ptr = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		zone = nf_ct_zone(ct);
	} else if (!nf_ct_get_tuplepr(pkt->skb, skb_network_offset(pkt->skb),
				      nft_pf(pkt), nft_net(pkt), &tuple)) {
		regs->verdict.code = NF_DROP;
		return;
	}

	if (nf_conncount_add(nft_net(pkt), priv->list, tuple_ptr, zone)) {
		regs->verdict.code = NF_DROP;
		return;
	}

	count = READ_ONCE(priv->list->count);

	if ((count > priv->limit) ^ priv->invert) {
		regs->verdict.code = NFT_BREAK;
		return;
	}
}

static int nft_connlimit_do_init(const struct nft_ctx *ctx,
				 const struct nlattr * const tb[],
				 struct nft_connlimit *priv)
{
	bool invert = false;
	u32 flags, limit;
	int err;

	if (!tb[NFTA_CONNLIMIT_COUNT])
		return -EINVAL;

	limit = ntohl(nla_get_be32(tb[NFTA_CONNLIMIT_COUNT]));

	if (tb[NFTA_CONNLIMIT_FLAGS]) {
		flags = ntohl(nla_get_be32(tb[NFTA_CONNLIMIT_FLAGS]));
		if (flags & ~NFT_CONNLIMIT_F_INV)
			return -EOPNOTSUPP;
		if (flags & NFT_CONNLIMIT_F_INV)
			invert = true;
	}

	priv->list = kmalloc(sizeof(*priv->list), GFP_KERNEL_ACCOUNT);
	if (!priv->list)
		return -ENOMEM;

	nf_conncount_list_init(priv->list);
	priv->limit	= limit;
	priv->invert	= invert;

	err = nf_ct_netns_get(ctx->net, ctx->family);
	if (err < 0)
		goto err_netns;

	return 0;
err_netns:
	kfree(priv->list);

	return err;
}

static void nft_connlimit_do_destroy(const struct nft_ctx *ctx,
				     struct nft_connlimit *priv)
{
	nf_ct_netns_put(ctx->net, ctx->family);
	nf_conncount_cache_free(priv->list);
	kfree(priv->list);
}

static int nft_connlimit_do_dump(struct sk_buff *skb,
				 struct nft_connlimit *priv)
{
	if (nla_put_be32(skb, NFTA_CONNLIMIT_COUNT, htonl(priv->limit)))
		goto nla_put_failure;
	if (priv->invert &&
	    nla_put_be32(skb, NFTA_CONNLIMIT_FLAGS, htonl(NFT_CONNLIMIT_F_INV)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static inline void nft_connlimit_obj_eval(struct nft_object *obj,
					struct nft_regs *regs,
					const struct nft_pktinfo *pkt)
{
	struct nft_connlimit *priv = nft_obj_data(obj);

	nft_connlimit_do_eval(priv, regs, pkt, NULL);
}

static int nft_connlimit_obj_init(const struct nft_ctx *ctx,
				const struct nlattr * const tb[],
				struct nft_object *obj)
{
	struct nft_connlimit *priv = nft_obj_data(obj);

	return nft_connlimit_do_init(ctx, tb, priv);
}

static void nft_connlimit_obj_destroy(const struct nft_ctx *ctx,
				      struct nft_object *obj)
{
	struct nft_connlimit *priv = nft_obj_data(obj);

	nft_connlimit_do_destroy(ctx, priv);
}

static int nft_connlimit_obj_dump(struct sk_buff *skb,
				  struct nft_object *obj, bool reset)
{
	struct nft_connlimit *priv = nft_obj_data(obj);

	return nft_connlimit_do_dump(skb, priv);
}

static const struct nla_policy nft_connlimit_policy[NFTA_CONNLIMIT_MAX + 1] = {
	[NFTA_CONNLIMIT_COUNT]	= { .type = NLA_U32 },
	[NFTA_CONNLIMIT_FLAGS]	= { .type = NLA_U32 },
};

static struct nft_object_type nft_connlimit_obj_type;
static const struct nft_object_ops nft_connlimit_obj_ops = {
	.type		= &nft_connlimit_obj_type,
	.size		= sizeof(struct nft_connlimit),
	.eval		= nft_connlimit_obj_eval,
	.init		= nft_connlimit_obj_init,
	.destroy	= nft_connlimit_obj_destroy,
	.dump		= nft_connlimit_obj_dump,
};

static struct nft_object_type nft_connlimit_obj_type __read_mostly = {
	.type		= NFT_OBJECT_CONNLIMIT,
	.ops		= &nft_connlimit_obj_ops,
	.maxattr	= NFTA_CONNLIMIT_MAX,
	.policy		= nft_connlimit_policy,
	.owner		= THIS_MODULE,
};

static void nft_connlimit_eval(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       const struct nft_pktinfo *pkt)
{
	struct nft_connlimit *priv = nft_expr_priv(expr);

	nft_connlimit_do_eval(priv, regs, pkt, NULL);
}

static int nft_connlimit_dump(struct sk_buff *skb,
			      const struct nft_expr *expr, bool reset)
{
	struct nft_connlimit *priv = nft_expr_priv(expr);

	return nft_connlimit_do_dump(skb, priv);
}

static int nft_connlimit_init(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nlattr * const tb[])
{
	struct nft_connlimit *priv = nft_expr_priv(expr);

	return nft_connlimit_do_init(ctx, tb, priv);
}

static void nft_connlimit_destroy(const struct nft_ctx *ctx,
				const struct nft_expr *expr)
{
	struct nft_connlimit *priv = nft_expr_priv(expr);

	nft_connlimit_do_destroy(ctx, priv);
}

static int nft_connlimit_clone(struct nft_expr *dst, const struct nft_expr *src, gfp_t gfp)
{
	struct nft_connlimit *priv_dst = nft_expr_priv(dst);
	struct nft_connlimit *priv_src = nft_expr_priv(src);

	priv_dst->list = kmalloc(sizeof(*priv_dst->list), gfp);
	if (!priv_dst->list)
		return -ENOMEM;

	nf_conncount_list_init(priv_dst->list);
	priv_dst->limit	 = priv_src->limit;
	priv_dst->invert = priv_src->invert;

	return 0;
}

static void nft_connlimit_destroy_clone(const struct nft_ctx *ctx,
					const struct nft_expr *expr)
{
	struct nft_connlimit *priv = nft_expr_priv(expr);

	nf_conncount_cache_free(priv->list);
	kfree(priv->list);
}

static bool nft_connlimit_gc(struct net *net, const struct nft_expr *expr)
{
	struct nft_connlimit *priv = nft_expr_priv(expr);
	bool ret;

	local_bh_disable();
	ret = nf_conncount_gc_list(net, priv->list);
	local_bh_enable();

	return ret;
}

static struct nft_expr_type nft_connlimit_type;
static const struct nft_expr_ops nft_connlimit_ops = {
	.type		= &nft_connlimit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_connlimit)),
	.eval		= nft_connlimit_eval,
	.init		= nft_connlimit_init,
	.destroy	= nft_connlimit_destroy,
	.clone		= nft_connlimit_clone,
	.destroy_clone	= nft_connlimit_destroy_clone,
	.dump		= nft_connlimit_dump,
	.gc		= nft_connlimit_gc,
	.reduce		= NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_connlimit_type __read_mostly = {
	.name		= "connlimit",
	.ops		= &nft_connlimit_ops,
	.policy		= nft_connlimit_policy,
	.maxattr	= NFTA_CONNLIMIT_MAX,
	.flags		= NFT_EXPR_STATEFUL | NFT_EXPR_GC,
	.owner		= THIS_MODULE,
};

static int __init nft_connlimit_module_init(void)
{
	int err;

	err = nft_register_obj(&nft_connlimit_obj_type);
	if (err < 0)
		return err;

	err = nft_register_expr(&nft_connlimit_type);
	if (err < 0)
		goto err1;

	return 0;
err1:
	nft_unregister_obj(&nft_connlimit_obj_type);
	return err;
}

static void __exit nft_connlimit_module_exit(void)
{
	nft_unregister_expr(&nft_connlimit_type);
	nft_unregister_obj(&nft_connlimit_obj_type);
}

module_init(nft_connlimit_module_init);
module_exit(nft_connlimit_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso");
MODULE_ALIAS_NFT_EXPR("connlimit");
MODULE_ALIAS_NFT_OBJ(NFT_OBJECT_CONNLIMIT);
MODULE_DESCRIPTION("nftables connlimit rule support");
