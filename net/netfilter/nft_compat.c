// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 2012-2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This software has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nf_tables_compat.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_arp/arp_tables.h>
#include <net/netfilter/nf_tables.h>

/* Used for matches where *info is larger than X byte */
#define NFT_MATCH_LARGE_THRESH	192

struct nft_xt_match_priv {
	void *info;
};

static int nft_compat_chain_validate_dependency(const struct nft_ctx *ctx,
						const char *tablename)
{
	enum nft_chain_types type = NFT_CHAIN_T_DEFAULT;
	const struct nft_chain *chain = ctx->chain;
	const struct nft_base_chain *basechain;

	if (!tablename ||
	    !nft_is_base_chain(chain))
		return 0;

	basechain = nft_base_chain(chain);
	if (strcmp(tablename, "nat") == 0) {
		if (ctx->family != NFPROTO_BRIDGE)
			type = NFT_CHAIN_T_NAT;
		if (basechain->type->type != type)
			return -EINVAL;
	}

	return 0;
}

union nft_entry {
	struct ipt_entry e4;
	struct ip6t_entry e6;
	struct ebt_entry ebt;
	struct arpt_entry arp;
};

static inline void
nft_compat_set_par(struct xt_action_param *par,
		   const struct nft_pktinfo *pkt,
		   const void *xt, const void *xt_info)
{
	par->state	= pkt->state;
	par->thoff	= nft_thoff(pkt);
	par->fragoff	= pkt->fragoff;
	par->target	= xt;
	par->targinfo	= xt_info;
	par->hotdrop	= false;
}

static void nft_target_eval_xt(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       const struct nft_pktinfo *pkt)
{
	void *info = nft_expr_priv(expr);
	struct xt_target *target = expr->ops->data;
	struct sk_buff *skb = pkt->skb;
	struct xt_action_param xt;
	int ret;

	nft_compat_set_par(&xt, pkt, target, info);

	ret = target->target(skb, &xt);

	if (xt.hotdrop)
		ret = NF_DROP;

	switch (ret) {
	case XT_CONTINUE:
		regs->verdict.code = NFT_CONTINUE;
		break;
	default:
		regs->verdict.code = ret;
		break;
	}
}

static void nft_target_eval_bridge(const struct nft_expr *expr,
				   struct nft_regs *regs,
				   const struct nft_pktinfo *pkt)
{
	void *info = nft_expr_priv(expr);
	struct xt_target *target = expr->ops->data;
	struct sk_buff *skb = pkt->skb;
	struct xt_action_param xt;
	int ret;

	nft_compat_set_par(&xt, pkt, target, info);

	ret = target->target(skb, &xt);

	if (xt.hotdrop)
		ret = NF_DROP;

	switch (ret) {
	case EBT_ACCEPT:
		regs->verdict.code = NF_ACCEPT;
		break;
	case EBT_DROP:
		regs->verdict.code = NF_DROP;
		break;
	case EBT_CONTINUE:
		regs->verdict.code = NFT_CONTINUE;
		break;
	case EBT_RETURN:
		regs->verdict.code = NFT_RETURN;
		break;
	default:
		regs->verdict.code = ret;
		break;
	}
}

static const struct nla_policy nft_target_policy[NFTA_TARGET_MAX + 1] = {
	[NFTA_TARGET_NAME]	= { .type = NLA_NUL_STRING },
	[NFTA_TARGET_REV]	= { .type = NLA_U32 },
	[NFTA_TARGET_INFO]	= { .type = NLA_BINARY },
};

