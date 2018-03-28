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
#include <linux/vmalloc.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/net_namespace.h>
#include <net/sock.h>

static LIST_HEAD(nf_tables_expressions);
static LIST_HEAD(nf_tables_objects);
static LIST_HEAD(nf_tables_flowtables);
static u64 table_handle;

static void nft_ctx_init(struct nft_ctx *ctx,
			 struct net *net,
			 const struct sk_buff *skb,
			 const struct nlmsghdr *nlh,
			 u8 family,
			 struct nft_table *table,
			 struct nft_chain *chain,
			 const struct nlattr * const *nla)
{
	ctx->net	= net;
	ctx->family	= family;
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

static int nf_tables_register_hook(struct net *net,
				   const struct nft_table *table,
				   struct nft_chain *chain)
{
	if (table->flags & NFT_TABLE_F_DORMANT ||
	    !nft_is_base_chain(chain))
		return 0;

	return nf_register_net_hook(net, &nft_base_chain(chain)->ops);
}

static void nf_tables_unregister_hook(struct net *net,
				      const struct nft_table *table,
				      struct nft_chain *chain)
{
	if (table->flags & NFT_TABLE_F_DORMANT ||
	    !nft_is_base_chain(chain))
		return;

	nf_unregister_net_hook(net, &nft_base_chain(chain)->ops);
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

	if (msg_type == NFT_MSG_NEWRULE && ctx->nla[NFTA_RULE_ID] != NULL) {
		nft_trans_rule_id(trans) =
			ntohl(nla_get_be32(ctx->nla[NFTA_RULE_ID]));
	}
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

static int nft_trans_flowtable_add(struct nft_ctx *ctx, int msg_type,
				   struct nft_flowtable *flowtable)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type,
				sizeof(struct nft_trans_flowtable));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWFLOWTABLE)
		nft_activate_next(ctx->net, flowtable);

	nft_trans_flowtable(trans) = flowtable;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
}

static int nft_delflowtable(struct nft_ctx *ctx,
			    struct nft_flowtable *flowtable)
{
	int err;

	err = nft_trans_flowtable_add(ctx, NFT_MSG_DELFLOWTABLE, flowtable);
	if (err < 0)
		return err;

	nft_deactivate_next(ctx->net, flowtable);
	ctx->table->use--;

	return err;
}

/*
 * Tables
 */

static struct nft_table *nft_table_lookup(const struct net *net,
					  const struct nlattr *nla,
					  u8 family, u8 genmask)
{
	struct nft_table *table;

	list_for_each_entry(table, &net->nft.tables, list) {
		if (!nla_strcmp(nla, table->name) &&
		    table->family == family &&
		    nft_active_genmask(table, genmask))
			return table;
	}
	return NULL;
}

static struct nft_table *nft_table_lookup_byhandle(const struct net *net,
						   const struct nlattr *nla,
						   u8 genmask)
{
	struct nft_table *table;

	list_for_each_entry(table, &net->nft.tables, list) {
		if (be64_to_cpu(nla_get_be64(nla)) == table->handle &&
		    nft_active_genmask(table, genmask))
			return table;
	}
	return NULL;
}

static struct nft_table *nf_tables_table_lookup(const struct net *net,
						const struct nlattr *nla,
						u8 family, u8 genmask)
{
	struct nft_table *table;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	table = nft_table_lookup(net, nla, family, genmask);
	if (table != NULL)
		return table;

	return ERR_PTR(-ENOENT);
}

static struct nft_table *nf_tables_table_lookup_byhandle(const struct net *net,
							 const struct nlattr *nla,
							 u8 genmask)
{
	struct nft_table *table;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	table = nft_table_lookup_byhandle(net, nla, genmask);
	if (table != NULL)
		return table;

	return ERR_PTR(-ENOENT);
}

static inline u64 nf_tables_alloc_handle(struct nft_table *table)
{
	return ++table->hgenerator;
}

static const struct nft_chain_type *chain_type[NFPROTO_NUMPROTO][NFT_CHAIN_T_MAX];

static const struct nft_chain_type *
__nf_tables_chain_type_lookup(const struct nlattr *nla, u8 family)
{
	int i;

	for (i = 0; i < NFT_CHAIN_T_MAX; i++) {
		if (chain_type[family][i] != NULL &&
		    !nla_strcmp(nla, chain_type[family][i]->name))
			return chain_type[family][i];
	}
	return NULL;
}

static const struct nft_chain_type *
nf_tables_chain_type_lookup(const struct nlattr *nla, u8 family, bool autoload)
{
	const struct nft_chain_type *type;

	type = __nf_tables_chain_type_lookup(nla, family);
	if (type != NULL)
		return type;
#ifdef CONFIG_MODULES
	if (autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-chain-%u-%.*s", family,
			       nla_len(nla), (const char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		type = __nf_tables_chain_type_lookup(nla, family);
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
	[NFTA_TABLE_HANDLE]	= { .type = NLA_U64 },
};

static int nf_tables_fill_table_info(struct sk_buff *skb, struct net *net,
				     u32 portid, u32 seq, int event, u32 flags,
				     int family, const struct nft_table *table)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_TABLE_NAME, table->name) ||
	    nla_put_be32(skb, NFTA_TABLE_FLAGS, htonl(table->flags)) ||
	    nla_put_be32(skb, NFTA_TABLE_USE, htonl(table->use)) ||
	    nla_put_be64(skb, NFTA_TABLE_HANDLE, cpu_to_be64(table->handle),
			 NFTA_TABLE_PAD))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static void nf_tables_table_notify(const struct nft_ctx *ctx, int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_table_info(skb, ctx->net, ctx->portid, ctx->seq,
					event, 0, ctx->family, ctx->table);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
		       ctx->report, GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES, -ENOBUFS);
}

static int nf_tables_dump_tables(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_table *table;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (family != NFPROTO_UNSPEC && family != table->family)
			continue;

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
					      NFT_MSG_NEWTABLE, NLM_F_MULTI,
					      table->family, table) < 0)
			goto done;

		nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
		idx++;
	}
done:
	rcu_read_unlock();
	cb->args[0] = idx;
	return skb->len;
}

static int nf_tables_gettable(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
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

	table = nf_tables_table_lookup(net, nla[NFTA_TABLE_NAME], family,
				       genmask);
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

static void nft_table_disable(struct net *net, struct nft_table *table, u32 cnt)
{
	struct nft_chain *chain;
	u32 i = 0;

	list_for_each_entry(chain, &table->chains, list) {
		if (!nft_is_active_next(net, chain))
			continue;
		if (!nft_is_base_chain(chain))
			continue;

		if (cnt && i++ == cnt)
			break;

		nf_unregister_net_hook(net, &nft_base_chain(chain)->ops);
	}
}

static int nf_tables_table_enable(struct net *net, struct nft_table *table)
{
	struct nft_chain *chain;
	int err, i = 0;

	list_for_each_entry(chain, &table->chains, list) {
		if (!nft_is_active_next(net, chain))
			continue;
		if (!nft_is_base_chain(chain))
			continue;

		err = nf_register_net_hook(net, &nft_base_chain(chain)->ops);
		if (err < 0)
			goto err;

		i++;
	}
	return 0;
err:
	if (i)
		nft_table_disable(net, table, i);
	return err;
}

static void nf_tables_table_disable(struct net *net, struct nft_table *table)
{
	nft_table_disable(net, table, 0);
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
		ret = nf_tables_table_enable(ctx->net, ctx->table);
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
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	const struct nlattr *name;
	struct nft_table *table;
	int family = nfmsg->nfgen_family;
	u32 flags = 0;
	struct nft_ctx ctx;
	int err;

	name = nla[NFTA_TABLE_NAME];
	table = nf_tables_table_lookup(net, name, family, genmask);
	if (IS_ERR(table)) {
		if (PTR_ERR(table) != -ENOENT)
			return PTR_ERR(table);
	} else {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);
		return nf_tables_updtable(&ctx);
	}

	if (nla[NFTA_TABLE_FLAGS]) {
		flags = ntohl(nla_get_be32(nla[NFTA_TABLE_FLAGS]));
		if (flags & ~NFT_TABLE_F_DORMANT)
			return -EINVAL;
	}

	err = -ENOMEM;
	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (table == NULL)
		goto err_kzalloc;

	table->name = nla_strdup(name, GFP_KERNEL);
	if (table->name == NULL)
		goto err_strdup;

	INIT_LIST_HEAD(&table->chains);
	INIT_LIST_HEAD(&table->sets);
	INIT_LIST_HEAD(&table->objects);
	INIT_LIST_HEAD(&table->flowtables);
	table->family = family;
	table->flags = flags;
	table->handle = ++table_handle;

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);
	err = nft_trans_table_add(&ctx, NFT_MSG_NEWTABLE);
	if (err < 0)
		goto err_trans;

	list_add_tail_rcu(&table->list, &net->nft.tables);
	return 0;
err_trans:
	kfree(table->name);
err_strdup:
	kfree(table);
err_kzalloc:
	return err;
}

