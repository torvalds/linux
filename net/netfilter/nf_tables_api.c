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

/**
 *	nft_unregister_afinfo - unregister nf_tables address family info
 *
 *	@afi: address family info to unregister
 *
 *	Unregister the address family for use with nf_tables.
 */
void nft_unregister_afinfo(struct nft_af_info *afi)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
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
			 const struct sk_buff *skb,
			 const struct nlmsghdr *nlh,
			 struct nft_af_info *afi,
			 struct nft_table *table,
			 struct nft_chain *chain,
			 const struct nlattr * const *nla)
{
	ctx->net	= sock_net(skb->sk);
	ctx->afi	= afi;
	ctx->table	= table;
	ctx->chain	= chain;
	ctx->nla   	= nla;
	ctx->portid	= NETLINK_CB(skb).portid;
	ctx->report	= nlmsg_report(nlh);
	ctx->seq	= nlh->nlmsg_seq;
}

static struct nft_trans *nft_trans_alloc(struct nft_ctx *ctx, int msg_type,
					 u32 size)
{
	struct nft_trans *trans;

	trans = kzalloc(sizeof(struct nft_trans) + size, GFP_KERNEL);
	if (trans == NULL)
		return NULL;

	trans->msg_type = msg_type;
	trans->ctx	= *ctx;

	return trans;
}

static void nft_trans_destroy(struct nft_trans *trans)
{
	list_del(&trans->list);
	kfree(trans);
}

/*
 * Tables
 */

static struct nft_table *nft_table_lookup(const struct nft_af_info *afi,
					  const struct nlattr *nla)
{
	struct nft_table *table;

	list_for_each_entry(table, &afi->tables, list) {
		if (!nla_strcmp(nla, table->name))
			return table;
	}
	return NULL;
}

static struct nft_table *nf_tables_table_lookup(const struct nft_af_info *afi,
						const struct nlattr *nla)
{
	struct nft_table *table;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	table = nft_table_lookup(afi, nla);
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
	[NFTA_TABLE_NAME]	= { .type = NLA_STRING },
	[NFTA_TABLE_FLAGS]	= { .type = NLA_U32 },
};

static int nf_tables_fill_table_info(struct sk_buff *skb, u32 portid, u32 seq,
				     int event, u32 flags, int family,
				     const struct nft_table *table)
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
	nfmsg->res_id		= 0;

	if (nla_put_string(skb, NFTA_TABLE_NAME, table->name) ||
	    nla_put_be32(skb, NFTA_TABLE_FLAGS, htonl(table->flags)) ||
	    nla_put_be32(skb, NFTA_TABLE_USE, htonl(table->use)))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

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

	err = nf_tables_fill_table_info(skb, ctx->portid, ctx->seq, event, 0,
					ctx->afi->family, ctx->table);
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
			if (nf_tables_fill_table_info(skb,
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

/* Internal table flags */
#define NFT_TABLE_INACTIVE	(1 << 15)

static int nf_tables_gettable(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	struct sk_buff *skb2;
	struct net *net = sock_net(skb->sk);
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

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_table_info(skb2, NETLINK_CB(skb).portid,
					nlh->nlmsg_seq, NFT_MSG_NEWTABLE, 0,
					family, table);
	if (err < 0)
		goto err;

	return nlmsg_unicast(nlsk, skb2, NETLINK_CB(skb).portid);

err:
	kfree_skb(skb2);
	return err;
}

static int nf_tables_table_enable(const struct nft_af_info *afi,
				  struct nft_table *table)
{
	struct nft_chain *chain;
	int err, i = 0;

	list_for_each_entry(chain, &table->chains, list) {
		if (!(chain->flags & NFT_BASE_CHAIN))
			continue;

		err = nf_register_hooks(nft_base_chain(chain)->ops, afi->nops);
		if (err < 0)
			goto err;

		i++;
	}
	return 0;
err:
	list_for_each_entry(chain, &table->chains, list) {
		if (!(chain->flags & NFT_BASE_CHAIN))
			continue;

		if (i-- <= 0)
			break;

		nf_unregister_hooks(nft_base_chain(chain)->ops, afi->nops);
	}
	return err;
}

static void nf_tables_table_disable(const struct nft_af_info *afi,
				   struct nft_table *table)
{
	struct nft_chain *chain;

	list_for_each_entry(chain, &table->chains, list) {
		if (chain->flags & NFT_BASE_CHAIN)
			nf_unregister_hooks(nft_base_chain(chain)->ops,
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
		ret = nf_tables_table_enable(ctx->afi, ctx->table);
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

static int nft_trans_table_add(struct nft_ctx *ctx, int msg_type)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_table));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWTABLE)
		ctx->table->flags |= NFT_TABLE_INACTIVE;

	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;
}

static int nf_tables_newtable(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr *name;
	struct nft_af_info *afi;
	struct nft_table *table;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	u32 flags = 0;
	struct nft_ctx ctx;
	int err;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	name = nla[NFTA_TABLE_NAME];
	table = nf_tables_table_lookup(afi, name);
	if (IS_ERR(table)) {
		if (PTR_ERR(table) != -ENOENT)
			return PTR_ERR(table);
		table = NULL;
	}

	if (table != NULL) {
		if (table->flags & NFT_TABLE_INACTIVE)
			return -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		nft_ctx_init(&ctx, skb, nlh, afi, table, NULL, nla);
		return nf_tables_updtable(&ctx);
	}

	if (nla[NFTA_TABLE_FLAGS]) {
		flags = ntohl(nla_get_be32(nla[NFTA_TABLE_FLAGS]));
		if (flags & ~NFT_TABLE_F_DORMANT)
			return -EINVAL;
	}

	if (!try_module_get(afi->owner))
		return -EAFNOSUPPORT;

	table = kzalloc(sizeof(*table) + nla_len(name), GFP_KERNEL);
	if (table == NULL) {
		module_put(afi->owner);
		return -ENOMEM;
	}

	nla_strlcpy(table->name, name, nla_len(name));
	INIT_LIST_HEAD(&table->chains);
	INIT_LIST_HEAD(&table->sets);
	table->flags = flags;

	nft_ctx_init(&ctx, skb, nlh, afi, table, NULL, nla);
	err = nft_trans_table_add(&ctx, NFT_MSG_NEWTABLE);
	if (err < 0) {
		kfree(table);
		module_put(afi->owner);
		return err;
	}
	list_add_tail_rcu(&table->list, &afi->tables);
	return 0;
}

static int nf_tables_deltable(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family, err;
	struct nft_ctx ctx;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;
	if (table->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, skb, nlh, afi, table, NULL, nla);
	err = nft_trans_table_add(&ctx, NFT_MSG_DELTABLE);
	if (err < 0)
		return err;

	list_del_rcu(&table->list);
	return 0;
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
nf_tables_chain_lookup_byhandle(const struct nft_table *table, u64 handle)
{
	struct nft_chain *chain;

	list_for_each_entry(chain, &table->chains, list) {
		if (chain->handle == handle)
			return chain;
	}

	return ERR_PTR(-ENOENT);
}

static struct nft_chain *nf_tables_chain_lookup(const struct nft_table *table,
						const struct nlattr *nla)
{
	struct nft_chain *chain;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(chain, &table->chains, list) {
		if (!nla_strcmp(nla, chain->name))
			return chain;
	}

	return ERR_PTR(-ENOENT);
}

static const struct nla_policy nft_chain_policy[NFTA_CHAIN_MAX + 1] = {
	[NFTA_CHAIN_TABLE]	= { .type = NLA_STRING },
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

	if (nla_put_be64(skb, NFTA_COUNTER_PACKETS, cpu_to_be64(total.pkts)) ||
	    nla_put_be64(skb, NFTA_COUNTER_BYTES, cpu_to_be64(total.bytes)))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static int nf_tables_fill_chain_info(struct sk_buff *skb, u32 portid, u32 seq,
				     int event, u32 flags, int family,
				     const struct nft_table *table,
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
	nfmsg->res_id		= 0;

	if (nla_put_string(skb, NFTA_CHAIN_TABLE, table->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_CHAIN_HANDLE, cpu_to_be64(chain->handle)))
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

	return nlmsg_end(skb, nlh);

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

	err = nf_tables_fill_chain_info(skb, ctx->portid, ctx->seq, event, 0,
					ctx->afi->family, ctx->table,
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
				if (nf_tables_fill_chain_info(skb, NETLINK_CB(cb->skb).portid,
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

static int nf_tables_getchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	struct sk_buff *skb2;
	struct net *net = sock_net(skb->sk);
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

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);
	if (chain->flags & NFT_CHAIN_INACTIVE)
		return -ENOENT;

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_chain_info(skb2, NETLINK_CB(skb).portid,
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
	stats = this_cpu_ptr(newstats);
	stats->bytes = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_BYTES]));
	stats->pkts = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_PACKETS]));

	return newstats;
}

