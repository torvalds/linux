// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021 Red Hat GmbH
 *
 * Author: Florian Westphal <fw@strlen.de>
 */

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/slab.h>

#include <linux/netfilter.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_hook.h>

#include <net/netfilter/nf_tables.h>
#include <net/sock.h>

static const struct nla_policy nfnl_hook_nla_policy[NFNLA_HOOK_MAX + 1] = {
	[NFNLA_HOOK_HOOKNUM]	= { .type = NLA_U32 },
	[NFNLA_HOOK_PRIORITY]	= { .type = NLA_U32 },
	[NFNLA_HOOK_DEV]	= { .type = NLA_STRING,
				    .len = IFNAMSIZ - 1 },
	[NFNLA_HOOK_FUNCTION_NAME] = { .type = NLA_NUL_STRING,
				       .len = KSYM_NAME_LEN, },
	[NFNLA_HOOK_MODULE_NAME] = { .type = NLA_NUL_STRING,
				     .len = MODULE_NAME_LEN, },
	[NFNLA_HOOK_CHAIN_INFO] = { .type = NLA_NESTED, },
};

static int nf_netlink_dump_start_rcu(struct sock *nlsk, struct sk_buff *skb,
				     const struct nlmsghdr *nlh,
				     struct netlink_dump_control *c)
{
	int err;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	rcu_read_unlock();
	err = netlink_dump_start(nlsk, skb, nlh, c);
	rcu_read_lock();
	module_put(THIS_MODULE);

	return err;
}

struct nfnl_dump_hook_data {
	char devname[IFNAMSIZ];
	unsigned long headv;
	u8 hook;
};

static int nfnl_hook_put_nft_chain_info(struct sk_buff *nlskb,
					const struct nfnl_dump_hook_data *ctx,
					unsigned int seq,
					const struct nf_hook_ops *ops)
{
	struct net *net = sock_net(nlskb->sk);
	struct nlattr *nest, *nest2;
	struct nft_chain *chain;
	int ret = 0;

	if (ops->hook_ops_type != NF_HOOK_OP_NF_TABLES)
		return 0;

	chain = ops->priv;
	if (WARN_ON_ONCE(!chain))
		return 0;

	if (!nft_is_active(net, chain))
		return 0;

	nest = nla_nest_start(nlskb, NFNLA_HOOK_CHAIN_INFO);
	if (!nest)
		return -EMSGSIZE;

	ret = nla_put_be32(nlskb, NFNLA_HOOK_INFO_TYPE,
			   htonl(NFNL_HOOK_TYPE_NFTABLES));
	if (ret)
		goto cancel_nest;

	nest2 = nla_nest_start(nlskb, NFNLA_HOOK_INFO_DESC);
	if (!nest2)
		goto cancel_nest;

	ret = nla_put_string(nlskb, NFNLA_CHAIN_TABLE, chain->table->name);
	if (ret)
		goto cancel_nest;

	ret = nla_put_string(nlskb, NFNLA_CHAIN_NAME, chain->name);
	if (ret)
		goto cancel_nest;

	ret = nla_put_u8(nlskb, NFNLA_CHAIN_FAMILY, chain->table->family);
	if (ret)
		goto cancel_nest;

	nla_nest_end(nlskb, nest2);
	nla_nest_end(nlskb, nest);
	return ret;

cancel_nest:
	nla_nest_cancel(nlskb, nest);
	return -EMSGSIZE;
}

