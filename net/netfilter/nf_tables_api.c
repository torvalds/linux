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
	list_add_tail(&afi->list, &net->nft.af_info);
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
	list_del(&afi->list);
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

static struct nf_chain_type *chain_type[AF_MAX][NFT_CHAIN_T_MAX];

static int __nf_tables_chain_type_lookup(int family, const struct nlattr *nla)
{
	int i;

	for (i=0; i<NFT_CHAIN_T_MAX; i++) {
		if (chain_type[family][i] != NULL &&
		    !nla_strcmp(nla, chain_type[family][i]->name))
			return i;
	}
	return -1;
}

static int nf_tables_chain_type_lookup(const struct nft_af_info *afi,
				       const struct nlattr *nla,
				       bool autoload)
{
	int type;

	type = __nf_tables_chain_type_lookup(afi->family, nla);
#ifdef CONFIG_MODULES
	if (type < 0 && autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-chain-%u-%*.s", afi->family,
			       nla_len(nla)-1, (const char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		type = __nf_tables_chain_type_lookup(afi->family, nla);
	}
#endif
	return type;
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
	    nla_put_be32(skb, NFTA_TABLE_FLAGS, htonl(table->flags)))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_table_notify(const struct sk_buff *oskb,
				  const struct nlmsghdr *nlh,
				  const struct nft_table *table,
				  int event, int family)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	u32 seq = nlh ? nlh->nlmsg_seq : 0;
	struct net *net = oskb ? sock_net(oskb->sk) : &init_net;
	bool report;
	int err;

	report = nlh ? nlmsg_report(nlh) : false;
	if (!report && !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_table_info(skb, portid, seq, event, 0,
					family, table);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, report,
			     GFP_KERNEL);
err:
	if (err < 0)
		nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, err);
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

	list_for_each_entry(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry(table, &afi->tables, list) {
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
cont:
			idx++;
		}
	}
done:
	cb->args[0] = idx;
	return skb->len;
}

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

static int nf_tables_table_enable(struct nft_table *table)
{
	struct nft_chain *chain;
	int err, i = 0;

	list_for_each_entry(chain, &table->chains, list) {
		err = nf_register_hook(&nft_base_chain(chain)->ops);
		if (err < 0)
			goto err;

		i++;
	}
	return 0;
err:
	list_for_each_entry(chain, &table->chains, list) {
		if (i-- <= 0)
			break;

		nf_unregister_hook(&nft_base_chain(chain)->ops);
	}
	return err;
}

static int nf_tables_table_disable(struct nft_table *table)
{
	struct nft_chain *chain;

	list_for_each_entry(chain, &table->chains, list)
		nf_unregister_hook(&nft_base_chain(chain)->ops);

	return 0;
}

static int nf_tables_updtable(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[],
			      struct nft_af_info *afi, struct nft_table *table)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	int family = nfmsg->nfgen_family, ret = 0;

	if (nla[NFTA_TABLE_FLAGS]) {
		__be32 flags;

		flags = ntohl(nla_get_be32(nla[NFTA_TABLE_FLAGS]));
		if (flags & ~NFT_TABLE_F_DORMANT)
			return -EINVAL;

		if ((flags & NFT_TABLE_F_DORMANT) &&
		    !(table->flags & NFT_TABLE_F_DORMANT)) {
			ret = nf_tables_table_disable(table);
			if (ret >= 0)
				table->flags |= NFT_TABLE_F_DORMANT;
		} else if (!(flags & NFT_TABLE_F_DORMANT) &&
			   table->flags & NFT_TABLE_F_DORMANT) {
			ret = nf_tables_table_enable(table);
			if (ret >= 0)
				table->flags &= ~NFT_TABLE_F_DORMANT;
		}
		if (ret < 0)
			goto err;
	}

	nf_tables_table_notify(skb, nlh, table, NFT_MSG_NEWTABLE, family);
err:
	return ret;
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
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;
		return nf_tables_updtable(nlsk, skb, nlh, nla, afi, table);
	}

	table = kzalloc(sizeof(*table) + nla_len(name), GFP_KERNEL);
	if (table == NULL)
		return -ENOMEM;

	nla_strlcpy(table->name, name, nla_len(name));
	INIT_LIST_HEAD(&table->chains);
	INIT_LIST_HEAD(&table->sets);

	if (nla[NFTA_TABLE_FLAGS]) {
		__be32 flags;

		flags = ntohl(nla_get_be32(nla[NFTA_TABLE_FLAGS]));
		if (flags & ~NFT_TABLE_F_DORMANT) {
			kfree(table);
			return -EINVAL;
		}

		table->flags |= flags;
	}

	list_add_tail(&table->list, &afi->tables);
	nf_tables_table_notify(skb, nlh, table, NFT_MSG_NEWTABLE, family);
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
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (table->use)
		return -EBUSY;

	list_del(&table->list);
	nf_tables_table_notify(skb, nlh, table, NFT_MSG_DELTABLE, family);
	kfree(table);
	return 0;
}

