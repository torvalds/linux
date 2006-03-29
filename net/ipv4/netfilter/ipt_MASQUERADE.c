/* Masquerade.  Simple mapping which alters range to a local IP address
   (depending on route). */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <net/route.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables MASQUERADE target module");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Lock protects masq region inside conntrack */
static DEFINE_RWLOCK(masq_lock);

/* FIXME: Multiple targets. --RR */
static int
masquerade_check(const char *tablename,
		 const void *e,
		 const struct xt_target *target,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
	const struct ip_nat_multi_range_compat *mr = targinfo;

	if (mr->range[0].flags & IP_NAT_RANGE_MAP_IPS) {
		DEBUGP("masquerade_check: bad MAP_IPS.\n");
		return 0;
	}
	if (mr->rangesize != 1) {
		DEBUGP("masquerade_check: bad rangesize %u.\n", mr->rangesize);
		return 0;
	}
	return 1;
}

static unsigned int
masquerade_target(struct sk_buff **pskb,
		  const struct net_device *in,
		  const struct net_device *out,
		  unsigned int hooknum,
		  const struct xt_target *target,
		  const void *targinfo,
		  void *userinfo)
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	const struct ip_nat_multi_range_compat *mr;
	struct ip_nat_range newrange;
	struct rtable *rt;
	u_int32_t newsrc;

	IP_NF_ASSERT(hooknum == NF_IP_POST_ROUTING);

	ct = ip_conntrack_get(*pskb, &ctinfo);
	IP_NF_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED
	                    || ctinfo == IP_CT_RELATED + IP_CT_IS_REPLY));

	/* Source address is 0.0.0.0 - locally generated packet that is
	 * probably not supposed to be masqueraded.
	 */
	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip == 0)
		return NF_ACCEPT;

	mr = targinfo;
	rt = (struct rtable *)(*pskb)->dst;
	newsrc = inet_select_addr(out, rt->rt_gateway, RT_SCOPE_UNIVERSE);
	if (!newsrc) {
		printk("MASQUERADE: %s ate my IP address\n", out->name);
		return NF_DROP;
	}

	write_lock_bh(&masq_lock);
	ct->nat.masq_index = out->ifindex;
	write_unlock_bh(&masq_lock);

	/* Transfer from original range. */
	newrange = ((struct ip_nat_range)
		{ mr->range[0].flags | IP_NAT_RANGE_MAP_IPS,
		  newsrc, newsrc,
		  mr->range[0].min, mr->range[0].max });

	/* Hand modified range to generic setup. */
	return ip_nat_setup_info(ct, &newrange, hooknum);
}

static inline int
device_cmp(struct ip_conntrack *i, void *ifindex)
{
	int ret;

	read_lock_bh(&masq_lock);
	ret = (i->nat.masq_index == (int)(long)ifindex);
	read_unlock_bh(&masq_lock);

	return ret;
}

static int masq_device_event(struct notifier_block *this,
			     unsigned long event,
			     void *ptr)
{
	struct net_device *dev = ptr;

	if (event == NETDEV_DOWN) {
		/* Device was downed.  Search entire table for
		   conntracks which were associated with that device,
		   and forget them. */
		IP_NF_ASSERT(dev->ifindex != 0);

		ip_ct_iterate_cleanup(device_cmp, (void *)(long)dev->ifindex);
	}

	return NOTIFY_DONE;
}

static int masq_inet_event(struct notifier_block *this,
			   unsigned long event,
			   void *ptr)
{
	struct net_device *dev = ((struct in_ifaddr *)ptr)->ifa_dev->dev;

	if (event == NETDEV_DOWN) {
		/* IP address was deleted.  Search entire table for
		   conntracks which were associated with that device,
		   and forget them. */
		IP_NF_ASSERT(dev->ifindex != 0);

		ip_ct_iterate_cleanup(device_cmp, (void *)(long)dev->ifindex);
	}

	return NOTIFY_DONE;
}

static struct notifier_block masq_dev_notifier = {
	.notifier_call	= masq_device_event,
};

static struct notifier_block masq_inet_notifier = {
	.notifier_call	= masq_inet_event,
};

static struct ipt_target masquerade = {
	.name		= "MASQUERADE",
	.target		= masquerade_target,
	.targetsize	= sizeof(struct ip_nat_multi_range_compat),
	.table		= "nat",
	.hooks		= 1 << NF_IP_POST_ROUTING,
	.checkentry	= masquerade_check,
	.me		= THIS_MODULE,
};

static int __init ipt_masquerade_init(void)
{
	int ret;

	ret = ipt_register_target(&masquerade);

	if (ret == 0) {
		/* Register for device down reports */
		register_netdevice_notifier(&masq_dev_notifier);
		/* Register IP address change reports */
		register_inetaddr_notifier(&masq_inet_notifier);
	}

	return ret;
}

static void __exit ipt_masquerade_fini(void)
{
	ipt_unregister_target(&masquerade);
	unregister_netdevice_notifier(&masq_dev_notifier);
	unregister_inetaddr_notifier(&masq_inet_notifier);	
}

module_init(ipt_masquerade_init);
module_exit(ipt_masquerade_fini);
