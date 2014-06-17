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

static int cgroup_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_cgroup_info *info = par->matchinfo;

	if (info->invert & ~1)
		return -EINVAL;

	return info->id ? 0 : -EINVAL;
}

static bool
cgroup_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_cgroup_info *info = par->matchinfo;

	if (skb->sk == NULL)
		return false;

	return (info->id == skb->sk->sk_classid) ^ info->invert;
}

static struct xt_match cgroup_mt_reg __read_mostly = {
	.name       = "cgroup",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = cgroup_mt_check,
	.match      = cgroup_mt,
	.matchsize  = sizeof(struct xt_cgroup_info),
	.me         = THIS_MODULE,
	.hooks      = (1 << NF_INET_LOCAL_OUT) |
		      (1 << NF_INET_POST_ROUTING) |
		      (1 << NF_INET_LOCAL_IN),
};

static int __init cgroup_mt_init(void)
{
	return xt_register_match(&cgroup_mt_reg);
}

static void __exit cgroup_mt_exit(void)
{
	xt_unregister_match(&cgroup_mt_reg);
}

module_init(cgroup_mt_init);
module_exit(cgroup_mt_exit);
