/*
 * Kernel module to match various things tied to sockets associated with
 * locally generated outgoing packets.
 *
 * (C) 2000 Marc Boucher <marc@mbsi.ca>
 *
 * Copyright © CC Computer Consultants GmbH, 2007 - 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <net/sock.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>

static int owner_check(const struct xt_mtchk_param *par)
{
	struct xt_owner_match_info *info = par->matchinfo;

	/* For now only allow adding matches from the initial user namespace */
	if ((info->match & (XT_OWNER_UID|XT_OWNER_GID)) &&
	    (current_user_ns() != &init_user_ns))
		return -EINVAL;
	return 0;
}

static bool
owner_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_owner_match_info *info = par->matchinfo;
	const struct file *filp;

	if (skb->sk == NULL || skb->sk->sk_socket == NULL)
		return (info->match ^ info->invert) == 0;
	else if (info->match & info->invert & XT_OWNER_SOCKET)
		/*
		 * Socket exists but user wanted ! --socket-exists.
		 * (Single ampersands intended.)
		 */
		return false;

	filp = skb->sk->sk_socket->file;
	if (filp == NULL)
		return ((info->match ^ info->invert) &
		       (XT_OWNER_UID | XT_OWNER_GID)) == 0;

	if (info->match & XT_OWNER_UID) {
		kuid_t uid_min = make_kuid(&init_user_ns, info->uid_min);
		kuid_t uid_max = make_kuid(&init_user_ns, info->uid_max);
		if ((uid_gte(filp->f_cred->fsuid, uid_min) &&
		     uid_lte(filp->f_cred->fsuid, uid_max)) ^
		    !(info->invert & XT_OWNER_UID))
			return false;
	}

	if (info->match & XT_OWNER_GID) {
		kgid_t gid_min = make_kgid(&init_user_ns, info->gid_min);
		kgid_t gid_max = make_kgid(&init_user_ns, info->gid_max);
		if ((gid_gte(filp->f_cred->fsgid, gid_min) &&
		     gid_lte(filp->f_cred->fsgid, gid_max)) ^
		    !(info->invert & XT_OWNER_GID))
			return false;
	}

	return true;
}

static struct xt_match owner_mt_reg __read_mostly = {
	.name       = "owner",
	.revision   = 1,
	.family     = NFPROTO_UNSPEC,
	.checkentry = owner_check,
	.match      = owner_mt,
	.matchsize  = sizeof(struct xt_owner_match_info),
	.hooks      = (1 << NF_INET_LOCAL_OUT) |
	              (1 << NF_INET_POST_ROUTING),
	.me         = THIS_MODULE,
};

static int __init owner_mt_init(void)
{
	return xt_register_match(&owner_mt_reg);
}

static void __exit owner_mt_exit(void)
{
	xt_unregister_match(&owner_mt_reg);
}

module_init(owner_mt_init);
module_exit(owner_mt_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: socket owner matching");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_owner");
MODULE_ALIAS("ip6t_owner");
