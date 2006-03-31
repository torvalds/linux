/*
 * Copyright (C)2004 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- support Layer 3 protocol independent connection tracking.
 *	  Based on the original ip_conntrack code which	had the following
 *	  copyright information:
 *		(C) 1999-2001 Paul `Rusty' Russell
 *		(C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * 23 Mar 2004: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- add get_features() to support various size of conntrack
 *	  structures.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/ipv6.h>

#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_core.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_PER_CPU(struct ip_conntrack_stat, nf_conntrack_stat);

static int ipv6_pkt_to_tuple(const struct sk_buff *skb, unsigned int nhoff,
			     struct nf_conntrack_tuple *tuple)
{
	u_int32_t _addrs[8], *ap;

	ap = skb_header_pointer(skb, nhoff + offsetof(struct ipv6hdr, saddr),
				sizeof(_addrs), _addrs);
	if (ap == NULL)
		return 0;

	memcpy(tuple->src.u3.ip6, ap, sizeof(tuple->src.u3.ip6));
	memcpy(tuple->dst.u3.ip6, ap + 4, sizeof(tuple->dst.u3.ip6));

	return 1;
}

static int ipv6_invert_tuple(struct nf_conntrack_tuple *tuple,
			     const struct nf_conntrack_tuple *orig)
{
	memcpy(tuple->src.u3.ip6, orig->dst.u3.ip6, sizeof(tuple->src.u3.ip6));
	memcpy(tuple->dst.u3.ip6, orig->src.u3.ip6, sizeof(tuple->dst.u3.ip6));

	return 1;
}

static int ipv6_print_tuple(struct seq_file *s,
			    const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "src=" NIP6_FMT " dst=" NIP6_FMT " ",
			  NIP6(*((struct in6_addr *)tuple->src.u3.ip6)),
			  NIP6(*((struct in6_addr *)tuple->dst.u3.ip6)));
}

static int ipv6_print_conntrack(struct seq_file *s,
				const struct nf_conn *conntrack)
{
	return 0;
}

/*
 * Based on ipv6_skip_exthdr() in net/ipv6/exthdr.c
 *
 * This function parses (probably truncated) exthdr set "hdr"
 * of length "len". "nexthdrp" initially points to some place,
 * where type of the first header can be found.
 *
 * It skips all well-known exthdrs, and returns pointer to the start
 * of unparsable area i.e. the first header with unknown type.
 * if success, *nexthdr is updated by type/protocol of this header.
 *
 * NOTES: - it may return pointer pointing beyond end of packet,
 *          if the last recognized header is truncated in the middle.
 *        - if packet is truncated, so that all parsed headers are skipped,
 *          it returns -1.
 *        - if packet is fragmented, return pointer of the fragment header.
 *        - ESP is unparsable for now and considered like
 *          normal payload protocol.
 *        - Note also special handling of AUTH header. Thanks to IPsec wizards.
 */

int nf_ct_ipv6_skip_exthdr(struct sk_buff *skb, int start, u8 *nexthdrp,
			   int len)
{
	u8 nexthdr = *nexthdrp;

	while (ipv6_ext_hdr(nexthdr)) {
		struct ipv6_opt_hdr hdr;
		int hdrlen;

		if (len < (int)sizeof(struct ipv6_opt_hdr))
			return -1;
		if (nexthdr == NEXTHDR_NONE)
			break;
		if (nexthdr == NEXTHDR_FRAGMENT)
			break;
		if (skb_copy_bits(skb, start, &hdr, sizeof(hdr)))
			BUG();
		if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hdr.hdrlen+2)<<2;
		else
			hdrlen = ipv6_optlen(&hdr);

		nexthdr = hdr.nexthdr;
		len -= hdrlen;
		start += hdrlen;
	}

	*nexthdrp = nexthdr;
	return start;
}

static int
ipv6_prepare(struct sk_buff **pskb, unsigned int hooknum, unsigned int *dataoff,
	     u_int8_t *protonum)
{
	unsigned int extoff;
	unsigned char pnum;
	int protoff;

	extoff = (u8*)((*pskb)->nh.ipv6h + 1) - (*pskb)->data;
	pnum = (*pskb)->nh.ipv6h->nexthdr;

	protoff = nf_ct_ipv6_skip_exthdr(*pskb, extoff, &pnum,
					 (*pskb)->len - extoff);

	/*
	 * (protoff == (*pskb)->len) mean that the packet doesn't have no data
	 * except of IPv6 & ext headers. but it's tracked anyway. - YK
	 */
	if ((protoff < 0) || (protoff > (*pskb)->len)) {
		DEBUGP("ip6_conntrack_core: can't find proto in pkt\n");
		NF_CT_STAT_INC(error);
		NF_CT_STAT_INC(invalid);
		return -NF_ACCEPT;
	}

	*dataoff = protoff;
	*protonum = pnum;
	return NF_ACCEPT;
}