static int nft_flush_table(struct nft_ctx *ctx)
{
	struct nft_flowtable *flowtable, *nft;
	struct nft_chain *chain, *nc;
	struct nft_object *obj, *ne;
	struct nft_set *set, *ns;
	int err;

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

		if (nft_set_is_anonymous(set) &&
		    !list_empty(&set->bindings))
			continue;

		err = nft_delset(ctx, set);
		if (err < 0)
			goto out;
	}

	list_for_each_entry_safe(flowtable, nft, &ctx->table->flowtables, list) {
		err = nft_delflowtable(ctx, flowtable);
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
	struct nft_table *table, *nt;
	const struct nlattr * const *nla = ctx->nla;
	int err = 0;

	list_for_each_entry_safe(table, nt, &ctx->net->nft.tables, list) {
		if (family != AF_UNSPEC && table->family != family)
			continue;

		ctx->family = table->family;

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
out:
	return err;
}

static int nf_tables_deltable(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_table *table;
	int family = nfmsg->nfgen_family;
	struct nft_ctx ctx;

	nft_ctx_init(&ctx, net, skb, nlh, 0, NULL, NULL, nla);
	if (family == AF_UNSPEC ||
	    (!nla[NFTA_TABLE_NAME] && !nla[NFTA_TABLE_HANDLE]))
		return nft_flush(&ctx, family);

	if (nla[NFTA_TABLE_HANDLE])
		table = nf_tables_table_lookup_byhandle(net,
							nla[NFTA_TABLE_HANDLE],
							genmask);
	else
		table = nf_tables_table_lookup(net, nla[NFTA_TABLE_NAME],
					       family, genmask);

	if (IS_ERR(table))
		return PTR_ERR(table);

	if (nlh->nlmsg_flags & NLM_F_NONREC &&
	    table->use > 0)
		return -EBUSY;

	ctx.family = family;
	ctx.table = table;

	return nft_flush_table(&ctx);
}

static void nf_tables_table_destroy(struct nft_ctx *ctx)
{
	BUG_ON(ctx->table->use > 0);

	kfree(ctx->table->name);
	kfree(ctx->table);
}

void nft_register_chain_type(const struct nft_chain_type *ctype)
{
	if (WARN_ON(ctype->family >= NFPROTO_NUMPROTO))
		return;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	if (WARN_ON(chain_type[ctype->family][ctype->type] != NULL)) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		return;
	}
	chain_type[ctype->family][ctype->type] = ctype;
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_register_chain_type);

void nft_unregister_chain_type(const struct nft_chain_type *ctype)
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

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
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

	if (nft_is_base_chain(chain)) {
		const struct nft_base_chain *basechain = nft_base_chain(chain);
		const struct nf_hook_ops *ops = &basechain->ops;
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

		if (basechain->stats && nft_dump_stats(skb, basechain->stats))
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

static void nf_tables_chain_notify(const struct nft_ctx *ctx, int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_chain_info(skb, ctx->net, ctx->portid, ctx->seq,
					event, 0, ctx->family, ctx->table,
					ctx->chain);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
		       ctx->report, GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES, -ENOBUFS);
}

static int nf_tables_dump_chains(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_table *table;
	const struct nft_chain *chain;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (family != NFPROTO_UNSPEC && family != table->family)
			continue;

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
						      table->family, table,
						      chain) < 0)
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

static int nf_tables_getchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
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

	table = nf_tables_table_lookup(net, nla[NFTA_CHAIN_TABLE], family,
				       genmask);
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

	err = nla_parse_nested(tb, NFTA_COUNTER_MAX, attr, nft_counter_policy,
			       NULL);
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
	struct nft_stats __percpu *oldstats;

	if (newstats == NULL)
		return;

	if (chain->stats) {
		oldstats = nfnl_dereference(chain->stats, NFNL_SUBSYS_NFTABLES);
		rcu_assign_pointer(chain->stats, newstats);
		synchronize_rcu();
		free_percpu(oldstats);
	} else
		rcu_assign_pointer(chain->stats, newstats);
}

static void nf_tables_chain_destroy(struct nft_ctx *ctx)
{
	struct nft_chain *chain = ctx->chain;

	BUG_ON(chain->use > 0);

	if (nft_is_base_chain(chain)) {
		struct nft_base_chain *basechain = nft_base_chain(chain);

		if (basechain->type->free)
			basechain->type->free(ctx);
		module_put(basechain->type->owner);
		free_percpu(basechain->stats);
		if (basechain->stats)
			static_branch_dec(&nft_counters_enabled);
		if (basechain->ops.dev != NULL)
			dev_put(basechain->ops.dev);
		kfree(chain->name);
		kfree(basechain);
	} else {
		kfree(chain->name);
		kfree(chain);
	}
}

struct nft_chain_hook {
	u32				num;
	s32				priority;
	const struct nft_chain_type	*type;
	struct net_device		*dev;
};

static int nft_chain_parse_hook(struct net *net,
				const struct nlattr * const nla[],
				struct nft_chain_hook *hook, u8 family,
				bool create)
{
	struct nlattr *ha[NFTA_HOOK_MAX + 1];
	const struct nft_chain_type *type;
	struct net_device *dev;
	int err;

	err = nla_parse_nested(ha, NFTA_HOOK_MAX, nla[NFTA_CHAIN_HOOK],
			       nft_hook_policy, NULL);
	if (err < 0)
		return err;

	if (ha[NFTA_HOOK_HOOKNUM] == NULL ||
	    ha[NFTA_HOOK_PRIORITY] == NULL)
		return -EINVAL;

	hook->num = ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM]));
	hook->priority = ntohl(nla_get_be32(ha[NFTA_HOOK_PRIORITY]));

	type = chain_type[family][NFT_CHAIN_T_DEFAULT];
	if (nla[NFTA_CHAIN_TYPE]) {
		type = nf_tables_chain_type_lookup(nla[NFTA_CHAIN_TYPE],
						   family, create);
		if (IS_ERR(type))
			return PTR_ERR(type);
	}
	if (!(type->hook_mask & (1 << hook->num)))
		return -EOPNOTSUPP;

	if (type->type == NFT_CHAIN_T_NAT &&
	    hook->priority <= NF_IP_PRI_CONNTRACK)
		return -EOPNOTSUPP;

	if (!try_module_get(type->owner))
		return -ENOENT;

	hook->type = type;

	hook->dev = NULL;
	if (family == NFPROTO_NETDEV) {
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

static int nf_tables_addchain(struct nft_ctx *ctx, u8 family, u8 genmask,
			      u8 policy, bool create)
{
	const struct nlattr * const *nla = ctx->nla;
	struct nft_table *table = ctx->table;
	struct nft_base_chain *basechain;
	struct nft_stats __percpu *stats;
	struct net *net = ctx->net;
	struct nft_chain *chain;
	int err;

	if (table->use == UINT_MAX)
		return -EOVERFLOW;

	if (nla[NFTA_CHAIN_HOOK]) {
		struct nft_chain_hook hook;
		struct nf_hook_ops *ops;

		err = nft_chain_parse_hook(net, nla, &hook, family, create);
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
			static_branch_inc(&nft_counters_enabled);
		}

		basechain->type = hook.type;
		if (basechain->type->init)
			basechain->type->init(ctx);

		chain = &basechain->chain;

		ops		= &basechain->ops;
		ops->pf		= family;
		ops->hooknum	= hook.num;
		ops->priority	= hook.priority;
		ops->priv	= chain;
		ops->hook	= hook.type->hooks[ops->hooknum];
		ops->dev	= hook.dev;

		if (basechain->type->type == NFT_CHAIN_T_NAT)
			ops->nat_hook = true;

		chain->flags |= NFT_BASE_CHAIN;
		basechain->policy = policy;
	} else {
		chain = kzalloc(sizeof(*chain), GFP_KERNEL);
		if (chain == NULL)
			return -ENOMEM;
	}
	ctx->chain = chain;

	INIT_LIST_HEAD(&chain->rules);
	chain->handle = nf_tables_alloc_handle(table);
	chain->table = table;
	chain->name = nla_strdup(nla[NFTA_CHAIN_NAME], GFP_KERNEL);
	if (!chain->name) {
		err = -ENOMEM;
		goto err1;
	}

	err = nf_tables_register_hook(net, table, chain);
	if (err < 0)
		goto err1;

	err = nft_trans_chain_add(ctx, NFT_MSG_NEWCHAIN);
	if (err < 0)
		goto err2;

	table->use++;
	list_add_tail_rcu(&chain->list, &table->chains);

	return 0;
err2:
	nf_tables_unregister_hook(net, table, chain);
err1:
	nf_tables_chain_destroy(ctx);

	return err;
}

static int nf_tables_updchain(struct nft_ctx *ctx, u8 genmask, u8 policy,
			      bool create)
{
	const struct nlattr * const *nla = ctx->nla;
	struct nft_table *table = ctx->table;
	struct nft_chain *chain = ctx->chain;
	struct nft_base_chain *basechain;
	struct nft_stats *stats = NULL;
	struct nft_chain_hook hook;
	const struct nlattr *name;
	struct nf_hook_ops *ops;
	struct nft_trans *trans;
	int err;

	if (nla[NFTA_CHAIN_HOOK]) {
		if (!nft_is_base_chain(chain))
			return -EBUSY;

		err = nft_chain_parse_hook(ctx->net, nla, &hook, ctx->family,
					   create);
		if (err < 0)
			return err;

		basechain = nft_base_chain(chain);
		if (basechain->type != hook.type) {
			nft_chain_release_hook(&hook);
			return -EBUSY;
		}

		ops = &basechain->ops;
		if (ops->hooknum != hook.num ||
		    ops->priority != hook.priority ||
		    ops->dev != hook.dev) {
			nft_chain_release_hook(&hook);
			return -EBUSY;
		}
		nft_chain_release_hook(&hook);
	}

	if (nla[NFTA_CHAIN_HANDLE] &&
	    nla[NFTA_CHAIN_NAME]) {
		struct nft_chain *chain2;

		chain2 = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME],
						genmask);
		if (!IS_ERR(chain2))
			return -EEXIST;
	}

	if (nla[NFTA_CHAIN_COUNTERS]) {
		if (!nft_is_base_chain(chain))
			return -EOPNOTSUPP;

		stats = nft_stats_alloc(nla[NFTA_CHAIN_COUNTERS]);
		if (IS_ERR(stats))
			return PTR_ERR(stats);
	}

	trans = nft_trans_alloc(ctx, NFT_MSG_NEWCHAIN,
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

	name = nla[NFTA_CHAIN_NAME];
	if (nla[NFTA_CHAIN_HANDLE] && name) {
		nft_trans_chain_name(trans) =
			nla_strdup(name, GFP_KERNEL);
		if (!nft_trans_chain_name(trans)) {
			kfree(trans);
			free_percpu(stats);
			return -ENOMEM;
		}
	}
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
}