static void nft_chain_stats_replace(struct nft_base_chain *chain,
				    struct nft_stats __percpu *newstats)
{
	if (chain->stats) {
		struct nft_stats __percpu *oldstats =
				nft_dereference(chain->stats);

		rcu_assign_pointer(chain->stats, newstats);
		synchronize_rcu();
		free_percpu(oldstats);
	} else
		rcu_assign_pointer(chain->stats, newstats);
}

static int nft_trans_chain_add(struct nft_ctx *ctx, int msg_type)
{
	struct nft_trans *trans;

	trans = nft_trans_alloc(ctx, msg_type, sizeof(struct nft_trans_chain));
	if (trans == NULL)
		return -ENOMEM;

	if (msg_type == NFT_MSG_NEWCHAIN)
		ctx->chain->flags |= NFT_CHAIN_INACTIVE;

	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;
}

static void nf_tables_chain_destroy(struct nft_chain *chain)
{
	BUG_ON(chain->use > 0);

	if (chain->flags & NFT_BASE_CHAIN) {
		module_put(nft_base_chain(chain)->type->owner);
		free_percpu(nft_base_chain(chain)->stats);
		kfree(nft_base_chain(chain));
	} else {
		kfree(chain);
	}
}

static int nf_tables_newchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr * uninitialized_var(name);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_base_chain *basechain = NULL;
	struct nlattr *ha[NFTA_HOOK_MAX + 1];
	struct net *net = sock_net(skb->sk);
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

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = NULL;
	name = nla[NFTA_CHAIN_NAME];

	if (nla[NFTA_CHAIN_HANDLE]) {
		handle = be64_to_cpu(nla_get_be64(nla[NFTA_CHAIN_HANDLE]));
		chain = nf_tables_chain_lookup_byhandle(table, handle);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
	} else {
		chain = nf_tables_chain_lookup(table, name);
		if (IS_ERR(chain)) {
			if (PTR_ERR(chain) != -ENOENT)
				return PTR_ERR(chain);
			chain = NULL;
		}
	}

	if (nla[NFTA_CHAIN_POLICY]) {
		if ((chain != NULL &&
		    !(chain->flags & NFT_BASE_CHAIN)) ||
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

		if (chain->flags & NFT_CHAIN_INACTIVE)
			return -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		if (nla[NFTA_CHAIN_HANDLE] && name &&
		    !IS_ERR(nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME])))
			return -EEXIST;

		if (nla[NFTA_CHAIN_COUNTERS]) {
			if (!(chain->flags & NFT_BASE_CHAIN))
				return -EOPNOTSUPP;

			stats = nft_stats_alloc(nla[NFTA_CHAIN_COUNTERS]);
			if (IS_ERR(stats))
				return PTR_ERR(stats);
		}

		nft_ctx_init(&ctx, skb, nlh, afi, table, chain, nla);
		trans = nft_trans_alloc(&ctx, NFT_MSG_NEWCHAIN,
					sizeof(struct nft_trans_chain));
		if (trans == NULL)
			return -ENOMEM;

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
		const struct nf_chain_type *type;
		struct nf_hook_ops *ops;
		nf_hookfn *hookfn;
		u32 hooknum, priority;

		type = chain_type[family][NFT_CHAIN_T_DEFAULT];
		if (nla[NFTA_CHAIN_TYPE]) {
			type = nf_tables_chain_type_lookup(afi,
							   nla[NFTA_CHAIN_TYPE],
							   create);
			if (IS_ERR(type))
				return PTR_ERR(type);
		}

		err = nla_parse_nested(ha, NFTA_HOOK_MAX, nla[NFTA_CHAIN_HOOK],
				       nft_hook_policy);
		if (err < 0)
			return err;
		if (ha[NFTA_HOOK_HOOKNUM] == NULL ||
		    ha[NFTA_HOOK_PRIORITY] == NULL)
			return -EINVAL;

		hooknum = ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM]));
		if (hooknum >= afi->nhooks)
			return -EINVAL;
		priority = ntohl(nla_get_be32(ha[NFTA_HOOK_PRIORITY]));

		if (!(type->hook_mask & (1 << hooknum)))
			return -EOPNOTSUPP;
		if (!try_module_get(type->owner))
			return -ENOENT;
		hookfn = type->hooks[hooknum];

		basechain = kzalloc(sizeof(*basechain), GFP_KERNEL);
		if (basechain == NULL)
			return -ENOMEM;

		if (nla[NFTA_CHAIN_COUNTERS]) {
			stats = nft_stats_alloc(nla[NFTA_CHAIN_COUNTERS]);
			if (IS_ERR(stats)) {
				module_put(type->owner);
				kfree(basechain);
				return PTR_ERR(stats);
			}
			basechain->stats = stats;
		} else {
			stats = netdev_alloc_pcpu_stats(struct nft_stats);
			if (IS_ERR(stats)) {
				module_put(type->owner);
				kfree(basechain);
				return PTR_ERR(stats);
			}
			rcu_assign_pointer(basechain->stats, stats);
		}

		basechain->type = type;
		chain = &basechain->chain;

		for (i = 0; i < afi->nops; i++) {
			ops = &basechain->ops[i];
			ops->pf		= family;
			ops->owner	= afi->owner;
			ops->hooknum	= hooknum;
			ops->priority	= priority;
			ops->priv	= chain;
			ops->hook	= afi->hooks[ops->hooknum];
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
	chain->net = net;
	chain->table = table;
	nla_strlcpy(chain->name, name, NFT_CHAIN_MAXNAMELEN);

	if (!(table->flags & NFT_TABLE_F_DORMANT) &&
	    chain->flags & NFT_BASE_CHAIN) {
		err = nf_register_hooks(nft_base_chain(chain)->ops, afi->nops);
		if (err < 0)
			goto err1;
	}

	nft_ctx_init(&ctx, skb, nlh, afi, table, chain, nla);
	err = nft_trans_chain_add(&ctx, NFT_MSG_NEWCHAIN);
	if (err < 0)
		goto err2;

	table->use++;
	list_add_tail_rcu(&chain->list, &table->chains);
	return 0;
err2:
	if (!(table->flags & NFT_TABLE_F_DORMANT) &&
	    chain->flags & NFT_BASE_CHAIN) {
		nf_unregister_hooks(nft_base_chain(chain)->ops,
				    afi->nops);
	}
err1:
	nf_tables_chain_destroy(chain);
	return err;
}