static u_int32_t ipv6_get_features(const struct nf_conntrack_tuple *tuple)
{
	return NF_CT_F_BASIC;
}

static unsigned int ipv6_confirm(unsigned int hooknum,
				 struct sk_buff **pskb,
				 const struct net_device *in,
				 const struct net_device *out,
				 int (*okfn)(struct sk_buff *))
{
	struct nf_conn *ct;
	struct nf_conn_help *help;
	enum ip_conntrack_info ctinfo;
	unsigned int ret, protoff;
	unsigned int extoff = (u8*)((*pskb)->nh.ipv6h + 1)
			      - (*pskb)->data;
	unsigned char pnum = (*pskb)->nh.ipv6h->nexthdr;


	/* This is where we call the helper: as the packet goes out. */
	ct = nf_ct_get(*pskb, &ctinfo);
	if (!ct)
		goto out;

	help = nfct_help(ct);
	if (!help || !help->helper)
		goto out;

	protoff = nf_ct_ipv6_skip_exthdr(*pskb, extoff, &pnum,
					 (*pskb)->len - extoff);
	if (protoff < 0 || protoff > (*pskb)->len ||
	    pnum == NEXTHDR_FRAGMENT) {
		DEBUGP("proto header not found\n");
		return NF_ACCEPT;
	}

	ret = help->helper->help(pskb, protoff, ct, ctinfo);
	if (ret != NF_ACCEPT)
		return ret;
out:
	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(pskb);
}

extern struct sk_buff *nf_ct_frag6_gather(struct sk_buff *skb);
extern void nf_ct_frag6_output(unsigned int hooknum, struct sk_buff *skb,
			       struct net_device *in,
			       struct net_device *out,
			       int (*okfn)(struct sk_buff *));
static unsigned int ipv6_defrag(unsigned int hooknum,
				struct sk_buff **pskb,
				const struct net_device *in,
				const struct net_device *out,
				int (*okfn)(struct sk_buff *))
{
	struct sk_buff *reasm;

	/* Previously seen (loopback)?  */
	if ((*pskb)->nfct)
		return NF_ACCEPT;

	reasm = nf_ct_frag6_gather(*pskb);

	/* queued */
	if (reasm == NULL)
		return NF_STOLEN;

	/* error occured or not fragmented */
	if (reasm == *pskb)
		return NF_ACCEPT;

	nf_ct_frag6_output(hooknum, reasm, (struct net_device *)in,
			   (struct net_device *)out, okfn);

	return NF_STOLEN;
}

static unsigned int ipv6_conntrack_in(unsigned int hooknum,
				      struct sk_buff **pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn)(struct sk_buff *))
{
	struct sk_buff *reasm = (*pskb)->nfct_reasm;

	/* This packet is fragmented and has reassembled packet. */
	if (reasm) {
		/* Reassembled packet isn't parsed yet ? */
		if (!reasm->nfct) {
			unsigned int ret;

			ret = nf_conntrack_in(PF_INET6, hooknum, &reasm);
			if (ret != NF_ACCEPT)
				return ret;
		}
		nf_conntrack_get(reasm->nfct);
		(*pskb)->nfct = reasm->nfct;
		return NF_ACCEPT;
	}

	return nf_conntrack_in(PF_INET6, hooknum, pskb);
}

static unsigned int ipv6_conntrack_local(unsigned int hooknum,
					 struct sk_buff **pskb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct ipv6hdr)) {
		if (net_ratelimit())
			printk("ipv6_conntrack_local: packet too short\n");
		return NF_ACCEPT;
	}
	return ipv6_conntrack_in(hooknum, pskb, in, out, okfn);
}

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ipv6_conntrack_defrag_ops = {
	.hook		= ipv6_defrag,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_PRE_ROUTING,
	.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
};

static struct nf_hook_ops ipv6_conntrack_in_ops = {
	.hook		= ipv6_conntrack_in,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_PRE_ROUTING,
	.priority	= NF_IP6_PRI_CONNTRACK,
};

