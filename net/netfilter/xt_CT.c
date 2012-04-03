/*
 * Copyright (c) 2010 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CT.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_conntrack_zones.h>

static unsigned int xt_ct_target_v0(struct sk_buff *skb,
				    const struct xt_action_param *par)
{
	const struct xt_ct_target_info *info = par->targinfo;
	struct nf_conn *ct = info->ct;

	/* Previously seen (loopback)? Ignore. */
	if (skb->nfct != NULL)
		return XT_CONTINUE;

	atomic_inc(&ct->ct_general.use);
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = IP_CT_NEW;

	return XT_CONTINUE;
}

static unsigned int xt_ct_target_v1(struct sk_buff *skb,
				    const struct xt_action_param *par)
{
	const struct xt_ct_target_info_v1 *info = par->targinfo;
	struct nf_conn *ct = info->ct;

	/* Previously seen (loopback)? Ignore. */
	if (skb->nfct != NULL)
		return XT_CONTINUE;

	atomic_inc(&ct->ct_general.use);
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = IP_CT_NEW;

	return XT_CONTINUE;
}

static u8 xt_ct_find_proto(const struct xt_tgchk_param *par)
{
	if (par->family == NFPROTO_IPV4) {
		const struct ipt_entry *e = par->entryinfo;

		if (e->ip.invflags & IPT_INV_PROTO)
			return 0;
		return e->ip.proto;
	} else if (par->family == NFPROTO_IPV6) {
		const struct ip6t_entry *e = par->entryinfo;

		if (e->ipv6.invflags & IP6T_INV_PROTO)
			return 0;
		return e->ipv6.proto;
	} else
		return 0;
}

static int xt_ct_tg_check_v0(const struct xt_tgchk_param *par)
{
	struct xt_ct_target_info *info = par->targinfo;
	struct nf_conntrack_tuple t;
	struct nf_conn_help *help;
	struct nf_conn *ct;
	int ret = 0;
	u8 proto;

	if (info->flags & ~XT_CT_NOTRACK)
		return -EINVAL;

	if (info->flags & XT_CT_NOTRACK) {
		ct = nf_ct_untracked_get();
		atomic_inc(&ct->ct_general.use);
		goto out;
	}

#ifndef CONFIG_NF_CONNTRACK_ZONES
	if (info->zone)
		goto err1;
#endif

	ret = nf_ct_l3proto_try_module_get(par->family);
	if (ret < 0)
		goto err1;

	memset(&t, 0, sizeof(t));
	ct = nf_conntrack_alloc(par->net, info->zone, &t, &t, GFP_KERNEL);
	ret = PTR_ERR(ct);
	if (IS_ERR(ct))
		goto err2;

	ret = 0;
	if ((info->ct_events || info->exp_events) &&
	    !nf_ct_ecache_ext_add(ct, info->ct_events, info->exp_events,
				  GFP_KERNEL))
		goto err3;

	if (info->helper[0]) {
		ret = -ENOENT;
		proto = xt_ct_find_proto(par);
		if (!proto) {
			pr_info("You must specify a L4 protocol, "
				"and not use inversions on it.\n");
			goto err3;
		}

		ret = -ENOMEM;
		help = nf_ct_helper_ext_add(ct, GFP_KERNEL);
		if (help == NULL)
			goto err3;

		ret = -ENOENT;
		help->helper = nf_conntrack_helper_try_module_get(info->helper,
								  par->family,
								  proto);
		if (help->helper == NULL) {
			pr_info("No such helper \"%s\"\n", info->helper);
			goto err3;
		}
	}

	__set_bit(IPS_TEMPLATE_BIT, &ct->status);
	__set_bit(IPS_CONFIRMED_BIT, &ct->status);
out:
	info->ct = ct;
	return 0;

err3:
	nf_conntrack_free(ct);
err2:
	nf_ct_l3proto_module_put(par->family);
err1:
	return ret;
}

