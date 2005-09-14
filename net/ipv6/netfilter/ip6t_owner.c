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

MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("IP6 tables owner matching module");
MODULE_LICENSE("GPL");


static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct ip6t_owner_info *info = matchinfo;

	if (!skb->sk || !skb->sk->sk_socket || !skb->sk->sk_socket->file)
		return 0;

	if(info->match & IP6T_OWNER_UID) {
		if((skb->sk->sk_socket->file->f_uid != info->uid) ^
		    !!(info->invert & IP6T_OWNER_UID))
			return 0;
	}

	if(info->match & IP6T_OWNER_GID) {
		if((skb->sk->sk_socket->file->f_gid != info->gid) ^
		    !!(info->invert & IP6T_OWNER_GID))
			return 0;
	}

	return 1;
}

static int
checkentry(const char *tablename,
           const struct ip6t_ip6 *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct ip6t_owner_info *info = matchinfo;

        if (hook_mask
            & ~((1 << NF_IP6_LOCAL_OUT) | (1 << NF_IP6_POST_ROUTING))) {
                printk("ip6t_owner: only valid for LOCAL_OUT or POST_ROUTING.\n");
                return 0;
        }

	if (matchsize != IP6T_ALIGN(sizeof(struct ip6t_owner_info)))
		return 0;

	if (info->match & (IP6T_OWNER_PID|IP6T_OWNER_SID)) {
		printk("ipt_owner: pid and sid matching "
		       "not supported anymore\n");
		return 0;
	}

	return 1;
}

static struct ip6t_match owner_match = {
	.name		= "owner",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&owner_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&owner_match);
}

module_init(init);
module_exit(fini);
