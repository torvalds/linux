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

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static u_int16_t
cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck)
{
	u_int32_t diffs[] = { oldvalinv, newval };
	return csum_fold(csum_partial((char *)diffs, sizeof(diffs),
                                      oldcheck^0xFFFF));
}

static inline unsigned int
optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0) return 1;
	else return opt[offset+1];
}

static unsigned int
ipt_tcpmss_target(struct sk_buff **pskb,
		  const struct net_device *in,
		  const struct net_device *out,
		  unsigned int hooknum,
		  const void *targinfo,
		  void *userinfo)
{
	const struct ipt_tcpmss_info *tcpmssinfo = targinfo;
	struct tcphdr *tcph;
	struct iphdr *iph;
	u_int16_t tcplen, newtotlen, oldval, newmss;
	unsigned int i;
	u_int8_t *opt;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return NF_DROP;

	if ((*pskb)->ip_summed == CHECKSUM_HW &&
	    skb_checksum_help(*pskb, out == NULL))
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

	if(tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU) {
		if(!(*pskb)->dst) {
			if (net_ratelimit())
				printk(KERN_ERR
			       		"ipt_tcpmss_target: no dst?! can't determine path-MTU\n");
			return NF_DROP; /* or IPT_CONTINUE ?? */
		}

		if(dst_mtu((*pskb)->dst) <= (sizeof(struct iphdr) + sizeof(struct tcphdr))) {
			if (net_ratelimit())
				printk(KERN_ERR
		       			"ipt_tcpmss_target: unknown or invalid path-MTU (%d)\n", dst_mtu((*pskb)->dst));
			return NF_DROP; /* or IPT_CONTINUE ?? */
		}

		newmss = dst_mtu((*pskb)->dst) - sizeof(struct iphdr) - sizeof(struct tcphdr);
	} else
		newmss = tcpmssinfo->mss;

 	opt = (u_int8_t *)tcph;
	for (i = sizeof(struct tcphdr); i < tcph->doff*4; i += optlen(opt, i)){
		if ((opt[i] == TCPOPT_MSS) &&
		    ((tcph->doff*4 - i) >= TCPOLEN_MSS) &&
		    (opt[i+1] == TCPOLEN_MSS)) {
			u_int16_t oldmss;

			oldmss = (opt[i+2] << 8) | opt[i+3];

			if((tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU) &&
				(oldmss <= newmss))
					return IPT_CONTINUE;

			opt[i+2] = (newmss & 0xff00) >> 8;
			opt[i+3] = (newmss & 0x00ff);

			tcph->check = cheat_check(htons(oldmss)^0xFFFF,
						  htons(newmss),
						  tcph->check);

			DEBUGP(KERN_INFO "ipt_tcpmss_target: %u.%u.%u.%u:%hu"
			       "->%u.%u.%u.%u:%hu changed TCP MSS option"
			       " (from %u to %u)\n", 
			       NIPQUAD((*pskb)->nh.iph->saddr),
			       ntohs(tcph->source),
			       NIPQUAD((*pskb)->nh.iph->daddr),
			       ntohs(tcph->dest),
			       oldmss, newmss);
			goto retmodified;
		}
	}

	/*
	 * MSS Option not found ?! add it..
	 */
	if (skb_tailroom((*pskb)) < TCPOLEN_MSS) {
		struct sk_buff *newskb;

		newskb = skb_copy_expand(*pskb, skb_headroom(*pskb),
					 TCPOLEN_MSS, GFP_ATOMIC);
		if (!newskb) {
			if (net_ratelimit())
				printk(KERN_ERR "ipt_tcpmss_target:"
				       " unable to allocate larger skb\n");
			return NF_DROP;
		}

		kfree_skb(*pskb);
		*pskb = newskb;
		iph = (*pskb)->nh.iph;
		tcph = (void *)iph + iph->ihl*4;
	}

	skb_put((*pskb), TCPOLEN_MSS);

 	opt = (u_int8_t *)tcph + sizeof(struct tcphdr);
	memmove(opt + TCPOLEN_MSS, opt, tcplen - sizeof(struct tcphdr));

	tcph->check = cheat_check(htons(tcplen) ^ 0xFFFF,
				  htons(tcplen + TCPOLEN_MSS), tcph->check);
	tcplen += TCPOLEN_MSS;

	opt[0] = TCPOPT_MSS;
	opt[1] = TCPOLEN_MSS;
	opt[2] = (newmss & 0xff00) >> 8;
	opt[3] = (newmss & 0x00ff);

	tcph->check = cheat_check(~0, *((u_int32_t *)opt), tcph->check);

	oldval = ((u_int16_t *)tcph)[6];
	tcph->doff += TCPOLEN_MSS/4;
	tcph->check = cheat_check(oldval ^ 0xFFFF,
				  ((u_int16_t *)tcph)[6], tcph->check);

	newtotlen = htons(ntohs(iph->tot_len) + TCPOLEN_MSS);
	iph->check = cheat_check(iph->tot_len ^ 0xFFFF,
				 newtotlen, iph->check);
	iph->tot_len = newtotlen;

	DEBUGP(KERN_INFO "ipt_tcpmss_target: %u.%u.%u.%u:%hu"
	       "->%u.%u.%u.%u:%hu added TCP MSS option (%u)\n",
	       NIPQUAD((*pskb)->nh.iph->saddr),
	       ntohs(tcph->source),
	       NIPQUAD((*pskb)->nh.iph->daddr),
	       ntohs(tcph->dest),
	       newmss);

 retmodified:
	return IPT_CONTINUE;
}

#define TH_SYN 0x02

static inline int find_syn_match(const struct ipt_entry_match *m)
{
	const struct ipt_tcp *tcpinfo = (const struct ipt_tcp *)m->data;

	if (strcmp(m->u.kernel.match->name, "tcp") == 0
	    && (tcpinfo->flg_cmp & TH_SYN)
	    && !(tcpinfo->invflags & IPT_TCP_INV_FLAGS))
		return 1;

	return 0;
}

/* Must specify -p tcp --syn/--tcp-flags SYN */
static int
ipt_tcpmss_checkentry(const char *tablename,
		      const struct ipt_entry *e,
		      void *targinfo,
		      unsigned int targinfosize,
		      unsigned int hook_mask)
{
	const struct ipt_tcpmss_info *tcpmssinfo = targinfo;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_tcpmss_info))) {
		DEBUGP("ipt_tcpmss_checkentry: targinfosize %u != %u\n",
		       targinfosize, IPT_ALIGN(sizeof(struct ipt_tcpmss_info)));
		return 0;
	}


	if((tcpmssinfo->mss == IPT_TCPMSS_CLAMP_PMTU) && 
			((hook_mask & ~((1 << NF_IP_FORWARD)
			   	| (1 << NF_IP_LOCAL_OUT)
			   	| (1 << NF_IP_POST_ROUTING))) != 0)) {
		printk("TCPMSS: path-MTU clamping only supported in FORWARD, OUTPUT and POSTROUTING hooks\n");
		return 0;
	}

	if (e->ip.proto == IPPROTO_TCP
	    && !(e->ip.invflags & IPT_INV_PROTO)
	    && IPT_MATCH_ITERATE(e, find_syn_match))
		return 1;

	printk("TCPMSS: Only works on TCP SYN packets\n");
	return 0;
}

static struct ipt_target ipt_tcpmss_reg = {
	.name		= "TCPMSS",
	.target		= ipt_tcpmss_target,
	.checkentry	= ipt_tcpmss_checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_target(&ipt_tcpmss_reg);
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_tcpmss_reg);
}

module_init(init);
module_exit(fini);