static int nf_tables_delchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	struct nft_ctx ctx;
	int err;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);
	if (chain->flags & NFT_CHAIN_INACTIVE)
		return -ENOENT;
	if (chain->use > 0)
		return -EBUSY;

	nft_ctx_init(&ctx, skb, nlh, afi, table, chain, nla);
	err = nft_trans_chain_add(&ctx, NFT_MSG_DELCHAIN);
	if (err < 0)
		return err;

	table->use--;
	list_del_rcu(&chain->list);
	return 0;
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
	[NFTA_RULE_TABLE]	= { .type = NLA_STRING },
	[NFTA_RULE_CHAIN]	= { .type = NLA_STRING,
				    .len = NFT_CHAIN_MAXNAMELEN - 1 },
	[NFTA_RULE_HANDLE]	= { .type = NLA_U64 },
	[NFTA_RULE_EXPRESSIONS]	= { .type = NLA_NESTED },
	[NFTA_RULE_COMPAT]	= { .type = NLA_NESTED },
	[NFTA_RULE_POSITION]	= { .type = NLA_U64 },
	[NFTA_RULE_USERDATA]	= { .type = NLA_BINARY,
				    .len = NFT_USERDATA_MAXLEN },
};

static int nf_tables_fill_rule_info(struct sk_buff *skb, u32 portid, u32 seq,
				    int event, u32 flags, int family,
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
	nfmsg->res_id		= 0;

	if (nla_put_string(skb, NFTA_RULE_TABLE, table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_RULE_CHAIN, chain->name))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_RULE_HANDLE, cpu_to_be64(rule->handle)))
		goto nla_put_failure;

	if ((event != NFT_MSG_DELRULE) && (rule->list.prev != &chain->rules)) {
		prule = list_entry(rule->list.prev, struct nft_rule, list);
		if (nla_put_be64(skb, NFTA_RULE_POSITION,
				 cpu_to_be64(prule->handle)))
			goto nla_put_failure;
	}

	list = nla_nest_start(skb, NFTA_RULE_EXPRESSIONS);
	if (list == NULL)
		goto nla_put_failure;
	nft_rule_for_each_expr(expr, next, rule) {
		struct nlattr *elem = nla_nest_start(skb, NFTA_LIST_ELEM);
		if (elem == NULL)
			goto nla_put_failure;
		if (nf_tables_fill_expr_info(skb, expr) < 0)
			goto nla_put_failure;
		nla_nest_end(skb, elem);
	}
	nla_nest_end(skb, list);

	if (rule->ulen &&
	    nla_put(skb, NFTA_RULE_USERDATA, rule->ulen, nft_userdata(rule)))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

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

	err = nf_tables_fill_rule_info(skb, ctx->portid, ctx->seq, event, 0,
				       ctx->afi->family, ctx->table,
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

static inline bool
nft_rule_is_active(struct net *net, const struct nft_rule *rule)
{
	return (rule->genmask & (1 << net->nft.gencursor)) == 0;
}

static inline int gencursor_next(struct net *net)
{
	return net->nft.gencursor+1 == 1 ? 1 : 0;
}

static inline int
nft_rule_is_active_next(struct net *net, const struct nft_rule *rule)
{
	return (rule->genmask & (1 << gencursor_next(net))) == 0;
}

static inline void
nft_rule_activate_next(struct net *net, struct nft_rule *rule)
{
	/* Now inactive, will be active in the future */
	rule->genmask = (1 << net->nft.gencursor);
}

static inline void
nft_rule_disactivate_next(struct net *net, struct nft_rule *rule)
{
	rule->genmask = (1 << gencursor_next(net));
}

static inline void nft_rule_clear(struct net *net, struct nft_rule *rule)
{
	rule->genmask = 0;
}

static int nf_tables_dump_rules(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
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
			list_for_each_entry_rcu(chain, &table->chains, list) {
				list_for_each_entry_rcu(rule, &chain->rules, list) {
					if (!nft_rule_is_active(net, rule))
						goto cont;
					if (idx < s_idx)
						goto cont;
					if (idx > s_idx)
						memset(&cb->args[1], 0,
						       sizeof(cb->args) - sizeof(cb->args[0]));
					if (nf_tables_fill_rule_info(skb, NETLINK_CB(cb->skb).portid,
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

static int nf_tables_getrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	const struct nft_rule *rule;
	struct sk_buff *skb2;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_rules,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);
	if (chain->flags & NFT_CHAIN_INACTIVE)
		return -ENOENT;

	rule = nf_tables_rule_lookup(chain, nla[NFTA_RULE_HANDLE]);
	if (IS_ERR(rule))
		return PTR_ERR(rule);

	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	err = nf_tables_fill_rule_info(skb2, NETLINK_CB(skb).portid,
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
	while (expr->ops && expr != nft_expr_last(rule)) {
		nf_tables_expr_destroy(ctx, expr);
		expr = nft_expr_next(expr);
	}
	kfree(rule);
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

#define NFT_RULE_MAXEXPRS	128

static struct nft_expr_info *info;

static int nf_tables_newrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule, *old_rule = NULL;
	struct nft_trans *trans = NULL;
	struct nft_expr *expr;
	struct nft_ctx ctx;
	struct nlattr *tmp;
	unsigned int size, i, n, ulen = 0;
	int err, rem;
	bool create;
	u64 handle, pos_handle;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, create);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN]);
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

	nft_ctx_init(&ctx, skb, nlh, afi, table, chain, nla);

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

	if (nla[NFTA_RULE_USERDATA])
		ulen = nla_len(nla[NFTA_RULE_USERDATA]);

	err = -ENOMEM;
	rule = kzalloc(sizeof(*rule) + size + ulen, GFP_KERNEL);
	if (rule == NULL)
		goto err1;

	nft_rule_activate_next(net, rule);

	rule->handle = handle;
	rule->dlen   = size;
	rule->ulen   = ulen;

	if (ulen)
		nla_memcpy(nft_userdata(rule), nla[NFTA_RULE_USERDATA], ulen);

	expr = nft_expr_first(rule);
	for (i = 0; i < n; i++) {
		err = nf_tables_newexpr(&ctx, &info[i], expr);
		if (err < 0)
			goto err2;
		info[i].ops = NULL;
		expr = nft_expr_next(expr);
	}

	if (nlh->nlmsg_flags & NLM_F_REPLACE) {
		if (nft_rule_is_active_next(net, old_rule)) {
			trans = nft_trans_rule_add(&ctx, NFT_MSG_DELRULE,
						   old_rule);
			if (trans == NULL) {
				err = -ENOMEM;
				goto err2;
			}
			nft_rule_disactivate_next(net, old_rule);
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
	if (trans) {
		list_del_rcu(&nft_trans_rule(trans)->list);
		nft_rule_clear(net, nft_trans_rule(trans));
		nft_trans_destroy(trans);
		chain->use++;
	}
err2:
	nf_tables_rule_destroy(&ctx, rule);
err1:
	for (i = 0; i < n; i++) {
		if (info[i].ops != NULL)
			module_put(info[i].ops->type->owner);
	}
	return err;
}

static int
nf_tables_delrule_one(struct nft_ctx *ctx, struct nft_rule *rule)
{
	/* You cannot delete the same rule twice */
	if (nft_rule_is_active_next(ctx->net, rule)) {
		if (nft_trans_rule_add(ctx, NFT_MSG_DELRULE, rule) == NULL)
			return -ENOMEM;
		nft_rule_disactivate_next(ctx->net, rule);
		ctx->chain->use--;
		return 0;
	}
	return -ENOENT;
}

static int nf_table_delrule_by_chain(struct nft_ctx *ctx)
{
	struct nft_rule *rule;
	int err;

	list_for_each_entry(rule, &ctx->chain->rules, list) {
		err = nf_tables_delrule_one(ctx, rule);
		if (err < 0)
			return err;
	}
	return 0;
}

static int nf_tables_delrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_chain *chain = NULL;
	struct nft_rule *rule;
	int family = nfmsg->nfgen_family, err = 0;
	struct nft_ctx ctx;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (table->flags & NFT_TABLE_INACTIVE)
		return -ENOENT;

	if (nla[NFTA_RULE_CHAIN]) {
		chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN]);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
	}

	nft_ctx_init(&ctx, skb, nlh, afi, table, chain, nla);

	if (chain) {
		if (nla[NFTA_RULE_HANDLE]) {
			rule = nf_tables_rule_lookup(chain,
						     nla[NFTA_RULE_HANDLE]);
			if (IS_ERR(rule))
				return PTR_ERR(rule);

			err = nf_tables_delrule_one(&ctx, rule);
		} else {
			err = nf_table_delrule_by_chain(&ctx);
		}
	} else {
		list_for_each_entry(chain, &table->chains, list) {
			ctx.chain = chain;
			err = nf_table_delrule_by_chain(&ctx);
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
		features &= NFT_SET_INTERVAL | NFT_SET_MAP;
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
	[NFTA_SET_TABLE]		= { .type = NLA_STRING },
	[NFTA_SET_NAME]			= { .type = NLA_STRING,
					    .len = IFNAMSIZ - 1 },
	[NFTA_SET_FLAGS]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_LEN]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_LEN]		= { .type = NLA_U32 },
	[NFTA_SET_POLICY]		= { .type = NLA_U32 },
	[NFTA_SET_DESC]			= { .type = NLA_NESTED },
	[NFTA_SET_ID]			= { .type = NLA_U32 },
};

static const struct nla_policy nft_set_desc_policy[NFTA_SET_DESC_MAX + 1] = {
	[NFTA_SET_DESC_SIZE]		= { .type = NLA_U32 },
};

static int nft_ctx_init_from_setattr(struct nft_ctx *ctx,
				     const struct sk_buff *skb,
				     const struct nlmsghdr *nlh,
				     const struct nlattr * const nla[])
{
	struct net *net = sock_net(skb->sk);
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

		table = nf_tables_table_lookup(afi, nla[NFTA_SET_TABLE]);
		if (IS_ERR(table))
			return PTR_ERR(table);
		if (table->flags & NFT_TABLE_INACTIVE)
			return -ENOENT;
	}

	nft_ctx_init(ctx, skb, nlh, afi, table, NULL, nla);
	return 0;
}

