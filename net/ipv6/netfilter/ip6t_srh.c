/* Kernel module to match Segment Routing Header (SRH) parameters. */

/* Author:
 * Ahmed Abdelsalam <amsalam20@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version 2
 *	of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <net/ipv6.h>
#include <net/seg6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6t_srh.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

/* Test a struct->mt_invflags and a boolean for inequality */
#define NF_SRH_INVF(ptr, flag, boolean)	\
	((boolean) ^ !!((ptr)->mt_invflags & (flag)))

static bool srh_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ip6t_srh *srhinfo = par->matchinfo;
	struct ipv6_sr_hdr *srh;
	struct ipv6_sr_hdr _srh;
	int hdrlen, srhoff = 0;

	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, NULL) < 0)
		return false;
	srh = skb_header_pointer(skb, srhoff, sizeof(_srh), &_srh);
	if (!srh)
		return false;

	hdrlen = ipv6_optlen(srh);
	if (skb->len - srhoff < hdrlen)
		return false;

	if (srh->type != IPV6_SRCRT_TYPE_4)
		return false;

	if (srh->segments_left > srh->first_segment)
		return false;

	/* Next Header matching */
	if (srhinfo->mt_flags & IP6T_SRH_NEXTHDR)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_NEXTHDR,
				!(srh->nexthdr == srhinfo->next_hdr)))
			return false;

	/* Header Extension Length matching */
	if (srhinfo->mt_flags & IP6T_SRH_LEN_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_EQ,
				!(srh->hdrlen == srhinfo->hdr_len)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_LEN_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_GT,
				!(srh->hdrlen > srhinfo->hdr_len)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_LEN_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_LT,
				!(srh->hdrlen < srhinfo->hdr_len)))
			return false;

	/* Segments Left matching */
	if (srhinfo->mt_flags & IP6T_SRH_SEGS_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_EQ,
				!(srh->segments_left == srhinfo->segs_left)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_SEGS_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_GT,
				!(srh->segments_left > srhinfo->segs_left)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_SEGS_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_LT,
				!(srh->segments_left < srhinfo->segs_left)))
			return false;

	/**
	 * Last Entry matching
	 * Last_Entry field was introduced in revision 6 of the SRH draft.
	 * It was called First_Segment in the previous revision
	 */
	if (srhinfo->mt_flags & IP6T_SRH_LAST_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_EQ,
				!(srh->first_segment == srhinfo->last_entry)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_LAST_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_GT,
				!(srh->first_segment > srhinfo->last_entry)))
			return false;

	if (srhinfo->mt_flags & IP6T_SRH_LAST_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_LT,
				!(srh->first_segment < srhinfo->last_entry)))
			return false;

	/**
	 * Tag matchig
	 * Tag field was introduced in revision 6 of the SRH draft.
	 */
	if (srhinfo->mt_flags & IP6T_SRH_TAG)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_TAG,
				!(srh->tag == srhinfo->tag)))
			return false;
	return true;
}

