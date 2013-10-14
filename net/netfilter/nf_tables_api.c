/*
 * Copyright (c) 2007, 2008 Patrick McHardy <kaber@trash.net>
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
#include <net/sock.h>

static LIST_HEAD(nf_tables_afinfo);
static LIST_HEAD(nf_tables_expressions);

/**
 *	nft_register_afinfo - register nf_tables address family info
 *
 *	@afi: address family info to register
 *
 *	Register the address family for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_afinfo(struct nft_af_info *afi)
{
	INIT_LIST_HEAD(&afi->tables);
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail(&afi->list, &nf_tables_afinfo);
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

static struct nft_af_info *nft_afinfo_lookup(int family)
{
	struct nft_af_info *afi;

	list_for_each_entry(afi, &nf_tables_afinfo, list) {
		if (afi->family == family)
			return afi;
	}
	return NULL;
}

static struct nft_af_info *nf_tables_afinfo_lookup(int family, bool autoload)
{
	struct nft_af_info *afi;

	afi = nft_afinfo_lookup(family);
	if (afi != NULL)
		return afi;
#ifdef CONFIG_MODULES
	if (autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-afinfo-%u", family);
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		afi = nft_afinfo_lookup(family);
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
						const struct nlattr *nla,
						bool autoload)
{
	struct nft_table *table;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	table = nft_table_lookup(afi, nla);
	if (table != NULL)
		return table;

#ifdef CONFIG_MODULES
	if (autoload) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-table-%u-%*.s", afi->family,
			       nla_len(nla)-1, (const char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (nft_table_lookup(afi, nla))
			return ERR_PTR(-EAGAIN);
	}
#endif
	return ERR_PTR(-ENOENT);
}

static inline u64 nf_tables_alloc_handle(struct nft_table *table)
{
	return ++table->hgenerator;
}

static const struct nla_policy nft_table_policy[NFTA_TABLE_MAX + 1] = {
	[NFTA_TABLE_NAME]	= { .type = NLA_STRING },
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

	if (nla_put_string(skb, NFTA_TABLE_NAME, table->name))
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
	int family = nfmsg->nfgen_family;

	list_for_each_entry(afi, &nf_tables_afinfo, list) {
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
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_tables,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME], false);
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

static int nf_tables_newtable(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr *name;
	struct nft_af_info *afi;
	struct nft_table *table;
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	name = nla[NFTA_TABLE_NAME];
	table = nf_tables_table_lookup(afi, name, false);
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
		return 0;
	}

	table = kzalloc(sizeof(*table) + nla_len(name), GFP_KERNEL);
	if (table == NULL)
		return -ENOMEM;

	nla_strlcpy(table->name, name, nla_len(name));
	INIT_LIST_HEAD(&table->chains);

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
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_TABLE_NAME], false);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (table->flags & NFT_TABLE_BUILTIN)
		return -EOPNOTSUPP;

	if (table->use)
		return -EBUSY;

	list_del(&table->list);
	nf_tables_table_notify(skb, nlh, table, NFT_MSG_DELTABLE, family);
	kfree(table);
	return 0;
}

static struct nft_table *__nf_tables_table_lookup(const struct nft_af_info *afi,
						  const char *name)
{
	struct nft_table *table;

	list_for_each_entry(table, &afi->tables, list) {
		if (!strcmp(name, table->name))
			return table;
	}

	return ERR_PTR(-ENOENT);
}

static int nf_tables_chain_notify(const struct sk_buff *oskb,
				  const struct nlmsghdr *nlh,
				  const struct nft_table *table,
				  const struct nft_chain *chain,
				  int event, int family);

/**
 *	nft_register_table - register a built-in table
 *
 *	@table: the table to register
 *	@family: protocol family to register table with
 *
 *	Register a built-in table for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_table(struct nft_table *table, int family)
{
	struct nft_af_info *afi;
	struct nft_table *t;
	struct nft_chain *chain;
	int err;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
again:
	afi = nf_tables_afinfo_lookup(family, true);
	if (IS_ERR(afi)) {
		err = PTR_ERR(afi);
		if (err == -EAGAIN)
			goto again;
		goto err;
	}

	t = __nf_tables_table_lookup(afi, table->name);
	if (IS_ERR(t)) {
		err = PTR_ERR(t);
		if (err != -ENOENT)
			goto err;
		t = NULL;
	}

	if (t != NULL) {
		err = -EEXIST;
		goto err;
	}

	table->flags |= NFT_TABLE_BUILTIN;
	list_add_tail(&table->list, &afi->tables);
	nf_tables_table_notify(NULL, NULL, table, NFT_MSG_NEWTABLE, family);
	list_for_each_entry(chain, &table->chains, list)
		nf_tables_chain_notify(NULL, NULL, table, chain,
				       NFT_MSG_NEWCHAIN, family);
	err = 0;
err:
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return err;
}
EXPORT_SYMBOL_GPL(nft_register_table);

/**
 *	nft_unregister_table - unregister a built-in table
 *
 *	@table: the table to unregister
 *	@family: protocol family to unregister table with
 *
 *	Unregister a built-in table for use with nf_tables.
 */