struct nft_set *nf_tables_set_lookup(const struct nft_table *table,
				     const struct nlattr *nla)
{
	struct nft_set *set;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(set, &table->sets, list) {
		if (!nla_strcmp(nla, set->name))
			return set;
	}
	return ERR_PTR(-ENOENT);
}

struct nft_set *nf_tables_set_lookup_byid(const struct net *net,
					  const struct nlattr *nla)
{
	struct nft_trans *trans;
	u32 id = ntohl(nla_get_be32(nla));

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		if (trans->msg_type == NFT_MSG_NEWSET &&
		    id == nft_trans_set_id(trans))
			return nft_trans_set(trans);
	}
	return ERR_PTR(-ENOENT);
}

static int nf_tables_set_alloc_name(struct nft_ctx *ctx, struct nft_set *set,
				    const char *name)
{
	const struct nft_set *i;
	const char *p;
	unsigned long *inuse;
	unsigned int n = 0, min = 0;

	p = strnchr(name, IFNAMSIZ, '%');
	if (p != NULL) {
		if (p[1] != 'd' || strchr(p + 2, '%'))
			return -EINVAL;

		inuse = (unsigned long *)get_zeroed_page(GFP_KERNEL);
		if (inuse == NULL)
			return -ENOMEM;
cont:
		list_for_each_entry(i, &ctx->table->sets, list) {
			int tmp;

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
	nfmsg->res_id		= 0;

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

	desc = nla_nest_start(skb, NFTA_SET_DESC);
	if (desc == NULL)
		goto nla_put_failure;
	if (set->size &&
	    nla_put_be32(skb, NFTA_SET_DESC_SIZE, htonl(set->size)))
		goto nla_put_failure;
	nla_nest_end(skb, desc);

	return nlmsg_end(skb, nlh);

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

static int nf_tables_dump_sets_table(struct nft_ctx *ctx, struct sk_buff *skb,
				     struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx = 0, s_idx = cb->args[0];

	if (cb->args[1])
		return skb->len;

	rcu_read_lock();
	cb->seq = ctx->net->nft.base_seq;

	list_for_each_entry_rcu(set, &ctx->table->sets, list) {
		if (idx < s_idx)
			goto cont;
		if (nf_tables_fill_set(skb, ctx, set, NFT_MSG_NEWSET,
				       NLM_F_MULTI) < 0) {
			cb->args[0] = idx;
			goto done;
		}
		nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
		idx++;
	}
	cb->args[1] = 1;
done:
	rcu_read_unlock();
	return skb->len;
}

static int nf_tables_dump_sets_family(struct nft_ctx *ctx, struct sk_buff *skb,
				      struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx, s_idx = cb->args[0];
	struct nft_table *table, *cur_table = (struct nft_table *)cb->args[2];

	if (cb->args[1])
		return skb->len;

	rcu_read_lock();
	cb->seq = ctx->net->nft.base_seq;

	list_for_each_entry_rcu(table, &ctx->afi->tables, list) {
		if (cur_table) {
			if (cur_table != table)
				continue;

			cur_table = NULL;
		}
		ctx->table = table;
		idx = 0;
		list_for_each_entry_rcu(set, &ctx->table->sets, list) {
			if (idx < s_idx)
				goto cont;
			if (nf_tables_fill_set(skb, ctx, set, NFT_MSG_NEWSET,
					       NLM_F_MULTI) < 0) {
				cb->args[0] = idx;
				cb->args[2] = (unsigned long) table;
				goto done;
			}
			nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
			idx++;
		}
	}
	cb->args[1] = 1;
done:
	rcu_read_unlock();
	return skb->len;
}

static int nf_tables_dump_sets_all(struct nft_ctx *ctx, struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx, s_idx = cb->args[0];
	struct nft_af_info *afi;
	struct nft_table *table, *cur_table = (struct nft_table *)cb->args[2];
	struct net *net = sock_net(skb->sk);
	int cur_family = cb->args[3];

	if (cb->args[1])
		return skb->len;

	rcu_read_lock();
	cb->seq = net->nft.base_seq;

	list_for_each_entry_rcu(afi, &net->nft.af_info, list) {
		if (cur_family) {
			if (afi->family != cur_family)
				continue;

			cur_family = 0;
		}

		list_for_each_entry_rcu(table, &afi->tables, list) {
			if (cur_table) {
				if (cur_table != table)
					continue;

				cur_table = NULL;
			}

			ctx->table = table;
			ctx->afi = afi;
			idx = 0;
			list_for_each_entry_rcu(set, &ctx->table->sets, list) {
				if (idx < s_idx)
					goto cont;
				if (nf_tables_fill_set(skb, ctx, set,
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

static int nf_tables_dump_sets(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	struct nlattr *nla[NFTA_SET_MAX + 1];
	struct nft_ctx ctx;
	int err, ret;

	err = nlmsg_parse(cb->nlh, sizeof(*nfmsg), nla, NFTA_SET_MAX,
			  nft_set_policy);
	if (err < 0)
		return err;

	err = nft_ctx_init_from_setattr(&ctx, cb->skb, cb->nlh, (void *)nla);
	if (err < 0)
		return err;

	if (ctx.table == NULL) {
		if (ctx.afi == NULL)
			ret = nf_tables_dump_sets_all(&ctx, skb, cb);
		else
			ret = nf_tables_dump_sets_family(&ctx, skb, cb);
	} else
		ret = nf_tables_dump_sets_table(&ctx, skb, cb);

	return ret;
}

#define NFT_SET_INACTIVE	(1 << 15)	/* Internal set flag */

static int nf_tables_getset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nft_set *set;
	struct nft_ctx ctx;
	struct sk_buff *skb2;
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	int err;

	/* Verify existance before starting dump */
	err = nft_ctx_init_from_setattr(&ctx, skb, nlh, nla);
	if (err < 0)
		return err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_sets,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	/* Only accept unspec with dump */
	if (nfmsg->nfgen_family == NFPROTO_UNSPEC)
		return -EAFNOSUPPORT;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (set->flags & NFT_SET_INACTIVE)
		return -ENOENT;

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
		set->flags |= NFT_SET_INACTIVE;
	}
	nft_trans_set(trans) = set;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	return 0;
}

static int nf_tables_newset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_set_ops *ops;
	struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_set *set;
	struct nft_ctx ctx;
	char name[IFNAMSIZ];
	unsigned int size;
	bool create;
	u32 ktype, dtype, flags, policy;
	struct nft_set_desc desc;
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
	if (desc.klen == 0 || desc.klen > FIELD_SIZEOF(struct nft_data, data))
		return -EINVAL;

	flags = 0;
	if (nla[NFTA_SET_FLAGS] != NULL) {
		flags = ntohl(nla_get_be32(nla[NFTA_SET_FLAGS]));
		if (flags & ~(NFT_SET_ANONYMOUS | NFT_SET_CONSTANT |
			      NFT_SET_INTERVAL | NFT_SET_MAP))
			return -EINVAL;
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
			if (desc.dlen == 0 ||
			    desc.dlen > FIELD_SIZEOF(struct nft_data, data))
				return -EINVAL;
		} else
			desc.dlen = sizeof(struct nft_data);
	} else if (flags & NFT_SET_MAP)
		return -EINVAL;

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

	table = nf_tables_table_lookup(afi, nla[NFTA_SET_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	nft_ctx_init(&ctx, skb, nlh, afi, table, NULL, nla);

	set = nf_tables_set_lookup(table, nla[NFTA_SET_NAME]);
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

	size = 0;
	if (ops->privsize != NULL)
		size = ops->privsize(nla);

	err = -ENOMEM;
	set = kzalloc(sizeof(*set) + size, GFP_KERNEL);
	if (set == NULL)
		goto err1;

	nla_strlcpy(name, nla[NFTA_SET_NAME], sizeof(set->name));
	err = nf_tables_set_alloc_name(&ctx, set, name);
	if (err < 0)
		goto err2;

	INIT_LIST_HEAD(&set->bindings);
	set->ops   = ops;
	set->ktype = ktype;
	set->klen  = desc.klen;
	set->dtype = dtype;
	set->dlen  = desc.dlen;
	set->flags = flags;
	set->size  = desc.size;

	err = ops->init(set, &desc, nla);
	if (err < 0)
		goto err2;

	err = nft_trans_set_add(&ctx, NFT_MSG_NEWSET, set);
	if (err < 0)
		goto err2;

	list_add_tail_rcu(&set->list, &table->sets);
	table->use++;
	return 0;

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

static int nf_tables_delset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_set *set;
	struct nft_ctx ctx;
	int err;

	if (nfmsg->nfgen_family == NFPROTO_UNSPEC)
		return -EAFNOSUPPORT;
	if (nla[NFTA_SET_TABLE] == NULL)
		return -EINVAL;

	err = nft_ctx_init_from_setattr(&ctx, skb, nlh, nla);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (set->flags & NFT_SET_INACTIVE)
		return -ENOENT;
	if (!list_empty(&set->bindings))
		return -EBUSY;

	err = nft_trans_set_add(&ctx, NFT_MSG_DELSET, set);
	if (err < 0)
		return err;

	list_del_rcu(&set->list);
	ctx.table->use--;
	return 0;
}

static int nf_tables_bind_check_setelem(const struct nft_ctx *ctx,
					const struct nft_set *set,
					const struct nft_set_iter *iter,
					const struct nft_set_elem *elem)
{
	enum nft_registers dreg;

	dreg = nft_type_to_reg(set->dtype);
	return nft_validate_data_load(ctx, dreg, &elem->data,
				      set->dtype == NFT_DATA_VERDICT ?
				      NFT_DATA_VERDICT : NFT_DATA_VALUE);
}

int nf_tables_bind_set(const struct nft_ctx *ctx, struct nft_set *set,
		       struct nft_set_binding *binding)
{
	struct nft_set_binding *i;
	struct nft_set_iter iter;

	if (!list_empty(&set->bindings) && set->flags & NFT_SET_ANONYMOUS)
		return -EBUSY;

	if (set->flags & NFT_SET_MAP) {
		/* If the set is already bound to the same chain all
		 * jumps are already validated for that chain.
		 */
		list_for_each_entry(i, &set->bindings, list) {
			if (i->chain == binding->chain)
				goto bind;
		}

		iter.skip 	= 0;
		iter.count	= 0;
		iter.err	= 0;
		iter.fn		= nf_tables_bind_check_setelem;

		set->ops->walk(ctx, set, &iter);
		if (iter.err < 0) {
			/* Destroy anonymous sets if binding fails */
			if (set->flags & NFT_SET_ANONYMOUS)
				nf_tables_set_destroy(ctx, set);

			return iter.err;
		}
	}
bind:
	binding->chain = ctx->chain;
	list_add_tail_rcu(&binding->list, &set->bindings);
	return 0;
}

void nf_tables_unbind_set(const struct nft_ctx *ctx, struct nft_set *set,
			  struct nft_set_binding *binding)
{
	list_del_rcu(&binding->list);

	if (list_empty(&set->bindings) && set->flags & NFT_SET_ANONYMOUS &&
	    !(set->flags & NFT_SET_INACTIVE))
		nf_tables_set_destroy(ctx, set);
}

/*
 * Set elements
 */

static const struct nla_policy nft_set_elem_policy[NFTA_SET_ELEM_MAX + 1] = {
	[NFTA_SET_ELEM_KEY]		= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_DATA]		= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_FLAGS]		= { .type = NLA_U32 },
};

static const struct nla_policy nft_set_elem_list_policy[NFTA_SET_ELEM_LIST_MAX + 1] = {
	[NFTA_SET_ELEM_LIST_TABLE]	= { .type = NLA_STRING },
	[NFTA_SET_ELEM_LIST_SET]	= { .type = NLA_STRING },
	[NFTA_SET_ELEM_LIST_ELEMENTS]	= { .type = NLA_NESTED },
	[NFTA_SET_ELEM_LIST_SET_ID]	= { .type = NLA_U32 },
};

static int nft_ctx_init_from_elemattr(struct nft_ctx *ctx,
				      const struct sk_buff *skb,
				      const struct nlmsghdr *nlh,
				      const struct nlattr * const nla[],
				      bool trans)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nft_af_info *afi;
	struct nft_table *table;
	struct net *net = sock_net(skb->sk);

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_SET_ELEM_LIST_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);
	if (!trans && (table->flags & NFT_TABLE_INACTIVE))
		return -ENOENT;

	nft_ctx_init(ctx, skb, nlh, afi, table, NULL, nla);
	return 0;
}

static int nf_tables_fill_setelem(struct sk_buff *skb,
				  const struct nft_set *set,
				  const struct nft_set_elem *elem)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_LIST_ELEM);
	if (nest == NULL)
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_SET_ELEM_KEY, &elem->key, NFT_DATA_VALUE,
			  set->klen) < 0)
		goto nla_put_failure;

	if (set->flags & NFT_SET_MAP &&
	    !(elem->flags & NFT_SET_ELEM_INTERVAL_END) &&
	    nft_data_dump(skb, NFTA_SET_ELEM_DATA, &elem->data,
			  set->dtype == NFT_DATA_VERDICT ? NFT_DATA_VERDICT : NFT_DATA_VALUE,
			  set->dlen) < 0)
		goto nla_put_failure;

	if (elem->flags != 0)
		if (nla_put_be32(skb, NFTA_SET_ELEM_FLAGS, htonl(elem->flags)))
			goto nla_put_failure;

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
				  const struct nft_set *set,
				  const struct nft_set_iter *iter,
				  const struct nft_set_elem *elem)
{
	struct nft_set_dump_args *args;

	args = container_of(iter, struct nft_set_dump_args, iter);
	return nf_tables_fill_setelem(args->skb, set, elem);
}