static struct nf_hook_ops ipv6_conntrack_local_out_ops = {
	.hook		= ipv6_conntrack_local,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_LOCAL_OUT,
	.priority	= NF_IP6_PRI_CONNTRACK,
};

static struct nf_hook_ops ipv6_conntrack_defrag_local_out_ops = {
	.hook		= ipv6_defrag,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_LOCAL_OUT,
	.priority	= NF_IP6_PRI_CONNTRACK_DEFRAG,
};

/* Refragmenter; last chance. */
static struct nf_hook_ops ipv6_conntrack_out_ops = {
	.hook		= ipv6_confirm,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_POST_ROUTING,
	.priority	= NF_IP6_PRI_LAST,
};

static struct nf_hook_ops ipv6_conntrack_local_in_ops = {
	.hook		= ipv6_confirm,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_LOCAL_IN,
	.priority	= NF_IP6_PRI_LAST-1,
};

#ifdef CONFIG_SYSCTL

/* From nf_conntrack_proto_icmpv6.c */
extern unsigned int nf_ct_icmpv6_timeout;

/* From nf_conntrack_frag6.c */
extern unsigned int nf_ct_frag6_timeout;
extern unsigned int nf_ct_frag6_low_thresh;
extern unsigned int nf_ct_frag6_high_thresh;

static struct ctl_table_header *nf_ct_ipv6_sysctl_header;

static ctl_table nf_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_NF_CONNTRACK_ICMPV6_TIMEOUT,
		.procname	= "nf_conntrack_icmpv6_timeout",
		.data		= &nf_ct_icmpv6_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_FRAG6_TIMEOUT,
		.procname	= "nf_conntrack_frag6_timeout",
		.data		= &nf_ct_frag6_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_FRAG6_LOW_THRESH,
		.procname	= "nf_conntrack_frag6_low_thresh",
		.data		= &nf_ct_frag6_low_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_FRAG6_HIGH_THRESH,
		.procname	= "nf_conntrack_frag6_high_thresh",
		.data		= &nf_ct_frag6_high_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
        { .ctl_name = 0 }
};

static ctl_table nf_ct_netfilter_table[] = {
	{
		.ctl_name	= NET_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
		.child		= nf_ct_sysctl_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table nf_ct_net_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= nf_ct_netfilter_table,
	},
	{ .ctl_name = 0 }
};
#endif

#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int ipv6_tuple_to_nfattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	NFA_PUT(skb, CTA_IP_V6_SRC, sizeof(u_int32_t) * 4,
		&tuple->src.u3.ip6);
	NFA_PUT(skb, CTA_IP_V6_DST, sizeof(u_int32_t) * 4,
		&tuple->dst.u3.ip6);
	return 0;

nfattr_failure:
	return -1;
}

static const size_t cta_min_ip[CTA_IP_MAX] = {
	[CTA_IP_V6_SRC-1]       = sizeof(u_int32_t)*4,
	[CTA_IP_V6_DST-1]       = sizeof(u_int32_t)*4,
};

static int ipv6_nfattr_to_tuple(struct nfattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V6_SRC-1] || !tb[CTA_IP_V6_DST-1])
		return -EINVAL;

	if (nfattr_bad_size(tb, CTA_IP_MAX, cta_min_ip))
		return -EINVAL;

	memcpy(&t->src.u3.ip6, NFA_DATA(tb[CTA_IP_V6_SRC-1]), 
	       sizeof(u_int32_t) * 4);
	memcpy(&t->dst.u3.ip6, NFA_DATA(tb[CTA_IP_V6_DST-1]),
	       sizeof(u_int32_t) * 4);

	return 0;
}
#endif

struct nf_conntrack_l3proto nf_conntrack_l3proto_ipv6 = {
	.l3proto		= PF_INET6,
	.name			= "ipv6",
	.pkt_to_tuple		= ipv6_pkt_to_tuple,
	.invert_tuple		= ipv6_invert_tuple,
	.print_tuple		= ipv6_print_tuple,
	.print_conntrack	= ipv6_print_conntrack,
	.prepare		= ipv6_prepare,
#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)
	.tuple_to_nfattr	= ipv6_tuple_to_nfattr,
	.nfattr_to_tuple	= ipv6_nfattr_to_tuple,
#endif
	.get_features		= ipv6_get_features,
	.me			= THIS_MODULE,
};