static int nf_tables_newchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr * uninitialized_var(name);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_table *table;
	struct nft_chain *chain;
	u8 policy = NF_ACCEPT;
	struct nft_ctx ctx;
	u64 handle = 0;
	bool create;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	table = nf_tables_table_lookup(net, nla[NFTA_CHAIN_TABLE], family,
				       genmask);
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
		if (chain != NULL &&
		    !nft_is_base_chain(chain))
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

	nft_ctx_init(&ctx, net, skb, nlh, family, table, chain, nla);

	if (chain != NULL) {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		return nf_tables_updchain(&ctx, genmask, policy, create);
	}

	return nf_tables_addchain(&ctx, family, genmask, policy, create);
}

static int nf_tables_delchain(struct net *net, struct sock *nlsk,
			      struct sk_buff *skb, const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule;
	int family = nfmsg->nfgen_family;
	struct nft_ctx ctx;
	u64 handle;
	u32 use;
	int err;

	table = nf_tables_table_lookup(net, nla[NFTA_CHAIN_TABLE], family,
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (nla[NFTA_CHAIN_HANDLE]) {
		handle = be64_to_cpu(nla_get_be64(nla[NFTA_CHAIN_HANDLE]));
		chain = nf_tables_chain_lookup_byhandle(table, handle, genmask);
	} else {
		chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME], genmask);
	}
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	if (nlh->nlmsg_flags & NLM_F_NONREC &&
	    chain->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, net, skb, nlh, family, table, chain, nla);

	use = chain->use;
	list_for_each_entry(rule, &chain->rules, list) {
		if (!nft_is_active_next(net, rule))
			continue;
		use--;

		err = nft_delrule(&ctx, rule);
		if (err < 0)
			return err;
	}

	/* There are rules and elements that are still holding references to us,
	 * we cannot do a recursive removal in this case.
	 */
	if (use > 0)
		return -EBUSY;

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

	err = nla_parse_nested(tb, NFTA_EXPR_MAX, nla, nft_expr_policy, NULL);
	if (err < 0)
		return err;

	type = nft_expr_type_get(ctx->family, tb[NFTA_EXPR_NAME]);
	if (IS_ERR(type))
		return PTR_ERR(type);

	if (tb[NFTA_EXPR_DATA]) {
		err = nla_parse_nested(info->tb, type->maxattr,
				       tb[NFTA_EXPR_DATA], type->policy, NULL);
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

	if (ops->validate) {
		const struct nft_data *data = NULL;

		err = ops->validate(ctx, expr, &data);
		if (err < 0)
			goto err2;
	}

	return 0;

err2:
	if (ops->destroy)
		ops->destroy(ctx, expr);
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
	u16 type = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(struct nfgenmsg), flags);
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
		prule = list_prev_entry(rule, list);
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

static void nf_tables_rule_notify(const struct nft_ctx *ctx,
				  const struct nft_rule *rule, int event)
{
	struct sk_buff *skb;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_rule_info(skb, ctx->net, ctx->portid, ctx->seq,
				       event, 0, ctx->family, ctx->table,
				       ctx->chain, rule);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
		       ctx->report, GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES, -ENOBUFS);
}

struct nft_rule_dump_ctx {
	char *table;
	char *chain;
};

static int nf_tables_dump_rules(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_rule_dump_ctx *ctx = cb->data;
	const struct nft_table *table;
	const struct nft_chain *chain;
	const struct nft_rule *rule;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (family != NFPROTO_UNSPEC && family != table->family)
			continue;

		if (ctx && ctx->table && strcmp(ctx->table, table->name) != 0)
			continue;

		list_for_each_entry_rcu(chain, &table->chains, list) {
			if (ctx && ctx->chain &&
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
							      table->family,
							      table, chain, rule) < 0)
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

static int nf_tables_dump_rules_done(struct netlink_callback *cb)
{
	struct nft_rule_dump_ctx *ctx = cb->data;

	if (ctx) {
		kfree(ctx->table);
		kfree(ctx->chain);
		kfree(ctx);
	}
	return 0;
}

static int nf_tables_getrule(struct net *net, struct sock *nlsk,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[],
			     struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
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

			if (nla[NFTA_RULE_TABLE]) {
				ctx->table = nla_strdup(nla[NFTA_RULE_TABLE],
							GFP_KERNEL);
				if (!ctx->table) {
					kfree(ctx);
					return -ENOMEM;
				}
			}
			if (nla[NFTA_RULE_CHAIN]) {
				ctx->chain = nla_strdup(nla[NFTA_RULE_CHAIN],
							GFP_KERNEL);
				if (!ctx->chain) {
					kfree(ctx->table);
					kfree(ctx);
					return -ENOMEM;
				}
			}
			c.data = ctx;
		}

		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	table = nf_tables_table_lookup(net, nla[NFTA_RULE_TABLE], family,
				       genmask);
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
			     const struct nlattr * const nla[],
			     struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
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

	table = nf_tables_table_lookup(net, nla[NFTA_RULE_TABLE], family,
				       genmask);
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

	nft_ctx_init(&ctx, net, skb, nlh, family, table, chain, nla);

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

static struct nft_rule *nft_rule_lookup_byid(const struct net *net,
					     const struct nlattr *nla)
{
	u32 id = ntohl(nla_get_be32(nla));
	struct nft_trans *trans;

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		struct nft_rule *rule = nft_trans_rule(trans);

		if (trans->msg_type == NFT_MSG_NEWRULE &&
		    id == nft_trans_rule_id(trans))
			return rule;
	}
	return ERR_PTR(-ENOENT);
}