static int nf_tables_dump_set(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nft_set *set;
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

	err = nft_ctx_init_from_elemattr(&ctx, cb->skb, cb->nlh, (void *)nla,
					 false);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (set->flags & NFT_SET_INACTIVE)
		return -ENOENT;

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
	nfmsg->res_id       = 0;

	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_TABLE, ctx.table->name))
		goto nla_put_failure;
	if (nla_put_string(skb, NFTA_SET_ELEM_LIST_SET, set->name))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NFTA_SET_ELEM_LIST_ELEMENTS);
	if (nest == NULL)
		goto nla_put_failure;

	args.cb		= cb;
	args.skb	= skb;
	args.iter.skip	= cb->args[0];
	args.iter.count	= 0;
	args.iter.err   = 0;
	args.iter.fn	= nf_tables_dump_setelem;
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

static int nf_tables_getsetelem(struct sock *nlsk, struct sk_buff *skb,
				const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	const struct nft_set *set;
	struct nft_ctx ctx;
	int err;

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla, false);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (set->flags & NFT_SET_INACTIVE)
		return -ENOENT;

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
	nfmsg->res_id		= 0;

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

	return nlmsg_end(skb, nlh);

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

static int nft_add_set_elem(struct nft_ctx *ctx, struct nft_set *set,
			    const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	struct nft_data_desc d1, d2;
	struct nft_set_elem elem;
	struct nft_set_binding *binding;
	enum nft_registers dreg;
	struct nft_trans *trans;
	int err;

	if (set->size && set->nelems == set->size)
		return -ENFILE;

	err = nla_parse_nested(nla, NFTA_SET_ELEM_MAX, attr,
			       nft_set_elem_policy);
	if (err < 0)
		return err;

	if (nla[NFTA_SET_ELEM_KEY] == NULL)
		return -EINVAL;

	elem.flags = 0;
	if (nla[NFTA_SET_ELEM_FLAGS] != NULL) {
		elem.flags = ntohl(nla_get_be32(nla[NFTA_SET_ELEM_FLAGS]));
		if (elem.flags & ~NFT_SET_ELEM_INTERVAL_END)
			return -EINVAL;
	}

	if (set->flags & NFT_SET_MAP) {
		if (nla[NFTA_SET_ELEM_DATA] == NULL &&
		    !(elem.flags & NFT_SET_ELEM_INTERVAL_END))
			return -EINVAL;
		if (nla[NFTA_SET_ELEM_DATA] != NULL &&
		    elem.flags & NFT_SET_ELEM_INTERVAL_END)
			return -EINVAL;
	} else {
		if (nla[NFTA_SET_ELEM_DATA] != NULL)
			return -EINVAL;
	}

	err = nft_data_init(ctx, &elem.key, &d1, nla[NFTA_SET_ELEM_KEY]);
	if (err < 0)
		goto err1;
	err = -EINVAL;
	if (d1.type != NFT_DATA_VALUE || d1.len != set->klen)
		goto err2;

	err = -EEXIST;
	if (set->ops->get(set, &elem) == 0)
		goto err2;

	if (nla[NFTA_SET_ELEM_DATA] != NULL) {
		err = nft_data_init(ctx, &elem.data, &d2, nla[NFTA_SET_ELEM_DATA]);
		if (err < 0)
			goto err2;

		err = -EINVAL;
		if (set->dtype != NFT_DATA_VERDICT && d2.len != set->dlen)
			goto err3;

		dreg = nft_type_to_reg(set->dtype);
		list_for_each_entry(binding, &set->bindings, list) {
			struct nft_ctx bind_ctx = {
				.afi	= ctx->afi,
				.table	= ctx->table,
				.chain	= (struct nft_chain *)binding->chain,
			};

			err = nft_validate_data_load(&bind_ctx, dreg,
						     &elem.data, d2.type);
			if (err < 0)
				goto err3;
		}
	}

	trans = nft_trans_elem_alloc(ctx, NFT_MSG_NEWSETELEM, set);
	if (trans == NULL)
		goto err3;

	err = set->ops->insert(set, &elem);
	if (err < 0)
		goto err4;

	nft_trans_elem(trans) = elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);
	return 0;