int nft_register_chain_type(struct nf_chain_type *ctype)
{
	int err = 0;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	if (chain_type[ctype->family][ctype->type] != NULL) {
		err = -EBUSY;
		goto out;
	}

	if (!try_module_get(ctype->me))
		goto out;

	chain_type[ctype->family][ctype->type] = ctype;
out:
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return err;
}
EXPORT_SYMBOL_GPL(nft_register_chain_type);

void nft_unregister_chain_type(struct nf_chain_type *ctype)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	chain_type[ctype->family][ctype->type] = NULL;
	module_put(ctype->me);
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
	[NFTA_CHAIN_TYPE]	= { .type = NLA_NUL_STRING },
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
	int cpu;

	memset(&total, 0, sizeof(total));
	for_each_possible_cpu(cpu) {
		cpu_stats = per_cpu_ptr(stats, cpu);
		total.pkts += cpu_stats->pkts;
		total.bytes += cpu_stats->bytes;
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
		const struct nf_hook_ops *ops = &basechain->ops;
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

		if (nla_put_string(skb, NFTA_CHAIN_TYPE,
			chain_type[ops->pf][nft_base_chain(chain)->type]->name))
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

static int nf_tables_chain_notify(const struct sk_buff *oskb,
				  const struct nlmsghdr *nlh,
				  const struct nft_table *table,
				  const struct nft_chain *chain,
				  int event, int family)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	struct net *net = oskb ? sock_net(oskb->sk) : &init_net;
	u32 seq = nlh ? nlh->nlmsg_seq : 0;
	bool report;
	int err;

	report = nlh ? nlmsg_report(nlh) : false;
	if (!report && !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_chain_info(skb, portid, seq, event, 0, family,
					table, chain);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, report,
			     GFP_KERNEL);
err:
	if (err < 0)
		nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, err);
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

	list_for_each_entry(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry(table, &afi->tables, list) {
			list_for_each_entry(chain, &table->chains, list) {
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
cont:
				idx++;
			}
		}
	}
done:
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

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

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

static int
nf_tables_chain_policy(struct nft_base_chain *chain, const struct nlattr *attr)
{
	switch (ntohl(nla_get_be32(attr))) {
	case NF_DROP:
		chain->policy = NF_DROP;
		break;
	case NF_ACCEPT:
		chain->policy = NF_ACCEPT;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct nla_policy nft_counter_policy[NFTA_COUNTER_MAX + 1] = {
	[NFTA_COUNTER_PACKETS]	= { .type = NLA_U64 },
	[NFTA_COUNTER_BYTES]	= { .type = NLA_U64 },
};

static int
nf_tables_counters(struct nft_base_chain *chain, const struct nlattr *attr)
{
	struct nlattr *tb[NFTA_COUNTER_MAX+1];
	struct nft_stats __percpu *newstats;
	struct nft_stats *stats;
	int err;

	err = nla_parse_nested(tb, NFTA_COUNTER_MAX, attr, nft_counter_policy);
	if (err < 0)
		return err;

	if (!tb[NFTA_COUNTER_BYTES] || !tb[NFTA_COUNTER_PACKETS])
		return -EINVAL;

	newstats = alloc_percpu(struct nft_stats);
	if (newstats == NULL)
		return -ENOMEM;

	/* Restore old counters on this cpu, no problem. Per-cpu statistics
	 * are not exposed to userspace.
	 */
	stats = this_cpu_ptr(newstats);
	stats->bytes = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_BYTES]));
	stats->pkts = be64_to_cpu(nla_get_be64(tb[NFTA_COUNTER_PACKETS]));

	if (chain->stats) {
		/* nfnl_lock is held, add some nfnl function for this, later */
		struct nft_stats __percpu *oldstats =
			rcu_dereference_protected(chain->stats, 1);

		rcu_assign_pointer(chain->stats, newstats);
		synchronize_rcu();
		free_percpu(oldstats);
	} else
		rcu_assign_pointer(chain->stats, newstats);

	return 0;
}

