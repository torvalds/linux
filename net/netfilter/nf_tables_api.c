/*
 * Copyright (c) 2007-2009 Patrick McHardy <kaber@trash.net>
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
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/net_namespace.h>
#include <net/sock.h>

static LIST_HEAD(nf_tables_expressions);
static LIST_HEAD(nf_tables_objects);

/**
 *	nft_register_afinfo - register nf_tables address family info
 *
 *	@afi: address family info to register
 *
 *	Register the address family for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_afinfo(struct net *net, struct nft_af_info *afi)
{
	INIT_LIST_HEAD(&afi->tables);
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail_rcu(&afi->list, &net->nft.af_info);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_afinfo);

static void __nft_release_afinfo(struct net *net, struct nft_af_info *afi);

/**
 *	nft_unregister_afinfo - unregister nf_tables address family info
 *
 *	@afi: address family info to unregister
 *
 *	Unregister the address family for use with nf_tables.
 */
void nft_unregister_afinfo(struct net *net, struct nft_af_info *afi)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	__nft_release_afinfo(net, afi);
	list_del_rcu(&afi->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_afinfo);

static struct nft_af_info *nft_afinfo_lookup(struct net *net, int family)
{
	struct nft_af_info *afi;

	list_for_each_entry(afi, &net->nft.af_info, list) {
		if (afi->family == family)
			return afi;
	}
	return NULL;
}

static struct nft_af_info *
nf_tables_afinfo_lookup(struct net *net, int family, bool autoload)
{
	struct nft_af_info *afi;

	afi = nft_afinfo_lookup(net, family);
	if (afi != NULL)
		return afi;
#ifdef CONFIG_MODULES
	if (autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-afinfo-%u", family);
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		afi = nft_afinfo_lookup(net, family);
		if (afi != NULL)
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-EAFNOSUPPORT);
}

static void nft_ctx_init(struct nft_ctx *ctx,
			 struct net *net,
			 const struct sk_buff *skb,
			 const struct nlmsghdr *nlh,
			 struct nft_af_info *afi,
			 struct nft_table *table,
			 struct nft_chain *chain,
			 const struct nlattr * const *nla)
{
	ctx->net	= net;
	ctx->afi	= afi;
	ctx->table	= table;
	ctx->chain	= chain;
	ctx->nla   	= nla;
	ctx->portid	= NETLINK_CB(skb).portid;
	ctx->report	= nlmsg_report(nlh);
	ctx->seq	= nlh->nlmsg_seq;
}

static struct nft_trans *nft_trans_alloc_gfp(const struct nft_ctx *ctx,
					     int msg_type, u32 size, gfp_t gfp)
{
	struct nft_trans *trans;

	trans = kzalloc(sizeof(struct nft_trans) + size, gfp);
	if (trans == NULL)
		return NULL;

	trans->msg_type = msg_type;
	trans->ctx	= *ctx;

	return trans;
}

static struct nft_trans *nft_trans_alloc(const struct nft_ctx *ctx,
					 int msg_type, u32 size)
{
	return nft_trans_alloc_gfp(ctx, msg_type, size, GFP_KERNEL);
}

static void nft_trans_destroy(struct nft_trans *trans)
{
	list_del(&trans->list);
	kfree(trans);
}

static int nf_tables_register_hooks(struct net *net,
				    const struct nft_table *table,
				    struct nft_chain *chain,
				    unsigned int hook_nops)
{
	if (table->flags & NFT_TABLE_F_DORMANT ||
	    !(chain->flags & NFT_BASE_CHAIN))
		return 0;

	return nf_register_net_hooks(net, nft_base_chain(chain)->ops,
				     hook_nops);
}

static void nf_tables_unregister_hooks(struct net *net,
				       const struct nft_table *table,
				       struct nft_chain *chain,
				       unsigned int hook_nops)
{
	if (table->flags & NFT_TABLE_F_DORMANT ||
	    !(chain->flags & NFT_BASE_CHAIN))
		return;

	nf_unregister_net_hooks(net, nft_base_chain(chain)->ops, hook_nops);
}

static int nft_trans_table_add(struct nft_ctx *ctx, int msg_type)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_table));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWTABLE)
		nft_activate_next(ctx->net, ctx->table);

	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;
}

static int nft_deltable(struct nft_ctx *ctx)
{
	int err;

	err = nft_trans_table_add(ctx, NFT_MSG_DELTABLE);
	if (err < 0)
		return err;

	nft_deactivate_next(ctx->net, ctx->table);
	return err;
}

static int nft_trans_chain_add(struct nft_ctx *ctx, int msg_type)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_chain));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWCHAIN)
		nft_activate_next(ctx->net, ctx->chain);

	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;
}

static int nft_delchain(struct nft_ctx *ctx)
{
	int err;

	err = nft_trans_chain_add(ctx, NFT_MSG_DELCHAIN);
	if (err < 0)
		return err;

	ctx->table->use--;
	nft_deactivate_next(ctx->net, ctx->chain);

	return err;
}

static int
nf_tables_delrule_deactivate(struct nft_ctx *ctx, struct nft_rule *rule)
{
	/* You cannot delete the same rule twice */
	if (nft_is_active_next(ctx->net, rule)) {
		nft_deactivate_next(ctx->net, rule);
		ctx->chain->use--;
		return 0;
	}
	return -ENOENT;
}

static struct nft_trans *nft_trans_rule_add(struct nft_ctx *ctx, int msg_type,
					    struct nft_rule *rule)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_rule));
	if (trans == NULL)
		return NULL;

	nft_trans_rule(trans) = rule;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return trans;
}

static int nft_delrule(struct nft_ctx *ctx, struct nft_rule *rule)
{
	struct nft_trans *trans;
	int err;

	trans = nft_trans_rule_add(ctx, NFT_MSG_DELRULE, rule);
	if (trans == NULL)
		return -ENOMEM;

	err = nf_tables_delrule_deactivate(ctx, rule);
	if (err < 0) {
		nft_trans_destroy(trans);
		return err;
	}

	return 0;
}

static int nft_delrule_by_chain(struct nft_ctx *ctx)
{
	struct nft_rule *rule;
	int err;

	list_for_each_entry(rule, &ctx->chain->rules, list) {
		err = nft_delrule(ctx, rule);
		if (err < 0)
			return err;
	}
	return 0;
}

static int nft_trans_set_add(struct nft_ctx *ctx, int msg_type,
			     struct nft_set *set)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_set));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWSET && ctx->nla[NFTA_SET_ID] != NULL) {
		nft_trans_set_id(trans) =
			ntohl(nla_get_be32(ctx->nla[NFTA_SET_ID]));
		nft_activate_next(ctx->net, set);
	}
	nft_trans_set(trans) = set;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
}

static int nft_delset(struct nft_ctx *ctx, struct nft_set *set)
{
	int err;

	err = nft_trans_set_add(ctx, NFT_MSG_DELSET, set);
	if (err < 0)
		return err;

	nft_deactivate_next(ctx->net, set);
	ctx->table->use--;

	return err;
}

static int nft_trans_obj_add(struct nft_ctx *ctx, int msg_type,
			     struct nft_object *obj)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_obj));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWOBJ)
		nft_activate_next(ctx->net, obj);

	nft_trans_obj(trans) = obj;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
}

static int nft_delobj(struct nft_ctx *ctx, struct nft_object *obj)
{
	int err;

	err = nft_trans_obj_add(ctx, NFT_MSG_DELOBJ, obj);
	if (err < 0)
		return err;

	nft_deactivate_next(ctx->net, obj);
	ctx->table->use--;

	return err;
}

/*
 * Tables
 */

static struct nft_table *nft_table_lookup(const struct nft_af_info *afi,
					  const struct nlattr *nla,
					  u8 genmask)
{
	struct nft_table *table;

	list_for_each_entry(table, &afi->tables, list) {
		if (!nla_strcmp(nla, table->name) &&
		    nft_active_genmask(table, genmask))
			return table;
	}
	return NULL;
}

static struct nft_table *nf_tables_table_lookup(const struct nft_af_info *afi,
						const struct nlattr *nla,
						u8 genmask)
{
	struct nft_table *table;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	table = nft_table_lookup(afi, nla, genmask);
	if (table != NULL)
		return table;

	return ERR_PTR(-ENOENT);
}

static inline u64 nf_tables_alloc_handle(struct nft_table *table)
{
	return ++table->hgenerator;
}

static const struct nf_chain_type *chain_type[AF_MAX][NFT_CHAIN_T_MAX];

static const struct nf_chain_type *
__nf_tables_chain_type_lookup(int family, const struct nlattr *nla)
{
	int i;

	for (i = 0; i < NFT_CHAIN_T_MAX; i++) {
		if (chain_type[family][i] != NULL &&
		    !nla_strcmp(nla, chain_type[family][i]->name))
			return chain_type[family][i];
	}
	return NULL;
}

static const struct nf_chain_type *
nf_tables_chain_type_lookup(const struct nft_af_info *afi,
			    const struct nlattr *nla,
			    bool autoload)
{
	const struct nf_chain_type *type;

	type = __nf_tables_chain_type_lookup(afi->family, nla);
	if (type != NULL)
		return type;
#ifdef CONFIG_MODULES
	if (autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-chain-%u-%.*s", afi->family,
			       nla_len(nla), (const char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		type = __nf_tables_chain_type_lookup(afi->family, nla);
		if (type != NULL)
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-ENOENT);
}

static const struct nla_policy nft_table_policy[NFTA_TABLE_MAX + 1] = {
	[NFTA_TABLE_NAME]	= { .type = NLA_STRING,
				    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_TABLE_FLAGS]	= { .type = NLA_U32 },
};

static int nf_tables_fill_table_info(struct sk_buff *skb, struct net *net,
				     u32 portid, u32 seq, int event, u32 flags,
				     int family, const struct nft_table *table)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_TABLE_NAME, table->name) ||
	    nla_put_be32(skb, NFTA_TABLE_FLAGS, htonl(table->flags)) ||
	    nla_put_be32(skb, NFTA_TABLE_USE, htonl(table->use)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_table_notify(const struct nft_ctx *ctx, int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_table_info(skb, ctx->net, ctx->portid, ctx->seq,
					event, 0, ctx->afi->family, ctx->table);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
			     ctx->report, GFP_KERNEL);
err:
	if (err < 0) {
		nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES,
				  err);
	}
	return err;
}

static int nf_tables_dump_tables(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry_rcu(table, &afi->tables, list) {
			if (idx < s_idx)
				goto cont;
			if (idx > s_idx)
				memset(&cb->args[1], 0,
				       sizeof(cb->args) - sizeof(cb->args[0]));
			if (!nft_is_active(net, table))
				continue;
			if (nf_tables_fill_table_info(skb, net,
						      NETLINK_CB(cb->skb).portid,
						      cb->nlh->nlmsg_seq,
						      NFT_MSG_NEWTABLE,
						      NLM_F_MULTI,
						      afi->family, table) < 0)
				goto done;

			nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
			idx++;
		}
	}
done:
	rcu_read_unlock();
	cb->args[0] = idx;
	return skb->len;
}

static int nf_tables_gettable(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	struct sk_buff *skb2;
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_tables,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_table_info(skb2, net, NETLINK_CB(skb).portid,
					nlh->nlmsg_seq, NFT_MSG_NEWTABLE, 0,
					family, table);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);

err:
	kfree_skb(skb2);
	return err;
}

static int nf_tables_table_enable(struct net *net,
				  const struct nft_af_info *afi,
				  struct nft_table *table)
{
	struct nft_chain *chain;
	int err, i = 0;

	list_for_each_entry(chain, &table->chains, list) {
		if (!nft_is_active_next(net, chain))
			continue;
		if (!(chain->flags & NFT_BASE_CHAIN))
			continue;

		err = nf_register_net_hooks(net, nft_base_chain(chain)->ops,
					    afi->nops);
		if (err < 0)
			goto err;

		i++;
	}
	return 0;
err:
	list_for_each_entry(chain, &table->chains, list) {
		if (!nft_is_active_next(net, chain))
			continue;
		if (!(chain->flags & NFT_BASE_CHAIN))
			continue;

		if (i-- <= 0)
			break;

		nf_unregister_net_hooks(net, nft_base_chain(chain)->ops,
					afi->nops);
	}
	return err;
}

static void nf_tables_table_disable(struct net *net,
				    const struct nft_af_info *afi,
				    struct nft_table *table)
{
	struct nft_chain *chain;

	list_for_each_entry(chain, &table->chains, list) {
		if (!nft_is_active_next(net, chain))
			continue;
		if (!(chain->flags & NFT_BASE_CHAIN))
			continue;

		nf_unregister_net_hooks(net, nft_base_chain(chain)->ops,
					afi->nops);
	}
}

static int nf_tables_updtable(struct nft_ctx *ctx)
{
	struct nft_trans *trans;
	u32 flags;
	int ret = 0;

	if (!ctx->nla[NFTA_TABLE_FLAGS])
		return 0;

	flags = ntohl(nla_get_be32(ctx->nla[NFTA_TABLE_FLAGS]));
	if (flags & ~NFT_TABLE_F_DORMANT)
		return -EINVAL;

	if (flags == ctx->table->flags)
		return 0;

	trans = nft_trans_alloc(ctx, NFT_MSG_NEWTABLE,
				sizeof(struct nft_trans_table));
	if (trans == NULL)
		return -ENOMEM;

	if ((flags & NFT_TABLE_F_DORMANT) &&
	    !(ctx->table->flags & NFT_TABLE_F_DORMANT)) {
		nft_trans_table_enable(trans) = false;
	} else if (!(flags & NFT_TABLE_F_DORMANT) &&
		   ctx->table->flags & NFT_TABLE_F_DORMANT) {
		ret = nf_tables_table_enable(ctx->net, ctx->afi, ctx->table);
		if (ret >= 0) {
			ctx->table->flags &= ~NFT_TABLE_F_DORMANT;
			nft_trans_table_enable(trans) = true;
		}
	}
	if (ret < 0)
		goto err;

	nft_trans_table_update(trans) = true;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;
err:
	nft_trans_destroy(trans);
	return ret;
}

static int nf_tables_newtable(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	const struct nlattr *name;
	struct nft_af_info *afi;
	struct nft_table *table;
	int family = nfmsg->nfgen_family;
	u32 flags = 0;
	struct nft_ctx ctx;
	int err;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	name = nla[NFTA_TABLE_NAME];
	table = nf_tables_table_lookup(afi, name, genmask);
	if (IS_ERR(table)) {
		if (PTR_ERR(table) != -ENOENT)
			return PTR_ERR(table);
		table = NULL;
	}

	if (table != NULL) {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		nft_ctx_init(&ctx, net, skb, nlh, afi, table, NULL, nla);
		return nf_tables_updtable(&ctx);
	}

	if (nla[NFTA_TABLE_FLAGS]) {
		flags = ntohl(nla_get_be32(nla[NFTA_TABLE_FLAGS]));
		if (flags & ~NFT_TABLE_F_DORMANT)
			return -EINVAL;
	}

	err = -EAFNOSUPPORT;
	if (!try_module_get(afi->owner))
		goto err1;

	err = -ENOMEM;
	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (table == NULL)
		goto err2;

	nla_strlcpy(table->name, name, NFT_TABLE_MAXNAMELEN);
	INIT_LIST_HEAD(&table->chains);
	INIT_LIST_HEAD(&table->sets);
	INIT_LIST_HEAD(&table->objects);
	table->flags = flags;

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, NULL, nla);
	err = nft_trans_table_add(&ctx, NFT_MSG_NEWTABLE);
	if (err < 0)
		goto err3;

	list_add_tail_rcu(&table->list, &afi->tables);
	return 0;
err3:
	kfree(table);
err2:
	module_put(afi->owner);
err1:
	return err;
}

static int nft_flush_table(struct nft_ctx *ctx)
{
	int err;
	struct nft_chain *chain, *nc;
	struct nft_object *obj, *ne;
	struct nft_set *set, *ns;

	list_for_each_entry(chain, &ctx->table->chains, list) {
		if (!nft_is_active_next(ctx->net, chain))
			continue;

		ctx->chain = chain;

		err = nft_delrule_by_chain(ctx);
		if (err < 0)
			goto out;
	}

	list_for_each_entry_safe(set, ns, &ctx->table->sets, list) {
		if (!nft_is_active_next(ctx->net, set))
			continue;

		if (set->flags & NFT_SET_ANONYMOUS &&
		    !list_empty(&set->bindings))
			continue;

		err = nft_delset(ctx, set);
		if (err < 0)
			goto out;
	}

	list_for_each_entry_safe(obj, ne, &ctx->table->objects, list) {
		err = nft_delobj(ctx, obj);
		if (err < 0)
			goto out;
	}

	list_for_each_entry_safe(chain, nc, &ctx->table->chains, list) {
		if (!nft_is_active_next(ctx->net, chain))
			continue;

		ctx->chain = chain;

		err = nft_delchain(ctx);
		if (err < 0)
			goto out;
	}

	err = nft_deltable(ctx);
out:
	return err;
}

static int nft_flush(struct nft_ctx *ctx, int family)
{
	struct nft_af_info *afi;
	struct nft_table *table, *nt;
	const struct nlattr * const *nla = ctx->nla;
	int err = 0;

	list_for_each_entry(afi, &ctx->net->nft.af_info, list) {
		if (family != AF_UNSPEC && afi->family != family)
			continue;

		ctx->afi = afi;
		list_for_each_entry_safe(table, nt, &afi->tables, list) {
			if (!nft_is_active_next(ctx->net, table))
				continue;

			if (nla[NFTA_TABLE_NAME] &&
			    nla_strcmp(nla[NFTA_TABLE_NAME], table->name) != 0)
				continue;

			ctx->table = table;

			err = nft_flush_table(ctx);
			if (err < 0)
				goto out;
		}
	}
out:
	return err;
}

