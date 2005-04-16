/* Kernel module to match connection tracking information.
 * Superset of Rusty's minimalistic state match.
 *
 * (C) 2001  Marc Boucher (marc@mbsi.ca).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_conntrack.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables connection tracking match module");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	const struct ipt_conntrack_info *sinfo = matchinfo;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int statebit;

	ct = ip_conntrack_get((struct sk_buff *)skb, &ctinfo);

#define FWINV(bool,invflg) ((bool) ^ !!(sinfo->invflags & invflg))

	if (ct == &ip_conntrack_untracked)
		statebit = IPT_CONNTRACK_STATE_UNTRACKED;
	else if (ct)
 		statebit = IPT_CONNTRACK_STATE_BIT(ctinfo);
 	else
 		statebit = IPT_CONNTRACK_STATE_INVALID;
 
	if(sinfo->flags & IPT_CONNTRACK_STATE) {
		if (ct) {
			if(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip !=
			    ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip)
				statebit |= IPT_CONNTRACK_STATE_SNAT;

			if(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip !=
			    ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip)
				statebit |= IPT_CONNTRACK_STATE_DNAT;
		}

		if (FWINV((statebit & sinfo->statemask) == 0, IPT_CONNTRACK_STATE))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_PROTO) {
		if (!ct || FWINV(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum != sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.protonum, IPT_CONNTRACK_PROTO))
                	return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_ORIGSRC) {
		if (!ct || FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip&sinfo->sipmsk[IP_CT_DIR_ORIGINAL].s_addr) != sinfo->tuple[IP_CT_DIR_ORIGINAL].src.ip, IPT_CONNTRACK_ORIGSRC))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_ORIGDST) {
		if (!ct || FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip&sinfo->dipmsk[IP_CT_DIR_ORIGINAL].s_addr) != sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.ip, IPT_CONNTRACK_ORIGDST))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_REPLSRC) {
		if (!ct || FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip&sinfo->sipmsk[IP_CT_DIR_REPLY].s_addr) != sinfo->tuple[IP_CT_DIR_REPLY].src.ip, IPT_CONNTRACK_REPLSRC))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_REPLDST) {
		if (!ct || FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip&sinfo->dipmsk[IP_CT_DIR_REPLY].s_addr) != sinfo->tuple[IP_CT_DIR_REPLY].dst.ip, IPT_CONNTRACK_REPLDST))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_STATUS) {
		if (!ct || FWINV((ct->status & sinfo->statusmask) == 0, IPT_CONNTRACK_STATUS))
			return 0;
	}

	if(sinfo->flags & IPT_CONNTRACK_EXPIRES) {
		unsigned long expires;

		if(!ct)
			return 0;

		expires = timer_pending(&ct->timeout) ? (ct->timeout.expires - jiffies)/HZ : 0;

		if (FWINV(!(expires >= sinfo->expires_min && expires <= sinfo->expires_max), IPT_CONNTRACK_EXPIRES))
			return 0;
	}

	return 1;
}

static int check(const char *tablename,
		 const struct ipt_ip *ip,
		 void *matchinfo,
		 unsigned int matchsize,
		 unsigned int hook_mask)
{
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_conntrack_info)))
		return 0;

	return 1;
}

static struct ipt_match conntrack_match = {
	.name		= "conntrack",
	.match		= &match,
	.checkentry	= &check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	need_ip_conntrack();
	return ipt_register_match(&conntrack_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&conntrack_match);
}

module_init(init);
module_exit(fini);