static int nf_tables_newchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr * uninitialized_var(name);
	const struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_base_chain *basechain = NULL;
	struct nlattr *ha[NFTA_HOOK_MAX + 1];
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;
	u64 handle = 0;
	int err;
	bool create;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(net, family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (table->use == UINT_MAX)
		return -EOVERFLOW;

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

	if (chain != NULL) {
		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (nlh->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		if (nla[NFTA_CHAIN_HANDLE] && name &&
		    !IS_ERR(nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME])))
			return -EEXIST;

		if (nla[NFTA_CHAIN_POLICY]) {
			if (!(chain->flags & NFT_BASE_CHAIN))
				return -EOPNOTSUPP;

			err = nf_tables_chain_policy(nft_base_chain(chain),
						     nla[NFTA_CHAIN_POLICY]);
			if (err < 0)
				return err;
		}

		if (nla[NFTA_CHAIN_COUNTERS]) {
			if (!(chain->flags & NFT_BASE_CHAIN))
				return -EOPNOTSUPP;

			err = nf_tables_counters(nft_base_chain(chain),
						 nla[NFTA_CHAIN_COUNTERS]);
			if (err < 0)
				return err;
		}

		if (nla[NFTA_CHAIN_HANDLE] && name)
			nla_strlcpy(chain->name, name, NFT_CHAIN_MAXNAMELEN);

		goto notify;
	}

	if (nla[NFTA_CHAIN_HOOK]) {
		struct nf_hook_ops *ops;
		nf_hookfn *hookfn;
		u32 hooknum;
		int type = NFT_CHAIN_T_DEFAULT;

		if (nla[NFTA_CHAIN_TYPE]) {
			type = nf_tables_chain_type_lookup(afi,
							   nla[NFTA_CHAIN_TYPE],
							   create);
			if (type < 0)
				return -ENOENT;
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

		hookfn = chain_type[family][type]->fn[hooknum];
		if (hookfn == NULL)
			return -EOPNOTSUPP;

		basechain = kzalloc(sizeof(*basechain), GFP_KERNEL);
		if (basechain == NULL)
			return -ENOMEM;

		basechain->type = type;
		chain = &basechain->chain;

		ops = &basechain->ops;
		ops->pf		= family;
		ops->owner	= afi->owner;
		ops->hooknum	= ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM]));
		ops->priority	= ntohl(nla_get_be32(ha[NFTA_HOOK_PRIORITY]));
		ops->priv	= chain;
		ops->hook       = hookfn;
		if (afi->hooks[ops->hooknum])
			ops->hook = afi->hooks[ops->hooknum];

		chain->flags |= NFT_BASE_CHAIN;

		if (nla[NFTA_CHAIN_POLICY]) {
			err = nf_tables_chain_policy(basechain,
						     nla[NFTA_CHAIN_POLICY]);
			if (err < 0) {
				free_percpu(basechain->stats);
				kfree(basechain);
				return err;
			}
		} else
			basechain->policy = NF_ACCEPT;

		if (nla[NFTA_CHAIN_COUNTERS]) {
			err = nf_tables_counters(basechain,
						 nla[NFTA_CHAIN_COUNTERS]);
			if (err < 0) {
				free_percpu(basechain->stats);
				kfree(basechain);
				return err;
			}
		} else {
			struct nft_stats __percpu *newstats;

			newstats = alloc_percpu(struct nft_stats);
			if (newstats == NULL)
				return -ENOMEM;

			rcu_assign_pointer(nft_base_chain(chain)->stats,
					   newstats);
		}
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
		err = nf_register_hook(&nft_base_chain(chain)->ops);
		if (err < 0) {
			free_percpu(basechain->stats);
			kfree(basechain);
			return err;
		}
	}
	list_add_tail(&chain->list, &table->chains);
	table->use++;
notify:
	nf_tables_chain_notify(skb, nlh, table, chain, NFT_MSG_NEWCHAIN,
			       family);
	return 0;
}

static void nf_tables_rcu_chain_destroy(struct rcu_head *head)
{
	struct nft_chain *chain = container_of(head, struct nft_chain, rcu_head);

	BUG_ON(chain->use > 0);

	if (chain->flags & NFT_BASE_CHAIN) {
		free_percpu(nft_base_chain(chain)->stats);
		kfree(nft_base_chain(chain));
	} else
		kfree(chain);
}

