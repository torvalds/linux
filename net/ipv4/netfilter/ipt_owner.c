/* Kernel module to match various things tied to sockets associated with
   locally generated outgoing packets. */

/* (C) 2000 Marc Boucher <marc@mbsi.ca>
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

#include <linux/netfilter_ipv4/ipt_owner.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables owner match");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct ipt_owner_info *info = matchinfo;

	if (!skb->sk || !skb->sk->sk_socket || !skb->sk->sk_socket->file)
		return 0;

	if(info->match & IPT_OWNER_UID) {
		if ((skb->sk->sk_socket->file->f_uid != info->uid) ^
		    !!(info->invert & IPT_OWNER_UID))
			return 0;
	}

	if(info->match & IPT_OWNER_GID) {
		if ((skb->sk->sk_socket->file->f_gid != info->gid) ^
		    !!(info->invert & IPT_OWNER_GID))
			return 0;
	}

	return 1;
}

static int
checkentry(const char *tablename,
           const void *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct ipt_owner_info *info = matchinfo;

        if (hook_mask
            & ~((1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_POST_ROUTING))) {
                printk("ipt_owner: only valid for LOCAL_OUT or POST_ROUTING.\n");
                return 0;
        }

	if (matchsize != IPT_ALIGN(sizeof(struct ipt_owner_info))) {
		printk("Matchsize %u != %Zu\n", matchsize,
		       IPT_ALIGN(sizeof(struct ipt_owner_info)));
		return 0;
	}

	if (info->match & (IPT_OWNER_PID|IPT_OWNER_SID|IPT_OWNER_COMM)) {
		printk("ipt_owner: pid, sid and command matching "
		       "not supported anymore\n");
		return 0;
	}

	return 1;
}

static struct ipt_match owner_match = {
	.name		= "owner",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&owner_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&owner_match);
}

module_init(init);
module_exit(fini);