static int nf_tables_deltable(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_af_info *afi;
	struct nft_table *table;
	int family = nfmsg->nfgen_family;
	struct nft_ctx ctx;

	nft_ctx_init(&ctx, net, skb, nlh, NULL, NULL, NULL, nla);
	if (family == AF_UNSPEC || nla[NFTA_TABLE_NAME] == NULL)
		return nft_flush(&ctx, family);

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	ctx.afi = afi;
	ctx.table = table;

	return nft_flush_table(&ctx);
}

static void nf_tables_table_destroy(struct nft_ctx *ctx)
{
	BUG_ON(ctx->table->use > 0);

	kfree(ctx->table);
	module_put(ctx->afi->owner);
}

int nft_register_chain_type(const struct nf_chain_type *ctype)
{
	int err = 0;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	if (chain_type[ctype->family][ctype->type] != NULL) {
		err = -EBUSY;
		goto out;
	}
	chain_type[ctype->family][ctype->type] = ctype;
out:
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return err;
}
EXPORT_SYMBOL_GPL(nft_register_chain_type);

void nft_unregister_chain_type(const struct nf_chain_type *ctype)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	chain_type[ctype->family][ctype->type] = NULL;
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_chain_type);

/*
 * Chains
 */

static struct nft_chain *
nf_tables_chain_lookup_byhandle(const struct nft_table *table, u64 handle,
				u8 genmask)
{
	struct nft_chain *chain;

	list_for_each_entry(chain, &table->chains, list) {
		if (chain->handle == handle &&
		    nft_active_genmask(chain, genmask))
			return chain;
	}

	return ERR_PTR(-ENOENT);
}

static struct nft_chain *nf_tables_chain_lookup(const struct nft_table *table,
						const struct nlattr *nla,
						u8 genmask)
{
	struct nft_chain *chain;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(chain, &table->chains, list) {
		if (!nla_strcmp(nla, chain->name) &&
		    nft_active_genmask(chain, genmask))
			return chain;
	}

	return ERR_PTR(-ENOENT);
}

static const struct nla_policy nft_chain_policy[NFTA_CHAIN_MAX + 1] = {
	[NFTA_CHAIN_TABLE]	= { .type = NLA_STRING,
				    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_CHAIN_HANDLE]	= { .type = NLA_U64 },
	[NFTA_CHAIN_NAME]	= { .type = NLA_STRING,
				    .len = NFT_CHAIN_MAXNAMELEN - 1 },
	[NFTA_CHAIN_HOOK]	= { .type = NLA_NESTED },
	[NFTA_CHAIN_POLICY]	= { .type = NLA_U32 },
	[NFTA_CHAIN_TYPE]	= { .type = NLA_STRING },
	[NFTA_CHAIN_COUNTERS]	= { .type = NLA_NESTED },
};

static const struct nla_policy nft_hook_policy[NFTA_HOOK_MAX + 1] = {
	[NFTA_HOOK_HOOKNUM]	= { .type = NLA_U32 },
	[NFTA_HOOK_PRIORITY]	= { .type = NLA_U32 },
	[NFTA_HOOK_DEV]		= { .type = NLA_STRING,
				    .len = IFNAMSIZ - 1 },
};

static int nft_dump_stats(struct sk_buff *skb, struct nft_stats __percpu *stats)
{
	struct nft_stats *cpu_stats, total;
	struct nlattr *nest;
	unsigned int seq;
	u64 pkts, bytes;
	int cpu;

	memset(&total, 0, sizeof(total));
	for_each_possible_cpu(cpu) {
		cpu_stats = per_cpu_ptr(stats, cpu);
		do {
			seq = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			pkts = cpu_stats->pkts;
			bytes = cpu_stats->bytes;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, seq));
		total.pkts += pkts;
		total.bytes += bytes;
	}
	nest = nla_nest_start(skb, NFTA_CHAIN_COUNTERS);
	if (nest == NULL)
		goto nla_put_failure;

	if (nla_put_be64(skb, NFTA_COUNTER_PACKETS, cpu_to_be64(total.pkts),
			 NFTA_COUNTER_PAD) ||
	    nla_put_be64(skb, NFTA_COUNTER_BYTES, cpu_to_be64(total.bytes),
			 NFTA_COUNTER_PAD))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static int nf_tables_fill_chain_info(struct sk_buff *skb, struct net *net,
				     u32 portid, u32 seq, int event, u32 flags,
				     int family, const struct nft_table *table,
				     const struct nft_chain *chain)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_CHAIN_TABLE, table->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_CHAIN_HANDLE, cpu_to_be64(chain->handle),
			 NFTA_CHAIN_PAD))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_CHAIN_NAME, chain->name))
		goto nla_put_failure;

	if (chain->flags & NFT_BASE_CHAIN) {
		const struct nft_base_chain *basechain = nft_base_chain(chain);
		const struct nf_hook_ops *ops = &basechain->ops[0];
		struct nlattr *nest;

		nest = nla_nest_start(skb, NFTA_CHAIN_HOOK);
		if (nest == NULL)
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_HOOK_HOOKNUM, htonl(ops->hooknum)))
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_HOOK_PRIORITY, htonl(ops->priority)))
			goto nla_put_failure;
		if (basechain->dev_name[0] &&
		    nla_put_string(skb, NFTA_HOOK_DEV, basechain->dev_name))
			goto nla_put_failure;
		nla_nest_end(skb, nest);

		if (nla_put_be32(skb, NFTA_CHAIN_POLICY,
				 htonl(basechain->policy)))
			goto nla_put_failure;

		if (nla_put_string(skb, NFTA_CHAIN_TYPE, basechain->type->name))
			goto nla_put_failure;

		if (nft_dump_stats(skb, nft_base_chain(chain)->stats))
			goto nla_put_failure;
	}

	if (nla_put_be32(skb, NFTA_CHAIN_USE, htonl(chain->use)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_chain_notify(const struct nft_ctx *ctx, int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_chain_info(skb, ctx->net, ctx->portid, ctx->seq,
					event, 0, ctx->afi->family, ctx->table,
					ctx->chain);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
			     ctx->report, GFP_KERNEL);
err:
	if (err < 0) {
		nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES,
				  err);
	}
	return err;
}

static int nf_tables_dump_chains(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry_rcu(table, &afi->tables, list) {
			list_for_each_entry_rcu(chain, &table->chains, list) {
				if (idx < s_idx)
					goto cont;
				if (idx > s_idx)
					memset(&cb->args[1], 0,
					       sizeof(cb->args) - sizeof(cb->args[0]));
				if (!nft_is_active(net, chain))
					continue;
				if (nf_tables_fill_chain_info(skb, net,
							      NETLINK_CB(cb->skb).portid,
							      cb->nlh->nlmsg_seq,
							      NFT_MSG_NEWCHAIN,
							      NLM_F_MULTI,
							      afi->family, table, chain) < 0)
					goto done;

				nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
				idx++;
			}
		}
	}
done:
	rcu_read_unlock();
	cb->args[0] = idx;
	return skb->len;
}

static int nf_tables_getchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	struct sk_buff *skb2;
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_chains,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME], genmask);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_chain_info(skb2, net, NETLINK_CB(skb).portid,
					nlh->nlmsg_seq, NFT_MSG_NEWCHAIN, 0,
					family, table, chain);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);

err:
	kfree_skb(skb2);
	return err;
}

static const struct nla_policy nft_counter_policy[NFTA_COUNTER_MAX + 1] = {
	[NFTA_COUNTER_PACKETS]	= { .type = NLA_U64 },
	[NFTA_COUNTER_BYTES]	= { .type = NLA_U64 },
};

static struct nft_stats __percpu *nft_stats_alloc(const struct nlattr *attr)
{
	struct nlattr *tb[NFTA_COUNTER_MAX+1];
	struct nft_stats __percpu *newstats;
	struct nft_stats *stats;
	int err;

	err = nla_parse_nested(tb, NFTA_COUNTER_MAX, attr, nft_counter_policy);
	if (err < 0)
		return ERR_PTR(err);

	if (!tb[NFTA_COUNTER_BYTES] || !tb[NFTA_COUNTER_PACKETS])
		return ERR_PTR(-EINVAL);

	newstats = netdev_alloc_pcpu_stats(struct nft_stats);
	if (newstats == NULL)
		return ERR_PTR(-ENOMEM);

	/* Restore old counters on this cpu, no problem. Per-cpu statistics
	 * are not exposed to userspace.
	 */
	preempt_disable();
	stats = this_cpu_ptr(newstats);
	stats->bytes = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_BYTES]));
	stats->pkts = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_PACKETS]));
	preempt_enable();

	return newstats;
}

static void nft_chain_stats_replace(struct nft_base_chain *chain,
				    struct nft_stats __percpu *newstats)
{
	if (newstats == NULL)
		return;

	if (chain->stats) {
		struct nft_stats __percpu *oldstats =
				nft_dereference(chain->stats);

		rcu_assign_pointer(chain->stats, newstats);
		synchronize_rcu();
		free_percpu(oldstats);
	} else
		rcu_assign_pointer(chain->stats, newstats);
}

static void nf_tables_chain_destroy(struct nft_chain *chain)
{
	BUG_ON(chain->use > 0);

	if (chain->flags & NFT_BASE_CHAIN) {
		struct nft_base_chain *basechain = nft_base_chain(chain);

		module_put(basechain->type->owner);
		free_percpu(basechain->stats);
		if (basechain->ops[0].dev != NULL)
			dev_put(basechain->ops[0].dev);
		kfree(basechain);
	} else {
		kfree(chain);
	}
}

struct nft_chain_hook {
	u32				num;
	u32				priority;
	const struct nf_chain_type	*type;
	struct net_device		*dev;
};

static int nft_chain_parse_hook(struct net *net,
				const struct nlattr * const nla[],
				struct nft_af_info *afi,
				struct nft_chain_hook *hook, bool create)
{
	struct nlattr *ha[NFTA_HOOK_MAX + 1];
	const struct nf_chain_type *type;
	struct net_device *dev;
	int err;

	err = nla_parse_nested(ha, NFTA_HOOK_MAX, nla[NFTA_CHAIN_HOOK],
			       nft_hook_policy);
	if (err < 0)
		return err;

	if (ha[NFTA_HOOK_HOOKNUM] == NULL ||
	    ha[NFTA_HOOK_PRIORITY] == NULL)
		return -EINVAL;

	hook->num = ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM]));
	if (hook->num >= afi->nhooks)
		return -EINVAL;

	hook->priority = ntohl(nla_get_be32(ha[NFTA_HOOK_PRIORITY]));

	type = chain_type[afi->family][NFT_CHAIN_T_DEFAULT];
	if (nla[NFTA_CHAIN_TYPE]) {
		type = nf_tables_chain_type_lookup(afi, nla[NFTA_CHAIN_TYPE],
						   create);
		if (IS_ERR(type))
			return PTR_ERR(type);
	}
	if (!(type->hook_mask & (1 << hook->num)))
		return -EOPNOTSUPP;
	if (!try_module_get(type->owner))
		return -ENOENT;

	hook->type = type;

	hook->dev = NULL;
	if (afi->flags & NFT_AF_NEEDS_DEV) {
		char ifname[IFNAMSIZ];

		if (!ha[NFTA_HOOK_DEV]) {
			module_put(type->owner);
			return -EOPNOTSUPP;
		}

		nla_strlcpy(ifname, ha[NFTA_HOOK_DEV], IFNAMSIZ);
		dev = dev_get_by_name(net, ifname);
		if (!dev) {
			module_put(type->owner);
			return -ENOENT;
		}
		hook->dev = dev;
	} else if (ha[NFTA_HOOK_DEV]) {
		module_put(type->owner);
		return -EOPNOTSUPP;
	}

	return 0;
}

static void nft_chain_release_hook(struct nft_chain_hook *hook)
{
	module_put(hook->type->owner);
	if (hook->dev != NULL)
		dev_put(hook->dev);
}

static int nf_tables_newchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr * uninitialized_var(name);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_base_chain *basechain = NULL;
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	u8 policy = NF_ACCEPT;
	u64 handle = 0;
	unsigned int i;
	struct nft_stats __percpu *stats;
	int err;
	bool create;
	struct nft_ctx ctx;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = NULL;
	name = nla[NFTA_CHAIN_NAME];

	if (nla[NFTA_CHAIN_HANDLE]) {
		handle = be64_to_cpu(nla_get_be64(nla[NFTA_CHAIN_HANDLE]));
		chain = nf_tables_chain_lookup_byhandle(table, handle, genmask);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
	} else {
		chain = nf_tables_chain_lookup(table, name, genmask);
		if (IS_ERR(chain)) {
			if (PTR_ERR(chain) != -ENOENT)
				return PTR_ERR(chain);
			chain = NULL;
		}
	}

	if (nla[NFTA_CHAIN_POLICY]) {
		if ((chain != NULL &&
		    !(chain->flags & NFT_BASE_CHAIN)))
			return -EOPNOTSUPP;

		if (chain == NULL &&
		    nla[NFTA_CHAIN_HOOK] == NULL)
			return -EOPNOTSUPP;

		policy = ntohl(nla_get_be32(nla[NFTA_CHAIN_POLICY]));
		switch (policy) {
		case NF_DROP:
		case NF_ACCEPT:
			break;
		default:
			return -EINVAL;
		}
	}

	if (chain != NULL) {
		struct nft_stats *stats = NULL;
		struct nft_trans *trans;

		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		if (nla[NFTA_CHAIN_HOOK]) {
			struct nft_base_chain *basechain;
			struct nft_chain_hook hook;
			struct nf_hook_ops *ops;

			if (!(chain->flags & NFT_BASE_CHAIN))
				return -EBUSY;

			err = nft_chain_parse_hook(net, nla, afi, &hook,
						   create);
			if (err < 0)
				return err;

			basechain = nft_base_chain(chain);
			if (basechain->type != hook.type) {
				nft_chain_release_hook(&hook);
				return -EBUSY;
			}

			for (i = 0; i < afi->nops; i++) {
				ops = &basechain->ops[i];
				if (ops->hooknum != hook.num ||
				    ops->priority != hook.priority ||
				    ops->dev != hook.dev) {
					nft_chain_release_hook(&hook);
					return -EBUSY;
				}
			}
			nft_chain_release_hook(&hook);
		}

		if (nla[NFTA_CHAIN_HANDLE] && name) {
			struct nft_chain *chain2;

			chain2 = nf_tables_chain_lookup(table,
							nla[NFTA_CHAIN_NAME],
							genmask);
			if (IS_ERR(chain2))
				return PTR_ERR(chain2);
		}

		if (nla[NFTA_CHAIN_COUNTERS]) {
			if (!(chain->flags & NFT_BASE_CHAIN))
				return -EOPNOTSUPP;

			stats = nft_stats_alloc(nla[NFTA_CHAIN_COUNTERS]);
			if (IS_ERR(stats))
				return PTR_ERR(stats);
		}

		nft_ctx_init(&ctx, net, skb, nlh, afi, table, chain, nla);
		trans = nft_trans_alloc(&ctx, NFT_MSG_NEWCHAIN,
					sizeof(struct nft_trans_chain));
		if (trans == NULL) {
			free_percpu(stats);
			return -ENOMEM;
		}

		nft_trans_chain_stats(trans) = stats;
		nft_trans_chain_update(trans) = true;

		if (nla[NFTA_CHAIN_POLICY])
			nft_trans_chain_policy(trans) = policy;
		else
			nft_trans_chain_policy(trans) = -1;

		if (nla[NFTA_CHAIN_HANDLE] && name) {
			nla_strlcpy(nft_trans_chain_name(trans), name,
				    NFT_CHAIN_MAXNAMELEN);
		}
		list_add_tail(&trans->list, &net->nft.commit_list);
		return 0;
	}

	if (table->use == UINT_MAX)
		return -EOVERFLOW;

	if (nla[NFTA_CHAIN_HOOK]) {
		struct nft_chain_hook hook;
		struct nf_hook_ops *ops;
		nf_hookfn *hookfn;

		err = nft_chain_parse_hook(net, nla, afi, &hook, create);
		if (err < 0)
			return err;

		basechain = kzalloc(sizeof(*basechain), GFP_KERNEL);
		if (basechain == NULL) {
			nft_chain_release_hook(&hook);
			return -ENOMEM;
		}

		if (hook.dev != NULL)
			strncpy(basechain->dev_name, hook.dev->name, IFNAMSIZ);

		if (nla[NFTA_CHAIN_COUNTERS]) {
			stats = nft_stats_alloc(nla[NFTA_CHAIN_COUNTERS]);
			if (IS_ERR(stats)) {
				nft_chain_release_hook(&hook);
				kfree(basechain);
				return PTR_ERR(stats);
			}
			basechain->stats = stats;
		} else {
			stats = netdev_alloc_pcpu_stats(struct nft_stats);
			if (stats == NULL) {
				nft_chain_release_hook(&hook);
				kfree(basechain);
				return -ENOMEM;
			}
			rcu_assign_pointer(basechain->stats, stats);
		}

		hookfn = hook.type->hooks[hook.num];
		basechain->type = hook.type;
		chain = &basechain->chain;

		for (i = 0; i < afi->nops; i++) {
			ops = &basechain->ops[i];
			ops->pf		= family;
			ops->hooknum	= hook.num;
			ops->priority	= hook.priority;
			ops->priv	= chain;
			ops->hook	= afi->hooks[ops->hooknum];
			ops->dev	= hook.dev;
			if (hookfn)
				ops->hook = hookfn;
			if (afi->hook_ops_init)
				afi->hook_ops_init(ops, i);
		}

		chain->flags |= NFT_BASE_CHAIN;
		basechain->policy = policy;
	} else {
		chain = kzalloc(sizeof(*chain), GFP_KERNEL);
		if (chain == NULL)
			return -ENOMEM;
	}

	INIT_LIST_HEAD(&chain->rules);
	chain->handle = nf_tables_alloc_handle(table);
	chain->table = table;
	nla_strlcpy(chain->name, name, NFT_CHAIN_MAXNAMELEN);

	err = nf_tables_register_hooks(net, table, chain, afi->nops);
	if (err < 0)
		goto err1;

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, chain, nla);
	err = nft_trans_chain_add(&ctx, NFT_MSG_NEWCHAIN);
	if (err < 0)
		goto err2;

	table->use++;
	list_add_tail_rcu(&chain->list, &table->chains);
	return 0;
