/*
 * This is a module which is used for setting the MSS option in TCP packets.
 *
 * Copyright (C) 2000 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/gfp.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/ipv6.h>
#include <net/route.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter/xt_TCPMSS.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("Xtables: TCP Maximum Segment Size (MSS) adjustment");
MODULE_ALIAS("ipt_TCPMSS");
MODULE_ALIAS("ip6t_TCPMSS");

static inline unsigned int
optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0)
		return 1;
	else
		return opt[offset+1];
}

static int
tcpmss_mangle_packet(struct sk_buff *skb,
		     const struct xt_tcpmss_info *info,
		     unsigned int in_mtu,
		     unsigned int tcphoff,
		     unsigned int minlen)
{
	struct tcphdr *tcph;
	unsigned int tcplen, i;
	__be16 oldval;
	u16 newmss;
	u8 *opt;

	if (!skb_make_writable(skb, skb->len))
		return -1;

	tcplen = skb->len - tcphoff;
	tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);

	/* Header cannot be larger than the packet */
	if (tcplen < tcph->doff*4)
		return -1;

	if (info->mss == XT_TCPMSS_CLAMP_PMTU) {
		if (dst_mtu(skb_dst(skb)) <= minlen) {
			if (net_ratelimit())
				printk(KERN_ERR "xt_TCPMSS: "
				       "unknown or invalid path-MTU (%u)\n",
				       dst_mtu(skb_dst(skb)));
			return -1;
		}
		if (in_mtu <= minlen) {
			if (net_ratelimit())
				printk(KERN_ERR "xt_TCPMSS: unknown or "
				       "invalid path-MTU (%u)\n", in_mtu);
			return -1;
		}
		newmss = min(dst_mtu(skb_dst(skb)), in_mtu) - minlen;
	} else
		newmss = info->mss;

	opt = (u_int8_t *)tcph;
	for (i = sizeof(struct tcphdr); i < tcph->doff*4; i += optlen(opt, i)) {
		if (opt[i] == TCPOPT_MSS && tcph->doff*4 - i >= TCPOLEN_MSS &&
		    opt[i+1] == TCPOLEN_MSS) {
			u_int16_t oldmss;

			oldmss = (opt[i+2] << 8) | opt[i+3];

			/* Never increase MSS, even when setting it, as
			 * doing so results in problems for hosts that rely
			 * on MSS being set correctly.
			 */
			if (oldmss <= newmss)
				return 0;

			opt[i+2] = (newmss & 0xff00) >> 8;
			opt[i+3] = newmss & 0x00ff;

			inet_proto_csum_replace2(&tcph->check, skb,
						 htons(oldmss), htons(newmss),
						 0);
			return 0;
		}
	}

	/* There is data after the header so the option can't be added
	   without moving it, and doing so may make the SYN packet
	   itself too large. Accept the packet unmodified instead. */
	if (tcplen > tcph->doff*4)
		return 0;

	/*
	 * MSS Option not found ?! add it..
	 */
	if (skb_tailroom(skb) < TCPOLEN_MSS) {
		if (pskb_expand_head(skb, 0,
				     TCPOLEN_MSS - skb_tailroom(skb),
				     GFP_ATOMIC))
			return -1;
		tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);
	}

	skb_put(skb, TCPOLEN_MSS);

	opt = (u_int8_t *)tcph + sizeof(struct tcphdr);
	memmove(opt + TCPOLEN_MSS, opt, tcplen - sizeof(struct tcphdr));

	inet_proto_csum_replace2(&tcph->check, skb,
				 htons(tcplen), htons(tcplen + TCPOLEN_MSS), 1);
	opt[0] = TCPOPT_MSS;
	opt[1] = TCPOLEN_MSS;
	opt[2] = (newmss & 0xff00) >> 8;
	opt[3] = newmss & 0x00ff;

	inet_proto_csum_replace4(&tcph->check, skb, 0, *((__be32 *)opt), 0);

	oldval = ((__be16 *)tcph)[6];
	tcph->doff += TCPOLEN_MSS/4;
	inet_proto_csum_replace2(&tcph->check, skb,
				 oldval, ((__be16 *)tcph)[6], 0);
	return TCPOLEN_MSS;
}

static u_int32_t tcpmss_reverse_mtu(const struct sk_buff *skb,
				    unsigned int family)
{
	struct flowi fl = {};
	const struct nf_afinfo *ai;
	struct rtable *rt = NULL;
	u_int32_t mtu     = ~0U;

	if (family == PF_INET)
		fl.fl4_dst = ip_hdr(skb)->saddr;
	else
		fl.fl6_dst = ipv6_hdr(skb)->saddr;

	rcu_read_lock();
	ai = nf_get_afinfo(family);
	if (ai != NULL)
		ai->route((struct dst_entry **)&rt, &fl);
	rcu_read_unlock();

	if (rt != NULL) {
		mtu = dst_mtu(&rt->u.dst);
		dst_release(&rt->u.dst);
	}
	return mtu;
}

