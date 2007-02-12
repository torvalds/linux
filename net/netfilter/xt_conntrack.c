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

#if defined(CONFIG_IP_NF_CONNTRACK) || defined(CONFIG_IP_NF_CONNTRACK_MODULE)
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#else
#include <net/netfilter/nf_conntrack.h>
#endif

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_conntrack.h>
#include <net/netfilter/nf_conntrack_compat.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables connection tracking match module");
MODULE_ALIAS("ipt_conntrack");

#if defined(CONFIG_IP_NF_CONNTRACK) || defined(CONFIG_IP_NF_CONNTRACK_MODULE)

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_conntrack_info *sinfo = matchinfo;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int statebit;

	ct = ip_conntrack_get((struct sk_buff *)skb, &ctinfo);

#define FWINV(bool, invflg) ((bool) ^ !!(sinfo->invflags & invflg))

	if (ct == &ip_conntrack_untracked)
		statebit = XT_CONNTRACK_STATE_UNTRACKED;
	else if (ct)
		statebit = XT_CONNTRACK_STATE_BIT(ctinfo);
	else
		statebit = XT_CONNTRACK_STATE_INVALID;

	if (sinfo->flags & XT_CONNTRACK_STATE) {
		if (ct) {
			if (test_bit(IPS_SRC_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_SNAT;
			if (test_bit(IPS_DST_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_DNAT;
		}
		if (FWINV((statebit & sinfo->statemask) == 0,
			  XT_CONNTRACK_STATE))
			return 0;
	}

	if (ct == NULL) {
		if (sinfo->flags & ~XT_CONNTRACK_STATE)
			return 0;
		return 1;
	}

	if (sinfo->flags & XT_CONNTRACK_PROTO &&
	    FWINV(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.protonum,
		  XT_CONNTRACK_PROTO))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_ORIGSRC &&
	    FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip &
		   sinfo->sipmsk[IP_CT_DIR_ORIGINAL].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].src.ip,
		  XT_CONNTRACK_ORIGSRC))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_ORIGDST &&
	    FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip &
		   sinfo->dipmsk[IP_CT_DIR_ORIGINAL].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.ip,
		  XT_CONNTRACK_ORIGDST))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_REPLSRC &&
	    FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip &
		   sinfo->sipmsk[IP_CT_DIR_REPLY].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_REPLY].src.ip,
		  XT_CONNTRACK_REPLSRC))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_REPLDST &&
	    FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip &
		   sinfo->dipmsk[IP_CT_DIR_REPLY].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_REPLY].dst.ip,
		  XT_CONNTRACK_REPLDST))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_STATUS &&
	    FWINV((ct->status & sinfo->statusmask) == 0,
		  XT_CONNTRACK_STATUS))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_EXPIRES) {
		unsigned long expires = timer_pending(&ct->timeout) ?
					(ct->timeout.expires - jiffies)/HZ : 0;

		if (FWINV(!(expires >= sinfo->expires_min &&
			    expires <= sinfo->expires_max),
			  XT_CONNTRACK_EXPIRES))
			return 0;
	}
	return 1;
}

#else /* CONFIG_IP_NF_CONNTRACK */
static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_conntrack_info *sinfo = matchinfo;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int statebit;

	ct = nf_ct_get((struct sk_buff *)skb, &ctinfo);

#define FWINV(bool,invflg) ((bool) ^ !!(sinfo->invflags & invflg))

	if (ct == &nf_conntrack_untracked)
		statebit = XT_CONNTRACK_STATE_UNTRACKED;
	else if (ct)
		statebit = XT_CONNTRACK_STATE_BIT(ctinfo);
	else
		statebit = XT_CONNTRACK_STATE_INVALID;

	if (sinfo->flags & XT_CONNTRACK_STATE) {
		if (ct) {
			if (test_bit(IPS_SRC_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_SNAT;
			if (test_bit(IPS_DST_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_DNAT;
		}
		if (FWINV((statebit & sinfo->statemask) == 0,
			  XT_CONNTRACK_STATE))
			return 0;
	}

	if (ct == NULL) {
		if (sinfo->flags & ~XT_CONNTRACK_STATE)
			return 0;
		return 1;
	}

	if (sinfo->flags & XT_CONNTRACK_PROTO &&
	    FWINV(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.protonum,
		  XT_CONNTRACK_PROTO))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_ORIGSRC &&
	    FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip &
		   sinfo->sipmsk[IP_CT_DIR_ORIGINAL].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].src.ip,
		  XT_CONNTRACK_ORIGSRC))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_ORIGDST &&
	    FWINV((ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.ip &
		   sinfo->dipmsk[IP_CT_DIR_ORIGINAL].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_ORIGINAL].dst.ip,
		  XT_CONNTRACK_ORIGDST))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_REPLSRC &&
	    FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.ip &
		   sinfo->sipmsk[IP_CT_DIR_REPLY].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_REPLY].src.ip,
		  XT_CONNTRACK_REPLSRC))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_REPLDST &&
	    FWINV((ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.ip &
		   sinfo->dipmsk[IP_CT_DIR_REPLY].s_addr) !=
		  sinfo->tuple[IP_CT_DIR_REPLY].dst.ip,
		  XT_CONNTRACK_REPLDST))
		return 0;

	if (sinfo->flags & XT_CONNTRACK_STATUS &&
	    FWINV((ct->status & sinfo->statusmask) == 0,
		  XT_CONNTRACK_STATUS))
		return 0;

	if(sinfo->flags & XT_CONNTRACK_EXPIRES) {
		unsigned long expires = timer_pending(&ct->timeout) ?
					(ct->timeout.expires - jiffies)/HZ : 0;

		if (FWINV(!(expires >= sinfo->expires_min &&
			    expires <= sinfo->expires_max),
			  XT_CONNTRACK_EXPIRES))
			return 0;
	}
	return 1;
}

#endif /* CONFIG_NF_IP_CONNTRACK */

static int
checkentry(const char *tablename,
	   const void *ip,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%d\n", match->family);
		return 0;
	}
	return 1;
}

static void destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match conntrack_match = {
	.name		= "conntrack",
	.match		= match,
	.checkentry	= checkentry,
	.destroy	= destroy,
	.matchsize	= sizeof(struct xt_conntrack_info),
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static int __init xt_conntrack_init(void)
{
	return xt_register_match(&conntrack_match);
}

static void __exit xt_conntrack_fini(void)
{
	xt_unregister_match(&conntrack_match);
}

module_init(xt_conntrack_init);
module_exit(xt_conntrack_fini);