err4:
	kfree(trans);
err3:
	if (nla[NFTA_SET_ELEM_DATA] != NULL)
		nft_data_uninit(&elem.data, d2.type);
err2:
	nft_data_uninit(&elem.key, d1.type);
err1:
	return err;
}

static int nf_tables_newsetelem(struct sock *nlsk, struct sk_buff *skb,
				const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	struct net *net = sock_net(skb->sk);
	const struct nlattr *attr;
	struct nft_set *set;
	struct nft_ctx ctx;
	int rem, err = 0;

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla, true);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
	if (IS_ERR(set)) {
		if (nla[NFTA_SET_ELEM_LIST_SET_ID]) {
			set = nf_tables_set_lookup_byid(net,
					nla[NFTA_SET_ELEM_LIST_SET_ID]);
		}
		if (IS_ERR(set))
			return PTR_ERR(set);
	}

	if (!list_empty(&set->bindings) && set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_add_set_elem(&ctx, set, attr);
		if (err < 0)
			break;

		set->nelems++;
	}
	return err;
}

static int nft_del_setelem(struct nft_ctx *ctx, struct nft_set *set,
			   const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	struct nft_data_desc desc;
	struct nft_set_elem elem;
	struct nft_trans *trans;
	int err;

	err = nla_parse_nested(nla, NFTA_SET_ELEM_MAX, attr,
			       nft_set_elem_policy);
	if (err < 0)
		goto err1;

	err = -EINVAL;
	if (nla[NFTA_SET_ELEM_KEY] == NULL)
		goto err1;

	err = nft_data_init(ctx, &elem.key, &desc, nla[NFTA_SET_ELEM_KEY]);
	if (err < 0)
		goto err1;

	err = -EINVAL;
	if (desc.type != NFT_DATA_VALUE || desc.len != set->klen)
		goto err2;

	err = set->ops->get(set, &elem);
	if (err < 0)
		goto err2;

	trans = nft_trans_elem_alloc(ctx, NFT_MSG_DELSETELEM, set);
	if (trans == NULL)
		goto err2;

	nft_trans_elem(trans) = elem;
	list_add_tail(&trans->list, &ctx->net->nft.commit_list);

	nft_data_uninit(&elem.key, NFT_DATA_VALUE);
	if (set->flags & NFT_SET_MAP)
		nft_data_uninit(&elem.data, set->dtype);

	return 0;
err2:
	nft_data_uninit(&elem.key, desc.type);
err1:
	return err;
}

