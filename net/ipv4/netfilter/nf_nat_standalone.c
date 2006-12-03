/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
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

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_nat_protocol.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/netfilter_ipv4/ip_tables.h>

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

#ifdef CONFIG_XFRM
static void nat_decode_session(struct sk_buff *skb, struct flowi *fl)
{
	struct nf_conn *ct;
	struct nf_conntrack_tuple *t;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	unsigned long statusbit;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct == NULL)
		return;
	dir = CTINFO2DIR(ctinfo);
	t = &ct->tuplehash[dir].tuple;

	if (dir == IP_CT_DIR_ORIGINAL)
		statusbit = IPS_DST_NAT;
	else
		statusbit = IPS_SRC_NAT;

	if (ct->status & statusbit) {
		fl->fl4_dst = t->dst.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP)
			fl->fl_ip_dport = t->dst.u.tcp.port;
	}

	statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		fl->fl4_src = t->src.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP)
			fl->fl_ip_sport = t->src.u.tcp.port;
	}
}
#endif

static unsigned int
nf_nat_fn(unsigned int hooknum,
	  struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	struct nf_nat_info *info;
	/* maniptype == SRC for postrouting. */
	enum nf_nat_manip_type maniptype = HOOK2MANIP(hooknum);

	/* We never see fragments: conntrack defrags on pre-routing
	   and local-out, and nf_nat_out protects post-routing. */
	NF_CT_ASSERT(!((*pskb)->nh.iph->frag_off
		       & htons(IP_MF|IP_OFFSET)));

	ct = nf_ct_get(*pskb, &ctinfo);
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

	/* Don't try to NAT if this packet is not conntracked */
	if (ct == &nf_conntrack_untracked)
		return NF_ACCEPT;

	nat = nfct_nat(ct);
	if (!nat)
		return NF_DROP;

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED+IP_CT_IS_REPLY:
		if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(ct, ctinfo,
							   hooknum, pskb))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall thru... (Only ICMPs can be IP_CT_IS_REPLY) */
	case IP_CT_NEW:
		info = &nat->info;

		/* Seen it before?  This can happen for loopback, retrans,
		   or local packets.. */
		if (!nf_nat_initialized(ct, maniptype)) {
			unsigned int ret;

			if (unlikely(nf_ct_is_confirmed(ct)))
				/* NAT module was loaded late */
				ret = alloc_null_binding_confirmed(ct, info,
				                                   hooknum);
			else if (hooknum == NF_IP_LOCAL_IN)
				/* LOCAL_IN hook doesn't have a chain!  */
				ret = alloc_null_binding(ct, info, hooknum);
			else
				ret = nf_nat_rule_find(pskb, hooknum, in, out,
						       ct, info);

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
		NF_CT_ASSERT(ctinfo == IP_CT_ESTABLISHED ||
			     ctinfo == (IP_CT_ESTABLISHED+IP_CT_IS_REPLY));
		info = &nat->info;
	}

	NF_CT_ASSERT(info);
	return nf_nat_packet(ct, ctinfo, hooknum, pskb);
}

static unsigned int
nf_nat_in(unsigned int hooknum,
          struct sk_buff **pskb,
          const struct net_device *in,
          const struct net_device *out,
          int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	__be32 daddr = (*pskb)->nh.iph->daddr;

	ret = nf_nat_fn(hooknum, pskb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    daddr != (*pskb)->nh.iph->daddr) {
		dst_release((*pskb)->dst);
		(*pskb)->dst = NULL;
	}
	return ret;
}

static unsigned int
nf_nat_out(unsigned int hooknum,
	   struct sk_buff **pskb,
	   const struct net_device *in,
	   const struct net_device *out,
	   int (*okfn)(struct sk_buff *))
{
#ifdef CONFIG_XFRM
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
#endif
	unsigned int ret;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr) ||
	    (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	ret = nf_nat_fn(hooknum, pskb, in, out, okfn);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(*pskb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.src.u3.ip !=
		    ct->tuplehash[!dir].tuple.dst.u3.ip
		    || ct->tuplehash[dir].tuple.src.u.all !=
		       ct->tuplehash[!dir].tuple.dst.u.all
		    )
			return ip_xfrm_me_harder(pskb) == 0 ? ret : NF_DROP;
	}