void nft_unregister_table(struct nft_table *table, int family)
{
	struct nft_chain *chain;

	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del(&table->list);
	list_for_each_entry(chain, &table->chains, list)
		nf_tables_chain_notify(NULL, NULL, table, chain,
				       NFT_MSG_DELCHAIN, family);
	nf_tables_table_notify(NULL, NULL, table, NFT_MSG_DELTABLE, family);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_table);

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
};

static const struct nla_policy nft_hook_policy[NFTA_HOOK_MAX + 1] = {
	[NFTA_HOOK_HOOKNUM]	= { .type = NLA_U32 },
	[NFTA_HOOK_PRIORITY]	= { .type = NLA_U32 },
};

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
		const struct nf_hook_ops *ops = &nft_base_chain(chain)->ops;
		struct nlattr *nest = nla_nest_start(skb, NFTA_CHAIN_HOOK);
		if (nest == NULL)
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_HOOK_HOOKNUM, htonl(ops->hooknum)))
			goto nla_put_failure;
		if (nla_put_be32(skb, NFTA_HOOK_PRIORITY, htonl(ops->priority)))
			goto nla_put_failure;
		nla_nest_end(skb, nest);
	}

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
	int family = nfmsg->nfgen_family;

	list_for_each_entry(afi, &nf_tables_afinfo, list) {
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
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_chains,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], false);
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

