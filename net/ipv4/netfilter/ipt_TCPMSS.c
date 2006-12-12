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
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_TCPMSS.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables TCP MSS modification module");

static inline unsigned int
optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0)
		return 1;
	else
		return opt[offset+1];
}

static unsigned int
ipt_tcpmss_target(struct sk_buff **pskb,
		  const struct net_device *in,
		  const struct net_device *out,
		  unsigned int hooknum,
		  const struct xt_target *target,
		  const void *targinfo)
{
	const struct ipt_tcpmss_info *tcpmssinfo = targinfo;
	struct tcphdr *tcph;
	struct iphdr *iph;
	u_int16_t tcplen, newmss;
	__be16 newtotlen, oldval;
	unsigned int i;
	u_int8_t *opt;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return NF_DROP;

	iph = (*pskb)->nh.iph;
	tcplen = (*pskb)->len - iph->ihl*4;
	tcph = (void *)iph + iph->ihl*4;

	/* Since it passed flags test in tcp match, we know it is is
	   not a fragment, and has data >= tcp header length.  SYN
	   packets should not contain data: if they did, then we risk
	   running over MTU, sending Frag Needed and breaking things
	   badly. --RR */
	if (tcplen != tcph->doff*4) {
		if (net_ratelimit())
			printk(KERN_ERR
			       "ipt_tcpmss_target: bad length (%d bytes)\n",
			       (*pskb)->len);
		return NF_DROP;
	}

	if (tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU) {
		if (dst_mtu((*pskb)->dst) <= sizeof(struct iphdr) +
					     sizeof(struct tcphdr)) {
			if (net_ratelimit())
				printk(KERN_ERR "ipt_tcpmss_target: "
				       "unknown or invalid path-MTU (%d)\n",
				       dst_mtu((*pskb)->dst));
			return NF_DROP; /* or IPT_CONTINUE ?? */
		}

		newmss = dst_mtu((*pskb)->dst) - sizeof(struct iphdr) -
						 sizeof(struct tcphdr);
	} else
		newmss = tcpmssinfo->mss;

 	opt = (u_int8_t *)tcph;
	for (i = sizeof(struct tcphdr); i < tcph->doff*4; i += optlen(opt, i)) {
		if (opt[i] == TCPOPT_MSS && tcph->doff*4 - i >= TCPOLEN_MSS &&
		    opt[i+1] == TCPOLEN_MSS) {
			u_int16_t oldmss;

			oldmss = (opt[i+2] << 8) | opt[i+3];

			if (tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU &&
			    oldmss <= newmss)
				return IPT_CONTINUE;

			opt[i+2] = (newmss & 0xff00) >> 8;
			opt[i+3] = (newmss & 0x00ff);

			nf_proto_csum_replace2(&tcph->check, *pskb,
						htons(oldmss), htons(newmss), 0);
			return IPT_CONTINUE;
		}
	}

	/*
	 * MSS Option not found ?! add it..
	 */
	if (skb_tailroom((*pskb)) < TCPOLEN_MSS) {
		struct sk_buff *newskb;

		newskb = skb_copy_expand(*pskb, skb_headroom(*pskb),
					 TCPOLEN_MSS, GFP_ATOMIC);
		if (!newskb)
			return NF_DROP;
		kfree_skb(*pskb);
		*pskb = newskb;
		iph = (*pskb)->nh.iph;
		tcph = (void *)iph + iph->ihl*4;
	}

	skb_put((*pskb), TCPOLEN_MSS);

 	opt = (u_int8_t *)tcph + sizeof(struct tcphdr);
	memmove(opt + TCPOLEN_MSS, opt, tcplen - sizeof(struct tcphdr));

	nf_proto_csum_replace2(&tcph->check, *pskb,
				htons(tcplen), htons(tcplen + TCPOLEN_MSS), 1);
	opt[0] = TCPOPT_MSS;
	opt[1] = TCPOLEN_MSS;
	opt[2] = (newmss & 0xff00) >> 8;
	opt[3] = (newmss & 0x00ff);

	nf_proto_csum_replace4(&tcph->check, *pskb, 0, *((__be32 *)opt), 0);

	oldval = ((__be16 *)tcph)[6];
	tcph->doff += TCPOLEN_MSS/4;
	nf_proto_csum_replace2(&tcph->check, *pskb,
				oldval, ((__be16 *)tcph)[6], 0);

	newtotlen = htons(ntohs(iph->tot_len) + TCPOLEN_MSS);
	nf_csum_replace2(&iph->check, iph->tot_len, newtotlen);
	iph->tot_len = newtotlen;
	return IPT_CONTINUE;
}

#define TH_SYN 0x02

static inline int find_syn_match(const struct ipt_entry_match *m)
{
	const struct ipt_tcp *tcpinfo = (const struct ipt_tcp *)m->data;

	if (strcmp(m->u.kernel.match->name, "tcp") == 0 &&
	    tcpinfo->flg_cmp & TH_SYN &&
	    !(tcpinfo->invflags & IPT_TCP_INV_FLAGS))
		return 1;

	return 0;
}

/* Must specify -p tcp --syn/--tcp-flags SYN */
static int
ipt_tcpmss_checkentry(const char *tablename,
		      const void *e_void,
		      const struct xt_target *target,
		      void *targinfo,
		      unsigned int hook_mask)
{
	const struct ipt_tcpmss_info *tcpmssinfo = targinfo;
	const struct ipt_entry *e = e_void;

	if (tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU &&
	    (hook_mask & ~((1 << NF_IP_FORWARD) |
			   (1 << NF_IP_LOCAL_OUT) |
			   (1 << NF_IP_POST_ROUTING))) != 0) {
		printk("TCPMSS: path-MTU clamping only supported in "
		       "FORWARD, OUTPUT and POSTROUTING hooks\n");
		return 0;
	}

	if (IPT_MATCH_ITERATE(e, find_syn_match))
		return 1;
	printk("TCPMSS: Only works on TCP SYN packets\n");
	return 0;
}

static struct ipt_target ipt_tcpmss_reg = {
	.name		= "TCPMSS",
	.target		= ipt_tcpmss_target,
	.targetsize	= sizeof(struct ipt_tcpmss_info),
	.proto		= IPPROTO_TCP,
	.checkentry	= ipt_tcpmss_checkentry,
	.me		= THIS_MODULE,
};

static int __init ipt_tcpmss_init(void)
{
	return ipt_register_target(&ipt_tcpmss_reg);
}

static void __exit ipt_tcpmss_fini(void)
{
	ipt_unregister_target(&ipt_tcpmss_reg);
}

module_init(ipt_tcpmss_init);
module_exit(ipt_tcpmss_fini);