err2:
	nf_tables_unregister_hooks(net, table, chain, afi->nops);
err1:
	nf_tables_chain_destroy(chain);
	return err;
}

static int nf_tables_delchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	int family = nfmsg->nfgen_family;
	struct nft_ctx ctx;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME], genmask);
	if (IS_ERR(chain))
		return PTR_ERR(chain);
	if (chain->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, chain, nla);

	return nft_delchain(&ctx);
}

/*
 * Expressions
 */

/**
 *	nft_register_expr - register nf_tables expr type
 *	@ops: expr type
 *
 *	Registers the expr type for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_expr(struct nft_expr_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	if (type->family == NFPROTO_UNSPEC)
		list_add_tail_rcu(&type->list, &nf_tables_expressions);
	else
		list_add_rcu(&type->list, &nf_tables_expressions);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_expr);

/**
 *	nft_unregister_expr - unregister nf_tables expr type
 *	@ops: expr type
 *
 * 	Unregisters the expr typefor use with nf_tables.
 */
void nft_unregister_expr(struct nft_expr_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del_rcu(&type->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_expr);

static const struct nft_expr_type *__nft_expr_type_get(u8 family,
						       struct nlattr *nla)
{
	const struct nft_expr_type *type;

	list_for_each_entry(type, &nf_tables_expressions, list) {
		if (!nla_strcmp(nla, type->name) &&
		    (!type->family || type->family == family))
			return type;
	}
	return NULL;
}

static const struct nft_expr_type *nft_expr_type_get(u8 family,
						     struct nlattr *nla)
{
	const struct nft_expr_type *type;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	type = __nft_expr_type_get(family, nla);
	if (type != NULL && try_module_get(type->owner))
		return type;

#ifdef CONFIG_MODULES
	if (type == NULL) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-expr-%u-%.*s", family,
			       nla_len(nla), (char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_expr_type_get(family, nla))
			return ERR_PTR(-EAGAIN);

		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-expr-%.*s",
			       nla_len(nla), (char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_expr_type_get(family, nla))
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-ENOENT);
}

static const struct nla_policy nft_expr_policy[NFTA_EXPR_MAX + 1] = {
	[NFTA_EXPR_NAME]	= { .type = NLA_STRING },
	[NFTA_EXPR_DATA]	= { .type = NLA_NESTED },
};

static int nf_tables_fill_expr_info(struct sk_buff *skb,
				    const struct nft_expr *expr)
{
	if (nla_put_string(skb, NFTA_EXPR_NAME, expr->ops->type->name))
		goto nla_put_failure;

	if (expr->ops->dump) {
		struct nlattr *data = nla_nest_start(skb, NFTA_EXPR_DATA);
		if (data == NULL)
			goto nla_put_failure;
		if (expr->ops->dump(skb, expr) < 0)
			goto nla_put_failure;
		nla_nest_end(skb, data);
	}

	return skb->len;

nla_put_failure:
	return -1;
};

int nft_expr_dump(struct sk_buff *skb, unsigned int attr,
		  const struct nft_expr *expr)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, attr);
	if (!nest)
		goto nla_put_failure;
	if (nf_tables_fill_expr_info(skb, expr) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

struct nft_expr_info {
	const struct nft_expr_ops	*ops;
	struct nlattr			*tb[NFT_EXPR_MAXATTR + 1];
};

static int nf_tables_expr_parse(const struct nft_ctx *ctx,
				const struct nlattr *nla,
				struct nft_expr_info *info)
{
	const struct nft_expr_type *type;
	const struct nft_expr_ops *ops;
	struct nlattr *tb[NFTA_EXPR_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_EXPR_MAX, nla, nft_expr_policy);
	if (err < 0)
		return err;

	type = nft_expr_type_get(ctx->afi->family, tb[NFTA_EXPR_NAME]);
	if (IS_ERR(type))
		return PTR_ERR(type);

	if (tb[NFTA_EXPR_DATA]) {
		err = nla_parse_nested(info->tb, type->maxattr,
				       tb[NFTA_EXPR_DATA], type->policy);
		if (err < 0)
			goto err1;
	} else
		memset(info->tb, 0, sizeof(info->tb[0]) * (type->maxattr + 1));

	if (type->select_ops != NULL) {
		ops = type->select_ops(ctx,
				       (const struct nlattr * const *)info->tb);
		if (IS_ERR(ops)) {
			err = PTR_ERR(ops);
			goto err1;
		}
	} else
		ops = type->ops;

	info->ops = ops;
	return 0;

err1:
	module_put(type->owner);
	return err;
}

static int nf_tables_newexpr(const struct nft_ctx *ctx,
			     const struct nft_expr_info *info,
			     struct nft_expr *expr)
{
	const struct nft_expr_ops *ops = info->ops;
	int err;

	expr->ops = ops;
	if (ops->init) {
		err = ops->init(ctx, expr, (const struct nlattr **)info->tb);
		if (err < 0)
			goto err1;
	}

	return 0;

err1:
	expr->ops = NULL;
	return err;
}

static void nf_tables_expr_destroy(const struct nft_ctx *ctx,
				   struct nft_expr *expr)
{
	if (expr->ops->destroy)
		expr->ops->destroy(ctx, expr);
	module_put(expr->ops->type->owner);
}

struct nft_expr *nft_expr_init(const struct nft_ctx *ctx,
			       const struct nlattr *nla)
{
	struct nft_expr_info info;
	struct nft_expr *expr;
	int err;

	err = nf_tables_expr_parse(ctx, nla, &info);
	if (err < 0)
		goto err1;

	err = -ENOMEM;
	expr = kzalloc(info.ops->size, GFP_KERNEL);
	if (expr == NULL)
		goto err2;

	err = nf_tables_newexpr(ctx, &info, expr);
	if (err < 0)
		goto err3;

	return expr;
err3:
	kfree(expr);
err2:
	module_put(info.ops->type->owner);
err1:
	return ERR_PTR(err);
}

void nft_expr_destroy(const struct nft_ctx *ctx, struct nft_expr *expr)
{
	nf_tables_expr_destroy(ctx, expr);
	kfree(expr);
}

/*
 * Rules
 */

static struct nft_rule *__nf_tables_rule_lookup(const struct nft_chain *chain,
						u64 handle)
{
	struct nft_rule *rule;

	// FIXME: this sucks
	list_for_each_entry(rule, &chain->rules, list) {
		if (handle == rule->handle)
			return rule;
	}

	return ERR_PTR(-ENOENT);
}

static struct nft_rule *nf_tables_rule_lookup(const struct nft_chain *chain,
					      const struct nlattr *nla)
{
	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	return __nf_tables_rule_lookup(chain, be64_to_cpu(nla_get_be64(nla)));
}

static const struct nla_policy nft_rule_policy[NFTA_RULE_MAX + 1] = {
	[NFTA_RULE_TABLE]	= { .type = NLA_STRING,
				    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_RULE_CHAIN]	= { .type = NLA_STRING,
				    .len = NFT_CHAIN_MAXNAMELEN - 1 },
	[NFTA_RULE_HANDLE]	= { .type = NLA_U64 },
	[NFTA_RULE_EXPRESSIONS]	= { .type = NLA_NESTED },
	[NFTA_RULE_COMPAT]	= { .type = NLA_NESTED },
	[NFTA_RULE_POSITION]	= { .type = NLA_U64 },
	[NFTA_RULE_USERDATA]	= { .type = NLA_BINARY,
				    .len = NFT_USERDATA_MAXLEN },
};

static int nf_tables_fill_rule_info(struct sk_buff *skb, struct net *net,
				    u32 portid, u32 seq, int event,
				    u32 flags, int family,
				    const struct nft_table *table,
				    const struct nft_chain *chain,
				    const struct nft_rule *rule)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	const struct nft_expr *expr, *next;
	struct nlattr *list;
	const struct nft_rule *prule;
	int type = event | NFNL_SUBSYS_NFTABLES << 8;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(struct nfgenmsg),
			flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_RULE_TABLE, table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_RULE_CHAIN, chain->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_RULE_HANDLE, cpu_to_be64(rule->handle),
			 NFTA_RULE_PAD))
		goto nla_put_failure;

	if ((event != NFT_MSG_DELRULE) && (rule->list.prev != &chain->rules)) {
		prule = list_entry(rule->list.prev, struct nft_rule, list);
		if (nla_put_be64(skb, NFTA_RULE_POSITION,
				 cpu_to_be64(prule->handle),
				 NFTA_RULE_PAD))
			goto nla_put_failure;
	}

	list = nla_nest_start(skb, NFTA_RULE_EXPRESSIONS);
	if (list == NULL)
		goto nla_put_failure;
	nft_rule_for_each_expr(expr, next, rule) {
		if (nft_expr_dump(skb, NFTA_LIST_ELEM, expr) < 0)
			goto nla_put_failure;
	}
	nla_nest_end(skb, list);

	if (rule->udata) {
		struct nft_userdata *udata = nft_userdata(rule);
		if (nla_put(skb, NFTA_RULE_USERDATA, udata->len + 1,
			    udata->data) < 0)
			goto nla_put_failure;
	}

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_rule_notify(const struct nft_ctx *ctx,
				 const struct nft_rule *rule,
				 int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_rule_info(skb, ctx->net, ctx->portid, ctx->seq,
				       event, 0, ctx->afi->family, ctx->table,
				       ctx->chain, rule);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
			     ctx->report, GFP_KERNEL);
err:
	if (err < 0) {
		nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES,
				  err);
	}
	return err;
}

struct nft_rule_dump_ctx {
	char table[NFT_TABLE_MAXNAMELEN];
	char chain[NFT_CHAIN_MAXNAMELEN];
};

static int nf_tables_dump_rules(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_rule_dump_ctx *ctx = cb->data;
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	const struct nft_rule *rule;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry_rcu(table, &afi->tables, list) {
			if (ctx && ctx->table[0] &&
			    strcmp(ctx->table, table->name) != 0)
				continue;

			list_for_each_entry_rcu(chain, &table->chains, list) {
				if (ctx && ctx->chain[0] &&
				    strcmp(ctx->chain, chain->name) != 0)
					continue;

				list_for_each_entry_rcu(rule, &chain->rules, list) {
					if (!nft_is_active(net, rule))
						goto cont;
					if (idx < s_idx)
						goto cont;
					if (idx > s_idx)
						memset(&cb->args[1], 0,
						       sizeof(cb->args) - sizeof(cb->args[0]));
					if (nf_tables_fill_rule_info(skb, net, NETLINK_CB(cb->skb).portid,
								      cb->nlh->nlmsg_seq,
								      NFT_MSG_NEWRULE,
								      NLM_F_MULTI | NLM_F_APPEND,
								      afi->family, table, chain, rule) < 0)
						goto done;

					nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
					idx++;
				}
			}
		}
	}
done:
	rcu_read_unlock();

	cb->args[0] = idx;
	return skb->len;
}

static int nf_tables_dump_rules_done(struct netlink_callback *cb)
{
	kfree(cb->data);
	return 0;
}

static int nf_tables_getrule(struct net *net, struct sock *nlsk,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	const struct nft_rule *rule;
	struct sk_buff *skb2;
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_rules,
			.done = nf_tables_dump_rules_done,
		};

		if (nla[NFTA_RULE_TABLE] || nla[NFTA_RULE_CHAIN]) {
			struct nft_rule_dump_ctx *ctx;

			ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
			if (!ctx)
				return -ENOMEM;

			if (nla[NFTA_RULE_TABLE])
				nla_strlcpy(ctx->table, nla[NFTA_RULE_TABLE],
					    sizeof(ctx->table));
			if (nla[NFTA_RULE_CHAIN])
				nla_strlcpy(ctx->chain, nla[NFTA_RULE_CHAIN],
					    sizeof(ctx->chain));
			c.data = ctx;
		}

		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN], genmask);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	rule = nf_tables_rule_lookup(chain, nla[NFTA_RULE_HANDLE]);
	if (IS_ERR(rule))
		return PTR_ERR(rule);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_rule_info(skb2, net, NETLINK_CB(skb).portid,
				       nlh->nlmsg_seq, NFT_MSG_NEWRULE, 0,
				       family, table, chain, rule);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);

err:
	kfree_skb(skb2);
	return err;
}

static void nf_tables_rule_destroy(const struct nft_ctx *ctx,
				   struct nft_rule *rule)
{
	struct nft_expr *expr;

	/*
	 * Careful: some expressions might not be initialized in case this
	 * is called on error from nf_tables_newrule().
	 */
	expr = nft_expr_first(rule);
	while (expr != nft_expr_last(rule) && expr->ops) {
		nf_tables_expr_destroy(ctx, expr);
		expr = nft_expr_next(expr);
	}
	kfree(rule);
}

#define NFT_RULE_MAXEXPRS	128

static struct nft_expr_info *info;

static int nf_tables_newrule(struct net *net, struct sock *nlsk,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule, *old_rule = NULL;
	struct nft_userdata *udata;
	struct nft_trans *trans = NULL;
	struct nft_expr *expr;
	struct nft_ctx ctx;
	struct nlattr *tmp;
	unsigned int size, i, n, ulen = 0, usize = 0;
	int err, rem;
	bool create;
	u64 handle, pos_handle;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, create);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN], genmask);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	if (nla[NFTA_RULE_HANDLE]) {
		handle = be64_to_cpu(nla_get_be64(nla[NFTA_RULE_HANDLE]));
		rule = __nf_tables_rule_lookup(chain, handle);
		if (IS_ERR(rule))
			return PTR_ERR(rule);

		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			old_rule = rule;
		else
			return -EOPNOTSUPP;
	} else {
		if (!create || nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EINVAL;
		handle = nf_tables_alloc_handle(table);

		if (chain->use == UINT_MAX)
			return -EOVERFLOW;
	}

	if (nla[NFTA_RULE_POSITION]) {
		if (!(nlh->nlmsg_flags & NLM_F_CREATE))
			return -EOPNOTSUPP;

		pos_handle = be64_to_cpu(nla_get_be64(nla[NFTA_RULE_POSITION]));
		old_rule = __nf_tables_rule_lookup(chain, pos_handle);
		if (IS_ERR(old_rule))
			return PTR_ERR(old_rule);
	}

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, chain, nla);

	n = 0;
	size = 0;
	if (nla[NFTA_RULE_EXPRESSIONS]) {
		nla_for_each_nested(tmp, nla[NFTA_RULE_EXPRESSIONS], rem) {
			err = -EINVAL;
			if (nla_type(tmp) != NFTA_LIST_ELEM)
				goto err1;
			if (n == NFT_RULE_MAXEXPRS)
				goto err1;
			err = nf_tables_expr_parse(&ctx, tmp, &info[n]);
			if (err < 0)
				goto err1;
			size += info[n].ops->size;
			n++;
		}
	}
	/* Check for overflow of dlen field */
	err = -EFBIG;
	if (size >= 1 << 12)
		goto err1;

	if (nla[NFTA_RULE_USERDATA]) {
		ulen = nla_len(nla[NFTA_RULE_USERDATA]);
		if (ulen > 0)
			usize = sizeof(struct nft_userdata) + ulen;
	}

	err = -ENOMEM;
	rule = kzalloc(sizeof(*rule) + size + usize, GFP_KERNEL);
	if (rule == NULL)
		goto err1;

	nft_activate_next(net, rule);

	rule->handle = handle;
	rule->dlen   = size;
	rule->udata  = ulen ? 1 : 0;

	if (ulen) {
		udata = nft_userdata(rule);
		udata->len = ulen - 1;
		nla_memcpy(udata->data, nla[NFTA_RULE_USERDATA], ulen);
	}

	expr = nft_expr_first(rule);
	for (i = 0; i < n; i++) {
		err = nf_tables_newexpr(&ctx, &info[i], expr);
		if (err < 0)
			goto err2;
		info[i].ops = NULL;
		expr = nft_expr_next(expr);
	}

	if (nlh->nlmsg_flags & NLM_F_REPLACE) {
		if (nft_is_active_next(net, old_rule)) {
			trans = nft_trans_rule_add(&ctx, NFT_MSG_DELRULE,
						   old_rule);
			if (trans == NULL) {
				err = -ENOMEM;
				goto err2;
			}
			nft_deactivate_next(net, old_rule);
			chain->use--;
			list_add_tail_rcu(&rule->list, &old_rule->list);
		} else {
			err = -ENOENT;
			goto err2;
		}
	} else if (nlh->nlmsg_flags & NLM_F_APPEND)
		if (old_rule)
			list_add_rcu(&rule->list, &old_rule->list);
		else
			list_add_tail_rcu(&rule->list, &chain->rules);
	else {
		if (old_rule)
			list_add_tail_rcu(&rule->list, &old_rule->list);
		else
			list_add_rcu(&rule->list, &chain->rules);
	}

	if (nft_trans_rule_add(&ctx, NFT_MSG_NEWRULE, rule) == NULL) {
		err = -ENOMEM;
		goto err3;
	}
	chain->use++;
	return 0;

err3:
	list_del_rcu(&rule->list);
err2:
	nf_tables_rule_destroy(&ctx, rule);
err1:
	for (i = 0; i < n; i++) {
		if (info[i].ops != NULL)
			module_put(info[i].ops->type->owner);
	}
	return err;
}