static int nfnl_hook_dump_one(struct sk_buff *nlskb,
			      const struct nfnl_dump_hook_data *ctx,
			      const struct nf_hook_ops *ops,
			      int family, unsigned int seq)
{
	u16 event = nfnl_msg_type(NFNL_SUBSYS_HOOK, NFNL_MSG_HOOK_GET);
	unsigned int portid = NETLINK_CB(nlskb).portid;
	struct nlmsghdr *nlh;
	int ret = -EMSGSIZE;
	u32 hooknum;
#ifdef CONFIG_KALLSYMS
	char sym[KSYM_SYMBOL_LEN];
	char *module_name;
#endif
	nlh = nfnl_msg_put(nlskb, portid, seq, event,
			   NLM_F_MULTI, family, NFNETLINK_V0, 0);
	if (!nlh)
		goto nla_put_failure;

#ifdef CONFIG_KALLSYMS
	ret = snprintf(sym, sizeof(sym), "%ps", ops->hook);
	if (ret >= sizeof(sym)) {
		ret = -EINVAL;
		goto nla_put_failure;
	}

	module_name = strstr(sym, " [");
	if (module_name) {
		char *end;

		*module_name = '\0';
		module_name += 2;
		end = strchr(module_name, ']');
		if (end) {
			*end = 0;

			ret = nla_put_string(nlskb, NFNLA_HOOK_MODULE_NAME, module_name);
			if (ret)
				goto nla_put_failure;
		}
	}

	ret = nla_put_string(nlskb, NFNLA_HOOK_FUNCTION_NAME, sym);
	if (ret)
		goto nla_put_failure;
#endif

	if (ops->pf == NFPROTO_INET && ops->hooknum == NF_INET_INGRESS)
		hooknum = NF_NETDEV_INGRESS;
	else
		hooknum = ops->hooknum;

	ret = nla_put_be32(nlskb, NFNLA_HOOK_HOOKNUM, htonl(hooknum));
	if (ret)
		goto nla_put_failure;

	ret = nla_put_be32(nlskb, NFNLA_HOOK_PRIORITY, htonl(ops->priority));
	if (ret)
		goto nla_put_failure;

	ret = nfnl_hook_put_nft_chain_info(nlskb, ctx, seq, ops);
	if (ret)
		goto nla_put_failure;

	nlmsg_end(nlskb, nlh);
	return 0;
nla_put_failure:
	nlmsg_trim(nlskb, nlh);
	return ret;
}

static const struct nf_hook_entries *
nfnl_hook_entries_head(u8 pf, unsigned int hook, struct net *net, const char *dev)
{
	const struct nf_hook_entries *hook_head = NULL;
#if defined(CONFIG_NETFILTER_INGRESS) || defined(CONFIG_NETFILTER_EGRESS)
	struct net_device *netdev;
#endif

	switch (pf) {
	case NFPROTO_IPV4:
		if (hook >= ARRAY_SIZE(net->nf.hooks_ipv4))
			return ERR_PTR(-EINVAL);
		hook_head = rcu_dereference(net->nf.hooks_ipv4[hook]);
		break;
	case NFPROTO_IPV6:
		if (hook >= ARRAY_SIZE(net->nf.hooks_ipv6))
			return ERR_PTR(-EINVAL);
		hook_head = rcu_dereference(net->nf.hooks_ipv6[hook]);
		break;
	case NFPROTO_ARP:
#ifdef CONFIG_NETFILTER_FAMILY_ARP
		if (hook >= ARRAY_SIZE(net->nf.hooks_arp))
			return ERR_PTR(-EINVAL);
		hook_head = rcu_dereference(net->nf.hooks_arp[hook]);
#endif
		break;
	case NFPROTO_BRIDGE:
#ifdef CONFIG_NETFILTER_FAMILY_BRIDGE
		if (hook >= ARRAY_SIZE(net->nf.hooks_bridge))
			return ERR_PTR(-EINVAL);
		hook_head = rcu_dereference(net->nf.hooks_bridge[hook]);
#endif
		break;
#if IS_ENABLED(CONFIG_DECNET)
	case NFPROTO_DECNET:
		if (hook >= ARRAY_SIZE(net->nf.hooks_decnet))
			return ERR_PTR(-EINVAL);
		hook_head = rcu_dereference(net->nf.hooks_decnet[hook]);
		break;
#endif
#if defined(CONFIG_NETFILTER_INGRESS) || defined(CONFIG_NETFILTER_EGRESS)
	case NFPROTO_NETDEV:
		if (hook >= NF_NETDEV_NUMHOOKS)
			return ERR_PTR(-EOPNOTSUPP);

		if (!dev)
			return ERR_PTR(-ENODEV);

		netdev = dev_get_by_name_rcu(net, dev);
		if (!netdev)
			return ERR_PTR(-ENODEV);

#ifdef CONFIG_NETFILTER_INGRESS
		if (hook == NF_NETDEV_INGRESS)
			return rcu_dereference(netdev->nf_hooks_ingress);
#endif
#ifdef CONFIG_NETFILTER_EGRESS
		if (hook == NF_NETDEV_EGRESS)
			return rcu_dereference(netdev->nf_hooks_egress);
#endif
		fallthrough;
#endif
	default:
		return ERR_PTR(-EPROTONOSUPPORT);
	}

	return hook_head;
}