static int nf_tables_newchain(struct sock *nlsk, struct sk_buff *skb,
			      const struct nlmsghdr *nlh,
			      const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nlattr * uninitialized_var(name);
	const struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_base_chain *basechain;
	struct nlattr *ha[NFTA_HOOK_MAX + 1];
	int family = nfmsg->nfgen_family;
	u64 handle = 0;
	int err;
	bool create;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(family, true);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], create);
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

		if (nla[NFTA_CHAIN_HANDLE] && name)
			nla_strlcpy(chain->name, name, NFT_CHAIN_MAXNAMELEN);

		goto notify;
	}

	if (nla[NFTA_CHAIN_HOOK]) {
		struct nf_hook_ops *ops;

		err = nla_parse_nested(ha, NFTA_HOOK_MAX, nla[NFTA_CHAIN_HOOK],
				       nft_hook_policy);
		if (err < 0)
			return err;
		if (ha[NFTA_HOOK_HOOKNUM] == NULL ||
		    ha[NFTA_HOOK_PRIORITY] == NULL)
			return -EINVAL;
		if (ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM])) >= afi->nhooks)
			return -EINVAL;

		basechain = kzalloc(sizeof(*basechain), GFP_KERNEL);
		if (basechain == NULL)
			return -ENOMEM;
		chain = &basechain->chain;

		ops = &basechain->ops;
		ops->pf		= family;
		ops->owner	= afi->owner;
		ops->hooknum	= ntohl(nla_get_be32(ha[NFTA_HOOK_HOOKNUM]));
		ops->priority	= ntohl(nla_get_be32(ha[NFTA_HOOK_PRIORITY]));
		ops->priv	= chain;
		ops->hook	= nft_do_chain;
		if (afi->hooks[ops->hooknum])
			ops->hook = afi->hooks[ops->hooknum];

		chain->flags |= NFT_BASE_CHAIN;
	} else {
		chain = kzalloc(sizeof(*chain), GFP_KERNEL);
		if (chain == NULL)
			return -ENOMEM;
	}

	INIT_LIST_HEAD(&chain->rules);
	chain->handle = nf_tables_alloc_handle(table);
	nla_strlcpy(chain->name, name, NFT_CHAIN_MAXNAMELEN);

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

	if (chain->flags & NFT_BASE_CHAIN)
		kfree(nft_base_chain(chain));
	else
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
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_CHAIN_TABLE], false);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_CHAIN_NAME]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	if (chain->flags & NFT_CHAIN_BUILTIN)
		return -EOPNOTSUPP;

	if (!list_empty(&chain->rules))
		return -EBUSY;

	list_del(&chain->list);
	table->use--;

	if (chain->flags & NFT_BASE_CHAIN)
		nf_unregister_hook(&nft_base_chain(chain)->ops);

	nf_tables_chain_notify(skb, nlh, table, chain, NFT_MSG_DELCHAIN,
			       family);

	/* Make sure all rule references are gone before this is released */
	call_rcu(&chain->rcu_head, nf_tables_rcu_chain_destroy);
	return 0;
}

static void nft_ctx_init(struct nft_ctx *ctx,
			 const struct nft_af_info *afi,
			 const struct nft_table *table,
			 const struct nft_chain *chain)
{
	ctx->afi   = afi;
	ctx->table = table;
	ctx->chain = chain;
}

/*
 * Expressions
 */

/**
 *	nft_register_expr - register nf_tables expr operations
 *	@ops: expr operations
 *
 *	Registers the expr operations for use with nf_tables. Returns zero on
 *	success or a negative errno code otherwise.
 */
int nft_register_expr(struct nft_expr_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_add_tail(&ops->list, &nf_tables_expressions);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
	return 0;
}
EXPORT_SYMBOL_GPL(nft_register_expr);

/**
 *	nft_unregister_expr - unregister nf_tables expr operations
 *	@ops: expr operations
 *
 * 	Unregisters the expr operations for use with nf_tables.
 */
void nft_unregister_expr(struct nft_expr_ops *ops)
{
	nfnl_lock(NFNL_SUBSYS_NFTABLES);
	list_del(&ops->list);
	nfnl_unlock(NFNL_SUBSYS_NFTABLES);
}
EXPORT_SYMBOL_GPL(nft_unregister_expr);

static const struct nft_expr_ops *__nft_expr_ops_get(struct nlattr *nla)
{
	const struct nft_expr_ops *ops;

	list_for_each_entry(ops, &nf_tables_expressions, list) {
		if (!nla_strcmp(nla, ops->name))
			return ops;
	}
	return NULL;
}