static void
nft_target_set_tgchk_param(struct xt_tgchk_param *par,
			   const struct nft_ctx *ctx,
			   struct xt_target *target, void *info,
			   union nft_entry *entry, u16 proto, bool inv)
{
	par->net	= ctx->net;
	par->table	= ctx->table->name;
	switch (ctx->family) {
	case AF_INET:
		entry->e4.ip.proto = proto;
		entry->e4.ip.invflags = inv ? IPT_INV_PROTO : 0;
		break;
	case AF_INET6:
		if (proto)
			entry->e6.ipv6.flags |= IP6T_F_PROTO;

		entry->e6.ipv6.proto = proto;
		entry->e6.ipv6.invflags = inv ? IP6T_INV_PROTO : 0;
		break;
	case NFPROTO_BRIDGE:
		entry->ebt.ethproto = (__force __be16)proto;
		entry->ebt.invflags = inv ? EBT_IPROTO : 0;
		break;
	case NFPROTO_ARP:
		break;
	}
	par->entryinfo	= entry;
	par->target	= target;
	par->targinfo	= info;
	if (nft_is_base_chain(ctx->chain)) {
		const struct nft_base_chain *basechain =
						nft_base_chain(ctx->chain);
		const struct nf_hook_ops *ops = &basechain->ops;

		par->hook_mask = 1 << ops->hooknum;
	} else {
		par->hook_mask = 0;
	}
	par->family	= ctx->family;
	par->nft_compat = true;
}

static void target_compat_from_user(struct xt_target *t, void *in, void *out)
{
	int pad;

	memcpy(out, in, t->targetsize);
	pad = XT_ALIGN(t->targetsize) - t->targetsize;
	if (pad > 0)
		memset(out + t->targetsize, 0, pad);
}

static const struct nla_policy nft_rule_compat_policy[NFTA_RULE_COMPAT_MAX + 1] = {
	[NFTA_RULE_COMPAT_PROTO]	= { .type = NLA_U32 },
	[NFTA_RULE_COMPAT_FLAGS]	= { .type = NLA_U32 },
};

static int nft_parse_compat(const struct nlattr *attr, u16 *proto, bool *inv)
{
	struct nlattr *tb[NFTA_RULE_COMPAT_MAX+1];
	u32 flags;
	int err;

	err = nla_parse_nested_deprecated(tb, NFTA_RULE_COMPAT_MAX, attr,
					  nft_rule_compat_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_RULE_COMPAT_PROTO] || !tb[NFTA_RULE_COMPAT_FLAGS])
		return -EINVAL;

	flags = ntohl(nla_get_be32(tb[NFTA_RULE_COMPAT_FLAGS]));
	if (flags & ~NFT_RULE_COMPAT_F_MASK)
		return -EINVAL;
	if (flags & NFT_RULE_COMPAT_F_INV)
		*inv = true;

	*proto = ntohl(nla_get_be32(tb[NFTA_RULE_COMPAT_PROTO]));
	return 0;
}

static void nft_compat_wait_for_destructors(void)
{
	/* xtables matches or targets can have side effects, e.g.
	 * creation/destruction of /proc files.
	 * The xt ->destroy functions are run asynchronously from
	 * work queue.  If we have pending invocations we thus
	 * need to wait for those to finish.
	 */
	nf_tables_trans_destroy_flush_work();
}

static int
nft_target_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
		const struct nlattr * const tb[])
{
	void *info = nft_expr_priv(expr);
	struct xt_target *target = expr->ops->data;
	struct xt_tgchk_param par;
	size_t size = XT_ALIGN(nla_len(tb[NFTA_TARGET_INFO]));
	u16 proto = 0;
	bool inv = false;
	union nft_entry e = {};
	int ret;

	target_compat_from_user(target, nla_data(tb[NFTA_TARGET_INFO]), info);

	if (ctx->nla[NFTA_RULE_COMPAT]) {
		ret = nft_parse_compat(ctx->nla[NFTA_RULE_COMPAT], &proto, &inv);
		if (ret < 0)
			return ret;
	}

	nft_target_set_tgchk_param(&par, ctx, target, info, &e, proto, inv);

	nft_compat_wait_for_destructors();

	ret = xt_check_target(&par, size, proto, inv);
	if (ret < 0)
		return ret;

	/* The standard target cannot be used */
	if (!target->target)
		return -EINVAL;

	return 0;
}

