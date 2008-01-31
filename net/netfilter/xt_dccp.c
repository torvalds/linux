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

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_dccp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: DCCP protocol packet match");
MODULE_ALIAS("ipt_dccp");
MODULE_ALIAS("ip6t_dccp");

#define DCCHECK(cond, option, flag, invflag) (!((flag) & (option)) \
				  || (!!((invflag) & (option)) ^ (cond)))

static unsigned char *dccp_optbuf;
static DEFINE_SPINLOCK(dccp_buflock);

static inline bool
dccp_find_option(u_int8_t option,
		 const struct sk_buff *skb,
		 unsigned int protoff,
		 const struct dccp_hdr *dh,
		 bool *hotdrop)
{
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	const unsigned char *op;
	unsigned int optoff = __dccp_hdr_len(dh);
	unsigned int optlen = dh->dccph_doff*4 - __dccp_hdr_len(dh);
	unsigned int i;

	if (dh->dccph_doff * 4 < __dccp_hdr_len(dh)) {
		*hotdrop = true;
		return false;
	}

	if (!optlen)
		return false;

	spin_lock_bh(&dccp_buflock);
	op = skb_header_pointer(skb, protoff + optoff, optlen, dccp_optbuf);
	if (op == NULL) {
		/* If we don't have the whole header, drop packet. */
		spin_unlock_bh(&dccp_buflock);
		*hotdrop = true;
		return false;
	}

	for (i = 0; i < optlen; ) {
		if (op[i] == option) {
			spin_unlock_bh(&dccp_buflock);
			return true;
		}

		if (op[i] < 2)
			i++;
		else
			i += op[i+1]?:1;
	}

	spin_unlock_bh(&dccp_buflock);
	return false;
}


static inline bool
match_types(const struct dccp_hdr *dh, u_int16_t typemask)
{
	return typemask & (1 << dh->dccph_type);
}

static inline bool
match_option(u_int8_t option, const struct sk_buff *skb, unsigned int protoff,
	     const struct dccp_hdr *dh, bool *hotdrop)
{
	return dccp_find_option(option, skb, protoff, dh, hotdrop);
}

static bool
dccp_mt(const struct sk_buff *skb, const struct net_device *in,
        const struct net_device *out, const struct xt_match *match,
        const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct xt_dccp_info *info = matchinfo;
	struct dccp_hdr _dh, *dh;

	if (offset)
		return false;

	dh = skb_header_pointer(skb, protoff, sizeof(_dh), &_dh);
	if (dh == NULL) {
		*hotdrop = true;
		return false;
	}

	return  DCCHECK(ntohs(dh->dccph_sport) >= info->spts[0]
			&& ntohs(dh->dccph_sport) <= info->spts[1],
			XT_DCCP_SRC_PORTS, info->flags, info->invflags)
		&& DCCHECK(ntohs(dh->dccph_dport) >= info->dpts[0]
			&& ntohs(dh->dccph_dport) <= info->dpts[1],
			XT_DCCP_DEST_PORTS, info->flags, info->invflags)
		&& DCCHECK(match_types(dh, info->typemask),
			   XT_DCCP_TYPE, info->flags, info->invflags)
		&& DCCHECK(match_option(info->option, skb, protoff, dh,
					hotdrop),
			   XT_DCCP_OPTION, info->flags, info->invflags);
}

static bool
dccp_mt_check(const char *tablename, const void *inf,
              const struct xt_match *match, void *matchinfo,
              unsigned int hook_mask)
{
	const struct xt_dccp_info *info = matchinfo;

	return !(info->flags & ~XT_DCCP_VALID_FLAGS)
		&& !(info->invflags & ~XT_DCCP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags);
}

static struct xt_match dccp_mt_reg[] __read_mostly = {
	{
		.name 		= "dccp",
		.family		= AF_INET,
		.checkentry	= dccp_mt_check,
		.match		= dccp_mt,
		.matchsize	= sizeof(struct xt_dccp_info),
		.proto		= IPPROTO_DCCP,
		.me 		= THIS_MODULE,
	},
	{
		.name 		= "dccp",
		.family		= AF_INET6,
		.checkentry	= dccp_mt_check,
		.match		= dccp_mt,
		.matchsize	= sizeof(struct xt_dccp_info),
		.proto		= IPPROTO_DCCP,
		.me 		= THIS_MODULE,
	},
};

static int __init dccp_mt_init(void)
{
	int ret;

	/* doff is 8 bits, so the maximum option size is (4*256).  Don't put
	 * this in BSS since DaveM is worried about locked TLB's for kernel
	 * BSS. */
	dccp_optbuf = kmalloc(256 * 4, GFP_KERNEL);
	if (!dccp_optbuf)
		return -ENOMEM;
	ret = xt_register_matches(dccp_mt_reg, ARRAY_SIZE(dccp_mt_reg));
	if (ret)
		goto out_kfree;
	return ret;

out_kfree:
	kfree(dccp_optbuf);
	return ret;
}

static void __exit dccp_mt_exit(void)
{
	xt_unregister_matches(dccp_mt_reg, ARRAY_SIZE(dccp_mt_reg));
	kfree(dccp_optbuf);
}

module_init(dccp_mt_init);
module_exit(dccp_mt_exit);
