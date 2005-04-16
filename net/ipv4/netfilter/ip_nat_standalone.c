/* This file contains all the functions required for the standalone
   ip_nat module.

   These are not required by the compatibility layer.
*/

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * 23 Apr 2001: Harald Welte <laforge@gnumonks.org>
 * 	- new API and handling of conntrack/nat helpers
 * 	- now capable of multiple expectations for one master
 * */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <linux/spinlock.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define HOOKNAME(hooknum) ((hooknum) == NF_IP_POST_ROUTING ? "POST_ROUTING"  \
			   : ((hooknum) == NF_IP_PRE_ROUTING ? "PRE_ROUTING" \
			      : ((hooknum) == NF_IP_LOCAL_OUT ? "LOCAL_OUT"  \
			         : ((hooknum) == NF_IP_LOCAL_IN ? "LOCAL_IN"  \
				    : "*ERROR*")))

static unsigned int
ip_nat_fn(unsigned int hooknum,
	  struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_nat_info *info;
	/* maniptype == SRC for postrouting. */
	enum ip_nat_manip_type maniptype = HOOK2MANIP(hooknum);

	/* We never see fragments: conntrack defrags on pre-routing
	   and local-out, and ip_nat_out protects post-routing. */
	IP_NF_ASSERT(!((*pskb)->nh.iph->frag_off
		       & htons(IP_MF|IP_OFFSET)));

	(*pskb)->nfcache |= NFC_UNKNOWN;

	/* If we had a hardware checksum before, it's now invalid */
	if ((*pskb)->ip_summed == CHECKSUM_HW)
		if (skb_checksum_help(*pskb, (out == NULL)))
			return NF_DROP;

	ct = ip_conntrack_get(*pskb, &ctinfo);
	/* Can't track?  It's not due to stress, or conntrack would
	   have dropped it.  Hence it's the user's responsibilty to
	   packet filter it out, or implement conntrack/NAT for that
	   protocol. 8) --RR */
	if (!ct) {
		/* Exception: ICMP redirect to new connection (not in
                   hash table yet).  We must not let this through, in
                   case we're doing NAT to the same network. */
		if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP) {
			struct icmphdr _hdr, *hp;

			hp = skb_header_pointer(*pskb,
						(*pskb)->nh.iph->ihl*4,
						sizeof(_hdr), &_hdr);
			if (hp != NULL &&
			    hp->type == ICMP_REDIRECT)
				return NF_DROP;
		}
		return NF_ACCEPT;
	}

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED+IP_CT_IS_REPLY:
		if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP) {
			if (!icmp_reply_translation(pskb, ct, maniptype,
						    CTINFO2DIR(ctinfo)))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall thru... (Only ICMPs can be IP_CT_IS_REPLY) */
	case IP_CT_NEW:
		info = &ct->nat.info;

		/* Seen it before?  This can happen for loopback, retrans,
		   or local packets.. */
		if (!ip_nat_initialized(ct, maniptype)) {
			unsigned int ret;

			/* LOCAL_IN hook doesn't have a chain!  */
			if (hooknum == NF_IP_LOCAL_IN)
				ret = alloc_null_binding(ct, info, hooknum);
			else
				ret = ip_nat_rule_find(pskb, hooknum,
						       in, out, ct,
						       info);

			if (ret != NF_ACCEPT) {
				return ret;
			}
		} else
			DEBUGP("Already setup manip %s for ct %p\n",
			       maniptype == IP_NAT_MANIP_SRC ? "SRC" : "DST",
			       ct);
		break;

	default:
		/* ESTABLISHED */
		IP_NF_ASSERT(ctinfo == IP_CT_ESTABLISHED
			     || ctinfo == (IP_CT_ESTABLISHED+IP_CT_IS_REPLY));
		info = &ct->nat.info;
	}

	IP_NF_ASSERT(info);
	return nat_packet(ct, ctinfo, hooknum, pskb);
}

static unsigned int
ip_nat_in(unsigned int hooknum,
          struct sk_buff **pskb,
          const struct net_device *in,
          const struct net_device *out,
          int (*okfn)(struct sk_buff *))
{
	u_int32_t saddr, daddr;
	unsigned int ret;

	saddr = (*pskb)->nh.iph->saddr;
	daddr = (*pskb)->nh.iph->daddr;

	ret = ip_nat_fn(hooknum, pskb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN
	    && ((*pskb)->nh.iph->saddr != saddr
	        || (*pskb)->nh.iph->daddr != daddr)) {
		dst_release((*pskb)->dst);
		(*pskb)->dst = NULL;
	}
	return ret;
}

