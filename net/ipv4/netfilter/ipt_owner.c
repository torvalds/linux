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
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables owner match");

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct ipt_owner_info *info = matchinfo;

	if (!skb->sk || !skb->sk->sk_socket || !skb->sk->sk_socket->file)
		return false;

	if(info->match & IPT_OWNER_UID) {
		if ((skb->sk->sk_socket->file->f_uid != info->uid) ^
		    !!(info->invert & IPT_OWNER_UID))
			return false;
	}

	if(info->match & IPT_OWNER_GID) {
		if ((skb->sk->sk_socket->file->f_gid != info->gid) ^
		    !!(info->invert & IPT_OWNER_GID))
			return false;
	}

	return true;
}

static bool
checkentry(const char *tablename,
	   const void *ip,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct ipt_owner_info *info = matchinfo;

	if (info->match & (IPT_OWNER_PID|IPT_OWNER_SID|IPT_OWNER_COMM)) {
		printk("ipt_owner: pid, sid and command matching "
		       "not supported anymore\n");
		return false;
	}
	return true;
}

static struct xt_match owner_match __read_mostly = {
	.name		= "owner",
	.family		= AF_INET,
	.match		= match,
	.matchsize	= sizeof(struct ipt_owner_info),
	.hooks		= (1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_POST_ROUTING),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ipt_owner_init(void)
{
	return xt_register_match(&owner_match);
}

static void __exit ipt_owner_fini(void)
{
	xt_unregister_match(&owner_match);
}

module_init(ipt_owner_init);
module_exit(ipt_owner_fini);