static int xt_ct_tg_check_v1(const struct xt_tgchk_param *par)
{
	struct xt_ct_target_info_v1 *info = par->targinfo;
	struct nf_conntrack_tuple t;
	struct nf_conn_help *help;
	struct nf_conn *ct;
	int ret = 0;
	u8 proto;

	if (info->flags & ~XT_CT_NOTRACK)
		return -EINVAL;

	if (info->flags & XT_CT_NOTRACK) {
		ct = nf_ct_untracked_get();
		atomic_inc(&ct->ct_general.use);
		goto out;
	}

#ifndef CONFIG_NF_CONNTRACK_ZONES
	if (info->zone)
		goto err1;
#endif

	ret = nf_ct_l3proto_try_module_get(par->family);
	if (ret < 0)
		goto err1;

	memset(&t, 0, sizeof(t));
	ct = nf_conntrack_alloc(par->net, info->zone, &t, &t, GFP_KERNEL);
	ret = PTR_ERR(ct);
	if (IS_ERR(ct))
		goto err2;

	ret = 0;
	if ((info->ct_events || info->exp_events) &&
	    !nf_ct_ecache_ext_add(ct, info->ct_events, info->exp_events,
				  GFP_KERNEL))
		goto err3;

	if (info->helper[0]) {
		ret = -ENOENT;
		proto = xt_ct_find_proto(par);
		if (!proto) {
			pr_info("You must specify a L4 protocol, "
				"and not use inversions on it.\n");
			goto err3;
		}

		ret = -ENOMEM;
		help = nf_ct_helper_ext_add(ct, GFP_KERNEL);
		if (help == NULL)
			goto err3;

		ret = -ENOENT;
		help->helper = nf_conntrack_helper_try_module_get(info->helper,
								  par->family,
								  proto);
		if (help->helper == NULL) {
			pr_info("No such helper \"%s\"\n", info->helper);
			goto err3;
		}
	}

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	if (info->timeout) {
		typeof(nf_ct_timeout_find_get_hook) timeout_find_get;
		struct ctnl_timeout *timeout;
		struct nf_conn_timeout *timeout_ext;

		rcu_read_lock();
		timeout_find_get =
			rcu_dereference(nf_ct_timeout_find_get_hook);

		if (timeout_find_get) {
			const struct ipt_entry *e = par->entryinfo;
			struct nf_conntrack_l4proto *l4proto;

			if (e->ip.invflags & IPT_INV_PROTO) {
				ret = -EINVAL;
				pr_info("You cannot use inversion on "
					 "L4 protocol\n");
				goto err4;
			}
			timeout = timeout_find_get(info->timeout);
			if (timeout == NULL) {
				ret = -ENOENT;
				pr_info("No such timeout policy \"%s\"\n",
					info->timeout);
				goto err4;
			}
			if (timeout->l3num != par->family) {
				ret = -EINVAL;
				pr_info("Timeout policy `%s' can only be "
					"used by L3 protocol number %d\n",
					info->timeout, timeout->l3num);
				goto err4;
			}
			/* Make sure the timeout policy matches any existing
			 * protocol tracker, otherwise default to generic.
			 */
			l4proto = __nf_ct_l4proto_find(par->family,
						       e->ip.proto);
			if (timeout->l4proto->l4proto != l4proto->l4proto) {
				ret = -EINVAL;
				pr_info("Timeout policy `%s' can only be "
					"used by L4 protocol number %d\n",
					info->timeout,
					timeout->l4proto->l4proto);
				goto err4;
			}
			timeout_ext = nf_ct_timeout_ext_add(ct, timeout,
							    GFP_ATOMIC);
			if (timeout_ext == NULL) {
				ret = -ENOMEM;
				goto err4;
			}
		} else {
			ret = -ENOENT;
			pr_info("Timeout policy base is empty\n");
			goto err4;
		}
		rcu_read_unlock();
	}
#endif

	__set_bit(IPS_TEMPLATE_BIT, &ct->status);
	__set_bit(IPS_CONFIRMED_BIT, &ct->status);
out:
	info->ct = ct;
	return 0;

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
err4:
	rcu_read_unlock();
#endif
err3:
	nf_conntrack_free(ct);
err2:
	nf_ct_l3proto_module_put(par->family);
err1:
	return ret;
}

static void xt_ct_tg_destroy_v0(const struct xt_tgdtor_param *par)
{
	struct xt_ct_target_info *info = par->targinfo;
	struct nf_conn *ct = info->ct;
	struct nf_conn_help *help;

	if (!nf_ct_is_untracked(ct)) {
		help = nfct_help(ct);
		if (help)
			module_put(help->helper->me);

		nf_ct_l3proto_module_put(par->family);
	}
	nf_ct_put(info->ct);
}

static void xt_ct_tg_destroy_v1(const struct xt_tgdtor_param *par)
{
	struct xt_ct_target_info_v1 *info = par->targinfo;
	struct nf_conn *ct = info->ct;
	struct nf_conn_help *help;
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	struct nf_conn_timeout *timeout_ext;
	typeof(nf_ct_timeout_put_hook) timeout_put;
#endif
	if (!nf_ct_is_untracked(ct)) {
		help = nfct_help(ct);
		if (help)
			module_put(help->helper->me);

		nf_ct_l3proto_module_put(par->family);

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
		rcu_read_lock();
		timeout_put = rcu_dereference(nf_ct_timeout_put_hook);

		if (timeout_put) {
			timeout_ext = nf_ct_timeout_find(ct);
			if (timeout_ext)
				timeout_put(timeout_ext->timeout);
		}
		rcu_read_unlock();
#endif
	}
	nf_ct_put(info->ct);
}

static struct xt_target xt_ct_tg_reg[] __read_mostly = {
	{
		.name		= "CT",
		.family		= NFPROTO_UNSPEC,
		.targetsize	= sizeof(struct xt_ct_target_info),
		.checkentry	= xt_ct_tg_check_v0,
		.destroy	= xt_ct_tg_destroy_v0,
		.target		= xt_ct_target_v0,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
	{
		.name		= "CT",
		.family		= NFPROTO_UNSPEC,
		.revision	= 1,
		.targetsize	= sizeof(struct xt_ct_target_info_v1),
		.checkentry	= xt_ct_tg_check_v1,
		.destroy	= xt_ct_tg_destroy_v1,
		.target		= xt_ct_target_v1,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
};

static int __init xt_ct_tg_init(void)
{
	return xt_register_targets(xt_ct_tg_reg, ARRAY_SIZE(xt_ct_tg_reg));
}

static void __exit xt_ct_tg_exit(void)
{
	xt_unregister_targets(xt_ct_tg_reg, ARRAY_SIZE(xt_ct_tg_reg));
}

module_init(xt_ct_tg_init);
module_exit(xt_ct_tg_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: connection tracking target");
MODULE_ALIAS("ipt_CT");
MODULE_ALIAS("ip6t_CT");