static int nf_tables_delrule(struct net *net, struct sock *nlsk,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain = NULL;
	struct nft_rule *rule;
	int family = nfmsg->nfgen_family, err = 0;
	struct nft_ctx ctx;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (nla[NFTA_RULE_CHAIN]) {
		chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN],
					       genmask);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
	}

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, chain, nla);

	if (chain) {
		if (nla[NFTA_RULE_HANDLE]) {
			rule = nf_tables_rule_lookup(chain,
						     nla[NFTA_RULE_HANDLE]);
			if (IS_ERR(rule))
				return PTR_ERR(rule);

			err = nft_delrule(&ctx, rule);
		} else {
			err = nft_delrule_by_chain(&ctx);
		}
	} else {
		list_for_each_entry(chain, &table->chains, list) {
			if (!nft_is_active_next(net, chain))
				continue;

			ctx.chain = chain;
			err = nft_delrule_by_chain(&ctx);
			if (err < 0)
				break;
		}
	}

	return err;
}

/*
 * Sets
 */

static LIST_HEAD(nf_tables_set_ops);

int nft_register_set(struct nft_set_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail_rcu(&ops->list, &nf_tables_set_ops);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_set);

void nft_unregister_set(struct nft_set_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del_rcu(&ops->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_set);

/*
 * Select a set implementation based on the data characteristics and the
 * given policy. The total memory use might not be known if no size is
 * given, in that case the amount of memory per element is used.
 */
static const struct nft_set_ops *
nft_select_set_ops(const struct nlattr * const nla[],
		   const struct nft_set_desc *desc,
		   enum nft_set_policies policy)
{
	const struct nft_set_ops *ops, *bops;
	struct nft_set_estimate est, best;
	u32 features;

#ifdef CONFIG_MODULES
	if (list_empty(&nf_tables_set_ops)) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-set");
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (!list_empty(&nf_tables_set_ops))
			return ERR_PTR(-EAGAIN);
	}
#endif
	features = 0;
	if (nla[NFTA_SET_FLAGS] != NULL) {
		features = ntohl(nla_get_be32(nla[NFTA_SET_FLAGS]));
		features &= NFT_SET_INTERVAL | NFT_SET_MAP | NFT_SET_TIMEOUT;
	}

	bops	   = NULL;
	best.size  = ~0;
	best.class = ~0;

	list_for_each_entry(ops, &nf_tables_set_ops, list) {
		if ((ops->features & features) != features)
			continue;
		if (!ops->estimate(desc, features, &est))
			continue;

		switch (policy) {
		case NFT_SET_POL_PERFORMANCE:
			if (est.class < best.class)
				break;
			if (est.class == best.class && est.size < best.size)
				break;
			continue;
		case NFT_SET_POL_MEMORY:
			if (est.size < best.size)
				break;
			if (est.size == best.size && est.class < best.class)
				break;
			continue;
		default:
			break;
		}

		if (!try_module_get(ops->owner))
			continue;
		if (bops != NULL)
			module_put(bops->owner);

		bops = ops;
		best = est;
	}

	if (bops != NULL)
		return bops;

	return ERR_PTR(-EOPNOTSUPP);
}

static const struct nla_policy nft_set_policy[NFTA_SET_MAX + 1] = {
	[NFTA_SET_TABLE]		= { .type = NLA_STRING,
					    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_SET_NAME]			= { .type = NLA_STRING,
					    .len = NFT_SET_MAXNAMELEN - 1 },
	[NFTA_SET_FLAGS]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_LEN]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_LEN]		= { .type = NLA_U32 },
	[NFTA_SET_POLICY]		= { .type = NLA_U32 },
	[NFTA_SET_DESC]			= { .type = NLA_NESTED },
	[NFTA_SET_ID]			= { .type = NLA_U32 },
	[NFTA_SET_TIMEOUT]		= { .type = NLA_U64 },
	[NFTA_SET_GC_INTERVAL]		= { .type = NLA_U32 },
	[NFTA_SET_USERDATA]		= { .type = NLA_BINARY,
					    .len  = NFT_USERDATA_MAXLEN },
	[NFTA_SET_OBJ_TYPE]		= { .type = NLA_U32 },
};

static const struct nla_policy nft_set_desc_policy[NFTA_SET_DESC_MAX + 1] = {
	[NFTA_SET_DESC_SIZE]		= { .type = NLA_U32 },
};

static int nft_ctx_init_from_setattr(struct nft_ctx *ctx, struct net *net,
				     const struct sk_buff *skb,
				     const struct nlmsghdr *nlh,
				     const struct nlattr * const nla[],
				     u8 genmask)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi = NULL;
	struct nft_table *table = NULL;

	if (nfmsg->nfgen_family != NFPROTO_UNSPEC) {
		afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, false);
		if (IS_ERR(afi))
			return PTR_ERR(afi);
	}

	if (nla[NFTA_SET_TABLE] != NULL) {
		if (afi == NULL)
			return -EAFNOSUPPORT;

		table = nf_tables_table_lookup(afi, nla[NFTA_SET_TABLE],
					       genmask);
		if (IS_ERR(table))
			return PTR_ERR(table);
	}

	nft_ctx_init(ctx, net, skb, nlh, afi, table, NULL, nla);
	return 0;
}

struct nft_set *nf_tables_set_lookup(const struct nft_table *table,
				     const struct nlattr *nla, u8 genmask)
{
	struct nft_set *set;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(set, &table->sets, list) {
		if (!nla_strcmp(nla, set->name) &&
		    nft_active_genmask(set, genmask))
			return set;
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(nf_tables_set_lookup);

struct nft_set *nf_tables_set_lookup_byid(const struct net *net,
					  const struct nlattr *nla,
					  u8 genmask)
{
	struct nft_trans *trans;
	u32 id = ntohl(nla_get_be32(nla));

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		struct nft_set *set = nft_trans_set(trans);

		if (trans->msg_type == NFT_MSG_NEWSET &&
		    id == nft_trans_set_id(trans) &&
		    nft_active_genmask(set, genmask))
			return set;
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(nf_tables_set_lookup_byid);

static int nf_tables_set_alloc_name(struct nft_ctx *ctx, struct nft_set *set,
				    const char *name)
{
	const struct nft_set *i;
	const char *p;
	unsigned long *inuse;
	unsigned int n = 0, min = 0;

	p = strnchr(name, NFT_SET_MAXNAMELEN, '%');
	if (p != NULL) {
		if (p[1] != 'd' || strchr(p + 2, '%'))
			return -EINVAL;

		inuse = (unsigned long *)get_zeroed_page(GFP_KERNEL);
		if (inuse == NULL)
			return -ENOMEM;
cont:
		list_for_each_entry(i, &ctx->table->sets, list) {
			int tmp;

			if (!nft_is_active_next(ctx->net, set))
				continue;
			if (!sscanf(i->name, name, &tmp))
				continue;
			if (tmp < min || tmp >= min + BITS_PER_BYTE * PAGE_SIZE)
				continue;

			set_bit(tmp - min, inuse);
		}

		n = find_first_zero_bit(inuse, BITS_PER_BYTE * PAGE_SIZE);
		if (n >= BITS_PER_BYTE * PAGE_SIZE) {
			min += BITS_PER_BYTE * PAGE_SIZE;
			memset(inuse, 0, PAGE_SIZE);
			goto cont;
		}
		free_page((unsigned long)inuse);
	}

	snprintf(set->name, sizeof(set->name), name, min + n);
	list_for_each_entry(i, &ctx->table->sets, list) {
		if (!nft_is_active_next(ctx->net, i))
			continue;
		if (!strcmp(set->name, i->name))
			return -ENFILE;
	}
	return 0;
}

static int nf_tables_fill_set(struct sk_buff *skb, const struct nft_ctx *ctx,
			      const struct nft_set *set, u16 event, u16 flags)
{
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	struct nlattr *desc;
	u32 portid = ctx->portid;
	u32 seq = ctx->seq;

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= ctx->afi->family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(ctx->net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_SET_TABLE, ctx->table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_NAME, set->name))
		goto nla_put_failure;
	if (set->flags != 0)
		if (nla_put_be32(skb, NFTA_SET_FLAGS, htonl(set->flags)))
			goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_SET_KEY_TYPE, htonl(set->ktype)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_SET_KEY_LEN, htonl(set->klen)))
		goto nla_put_failure;
	if (set->flags & NFT_SET_MAP) {
		if (nla_put_be32(skb, NFTA_SET_DATA_TYPE, htonl(set->dtype)))
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_SET_DATA_LEN, htonl(set->dlen)))
			goto nla_put_failure;
	}
	if (set->flags & NFT_SET_OBJECT &&
	    nla_put_be32(skb, NFTA_SET_OBJ_TYPE, htonl(set->objtype)))
		goto nla_put_failure;

	if (set->timeout &&
	    nla_put_be64(skb, NFTA_SET_TIMEOUT,
			 cpu_to_be64(jiffies_to_msecs(set->timeout)),
			 NFTA_SET_PAD))
		goto nla_put_failure;
	if (set->gc_int &&
	    nla_put_be32(skb, NFTA_SET_GC_INTERVAL, htonl(set->gc_int)))
		goto nla_put_failure;

	if (set->policy != NFT_SET_POL_PERFORMANCE) {
		if (nla_put_be32(skb, NFTA_SET_POLICY, htonl(set->policy)))
			goto nla_put_failure;
	}

	if (nla_put(skb, NFTA_SET_USERDATA, set->udlen, set->udata))
		goto nla_put_failure;

	desc = nla_nest_start(skb, NFTA_SET_DESC);
	if (desc == NULL)
		goto nla_put_failure;
	if (set->size &&
	    nla_put_be32(skb, NFTA_SET_DESC_SIZE, htonl(set->size)))
		goto nla_put_failure;
	nla_nest_end(skb, desc);

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_set_notify(const struct nft_ctx *ctx,
				const struct nft_set *set,
				int event, gfp_t gfp_flags)
{
	struct sk_buff *skb;
	u32 portid = ctx->portid;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, gfp_flags);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_set(skb, ctx, set, event, 0);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, ctx->net, portid, NFNLGRP_NFTABLES,
			     ctx->report, gfp_flags);
err:
	if (err < 0)
		nfnetlink_set_err(ctx->net, portid, NFNLGRP_NFTABLES, err);
	return err;
}

static int nf_tables_dump_sets(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx, s_idx = cb->args[0];
	struct nft_af_info *afi;
	struct nft_table *table, *cur_table = (struct nft_table *)cb->args[2];
	struct net *net = sock_net(skb->sk);
	int cur_family = cb->args[3];
	struct nft_ctx *ctx = cb->data, ctx_set;

	if (cb->args[1])
		return skb->len;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (ctx->afi && ctx->afi != afi)
			continue;

		if (cur_family) {
			if (afi->family != cur_family)
				continue;

			cur_family = 0;
		}
		list_for_each_entry_rcu(table, &afi->tables, list) {
			if (ctx->table && ctx->table != table)
				continue;

			if (cur_table) {
				if (cur_table != table)
					continue;

				cur_table = NULL;
			}
			idx = 0;
			list_for_each_entry_rcu(set, &table->sets, list) {
				if (idx < s_idx)
					goto cont;
				if (!nft_is_active(net, set))
					goto cont;

				ctx_set = *ctx;
				ctx_set.table = table;
				ctx_set.afi = afi;
				if (nf_tables_fill_set(skb, &ctx_set, set,
						       NFT_MSG_NEWSET,
						       NLM_F_MULTI) < 0) {
					cb->args[0] = idx;
					cb->args[2] = (unsigned long) table;
					cb->args[3] = afi->family;
					goto done;
				}
				nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
				idx++;
			}
			if (s_idx)
				s_idx = 0;
		}
	}
	cb->args[1] = 1;
done:
	rcu_read_unlock();
	return skb->len;
}

static int nf_tables_dump_sets_done(struct netlink_callback *cb)
{
	kfree(cb->data);
	return 0;
}

static int nf_tables_getset(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	u8 genmask = nft_genmask_cur(net);
	const struct nft_set *set;
	struct nft_ctx ctx;
	struct sk_buff *skb2;
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	int err;

	/* Verify existence before starting dump */
	err = nft_ctx_init_from_setattr(&ctx, net, skb, nlh, nla, genmask);
	if (err < 0)
		return err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_sets,
			.done = nf_tables_dump_sets_done,
		};
		struct nft_ctx *ctx_dump;

		ctx_dump = kmalloc(sizeof(*ctx_dump), GFP_KERNEL);
		if (ctx_dump == NULL)
			return -ENOMEM;

		*ctx_dump = ctx;
		c.data = ctx_dump;

		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	/* Only accept unspec with dump */
	if (nfmsg->nfgen_family == NFPROTO_UNSPEC)
		return -EAFNOSUPPORT;
	if (!nla[NFTA_SET_TABLE])
		return -EINVAL;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME], genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	err = nf_tables_fill_set(skb2, &ctx, set, NFT_MSG_NEWSET, 0);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);

err:
	kfree_skb(skb2);
	return err;
}

static int nf_tables_set_desc_parse(const struct nft_ctx *ctx,
				    struct nft_set_desc *desc,
				    const struct nlattr *nla)
{
	struct nlattr *da[NFTA_SET_DESC_MAX + 1];
	int err;

	err = nla_parse_nested(da, NFTA_SET_DESC_MAX, nla, nft_set_desc_policy);
	if (err < 0)
		return err;

	if (da[NFTA_SET_DESC_SIZE] != NULL)
		desc->size = ntohl(nla_get_be32(da[NFTA_SET_DESC_SIZE]));

	return 0;
}

static int nf_tables_newset(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	const struct nft_set_ops *ops;
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_set *set;
	struct nft_ctx ctx;
	char name[NFT_SET_MAXNAMELEN];
	unsigned int size;
	bool create;
	u64 timeout;
	u32 ktype, dtype, flags, policy, gc_int, objtype;
	struct nft_set_desc desc;
	unsigned char *udata;
	u16 udlen;
	int err;

	if (nla[NFTA_SET_TABLE] == NULL ||
	    nla[NFTA_SET_NAME] == NULL ||
	    nla[NFTA_SET_KEY_LEN] == NULL ||
	    nla[NFTA_SET_ID] == NULL)
		return -EINVAL;

	memset(&desc, 0, sizeof(desc));

	ktype = NFT_DATA_VALUE;
	if (nla[NFTA_SET_KEY_TYPE] != NULL) {
		ktype = ntohl(nla_get_be32(nla[NFTA_SET_KEY_TYPE]));
		if ((ktype & NFT_DATA_RESERVED_MASK) == NFT_DATA_RESERVED_MASK)
			return -EINVAL;
	}

	desc.klen = ntohl(nla_get_be32(nla[NFTA_SET_KEY_LEN]));
	if (desc.klen == 0 || desc.klen > NFT_DATA_VALUE_MAXLEN)
		return -EINVAL;

	flags = 0;
	if (nla[NFTA_SET_FLAGS] != NULL) {
		flags = ntohl(nla_get_be32(nla[NFTA_SET_FLAGS]));
		if (flags & ~(NFT_SET_ANONYMOUS | NFT_SET_CONSTANT |
			      NFT_SET_INTERVAL | NFT_SET_TIMEOUT |
			      NFT_SET_MAP | NFT_SET_EVAL |
			      NFT_SET_OBJECT))
			return -EINVAL;
		/* Only one of these operations is supported */
		if ((flags & (NFT_SET_MAP | NFT_SET_EVAL | NFT_SET_OBJECT)) ==
			     (NFT_SET_MAP | NFT_SET_EVAL | NFT_SET_OBJECT))
			return -EOPNOTSUPP;
	}

	dtype = 0;
	if (nla[NFTA_SET_DATA_TYPE] != NULL) {
		if (!(flags & NFT_SET_MAP))
			return -EINVAL;

		dtype = ntohl(nla_get_be32(nla[NFTA_SET_DATA_TYPE]));
		if ((dtype & NFT_DATA_RESERVED_MASK) == NFT_DATA_RESERVED_MASK &&
		    dtype != NFT_DATA_VERDICT)
			return -EINVAL;

		if (dtype != NFT_DATA_VERDICT) {
			if (nla[NFTA_SET_DATA_LEN] == NULL)
				return -EINVAL;
			desc.dlen = ntohl(nla_get_be32(nla[NFTA_SET_DATA_LEN]));
			if (desc.dlen == 0 || desc.dlen > NFT_DATA_VALUE_MAXLEN)
				return -EINVAL;
		} else
			desc.dlen = sizeof(struct nft_verdict);
	} else if (flags & NFT_SET_MAP)
		return -EINVAL;

	if (nla[NFTA_SET_OBJ_TYPE] != NULL) {
		if (!(flags & NFT_SET_OBJECT))
			return -EINVAL;

		objtype = ntohl(nla_get_be32(nla[NFTA_SET_OBJ_TYPE]));
		if (objtype == NFT_OBJECT_UNSPEC ||
		    objtype > NFT_OBJECT_MAX)
			return -EINVAL;
	} else if (flags & NFT_SET_OBJECT)
		return -EINVAL;
	else
		objtype = NFT_OBJECT_UNSPEC;

	timeout = 0;
	if (nla[NFTA_SET_TIMEOUT] != NULL) {
		if (!(flags & NFT_SET_TIMEOUT))
			return -EINVAL;
		timeout = msecs_to_jiffies(be64_to_cpu(nla_get_be64(
						nla[NFTA_SET_TIMEOUT])));
	}
	gc_int = 0;
	if (nla[NFTA_SET_GC_INTERVAL] != NULL) {
		if (!(flags & NFT_SET_TIMEOUT))
			return -EINVAL;
		gc_int = ntohl(nla_get_be32(nla[NFTA_SET_GC_INTERVAL]));
	}

	policy = NFT_SET_POL_PERFORMANCE;
	if (nla[NFTA_SET_POLICY] != NULL)
		policy = ntohl(nla_get_be32(nla[NFTA_SET_POLICY]));

	if (nla[NFTA_SET_DESC] != NULL) {
		err = nf_tables_set_desc_parse(&ctx, &desc, nla[NFTA_SET_DESC]);
		if (err < 0)
			return err;
	}

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, create);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_SET_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, NULL, nla);

	set = nf_tables_set_lookup(table, nla[NFTA_SET_NAME], genmask);
	if (IS_ERR(set)) {
		if (PTR_ERR(set) != -ENOENT)
			return PTR_ERR(set);
		set = NULL;
	}

	if (set != NULL) {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;
		return 0;
	}

	if (!(nlh->nlmsg_flags & NLM_F_CREATE))
		return -ENOENT;

	ops = nft_select_set_ops(nla, &desc, policy);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	udlen = 0;
	if (nla[NFTA_SET_USERDATA])
		udlen = nla_len(nla[NFTA_SET_USERDATA]);

	size = 0;
	if (ops->privsize != NULL)
		size = ops->privsize(nla);

	err = -ENOMEM;
	set = kzalloc(sizeof(*set) + size + udlen, GFP_KERNEL);
	if (set == NULL)
		goto err1;

	nla_strlcpy(name, nla[NFTA_SET_NAME], sizeof(set->name));
	err = nf_tables_set_alloc_name(&ctx, set, name);
	if (err < 0)
		goto err2;

	udata = NULL;
	if (udlen) {
		udata = set->data + size;
		nla_memcpy(udata, nla[NFTA_SET_USERDATA], udlen);
	}

	INIT_LIST_HEAD(&set->bindings);
	set->ops   = ops;
	set->ktype = ktype;
	set->klen  = desc.klen;
	set->dtype = dtype;
	set->objtype = objtype;
	set->dlen  = desc.dlen;
	set->flags = flags;
	set->size  = desc.size;
	set->policy = policy;
	set->udlen  = udlen;
	set->udata  = udata;
	set->timeout = timeout;
	set->gc_int = gc_int;

	err = ops->init(set, &desc, nla);
	if (err < 0)
		goto err2;

	err = nft_trans_set_add(&ctx, NFT_MSG_NEWSET, set);
	if (err < 0)
		goto err3;

	list_add_tail_rcu(&set->list, &table->sets);
	table->use++;
	return 0;