static const struct nft_expr_ops *nft_expr_ops_get(struct nlattr *nla)
{
	const struct nft_expr_ops *ops;

	if (nla == NULL)
		return ERR_PTR(-EINVAL);

	ops = __nft_expr_ops_get(nla);
	if (ops != NULL && try_module_get(ops->owner))
		return ops;

#ifdef CONFIG_MODULES
	if (ops == NULL) {
		nfnl_unlock(NFNL_SUBSYS_NFTABLES);
		request_module("nft-expr-%.*s",
			       nla_len(nla), (char *)nla_data(nla));
		nfnl_lock(NFNL_SUBSYS_NFTABLES);
		if (__nft_expr_ops_get(nla))
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
	if (nla_put_string(skb, NFTA_EXPR_NAME, expr->ops->name))
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
	struct nlattr			*tb[NFTA_EXPR_MAX + 1];
};

static int nf_tables_expr_parse(const struct nlattr *nla,
				struct nft_expr_info *info)
{
	const struct nft_expr_ops *ops;
	int err;

	err = nla_parse_nested(info->tb, NFTA_EXPR_MAX, nla, nft_expr_policy);
	if (err < 0)
		return err;

	ops = nft_expr_ops_get(info->tb[NFTA_EXPR_NAME]);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	info->ops = ops;
	return 0;
}

static int nf_tables_newexpr(const struct nft_ctx *ctx,
			     struct nft_expr_info *info,
			     struct nft_expr *expr)
{
	const struct nft_expr_ops *ops = info->ops;
	int err;

	expr->ops = ops;
	if (ops->init) {
		struct nlattr *ma[ops->maxattr + 1];

		if (info->tb[NFTA_EXPR_DATA]) {
			err = nla_parse_nested(ma, ops->maxattr,
					       info->tb[NFTA_EXPR_DATA],
					       ops->policy);
			if (err < 0)
				goto err1;
		} else
			memset(ma, 0, sizeof(ma[0]) * (ops->maxattr + 1));

		err = ops->init(ctx, expr, (const struct nlattr **)ma);
		if (err < 0)
			goto err1;
	}

	info->ops = NULL;
	return 0;

err1:
	expr->ops = NULL;
	return err;
}

static void nf_tables_expr_destroy(struct nft_expr *expr)
{
	if (expr->ops->destroy)
		expr->ops->destroy(expr);
	module_put(expr->ops->owner);
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

	event |= NFNL_SUBSYS_NFTABLES << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct nfgenmsg),
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

static int nf_tables_dump_rules(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	const struct nft_chain *chain;
	const struct nft_rule *rule;
	unsigned int idx = 0, s_idx = cb->args[0];
	int family = nfmsg->nfgen_family;

	list_for_each_entry(afi, &nf_tables_afinfo, list) {
		if (family != NFPROTO_UNSPEC && family != afi->family)
			continue;

		list_for_each_entry(table, &afi->tables, list) {
			list_for_each_entry(chain, &table->chains, list) {
				list_for_each_entry(rule, &chain->rules, list) {
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
	int family = nfmsg->nfgen_family;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nf_tables_dump_rules,
		};
		return netlink_dump_start(nlsk, skb, nlh, &c);
	}

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], false);
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

static int nf_tables_newrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule, *old_rule = NULL;
	struct nft_expr *expr;
	struct nft_ctx ctx;
	struct nlattr *tmp;
	unsigned int size, i, n;
	int err, rem;
	bool create;
	u64 handle;

	create = nlh->nlmsg_flags & NLM_F_CREATE ? true : false;

	afi = nf_tables_afinfo_lookup(nfmsg->nfgen_family, create);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], create);
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

	n = 0;
	size = 0;
	if (nla[NFTA_RULE_EXPRESSIONS]) {
		nla_for_each_nested(tmp, nla[NFTA_RULE_EXPRESSIONS], rem) {
			err = -EINVAL;
			if (nla_type(tmp) != NFTA_LIST_ELEM)
				goto err1;
			if (n == NFT_RULE_MAXEXPRS)
				goto err1;
			err = nf_tables_expr_parse(tmp, &info[n]);
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

	rule->handle = handle;
	rule->dlen   = size;

	nft_ctx_init(&ctx, afi, table, chain);
	expr = nft_expr_first(rule);
	for (i = 0; i < n; i++) {
		err = nf_tables_newexpr(&ctx, &info[i], expr);
		if (err < 0)
			goto err2;
		expr = nft_expr_next(expr);
	}

	/* Register hook when first rule is inserted into a base chain */
	if (list_empty(&chain->rules) && chain->flags & NFT_BASE_CHAIN) {
		err = nf_register_hook(&nft_base_chain(chain)->ops);
		if (err < 0)
			goto err2;
	}

	if (nlh->nlmsg_flags & NLM_F_REPLACE) {
		list_replace_rcu(&old_rule->list, &rule->list);
		nf_tables_rule_destroy(old_rule);
	} else if (nlh->nlmsg_flags & NLM_F_APPEND)
		list_add_tail_rcu(&rule->list, &chain->rules);
	else
		list_add_rcu(&rule->list, &chain->rules);

	nf_tables_rule_notify(skb, nlh, table, chain, rule, NFT_MSG_NEWRULE,
			      nlh->nlmsg_flags & (NLM_F_APPEND | NLM_F_REPLACE),
			      nfmsg->nfgen_family);
	return 0;

err2:
	nf_tables_rule_destroy(rule);
err1:
	for (i = 0; i < n; i++) {
		if (info[i].ops != NULL)
			module_put(info[i].ops->owner);
	}
	return err;
}

static int nf_tables_delrule(struct sock *nlsk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     const struct nlattr * const nla[])
{
	const struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	const struct nft_af_info *afi;
	const struct nft_table *table;
	struct nft_chain *chain;
	struct nft_rule *rule, *tmp;
	int family = nfmsg->nfgen_family;

	afi = nf_tables_afinfo_lookup(family, false);
	if (IS_ERR(afi))
		return PTR_ERR(afi);

	table = nf_tables_table_lookup(afi, nla[NFTA_RULE_TABLE], false);
	if (IS_ERR(table))
		return PTR_ERR(table);

	chain = nf_tables_chain_lookup(table, nla[NFTA_RULE_CHAIN]);
	if (IS_ERR(chain))
		return PTR_ERR(chain);

	if (nla[NFTA_RULE_HANDLE]) {
		rule = nf_tables_rule_lookup(chain, nla[NFTA_RULE_HANDLE]);
		if (IS_ERR(rule))
			return PTR_ERR(rule);

		/* List removal must be visible before destroying expressions */
		list_del_rcu(&rule->list);

		nf_tables_rule_notify(skb, nlh, table, chain, rule,
				      NFT_MSG_DELRULE, 0, family);
		nf_tables_rule_destroy(rule);
	} else {
		/* Remove all rules in this chain */
		list_for_each_entry_safe(rule, tmp, &chain->rules, list) {
			list_del_rcu(&rule->list);

			nf_tables_rule_notify(skb, nlh, table, chain, rule,
					      NFT_MSG_DELRULE, 0, family);
			nf_tables_rule_destroy(rule);
		}
	}

	/* Unregister hook when last rule from base chain is deleted */
	if (list_empty(&chain->rules) && chain->flags & NFT_BASE_CHAIN)
		nf_unregister_hook(&nft_base_chain(chain)->ops);

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
		.call		= nf_tables_newrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
	[NFT_MSG_GETRULE] = {
		.call		= nf_tables_getrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
	[NFT_MSG_DELRULE] = {
		.call		= nf_tables_delrule,
		.attr_count	= NFTA_RULE_MAX,
		.policy		= nft_rule_policy,
	},
};

static const struct nfnetlink_subsystem nf_tables_subsys = {
	.name		= "nf_tables",
	.subsys_id	= NFNL_SUBSYS_NFTABLES,
	.cb_count	= NFT_MSG_MAX,
	.cb		= nf_tables_cb,
};

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
	switch (reg) {
	case NFT_REG_VERDICT:
		if (data == NULL || type != NFT_DATA_VERDICT)
			return -EINVAL;
		// FIXME: do loop detection
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

		if (ctx->chain->level + 1 > chain->level) {
			if (ctx->chain->level + 1 == 16)
				return -EMLINK;
			chain->level = ctx->chain->level + 1;
		}
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
	return 0;
err3:
	nf_tables_core_module_exit();
err2:
	kfree(info);
err1:
	return err;
}

static void __exit nf_tables_module_exit(void)
{
	nfnetlink_subsys_unregister(&nf_tables_subsys);
	nf_tables_core_module_exit();
	kfree(info);
}

module_init(nf_tables_module_init);
module_exit(nf_tables_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_NFTABLES);
