/* Kernel module to match various things tied to sockets associated with
   locally generated outgoing packets. */

/* (C) 2000-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/rcupdate.h>
#include <net/sock.h>

#include <linux/netfilter_ipv6/ip6t_owner.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("IP6 tables owner matching module");
MODULE_LICENSE("GPL");


static bool
owner_mt6(const struct sk_buff *skb, const struct net_device *in,
          const struct net_device *out, const struct xt_match *match,
          const void *matchinfo, int offset, unsigned int protoff,
          bool *hotdrop)
{
	const struct ip6t_owner_info *info = matchinfo;

	if (!skb->sk || !skb->sk->sk_socket || !skb->sk->sk_socket->file)
		return false;

	if (info->match & IP6T_OWNER_UID)
		if ((skb->sk->sk_socket->file->f_uid != info->uid) ^
		    !!(info->invert & IP6T_OWNER_UID))
			return false;

	if (info->match & IP6T_OWNER_GID)
		if ((skb->sk->sk_socket->file->f_gid != info->gid) ^
		    !!(info->invert & IP6T_OWNER_GID))
			return false;

	return true;
}

static bool
owner_mt6_check(const char *tablename, const void *ip,
                const struct xt_match *match, void *matchinfo,
                unsigned int hook_mask)
{
	const struct ip6t_owner_info *info = matchinfo;

	if (info->match & (IP6T_OWNER_PID | IP6T_OWNER_SID)) {
		printk("ipt_owner: pid and sid matching "
		       "not supported anymore\n");
		return false;
	}
	return true;
}

static struct xt_match owner_mt6_reg __read_mostly = {
	.name		= "owner",
	.family		= AF_INET6,
	.match		= owner_mt6,
	.matchsize	= sizeof(struct ip6t_owner_info),
	.hooks		= (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_POST_ROUTING),
	.checkentry	= owner_mt6_check,
	.me		= THIS_MODULE,
};

static int __init owner_mt6_init(void)
{
	return xt_register_match(&owner_mt6_reg);
}

static void __exit owner_mt6_exit(void)
{
	xt_unregister_match(&owner_mt6_reg);
}

module_init(owner_mt6_init);
module_exit(owner_mt6_exit);
