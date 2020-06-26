// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_tables.h>
#include <net/ip.h> /* for ipv4 options. */
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_flow_table.h>

struct nft_flow_offload {
	struct nft_flowtable	*flowtable;
};

static int nft_flow_route(const struct nft_pktinfo *pkt,
			  const struct nf_conn *ct,
			  struct nf_flow_route *route,
			  enum ip_conntrack_dir dir)
{
	struct dst_entry *this_dst = skb_dst(pkt->skb);
	struct dst_entry *other_dst = NULL;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		fl.u.ip4.daddr = ct->tuplehash[dir].tuple.src.u3.ip;
		fl.u.ip4.flowi4_oif = nft_in(pkt)->ifindex;
		break;
	case NFPROTO_IPV6:
		fl.u.ip6.daddr = ct->tuplehash[dir].tuple.src.u3.in6;
		fl.u.ip6.flowi6_oif = nft_in(pkt)->ifindex;
		break;
	}

	nf_route(nft_net(pkt), &other_dst, &fl, false, nft_pf(pkt));
	if (!other_dst)
		return -ENOENT;

	route->tuple[dir].dst		= this_dst;
	route->tuple[!dir].dst		= other_dst;

	return 0;
}

static bool nft_flow_offload_skip(struct sk_buff *skb, int family)
{
	if (skb_sec_path(skb))
		return true;

	if (family == NFPROTO_IPV4) {
		const struct ip_options *opt;

		opt = &(IPCB(skb)->opt);

		if (unlikely(opt->optlen))
			return true;
	}

	return false;
}

static void nft_flow_offload_eval(const struct nft_expr *expr,
				  struct nft_regs *regs,
				  const struct nft_pktinfo *pkt)
{
	struct nft_flow_offload *priv = nft_expr_priv(expr);
	struct nf_flowtable *flowtable = &priv->flowtable->data;
	struct tcphdr _tcph, *tcph = NULL;
	enum ip_conntrack_info ctinfo;
	struct nf_flow_route route;
	struct flow_offload *flow;
	enum ip_conntrack_dir dir;
	struct nf_conn *ct;
	int ret;

	if (nft_flow_offload_skip(pkt->skb, nft_pf(pkt)))
		goto out;

	ct = nf_ct_get(pkt->skb, &ctinfo);
	if (!ct)
		goto out;

	switch (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum) {
	case IPPROTO_TCP:
		tcph = skb_header_pointer(pkt->skb, pkt->xt.thoff,
					  sizeof(_tcph), &_tcph);
		if (unlikely(!tcph || tcph->fin || tcph->rst))
			goto out;
		break;
	case IPPROTO_UDP:
		break;
	default:
		goto out;
	}

	if (nf_ct_ext_exist(ct, NF_CT_EXT_HELPER) ||
	    ct->status & IPS_SEQ_ADJUST)
		goto out;

	if (!nf_ct_is_confirmed(ct))
		goto out;

	if (test_and_set_bit(IPS_OFFLOAD_BIT, &ct->status))
		goto out;

	dir = CTINFO2DIR(ctinfo);
	if (nft_flow_route(pkt, ct, &route, dir) < 0)
		goto err_flow_route;

	flow = flow_offload_alloc(ct);
	if (!flow)
		goto err_flow_alloc;

	if (flow_offload_route_init(flow, &route) < 0)
		goto err_flow_add;

	if (tcph) {
		ct->proto.tcp.seen[0].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
		ct->proto.tcp.seen[1].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
	}

	ret = flow_offload_add(flowtable, flow);
	if (ret < 0)
		goto err_flow_add;

	dst_release(route.tuple[!dir].dst);
	return;

err_flow_add:
	flow_offload_free(flow);
err_flow_alloc:
	dst_release(route.tuple[!dir].dst);
err_flow_route:
	clear_bit(IPS_OFFLOAD_BIT, &ct->status);
out:
	regs->verdict.code = NFT_BREAK;
}