static unsigned int
ip_nat_out(unsigned int hooknum,
	   struct sk_buff **pskb,
	   const struct net_device *in,
	   const struct net_device *out,
	   int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	/* We can hit fragment here; forwarded packets get
	   defragmented by connection tracking coming in, then
	   fragmented (grr) by the forward code.

	   In future: If we have nfct != NULL, AND we have NAT
	   initialized, AND there is no helper, then we can do full
	   NAPT on the head, and IP-address-only NAT on the rest.

	   I'm starting to have nightmares about fragments.  */

	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb, IP_DEFRAG_NAT_OUT);

		if (!*pskb)
			return NF_STOLEN;
	}

	return ip_nat_fn(hooknum, pskb, in, out, okfn);
}

static unsigned int
ip_nat_local_fn(unsigned int hooknum,
		struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
	u_int32_t saddr, daddr;
	unsigned int ret;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	saddr = (*pskb)->nh.iph->saddr;
	daddr = (*pskb)->nh.iph->daddr;

	ret = ip_nat_fn(hooknum, pskb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN
	    && ((*pskb)->nh.iph->saddr != saddr
		|| (*pskb)->nh.iph->daddr != daddr))
		return ip_route_me_harder(pskb) == 0 ? ret : NF_DROP;
	return ret;
}

/* We must be after connection tracking and before packet filtering. */

/* Before packet filtering, change destination */
static struct nf_hook_ops ip_nat_in_ops = {
	.hook		= ip_nat_in,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_PRE_ROUTING,
	.priority	= NF_IP_PRI_NAT_DST,
};

/* After packet filtering, change source */
static struct nf_hook_ops ip_nat_out_ops = {
	.hook		= ip_nat_out,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_POST_ROUTING,
	.priority	= NF_IP_PRI_NAT_SRC,
};

/* Before packet filtering, change destination */
static struct nf_hook_ops ip_nat_local_out_ops = {
	.hook		= ip_nat_local_fn,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_LOCAL_OUT,
	.priority	= NF_IP_PRI_NAT_DST,
};

/* After packet filtering, change source for reply packets of LOCAL_OUT DNAT */
static struct nf_hook_ops ip_nat_local_in_ops = {
	.hook		= ip_nat_fn,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_LOCAL_IN,
	.priority	= NF_IP_PRI_NAT_SRC,
};

static int init_or_cleanup(int init)
{
	int ret = 0;

	need_ip_conntrack();

	if (!init) goto cleanup;

	ret = ip_nat_rule_init();
	if (ret < 0) {
		printk("ip_nat_init: can't setup rules.\n");
		goto cleanup_nothing;
	}
	ret = ip_nat_init();
	if (ret < 0) {
		printk("ip_nat_init: can't setup rules.\n");
		goto cleanup_rule_init;
	}
	ret = nf_register_hook(&ip_nat_in_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register in hook.\n");
		goto cleanup_nat;
	}
	ret = nf_register_hook(&ip_nat_out_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register out hook.\n");
		goto cleanup_inops;
	}
	ret = nf_register_hook(&ip_nat_local_out_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register local out hook.\n");
		goto cleanup_outops;
	}
	ret = nf_register_hook(&ip_nat_local_in_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register local in hook.\n");
		goto cleanup_localoutops;
	}
	return ret;

 cleanup:
	nf_unregister_hook(&ip_nat_local_in_ops);
 cleanup_localoutops:
	nf_unregister_hook(&ip_nat_local_out_ops);
 cleanup_outops:
	nf_unregister_hook(&ip_nat_out_ops);
 cleanup_inops:
	nf_unregister_hook(&ip_nat_in_ops);
 cleanup_nat:
	ip_nat_cleanup();
 cleanup_rule_init:
	ip_nat_rule_cleanup();
 cleanup_nothing:
	MUST_BE_READ_WRITE_UNLOCKED(&ip_nat_lock);
	return ret;
}

static int __init init(void)
{
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

module_init(init);
module_exit(fini);

EXPORT_SYMBOL(ip_nat_setup_info);
EXPORT_SYMBOL(ip_nat_protocol_register);
EXPORT_SYMBOL(ip_nat_protocol_unregister);
EXPORT_SYMBOL(ip_nat_cheat_check);
EXPORT_SYMBOL(ip_nat_mangle_tcp_packet);
EXPORT_SYMBOL(ip_nat_mangle_udp_packet);
EXPORT_SYMBOL(ip_nat_used_tuple);
EXPORT_SYMBOL(ip_nat_follow_master);
MODULE_LICENSE("GPL");