err3:
	ops->destroy(set);
err2:
	kfree(set);
err1:
	module_put(ops->owner);
	return err;
}

static void nft_set_destroy(struct nft_set *set)
{
	set->ops->destroy(set);
	module_put(set->ops->owner);
	kfree(set);
}

static void nf_tables_set_destroy(const struct nft_ctx *ctx, struct nft_set *set)
{
	list_del_rcu(&set->list);
	nf_tables_set_notify(ctx, set, NFT_MSG_DELSET, GFP_ATOMIC);
	nft_set_destroy(set);
}

static int nf_tables_delset(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_set *set;
	struct nft_ctx ctx;
	int err;

	if (nfmsg->nfgen_family == NFPROTO_UNSPEC)
		return -EAFNOSUPPORT;
	if (nla[NFTA_SET_TABLE] == NULL)
		return -EINVAL;

	err = nft_ctx_init_from_setattr(&ctx, net, skb, nlh, nla, genmask);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME], genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (!list_empty(&set->bindings))
		return -EBUSY;

	return nft_delset(&ctx, set);
}

static int nf_tables_bind_check_setelem(const struct nft_ctx *ctx,
					struct nft_set *set,
					const struct nft_set_iter *iter,
					struct nft_set_elem *elem)
{
	const struct nft_set_ext *ext = nft_set_elem_ext(set, elem->priv);
	enum nft_registers dreg;

	dreg = nft_type_to_reg(set->dtype);
	return nft_validate_register_store(ctx, dreg, nft_set_ext_data(ext),
					   set->dtype == NFT_DATA_VERDICT ?
					   NFT_DATA_VERDICT : NFT_DATA_VALUE,
					   set->dlen);
}

int nf_tables_bind_set(const struct nft_ctx *ctx, struct nft_set *set,
		       struct nft_set_binding *binding)
{
	struct nft_set_binding *i;
	struct nft_set_iter iter;

	if (!list_empty(&set->bindings) && set->flags & NFT_SET_ANONYMOUS)
		return -EBUSY;

	if (binding->flags & NFT_SET_MAP) {
		/* If the set is already bound to the same chain all
		 * jumps are already validated for that chain.
		 */
		list_for_each_entry(i, &set->bindings, list) {
			if (i->flags & NFT_SET_MAP &&
			    i->chain == binding->chain)
				goto bind;
		}

		iter.genmask	= nft_genmask_next(ctx->net);
		iter.skip 	= 0;
		iter.count	= 0;
		iter.err	= 0;
		iter.fn		= nf_tables_bind_check_setelem;

		set->ops->walk(ctx, set, &iter);
		if (iter.err < 0)
			return iter.err;
	}
bind:
	binding->chain = ctx->chain;
	list_add_tail_rcu(&binding->list, &set->bindings);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_tables_bind_set);

void nf_tables_unbind_set(const struct nft_ctx *ctx, struct nft_set *set,
			  struct nft_set_binding *binding)
{
	list_del_rcu(&binding->list);

	if (list_empty(&set->bindings) && set->flags & NFT_SET_ANONYMOUS &&
	    nft_is_active(ctx->net, set))
		nf_tables_set_destroy(ctx, set);
}
EXPORT_SYMBOL_GPL(nf_tables_unbind_set);

const struct nft_set_ext_type nft_set_ext_types[] = {
	[NFT_SET_EXT_KEY]		= {
		.align	= __alignof__(u32),
	},
	[NFT_SET_EXT_DATA]		= {
		.align	= __alignof__(u32),
	},
	[NFT_SET_EXT_EXPR]		= {
		.align	= __alignof__(struct nft_expr),
	},
	[NFT_SET_EXT_OBJREF]		= {
		.len	= sizeof(struct nft_object *),
		.align	= __alignof__(struct nft_object *),
	},
	[NFT_SET_EXT_FLAGS]		= {
		.len	= sizeof(u8),
		.align	= __alignof__(u8),
	},
	[NFT_SET_EXT_TIMEOUT]		= {
		.len	= sizeof(u64),
		.align	= __alignof__(u64),
	},
	[NFT_SET_EXT_EXPIRATION]	= {
		.len	= sizeof(unsigned long),
		.align	= __alignof__(unsigned long),
	},
	[NFT_SET_EXT_USERDATA]		= {
		.len	= sizeof(struct nft_userdata),
		.align	= __alignof__(struct nft_userdata),
	},
};
EXPORT_SYMBOL_GPL(nft_set_ext_types);

/*
 * Set elements
 */

static const struct nla_policy nft_set_elem_policy[NFTA_SET_ELEM_MAX + 1] = {
	[NFTA_SET_ELEM_KEY]		= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_DATA]		= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_FLAGS]		= { .type = NLA_U32 },
	[NFTA_SET_ELEM_TIMEOUT]		= { .type = NLA_U64 },
	[NFTA_SET_ELEM_USERDATA]	= { .type = NLA_BINARY,
					    .len = NFT_USERDATA_MAXLEN },
};

static const struct nla_policy nft_set_elem_list_policy[NFTA_SET_ELEM_LIST_MAX + 1] = {
	[NFTA_SET_ELEM_LIST_TABLE]	= { .type = NLA_STRING,
					    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_SET_ELEM_LIST_SET]	= { .type = NLA_STRING,
					    .len = NFT_SET_MAXNAMELEN - 1 },
	[NFTA_SET_ELEM_LIST_ELEMENTS]	= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_LIST_SET_ID]	= { .type = NLA_U32 },
};

static int nft_ctx_init_from_elemattr(struct nft_ctx *ctx, struct net *net,
				      const struct sk_buff *skb,
				      const struct nlmsghdr *nlh,
				      const struct nlattr * const nla[],
				      u8 genmask)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct nft_table *table;

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_SET_ELEM_LIST_TABLE],
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	nft_ctx_init(ctx, net, skb, nlh, afi, table, NULL, nla);
	return 0;
}

static int nf_tables_fill_setelem(struct sk_buff *skb,
				  const struct nft_set *set,
				  const struct nft_set_elem *elem)
{
	const struct nft_set_ext *ext = nft_set_elem_ext(set, elem->priv);
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_LIST_ELEM);
	if (nest == NULL)
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_SET_ELEM_KEY, nft_set_ext_key(ext),
			  NFT_DATA_VALUE, set->klen) < 0)
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA) &&
	    nft_data_dump(skb, NFTA_SET_ELEM_DATA, nft_set_ext_data(ext),
			  set->dtype == NFT_DATA_VERDICT ? NFT_DATA_VERDICT : NFT_DATA_VALUE,
			  set->dlen) < 0)
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_EXPR) &&
	    nft_expr_dump(skb, NFTA_SET_ELEM_EXPR, nft_set_ext_expr(ext)) < 0)
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF) &&
	    nla_put_string(skb, NFTA_SET_ELEM_OBJREF,
			   (*nft_set_ext_obj(ext))->name) < 0)
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_FLAGS) &&
	    nla_put_be32(skb, NFTA_SET_ELEM_FLAGS,
		         htonl(*nft_set_ext_flags(ext))))
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_TIMEOUT) &&
	    nla_put_be64(skb, NFTA_SET_ELEM_TIMEOUT,
			 cpu_to_be64(jiffies_to_msecs(
						*nft_set_ext_timeout(ext))),
			 NFTA_SET_ELEM_PAD))
		goto nla_put_failure;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_EXPIRATION)) {
		unsigned long expires, now = jiffies;

		expires = *nft_set_ext_expiration(ext);
		if (time_before(now, expires))
			expires -= now;
		else
			expires = 0;

		if (nla_put_be64(skb, NFTA_SET_ELEM_EXPIRATION,
				 cpu_to_be64(jiffies_to_msecs(expires)),
				 NFTA_SET_ELEM_PAD))
			goto nla_put_failure;
	}

	if (nft_set_ext_exists(ext, NFT_SET_EXT_USERDATA)) {
		struct nft_userdata *udata;

		udata = nft_set_ext_userdata(ext);
		if (nla_put(skb, NFTA_SET_ELEM_USERDATA,
			    udata->len + 1, udata->data))
			goto nla_put_failure;
	}

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -EMSGSIZE;
}

struct nft_set_dump_args {
	const struct netlink_callback	*cb;
	struct nft_set_iter		iter;
	struct sk_buff			*skb;
};

static int nf_tables_dump_setelem(const struct nft_ctx *ctx,
				  struct nft_set *set,
				  const struct nft_set_iter *iter,
				  struct nft_set_elem *elem)
{
	struct nft_set_dump_args *args;

	args = container_of(iter, struct nft_set_dump_args, iter);
	return nf_tables_fill_setelem(args->skb, set, elem);
}

static int nf_tables_dump_set(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u8 genmask = nft_genmask_cur(net);
	struct nft_set *set;
	struct nft_set_dump_args args;
	struct nft_ctx ctx;
	struct nlattr *nla[NFTA_SET_ELEM_LIST_MAX + 1];
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	struct nlattr *nest;
	u32 portid, seq;
	int event, err;

	err = nlmsg_parse(cb->nlh, sizeof(struct nfgenmsg), nla,
			  NFTA_SET_ELEM_LIST_MAX, nft_set_elem_list_policy);
	if (err < 0)
		return err;

	err = nft_ctx_init_from_elemattr(&ctx, net, cb->skb, cb->nlh,
					 (void *)nla, genmask);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET],
				   genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);

	event  = NFT_MSG_NEWSETELEM;
	event |= NFNL_SUBSYS_NFTABLES << 8;
	portid = NETLINK_CB(cb->skb).portid;
	seq    = cb->nlh->nlmsg_seq;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			NLM_F_MULTI);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = ctx.afi->family;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = htons(ctx.net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_TABLE, ctx.table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_SET, set->name))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NFTA_SET_ELEM_LIST_ELEMENTS);
	if (nest == NULL)
		goto nla_put_failure;

	args.cb			= cb;
	args.skb		= skb;
	args.iter.genmask	= nft_genmask_cur(ctx.net);
	args.iter.skip		= cb->args[0];
	args.iter.count		= 0;
	args.iter.err		= 0;
	args.iter.fn		= nf_tables_dump_setelem;
	set->ops->walk(&ctx, set, &args.iter);

	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);

	if (args.iter.err && args.iter.err != -EMSGSIZE)
		return args.iter.err;
	if (args.iter.count == cb->args[0])
		return 0;

	cb->args[0] = args.iter.count;
	return skb->len;

nla_put_failure:
	return -ENOSPC;
}

static int nf_tables_getsetelem(struct net *net, struct sock *nlsk,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	u8 genmask = nft_genmask_cur(net);
	const struct nft_set *set;
	struct nft_ctx ctx;
	int err;

	err = nft_ctx_init_from_elemattr(&ctx, net, skb, nlh, nla, genmask);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET],
				   genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_set,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}
	return -EOPNOTSUPP;
}

static int nf_tables_fill_setelem_info(struct sk_buff *skb,
				       const struct nft_ctx *ctx, u32 seq,
				       u32 portid, int event, u16 flags,
				       const struct nft_set *set,
				       const struct nft_set_elem *elem)
{
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	struct nlattr *nest;
	int err;

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= ctx->afi->family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(ctx->net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_SET_TABLE, ctx->table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_NAME, set->name))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NFTA_SET_ELEM_LIST_ELEMENTS);
	if (nest == NULL)
		goto nla_put_failure;

	err = nf_tables_fill_setelem(skb, set, elem);
	if (err < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_setelem_notify(const struct nft_ctx *ctx,
				    const struct nft_set *set,
				    const struct nft_set_elem *elem,
				    int event, u16 flags)
{
	struct net *net = ctx->net;
	u32 portid = ctx->portid;
	struct sk_buff *skb;
	int err;

	if (!ctx->report && !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_setelem_info(skb, ctx, 0, portid, event, flags,
					  set, elem);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, ctx->report,
			     GFP_KERNEL);
err:
	if (err < 0)
		nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, err);
	return err;
}

static struct nft_trans *nft_trans_elem_alloc(struct nft_ctx *ctx,
					      int msg_type,
					      struct nft_set *set)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_elem));
	if (trans == NULL)
		return NULL;

	nft_trans_elem_set(trans) = set;
	return trans;
}

void *nft_set_elem_init(const struct nft_set *set,
			const struct nft_set_ext_tmpl *tmpl,
			const u32 *key, const u32 *data,
			u64 timeout, gfp_t gfp)
{
	struct nft_set_ext *ext;
	void *elem;

	elem = kzalloc(set->ops->elemsize + tmpl->len, gfp);
	if (elem == NULL)
		return NULL;

	ext = nft_set_elem_ext(set, elem);
	nft_set_ext_init(ext, tmpl);

	memcpy(nft_set_ext_key(ext), key, set->klen);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA))
		memcpy(nft_set_ext_data(ext), data, set->dlen);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_EXPIRATION))
		*nft_set_ext_expiration(ext) =
			jiffies + timeout;
	if (nft_set_ext_exists(ext, NFT_SET_EXT_TIMEOUT))
		*nft_set_ext_timeout(ext) = timeout;

	return elem;
}

void nft_set_elem_destroy(const struct nft_set *set, void *elem,
			  bool destroy_expr)
{
	struct nft_set_ext *ext = nft_set_elem_ext(set, elem);

	nft_data_uninit(nft_set_ext_key(ext), NFT_DATA_VALUE);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA))
		nft_data_uninit(nft_set_ext_data(ext), set->dtype);
	if (destroy_expr && nft_set_ext_exists(ext, NFT_SET_EXT_EXPR))
		nf_tables_expr_destroy(NULL, nft_set_ext_expr(ext));
	if (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF))
		(*nft_set_ext_obj(ext))->use--;
	kfree(elem);
}
EXPORT_SYMBOL_GPL(nft_set_elem_destroy);

static int nft_setelem_parse_flags(const struct nft_set *set,
				   const struct nlattr *attr, u32 *flags)
{
	if (attr == NULL)
		return 0;

	*flags = ntohl(nla_get_be32(attr));
	if (*flags & ~NFT_SET_ELEM_INTERVAL_END)
		return -EINVAL;
	if (!(set->flags & NFT_SET_INTERVAL) &&
	    *flags & NFT_SET_ELEM_INTERVAL_END)
		return -EINVAL;

	return 0;
}