static void __nft_mt_tg_destroy(struct module *me, const struct nft_expr *expr)
{
	module_put(me);
	kfree(expr->ops);
}

static void
nft_target_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	struct xt_target *target = expr->ops->data;
	void *info = nft_expr_priv(expr);
	struct module *me = target->me;
	struct xt_tgdtor_param par;

	par.net = ctx->net;
	par.target = target;
	par.targinfo = info;
	par.family = ctx->family;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);

	__nft_mt_tg_destroy(me, expr);
}

static int nft_extension_dump_info(struct sk_buff *skb, int attr,
				   const void *info,
				   unsigned int size, unsigned int user_size)
{
	unsigned int info_size, aligned_size = XT_ALIGN(size);
	struct nlattr *nla;

	nla = nla_reserve(skb, attr, aligned_size);
	if (!nla)
		return -1;

	info_size = user_size ? : size;
	memcpy(nla_data(nla), info, info_size);
	memset(nla_data(nla) + info_size, 0, aligned_size - info_size);

	return 0;
}

static int nft_target_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct xt_target *target = expr->ops->data;
	void *info = nft_expr_priv(expr);

	if (nla_put_string(skb, NFTA_TARGET_NAME, target->name) ||
	    nla_put_be32(skb, NFTA_TARGET_REV, htonl(target->revision)) ||
	    nft_extension_dump_info(skb, NFTA_TARGET_INFO, info,
				    target->targetsize, target->usersize))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_target_validate(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nft_data **data)
{
	struct xt_target *target = expr->ops->data;
	unsigned int hook_mask = 0;
	int ret;

	if (nft_is_base_chain(ctx->chain)) {
		const struct nft_base_chain *basechain =
						nft_base_chain(ctx->chain);
		const struct nf_hook_ops *ops = &basechain->ops;

		hook_mask = 1 << ops->hooknum;
		if (target->hooks && !(hook_mask & target->hooks))
			return -EINVAL;

		ret = nft_compat_chain_validate_dependency(ctx, target->table);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void __nft_match_eval(const struct nft_expr *expr,
			     struct nft_regs *regs,
			     const struct nft_pktinfo *pkt,
			     void *info)
{
	struct xt_match *match = expr->ops->data;
	struct sk_buff *skb = pkt->skb;
	struct xt_action_param xt;
	bool ret;

	nft_compat_set_par(&xt, pkt, match, info);

	ret = match->match(skb, &xt);

	if (xt.hotdrop) {
		regs->verdict.code = NF_DROP;
		return;
	}

	switch (ret ? 1 : 0) {
	case 1:
		regs->verdict.code = NFT_CONTINUE;
		break;
	case 0:
		regs->verdict.code = NFT_BREAK;
		break;
	}
}

static void nft_match_large_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	struct nft_xt_match_priv *priv = nft_expr_priv(expr);

	__nft_match_eval(expr, regs, pkt, priv->info);
}

static void nft_match_eval(const struct nft_expr *expr,
			   struct nft_regs *regs,
			   const struct nft_pktinfo *pkt)
{
	__nft_match_eval(expr, regs, pkt, nft_expr_priv(expr));
}

static const struct nla_policy nft_match_policy[NFTA_MATCH_MAX + 1] = {
	[NFTA_MATCH_NAME]	= { .type = NLA_NUL_STRING },
	[NFTA_MATCH_REV]	= { .type = NLA_U32 },
	[NFTA_MATCH_INFO]	= { .type = NLA_BINARY },
};

/* struct xt_mtchk_param and xt_tgchk_param look very similar */
static void
nft_match_set_mtchk_param(struct xt_mtchk_param *par, const struct nft_ctx *ctx,
			  struct xt_match *match, void *info,
			  union nft_entry *entry, u16 proto, bool inv)
{
	par->net	= ctx->net;
	par->table	= ctx->table->name;
	switch (ctx->family) {
	case AF_INET:
		entry->e4.ip.proto = proto;
		entry->e4.ip.invflags = inv ? IPT_INV_PROTO : 0;
		break;
	case AF_INET6:
		if (proto)
			entry->e6.ipv6.flags |= IP6T_F_PROTO;

		entry->e6.ipv6.proto = proto;
		entry->e6.ipv6.invflags = inv ? IP6T_INV_PROTO : 0;
		break;
	case NFPROTO_BRIDGE:
		entry->ebt.ethproto = (__force __be16)proto;
		entry->ebt.invflags = inv ? EBT_IPROTO : 0;
		break;
	case NFPROTO_ARP:
		break;
	}
	par->entryinfo	= entry;
	par->match	= match;
	par->matchinfo	= info;
	if (nft_is_base_chain(ctx->chain)) {
		const struct nft_base_chain *basechain =
						nft_base_chain(ctx->chain);
		const struct nf_hook_ops *ops = &basechain->ops;

		par->hook_mask = 1 << ops->hooknum;
	} else {
		par->hook_mask = 0;
	}
	par->family	= ctx->family;
	par->nft_compat = true;
}

static void match_compat_from_user(struct xt_match *m, void *in, void *out)
{
	int pad;

	memcpy(out, in, m->matchsize);
	pad = XT_ALIGN(m->matchsize) - m->matchsize;
	if (pad > 0)
		memset(out + m->matchsize, 0, pad);
}

static int
__nft_match_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
		 const struct nlattr * const tb[],
		 void *info)
{
	struct xt_match *match = expr->ops->data;
	struct xt_mtchk_param par;
	size_t size = XT_ALIGN(nla_len(tb[NFTA_MATCH_INFO]));
	u16 proto = 0;
	bool inv = false;
	union nft_entry e = {};
	int ret;

	match_compat_from_user(match, nla_data(tb[NFTA_MATCH_INFO]), info);

	if (ctx->nla[NFTA_RULE_COMPAT]) {
		ret = nft_parse_compat(ctx->nla[NFTA_RULE_COMPAT], &proto, &inv);
		if (ret < 0)
			return ret;
	}

	nft_match_set_mtchk_param(&par, ctx, match, info, &e, proto, inv);

	nft_compat_wait_for_destructors();

	return xt_check_match(&par, size, proto, inv);
}