static int nf_tables_delrule(struct net *net, struct sock *nlsk,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[],
			     struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	struct nft_table *table;
	struct nft_chain *chain = NULL;
	struct nft_rule *rule;
	int family = nfmsg->nfgen_family, err = 0;
	struct nft_ctx ctx;

	table = nf_tables_table_lookup(net, nla[NFTA_RULE_TABLE], family,
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (nla[NFTA_RULE_CHAIN]) {
		chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN],
					       genmask);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
	}

	nft_ctx_init(&ctx, net, skb, nlh, family, table, chain, nla);

	if (chain) {
		if (nla[NFTA_RULE_HANDLE]) {
			rule = nf_tables_rule_lookup(chain,
						     nla[NFTA_RULE_HANDLE]);
			if (IS_ERR(rule))
				return PTR_ERR(rule);

			err = nft_delrule(&ctx, rule);
		} else if (nla[NFTA_RULE_ID]) {
			rule = nft_rule_lookup_byid(net, nla[NFTA_RULE_ID]);
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

static LIST_HEAD(nf_tables_set_types);

int nft_register_set(struct nft_set_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail_rcu(&type->list, &nf_tables_set_types);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_set);

void nft_unregister_set(struct nft_set_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del_rcu(&type->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_set);

#define NFT_SET_FEATURES	(NFT_SET_INTERVAL | NFT_SET_MAP | \
				 NFT_SET_TIMEOUT | NFT_SET_OBJECT)

static bool nft_set_ops_candidate(const struct nft_set_ops *ops, u32 flags)
{
	return (flags & ops->features) == (flags & NFT_SET_FEATURES);
}

/*
 * Select a set implementation based on the data characteristics and the
 * given policy. The total memory use might not be known if no size is
 * given, in that case the amount of memory per element is used.
 */
static const struct nft_set_ops *
nft_select_set_ops(const struct nft_ctx *ctx,
		   const struct nlattr * const nla[],
		   const struct nft_set_desc *desc,
		   enum nft_set_policies policy)
{
	const struct nft_set_ops *ops, *bops;
	struct nft_set_estimate est, best;
	const struct nft_set_type *type;
	u32 flags = 0;

#ifdef CONFIG_MODULES
	if (list_empty(&nf_tables_set_types)) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-set");
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (!list_empty(&nf_tables_set_types))
			return ERR_PTR(-EAGAIN);
	}
#endif
	if (nla[NFTA_SET_FLAGS] != NULL)
		flags = ntohl(nla_get_be32(nla[NFTA_SET_FLAGS]));

	bops	    = NULL;
	best.size   = ~0;
	best.lookup = ~0;
	best.space  = ~0;

	list_for_each_entry(type, &nf_tables_set_types, list) {
		if (!type->select_ops)
			ops = type->ops;
		else
			ops = type->select_ops(ctx, desc, flags);
		if (!ops)
			continue;

		if (!nft_set_ops_candidate(ops, flags))
			continue;
		if (!ops->estimate(desc, flags, &est))
			continue;

		switch (policy) {
		case NFT_SET_POL_PERFORMANCE:
			if (est.lookup < best.lookup)
				break;
			if (est.lookup == best.lookup &&
			    est.space < best.space)
				break;
			continue;
		case NFT_SET_POL_MEMORY:
			if (!desc->size) {
				if (est.space < best.space)
					break;
				if (est.space == best.space &&
				    est.lookup < best.lookup)
					break;
			} else if (est.size < best.size) {
				break;
			}
			continue;
		default:
			break;
		}

		if (!try_module_get(type->owner))
			continue;
		if (bops != NULL)
			module_put(bops->type->owner);

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
	[NFTA_SET_HANDLE]		= { .type = NLA_U64 },
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
	int family = nfmsg->nfgen_family;
	struct nft_table *table = NULL;

	if (nla[NFTA_SET_TABLE] != NULL) {
		table = nf_tables_table_lookup(net, nla[NFTA_SET_TABLE],
					       family, genmask);
		if (IS_ERR(table))
			return PTR_ERR(table);
	}

	nft_ctx_init(ctx, net, skb, nlh, family, table, NULL, nla);
	return 0;
}

static struct nft_set *nf_tables_set_lookup(const struct nft_table *table,
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

static struct nft_set *nf_tables_set_lookup_byhandle(const struct nft_table *table,
						     const struct nlattr *nla, u8 genmask)
{
	struct nft_set *set;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(set, &table->sets, list) {
		if (be64_to_cpu(nla_get_be64(nla)) == set->handle &&
		    nft_active_genmask(set, genmask))
			return set;
	}
	return ERR_PTR(-ENOENT);
}

static struct nft_set *nf_tables_set_lookup_byid(const struct net *net,
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

struct nft_set *nft_set_lookup_global(const struct net *net,
				      const struct nft_table *table,
				      const struct nlattr *nla_set_name,
				      const struct nlattr *nla_set_id,
				      u8 genmask)
{
	struct nft_set *set;

	set = nf_tables_set_lookup(table, nla_set_name, genmask);
	if (IS_ERR(set)) {
		if (!nla_set_id)
			return set;

		set = nf_tables_set_lookup_byid(net, nla_set_id, genmask);
	}
	return set;
}
EXPORT_SYMBOL_GPL(nft_set_lookup_global);

static int nf_tables_set_alloc_name(struct nft_ctx *ctx, struct nft_set *set,
				    const char *name)
{
	const struct nft_set *i;
	const char *p;
	unsigned long *inuse;
	unsigned int n = 0, min = 0;

	p = strchr(name, '%');
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

	set->name = kasprintf(GFP_KERNEL, name, min + n);
	if (!set->name)
		return -ENOMEM;

	list_for_each_entry(i, &ctx->table->sets, list) {
		if (!nft_is_active_next(ctx->net, i))
			continue;
		if (!strcmp(set->name, i->name)) {
			kfree(set->name);
			return -ENFILE;
		}
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

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= ctx->family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(ctx->net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_SET_TABLE, ctx->table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_NAME, set->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_SET_HANDLE, cpu_to_be64(set->handle),
			 NFTA_SET_PAD))
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

static void nf_tables_set_notify(const struct nft_ctx *ctx,
				 const struct nft_set *set, int event,
			         gfp_t gfp_flags)
{
	struct sk_buff *skb;
	u32 portid = ctx->portid;
	int err;

	if (!ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, gfp_flags);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_set(skb, ctx, set, event, 0);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, ctx->net, portid, NFNLGRP_NFTABLES, ctx->report,
		       gfp_flags);
	return;
err:
	nfnetlink_set_err(ctx->net, portid, NFNLGRP_NFTABLES, -ENOBUFS);
}

static int nf_tables_dump_sets(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx, s_idx = cb->args[0];
	struct nft_table *table, *cur_table = (struct nft_table *)cb->args[2];
	struct net *net = sock_net(skb->sk);
	struct nft_ctx *ctx = cb->data, ctx_set;

	if (cb->args[1])
		return skb->len;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (ctx->family != NFPROTO_UNSPEC &&
		    ctx->family != table->family)
			continue;

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
			ctx_set.family = table->family;

			if (nf_tables_fill_set(skb, &ctx_set, set,
					       NFT_MSG_NEWSET,
					       NLM_F_MULTI) < 0) {
				cb->args[0] = idx;
				cb->args[2] = (unsigned long) table;
				goto done;
			}
			nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
			idx++;
		}
		if (s_idx)
			s_idx = 0;
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
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
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

	err = nla_parse_nested(da, NFTA_SET_DESC_MAX, nla,
			       nft_set_desc_policy, NULL);
	if (err < 0)
		return err;

	if (da[NFTA_SET_DESC_SIZE] != NULL)
		desc->size = ntohl(nla_get_be32(da[NFTA_SET_DESC_SIZE]));

	return 0;
}

static int nf_tables_newset(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	const struct nft_set_ops *ops;
	struct nft_table *table;
	struct nft_set *set;
	struct nft_ctx ctx;
	char *name;
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

	table = nf_tables_table_lookup(net, nla[NFTA_SET_TABLE], family,
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);

	set = nf_tables_set_lookup(table, nla[NFTA_SET_NAME], genmask);
	if (IS_ERR(set)) {
		if (PTR_ERR(set) != -ENOENT)
			return PTR_ERR(set);
	} else {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;
		return 0;
	}

	if (!(nlh->nlmsg_flags & NLM_F_CREATE))
		return -ENOENT;

	ops = nft_select_set_ops(&ctx, nla, &desc, policy);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	udlen = 0;
	if (nla[NFTA_SET_USERDATA])
		udlen = nla_len(nla[NFTA_SET_USERDATA]);

	size = 0;
	if (ops->privsize != NULL)
		size = ops->privsize(nla, &desc);

	set = kvzalloc(sizeof(*set) + size + udlen, GFP_KERNEL);
	if (!set) {
		err = -ENOMEM;
		goto err1;
	}

	name = nla_strdup(nla[NFTA_SET_NAME], GFP_KERNEL);
	if (!name) {
		err = -ENOMEM;
		goto err2;
	}

	err = nf_tables_set_alloc_name(&ctx, set, name);
	kfree(name);
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
	set->handle = nf_tables_alloc_handle(table);

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
	kvfree(set);
err1:
	module_put(ops->type->owner);
	return err;
}

static void nft_set_destroy(struct nft_set *set)
{
	set->ops->destroy(set);
	module_put(set->ops->type->owner);
	kfree(set->name);
	kvfree(set);
}

static void nf_tables_set_destroy(const struct nft_ctx *ctx, struct nft_set *set)
{
	list_del_rcu(&set->list);
	nf_tables_set_notify(ctx, set, NFT_MSG_DELSET, GFP_ATOMIC);
	nft_set_destroy(set);
}

static int nf_tables_delset(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
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

	if (nla[NFTA_SET_HANDLE])
		set = nf_tables_set_lookup_byhandle(ctx.table, nla[NFTA_SET_HANDLE], genmask);
	else
		set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME], genmask);
	if (IS_ERR(set))
		return PTR_ERR(set);

	if (!list_empty(&set->bindings) ||
	    (nlh->nlmsg_flags & NLM_F_NONREC && atomic_read(&set->nelems) > 0))
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

	if (!list_empty(&set->bindings) && nft_set_is_anonymous(set))
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

	if (list_empty(&set->bindings) && nft_set_is_anonymous(set) &&
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
	int family = nfmsg->nfgen_family;
	struct nft_table *table;

	table = nf_tables_table_lookup(net, nla[NFTA_SET_ELEM_LIST_TABLE],
				       family, genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	nft_ctx_init(ctx, net, skb, nlh, family, table, NULL, nla);
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

struct nft_set_dump_ctx {
	const struct nft_set	*set;
	struct nft_ctx		ctx;
};

static int nf_tables_dump_set(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nft_set_dump_ctx *dump_ctx = cb->data;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_set *set;
	struct nft_set_dump_args args;
	bool set_found = false;
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	struct nlattr *nest;
	u32 portid, seq;
	int event;

	rcu_read_lock();
	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (dump_ctx->ctx.family != NFPROTO_UNSPEC &&
		    dump_ctx->ctx.family != table->family)
			continue;

		if (table != dump_ctx->ctx.table)
			continue;

		list_for_each_entry_rcu(set, &table->sets, list) {
			if (set == dump_ctx->set) {
				set_found = true;
				break;
			}
		}
		break;
	}

	if (!set_found) {
		rcu_read_unlock();
		return -ENOENT;
	}

	event  = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, NFT_MSG_NEWSETELEM);
	portid = NETLINK_CB(cb->skb).portid;
	seq    = cb->nlh->nlmsg_seq;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			NLM_F_MULTI);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = table->family;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_TABLE, table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_SET, set->name))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NFTA_SET_ELEM_LIST_ELEMENTS);
	if (nest == NULL)
		goto nla_put_failure;

	args.cb			= cb;
	args.skb		= skb;
	args.iter.genmask	= nft_genmask_cur(net);
	args.iter.skip		= cb->args[0];
	args.iter.count		= 0;
	args.iter.err		= 0;
	args.iter.fn		= nf_tables_dump_setelem;
	set->ops->walk(&dump_ctx->ctx, set, &args.iter);
	rcu_read_unlock();

	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);

	if (args.iter.err && args.iter.err != -EMSGSIZE)
		return args.iter.err;
	if (args.iter.count == cb->args[0])
		return 0;

	cb->args[0] = args.iter.count;
	return skb->len;

nla_put_failure:
	rcu_read_unlock();
	return -ENOSPC;
}

static int nf_tables_dump_set_done(struct netlink_callback *cb)
{
	kfree(cb->data);
	return 0;
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

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
			flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= ctx->family;
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

static int nft_get_set_elem(struct nft_ctx *ctx, struct nft_set *set,
			    const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	const struct nft_set_ext *ext;
	struct nft_data_desc desc;
	struct nft_set_elem elem;
	struct sk_buff *skb;
	uint32_t flags = 0;
	void *priv;
	int err;

	err = nla_parse_nested(nla, NFTA_SET_ELEM_MAX, attr,
			       nft_set_elem_policy, NULL);
	if (err < 0)
		return err;

	if (!nla[NFTA_SET_ELEM_KEY])
		return -EINVAL;

	err = nft_setelem_parse_flags(set, nla[NFTA_SET_ELEM_FLAGS], &flags);
	if (err < 0)
		return err;

	err = nft_data_init(ctx, &elem.key.val, sizeof(elem.key), &desc,
			    nla[NFTA_SET_ELEM_KEY]);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (desc.type != NFT_DATA_VALUE || desc.len != set->klen)
		return err;

	priv = set->ops->get(ctx->net, set, &elem, flags);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	elem.priv = priv;
	ext = nft_set_elem_ext(set, &elem);

	err = -ENOMEM;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err1;

	err = nf_tables_fill_setelem_info(skb, ctx, ctx->seq, ctx->portid,
					  NFT_MSG_NEWSETELEM, 0, set, &elem);
	if (err < 0)
		goto err2;

	err = nfnetlink_unicast(skb, ctx->net, ctx->portid, MSG_DONTWAIT);
	/* This avoids a loop in nfnetlink. */
	if (err < 0)
		goto err1;

	return 0;
err2:
	kfree_skb(skb);
err1:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static int nf_tables_getsetelem(struct net *net, struct sock *nlsk,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const nla[],
				struct netlink_ext_ack *extack)
{
	u8 genmask = nft_genmask_cur(net);
	struct nft_set *set;
	struct nlattr *attr;
	struct nft_ctx ctx;
	int rem, err = 0;

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
			.done = nf_tables_dump_set_done,
		};
		struct nft_set_dump_ctx *dump_ctx;

		dump_ctx = kmalloc(sizeof(*dump_ctx), GFP_KERNEL);
		if (!dump_ctx)
			return -ENOMEM;

		dump_ctx->set = set;
		dump_ctx->ctx = ctx;

		c.data = dump_ctx;
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	if (!nla[NFTA_SET_ELEM_LIST_ELEMENTS])
		return -EINVAL;

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_get_set_elem(&ctx, set, attr);
		if (err < 0)
			break;
	}

	return err;
}