static int nft_add_set_elem(struct nft_ctx *ctx, struct nft_set *set,
			    const struct nlattr *attr, u32 nlmsg_flags)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	u8 genmask = nft_genmask_next(ctx->net);
	struct nft_data_desc d1, d2;
	struct nft_set_ext_tmpl tmpl;
	struct nft_set_ext *ext, *ext2;
	struct nft_set_elem elem;
	struct nft_set_binding *binding;
	struct nft_object *obj = NULL;
	struct nft_userdata *udata;
	struct nft_data data;
	enum nft_registers dreg;
	struct nft_trans *trans;
	u32 flags = 0;
	u64 timeout;
	u8 ulen;
	int err;

	err = nla_parse_nested(nla, NFTA_SET_ELEM_MAX, attr,
			       nft_set_elem_policy);
	if (err < 0)
		return err;

	if (nla[NFTA_SET_ELEM_KEY] == NULL)
		return -EINVAL;

	nft_set_ext_prepare(&tmpl);

	err = nft_setelem_parse_flags(set, nla[NFTA_SET_ELEM_FLAGS], &flags);
	if (err < 0)
		return err;
	if (flags != 0)
		nft_set_ext_add(&tmpl, NFT_SET_EXT_FLAGS);

	if (set->flags & NFT_SET_MAP) {
		if (nla[NFTA_SET_ELEM_DATA] == NULL &&
		    !(flags & NFT_SET_ELEM_INTERVAL_END))
			return -EINVAL;
		if (nla[NFTA_SET_ELEM_DATA] != NULL &&
		    flags & NFT_SET_ELEM_INTERVAL_END)
			return -EINVAL;
	} else {
		if (nla[NFTA_SET_ELEM_DATA] != NULL)
			return -EINVAL;
	}

	timeout = 0;
	if (nla[NFTA_SET_ELEM_TIMEOUT] != NULL) {
		if (!(set->flags & NFT_SET_TIMEOUT))
			return -EINVAL;
		timeout = msecs_to_jiffies(be64_to_cpu(nla_get_be64(
					nla[NFTA_SET_ELEM_TIMEOUT])));
	} else if (set->flags & NFT_SET_TIMEOUT) {
		timeout = set->timeout;
	}

	err = nft_data_init(ctx, &elem.key.val, sizeof(elem.key), &d1,
			    nla[NFTA_SET_ELEM_KEY]);
	if (err < 0)
		goto err1;
	err = -EINVAL;
	if (d1.type != NFT_DATA_VALUE || d1.len != set->klen)
		goto err2;

	nft_set_ext_add_length(&tmpl, NFT_SET_EXT_KEY, d1.len);
	if (timeout > 0) {
		nft_set_ext_add(&tmpl, NFT_SET_EXT_EXPIRATION);
		if (timeout != set->timeout)
			nft_set_ext_add(&tmpl, NFT_SET_EXT_TIMEOUT);
	}

	if (nla[NFTA_SET_ELEM_OBJREF] != NULL) {
		if (!(set->flags & NFT_SET_OBJECT)) {
			err = -EINVAL;
			goto err2;
		}
		obj = nf_tables_obj_lookup(ctx->table, nla[NFTA_SET_ELEM_OBJREF],
					   set->objtype, genmask);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err2;
		}
		nft_set_ext_add(&tmpl, NFT_SET_EXT_OBJREF);
	}

	if (nla[NFTA_SET_ELEM_DATA] != NULL) {
		err = nft_data_init(ctx, &data, sizeof(data), &d2,
				    nla[NFTA_SET_ELEM_DATA]);
		if (err < 0)
			goto err2;

		err = -EINVAL;
		if (set->dtype != NFT_DATA_VERDICT && d2.len != set->dlen)
			goto err3;

		dreg = nft_type_to_reg(set->dtype);
		list_for_each_entry(binding, &set->bindings, list) {
			struct nft_ctx bind_ctx = {
				.net	= ctx->net,
				.afi	= ctx->afi,
				.table	= ctx->table,
				.chain	= (struct nft_chain *)binding->chain,
			};

			if (!(binding->flags & NFT_SET_MAP))
				continue;

			err = nft_validate_register_store(&bind_ctx, dreg,
							  &data,
							  d2.type, d2.len);
			if (err < 0)
				goto err3;
		}

		nft_set_ext_add_length(&tmpl, NFT_SET_EXT_DATA, d2.len);
	}

	/* The full maximum length of userdata can exceed the maximum
	 * offset value (U8_MAX) for following extensions, therefor it
	 * must be the last extension added.
	 */
	ulen = 0;
	if (nla[NFTA_SET_ELEM_USERDATA] != NULL) {
		ulen = nla_len(nla[NFTA_SET_ELEM_USERDATA]);
		if (ulen > 0)
			nft_set_ext_add_length(&tmpl, NFT_SET_EXT_USERDATA,
					       ulen);
	}

	err = -ENOMEM;
	elem.priv = nft_set_elem_init(set, &tmpl, elem.key.val.data, data.data,
				      timeout, GFP_KERNEL);
	if (elem.priv == NULL)
		goto err3;

	ext = nft_set_elem_ext(set, elem.priv);
	if (flags)
		*nft_set_ext_flags(ext) = flags;
	if (ulen > 0) {
		udata = nft_set_ext_userdata(ext);
		udata->len = ulen - 1;
		nla_memcpy(&udata->data, nla[NFTA_SET_ELEM_USERDATA], ulen);
	}
	if (obj) {
		*nft_set_ext_obj(ext) = obj;
		obj->use++;
	}

	trans = nft_trans_elem_alloc(ctx, NFT_MSG_NEWSETELEM, set);
	if (trans == NULL)
		goto err4;

	ext->genmask = nft_genmask_cur(ctx->net) | NFT_SET_ELEM_BUSY_MASK;
	err = set->ops->insert(ctx->net, set, &elem, &ext2);
	if (err) {
		if (err == -EEXIST) {
			if ((nft_set_ext_exists(ext, NFT_SET_EXT_DATA) &&
			     nft_set_ext_exists(ext2, NFT_SET_EXT_DATA) &&
			     memcmp(nft_set_ext_data(ext),
				    nft_set_ext_data(ext2), set->dlen) != 0) ||
			    (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF) &&
			     nft_set_ext_exists(ext2, NFT_SET_EXT_OBJREF) &&
			     *nft_set_ext_obj(ext) != *nft_set_ext_obj(ext2)))
				err = -EBUSY;
			else if (!(nlmsg_flags & NLM_F_EXCL))
				err = 0;
		}
		goto err5;
	}

	if (set->size &&
	    !atomic_add_unless(&set->nelems, 1, set->size + set->ndeact)) {
		err = -ENFILE;
		goto err6;
	}

	nft_trans_elem(trans) = elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;

err6:
	set->ops->remove(set, &elem);
err5:
	kfree(trans);
err4:
	kfree(elem.priv);
err3:
	if (nla[NFTA_SET_ELEM_DATA] != NULL)
		nft_data_uninit(&data, d2.type);
err2:
	nft_data_uninit(&elem.key.val, d1.type);
err1:
	return err;
}

static int nf_tables_newsetelem(struct net *net, struct sock *nlsk,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	u8 genmask = nft_genmask_next(net);
	const struct nlattr *attr;
	struct nft_set *set;
	struct nft_ctx ctx;
	int rem, err = 0;

	if (nla[NFTA_SET_ELEM_LIST_ELEMENTS] == NULL)
		return -EINVAL;

	err = nft_ctx_init_from_elemattr(&ctx, net, skb, nlh, nla, genmask);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET],
				   genmask);
	if (IS_ERR(set)) {
		if (nla[NFTA_SET_ELEM_LIST_SET_ID]) {
			set = nf_tables_set_lookup_byid(net,
					nla[NFTA_SET_ELEM_LIST_SET_ID],
					genmask);
		}
		if (IS_ERR(set))
			return PTR_ERR(set);
	}

	if (!list_empty(&set->bindings) && set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_add_set_elem(&ctx, set, attr, nlh->nlmsg_flags);
		if (err < 0)
			break;
	}
	return err;
}

static int nft_del_setelem(struct nft_ctx *ctx, struct nft_set *set,
			   const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	struct nft_set_ext_tmpl tmpl;
	struct nft_data_desc desc;
	struct nft_set_elem elem;
	struct nft_set_ext *ext;
	struct nft_trans *trans;
	u32 flags = 0;
	void *priv;
	int err;

	err = nla_parse_nested(nla, NFTA_SET_ELEM_MAX, attr,
			       nft_set_elem_policy);
	if (err < 0)
		goto err1;

	err = -EINVAL;
	if (nla[NFTA_SET_ELEM_KEY] == NULL)
		goto err1;

	nft_set_ext_prepare(&tmpl);

	err = nft_setelem_parse_flags(set, nla[NFTA_SET_ELEM_FLAGS], &flags);
	if (err < 0)
		return err;
	if (flags != 0)
		nft_set_ext_add(&tmpl, NFT_SET_EXT_FLAGS);

	err = nft_data_init(ctx, &elem.key.val, sizeof(elem.key), &desc,
			    nla[NFTA_SET_ELEM_KEY]);
	if (err < 0)
		goto err1;

	err = -EINVAL;
	if (desc.type != NFT_DATA_VALUE || desc.len != set->klen)
		goto err2;

	nft_set_ext_add_length(&tmpl, NFT_SET_EXT_KEY, desc.len);

	err = -ENOMEM;
	elem.priv = nft_set_elem_init(set, &tmpl, elem.key.val.data, NULL, 0,
				      GFP_KERNEL);
	if (elem.priv == NULL)
		goto err2;

	ext = nft_set_elem_ext(set, elem.priv);
	if (flags)
		*nft_set_ext_flags(ext) = flags;

	trans = nft_trans_elem_alloc(ctx, NFT_MSG_DELSETELEM, set);
	if (trans == NULL) {
		err = -ENOMEM;
		goto err3;
	}

	priv = set->ops->deactivate(ctx->net, set, &elem);
	if (priv == NULL) {
		err = -ENOENT;
		goto err4;
	}
	kfree(elem.priv);
	elem.priv = priv;

	nft_trans_elem(trans) = elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;

err4:
	kfree(trans);
err3:
	kfree(elem.priv);
err2:
	nft_data_uninit(&elem.key.val, desc.type);
err1:
	return err;
}

static int nft_flush_set(const struct nft_ctx *ctx,
			 struct nft_set *set,
			 const struct nft_set_iter *iter,
			 struct nft_set_elem *elem)
{
	struct nft_trans *trans;
	int err;

	trans = nft_trans_alloc_gfp(ctx, NFT_MSG_DELSETELEM,
				    sizeof(struct nft_trans_elem), GFP_ATOMIC);
	if (!trans)
		return -ENOMEM;

	if (!set->ops->deactivate_one(ctx->net, set, elem->priv)) {
		err = -ENOENT;
		goto err1;
	}
	set->ndeact++;

	nft_trans_elem_set(trans) = set;
	nft_trans_elem(trans) = *elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
err1:
	kfree(trans);
	return err;
}

static int nf_tables_delsetelem(struct net *net, struct sock *nlsk,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	u8 genmask = nft_genmask_next(net);
	const struct nlattr *attr;
	struct nft_set *set;
	struct nft_ctx ctx;
	int rem, err = 0;

	err = nft_ctx_init_from_elemattr(&ctx, net, skb, nlh, nla, genmask);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET],
				   genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (!list_empty(&set->bindings) && set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	if (nla[NFTA_SET_ELEM_LIST_ELEMENTS] == NULL) {
		struct nft_set_dump_args args = {
			.iter	= {
				.genmask	= genmask,
				.fn		= nft_flush_set,
			},
		};
		set->ops->walk(&ctx, set, &args.iter);

		return args.iter.err;
	}

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_del_setelem(&ctx, set, attr);
		if (err < 0)
			break;

		set->ndeact++;
	}
	return err;
}

void nft_set_gc_batch_release(struct rcu_head *rcu)
{
	struct nft_set_gc_batch *gcb;
	unsigned int i;

	gcb = container_of(rcu, struct nft_set_gc_batch, head.rcu);
	for (i = 0; i < gcb->head.cnt; i++)
		nft_set_elem_destroy(gcb->head.set, gcb->elems[i], true);
	kfree(gcb);
}
EXPORT_SYMBOL_GPL(nft_set_gc_batch_release);

struct nft_set_gc_batch *nft_set_gc_batch_alloc(const struct nft_set *set,
						gfp_t gfp)
{
	struct nft_set_gc_batch *gcb;

	gcb = kzalloc(sizeof(*gcb), gfp);
	if (gcb == NULL)
		return gcb;
	gcb->head.set = set;
	return gcb;
}
EXPORT_SYMBOL_GPL(nft_set_gc_batch_alloc);

/*
 * Stateful objects
 */

/**
 *	nft_register_obj- register nf_tables stateful object type
 *	@obj: object type
 *
 *	Registers the object type for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_obj(struct nft_object_type *obj_type)
{
	if (obj_type->type == NFT_OBJECT_UNSPEC)
		return -EINVAL;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_rcu(&obj_type->list, &nf_tables_objects);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_obj);

/**
 *	nft_unregister_obj - unregister nf_tables object type
 *	@obj: object type
 *
 * 	Unregisters the object type for use with nf_tables.
 */
void nft_unregister_obj(struct nft_object_type *obj_type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del_rcu(&obj_type->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_obj);

struct nft_object *nf_tables_obj_lookup(const struct nft_table *table,
					const struct nlattr *nla,
					u32 objtype, u8 genmask)
{
	struct nft_object *obj;

	list_for_each_entry(obj, &table->objects, list) {
		if (!nla_strcmp(nla, obj->name) &&
		    objtype == obj->type->type &&
		    nft_active_genmask(obj, genmask))
			return obj;
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(nf_tables_obj_lookup);

static const struct nla_policy nft_obj_policy[NFTA_OBJ_MAX + 1] = {
	[NFTA_OBJ_TABLE]	= { .type = NLA_STRING,
				    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_OBJ_NAME]		= { .type = NLA_STRING,
				    .len = NFT_OBJ_MAXNAMELEN - 1 },
	[NFTA_OBJ_TYPE]		= { .type = NLA_U32 },
	[NFTA_OBJ_DATA]		= { .type = NLA_NESTED },
};

static struct nft_object *nft_obj_init(const struct nft_object_type *type,
				       const struct nlattr *attr)
{
	struct nlattr *tb[type->maxattr + 1];
	struct nft_object *obj;
	int err;

	if (attr) {
		err = nla_parse_nested(tb, type->maxattr, attr, type->policy);
		if (err < 0)
			goto err1;
	} else {
		memset(tb, 0, sizeof(tb[0]) * (type->maxattr + 1));
	}

	err = -ENOMEM;
	obj = kzalloc(sizeof(struct nft_object) + type->size, GFP_KERNEL);
	if (obj == NULL)
		goto err1;

	err = type->init((const struct nlattr * const *)tb, obj);
	if (err < 0)
		goto err2;

	obj->type = type;
	return obj;
err2:
	kfree(obj);
err1:
	return ERR_PTR(err);
}

static int nft_object_dump(struct sk_buff *skb, unsigned int attr,
			   struct nft_object *obj, bool reset)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, attr);
	if (!nest)
		goto nla_put_failure;
	if (obj->type->dump(skb, obj, reset) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_object_type *__nft_obj_type_get(u32 objtype)
{
	const struct nft_object_type *type;

	list_for_each_entry(type, &nf_tables_objects, list) {
		if (objtype == type->type)
			return type;
	}
	return NULL;
}

static const struct nft_object_type *nft_obj_type_get(u32 objtype)
{
	const struct nft_object_type *type;

	type = __nft_obj_type_get(objtype);
	if (type != NULL && try_module_get(type->owner))
		return type;

#ifdef CONFIG_MODULES
	if (type == NULL) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-obj-%u", objtype);
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_obj_type_get(objtype))
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-ENOENT);
}

static int nf_tables_newobj(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_object_type *type;
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_object *obj;
	struct nft_ctx ctx;
	u32 objtype;
	int err;

	if (!nla[NFTA_OBJ_TYPE] ||
	    !nla[NFTA_OBJ_NAME] ||
	    !nla[NFTA_OBJ_DATA])
		return -EINVAL;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_OBJ_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	objtype = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));
	obj = nf_tables_obj_lookup(table, nla[NFTA_OBJ_NAME], objtype, genmask);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		if (err != -ENOENT)
			return err;

		obj = NULL;
	}

	if (obj != NULL) {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		return 0;
	}

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, NULL, nla);

	type = nft_obj_type_get(objtype);
	if (IS_ERR(type))
		return PTR_ERR(type);

	obj = nft_obj_init(type, nla[NFTA_OBJ_DATA]);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err1;
	}
	obj->table = table;
	nla_strlcpy(obj->name, nla[NFTA_OBJ_NAME], NFT_OBJ_MAXNAMELEN);

	err = nft_trans_obj_add(&ctx, NFT_MSG_NEWOBJ, obj);
	if (err < 0)
		goto err2;

	list_add_tail_rcu(&obj->list, &table->objects);
	table->use++;
	return 0;
err2:
	if (obj->type->destroy)
		obj->type->destroy(obj);
	kfree(obj);
err1:
	module_put(type->owner);
	return err;
}

static int nf_tables_fill_obj_info(struct sk_buff *skb, struct net *net,
				   u32 portid, u32 seq, int event, u32 flags,
				   int family, const struct nft_table *table,
				   struct nft_object *obj, bool reset)
{
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_OBJ_TABLE, table->name) ||
	    nla_put_string(skb, NFTA_OBJ_NAME, obj->name) ||
	    nla_put_be32(skb, NFTA_OBJ_TYPE, htonl(obj->type->type)) ||
	    nla_put_be32(skb, NFTA_OBJ_USE, htonl(obj->use)) ||
	    nft_object_dump(skb, NFTA_OBJ_DATA, obj, reset))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

struct nft_obj_filter {
	char		table[NFT_OBJ_MAXNAMELEN];
	u32		type;
};

static int nf_tables_dump_obj(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct nft_obj_filter *filter = cb->data;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	struct nft_object *obj;
	bool reset = false;

	if (NFNL_MSG_TYPE(cb->nlh->nlmsg_type) == NFT_MSG_GETOBJ_RESET)
		reset = true;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry_rcu(table, &afi->tables, list) {
			list_for_each_entry_rcu(obj, &table->objects, list) {
				if (!nft_is_active(net, obj))
					goto cont;
				if (idx < s_idx)
					goto cont;
				if (idx > s_idx)
					memset(&cb->args[1], 0,
					       sizeof(cb->args) - sizeof(cb->args[0]));
				if (filter && filter->table[0] &&
				    strcmp(filter->table, table->name))
					goto cont;
				if (filter &&
				    filter->type != NFT_OBJECT_UNSPEC &&
				    obj->type->type != filter->type)
					goto cont;

				if (nf_tables_fill_obj_info(skb, net, NETLINK_CB(cb->skb).portid,
							    cb->nlh->nlmsg_seq,
							    NFT_MSG_NEWOBJ,
							    NLM_F_MULTI | NLM_F_APPEND,
							    afi->family, table, obj, reset) < 0)
					goto done;

				nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
				idx++;
			}
		}
	}