static int
nft_match_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
	       const struct nlattr * const tb[])
{
	return __nft_match_init(ctx, expr, tb, nft_expr_priv(expr));
}

static int
nft_match_large_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
		     const struct nlattr * const tb[])
{
	struct nft_xt_match_priv *priv = nft_expr_priv(expr);
	struct xt_match *m = expr->ops->data;
	int ret;

	priv->info = kmalloc(XT_ALIGN(m->matchsize), GFP_KERNEL);
	if (!priv->info)
		return -ENOMEM;

	ret = __nft_match_init(ctx, expr, tb, priv->info);
	if (ret)
		kfree(priv->info);
	return ret;
}

static void
__nft_match_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr,
		    void *info)
{
	struct xt_match *match = expr->ops->data;
	struct module *me = match->me;
	struct xt_mtdtor_param par;

	par.net = ctx->net;
	par.match = match;
	par.matchinfo = info;
	par.family = ctx->family;
	if (par.match->destroy != NULL)
		par.match->destroy(&par);

	__nft_mt_tg_destroy(me, expr);
}

static void
nft_match_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	__nft_match_destroy(ctx, expr, nft_expr_priv(expr));
}

static void
nft_match_large_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	struct nft_xt_match_priv *priv = nft_expr_priv(expr);

	__nft_match_destroy(ctx, expr, priv->info);
	kfree(priv->info);
}