static int nf_tables_delchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct net *net = sock_net(skb->sk);
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(net, family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	if (!list_empty(&chain->rules))
		return -EBUSY;

	list_del(&chain->list);
	table->use--;

	if (!(table->flags & NFT_TABLE_F_DORMANT) &&
	    chain->flags & NFT_BASE_CHAIN)
		nf_unregister_hook(&nft_base_chain(chain)->ops);

	nf_tables_chain_notify(skb, nlh, table, chain, NFT_MSG_DELCHAIN,
			       family);

	/* Make sure all rule references are gone before this is released */
	call_rcu(&chain->rcu_head, nf_tables_rcu_chain_destroy);
	return 0;
}

static void nft_ctx_init(struct nft_ctx *ctx,
			 const struct sk_buff *skb,
			 const struct nlmsghdr *nlh,
			 const struct nft_af_info *afi,
			 const struct nft_table *table,
			 const struct nft_chain *chain,
			 const struct nlattr * const *nla)
{
	ctx->net   = sock_net(skb->sk);
	ctx->skb   = skb;
	ctx->nlh   = nlh;
	ctx->afi   = afi;
	ctx->table = table;
	ctx->chain = chain;
	ctx->nla   = nla;
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
	list_add_tail(&type->list, &nf_tables_expressions);
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
	list_del(&type->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_expr);

static const struct nft_expr_type *__nft_expr_type_get(struct nlattr *nla)
{
	const struct nft_expr_type *type;

	list_for_each_entry(type, &nf_tables_expressions, list) {
		if (!nla_strcmp(nla, type->name))
			return type;
	}
	return NULL;
}

static const struct nft_expr_type *nft_expr_type_get(struct nlattr *nla)
{
	const struct nft_expr_type *type;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	type = __nft_expr_type_get(nla);
	if (type != NULL && try_module_get(type->owner))
		return type;

#ifdef CONFIG_MODULES
	if (type == NULL) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-expr-%.*s",
			       nla_len(nla), (char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_expr_type_get(nla))
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

	type = nft_expr_type_get(tb[NFTA_EXPR_NAME]);
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

static void nf_tables_expr_destroy(struct nft_expr *expr)
{
	if (expr->ops->destroy)
		expr->ops->destroy(expr);
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

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_rule_notify(const struct sk_buff *oskb,
				 const struct nlmsghdr *nlh,
				 const struct nft_table *table,
				 const struct nft_chain *chain,
				 const struct nft_rule *rule,
				 int event, u32 flags, int family)
{
	struct sk_buff *skb;
	u32 portid = NETLINK_CB(oskb).portid;
	struct net *net = oskb ? sock_net(oskb->sk) : &init_net;
	u32 seq = nlh->nlmsg_seq;
	bool report;
	int err;

	report = nlmsg_report(nlh);
	if (!report && !nfnetlink_has_listeners(net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_rule_info(skb, portid, seq, event, flags,
				       family, table, chain, rule);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, net, portid, NFNLGRP_NFTABLES, report,
			     GFP_KERNEL);
err:
	if (err < 0)
		nfnetlink_set_err(net, portid, NFNLGRP_NFTABLES, err);
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
	u8 genctr = ACCESS_ONCE(net->nft.genctr);
	u8 gencursor = ACCESS_ONCE(net->nft.gencursor);

	list_for_each_entry(afi, &net->nft.af_info, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry(table, &afi->tables, list) {
			list_for_each_entry(chain, &table->chains, list) {
				list_for_each_entry(rule, &chain->rules, list) {
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
cont:
					idx++;
				}
			}
		}
	}
done:
	/* Invalidate this dump, a transition to the new generation happened */
	if (gencursor != net->nft.gencursor || genctr != net->nft.genctr)
		return -EBUSY;

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

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

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

static void nf_tables_rcu_rule_destroy(struct rcu_head *head)
{
	struct nft_rule *rule = container_of(head, struct nft_rule, rcu_head);
	struct nft_expr *expr;

	/*
	 * Careful: some expressions might not be initialized in case this
	 * is called on error from nf_tables_newrule().
	 */
	expr = nft_expr_first(rule);
	while (expr->ops && expr != nft_expr_last(rule)) {
		nf_tables_expr_destroy(expr);
		expr = nft_expr_next(expr);
	}
	kfree(rule);
}

static void nf_tables_rule_destroy(struct nft_rule *rule)
{
	call_rcu(&rule->rcu_head, nf_tables_rcu_rule_destroy);
}

#define NFT_RULE_MAXEXPRS	128

static struct nft_expr_info *info;

static struct nft_rule_trans *
nf_tables_trans_add(struct nft_rule *rule, const struct nft_ctx *ctx)
{
	struct nft_rule_trans *rupd;

	rupd = kmalloc(sizeof(struct nft_rule_trans), GFP_KERNEL);
	if (rupd == NULL)
	       return NULL;

	rupd->chain = ctx->chain;
	rupd->table = ctx->table;
	rupd->rule = rule;
	rupd->family = ctx->afi->family;
	rupd->nlh = ctx->nlh;
	list_add_tail(&rupd->list, &ctx->net->nft.commit_list);

	return rupd;
}

static int nf_tables_newrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule, *old_rule = NULL;
	struct nft_rule_trans *repl = NULL;
	struct nft_expr *expr;
	struct nft_ctx ctx;
	struct nlattr *tmp;
	unsigned int size, i, n;
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

	err = -ENOMEM;
	rule = kzalloc(sizeof(*rule) + size, GFP_KERNEL);
	if (rule == NULL)
		goto err1;

	nft_rule_activate_next(net, rule);

	rule->handle = handle;
	rule->dlen   = size;

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
			repl = nf_tables_trans_add(old_rule, &ctx);
			if (repl == NULL) {
				err = -ENOMEM;
				goto err2;
			}
			nft_rule_disactivate_next(net, old_rule);
			list_add_tail(&rule->list, &old_rule->list);
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

	if (nf_tables_trans_add(rule, &ctx) == NULL) {
		err = -ENOMEM;
		goto err3;
	}
	return 0;

err3:
	list_del_rcu(&rule->list);
	if (repl) {
		list_del_rcu(&repl->rule->list);
		list_del(&repl->list);
		nft_rule_clear(net, repl->rule);
		kfree(repl);
	}
err2:
	nf_tables_rule_destroy(rule);
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
		if (nf_tables_trans_add(rule, ctx) == NULL)
			return -ENOMEM;
		nft_rule_disactivate_next(ctx->net, rule);
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
	const struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	const struct nft_table *table;
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

static int nf_tables_commit(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);
	struct nft_rule_trans *rupd, *tmp;

	/* Bump generation counter, invalidate any dump in progress */
	net->nft.genctr++;

	/* A new generation has just started */
	net->nft.gencursor = gencursor_next(net);

	/* Make sure all packets have left the previous generation before
	 * purging old rules.
	 */
	synchronize_rcu();

	list_for_each_entry_safe(rupd, tmp, &net->nft.commit_list, list) {
		/* Delete this rule from the dirty list */
		list_del(&rupd->list);

		/* This rule was inactive in the past and just became active.
		 * Clear the next bit of the genmask since its meaning has
		 * changed, now it is the future.
		 */
		if (nft_rule_is_active(net, rupd->rule)) {
			nft_rule_clear(net, rupd->rule);
			nf_tables_rule_notify(skb, rupd->nlh, rupd->table,
					      rupd->chain, rupd->rule,
					      NFT_MSG_NEWRULE, 0,
					      rupd->family);
			kfree(rupd);
			continue;
		}

		/* This rule is in the past, get rid of it */
		list_del_rcu(&rupd->rule->list);
		nf_tables_rule_notify(skb, rupd->nlh, rupd->table, rupd->chain,
				      rupd->rule, NFT_MSG_DELRULE, 0,
				      rupd->family);
		nf_tables_rule_destroy(rupd->rule);
		kfree(rupd);
	}

	return 0;
}

static int nf_tables_abort(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);
	struct nft_rule_trans *rupd, *tmp;

	list_for_each_entry_safe(rupd, tmp, &net->nft.commit_list, list) {
		/* Delete all rules from the dirty list */
		list_del(&rupd->list);

		if (!nft_rule_is_active_next(net, rupd->rule)) {
			nft_rule_clear(net, rupd->rule);
			kfree(rupd);
			continue;
		}

		/* This rule is inactive, get rid of it */
		list_del_rcu(&rupd->rule->list);
		nf_tables_rule_destroy(rupd->rule);
		kfree(rupd);
	}
	return 0;
}

/*
 * Sets
 */

static LIST_HEAD(nf_tables_set_ops);

int nft_register_set(struct nft_set_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail(&ops->list, &nf_tables_set_ops);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_set);

void nft_unregister_set(struct nft_set_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del(&ops->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_set);

static const struct nft_set_ops *nft_select_set_ops(const struct nlattr * const nla[])
{
	const struct nft_set_ops *ops;
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

	// FIXME: implement selection properly
	list_for_each_entry(ops, &nf_tables_set_ops, list) {
		if ((ops->features & features) != features)
			continue;
		if (!try_module_get(ops->owner))
			continue;
		return ops;
	}

	return ERR_PTR(-EOPNOTSUPP);
}

static const struct nla_policy nft_set_policy[NFTA_SET_MAX + 1] = {
	[NFTA_SET_TABLE]		= { .type = NLA_STRING },
	[NFTA_SET_NAME]			= { .type = NLA_STRING },
	[NFTA_SET_FLAGS]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_KEY_LEN]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_TYPE]		= { .type = NLA_U32 },
	[NFTA_SET_DATA_LEN]		= { .type = NLA_U32 },
};

static int nft_ctx_init_from_setattr(struct nft_ctx *ctx,
				     const struct sk_buff *skb,
				     const struct nlmsghdr *nlh,
				     const struct nlattr * const nla[])
{
	struct net *net = sock_net(skb->sk);
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table = NULL;

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	if (nla[NFTA_SET_TABLE] != NULL) {
		table = nf_tables_table_lookup(afi, nla[NFTA_SET_TABLE]);
		if (IS_ERR(table))
			return PTR_ERR(table);
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

static int nf_tables_set_alloc_name(struct nft_ctx *ctx, struct nft_set *set,
				    const char *name)
{
	const struct nft_set *i;
	const char *p;
	unsigned long *inuse;
	unsigned int n = 0;

	p = strnchr(name, IFNAMSIZ, '%');
	if (p != NULL) {
		if (p[1] != 'd' || strchr(p + 2, '%'))
			return -EINVAL;

		inuse = (unsigned long *)get_zeroed_page(GFP_KERNEL);
		if (inuse == NULL)
			return -ENOMEM;

		list_for_each_entry(i, &ctx->table->sets, list) {
			if (!sscanf(i->name, name, &n))
				continue;
			if (n < 0 || n > BITS_PER_LONG * PAGE_SIZE)
				continue;
			set_bit(n, inuse);
		}

		n = find_first_zero_bit(inuse, BITS_PER_LONG * PAGE_SIZE);
		free_page((unsigned long)inuse);
	}

	snprintf(set->name, sizeof(set->name), name, n);
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
	u32 portid = NETLINK_CB(ctx->skb).portid;
	u32 seq = ctx->nlh->nlmsg_seq;

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

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_trim(skb, nlh);
	return -1;
}

static int nf_tables_set_notify(const struct nft_ctx *ctx,
				const struct nft_set *set,
				int event)
{
	struct sk_buff *skb;
	u32 portid = NETLINK_CB(ctx->skb).portid;
	bool report;
	int err;

	report = nlmsg_report(ctx->nlh);
	if (!report && !nfnetlink_has_listeners(ctx->net, NFNLGRP_NFTABLES))
		return 0;

	err = -ENOBUFS;
	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	err = nf_tables_fill_set(skb, ctx, set, event, 0);
	if (err < 0) {
		kfree_skb(skb);
		goto err;
	}

	err = nfnetlink_send(skb, ctx->net, portid, NFNLGRP_NFTABLES, report,
			     GFP_KERNEL);
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

	list_for_each_entry(set, &ctx->table->sets, list) {
		if (idx < s_idx)
			goto cont;
		if (nf_tables_fill_set(skb, ctx, set, NFT_MSG_NEWSET,
				       NLM_F_MULTI) < 0) {
			cb->args[0] = idx;
			goto done;
		}
cont:
		idx++;
	}
	cb->args[1] = 1;
done:
	return skb->len;
}

static int nf_tables_dump_sets_all(struct nft_ctx *ctx, struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	const struct nft_set *set;
	unsigned int idx = 0, s_idx = cb->args[0];
	struct nft_table *table, *cur_table = (struct nft_table *)cb->args[2];

	if (cb->args[1])
		return skb->len;

	list_for_each_entry(table, &ctx->afi->tables, list) {
		if (cur_table && cur_table != table)
			continue;

		ctx->table = table;
		list_for_each_entry(set, &ctx->table->sets, list) {
			if (idx < s_idx)
				goto cont;
			if (nf_tables_fill_set(skb, ctx, set, NFT_MSG_NEWSET,
					       NLM_F_MULTI) < 0) {
				cb->args[0] = idx;
				cb->args[2] = (unsigned long) table;
				goto done;
			}
cont:
			idx++;
		}
	}
	cb->args[1] = 1;
done:
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

	if (ctx.table == NULL)
		ret = nf_tables_dump_sets_all(&ctx, skb, cb);
	else
		ret = nf_tables_dump_sets_table(&ctx, skb, cb);

	return ret;
}

static int nf_tables_getset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nft_set *set;
	struct nft_ctx ctx;
	struct sk_buff *skb2;
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

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME]);
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

static int nf_tables_newset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_set_ops *ops;
	const struct nft_af_info *afi;
	struct net *net = sock_net(skb->sk);
	struct nft_table *table;
	struct nft_set *set;
	struct nft_ctx ctx;
	char name[IFNAMSIZ];
	unsigned int size;
	bool create;
	u32 ktype, klen, dlen, dtype, flags;
	int err;

	if (nla[NFTA_SET_TABLE] == NULL ||
	    nla[NFTA_SET_NAME] == NULL ||
	    nla[NFTA_SET_KEY_LEN] == NULL)
		return -EINVAL;

	ktype = NFT_DATA_VALUE;
	if (nla[NFTA_SET_KEY_TYPE] != NULL) {
		ktype = ntohl(nla_get_be32(nla[NFTA_SET_KEY_TYPE]));
		if ((ktype & NFT_DATA_RESERVED_MASK) == NFT_DATA_RESERVED_MASK)
			return -EINVAL;
	}

	klen = ntohl(nla_get_be32(nla[NFTA_SET_KEY_LEN]));
	if (klen == 0 || klen > FIELD_SIZEOF(struct nft_data, data))
		return -EINVAL;

	flags = 0;
	if (nla[NFTA_SET_FLAGS] != NULL) {
		flags = ntohl(nla_get_be32(nla[NFTA_SET_FLAGS]));
		if (flags & ~(NFT_SET_ANONYMOUS | NFT_SET_CONSTANT |
			      NFT_SET_INTERVAL | NFT_SET_MAP))
			return -EINVAL;
	}

	dtype = 0;
	dlen  = 0;
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
			dlen = ntohl(nla_get_be32(nla[NFTA_SET_DATA_LEN]));
			if (dlen == 0 ||
			    dlen > FIELD_SIZEOF(struct nft_data, data))
				return -EINVAL;
		} else
			dlen = sizeof(struct nft_data);
	} else if (flags & NFT_SET_MAP)
		return -EINVAL;

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

	ops = nft_select_set_ops(nla);
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
	set->klen  = klen;
	set->dtype = dtype;
	set->dlen  = dlen;
	set->flags = flags;

	err = ops->init(set, nla);
	if (err < 0)
		goto err2;

	list_add_tail(&set->list, &table->sets);
	nf_tables_set_notify(&ctx, set, NFT_MSG_NEWSET);
	return 0;