done:
	rcu_read_unlock();

	cb->args[0] = idx;
	return skb->len;
}

static int nf_tables_dump_obj_done(struct netlink_callback *cb)
{
	kfree(cb->data);

	return 0;
}

static struct nft_obj_filter *
nft_obj_filter_alloc(const struct nlattr * const nla[])
{
	struct nft_obj_filter *filter;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return ERR_PTR(-ENOMEM);

	if (nla[NFTA_OBJ_TABLE])
		nla_strlcpy(filter->table, nla[NFTA_OBJ_TABLE],
			    NFT_TABLE_MAXNAMELEN);
	if (nla[NFTA_OBJ_TYPE])
		filter->type = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));

	return filter;
}

static int nf_tables_getobj(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	int family = nfmsg->nfgen_family;
	const struct nft_af_info *afi;
	const struct nft_table *table;
	struct nft_object *obj;
	struct sk_buff *skb2;
	bool reset = false;
	u32 objtype;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_obj,
			.done = nf_tables_dump_obj_done,
		};

		if (nla[NFTA_OBJ_TABLE] ||
		    nla[NFTA_OBJ_TYPE]) {
			struct nft_obj_filter *filter;

			filter = nft_obj_filter_alloc(nla);
			if (IS_ERR(filter))
				return -ENOMEM;

			c.data = filter;
		}
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	if (!nla[NFTA_OBJ_NAME] ||
	    !nla[NFTA_OBJ_TYPE])
		return -EINVAL;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_OBJ_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	objtype = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));
	obj = nf_tables_obj_lookup(table, nla[NFTA_OBJ_NAME], objtype, genmask);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	if (NFNL_MSG_TYPE(nlh->nlmsg_type) == NFT_MSG_GETOBJ_RESET)
		reset = true;

	err = nf_tables_fill_obj_info(skb2, net, NETLINK_CB(skb).portid,
				      nlh->nlmsg_seq, NFT_MSG_NEWOBJ, 0,
				      family, table, obj, reset);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);
err:
	kfree_skb(skb2);
	return err;

	return 0;
}

static void nft_obj_destroy(struct nft_object *obj)
{
	if (obj->type->destroy)
		obj->type->destroy(obj);

	module_put(obj->type->owner);
	kfree(obj);
}

static int nf_tables_delobj(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_object *obj;
	struct nft_ctx ctx;
	u32 objtype;

	if (!nla[NFTA_OBJ_TYPE] ||
	    !nla[NFTA_OBJ_NAME])
		return -EINVAL;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_OBJ_TABLE], genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	objtype = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));
	obj = nf_tables_obj_lookup(table, nla[NFTA_OBJ_NAME], objtype, genmask);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	if (obj->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, net, skb, nlh, afi, table, NULL, nla);

	return nft_delobj(&ctx, obj);
}

int nft_obj_notify(struct net *net, struct nft_table *table,
		   struct nft_object *obj, u32 portid, u32 seq, int event,
		   int family, int report, gfp_t gfp)
{
	struct sk_buff *skb;
	int err;

	if (!report &&
	    !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_obj_info(skb, net, portid, seq, event, 0, family,
				      table, obj, false);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, report, gfp);
err:
	if (err < 0) {
		nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, err);
	}
	return err;
}
EXPORT_SYMBOL_GPL(nft_obj_notify);

static int nf_tables_obj_notify(const struct nft_ctx *ctx,
				struct nft_object *obj, int event)
{
	return nft_obj_notify(ctx->net, ctx->table, obj, ctx->portid,
			      ctx->seq, event, ctx->afi->family, ctx->report,
			      GFP_KERNEL);
}

static int nf_tables_fill_gen_info(struct sk_buff *skb, struct net *net,
				   u32 portid, u32 seq)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	int event = (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWGEN;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), 0);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= AF_UNSPEC;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_be32(skb, NFTA_GEN_ID, htonl(net->nft.base_seq)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -EMSGSIZE;
}

static int nf_tables_gen_notify(struct net *net, struct sk_buff *skb, int event)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct sk_buff *skb2;
	int err;

	if (nlmsg_report(nlh) &&
	    !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb2 = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb2 == NULL)
		goto err;

	err = nf_tables_fill_gen_info(skb2, net, NETLINK_CB(skb).portid,
				      nlh->nlmsg_seq);
	if (err < 0) {
		kfree_skb(skb2);
		goto err;
	}

	err = nfnetlink_send(skb2, net, NETLINK_CB(skb).portid,
			     NFNLGRP_NFTABLES, nlmsg_report(nlh), GFP_KERNEL);
err:
	if (err < 0) {
		nfnetlink_set_err(net, NETLINK_CB(skb).portid, NFNLGRP_NFTABLES,
				  err);
	}
	return err;
}

static int nf_tables_getgen(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	struct sk_buff *skb2;
	int err;

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	err = nf_tables_fill_gen_info(skb2, net, NETLINK_CB(skb).portid,
				      nlh->nlmsg_seq);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);
err:
	kfree_skb(skb2);
	return err;
}