static void nf_tables_setelem_notify(const struct nft_ctx *ctx,
				     const struct nft_set *set,
				     const struct nft_set_elem *elem,
				     int event, u16 flags)
{
	struct net *net = ctx->net;
	u32 portid = ctx->portid;
	struct sk_buff *skb;
	int err;

	if (!ctx->report && !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_setelem_info(skb, ctx, 0, portid, event, flags,
					  set, elem);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, ctx->report,
		       GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, -ENOBUFS);
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

	nft_data_release(nft_set_ext_key(ext), NFT_DATA_VALUE);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA))
		nft_data_release(nft_set_ext_data(ext), set->dtype);
	if (destroy_expr && nft_set_ext_exists(ext, NFT_SET_EXT_EXPR))
		nf_tables_expr_destroy(NULL, nft_set_ext_expr(ext));
	if (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF))
		(*nft_set_ext_obj(ext))->use--;
	kfree(elem);
}
EXPORT_SYMBOL_GPL(nft_set_elem_destroy);

/* Only called from commit path, nft_set_elem_deactivate() already deals with
 * the refcounting from the preparation phase.
 */
static void nf_tables_set_elem_destroy(const struct nft_set *set, void *elem)
{
	struct nft_set_ext *ext = nft_set_elem_ext(set, elem);

	if (nft_set_ext_exists(ext, NFT_SET_EXT_EXPR))
		nf_tables_expr_destroy(NULL, nft_set_ext_expr(ext));
	kfree(elem);
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
			       nft_set_elem_policy, NULL);
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
				.family	= ctx->family,
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
			if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA) ^
			    nft_set_ext_exists(ext2, NFT_SET_EXT_DATA) ||
			    nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF) ^
			    nft_set_ext_exists(ext2, NFT_SET_EXT_OBJREF))
				return -EBUSY;
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
	set->ops->remove(ctx->net, set, &elem);
err5:
	kfree(trans);
err4:
	kfree(elem.priv);
err3:
	if (nla[NFTA_SET_ELEM_DATA] != NULL)
		nft_data_release(&data, d2.type);
err2:
	nft_data_release(&elem.key.val, d1.type);
err1:
	return err;
}

static int nf_tables_newsetelem(struct net *net, struct sock *nlsk,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const nla[],
				struct netlink_ext_ack *extack)
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

/**
 *	nft_data_hold - hold a nft_data item
 *
 *	@data: struct nft_data to release
 *	@type: type of data
 *
 *	Hold a nft_data item. NFT_DATA_VALUE types can be silently discarded,
 *	NFT_DATA_VERDICT bumps the reference to chains in case of NFT_JUMP and
 *	NFT_GOTO verdicts. This function must be called on active data objects
 *	from the second phase of the commit protocol.
 */
static void nft_data_hold(const struct nft_data *data, enum nft_data_types type)
{
	if (type == NFT_DATA_VERDICT) {
		switch (data->verdict.code) {
		case NFT_JUMP:
		case NFT_GOTO:
			data->verdict.chain->use++;
			break;
		}
	}
}

static void nft_set_elem_activate(const struct net *net,
				  const struct nft_set *set,
				  struct nft_set_elem *elem)
{
	const struct nft_set_ext *ext = nft_set_elem_ext(set, elem->priv);

	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA))
		nft_data_hold(nft_set_ext_data(ext), set->dtype);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF))
		(*nft_set_ext_obj(ext))->use++;
}

static void nft_set_elem_deactivate(const struct net *net,
				    const struct nft_set *set,
				    struct nft_set_elem *elem)
{
	const struct nft_set_ext *ext = nft_set_elem_ext(set, elem->priv);

	if (nft_set_ext_exists(ext, NFT_SET_EXT_DATA))
		nft_data_release(nft_set_ext_data(ext), set->dtype);
	if (nft_set_ext_exists(ext, NFT_SET_EXT_OBJREF))
		(*nft_set_ext_obj(ext))->use--;
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
			       nft_set_elem_policy, NULL);
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

	nft_set_elem_deactivate(ctx->net, set, &elem);

	nft_trans_elem(trans) = elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;

err4:
	kfree(trans);
err3:
	kfree(elem.priv);
err2:
	nft_data_release(&elem.key.val, desc.type);
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

	if (!set->ops->flush(ctx->net, set, elem->priv)) {
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
				const struct nlattr * const nla[],
				struct netlink_ext_ack *extack)
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
		struct nft_set_iter iter = {
			.genmask	= genmask,
			.fn		= nft_flush_set,
		};
		set->ops->walk(&ctx, set, &iter);

		return iter.err;
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
		    objtype == obj->ops->type->type &&
		    nft_active_genmask(obj, genmask))
			return obj;
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(nf_tables_obj_lookup);

static struct nft_object *nf_tables_obj_lookup_byhandle(const struct nft_table *table,
							const struct nlattr *nla,
							u32 objtype, u8 genmask)
{
	struct nft_object *obj;

	list_for_each_entry(obj, &table->objects, list) {
		if (be64_to_cpu(nla_get_be64(nla)) == obj->handle &&
		    objtype == obj->ops->type->type &&
		    nft_active_genmask(obj, genmask))
			return obj;
	}
	return ERR_PTR(-ENOENT);
}

static const struct nla_policy nft_obj_policy[NFTA_OBJ_MAX + 1] = {
	[NFTA_OBJ_TABLE]	= { .type = NLA_STRING,
				    .len = NFT_TABLE_MAXNAMELEN - 1 },
	[NFTA_OBJ_NAME]		= { .type = NLA_STRING,
				    .len = NFT_OBJ_MAXNAMELEN - 1 },
	[NFTA_OBJ_TYPE]		= { .type = NLA_U32 },
	[NFTA_OBJ_DATA]		= { .type = NLA_NESTED },
	[NFTA_OBJ_HANDLE]	= { .type = NLA_U64},
};

static struct nft_object *nft_obj_init(const struct nft_ctx *ctx,
				       const struct nft_object_type *type,
				       const struct nlattr *attr)
{
	struct nlattr **tb;
	const struct nft_object_ops *ops;
	struct nft_object *obj;
	int err = -ENOMEM;

	tb = kmalloc_array(type->maxattr + 1, sizeof(*tb), GFP_KERNEL);
	if (!tb)
		goto err1;

	if (attr) {
		err = nla_parse_nested(tb, type->maxattr, attr, type->policy,
				       NULL);
		if (err < 0)
			goto err2;
	} else {
		memset(tb, 0, sizeof(tb[0]) * (type->maxattr + 1));
	}

	if (type->select_ops) {
		ops = type->select_ops(ctx, (const struct nlattr * const *)tb);
		if (IS_ERR(ops)) {
			err = PTR_ERR(ops);
			goto err2;
		}
	} else {
		ops = type->ops;
	}

	err = -ENOMEM;
	obj = kzalloc(sizeof(*obj) + ops->size, GFP_KERNEL);
	if (!obj)
		goto err2;

	err = ops->init(ctx, (const struct nlattr * const *)tb, obj);
	if (err < 0)
		goto err3;

	obj->ops = ops;

	kfree(tb);
	return obj;
err3:
	kfree(obj);
err2:
	kfree(tb);
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
	if (obj->ops->dump(skb, obj, reset) < 0)
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
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_object_type *type;
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_table *table;
	struct nft_object *obj;
	struct nft_ctx ctx;
	u32 objtype;
	int err;

	if (!nla[NFTA_OBJ_TYPE] ||
	    !nla[NFTA_OBJ_NAME] ||
	    !nla[NFTA_OBJ_DATA])
		return -EINVAL;

	table = nf_tables_table_lookup(net, nla[NFTA_OBJ_TABLE], family,
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	objtype = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));
	obj = nf_tables_obj_lookup(table, nla[NFTA_OBJ_NAME], objtype, genmask);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		if (err != -ENOENT)
			return err;

	} else {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		return 0;
	}

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);

	type = nft_obj_type_get(objtype);
	if (IS_ERR(type))
		return PTR_ERR(type);

	obj = nft_obj_init(&ctx, type, nla[NFTA_OBJ_DATA]);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err1;
	}
	obj->table = table;
	obj->handle = nf_tables_alloc_handle(table);

	obj->name = nla_strdup(nla[NFTA_OBJ_NAME], GFP_KERNEL);
	if (!obj->name) {
		err = -ENOMEM;
		goto err2;
	}

	err = nft_trans_obj_add(&ctx, NFT_MSG_NEWOBJ, obj);
	if (err < 0)
		goto err3;

	list_add_tail_rcu(&obj->list, &table->objects);
	table->use++;
	return 0;
err3:
	kfree(obj->name);
err2:
	if (obj->ops->destroy)
		obj->ops->destroy(obj);
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

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_OBJ_TABLE, table->name) ||
	    nla_put_string(skb, NFTA_OBJ_NAME, obj->name) ||
	    nla_put_be32(skb, NFTA_OBJ_TYPE, htonl(obj->ops->type->type)) ||
	    nla_put_be32(skb, NFTA_OBJ_USE, htonl(obj->use)) ||
	    nft_object_dump(skb, NFTA_OBJ_DATA, obj, reset) ||
	    nla_put_be64(skb, NFTA_OBJ_HANDLE, cpu_to_be64(obj->handle),
			 NFTA_OBJ_PAD))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

struct nft_obj_filter {
	char		*table;
	u32		type;
};

static int nf_tables_dump_obj(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
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

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (family != NFPROTO_UNSPEC && family != table->family)
			continue;

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
			    obj->ops->type->type != filter->type)
				goto cont;

			if (nf_tables_fill_obj_info(skb, net, NETLINK_CB(cb->skb).portid,
						    cb->nlh->nlmsg_seq,
						    NFT_MSG_NEWOBJ,
						    NLM_F_MULTI | NLM_F_APPEND,
						    table->family, table,
						    obj, reset) < 0)
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

