/*
 * Xtables module to match the process control group.
 *
 * Might be used to implement individual "per-application" firewall
 * policies in contrast to global policies based on control groups.
 * Matching is based upon processes tagged to net_cls' classid marker.
 *
 * (C) 2013 Daniel Borkmann <dborkman@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_cgroup.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Borkmann <dborkman@redhat.com>");
MODULE_DESCRIPTION("Xtables: process control group matching");
MODULE_ALIAS("ipt_cgroup");
MODULE_ALIAS("ip6t_cgroup");

static int cgroup_mt_check_v0(const struct xt_mtchk_param *par)
{
	struct xt_cgroup_info_v0 *info = par->matchinfo;

	if (info->invert & ~1)
		return -EINVAL;

	return 0;
}

static int cgroup_mt_check_v1(const struct xt_mtchk_param *par)
{
	struct xt_cgroup_info_v1 *info = par->matchinfo;
	struct cgroup *cgrp;

	if ((info->invert_path & ~1) || (info->invert_classid & ~1))
		return -EINVAL;

	if (!info->has_path && !info->has_classid) {
		pr_info("xt_cgroup: no path or classid specified\n");
		return -EINVAL;
	}

	if (info->has_path && info->has_classid) {
		pr_info_ratelimited("path and classid specified\n");
		return -EINVAL;
	}

	info->priv = NULL;
	if (info->has_path) {
		cgrp = cgroup_get_from_path(info->path);
		if (IS_ERR(cgrp)) {
			pr_info_ratelimited("invalid path, errno=%ld\n",
					    PTR_ERR(cgrp));
			return -EINVAL;
		}
		info->priv = cgrp;
	}

	return 0;
}

static int cgroup_mt_check_v2(const struct xt_mtchk_param *par)
{
	struct xt_cgroup_info_v2 *info = par->matchinfo;
	struct cgroup *cgrp;

	if ((info->invert_path & ~1) || (info->invert_classid & ~1))
		return -EINVAL;

	if (!info->has_path && !info->has_classid) {
		pr_info("xt_cgroup: no path or classid specified\n");
		return -EINVAL;
	}

	if (info->has_path && info->has_classid) {
		pr_info_ratelimited("path and classid specified\n");
		return -EINVAL;
	}

	info->priv = NULL;
	if (info->has_path) {
		cgrp = cgroup_get_from_path(info->path);
		if (IS_ERR(cgrp)) {
			pr_info_ratelimited("invalid path, errno=%ld\n",
					    PTR_ERR(cgrp));
			return -EINVAL;
		}
		info->priv = cgrp;
	}

	return 0;
}

static bool
cgroup_mt_v0(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_cgroup_info_v0 *info = par->matchinfo;
	struct sock *sk = skb->sk;

	if (!sk || !sk_fullsock(sk) || !net_eq(xt_net(par), sock_net(sk)))
		return false;

	return (info->id == sock_cgroup_classid(&skb->sk->sk_cgrp_data)) ^
		info->invert;
}

static bool cgroup_mt_v1(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_cgroup_info_v1 *info = par->matchinfo;
	struct sock_cgroup_data *skcd = &skb->sk->sk_cgrp_data;
	struct cgroup *ancestor = info->priv;
	struct sock *sk = skb->sk;

	if (!sk || !sk_fullsock(sk) || !net_eq(xt_net(par), sock_net(sk)))
		return false;

	if (ancestor)
		return cgroup_is_descendant(sock_cgroup_ptr(skcd), ancestor) ^
			info->invert_path;
	else
		return (info->classid == sock_cgroup_classid(skcd)) ^
			info->invert_classid;
}

static bool cgroup_mt_v2(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_cgroup_info_v2 *info = par->matchinfo;
	struct sock_cgroup_data *skcd = &skb->sk->sk_cgrp_data;
	struct cgroup *ancestor = info->priv;
	struct sock *sk = skb->sk;

	if (!sk || !sk_fullsock(sk) || !net_eq(xt_net(par), sock_net(sk)))
		return false;

	if (ancestor)
		return cgroup_is_descendant(sock_cgroup_ptr(skcd), ancestor) ^
			info->invert_path;
	else
		return (info->classid == sock_cgroup_classid(skcd)) ^
			info->invert_classid;
}

static void cgroup_mt_destroy_v1(const struct xt_mtdtor_param *par)
{
	struct xt_cgroup_info_v1 *info = par->matchinfo;

	if (info->priv)
		cgroup_put(info->priv);
}

static void cgroup_mt_destroy_v2(const struct xt_mtdtor_param *par)
{
	struct xt_cgroup_info_v2 *info = par->matchinfo;

	if (info->priv)
		cgroup_put(info->priv);
}

static struct xt_match cgroup_mt_reg[] __read_mostly = {
	{
		.name		= "cgroup",
		.revision	= 0,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= cgroup_mt_check_v0,
		.match		= cgroup_mt_v0,
		.matchsize	= sizeof(struct xt_cgroup_info_v0),
		.me		= THIS_MODULE,
		.hooks		= (1 << NF_INET_LOCAL_OUT) |
				  (1 << NF_INET_POST_ROUTING) |
				  (1 << NF_INET_LOCAL_IN),
	},
	{
		.name		= "cgroup",
		.revision	= 1,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= cgroup_mt_check_v1,
		.match		= cgroup_mt_v1,
		.matchsize	= sizeof(struct xt_cgroup_info_v1),
		.usersize	= offsetof(struct xt_cgroup_info_v1, priv),
		.destroy	= cgroup_mt_destroy_v1,
		.me		= THIS_MODULE,
		.hooks		= (1 << NF_INET_LOCAL_OUT) |
				  (1 << NF_INET_POST_ROUTING) |
				  (1 << NF_INET_LOCAL_IN),
	},
	{
		.name		= "cgroup",
		.revision	= 2,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= cgroup_mt_check_v2,
		.match		= cgroup_mt_v2,
		.matchsize	= sizeof(struct xt_cgroup_info_v2),
		.usersize	= offsetof(struct xt_cgroup_info_v2, priv),
		.destroy	= cgroup_mt_destroy_v2,
		.me		= THIS_MODULE,
		.hooks		= (1 << NF_INET_LOCAL_OUT) |
				  (1 << NF_INET_POST_ROUTING) |
				  (1 << NF_INET_LOCAL_IN),
	},
};

static int __init cgroup_mt_init(void)
{
	return xt_register_matches(cgroup_mt_reg, ARRAY_SIZE(cgroup_mt_reg));
}

static void __exit cgroup_mt_exit(void)
{
	xt_unregister_matches(cgroup_mt_reg, ARRAY_SIZE(cgroup_mt_reg));
}

module_init(cgroup_mt_init);
module_exit(cgroup_mt_exit);
