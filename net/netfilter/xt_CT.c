/*
 * Copyright (c) 2010 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/selinux.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CT.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_zones.h>

static unsigned int xt_ct_target(struct sk_buff *skb,
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

static int xt_ct_tg_check(const struct xt_tgchk_param *par)
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
		if (!proto)
			goto err3;

		ret = -ENOMEM;
		help = nf_ct_helper_ext_add(ct, GFP_KERNEL);
		if (help == NULL)
			goto err3;

		ret = -ENOENT;
		help->helper = nf_conntrack_helper_try_module_get(info->helper,
								  par->family,
								  proto);
		if (help->helper == NULL)
			goto err3;
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

static void xt_ct_tg_destroy(const struct xt_tgdtor_param *par)
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

static struct xt_target xt_ct_tg __read_mostly = {
	.name		= "CT",
	.family		= NFPROTO_UNSPEC,
	.targetsize	= sizeof(struct xt_ct_target_info),
	.checkentry	= xt_ct_tg_check,
	.destroy	= xt_ct_tg_destroy,
	.target		= xt_ct_target,
	.table		= "raw",
	.me		= THIS_MODULE,
};

static int __init xt_ct_tg_init(void)
{
	return xt_register_target(&xt_ct_tg);
}

static void __exit xt_ct_tg_exit(void)
{
	xt_unregister_target(&xt_ct_tg);
}

module_init(xt_ct_tg_init);
module_exit(xt_ct_tg_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: connection tracking target");
MODULE_ALIAS("ipt_CT");
MODULE_ALIAS("ip6t_CT");