static int nf_tables_delsetelem(struct sock *nlsk, struct sk_buff *skb,
				const struct nlmsghdr *nlh,
				const struct nlattr * const nla[])
{
	const struct nlattr *attr;
	struct nft_set *set;
	struct nft_ctx ctx;
	int rem, err = 0;

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla, false);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (!list_empty(&set->bindings) && set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_del_setelem(&ctx, set, attr);
		if (err < 0)
			break;

		set->nelems--;
	}
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

/* Schedule objects for release via rcu to make sure no packets are accesing
 * removed rules.
 */
static void nf_tables_commit_release_rcu(struct rcu_head *rt)
{
	struct nft_trans *trans = container_of(rt, struct nft_trans, rcu_head);

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
	}
	kfree(trans);
}

static int nf_tables_commit(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);
	struct nft_trans *trans, *next;
	struct nft_set *set;

	/* Bump generation counter, invalidate any dump in progress */
	while (++net->nft.base_seq == 0);

	/* A new generation has just started */
	net->nft.gencursor = gencursor_next(net);

	/* Make sure all packets have left the previous generation before
	 * purging old rules.
	 */
	synchronize_rcu();

	list_for_each_entry_safe(trans, next, &net->nft.commit_list, list) {
		switch (trans->msg_type) {
		case NFT_MSG_NEWTABLE:
			if (nft_trans_table_update(trans)) {
				if (!nft_trans_table_enable(trans)) {
					nf_tables_table_disable(trans->ctx.afi,
								trans->ctx.table);
					trans->ctx.table->flags |= NFT_TABLE_F_DORMANT;
				}
			} else {
				trans->ctx.table->flags &= ~NFT_TABLE_INACTIVE;
			}
			nf_tables_table_notify(&trans->ctx, NFT_MSG_NEWTABLE);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELTABLE:
			nf_tables_table_notify(&trans->ctx, NFT_MSG_DELTABLE);
			break;
		case NFT_MSG_NEWCHAIN:
			if (nft_trans_chain_update(trans))
				nft_chain_commit_update(trans);
			else
				trans->ctx.chain->flags &= ~NFT_CHAIN_INACTIVE;

			nf_tables_chain_notify(&trans->ctx, NFT_MSG_NEWCHAIN);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELCHAIN:
			nf_tables_chain_notify(&trans->ctx, NFT_MSG_DELCHAIN);
			if (!(trans->ctx.table->flags & NFT_TABLE_F_DORMANT) &&
			    trans->ctx.chain->flags & NFT_BASE_CHAIN) {
				nf_unregister_hooks(nft_base_chain(trans->ctx.chain)->ops,
						    trans->ctx.afi->nops);
			}
			break;
		case NFT_MSG_NEWRULE:
			nft_rule_clear(trans->ctx.net, nft_trans_rule(trans));
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
			nft_trans_set(trans)->flags &= ~NFT_SET_INACTIVE;
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
			nf_tables_set_notify(&trans->ctx, nft_trans_set(trans),
					     NFT_MSG_DELSET, GFP_KERNEL);
			break;
		case NFT_MSG_NEWSETELEM:
			nf_tables_setelem_notify(&trans->ctx,
						 nft_trans_elem_set(trans),
						 &nft_trans_elem(trans),
						 NFT_MSG_NEWSETELEM, 0);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELSETELEM:
			nf_tables_setelem_notify(&trans->ctx,
						 nft_trans_elem_set(trans),
						 &nft_trans_elem(trans),
						 NFT_MSG_DELSETELEM, 0);
			set = nft_trans_elem_set(trans);
			set->ops->get(set, &nft_trans_elem(trans));
			set->ops->remove(set, &nft_trans_elem(trans));
			nft_trans_destroy(trans);
			break;
		}
	}

	list_for_each_entry_safe(trans, next, &net->nft.commit_list, list) {
		list_del(&trans->list);
		trans->ctx.nla = NULL;
		call_rcu(&trans->rcu_head, nf_tables_commit_release_rcu);
	}

	return 0;
}

/* Schedule objects for release via rcu to make sure no packets are accesing
 * aborted rules.
 */
static void nf_tables_abort_release_rcu(struct rcu_head *rt)
{
	struct nft_trans *trans = container_of(rt, struct nft_trans, rcu_head);

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
	}
	kfree(trans);
}

static int nf_tables_abort(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);
	struct nft_trans *trans, *next;
	struct nft_set *set;

	list_for_each_entry_safe(trans, next, &net->nft.commit_list, list) {
		switch (trans->msg_type) {
		case NFT_MSG_NEWTABLE:
			if (nft_trans_table_update(trans)) {
				if (nft_trans_table_enable(trans)) {
					nf_tables_table_disable(trans->ctx.afi,
								trans->ctx.table);
					trans->ctx.table->flags |= NFT_TABLE_F_DORMANT;
				}
				nft_trans_destroy(trans);
			} else {
				list_del_rcu(&trans->ctx.table->list);
			}
			break;
		case NFT_MSG_DELTABLE:
			list_add_tail_rcu(&trans->ctx.table->list,
					  &trans->ctx.afi->tables);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWCHAIN:
			if (nft_trans_chain_update(trans)) {
				if (nft_trans_chain_stats(trans))
					free_percpu(nft_trans_chain_stats(trans));

				nft_trans_destroy(trans);
			} else {
				trans->ctx.table->use--;
				list_del_rcu(&trans->ctx.chain->list);
				if (!(trans->ctx.table->flags & NFT_TABLE_F_DORMANT) &&
				    trans->ctx.chain->flags & NFT_BASE_CHAIN) {
					nf_unregister_hooks(nft_base_chain(trans->ctx.chain)->ops,
							    trans->ctx.afi->nops);
				}
			}
			break;
		case NFT_MSG_DELCHAIN:
			trans->ctx.table->use++;
			list_add_tail_rcu(&trans->ctx.chain->list,
					  &trans->ctx.table->chains);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWRULE:
			trans->ctx.chain->use--;
			list_del_rcu(&nft_trans_rule(trans)->list);
			break;
		case NFT_MSG_DELRULE:
			trans->ctx.chain->use++;
			nft_rule_clear(trans->ctx.net, nft_trans_rule(trans));
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWSET:
			trans->ctx.table->use--;
			list_del_rcu(&nft_trans_set(trans)->list);
			break;
		case NFT_MSG_DELSET:
			trans->ctx.table->use++;
			list_add_tail_rcu(&nft_trans_set(trans)->list,
					  &trans->ctx.table->sets);
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_NEWSETELEM:
			nft_trans_elem_set(trans)->nelems--;
			set = nft_trans_elem_set(trans);
			set->ops->get(set, &nft_trans_elem(trans));
			set->ops->remove(set, &nft_trans_elem(trans));
			nft_trans_destroy(trans);
			break;
		case NFT_MSG_DELSETELEM:
			nft_trans_elem_set(trans)->nelems++;
			nft_trans_destroy(trans);
			break;
		}
	}

	list_for_each_entry_safe_reverse(trans, next,
					 &net->nft.commit_list, list) {
		list_del(&trans->list);
		trans->ctx.nla = NULL;
		call_rcu(&trans->rcu_head, nf_tables_abort_release_rcu);
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
					const struct nft_set *set,
					const struct nft_set_iter *iter,
					const struct nft_set_elem *elem)
{
	if (elem->flags & NFT_SET_ELEM_INTERVAL_END)
		return 0;

	switch (elem->data.verdict) {
	case NFT_JUMP:
	case NFT_GOTO:
		return nf_tables_check_loops(ctx, elem->data.chain);
	default:
		return 0;
	}
}