err2:
	kfree(set);
err1:
	module_put(ops->owner);
	return err;
}

static void nf_tables_set_destroy(const struct nft_ctx *ctx, struct nft_set *set)
{
	list_del(&set->list);
	if (!(set->flags & NFT_SET_ANONYMOUS))
		nf_tables_set_notify(ctx, set, NFT_MSG_DELSET);

	set->ops->destroy(set);
	module_put(set->ops->owner);
	kfree(set);
}

static int nf_tables_delset(struct sock *nlsk, struct sk_buff *skb,
			    const struct nlmsghdr *nlh,
			    const struct nlattr * const nla[])
{
	struct nft_set *set;
	struct nft_ctx ctx;
	int err;

	if (nla[NFTA_SET_TABLE] == NULL)
		return -EINVAL;

	err = nft_ctx_init_from_setattr(&ctx, skb, nlh, nla);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_NAME]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (!list_empty(&set->bindings))
		return -EBUSY;

	nf_tables_set_destroy(&ctx, set);
	return 0;
}

static int nf_tables_bind_check_setelem(const struct nft_ctx *ctx,
					const struct nft_set *set,
					const struct nft_set_iter *iter,
					const struct nft_set_elem *elem)
{
	enum nft_registers dreg;

	dreg = nft_type_to_reg(set->dtype);
	return nft_validate_data_load(ctx, dreg, &elem->data, set->dtype);
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
	list_add_tail(&binding->list, &set->bindings);
	return 0;
}