extern struct nf_conntrack_protocol nf_conntrack_protocol_tcp6;
extern struct nf_conntrack_protocol nf_conntrack_protocol_udp6;
extern struct nf_conntrack_protocol nf_conntrack_protocol_icmpv6;
extern int nf_ct_frag6_init(void);
extern void nf_ct_frag6_cleanup(void);
static int init_or_cleanup(int init)
{
	int ret = 0;

	if (!init) goto cleanup;

	ret = nf_ct_frag6_init();
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't initialize frag6.\n");
		goto cleanup_nothing;
	}
	ret = nf_conntrack_protocol_register(&nf_conntrack_protocol_tcp6);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register tcp.\n");
		goto cleanup_frag6;
	}

	ret = nf_conntrack_protocol_register(&nf_conntrack_protocol_udp6);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register udp.\n");
		goto cleanup_tcp;
	}

	ret = nf_conntrack_protocol_register(&nf_conntrack_protocol_icmpv6);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register icmpv6.\n");
		goto cleanup_udp;
	}

	ret = nf_conntrack_l3proto_register(&nf_conntrack_l3proto_ipv6);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register ipv6\n");
		goto cleanup_icmpv6;
	}

	ret = nf_register_hook(&ipv6_conntrack_defrag_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register pre-routing defrag "
		       "hook.\n");
		goto cleanup_ipv6;
	}

	ret = nf_register_hook(&ipv6_conntrack_defrag_local_out_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register local_out defrag "
		       "hook.\n");
		goto cleanup_defragops;
	}

	ret = nf_register_hook(&ipv6_conntrack_in_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register pre-routing hook.\n");
		goto cleanup_defraglocalops;
	}

	ret = nf_register_hook(&ipv6_conntrack_local_out_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register local out hook.\n");
		goto cleanup_inops;
	}

	ret = nf_register_hook(&ipv6_conntrack_out_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register post-routing hook.\n");
		goto cleanup_inandlocalops;
	}

	ret = nf_register_hook(&ipv6_conntrack_local_in_ops);
	if (ret < 0) {
		printk("nf_conntrack_ipv6: can't register local in hook.\n");
		goto cleanup_inoutandlocalops;
	}

#ifdef CONFIG_SYSCTL
	nf_ct_ipv6_sysctl_header = register_sysctl_table(nf_ct_net_table, 0);
	if (nf_ct_ipv6_sysctl_header == NULL) {
		printk("nf_conntrack: can't register to sysctl.\n");
		ret = -ENOMEM;
		goto cleanup_localinops;
	}
#endif
	return ret;

 cleanup:
	synchronize_net();
#ifdef CONFIG_SYSCTL
 	unregister_sysctl_table(nf_ct_ipv6_sysctl_header);
 cleanup_localinops:
#endif
	nf_unregister_hook(&ipv6_conntrack_local_in_ops);
 cleanup_inoutandlocalops:
	nf_unregister_hook(&ipv6_conntrack_out_ops);
 cleanup_inandlocalops:
	nf_unregister_hook(&ipv6_conntrack_local_out_ops);
 cleanup_inops:
	nf_unregister_hook(&ipv6_conntrack_in_ops);
 cleanup_defraglocalops:
	nf_unregister_hook(&ipv6_conntrack_defrag_local_out_ops);
 cleanup_defragops:
	nf_unregister_hook(&ipv6_conntrack_defrag_ops);
 cleanup_ipv6:
	nf_conntrack_l3proto_unregister(&nf_conntrack_l3proto_ipv6);
 cleanup_icmpv6:
	nf_conntrack_protocol_unregister(&nf_conntrack_protocol_icmpv6);
 cleanup_udp:
	nf_conntrack_protocol_unregister(&nf_conntrack_protocol_udp6);
 cleanup_tcp:
	nf_conntrack_protocol_unregister(&nf_conntrack_protocol_tcp6);
 cleanup_frag6:
	nf_ct_frag6_cleanup();
 cleanup_nothing:
	return ret;
}

MODULE_ALIAS("nf_conntrack-" __stringify(AF_INET6));
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yasuyuki KOZAKAI @USAGI <yasuyuki.kozakai@toshiba.co.jp>");

static int __init nf_conntrack_l3proto_ipv6_init(void)
{
	need_conntrack();
	return init_or_cleanup(1);
}

static void __exit nf_conntrack_l3proto_ipv6_fini(void)
{
	init_or_cleanup(0);
}

module_init(nf_conntrack_l3proto_ipv6_init);
module_exit(nf_conntrack_l3proto_ipv6_fini);