static unsigned int
tcpmss_tg4(struct sk_buff *skb, const struct xt_target_param *par)
{
	struct iphdr *iph = ip_hdr(skb);
	__be16 newlen;
	int ret;

	ret = tcpmss_mangle_packet(skb, par->targinfo,
				   tcpmss_reverse_mtu(skb, PF_INET),
				   iph->ihl * 4,
				   sizeof(*iph) + sizeof(struct tcphdr));
	if (ret < 0)
		return NF_DROP;
	if (ret > 0) {
		iph = ip_hdr(skb);
		newlen = htons(ntohs(iph->tot_len) + ret);
		csum_replace2(&iph->check, iph->tot_len, newlen);
		iph->tot_len = newlen;
	}
	return XT_CONTINUE;
}

#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
static unsigned int
tcpmss_tg6(struct sk_buff *skb, const struct xt_target_param *par)
{
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	u8 nexthdr;
	int tcphoff;
	int ret;

	nexthdr = ipv6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(skb, sizeof(*ipv6h), &nexthdr);
	if (tcphoff < 0)
		return NF_DROP;
	ret = tcpmss_mangle_packet(skb, par->targinfo,
				   tcpmss_reverse_mtu(skb, PF_INET6),
				   tcphoff,
				   sizeof(*ipv6h) + sizeof(struct tcphdr));
	if (ret < 0)
		return NF_DROP;
	if (ret > 0) {
		ipv6h = ipv6_hdr(skb);
		ipv6h->payload_len = htons(ntohs(ipv6h->payload_len) + ret);
	}
	return XT_CONTINUE;
}
#endif

#define TH_SYN 0x02

/* Must specify -p tcp --syn */
static inline bool find_syn_match(const struct xt_entry_match *m)
{
	const struct xt_tcp *tcpinfo = (const struct xt_tcp *)m->data;

	if (strcmp(m->u.kernel.match->name, "tcp") == 0 &&
	    tcpinfo->flg_cmp & TH_SYN &&
	    !(tcpinfo->invflags & XT_TCP_INV_FLAGS))
		return true;

	return false;
}

static bool tcpmss_tg4_check(const struct xt_tgchk_param *par)
{
	const struct xt_tcpmss_info *info = par->targinfo;
	const struct ipt_entry *e = par->entryinfo;
	const struct xt_entry_match *ematch;

	if (info->mss == XT_TCPMSS_CLAMP_PMTU &&
	    (par->hook_mask & ~((1 << NF_INET_FORWARD) |
			   (1 << NF_INET_LOCAL_OUT) |
			   (1 << NF_INET_POST_ROUTING))) != 0) {
		printk("xt_TCPMSS: path-MTU clamping only supported in "
		       "FORWARD, OUTPUT and POSTROUTING hooks\n");
		return false;
	}
	xt_ematch_foreach(ematch, e)
		if (find_syn_match(ematch))
			return true;
	printk("xt_TCPMSS: Only works on TCP SYN packets\n");
	return false;
}

#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
static bool tcpmss_tg6_check(const struct xt_tgchk_param *par)
{
	const struct xt_tcpmss_info *info = par->targinfo;
	const struct ip6t_entry *e = par->entryinfo;
	const struct xt_entry_match *ematch;

	if (info->mss == XT_TCPMSS_CLAMP_PMTU &&
	    (par->hook_mask & ~((1 << NF_INET_FORWARD) |
			   (1 << NF_INET_LOCAL_OUT) |
			   (1 << NF_INET_POST_ROUTING))) != 0) {
		printk("xt_TCPMSS: path-MTU clamping only supported in "
		       "FORWARD, OUTPUT and POSTROUTING hooks\n");
		return false;
	}
	xt_ematch_foreach(ematch, e)
		if (find_syn_match(ematch))
			return true;
	printk("xt_TCPMSS: Only works on TCP SYN packets\n");
	return false;
}
#endif

static struct xt_target tcpmss_tg_reg[] __read_mostly = {
	{
		.family		= NFPROTO_IPV4,
		.name		= "TCPMSS",
		.checkentry	= tcpmss_tg4_check,
		.target		= tcpmss_tg4,
		.targetsize	= sizeof(struct xt_tcpmss_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
	{
		.family		= NFPROTO_IPV6,
		.name		= "TCPMSS",
		.checkentry	= tcpmss_tg6_check,
		.target		= tcpmss_tg6,
		.targetsize	= sizeof(struct xt_tcpmss_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
#endif
};

static int __init tcpmss_tg_init(void)
{
	return xt_register_targets(tcpmss_tg_reg, ARRAY_SIZE(tcpmss_tg_reg));
}

static void __exit tcpmss_tg_exit(void)
{
	xt_unregister_targets(tcpmss_tg_reg, ARRAY_SIZE(tcpmss_tg_reg));
}

module_init(tcpmss_tg_init);
module_exit(tcpmss_tg_exit);