static int nf_tables_check_loops(const struct nft_ctx *ctx,
				 const struct nft_chain *chain)
{
	const struct nft_rule *rule;
	const struct nft_expr *expr, *last;
	const struct nft_set *set;
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

			switch (data->verdict) {
			case NFT_JUMP:
			case NFT_GOTO:
				err = nf_tables_check_loops(ctx, data->chain);
				if (err < 0)
					return err;
			default:
				break;
			}
		}
	}

	list_for_each_entry(set, &ctx->table->sets, list) {
		if (!(set->flags & NFT_SET_MAP) ||
		    set->dtype != NFT_DATA_VERDICT)
			continue;

		list_for_each_entry(binding, &set->bindings, list) {
			if (binding->chain != chain)
				continue;

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
 *	nft_validate_input_register - validate an expressions' input register
 *
 *	@reg: the register number
 *
 * 	Validate that the input register is one of the general purpose
 * 	registers.
 */
int nft_validate_input_register(enum nft_registers reg)
{
	if (reg <= NFT_REG_VERDICT)
		return -EINVAL;
	if (reg > NFT_REG_MAX)
		return -ERANGE;
	return 0;
}
EXPORT_SYMBOL_GPL(nft_validate_input_register);

/**
 *	nft_validate_output_register - validate an expressions' output register
 *
 *	@reg: the register number
 *
 * 	Validate that the output register is one of the general purpose
 * 	registers or the verdict register.
 */
int nft_validate_output_register(enum nft_registers reg)
{
	if (reg < NFT_REG_VERDICT)
		return -EINVAL;
	if (reg > NFT_REG_MAX)
		return -ERANGE;
	return 0;
}
EXPORT_SYMBOL_GPL(nft_validate_output_register);

/**
 *	nft_validate_data_load - validate an expressions' data load
 *
 *	@ctx: context of the expression performing the load
 * 	@reg: the destination register number
 * 	@data: the data to load
 * 	@type: the data type
 *
 * 	Validate that a data load uses the appropriate data type for
 * 	the destination register. A value of NULL for the data means
 * 	that its runtime gathered data, which is always of type
 * 	NFT_DATA_VALUE.
 */
int nft_validate_data_load(const struct nft_ctx *ctx, enum nft_registers reg,
			   const struct nft_data *data,
			   enum nft_data_types type)
{
	int err;

	switch (reg) {
	case NFT_REG_VERDICT:
		if (data == NULL || type != NFT_DATA_VERDICT)
			return -EINVAL;

		if (data->verdict == NFT_GOTO || data->verdict == NFT_JUMP) {
			err = nf_tables_check_loops(ctx, data->chain);
			if (err < 0)
				return err;

			if (ctx->chain->level + 1 > data->chain->level) {
				if (ctx->chain->level + 1 == NFT_JUMP_STACK_SIZE)
					return -EMLINK;
				data->chain->level = ctx->chain->level + 1;
			}
		}

		return 0;
	default:
		if (data != NULL && type != NFT_DATA_VALUE)
			return -EINVAL;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(nft_validate_data_load);

static const struct nla_policy nft_verdict_policy[NFTA_VERDICT_MAX + 1] = {
	[NFTA_VERDICT_CODE]	= { .type = NLA_U32 },
	[NFTA_VERDICT_CHAIN]	= { .type = NLA_STRING,
				    .len = NFT_CHAIN_MAXNAMELEN - 1 },
};

static int nft_verdict_init(const struct nft_ctx *ctx, struct nft_data *data,
			    struct nft_data_desc *desc, const struct nlattr *nla)
{
	struct nlattr *tb[NFTA_VERDICT_MAX + 1];
	struct nft_chain *chain;
	int err;

	err = nla_parse_nested(tb, NFTA_VERDICT_MAX, nla, nft_verdict_policy);
	if (err < 0)
		return err;

	if (!tb[NFTA_VERDICT_CODE])
		return -EINVAL;
	data->verdict = ntohl(nla_get_be32(tb[NFTA_VERDICT_CODE]));

	switch (data->verdict) {
	default:
		switch (data->verdict & NF_VERDICT_MASK) {
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
		desc->len = sizeof(data->verdict);
		break;
	case NFT_JUMP:
	case NFT_GOTO:
		if (!tb[NFTA_VERDICT_CHAIN])
			return -EINVAL;
		chain = nf_tables_chain_lookup(ctx->table,
					       tb[NFTA_VERDICT_CHAIN]);
		if (IS_ERR(chain))
			return PTR_ERR(chain);
		if (chain->flags & NFT_BASE_CHAIN)
			return -EOPNOTSUPP;

		chain->use++;
		data->chain = chain;
		desc->len = sizeof(data);
		break;
	}

	desc->type = NFT_DATA_VERDICT;
	return 0;
}

static void nft_verdict_uninit(const struct nft_data *data)
{
	switch (data->verdict) {
	case NFT_JUMP:
	case NFT_GOTO:
		data->chain->use--;
		break;
	}
}

static int nft_verdict_dump(struct sk_buff *skb, const struct nft_data *data)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_DATA_VERDICT);
	if (!nest)
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_VERDICT_CODE, htonl(data->verdict)))
		goto nla_put_failure;

	switch (data->verdict) {
	case NFT_JUMP:
	case NFT_GOTO:
		if (nla_put_string(skb, NFTA_VERDICT_CHAIN, data->chain->name))
			goto nla_put_failure;
	}
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	return -1;
}

static int nft_value_init(const struct nft_ctx *ctx, struct nft_data *data,
			  struct nft_data_desc *desc, const struct nlattr *nla)
{
	unsigned int len;

	len = nla_len(nla);
	if (len == 0)
		return -EINVAL;
	if (len > sizeof(data->data))
		return -EOVERFLOW;

	nla_memcpy(data->data, nla, sizeof(data->data));
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
	[NFTA_DATA_VALUE]	= { .type = NLA_BINARY,
				    .len  = FIELD_SIZEOF(struct nft_data, data) },
	[NFTA_DATA_VERDICT]	= { .type = NLA_NESTED },
};

/**
 *	nft_data_init - parse nf_tables data netlink attributes
 *
 *	@ctx: context of the expression using the data
 *	@data: destination struct nft_data
 *	@desc: data description
 *	@nla: netlink attribute containing data
 *
 *	Parse the netlink data attributes and initialize a struct nft_data.
 *	The type and length of data are returned in the data description.
 *
 *	The caller can indicate that it only wants to accept data of type
 *	NFT_DATA_VALUE by passing NULL for the ctx argument.
 */
int nft_data_init(const struct nft_ctx *ctx, struct nft_data *data,
		  struct nft_data_desc *desc, const struct nlattr *nla)
{
	struct nlattr *tb[NFTA_DATA_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_DATA_MAX, nla, nft_data_policy);
	if (err < 0)
		return err;

	if (tb[NFTA_DATA_VALUE])
		return nft_value_init(ctx, data, desc, tb[NFTA_DATA_VALUE]);
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
	switch (type) {
	case NFT_DATA_VALUE:
		return;
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
		err = nft_verdict_dump(skb, data);
		break;
	default:
		err = -EINVAL;
		WARN_ON(1);
	}

	nla_nest_end(skb, nest);
	return err;
}
EXPORT_SYMBOL_GPL(nft_data_dump);

static int nf_tables_init_net(struct net *net)
{
	INIT_LIST_HEAD(&net->nft.af_info);
	INIT_LIST_HEAD(&net->nft.commit_list);
	net->nft.base_seq = 1;
	return 0;
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
	nf_tables_core_module_exit();
	kfree(info);
}

module_init(nf_tables_module_init);
module_exit(nf_tables_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_NFTABLES);