static const struct nfnl_callback nf_tables_cb[NFT_MSG_MAX] = {
	[NFT_MSG_NEWTABLE] = {
		.call_batch	= nf_tables_newtable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_GETTABLE] = {
		.call		= nf_tables_gettable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_DELTABLE] = {
		.call_batch	= nf_tables_deltable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_NEWCHAIN] = {
		.call_batch	= nf_tables_newchain,
		.attr_count	= NFTA_CHAIN_MAX,
		.policy		= nft_chain_policy,
	},
	[NFT_MSG_GETCHAIN] = {
		.call		= nf_tables_getchain,
		.attr_count	= NFTA_CHAIN_MAX,
		.policy		= nft_chain_policy,
	},
	[NFT_MSG_DELCHAIN] = {
		.call_batch	= nf_tables_delchain,
		.attr_count	= NFTA_CHAIN_MAX,
		.policy		= nft_chain_policy,
	},
	[NFT_MSG_NEWRULE] = {
		.call_batch	= nf_tables_newrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
	[NFT_MSG_GETRULE] = {
		.call		= nf_tables_getrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
	[NFT_MSG_DELRULE] = {
		.call_batch	= nf_tables_delrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
	[NFT_MSG_NEWSET] = {
		.call_batch	= nf_tables_newset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_GETSET] = {
		.call		= nf_tables_getset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_DELSET] = {
		.call_batch	= nf_tables_delset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_NEWSETELEM] = {
		.call_batch	= nf_tables_newsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
	[NFT_MSG_GETSETELEM] = {
		.call		= nf_tables_getsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
	[NFT_MSG_DELSETELEM] = {
		.call_batch	= nf_tables_delsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
	[NFT_MSG_GETGEN] = {
		.call		= nf_tables_getgen,
	},
	[NFT_MSG_NEWOBJ] = {
		.call_batch	= nf_tables_newobj,
		.attr_count	= NFTA_OBJ_MAX,
		.policy		= nft_obj_policy,
	},
	[NFT_MSG_GETOBJ] = {
		.call		= nf_tables_getobj,
		.attr_count	= NFTA_OBJ_MAX,
		.policy		= nft_obj_policy,
	},
	[NFT_MSG_DELOBJ] = {
		.call_batch	= nf_tables_delobj,
		.attr_count	= NFTA_OBJ_MAX,
		.policy		= nft_obj_policy,
	},
	[NFT_MSG_GETOBJ_RESET] = {
		.call		= nf_tables_getobj,
		.attr_count	= NFTA_OBJ_MAX,
		.policy		= nft_obj_policy,
	},
};

static void nft_chain_commit_update(struct nft_trans *trans)
{
	struct nft_base_chain *basechain;

	if (nft_trans_chain_name(trans)[0])
		strcpy(trans->ctx.chain->name, nft_trans_chain_name(trans));

	if (!(trans->ctx.chain->flags & NFT_BASE_CHAIN))
		return;

	basechain = nft_base_chain(trans->ctx.chain);
	nft_chain_stats_replace(basechain, nft_trans_chain_stats(trans));

	switch (nft_trans_chain_policy(trans)) {
	case NF_DROP:
	case NF_ACCEPT:
		basechain->policy = nft_trans_chain_policy(trans);
		break;
	}
}

static void nf_tables_commit_release(struct nft_trans *trans)
{
	switch (trans->msg_type) {
	case NFT_MSG_DELTABLE:
		nf_tables_table_destroy(&trans->ctx);
		break;
	case NFT_MSG_DELCHAIN:
		nf_tables_chain_destroy(trans->ctx.chain);
		break;
	case NFT_MSG_DELRULE:
		nf_tables_rule_destroy(&trans->ctx, nft_trans_rule(trans));
		break;
	case NFT_MSG_DELSET:
		nft_set_destroy(nft_trans_set(trans));
		break;
	case NFT_MSG_DELSETELEM:
		nft_set_elem_destroy(nft_trans_elem_set(trans),
				     nft_trans_elem(trans).priv, true);
		break;
	case NFT_MSG_DELOBJ:
		nft_obj_destroy(nft_trans_obj(trans));
		break;
	}
	kfree(trans);
}

static int nf_tables_commit(struct net *net, struct sk_buff *skb)
{
	struct nft_trans *trans, *next;
	struct nft_trans_elem *te;

	/* Bump generation counter, invalidate any dump in progress */
	while (++net->nft.base_seq == 0);

	/* A new generation has just started */
	net->nft.gencursor = nft_gencursor_next(net);

	/* Make sure all packets have left the previous generation before
	 * purging old rules.
	 */
	synchronize_rcu();

	list_for_each_entry_safe(trans, next, &net->nft.commit_list, list) {
		switch (trans->msg_type) {
		case NFT_MSG_NEWTABLE:
			if (nft_trans_table_update(trans)) {
				if (!nft_trans_table_enable(trans)) {
					nf_tables_table_disable(net,
								trans->ctx.afi,
								trans->ctx.table);
					trans->ctx.table->flags |= NFT_TABLE_F_DORMANT;
				}
			} else {
				nft_clear(net, trans->ctx.table);
			}
			nf_tables_table_notify(&trans->ctx, NFT_MSG_NEWTABLE);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELTABLE:
			list_del_rcu(&trans->ctx.table->list);
			nf_tables_table_notify(&trans->ctx, NFT_MSG_DELTABLE);
			break;
		case NFT_MSG_NEWCHAIN:
			if (nft_trans_chain_update(trans))
				nft_chain_commit_update(trans);
			else
				nft_clear(net, trans->ctx.chain);

			nf_tables_chain_notify(&trans->ctx, NFT_MSG_NEWCHAIN);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELCHAIN:
			list_del_rcu(&trans->ctx.chain->list);
			nf_tables_chain_notify(&trans->ctx, NFT_MSG_DELCHAIN);
			nf_tables_unregister_hooks(trans->ctx.net,
						   trans->ctx.table,
						   trans->ctx.chain,
						   trans->ctx.afi->nops);
			break;
		case NFT_MSG_NEWRULE:
			nft_clear(trans->ctx.net, nft_trans_rule(trans));
			nf_tables_rule_notify(&trans->ctx,
					      nft_trans_rule(trans),
					      NFT_MSG_NEWRULE);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELRULE:
			list_del_rcu(&nft_trans_rule(trans)->list);
			nf_tables_rule_notify(&trans->ctx,
					      nft_trans_rule(trans),
					      NFT_MSG_DELRULE);
			break;
		case NFT_MSG_NEWSET:
			nft_clear(net, nft_trans_set(trans));
			/* This avoids hitting -EBUSY when deleting the table
			 * from the transaction.
			 */
			if (nft_trans_set(trans)->flags & NFT_SET_ANONYMOUS &&
			    !list_empty(&nft_trans_set(trans)->bindings))
				trans->ctx.table->use--;

			nf_tables_set_notify(&trans->ctx, nft_trans_set(trans),
					     NFT_MSG_NEWSET, GFP_KERNEL);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELSET:
			list_del_rcu(&nft_trans_set(trans)->list);
			nf_tables_set_notify(&trans->ctx, nft_trans_set(trans),
					     NFT_MSG_DELSET, GFP_KERNEL);
			break;
		case NFT_MSG_NEWSETELEM:
			te = (struct nft_trans_elem *)trans->data;

			te->set->ops->activate(net, te->set, &te->elem);
			nf_tables_setelem_notify(&trans->ctx, te->set,
						 &te->elem,
						 NFT_MSG_NEWSETELEM, 0);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELSETELEM:
			te = (struct nft_trans_elem *)trans->data;

			nf_tables_setelem_notify(&trans->ctx, te->set,
						 &te->elem,
						 NFT_MSG_DELSETELEM, 0);
			te->set->ops->remove(te->set, &te->elem);
			atomic_dec(&te->set->nelems);
			te->set->ndeact--;
			break;
		case NFT_MSG_NEWOBJ:
			nft_clear(net, nft_trans_obj(trans));
			nf_tables_obj_notify(&trans->ctx, nft_trans_obj(trans),
					     NFT_MSG_NEWOBJ);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELOBJ:
			list_del_rcu(&nft_trans_obj(trans)->list);
			nf_tables_obj_notify(&trans->ctx, nft_trans_obj(trans),
					     NFT_MSG_DELOBJ);
			break;
		}
	}

	synchronize_rcu();

	list_for_each_entry_safe(trans, next, &net->nft.commit_list, list) {
		list_del(&trans->list);
		nf_tables_commit_release(trans);
	}

	nf_tables_gen_notify(net, skb, NFT_MSG_NEWGEN);

	return 0;
}

static void nf_tables_abort_release(struct nft_trans *trans)
{
	switch (trans->msg_type) {
	case NFT_MSG_NEWTABLE:
		nf_tables_table_destroy(&trans->ctx);
		break;
	case NFT_MSG_NEWCHAIN:
		nf_tables_chain_destroy(trans->ctx.chain);
		break;
	case NFT_MSG_NEWRULE:
		nf_tables_rule_destroy(&trans->ctx, nft_trans_rule(trans));
		break;
	case NFT_MSG_NEWSET:
		nft_set_destroy(nft_trans_set(trans));
		break;
	case NFT_MSG_NEWSETELEM:
		nft_set_elem_destroy(nft_trans_elem_set(trans),
				     nft_trans_elem(trans).priv, true);
		break;
	case NFT_MSG_NEWOBJ:
		nft_obj_destroy(nft_trans_obj(trans));
		break;
	}
	kfree(trans);
}

static int nf_tables_abort(struct net *net, struct sk_buff *skb)
{
	struct nft_trans *trans, *next;
	struct nft_trans_elem *te;

	list_for_each_entry_safe_reverse(trans, next, &net->nft.commit_list,
					 list) {
		switch (trans->msg_type) {
		case NFT_MSG_NEWTABLE:
			if (nft_trans_table_update(trans)) {
				if (nft_trans_table_enable(trans)) {
					nf_tables_table_disable(net,
								trans->ctx.afi,
								trans->ctx.table);
					trans->ctx.table->flags |= NFT_TABLE_F_DORMANT;
				}
				nft_trans_destroy(trans);
			} else {
				list_del_rcu(&trans->ctx.table->list);
			}
			break;
		case NFT_MSG_DELTABLE:
			nft_clear(trans->ctx.net, trans->ctx.table);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWCHAIN:
			if (nft_trans_chain_update(trans)) {
				free_percpu(nft_trans_chain_stats(trans));

				nft_trans_destroy(trans);
			} else {
				trans->ctx.table->use--;
				list_del_rcu(&trans->ctx.chain->list);
				nf_tables_unregister_hooks(trans->ctx.net,
							   trans->ctx.table,
							   trans->ctx.chain,
							   trans->ctx.afi->nops);
			}
			break;
		case NFT_MSG_DELCHAIN:
			trans->ctx.table->use++;
			nft_clear(trans->ctx.net, trans->ctx.chain);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWRULE:
			trans->ctx.chain->use--;
			list_del_rcu(&nft_trans_rule(trans)->list);
			break;
		case NFT_MSG_DELRULE:
			trans->ctx.chain->use++;
			nft_clear(trans->ctx.net, nft_trans_rule(trans));
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWSET:
			trans->ctx.table->use--;
			list_del_rcu(&nft_trans_set(trans)->list);
			break;
		case NFT_MSG_DELSET:
			trans->ctx.table->use++;
			nft_clear(trans->ctx.net, nft_trans_set(trans));
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWSETELEM:
			te = (struct nft_trans_elem *)trans->data;

			te->set->ops->remove(te->set, &te->elem);
			atomic_dec(&te->set->nelems);
			break;
		case NFT_MSG_DELSETELEM:
			te = (struct nft_trans_elem *)trans->data;

			te->set->ops->activate(net, te->set, &te->elem);
			te->set->ndeact--;

			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWOBJ:
			trans->ctx.table->use--;
			list_del_rcu(&nft_trans_obj(trans)->list);
			break;
		case NFT_MSG_DELOBJ:
			trans->ctx.table->use++;
			nft_clear(trans->ctx.net, nft_trans_obj(trans));
			nft_trans_destroy(trans);
			break;
		}
	}

	synchronize_rcu();

	list_for_each_entry_safe_reverse(trans, next,
					 &net->nft.commit_list, list) {
		list_del(&trans->list);
		nf_tables_abort_release(trans);
	}

	return 0;
}

static const struct nfnetlink_subsystem nf_tables_subsys = {
	.name		= "nf_tables",
	.subsys_id	= NFNL_SUBSYS_NFTABLES,
	.cb_count	= NFT_MSG_MAX,
	.cb		= nf_tables_cb,
	.commit		= nf_tables_commit,
	.abort		= nf_tables_abort,
};

int nft_chain_validate_dependency(const struct nft_chain *chain,
				  enum nft_chain_type type)
{
	const struct nft_base_chain *basechain;

	if (chain->flags & NFT_BASE_CHAIN) {
		basechain = nft_base_chain(chain);
		if (basechain->type->type != type)
			return -EOPNOTSUPP;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(nft_chain_validate_dependency);

int nft_chain_validate_hooks(const struct nft_chain *chain,
			     unsigned int hook_flags)
{
	struct nft_base_chain *basechain;

	if (chain->flags & NFT_BASE_CHAIN) {
		basechain = nft_base_chain(chain);

		if ((1 << basechain->ops[0].hooknum) & hook_flags)
			return 0;

		return -EOPNOTSUPP;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nft_chain_validate_hooks);

/*
 * Loop detection - walk through the ruleset beginning at the destination chain
 * of a new jump until either the source chain is reached (loop) or all
 * reachable chains have been traversed.
 *
 * The loop check is performed whenever a new jump verdict is added to an
 * expression or verdict map or a verdict map is bound to a new chain.
 */

static int nf_tables_check_loops(const struct nft_ctx *ctx,
				 const struct nft_chain *chain);

static int nf_tables_loop_check_setelem(const struct nft_ctx *ctx,
					struct nft_set *set,
					const struct nft_set_iter *iter,
					struct nft_set_elem *elem)
{
	const struct nft_set_ext *ext = nft_set_elem_ext(set, elem->priv);
	const struct nft_data *data;

	if (nft_set_ext_exists(ext, NFT_SET_EXT_FLAGS) &&
	    *nft_set_ext_flags(ext) & NFT_SET_ELEM_INTERVAL_END)
		return 0;

	data = nft_set_ext_data(ext);
	switch (data->verdict.code) {
	case NFT_JUMP:
	case NFT_GOTO:
		return nf_tables_check_loops(ctx, data->verdict.chain);
	default:
		return 0;
	}
}

static int nf_tables_check_loops(const struct nft_ctx *ctx,
				 const struct nft_chain *chain)
{
	const struct nft_rule *rule;
	const struct nft_expr *expr, *last;
	struct nft_set *set;
	struct nft_set_binding *binding;
	struct nft_set_iter iter;

	if (ctx->chain == chain)
		return -ELOOP;

	list_for_each_entry(rule, &chain->rules, list) {
		nft_rule_for_each_expr(expr, last, rule) {
			const struct nft_data *data = NULL;
			int err;

			if (!expr->ops->validate)
				continue;

			err = expr->ops->validate(ctx, expr, &data);
			if (err < 0)
				return err;

			if (data == NULL)
				continue;

			switch (data->verdict.code) {
			case NFT_JUMP:
			case NFT_GOTO:
				err = nf_tables_check_loops(ctx,
							data->verdict.chain);
				if (err < 0)
					return err;
			default:
				break;
			}
		}
	}

	list_for_each_entry(set, &ctx->table->sets, list) {
		if (!nft_is_active_next(ctx->net, set))
			continue;
		if (!(set->flags & NFT_SET_MAP) ||
		    set->dtype != NFT_DATA_VERDICT)
			continue;

		list_for_each_entry(binding, &set->bindings, list) {
			if (!(binding->flags & NFT_SET_MAP) ||
			    binding->chain != chain)
				continue;

			iter.genmask	= nft_genmask_next(ctx->net);
			iter.skip 	= 0;
			iter.count	= 0;
			iter.err	= 0;
			iter.fn		= nf_tables_loop_check_setelem;

			set->ops->walk(ctx, set, &iter);
			if (iter.err < 0)
				return iter.err;
		}
	}

	return 0;
}

/**
 *	nft_parse_u32_check - fetch u32 attribute and check for maximum value
 *
 *	@attr: netlink attribute to fetch value from
 *	@max: maximum value to be stored in dest
 *	@dest: pointer to the variable
 *
 *	Parse, check and store a given u32 netlink attribute into variable.
 *	This function returns -ERANGE if the value goes over maximum value.
 *	Otherwise a 0 is returned and the attribute value is stored in the
 *	destination variable.
 */
int nft_parse_u32_check(const struct nlattr *attr, int max, u32 *dest)
{
	u32 val;

	val = ntohl(nla_get_be32(attr));
	if (val > max)
		return -ERANGE;

	*dest = val;
	return 0;
}
EXPORT_SYMBOL_GPL(nft_parse_u32_check);

/**
 *	nft_parse_register - parse a register value from a netlink attribute
 *
 *	@attr: netlink attribute
 *
 *	Parse and translate a register value from a netlink attribute.
 *	Registers used to be 128 bit wide, these register numbers will be
 *	mapped to the corresponding 32 bit register numbers.
 */
unsigned int nft_parse_register(const struct nlattr *attr)
{
	unsigned int reg;

	reg = ntohl(nla_get_be32(attr));
	switch (reg) {
	case NFT_REG_VERDICT...NFT_REG_4:
		return reg * NFT_REG_SIZE / NFT_REG32_SIZE;
	default:
		return reg + NFT_REG_SIZE / NFT_REG32_SIZE - NFT_REG32_00;
	}
}
EXPORT_SYMBOL_GPL(nft_parse_register);

/**
 *	nft_dump_register - dump a register value to a netlink attribute
 *
 *	@skb: socket buffer
 *	@attr: attribute number
 *	@reg: register number
 *
 *	Construct a netlink attribute containing the register number. For
 *	compatibility reasons, register numbers being a multiple of 4 are
 *	translated to the corresponding 128 bit register numbers.
 */
int nft_dump_register(struct sk_buff *skb, unsigned int attr, unsigned int reg)
{
	if (reg % (NFT_REG_SIZE / NFT_REG32_SIZE) == 0)
		reg = reg / (NFT_REG_SIZE / NFT_REG32_SIZE);
	else
		reg = reg - NFT_REG_SIZE / NFT_REG32_SIZE + NFT_REG32_00;

	return nla_put_be32(skb, attr, htonl(reg));
}
EXPORT_SYMBOL_GPL(nft_dump_register);

/**
 *	nft_validate_register_load - validate a load from a register
 *
 *	@reg: the register number
 *	@len: the length of the data
 *
 * 	Validate that the input register is one of the general purpose
 * 	registers and that the length of the load is within the bounds.
 */
int nft_validate_register_load(enum nft_registers reg, unsigned int len)
{
	if (reg < NFT_REG_1 * NFT_REG_SIZE / NFT_REG32_SIZE)
		return -EINVAL;
	if (len == 0)
		return -EINVAL;
	if (reg * NFT_REG32_SIZE + len > FIELD_SIZEOF(struct nft_regs, data))
		return -ERANGE;

	return 0;
}
EXPORT_SYMBOL_GPL(nft_validate_register_load);

/**
 *	nft_validate_register_store - validate an expressions' register store
 *
 *	@ctx: context of the expression performing the load
 * 	@reg: the destination register number
 * 	@data: the data to load
 * 	@type: the data type
 * 	@len: the length of the data
 *
 * 	Validate that a data load uses the appropriate data type for
 * 	the destination register and the length is within the bounds.
 * 	A value of NULL for the data means that its runtime gathered
 * 	data.
 */
int nft_validate_register_store(const struct nft_ctx *ctx,
				enum nft_registers reg,
				const struct nft_data *data,
				enum nft_data_types type, unsigned int len)
{
	int err;

	switch (reg) {
	case NFT_REG_VERDICT:
		if (type != NFT_DATA_VERDICT)
			return -EINVAL;

		if (data != NULL &&
		    (data->verdict.code == NFT_GOTO ||
		     data->verdict.code == NFT_JUMP)) {
			err = nf_tables_check_loops(ctx, data->verdict.chain);
			if (err < 0)
				return err;

			if (ctx->chain->level + 1 >
			    data->verdict.chain->level) {
				if (ctx->chain->level + 1 == NFT_JUMP_STACK_SIZE)
					return -EMLINK;
				data->verdict.chain->level = ctx->chain->level + 1;
			}
		}

		return 0;
	default:
		if (reg < NFT_REG_1 * NFT_REG_SIZE / NFT_REG32_SIZE)
			return -EINVAL;
		if (len == 0)
			return -EINVAL;
		if (reg * NFT_REG32_SIZE + len >
		    FIELD_SIZEOF(struct nft_regs, data))
			return -ERANGE;

		if (data != NULL && type != NFT_DATA_VALUE)
			return -EINVAL;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(nft_validate_register_store);

static const struct nla_policy nft_verdict_policy[NFTA_VERDICT_MAX + 1] = {
	[NFTA_VERDICT_CODE]	= { .type = NLA_U32 },
	[NFTA_VERDICT_CHAIN]	= { .type = NLA_STRING,
				    .len = NFT_CHAIN_MAXNAMELEN - 1 },
};

static int nft_verdict_init(const struct nft_ctx *ctx, struct nft_data *data,
			    struct nft_data_desc *desc, const struct nlattr *nla)
{
	u8 genmask = nft_genmask_next(ctx->net);
	struct nlattr *tb[NFTA_VERDICT_MAX + 1];
	struct nft_chain *chain;
	int err;

	err = nla_parse_nested(tb, NFTA_VERDICT_MAX, nla, nft_verdict_policy);
	if (err < 0)
		return err;

	if (!tb[NFTA_VERDICT_CODE])
		return -EINVAL;
	data->verdict.code = ntohl(nla_get_be32(tb[NFTA_VERDICT_CODE]));

	switch (data->verdict.code) {
	default:
		switch (data->verdict.code & NF_VERDICT_MASK) {
		case NF_ACCEPT:
		case NF_DROP:
		case NF_QUEUE:
			break;
		default:
			return -EINVAL;
		}
		/* fall through */
	case NFT_CONTINUE:
	case NFT_BREAK:
	case NFT_RETURN:
		break;
	case NFT_JUMP:
	case NFT_GOTO:
		if (!tb[NFTA_VERDICT_CHAIN])
			return -EINVAL;
		chain = nf_tables_chain_lookup(ctx->table,
					       tb[NFTA_VERDICT_CHAIN], genmask);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
		if (chain->flags & NFT_BASE_CHAIN)
			return -EOPNOTSUPP;

		chain->use++;
		data->verdict.chain = chain;
		break;
	}

	desc->len = sizeof(data->verdict);
	desc->type = NFT_DATA_VERDICT;
	return 0;
}

static void nft_verdict_uninit(const struct nft_data *data)
{
	switch (data->verdict.code) {
	case NFT_JUMP:
	case NFT_GOTO:
		data->verdict.chain->use--;
		break;
	}
}

int nft_verdict_dump(struct sk_buff *skb, int type, const struct nft_verdict *v)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, type);
	if (!nest)
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_VERDICT_CODE, htonl(v->code)))
		goto nla_put_failure;

	switch (v->code) {
	case NFT_JUMP:
	case NFT_GOTO:
		if (nla_put_string(skb, NFTA_VERDICT_CHAIN,
				   v->chain->name))
			goto nla_put_failure;
	}
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

static int nft_value_init(const struct nft_ctx *ctx,
			  struct nft_data *data, unsigned int size,
			  struct nft_data_desc *desc, const struct nlattr *nla)
{
	unsigned int len;

	len = nla_len(nla);
	if (len == 0)
		return -EINVAL;
	if (len > size)
		return -EOVERFLOW;

	nla_memcpy(data->data, nla, len);
	desc->type = NFT_DATA_VALUE;
	desc->len  = len;
	return 0;
}

static int nft_value_dump(struct sk_buff *skb, const struct nft_data *data,
			  unsigned int len)
{
	return nla_put(skb, NFTA_DATA_VALUE, len, data->data);
}

static const struct nla_policy nft_data_policy[NFTA_DATA_MAX + 1] = {
	[NFTA_DATA_VALUE]	= { .type = NLA_BINARY },
	[NFTA_DATA_VERDICT]	= { .type = NLA_NESTED },
};

/**
 *	nft_data_init - parse nf_tables data netlink attributes
 *
 *	@ctx: context of the expression using the data
 *	@data: destination struct nft_data
 *	@size: maximum data length
 *	@desc: data description
 *	@nla: netlink attribute containing data
 *
 *	Parse the netlink data attributes and initialize a struct nft_data.
 *	The type and length of data are returned in the data description.
 *
 *	The caller can indicate that it only wants to accept data of type
 *	NFT_DATA_VALUE by passing NULL for the ctx argument.
 */
int nft_data_init(const struct nft_ctx *ctx,
		  struct nft_data *data, unsigned int size,
		  struct nft_data_desc *desc, const struct nlattr *nla)
{
	struct nlattr *tb[NFTA_DATA_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_DATA_MAX, nla, nft_data_policy);
	if (err < 0)
		return err;

	if (tb[NFTA_DATA_VALUE])
		return nft_value_init(ctx, data, size, desc,
				      tb[NFTA_DATA_VALUE]);
	if (tb[NFTA_DATA_VERDICT] && ctx != NULL)
		return nft_verdict_init(ctx, data, desc, tb[NFTA_DATA_VERDICT]);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(nft_data_init);

/**
 *	nft_data_uninit - release a nft_data item
 *
 *	@data: struct nft_data to release
 *	@type: type of data
 *
 *	Release a nft_data item. NFT_DATA_VALUE types can be silently discarded,
 *	all others need to be released by calling this function.
 */
void nft_data_uninit(const struct nft_data *data, enum nft_data_types type)
{
	if (type < NFT_DATA_VERDICT)
		return;
	switch (type) {
	case NFT_DATA_VERDICT:
		return nft_verdict_uninit(data);
	default:
		WARN_ON(1);
	}
}
EXPORT_SYMBOL_GPL(nft_data_uninit);

int nft_data_dump(struct sk_buff *skb, int attr, const struct nft_data *data,
		  enum nft_data_types type, unsigned int len)
{
	struct nlattr *nest;
	int err;

	nest = nla_nest_start(skb, attr);
	if (nest == NULL)
		return -1;

	switch (type) {
	case NFT_DATA_VALUE:
		err = nft_value_dump(skb, data, len);
		break;
	case NFT_DATA_VERDICT:
		err = nft_verdict_dump(skb, NFTA_DATA_VERDICT, &data->verdict);
		break;
	default:
		err = -EINVAL;
		WARN_ON(1);
	}

	nla_nest_end(skb, nest);
	return err;
}
EXPORT_SYMBOL_GPL(nft_data_dump);

static int __net_init nf_tables_init_net(struct net *net)
{
	INIT_LIST_HEAD(&net->nft.af_info);
	INIT_LIST_HEAD(&net->nft.commit_list);
	net->nft.base_seq = 1;
	return 0;
}

int __nft_release_basechain(struct nft_ctx *ctx)
{
	struct nft_rule *rule, *nr;

	BUG_ON(!(ctx->chain->flags & NFT_BASE_CHAIN));

	nf_tables_unregister_hooks(ctx->net, ctx->chain->table, ctx->chain,
				   ctx->afi->nops);
	list_for_each_entry_safe(rule, nr, &ctx->chain->rules, list) {
		list_del(&rule->list);
		ctx->chain->use--;
		nf_tables_rule_destroy(ctx, rule);
	}
	list_del(&ctx->chain->list);
	ctx->table->use--;
	nf_tables_chain_destroy(ctx->chain);

	return 0;
}
EXPORT_SYMBOL_GPL(__nft_release_basechain);

/* Called by nft_unregister_afinfo() from __net_exit path, nfnl_lock is held. */
static void __nft_release_afinfo(struct net *net, struct nft_af_info *afi)
{
	struct nft_table *table, *nt;
	struct nft_chain *chain, *nc;
	struct nft_object *obj, *ne;
	struct nft_rule *rule, *nr;
	struct nft_set *set, *ns;
	struct nft_ctx ctx = {
		.net	= net,
		.afi	= afi,
	};

	list_for_each_entry_safe(table, nt, &afi->tables, list) {
		list_for_each_entry(chain, &table->chains, list)
			nf_tables_unregister_hooks(net, table, chain,
						   afi->nops);
		/* No packets are walking on these chains anymore. */
		ctx.table = table;
		list_for_each_entry(chain, &table->chains, list) {
			ctx.chain = chain;
			list_for_each_entry_safe(rule, nr, &chain->rules, list) {
				list_del(&rule->list);
				chain->use--;
				nf_tables_rule_destroy(&ctx, rule);
			}
		}
		list_for_each_entry_safe(set, ns, &table->sets, list) {
			list_del(&set->list);
			table->use--;
			nft_set_destroy(set);
		}
		list_for_each_entry_safe(obj, ne, &table->objects, list) {
			list_del(&obj->list);
			table->use--;
			nft_obj_destroy(obj);
		}
		list_for_each_entry_safe(chain, nc, &table->chains, list) {
			list_del(&chain->list);
			table->use--;
			nf_tables_chain_destroy(chain);
		}
		list_del(&table->list);
		nf_tables_table_destroy(&ctx);
	}
}

static struct pernet_operations nf_tables_net_ops = {
	.init	= nf_tables_init_net,
};

static int __init nf_tables_module_init(void)
{
	int err;

	info = kmalloc(sizeof(struct nft_expr_info) * NFT_RULE_MAXEXPRS,
		       GFP_KERNEL);
	if (info == NULL) {
		err = -ENOMEM;
		goto err1;
	}

	err = nf_tables_core_module_init();
	if (err < 0)
		goto err2;

	err = nfnetlink_subsys_register(&nf_tables_subsys);
	if (err < 0)
		goto err3;

	pr_info("nf_tables: (c) 2007-2009 Patrick McHardy <kaber@trash.net>\n");
	return register_pernet_subsys(&nf_tables_net_ops);
err3:
	nf_tables_core_module_exit();
err2:
	kfree(info);
err1:
	return err;
}

static void __exit nf_tables_module_exit(void)
{
	unregister_pernet_subsys(&nf_tables_net_ops);
	nfnetlink_subsys_unregister(&nf_tables_subsys);
	rcu_barrier();
	nf_tables_core_module_exit();
	kfree(info);
}

module_init(nf_tables_module_init);
module_exit(nf_tables_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_NFTABLES);