static bool srh1_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	int hdrlen, psidoff, nsidoff, lsidoff, srhoff = 0;
	const struct ip6t_srh1 *srhinfo = par->matchinfo;
	struct in6_addr *psid, *nsid, *lsid;
	struct in6_addr _psid, _nsid, _lsid;
	struct ipv6_sr_hdr *srh;
	struct ipv6_sr_hdr _srh;

	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, NULL) < 0)
		return false;
	srh = skb_header_pointer(skb, srhoff, sizeof(_srh), &_srh);
	if (!srh)
		return false;

	hdrlen = ipv6_optlen(srh);
	if (skb->len - srhoff < hdrlen)
		return false;

	if (srh->type != IPV6_SRCRT_TYPE_4)
		return false;

	if (srh->segments_left > srh->first_segment)
		return false;

	/* Next Header matching */
	if (srhinfo->mt_flags & IP6T_SRH_NEXTHDR)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_NEXTHDR,
				!(srh->nexthdr == srhinfo->next_hdr)))
			return false;

	/* Header Extension Length matching */
	if (srhinfo->mt_flags & IP6T_SRH_LEN_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_EQ,
				!(srh->hdrlen == srhinfo->hdr_len)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_LEN_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_GT,
				!(srh->hdrlen > srhinfo->hdr_len)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_LEN_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LEN_LT,
				!(srh->hdrlen < srhinfo->hdr_len)))
			return false;

	/* Segments Left matching */
	if (srhinfo->mt_flags & IP6T_SRH_SEGS_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_EQ,
				!(srh->segments_left == srhinfo->segs_left)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_SEGS_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_GT,
				!(srh->segments_left > srhinfo->segs_left)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_SEGS_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_SEGS_LT,
				!(srh->segments_left < srhinfo->segs_left)))
			return false;

	/**
	 * Last Entry matching
	 * Last_Entry field was introduced in revision 6 of the SRH draft.
	 * It was called First_Segment in the previous revision
	 */
	if (srhinfo->mt_flags & IP6T_SRH_LAST_EQ)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_EQ,
				!(srh->first_segment == srhinfo->last_entry)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_LAST_GT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_GT,
				!(srh->first_segment > srhinfo->last_entry)))
			return false;
	if (srhinfo->mt_flags & IP6T_SRH_LAST_LT)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LAST_LT,
				!(srh->first_segment < srhinfo->last_entry)))
			return false;

	/**
	 * Tag matchig
	 * Tag field was introduced in revision 6 of the SRH draft
	 */
	if (srhinfo->mt_flags & IP6T_SRH_TAG)
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_TAG,
				!(srh->tag == srhinfo->tag)))
			return false;

	/* Previous SID matching */
	if (srhinfo->mt_flags & IP6T_SRH_PSID) {
		if (srh->segments_left == srh->first_segment)
			return false;
		psidoff = srhoff + sizeof(struct ipv6_sr_hdr) +
			  ((srh->segments_left + 1) * sizeof(struct in6_addr));
		psid = skb_header_pointer(skb, psidoff, sizeof(_psid), &_psid);
		if (!psid)
			return false;
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_PSID,
				ipv6_masked_addr_cmp(psid, &srhinfo->psid_msk,
						     &srhinfo->psid_addr)))
			return false;
	}

	/* Next SID matching */
	if (srhinfo->mt_flags & IP6T_SRH_NSID) {
		if (srh->segments_left == 0)
			return false;
		nsidoff = srhoff + sizeof(struct ipv6_sr_hdr) +
			  ((srh->segments_left - 1) * sizeof(struct in6_addr));
		nsid = skb_header_pointer(skb, nsidoff, sizeof(_nsid), &_nsid);
		if (!nsid)
			return false;
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_NSID,
				ipv6_masked_addr_cmp(nsid, &srhinfo->nsid_msk,
						     &srhinfo->nsid_addr)))
			return false;
	}

	/* Last SID matching */
	if (srhinfo->mt_flags & IP6T_SRH_LSID) {
		lsidoff = srhoff + sizeof(struct ipv6_sr_hdr);
		lsid = skb_header_pointer(skb, lsidoff, sizeof(_lsid), &_lsid);
		if (!lsid)
			return false;
		if (NF_SRH_INVF(srhinfo, IP6T_SRH_INV_LSID,
				ipv6_masked_addr_cmp(lsid, &srhinfo->lsid_msk,
						     &srhinfo->lsid_addr)))
			return false;
	}
	return true;
}

static int srh_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_srh *srhinfo = par->matchinfo;

	if (srhinfo->mt_flags & ~IP6T_SRH_MASK) {
		pr_info_ratelimited("unknown srh match flags  %X\n",
				    srhinfo->mt_flags);
		return -EINVAL;
	}

	if (srhinfo->mt_invflags & ~IP6T_SRH_INV_MASK) {
		pr_info_ratelimited("unknown srh invflags %X\n",
				    srhinfo->mt_invflags);
		return -EINVAL;
	}

	return 0;
}

static int srh1_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_srh1 *srhinfo = par->matchinfo;

	if (srhinfo->mt_flags & ~IP6T_SRH_MASK) {
		pr_info_ratelimited("unknown srh match flags  %X\n",
				    srhinfo->mt_flags);
		return -EINVAL;
	}

	if (srhinfo->mt_invflags & ~IP6T_SRH_INV_MASK) {
		pr_info_ratelimited("unknown srh invflags %X\n",
				    srhinfo->mt_invflags);
		return -EINVAL;
	}

	return 0;
}

static struct xt_match srh_mt6_reg[] __read_mostly = {
	{
		.name		= "srh",
		.revision	= 0,
		.family		= NFPROTO_IPV6,
		.match		= srh_mt6,
		.matchsize	= sizeof(struct ip6t_srh),
		.checkentry	= srh_mt6_check,
		.me		= THIS_MODULE,
	},
	{
		.name           = "srh",
		.revision       = 1,
		.family         = NFPROTO_IPV6,
		.match          = srh1_mt6,
		.matchsize      = sizeof(struct ip6t_srh1),
		.checkentry     = srh1_mt6_check,
		.me             = THIS_MODULE,
	}
};

static int __init srh_mt6_init(void)
{
	return xt_register_matches(srh_mt6_reg, ARRAY_SIZE(srh_mt6_reg));
}

static void __exit srh_mt6_exit(void)
{
	xt_unregister_matches(srh_mt6_reg, ARRAY_SIZE(srh_mt6_reg));
}

module_init(srh_mt6_init);
module_exit(srh_mt6_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: IPv6 Segment Routing Header match");
MODULE_AUTHOR("Ahmed Abdelsalam <amsalam20@gmail.com>");