static int nfnl_hook_dump(struct sk_buff *nlskb,
			  struct netlink_callback *cb)
{
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	struct nfnl_dump_hook_data *ctx = cb->data;
	int err, family = nfmsg->nfgen_family;
	struct net *net = sock_net(nlskb->sk);
	struct nf_hook_ops * const *ops;
	const struct nf_hook_entries *e;
	unsigned int i = cb->args[0];

	rcu_read_lock();

	e = nfnl_hook_entries_head(family, ctx->hook, net, ctx->devname);
	if (!e)
		goto done;

	if (IS_ERR(e)) {
		cb->seq++;
		goto done;
	}

	if ((unsigned long)e != ctx->headv || i >= e->num_hook_entries)
		cb->seq++;

	ops = nf_hook_entries_get_hook_ops(e);

	for (; i < e->num_hook_entries; i++) {
		err = nfnl_hook_dump_one(nlskb, ctx, ops[i], family,
					 cb->nlh->nlmsg_seq);
		if (err)
			break;
	}

done:
	nl_dump_check_consistent(cb, nlmsg_hdr(nlskb));
	rcu_read_unlock();
	cb->args[0] = i;
	return nlskb->len;
}

static int nfnl_hook_dump_start(struct netlink_callback *cb)
{
	const struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	const struct nlattr * const *nla = cb->data;
	struct nfnl_dump_hook_data *ctx = NULL;
	struct net *net = sock_net(cb->skb->sk);
	u8 family = nfmsg->nfgen_family;
	char name[IFNAMSIZ] = "";
	const void *head;
	u32 hooknum;

	hooknum = ntohl(nla_get_be32(nla[NFNLA_HOOK_HOOKNUM]));
	if (hooknum > 255)
		return -EINVAL;

	if (family == NFPROTO_NETDEV) {
		if (!nla[NFNLA_HOOK_DEV])
			return -EINVAL;

		nla_strscpy(name, nla[NFNLA_HOOK_DEV], sizeof(name));
	}

	rcu_read_lock();
	/* Not dereferenced; for consistency check only */
	head = nfnl_hook_entries_head(family, hooknum, net, name);
	rcu_read_unlock();

	if (head && IS_ERR(head))
		return PTR_ERR(head);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	strscpy(ctx->devname, name, sizeof(ctx->devname));
	ctx->headv = (unsigned long)head;
	ctx->hook = hooknum;

	cb->seq = 1;
	cb->data = ctx;

	return 0;
}

static int nfnl_hook_dump_stop(struct netlink_callback *cb)
{
	kfree(cb->data);
	return 0;
}

static int nfnl_hook_get(struct sk_buff *skb,
			 const struct nfnl_info *info,
			 const struct nlattr * const nla[])
{
	if (!nla[NFNLA_HOOK_HOOKNUM])
		return -EINVAL;

	if (info->nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.start = nfnl_hook_dump_start,
			.done = nfnl_hook_dump_stop,
			.dump = nfnl_hook_dump,
			.module = THIS_MODULE,
			.data = (void *)nla,
		};

		return nf_netlink_dump_start_rcu(info->sk, skb, info->nlh, &c);
	}

	return -EOPNOTSUPP;
}

static const struct nfnl_callback nfnl_hook_cb[NFNL_MSG_HOOK_MAX] = {
	[NFNL_MSG_HOOK_GET] = {
		.call		= nfnl_hook_get,
		.type		= NFNL_CB_RCU,
		.attr_count	= NFNLA_HOOK_MAX,
		.policy		= nfnl_hook_nla_policy
	},
};

static const struct nfnetlink_subsystem nfhook_subsys = {
	.name				= "nfhook",
	.subsys_id			= NFNL_SUBSYS_HOOK,
	.cb_count			= NFNL_MSG_HOOK_MAX,
	.cb				= nfnl_hook_cb,
};

MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_HOOK);

static int __init nfnetlink_hook_init(void)
{
	return nfnetlink_subsys_register(&nfhook_subsys);
}

static void __exit nfnetlink_hook_exit(void)
{
	nfnetlink_subsys_unregister(&nfhook_subsys);
}

module_init(nfnetlink_hook_init);
module_exit(nfnetlink_hook_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("nfnetlink_hook: list registered netfilter hooks");