static int nf_tables_dump_obj_done(struct netlink_callback *cb)
{
	struct nft_obj_filter *filter = cb->data;

	if (filter) {
		kfree(filter->table);
		kfree(filter);
	}

	return 0;
}

static struct nft_obj_filter *
nft_obj_filter_alloc(const struct nlattr * const nla[])
{
	struct nft_obj_filter *filter;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return ERR_PTR(-ENOMEM);

	if (nla[NFTA_OBJ_TABLE]) {
		filter->table = nla_strdup(nla[NFTA_OBJ_TABLE], GFP_KERNEL);
		if (!filter->table) {
			kfree(filter);
			return ERR_PTR(-ENOMEM);
		}
	}
	if (nla[NFTA_OBJ_TYPE])
		filter->type = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));

	return filter;
}

static int nf_tables_getobj(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	int family = nfmsg->nfgen_family;
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

	table = nf_tables_table_lookup(net, nla[NFTA_OBJ_TABLE], family,
				       genmask);
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
}

static void nft_obj_destroy(struct nft_object *obj)
{
	if (obj->ops->destroy)
		obj->ops->destroy(obj);

	module_put(obj->ops->type->owner);
	kfree(obj->name);
	kfree(obj);
}

static int nf_tables_delobj(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_table *table;
	struct nft_object *obj;
	struct nft_ctx ctx;
	u32 objtype;

	if (!nla[NFTA_OBJ_TYPE] ||
	    (!nla[NFTA_OBJ_NAME] && !nla[NFTA_OBJ_HANDLE]))
		return -EINVAL;

	table = nf_tables_table_lookup(net, nla[NFTA_OBJ_TABLE], family,
				       genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	objtype = ntohl(nla_get_be32(nla[NFTA_OBJ_TYPE]));
	if (nla[NFTA_OBJ_HANDLE])
		obj = nf_tables_obj_lookup_byhandle(table, nla[NFTA_OBJ_HANDLE],
						    objtype, genmask);
	else
		obj = nf_tables_obj_lookup(table, nla[NFTA_OBJ_NAME],
					   objtype, genmask);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	if (obj->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);

	return nft_delobj(&ctx, obj);
}

void nft_obj_notify(struct net *net, struct nft_table *table,
		    struct nft_object *obj, u32 portid, u32 seq, int event,
		    int family, int report, gfp_t gfp)
{
	struct sk_buff *skb;
	int err;

	if (!report &&
	    !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_obj_info(skb, net, portid, seq, event, 0, family,
				      table, obj, false);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, report, gfp);
	return;
err:
	nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, -ENOBUFS);
}
EXPORT_SYMBOL_GPL(nft_obj_notify);

static void nf_tables_obj_notify(const struct nft_ctx *ctx,
				 struct nft_object *obj, int event)
{
	nft_obj_notify(ctx->net, ctx->table, obj, ctx->portid, ctx->seq, event,
		       ctx->family, ctx->report, GFP_KERNEL);
}

/*
 * Flow tables
 */
void nft_register_flowtable_type(struct nf_flowtable_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail_rcu(&type->list, &nf_tables_flowtables);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_register_flowtable_type);

void nft_unregister_flowtable_type(struct nf_flowtable_type *type)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del_rcu(&type->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_flowtable_type);

static const struct nla_policy nft_flowtable_policy[NFTA_FLOWTABLE_MAX + 1] = {
	[NFTA_FLOWTABLE_TABLE]		= { .type = NLA_STRING,
					    .len = NFT_NAME_MAXLEN - 1 },
	[NFTA_FLOWTABLE_NAME]		= { .type = NLA_STRING,
					    .len = NFT_NAME_MAXLEN - 1 },
	[NFTA_FLOWTABLE_HOOK]		= { .type = NLA_NESTED },
	[NFTA_FLOWTABLE_HANDLE]		= { .type = NLA_U64 },
};

struct nft_flowtable *nf_tables_flowtable_lookup(const struct nft_table *table,
						 const struct nlattr *nla,
						 u8 genmask)
{
	struct nft_flowtable *flowtable;

	list_for_each_entry(flowtable, &table->flowtables, list) {
		if (!nla_strcmp(nla, flowtable->name) &&
		    nft_active_genmask(flowtable, genmask))
			return flowtable;
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(nf_tables_flowtable_lookup);

static struct nft_flowtable *
nf_tables_flowtable_lookup_byhandle(const struct nft_table *table,
				    const struct nlattr *nla, u8 genmask)
{
       struct nft_flowtable *flowtable;

       list_for_each_entry(flowtable, &table->flowtables, list) {
               if (be64_to_cpu(nla_get_be64(nla)) == flowtable->handle &&
                   nft_active_genmask(flowtable, genmask))
                       return flowtable;
       }
       return ERR_PTR(-ENOENT);
}

#define NFT_FLOWTABLE_DEVICE_MAX	8

static int nf_tables_parse_devices(const struct nft_ctx *ctx,
				   const struct nlattr *attr,
				   struct net_device *dev_array[], int *len)
{
	const struct nlattr *tmp;
	struct net_device *dev;
	char ifname[IFNAMSIZ];
	int rem, n = 0, err;

	nla_for_each_nested(tmp, attr, rem) {
		if (nla_type(tmp) != NFTA_DEVICE_NAME) {
			err = -EINVAL;
			goto err1;
		}

		nla_strlcpy(ifname, tmp, IFNAMSIZ);
		dev = dev_get_by_name(ctx->net, ifname);
		if (!dev) {
			err = -ENOENT;
			goto err1;
		}

		dev_array[n++] = dev;
		if (n == NFT_FLOWTABLE_DEVICE_MAX) {
			err = -EFBIG;
			goto err1;
		}
	}
	if (!len)
		return -EINVAL;

	err = 0;
err1:
	*len = n;
	return err;
}

static const struct nla_policy nft_flowtable_hook_policy[NFTA_FLOWTABLE_HOOK_MAX + 1] = {
	[NFTA_FLOWTABLE_HOOK_NUM]	= { .type = NLA_U32 },
	[NFTA_FLOWTABLE_HOOK_PRIORITY]	= { .type = NLA_U32 },
	[NFTA_FLOWTABLE_HOOK_DEVS]	= { .type = NLA_NESTED },
};

static int nf_tables_flowtable_parse_hook(const struct nft_ctx *ctx,
					  const struct nlattr *attr,
					  struct nft_flowtable *flowtable)
{
	struct net_device *dev_array[NFT_FLOWTABLE_DEVICE_MAX];
	struct nlattr *tb[NFTA_FLOWTABLE_HOOK_MAX + 1];
	struct nf_hook_ops *ops;
	int hooknum, priority;
	int err, n = 0, i;

	err = nla_parse_nested(tb, NFTA_FLOWTABLE_HOOK_MAX, attr,
			       nft_flowtable_hook_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_FLOWTABLE_HOOK_NUM] ||
	    !tb[NFTA_FLOWTABLE_HOOK_PRIORITY] ||
	    !tb[NFTA_FLOWTABLE_HOOK_DEVS])
		return -EINVAL;

	hooknum = ntohl(nla_get_be32(tb[NFTA_FLOWTABLE_HOOK_NUM]));
	if (hooknum != NF_NETDEV_INGRESS)
		return -EINVAL;

	priority = ntohl(nla_get_be32(tb[NFTA_FLOWTABLE_HOOK_PRIORITY]));

	err = nf_tables_parse_devices(ctx, tb[NFTA_FLOWTABLE_HOOK_DEVS],
				      dev_array, &n);
	if (err < 0)
		goto err1;

	ops = kzalloc(sizeof(struct nf_hook_ops) * n, GFP_KERNEL);
	if (!ops) {
		err = -ENOMEM;
		goto err1;
	}

	flowtable->hooknum	= hooknum;
	flowtable->priority	= priority;
	flowtable->ops		= ops;
	flowtable->ops_len	= n;

	for (i = 0; i < n; i++) {
		flowtable->ops[i].pf		= NFPROTO_NETDEV;
		flowtable->ops[i].hooknum	= hooknum;
		flowtable->ops[i].priority	= priority;
		flowtable->ops[i].priv		= &flowtable->data.rhashtable;
		flowtable->ops[i].hook		= flowtable->data.type->hook;
		flowtable->ops[i].dev		= dev_array[i];
	}

	err = 0;
err1:
	for (i = 0; i < n; i++)
		dev_put(dev_array[i]);

	return err;
}

static const struct nf_flowtable_type *__nft_flowtable_type_get(u8 family)
{
	const struct nf_flowtable_type *type;

	list_for_each_entry(type, &nf_tables_flowtables, list) {
		if (family == type->family)
			return type;
	}
	return NULL;
}

static const struct nf_flowtable_type *nft_flowtable_type_get(u8 family)
{
	const struct nf_flowtable_type *type;

	type = __nft_flowtable_type_get(family);
	if (type != NULL && try_module_get(type->owner))
		return type;

#ifdef CONFIG_MODULES
	if (type == NULL) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nf-flowtable-%u", family);
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_flowtable_type_get(family))
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-ENOENT);
}

void nft_flow_table_iterate(struct net *net,
			    void (*iter)(struct nf_flowtable *flowtable, void *data),
			    void *data)
{
	struct nft_flowtable *flowtable;
	const struct nft_table *table;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_for_each_entry(table, &net->nft.tables, list) {
		list_for_each_entry(flowtable, &table->flowtables, list) {
			iter(&flowtable->data, data);
		}
	}
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_flow_table_iterate);

static void nft_unregister_flowtable_net_hooks(struct net *net,
					       struct nft_flowtable *flowtable)
{
	int i;

	for (i = 0; i < flowtable->ops_len; i++) {
		if (!flowtable->ops[i].dev)
			continue;

		nf_unregister_net_hook(net, &flowtable->ops[i]);
	}
}