void nf_tables_unbind_set(const struct nft_ctx *ctx, struct nft_set *set,
			  struct nft_set_binding *binding)
{
	list_del(&binding->list);

	if (list_empty(&set->bindings) && set->flags & NFT_SET_ANONYMOUS)
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
};

static int nft_ctx_init_from_elemattr(struct nft_ctx *ctx,
				      const struct sk_buff *skb,
				      const struct nlmsghdr *nlh,
				      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	struct net *net = sock_net(skb->sk);

	afi = nf_tables_afinfo_lookup(net, nfmsg->nfgen_family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_SET_ELEM_LIST_TABLE]);
	if (IS_ERR(table))
		return PTR_ERR(table);

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

	nfmsg = nlmsg_data(cb->nlh);
	err = nlmsg_parse(cb->nlh, sizeof(*nfmsg), nla, NFTA_SET_ELEM_LIST_MAX,
			  nft_set_elem_list_policy);
	if (err < 0)
		return err;

	err = nft_ctx_init_from_elemattr(&ctx, cb->skb, cb->nlh, (void *)nla);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
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
	nfmsg->nfgen_family = NFPROTO_UNSPEC;
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

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
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

static int nft_add_set_elem(const struct nft_ctx *ctx, struct nft_set *set,
			    const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	struct nft_data_desc d1, d2;
	struct nft_set_elem elem;
	struct nft_set_binding *binding;
	enum nft_registers dreg;
	int err;

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
				.chain	= binding->chain,
			};

			err = nft_validate_data_load(&bind_ctx, dreg,
						     &elem.data, d2.type);
			if (err < 0)
				goto err3;
		}
	}

	err = set->ops->insert(set, &elem);
	if (err < 0)
		goto err3;

	return 0;

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
	const struct nlattr *attr;
	struct nft_set *set;
	struct nft_ctx ctx;
	int rem, err;

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla);
	if (err < 0)
		return err;

	set = nf_tables_set_lookup(ctx.table, nla[NFTA_SET_ELEM_LIST_SET]);
	if (IS_ERR(set))
		return PTR_ERR(set);
	if (!list_empty(&set->bindings) && set->flags & NFT_SET_CONSTANT)
		return -EBUSY;

	nla_for_each_nested(attr, nla[NFTA_SET_ELEM_LIST_ELEMENTS], rem) {
		err = nft_add_set_elem(&ctx, set, attr);
		if (err < 0)
			return err;
	}
	return 0;
}