static int __nft_match_dump(struct sk_buff *skb, const struct nft_expr *expr,
			    void *info)
{
	struct xt_match *match = expr->ops->data;

	if (nla_put_string(skb, NFTA_MATCH_NAME, match->name) ||
	    nla_put_be32(skb, NFTA_MATCH_REV, htonl(match->revision)) ||
	    nft_extension_dump_info(skb, NFTA_MATCH_INFO, info,
				    match->matchsize, match->usersize))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_match_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	return __nft_match_dump(skb, expr, nft_expr_priv(expr));
}

static int nft_match_large_dump(struct sk_buff *skb, const struct nft_expr *e)
{
	struct nft_xt_match_priv *priv = nft_expr_priv(e);

	return __nft_match_dump(skb, e, priv->info);
}

static int nft_match_validate(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nft_data **data)
{
	struct xt_match *match = expr->ops->data;
	unsigned int hook_mask = 0;
	int ret;

	if (nft_is_base_chain(ctx->chain)) {
		const struct nft_base_chain *basechain =
						nft_base_chain(ctx->chain);
		const struct nf_hook_ops *ops = &basechain->ops;

		hook_mask = 1 << ops->hooknum;
		if (match->hooks && !(hook_mask & match->hooks))
			return -EINVAL;

		ret = nft_compat_chain_validate_dependency(ctx, match->table);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int
nfnl_compat_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
		      int event, u16 family, const char *name,
		      int rev, int target)
{
	struct nlmsghdr *nlh;
	unsigned int flags = portid ? NLM_F_MULTI : 0;

	event = nfnl_msg_type(NFNL_SUBSYS_NFT_COMPAT, event);
	nlh = nfnl_msg_put(skb, portid, seq, event, flags, family,
			   NFNETLINK_V0, 0);
	if (!nlh)
		goto nlmsg_failure;

	if (nla_put_string(skb, NFTA_COMPAT_NAME, name) ||
	    nla_put_be32(skb, NFTA_COMPAT_REV, htonl(rev)) ||
	    nla_put_be32(skb, NFTA_COMPAT_TYPE, htonl(target)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int nfnl_compat_get_rcu(struct sk_buff *skb,
			       const struct nfnl_info *info,
			       const struct nlattr * const tb[])
{
	u8 family = info->nfmsg->nfgen_family;
	const char *name, *fmt;
	struct sk_buff *skb2;
	int ret = 0, target;
	u32 rev;

	if (tb[NFTA_COMPAT_NAME] == NULL ||
	    tb[NFTA_COMPAT_REV] == NULL ||
	    tb[NFTA_COMPAT_TYPE] == NULL)
		return -EINVAL;

	name = nla_data(tb[NFTA_COMPAT_NAME]);
	rev = ntohl(nla_get_be32(tb[NFTA_COMPAT_REV]));
	target = ntohl(nla_get_be32(tb[NFTA_COMPAT_TYPE]));

	switch(family) {
	case AF_INET:
		fmt = "ipt_%s";
		break;
	case AF_INET6:
		fmt = "ip6t_%s";
		break;
	case NFPROTO_BRIDGE:
		fmt = "ebt_%s";
		break;
	case NFPROTO_ARP:
		fmt = "arpt_%s";
		break;
	default:
		pr_err("nft_compat: unsupported protocol %d\n", family);
		return -EINVAL;
	}

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	rcu_read_unlock();
	try_then_request_module(xt_find_revision(family, name, rev, target, &ret),
				fmt, name);
	if (ret < 0)
		goto out_put;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL) {
		ret = -ENOMEM;
		goto out_put;
	}

	/* include the best revision for this extension in the message */
	if (nfnl_compat_fill_info(skb2, NETLINK_CB(skb).portid,
				  info->nlh->nlmsg_seq,
				  NFNL_MSG_TYPE(info->nlh->nlmsg_type),
				  NFNL_MSG_COMPAT_GET,
				  family, name, ret, target) <= 0) {
		kfree_skb(skb2);
		goto out_put;
	}

	ret = netlink_unicast(info->sk, skb2, NETLINK_CB(skb).portid,
			      MSG_DONTWAIT);
	if (ret > 0)
		ret = 0;
out_put:
	rcu_read_lock();
	module_put(THIS_MODULE);
	return ret == -EAGAIN ? -ENOBUFS : ret;
}

static const struct nla_policy nfnl_compat_policy_get[NFTA_COMPAT_MAX+1] = {
	[NFTA_COMPAT_NAME]	= { .type = NLA_NUL_STRING,
				    .len = NFT_COMPAT_NAME_MAX-1 },
	[NFTA_COMPAT_REV]	= { .type = NLA_U32 },
	[NFTA_COMPAT_TYPE]	= { .type = NLA_U32 },
};

static const struct nfnl_callback nfnl_nft_compat_cb[NFNL_MSG_COMPAT_MAX] = {
	[NFNL_MSG_COMPAT_GET]	= {
		.call		= nfnl_compat_get_rcu,
		.type		= NFNL_CB_RCU,
		.attr_count	= NFTA_COMPAT_MAX,
		.policy		= nfnl_compat_policy_get
	},
};

static const struct nfnetlink_subsystem nfnl_compat_subsys = {
	.name		= "nft-compat",
	.subsys_id	= NFNL_SUBSYS_NFT_COMPAT,
	.cb_count	= NFNL_MSG_COMPAT_MAX,
	.cb		= nfnl_nft_compat_cb,
};

static struct nft_expr_type nft_match_type;

static const struct nft_expr_ops *
nft_match_select_ops(const struct nft_ctx *ctx,
		     const struct nlattr * const tb[])
{
	struct nft_expr_ops *ops;
	struct xt_match *match;
	unsigned int matchsize;
	char *mt_name;
	u32 rev, family;
	int err;

	if (tb[NFTA_MATCH_NAME] == NULL ||
	    tb[NFTA_MATCH_REV] == NULL ||
	    tb[NFTA_MATCH_INFO] == NULL)
		return ERR_PTR(-EINVAL);

	mt_name = nla_data(tb[NFTA_MATCH_NAME]);
	rev = ntohl(nla_get_be32(tb[NFTA_MATCH_REV]));
	family = ctx->family;

	match = xt_request_find_match(family, mt_name, rev);
	if (IS_ERR(match))
		return ERR_PTR(-ENOENT);

	if (match->matchsize > nla_len(tb[NFTA_MATCH_INFO])) {
		err = -EINVAL;
		goto err;
	}

	ops = kzalloc(sizeof(struct nft_expr_ops), GFP_KERNEL);
	if (!ops) {
		err = -ENOMEM;
		goto err;
	}

	ops->type = &nft_match_type;
	ops->eval = nft_match_eval;
	ops->init = nft_match_init;
	ops->destroy = nft_match_destroy;
	ops->dump = nft_match_dump;
	ops->validate = nft_match_validate;
	ops->data = match;

	matchsize = NFT_EXPR_SIZE(XT_ALIGN(match->matchsize));
	if (matchsize > NFT_MATCH_LARGE_THRESH) {
		matchsize = NFT_EXPR_SIZE(sizeof(struct nft_xt_match_priv));

		ops->eval = nft_match_large_eval;
		ops->init = nft_match_large_init;
		ops->destroy = nft_match_large_destroy;
		ops->dump = nft_match_large_dump;
	}

	ops->size = matchsize;

	return ops;
err:
	module_put(match->me);
	return ERR_PTR(err);
}

static void nft_match_release_ops(const struct nft_expr_ops *ops)
{
	struct xt_match *match = ops->data;

	module_put(match->me);
	kfree(ops);
}

static struct nft_expr_type nft_match_type __read_mostly = {
	.name		= "match",
	.select_ops	= nft_match_select_ops,
	.release_ops	= nft_match_release_ops,
	.policy		= nft_match_policy,
	.maxattr	= NFTA_MATCH_MAX,
	.owner		= THIS_MODULE,
};

static struct nft_expr_type nft_target_type;

static const struct nft_expr_ops *
nft_target_select_ops(const struct nft_ctx *ctx,
		      const struct nlattr * const tb[])
{
	struct nft_expr_ops *ops;
	struct xt_target *target;
	char *tg_name;
	u32 rev, family;
	int err;

	if (tb[NFTA_TARGET_NAME] == NULL ||
	    tb[NFTA_TARGET_REV] == NULL ||
	    tb[NFTA_TARGET_INFO] == NULL)
		return ERR_PTR(-EINVAL);

	tg_name = nla_data(tb[NFTA_TARGET_NAME]);
	rev = ntohl(nla_get_be32(tb[NFTA_TARGET_REV]));
	family = ctx->family;

	if (strcmp(tg_name, XT_ERROR_TARGET) == 0 ||
	    strcmp(tg_name, XT_STANDARD_TARGET) == 0 ||
	    strcmp(tg_name, "standard") == 0)
		return ERR_PTR(-EINVAL);

	target = xt_request_find_target(family, tg_name, rev);
	if (IS_ERR(target))
		return ERR_PTR(-ENOENT);

	if (!target->target) {
		err = -EINVAL;
		goto err;
	}

	if (target->targetsize > nla_len(tb[NFTA_TARGET_INFO])) {
		err = -EINVAL;
		goto err;
	}

	ops = kzalloc(sizeof(struct nft_expr_ops), GFP_KERNEL);
	if (!ops) {
		err = -ENOMEM;
		goto err;
	}

	ops->type = &nft_target_type;
	ops->size = NFT_EXPR_SIZE(XT_ALIGN(target->targetsize));
	ops->init = nft_target_init;
	ops->destroy = nft_target_destroy;
	ops->dump = nft_target_dump;
	ops->validate = nft_target_validate;
	ops->data = target;

	if (family == NFPROTO_BRIDGE)
		ops->eval = nft_target_eval_bridge;
	else
		ops->eval = nft_target_eval_xt;

	return ops;
err:
	module_put(target->me);
	return ERR_PTR(err);
}

static void nft_target_release_ops(const struct nft_expr_ops *ops)
{
	struct xt_target *target = ops->data;

	module_put(target->me);
	kfree(ops);
}

static struct nft_expr_type nft_target_type __read_mostly = {
	.name		= "target",
	.select_ops	= nft_target_select_ops,
	.release_ops	= nft_target_release_ops,
	.policy		= nft_target_policy,
	.maxattr	= NFTA_TARGET_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_compat_module_init(void)
{
	int ret;

	ret = nft_register_expr(&nft_match_type);
	if (ret < 0)
		return ret;

	ret = nft_register_expr(&nft_target_type);
	if (ret < 0)
		goto err_match;

	ret = nfnetlink_subsys_register(&nfnl_compat_subsys);
	if (ret < 0) {
		pr_err("nft_compat: cannot register with nfnetlink.\n");
		goto err_target;
	}

	return ret;
err_target:
	nft_unregister_expr(&nft_target_type);
err_match:
	nft_unregister_expr(&nft_match_type);
	return ret;
}

static void __exit nft_compat_module_exit(void)
{
	nfnetlink_subsys_unregister(&nfnl_compat_subsys);
	nft_unregister_expr(&nft_target_type);
	nft_unregister_expr(&nft_match_type);
}

MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_NFT_COMPAT);

module_init(nft_compat_module_init);
module_exit(nft_compat_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_EXPR("match");
MODULE_ALIAS_NFT_EXPR("target");
MODULE_DESCRIPTION("x_tables over nftables support");