static int nf_tables_newflowtable(struct net *net, struct sock *nlsk,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const nla[],
				  struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nf_flowtable_type *type;
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_flowtable *flowtable;
	struct nft_table *table;
	struct nft_ctx ctx;
	int err, i, k;

	if (!nla[NFTA_FLOWTABLE_TABLE] ||
	    !nla[NFTA_FLOWTABLE_NAME] ||
	    !nla[NFTA_FLOWTABLE_HOOK])
		return -EINVAL;

	table = nf_tables_table_lookup(net, nla[NFTA_FLOWTABLE_TABLE],
				       family, genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	flowtable = nf_tables_flowtable_lookup(table, nla[NFTA_FLOWTABLE_NAME],
					       genmask);
	if (IS_ERR(flowtable)) {
		err = PTR_ERR(flowtable);
		if (err != -ENOENT)
			return err;
	} else {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		return 0;
	}

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);

	flowtable = kzalloc(sizeof(*flowtable), GFP_KERNEL);
	if (!flowtable)
		return -ENOMEM;

	flowtable->table = table;
	flowtable->handle = nf_tables_alloc_handle(table);

	flowtable->name = nla_strdup(nla[NFTA_FLOWTABLE_NAME], GFP_KERNEL);
	if (!flowtable->name) {
		err = -ENOMEM;
		goto err1;
	}

	type = nft_flowtable_type_get(family);
	if (IS_ERR(type)) {
		err = PTR_ERR(type);
		goto err2;
	}

	flowtable->data.type = type;
	err = rhashtable_init(&flowtable->data.rhashtable, type->params);
	if (err < 0)
		goto err3;

	err = nf_tables_flowtable_parse_hook(&ctx, nla[NFTA_FLOWTABLE_HOOK],
					     flowtable);
	if (err < 0)
		goto err3;

	for (i = 0; i < flowtable->ops_len; i++) {
		err = nf_register_net_hook(net, &flowtable->ops[i]);
		if (err < 0)
			goto err4;
	}

	err = nft_trans_flowtable_add(&ctx, NFT_MSG_NEWFLOWTABLE, flowtable);
	if (err < 0)
		goto err5;

	INIT_DEFERRABLE_WORK(&flowtable->data.gc_work, type->gc);
	queue_delayed_work(system_power_efficient_wq,
			   &flowtable->data.gc_work, HZ);

	list_add_tail_rcu(&flowtable->list, &table->flowtables);
	table->use++;

	return 0;
err5:
	i = flowtable->ops_len;
err4:
	for (k = i - 1; k >= 0; k--)
		nf_unregister_net_hook(net, &flowtable->ops[i]);

	kfree(flowtable->ops);
err3:
	module_put(type->owner);
err2:
	kfree(flowtable->name);
err1:
	kfree(flowtable);
	return err;
}

static int nf_tables_delflowtable(struct net *net, struct sock *nlsk,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const nla[],
				  struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_next(net);
	int family = nfmsg->nfgen_family;
	struct nft_flowtable *flowtable;
	struct nft_table *table;
	struct nft_ctx ctx;

	table = nf_tables_table_lookup(net, nla[NFTA_FLOWTABLE_TABLE],
				       family, genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (nla[NFTA_FLOWTABLE_HANDLE])
		flowtable = nf_tables_flowtable_lookup_byhandle(table,
								nla[NFTA_FLOWTABLE_HANDLE],
								genmask);
	else
		flowtable = nf_tables_flowtable_lookup(table,
						       nla[NFTA_FLOWTABLE_NAME],
						       genmask);
	if (IS_ERR(flowtable))
                return PTR_ERR(flowtable);
	if (flowtable->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, net, skb, nlh, family, table, NULL, nla);

	return nft_delflowtable(&ctx, flowtable);
}

static int nf_tables_fill_flowtable_info(struct sk_buff *skb, struct net *net,
					 u32 portid, u32 seq, int event,
					 u32 flags, int family,
					 struct nft_flowtable *flowtable)
{
	struct nlattr *nest, *nest_devs;
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	int i;

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), flags);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_string(skb, NFTA_FLOWTABLE_TABLE, flowtable->table->name) ||
	    nla_put_string(skb, NFTA_FLOWTABLE_NAME, flowtable->name) ||
	    nla_put_be32(skb, NFTA_FLOWTABLE_USE, htonl(flowtable->use)) ||
	    nla_put_be64(skb, NFTA_FLOWTABLE_HANDLE, cpu_to_be64(flowtable->handle),
			 NFTA_FLOWTABLE_PAD))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NFTA_FLOWTABLE_HOOK);
	if (nla_put_be32(skb, NFTA_FLOWTABLE_HOOK_NUM, htonl(flowtable->hooknum)) ||
	    nla_put_be32(skb, NFTA_FLOWTABLE_HOOK_PRIORITY, htonl(flowtable->priority)))
		goto nla_put_failure;

	nest_devs = nla_nest_start(skb, NFTA_FLOWTABLE_HOOK_DEVS);
	if (!nest_devs)
		goto nla_put_failure;

	for (i = 0; i < flowtable->ops_len; i++) {
		if (flowtable->ops[i].dev &&
		    nla_put_string(skb, NFTA_DEVICE_NAME,
				   flowtable->ops[i].dev->name))
			goto nla_put_failure;
	}
	nla_nest_end(skb, nest_devs);
	nla_nest_end(skb, nest);

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

struct nft_flowtable_filter {
	char		*table;
};

static int nf_tables_dump_flowtable(struct sk_buff *skb,
				    struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	struct nft_flowtable_filter *filter = cb->data;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	struct nft_flowtable *flowtable;
	const struct nft_table *table;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(table, &net->nft.tables, list) {
		if (family != NFPROTO_UNSPEC && family != table->family)
			continue;

		list_for_each_entry_rcu(flowtable, &table->flowtables, list) {
			if (!nft_is_active(net, flowtable))
				goto cont;
			if (idx < s_idx)
				goto cont;
			if (idx > s_idx)
				memset(&cb->args[1], 0,
				       sizeof(cb->args) - sizeof(cb->args[0]));
			if (filter && filter->table[0] &&
			    strcmp(filter->table, table->name))
				goto cont;

			if (nf_tables_fill_flowtable_info(skb, net, NETLINK_CB(cb->skb).portid,
							  cb->nlh->nlmsg_seq,
							  NFT_MSG_NEWFLOWTABLE,
							  NLM_F_MULTI | NLM_F_APPEND,
							  table->family, flowtable) < 0)
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

static int nf_tables_dump_flowtable_done(struct netlink_callback *cb)
{
	struct nft_flowtable_filter *filter = cb->data;

	if (!filter)
		return 0;

	kfree(filter->table);
	kfree(filter);

	return 0;
}

static struct nft_flowtable_filter *
nft_flowtable_filter_alloc(const struct nlattr * const nla[])
{
	struct nft_flowtable_filter *filter;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return ERR_PTR(-ENOMEM);

	if (nla[NFTA_FLOWTABLE_TABLE]) {
		filter->table = nla_strdup(nla[NFTA_FLOWTABLE_TABLE],
					   GFP_KERNEL);
		if (!filter->table) {
			kfree(filter);
			return ERR_PTR(-ENOMEM);
		}
	}
	return filter;
}

static int nf_tables_getflowtable(struct net *net, struct sock *nlsk,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const nla[],
				  struct netlink_ext_ack *extack)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u8 genmask = nft_genmask_cur(net);
	int family = nfmsg->nfgen_family;
	struct nft_flowtable *flowtable;
	const struct nft_table *table;
	struct sk_buff *skb2;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_flowtable,
			.done = nf_tables_dump_flowtable_done,
		};

		if (nla[NFTA_FLOWTABLE_TABLE]) {
			struct nft_flowtable_filter *filter;

			filter = nft_flowtable_filter_alloc(nla);
			if (IS_ERR(filter))
				return -ENOMEM;

			c.data = filter;
		}
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	if (!nla[NFTA_FLOWTABLE_NAME])
		return -EINVAL;

	table = nf_tables_table_lookup(net, nla[NFTA_FLOWTABLE_TABLE],
				       family, genmask);
	if (IS_ERR(table))
		return PTR_ERR(table);

	flowtable = nf_tables_flowtable_lookup(table, nla[NFTA_FLOWTABLE_NAME],
					       genmask);
	if (IS_ERR(flowtable))
		return PTR_ERR(flowtable);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_flowtable_info(skb2, net, NETLINK_CB(skb).portid,
					    nlh->nlmsg_seq,
					    NFT_MSG_NEWFLOWTABLE, 0, family,
					    flowtable);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);
err:
	kfree_skb(skb2);
	return err;
}

static void nf_tables_flowtable_notify(struct nft_ctx *ctx,
				       struct nft_flowtable *flowtable,
				       int event)
{
	struct sk_buff *skb;
	int err;

	if (ctx->report &&
	    !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_flowtable_info(skb, ctx->net, ctx->portid,
					    ctx->seq, event, 0,
					    ctx->family, flowtable);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	nfnetlink_send(skb, ctx->net, ctx->portid, NFNLGRP_NFTABLES,
		       ctx->report, GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(ctx->net, ctx->portid, NFNLGRP_NFTABLES, -ENOBUFS);
}

static void nf_tables_flowtable_destroy(struct nft_flowtable *flowtable)
{
	cancel_delayed_work_sync(&flowtable->data.gc_work);
	kfree(flowtable->name);
	flowtable->data.type->free(&flowtable->data);
	rhashtable_destroy(&flowtable->data.rhashtable);
	module_put(flowtable->data.type->owner);
}

static int nf_tables_fill_gen_info(struct sk_buff *skb, struct net *net,
				   u32 portid, u32 seq)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	char buf[TASK_COMM_LEN];
	int event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, NFT_MSG_NEWGEN);

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg), 0);
	if (nlh == NULL)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= AF_UNSPEC;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= htons(net->nft.base_seq & 0xffff);

	if (nla_put_be32(skb, NFTA_GEN_ID, htonl(net->nft.base_seq)) ||
	    nla_put_be32(skb, NFTA_GEN_PROC_PID, htonl(task_pid_nr(current))) ||
	    nla_put_string(skb, NFTA_GEN_PROC_NAME, get_task_comm(buf, current)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -EMSGSIZE;
}

static void nft_flowtable_event(unsigned long event, struct net_device *dev,
				struct nft_flowtable *flowtable)
{
	int i;

	for (i = 0; i < flowtable->ops_len; i++) {
		if (flowtable->ops[i].dev != dev)
			continue;

		nf_unregister_net_hook(dev_net(dev), &flowtable->ops[i]);
		flowtable->ops[i].dev = NULL;
		break;
	}
}

static int nf_tables_flowtable_event(struct notifier_block *this,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nft_flowtable *flowtable;
	struct nft_table *table;

	if (event != NETDEV_UNREGISTER)
		return 0;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_for_each_entry(table, &dev_net(dev)->nft.tables, list) {
		list_for_each_entry(flowtable, &table->flowtables, list) {
			nft_flowtable_event(event, dev, flowtable);
		}
	}
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);

	return NOTIFY_DONE;
}