#endif
	return ret;
}

static unsigned int
nf_nat_local_fn(unsigned int hooknum,
		struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr) ||
	    (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	ret = nf_nat_fn(hooknum, pskb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(*pskb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.dst.u3.ip !=
		    ct->tuplehash[!dir].tuple.src.u3.ip
#ifdef CONFIG_XFRM
		    || ct->tuplehash[dir].tuple.dst.u.all !=
		       ct->tuplehash[!dir].tuple.src.u.all
#endif
		    )
			if (ip_route_me_harder(pskb, RTN_UNSPEC))
				ret = NF_DROP;
	}
	return ret;
}

static unsigned int
nf_nat_adjust(unsigned int hooknum,
	      struct sk_buff **pskb,
	      const struct net_device *in,
	      const struct net_device *out,
	      int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(*pskb, &ctinfo);
	if (ct && test_bit(IPS_SEQ_ADJUST_BIT, &ct->status)) {
	        DEBUGP("nf_nat_standalone: adjusting sequence number\n");
	        if (!nf_nat_seq_adjust(pskb, ct, ctinfo))
	                return NF_DROP;
	}
	return NF_ACCEPT;
}

/* We must be after connection tracking and before packet filtering. */

static struct nf_hook_ops nf_nat_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_in,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_out,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	/* After conntrack, adjust sequence number */
	{
		.hook		= nf_nat_adjust,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SEQ_ADJUST,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_local_fn,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_fn,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	/* After conntrack, adjust sequence number */
	{
		.hook		= nf_nat_adjust,
		.owner		= THIS_MODULE,
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SEQ_ADJUST,
	},
};

static int __init nf_nat_standalone_init(void)
{
	int size, ret = 0;

	need_conntrack();

	size = ALIGN(sizeof(struct nf_conn), __alignof__(struct nf_conn_nat)) +
	       sizeof(struct nf_conn_nat);
	ret = nf_conntrack_register_cache(NF_CT_F_NAT, "nf_nat:base", size);
	if (ret < 0) {
		printk(KERN_ERR "nf_nat_init: Unable to create slab cache\n");
		return ret;
	}

	size = ALIGN(size, __alignof__(struct nf_conn_help)) +
	       sizeof(struct nf_conn_help);
	ret = nf_conntrack_register_cache(NF_CT_F_NAT|NF_CT_F_HELP,
					  "nf_nat:help", size);
	if (ret < 0) {
		printk(KERN_ERR "nf_nat_init: Unable to create slab cache\n");
		goto cleanup_register_cache;
	}
#ifdef CONFIG_XFRM
	BUG_ON(ip_nat_decode_session != NULL);
	ip_nat_decode_session = nat_decode_session;
#endif
	ret = nf_nat_rule_init();
	if (ret < 0) {
		printk("nf_nat_init: can't setup rules.\n");
		goto cleanup_decode_session;
	}
	ret = nf_register_hooks(nf_nat_ops, ARRAY_SIZE(nf_nat_ops));
	if (ret < 0) {
		printk("nf_nat_init: can't register hooks.\n");
		goto cleanup_rule_init;
	}
	nf_nat_module_is_loaded = 1;
	return ret;

 cleanup_rule_init:
	nf_nat_rule_cleanup();
 cleanup_decode_session:
#ifdef CONFIG_XFRM
	ip_nat_decode_session = NULL;
	synchronize_net();
#endif
	nf_conntrack_unregister_cache(NF_CT_F_NAT|NF_CT_F_HELP);
 cleanup_register_cache:
	nf_conntrack_unregister_cache(NF_CT_F_NAT);
	return ret;
}

static void __exit nf_nat_standalone_fini(void)
{
	nf_unregister_hooks(nf_nat_ops, ARRAY_SIZE(nf_nat_ops));
	nf_nat_rule_cleanup();
	nf_nat_module_is_loaded = 0;
#ifdef CONFIG_XFRM
	ip_nat_decode_session = NULL;
	synchronize_net();
#endif
	/* Conntrack caches are unregistered in nf_conntrack_cleanup */
}

module_init(nf_nat_standalone_init);
module_exit(nf_nat_standalone_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_nat");
