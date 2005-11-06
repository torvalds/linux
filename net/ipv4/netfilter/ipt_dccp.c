/*
 * iptables module for DCCP protocol header matching
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/ip.h>
#include <linux/dccp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_dccp.h>

#define DCCHECK(cond, option, flag, invflag) (!((flag) & (option)) \
		                  || (!!((invflag) & (option)) ^ (cond)))

static unsigned char *dccp_optbuf;
static DEFINE_SPINLOCK(dccp_buflock);

static inline int
dccp_find_option(u_int8_t option,
		 const struct sk_buff *skb,
		 const struct dccp_hdr *dh,
		 int *hotdrop)
{
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	unsigned char *op;
	unsigned int optoff = __dccp_hdr_len(dh);
	unsigned int optlen = dh->dccph_doff*4 - __dccp_hdr_len(dh);
	unsigned int i;

	if (dh->dccph_doff * 4 < __dccp_hdr_len(dh)) {
		*hotdrop = 1;
		return 0;
	}

	if (!optlen)
		return 0;

	spin_lock_bh(&dccp_buflock);
	op = skb_header_pointer(skb,
				skb->nh.iph->ihl*4 + optoff,
				optlen, dccp_optbuf);
	if (op == NULL) {
		/* If we don't have the whole header, drop packet. */
		spin_unlock_bh(&dccp_buflock);
		*hotdrop = 1;
		return 0;
	}

	for (i = 0; i < optlen; ) {
		if (op[i] == option) {
			spin_unlock_bh(&dccp_buflock);
			return 1;
		}

		if (op[i] < 2) 
			i++;
		else 
			i += op[i+1]?:1;
	}

	spin_unlock_bh(&dccp_buflock);
	return 0;
}


static inline int
match_types(const struct dccp_hdr *dh, u_int16_t typemask)
{
	return (typemask & (1 << dh->dccph_type));
}

static inline int
match_option(u_int8_t option, const struct sk_buff *skb,
	     const struct dccp_hdr *dh, int *hotdrop)
{
	return dccp_find_option(option, skb, dh, hotdrop);
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	const struct ipt_dccp_info *info = 
				(const struct ipt_dccp_info *)matchinfo;
	struct dccp_hdr _dh, *dh;

	if (offset)
		return 0;
	
	dh = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_dh), &_dh);
	if (dh == NULL) {
		*hotdrop = 1;
		return 0;
       	}

	return  DCCHECK(((ntohs(dh->dccph_sport) >= info->spts[0]) 
			&& (ntohs(dh->dccph_sport) <= info->spts[1])), 
		   	IPT_DCCP_SRC_PORTS, info->flags, info->invflags)
		&& DCCHECK(((ntohs(dh->dccph_dport) >= info->dpts[0]) 
			&& (ntohs(dh->dccph_dport) <= info->dpts[1])), 
			IPT_DCCP_DEST_PORTS, info->flags, info->invflags)
		&& DCCHECK(match_types(dh, info->typemask),
			   IPT_DCCP_TYPE, info->flags, info->invflags)
		&& DCCHECK(match_option(info->option, skb, dh, hotdrop),
			   IPT_DCCP_OPTION, info->flags, info->invflags);
}

static int
checkentry(const char *tablename,
	   const struct ipt_ip *ip,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ipt_dccp_info *info;

	info = (const struct ipt_dccp_info *)matchinfo;

	return ip->proto == IPPROTO_DCCP
		&& !(ip->invflags & IPT_INV_PROTO)
		&& matchsize == IPT_ALIGN(sizeof(struct ipt_dccp_info))
		&& !(info->flags & ~IPT_DCCP_VALID_FLAGS)
		&& !(info->invflags & ~IPT_DCCP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags);
}

static struct ipt_match dccp_match = 
{ 
	.name 		= "dccp",
	.match		= &match,
	.checkentry	= &checkentry,
	.me 		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;

	/* doff is 8 bits, so the maximum option size is (4*256).  Don't put
	 * this in BSS since DaveM is worried about locked TLB's for kernel
	 * BSS. */
	dccp_optbuf = kmalloc(256 * 4, GFP_KERNEL);
	if (!dccp_optbuf)
		return -ENOMEM;
	ret = ipt_register_match(&dccp_match);
	if (ret)
		kfree(dccp_optbuf);

	return ret;
}

static void __exit fini(void)
{
	ipt_unregister_match(&dccp_match);
	kfree(dccp_optbuf);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Match for DCCP protocol packets");