static int nft_del_setelem(const struct nft_ctx *ctx, struct nft_set *set,
			   const struct nlattr *attr)
{
	struct nlattr *nla[NFTA_SET_ELEM_MAX + 1];
	struct nft_data_desc desc;
	struct nft_set_elem elem;
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

	set->ops->remove(set, &elem);

	nft_data_uninit(&elem.key, NFT_DATA_VALUE);
	if (set->flags & NFT_SET_MAP)
		nft_data_uninit(&elem.data, set->dtype);

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
	int rem, err;

	err = nft_ctx_init_from_elemattr(&ctx, skb, nlh, nla);
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
			return err;
	}
	return 0;
}

static const struct nfnl_callback nf_tables_cb[NFT_MSG_MAX] = {
	[NFT_MSG_NEWTABLE] = {
		.call		= nf_tables_newtable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_GETTABLE] = {
		.call		= nf_tables_gettable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_DELTABLE] = {
		.call		= nf_tables_deltable,
		.attr_count	= NFTA_TABLE_MAX,
		.policy		= nft_table_policy,
	},
	[NFT_MSG_NEWCHAIN] = {
		.call		= nf_tables_newchain,
		.attr_count	= NFTA_CHAIN_MAX,
		.policy		= nft_chain_policy,
	},
	[NFT_MSG_GETCHAIN] = {
		.call		= nf_tables_getchain,
		.attr_count	= NFTA_CHAIN_MAX,
		.policy		= nft_chain_policy,
	},
	[NFT_MSG_DELCHAIN] = {
		.call		= nf_tables_delchain,
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
		.call		= nf_tables_newset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_GETSET] = {
		.call		= nf_tables_getset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_DELSET] = {
		.call		= nf_tables_delset,
		.attr_count	= NFTA_SET_MAX,
		.policy		= nft_set_policy,
	},
	[NFT_MSG_NEWSETELEM] = {
		.call		= nf_tables_newsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
	[NFT_MSG_GETSETELEM] = {
		.call		= nf_tables_getsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
	[NFT_MSG_DELSETELEM] = {
		.call		= nf_tables_delsetelem,
		.attr_count	= NFTA_SET_ELEM_LIST_MAX,
		.policy		= nft_set_elem_list_policy,
	},
};

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
	case NF_ACCEPT:
	case NF_DROP:
	case NF_QUEUE:
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
	default:
		return -EINVAL;
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