static int nft_flow_offload_validate(const struct nft_ctx *ctx,
				     const struct nft_expr *expr,
				     const struct nft_data **data)
{
	unsigned int hook_mask = (1 << NF_INET_FORWARD);

	return nft_chain_validate_hooks(ctx->chain, hook_mask);
}

static const struct nla_policy nft_flow_offload_policy[NFTA_FLOW_MAX + 1] = {
	[NFTA_FLOW_TABLE_NAME]	= { .type = NLA_STRING,
				    .len = NFT_NAME_MAXLEN - 1 },
};

static int nft_flow_offload_init(const struct nft_ctx *ctx,
				 const struct nft_expr *expr,
				 const struct nlattr * const tb[])
{
	struct nft_flow_offload *priv = nft_expr_priv(expr);
	u8 genmask = nft_genmask_next(ctx->net);
	struct nft_flowtable *flowtable;

	if (!tb[NFTA_FLOW_TABLE_NAME])
		return -EINVAL;

	flowtable = nft_flowtable_lookup(ctx->table, tb[NFTA_FLOW_TABLE_NAME],
					 genmask);
	if (IS_ERR(flowtable))
		return PTR_ERR(flowtable);

	priv->flowtable = flowtable;
	flowtable->use++;

	return nf_ct_netns_get(ctx->net, ctx->family);
}

static void nft_flow_offload_deactivate(const struct nft_ctx *ctx,
					const struct nft_expr *expr,
					enum nft_trans_phase phase)
{
	struct nft_flow_offload *priv = nft_expr_priv(expr);

	nf_tables_deactivate_flowtable(ctx, priv->flowtable, phase);
}

static void nft_flow_offload_activate(const struct nft_ctx *ctx,
				      const struct nft_expr *expr)
{
	struct nft_flow_offload *priv = nft_expr_priv(expr);

	priv->flowtable->use++;
}

static void nft_flow_offload_destroy(const struct nft_ctx *ctx,
				     const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, ctx->family);
}

static int nft_flow_offload_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_flow_offload *priv = nft_expr_priv(expr);

	if (nla_put_string(skb, NFTA_FLOW_TABLE_NAME, priv->flowtable->name))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_flow_offload_type;
static const struct nft_expr_ops nft_flow_offload_ops = {
	.type		= &nft_flow_offload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_flow_offload)),
	.eval		= nft_flow_offload_eval,
	.init		= nft_flow_offload_init,
	.activate	= nft_flow_offload_activate,
	.deactivate	= nft_flow_offload_deactivate,
	.destroy	= nft_flow_offload_destroy,
	.validate	= nft_flow_offload_validate,
	.dump		= nft_flow_offload_dump,
};

static struct nft_expr_type nft_flow_offload_type __read_mostly = {
	.name		= "flow_offload",
	.ops		= &nft_flow_offload_ops,
	.policy		= nft_flow_offload_policy,
	.maxattr	= NFTA_FLOW_MAX,
	.owner		= THIS_MODULE,
};

static int flow_offload_netdev_event(struct notifier_block *this,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;

	nf_flow_table_cleanup(dev);

	return NOTIFY_DONE;
}

static struct notifier_block flow_offload_netdev_notifier = {
	.notifier_call	= flow_offload_netdev_event,
};

static int __init nft_flow_offload_module_init(void)
{
	int err;

	err = register_netdevice_notifier(&flow_offload_netdev_notifier);
	if (err)
		goto err;

	err = nft_register_expr(&nft_flow_offload_type);
	if (err < 0)
		goto register_expr;

	return 0;

register_expr:
	unregister_netdevice_notifier(&flow_offload_netdev_notifier);
err:
	return err;
}

static void __exit nft_flow_offload_module_exit(void)
{
	nft_unregister_expr(&nft_flow_offload_type);
	unregister_netdevice_notifier(&flow_offload_netdev_notifier);
}

module_init(nft_flow_offload_module_init);
module_exit(nft_flow_offload_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_EXPR("flow_offload");
MODULE_DESCRIPTION("nftables hardware flow offload module");