static struct notifier_block nf_tables_flowtable_notifier = {
	.notifier_call	= nf_tables_flowtable_event,
};

static void nf_tables_gen_notify(struct net *net, struct sk_buff *skb,
				 int event)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct sk_buff *skb2;
	int err;

	if (nlmsg_report(nlh) &&
	    !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return;

	skb2 = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb2 == NULL)
		goto err;

	err = nf_tables_fill_gen_info(skb2, net, NETLINK_CB(skb).portid,
				      nlh->nlmsg_seq);
	if (err < 0) {
		kfree_skb(skb2);
		goto err;
	}

	nfnetlink_send(skb2, net, NETLINK_CB(skb).portid, NFNLGRP_NFTABLES,
		       nlmsg_report(nlh), GFP_KERNEL);
	return;
err:
	nfnetlink_set_err(net, NETLINK_CB(skb).portid, NFNLGRP_NFTABLES,
			  -ENOBUFS);
}

static int nf_tables_getgen(struct net *net, struct sock *nlsk,
			    struct sk_buff *skb, const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[],
			    struct netlink_ext_ack *extack)
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
	[NFT_MSG_NEWFLOWTABLE] = {
		.call_batch	= nf_tables_newflowtable,
		.attr_count	= NFTA_FLOWTABLE_MAX,
		.policy		= nft_flowtable_policy,
	},
	[NFT_MSG_GETFLOWTABLE] = {
		.call		= nf_tables_getflowtable,
		.attr_count	= NFTA_FLOWTABLE_MAX,
		.policy		= nft_flowtable_policy,
	},
	[NFT_MSG_DELFLOWTABLE] = {
		.call_batch	= nf_tables_delflowtable,
		.attr_count	= NFTA_FLOWTABLE_MAX,
		.policy		= nft_flowtable_policy,
	},
};

static void nft_chain_commit_update(struct nft_trans *trans)
{
	struct nft_base_chain *basechain;

	if (nft_trans_chain_name(trans))
		strcpy(trans->ctx.chain->name, nft_trans_chain_name(trans));

	if (!nft_is_base_chain(trans->ctx.chain))
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
		nf_tables_chain_destroy(&trans->ctx);
		break;
	case NFT_MSG_DELRULE:
		nf_tables_rule_destroy(&trans->ctx, nft_trans_rule(trans));
		break;
	case NFT_MSG_DELSET:
		nft_set_destroy(nft_trans_set(trans));
		break;
	case NFT_MSG_DELSETELEM:
		nf_tables_set_elem_destroy(nft_trans_elem_set(trans),
					   nft_trans_elem(trans).priv);
		break;
	case NFT_MSG_DELOBJ:
		nft_obj_destroy(nft_trans_obj(trans));
		break;
	case NFT_MSG_DELFLOWTABLE:
		nf_tables_flowtable_destroy(nft_trans_flowtable(trans));
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
			nf_tables_unregister_hook(trans->ctx.net,
						  trans->ctx.table,
						  trans->ctx.chain);
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
			if (nft_set_is_anonymous(nft_trans_set(trans)) &&
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
			te->set->ops->remove(net, te->set, &te->elem);
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
		case NFT_MSG_NEWFLOWTABLE:
			nft_clear(net, nft_trans_flowtable(trans));
			nf_tables_flowtable_notify(&trans->ctx,
						   nft_trans_flowtable(trans),
						   NFT_MSG_NEWFLOWTABLE);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELFLOWTABLE:
			list_del_rcu(&nft_trans_flowtable(trans)->list);
			nf_tables_flowtable_notify(&trans->ctx,
						   nft_trans_flowtable(trans),
						   NFT_MSG_DELFLOWTABLE);
			nft_unregister_flowtable_net_hooks(net,
					nft_trans_flowtable(trans));
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
		nf_tables_chain_destroy(&trans->ctx);
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
	case NFT_MSG_NEWFLOWTABLE:
		nf_tables_flowtable_destroy(nft_trans_flowtable(trans));
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
				nf_tables_unregister_hook(trans->ctx.net,
							  trans->ctx.table,
							  trans->ctx.chain);
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

			te->set->ops->remove(net, te->set, &te->elem);
			atomic_dec(&te->set->nelems);
			break;
		case NFT_MSG_DELSETELEM:
			te = (struct nft_trans_elem *)trans->data;

			nft_set_elem_activate(net, te->set, &te->elem);
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
		case NFT_MSG_NEWFLOWTABLE:
			trans->ctx.table->use--;
			list_del_rcu(&nft_trans_flowtable(trans)->list);
			nft_unregister_flowtable_net_hooks(net,
					nft_trans_flowtable(trans));
			break;
		case NFT_MSG_DELFLOWTABLE:
			trans->ctx.table->use++;
			nft_clear(trans->ctx.net, nft_trans_flowtable(trans));
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

static bool nf_tables_valid_genid(struct net *net, u32 genid)
{
	return net->nft.base_seq == genid;
}

static const struct nfnetlink_subsystem nf_tables_subsys = {
	.name		= "nf_tables",
	.subsys_id	= NFNL_SUBSYS_NFTABLES,
	.cb_count	= NFT_MSG_MAX,
	.cb		= nf_tables_cb,
	.commit		= nf_tables_commit,
	.abort		= nf_tables_abort,
	.valid_genid	= nf_tables_valid_genid,
};

int nft_chain_validate_dependency(const struct nft_chain *chain,
				  enum nft_chain_types type)
{
	const struct nft_base_chain *basechain;

	if (nft_is_base_chain(chain)) {
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

	if (nft_is_base_chain(chain)) {
		basechain = nft_base_chain(chain);

		if ((1 << basechain->ops.hooknum) & hook_flags)
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

	err = nla_parse_nested(tb, NFTA_VERDICT_MAX, nla, nft_verdict_policy,
			       NULL);
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
		if (nft_is_base_chain(chain))
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

	err = nla_parse_nested(tb, NFTA_DATA_MAX, nla, nft_data_policy, NULL);
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
 *	nft_data_release - release a nft_data item
 *
 *	@data: struct nft_data to release
 *	@type: type of data
 *
 *	Release a nft_data item. NFT_DATA_VALUE types can be silently discarded,
 *	all others need to be released by calling this function.
 */
void nft_data_release(const struct nft_data *data, enum nft_data_types type)
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
EXPORT_SYMBOL_GPL(nft_data_release);

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

int __nft_release_basechain(struct nft_ctx *ctx)
{
	struct nft_rule *rule, *nr;

	BUG_ON(!nft_is_base_chain(ctx->chain));

	nf_tables_unregister_hook(ctx->net, ctx->chain->table, ctx->chain);
	list_for_each_entry_safe(rule, nr, &ctx->chain->rules, list) {
		list_del(&rule->list);
		ctx->chain->use--;
		nf_tables_rule_destroy(ctx, rule);
	}
	list_del(&ctx->chain->list);
	ctx->table->use--;
	nf_tables_chain_destroy(ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(__nft_release_basechain);

static void __nft_release_tables(struct net *net)
{
	struct nft_flowtable *flowtable, *nf;
	struct nft_table *table, *nt;
	struct nft_chain *chain, *nc;
	struct nft_object *obj, *ne;
	struct nft_rule *rule, *nr;
	struct nft_set *set, *ns;
	struct nft_ctx ctx = {
		.net	= net,
		.family	= NFPROTO_NETDEV,
	};

	list_for_each_entry_safe(table, nt, &net->nft.tables, list) {
		ctx.family = table->family;

		list_for_each_entry(chain, &table->chains, list)
			nf_tables_unregister_hook(net, table, chain);
		list_for_each_entry(flowtable, &table->flowtables, list)
			nf_unregister_net_hooks(net, flowtable->ops,
						flowtable->ops_len);
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
		list_for_each_entry_safe(flowtable, nf, &table->flowtables, list) {
			list_del(&flowtable->list);
			table->use--;
			nf_tables_flowtable_destroy(flowtable);
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
			ctx.chain = chain;
			list_del(&chain->list);
			table->use--;
			nf_tables_chain_destroy(&ctx);
		}
		list_del(&table->list);
		nf_tables_table_destroy(&ctx);
	}
}

static int __net_init nf_tables_init_net(struct net *net)
{
	INIT_LIST_HEAD(&net->nft.tables);
	INIT_LIST_HEAD(&net->nft.commit_list);
	net->nft.base_seq = 1;
	return 0;
}

static void __net_exit nf_tables_exit_net(struct net *net)
{
	__nft_release_tables(net);
	WARN_ON_ONCE(!list_empty(&net->nft.tables));
	WARN_ON_ONCE(!list_empty(&net->nft.commit_list));
}

static struct pernet_operations nf_tables_net_ops = {
	.init	= nf_tables_init_net,
	.exit	= nf_tables_exit_net,
};

static int __init nf_tables_module_init(void)
{
	int err;

	nft_chain_filter_init();

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

	register_netdevice_notifier(&nf_tables_flowtable_notifier);

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
	unregister_netdevice_notifier(&nf_tables_flowtable_notifier);
	rcu_barrier();
	nf_tables_core_module_exit();
	kfree(info);
	nft_chain_filter_fini();
}

module_init(nf_tables_module_init);
module_exit(nf_tables_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_NFTABLES);
