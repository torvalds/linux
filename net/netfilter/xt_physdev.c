/* Kernel module to match the bridge port in and
 * out device for IP packets coming into contact with a bridge. */

/* (C) 2001-2003 Bart De Schuymer <bdschuym@pandora.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter/xt_physdev.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bart De Schuymer <bdschuym@pandora.be>");
MODULE_DESCRIPTION("Xtables: Bridge physical device match");
MODULE_ALIAS("ipt_physdev");
MODULE_ALIAS("ip6t_physdev");

static bool
physdev_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	int i;
	static const char nulldevname[IFNAMSIZ];
	const struct xt_physdev_info *info = par->matchinfo;
	bool ret;
	const char *indev, *outdev;
	const struct nf_bridge_info *nf_bridge;

	/* Not a bridged IP packet or no info available yet:
	 * LOCAL_OUT/mangle and LOCAL_OUT/nat don't know if
	 * the destination device will be a bridge. */
	if (!(nf_bridge = skb->nf_bridge)) {
		/* Return MATCH if the invert flags of the used options are on */
		if ((info->bitmask & XT_PHYSDEV_OP_BRIDGED) &&
		    !(info->invert & XT_PHYSDEV_OP_BRIDGED))
			return false;
		if ((info->bitmask & XT_PHYSDEV_OP_ISIN) &&
		    !(info->invert & XT_PHYSDEV_OP_ISIN))
			return false;
		if ((info->bitmask & XT_PHYSDEV_OP_ISOUT) &&
		    !(info->invert & XT_PHYSDEV_OP_ISOUT))
			return false;
		if ((info->bitmask & XT_PHYSDEV_OP_IN) &&
		    !(info->invert & XT_PHYSDEV_OP_IN))
			return false;
		if ((info->bitmask & XT_PHYSDEV_OP_OUT) &&
		    !(info->invert & XT_PHYSDEV_OP_OUT))
			return false;
		return true;
	}

	/* This only makes sense in the FORWARD and POSTROUTING chains */
	if ((info->bitmask & XT_PHYSDEV_OP_BRIDGED) &&
	    (!!(nf_bridge->mask & BRNF_BRIDGED) ^
	    !(info->invert & XT_PHYSDEV_OP_BRIDGED)))
		return false;

	if ((info->bitmask & XT_PHYSDEV_OP_ISIN &&
	    (!nf_bridge->physindev ^ !!(info->invert & XT_PHYSDEV_OP_ISIN))) ||
	    (info->bitmask & XT_PHYSDEV_OP_ISOUT &&
	    (!nf_bridge->physoutdev ^ !!(info->invert & XT_PHYSDEV_OP_ISOUT))))
		return false;

	if (!(info->bitmask & XT_PHYSDEV_OP_IN))
		goto match_outdev;
	indev = nf_bridge->physindev ? nf_bridge->physindev->name : nulldevname;
	for (i = 0, ret = false; i < IFNAMSIZ/sizeof(unsigned int); i++) {
		ret |= (((const unsigned int *)indev)[i]
			^ ((const unsigned int *)info->physindev)[i])
			& ((const unsigned int *)info->in_mask)[i];
	}

	if (!ret ^ !(info->invert & XT_PHYSDEV_OP_IN))
		return false;

match_outdev:
	if (!(info->bitmask & XT_PHYSDEV_OP_OUT))
		return true;
	outdev = nf_bridge->physoutdev ?
		 nf_bridge->physoutdev->name : nulldevname;
	for (i = 0, ret = false; i < IFNAMSIZ/sizeof(unsigned int); i++) {
		ret |= (((const unsigned int *)outdev)[i]
			^ ((const unsigned int *)info->physoutdev)[i])
			& ((const unsigned int *)info->out_mask)[i];
	}

	return ret ^ !(info->invert & XT_PHYSDEV_OP_OUT);
}

static bool physdev_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_physdev_info *info = par->matchinfo;

	if (!(info->bitmask & XT_PHYSDEV_OP_MASK) ||
	    info->bitmask & ~XT_PHYSDEV_OP_MASK)
		return false;
	if (info->bitmask & XT_PHYSDEV_OP_OUT &&
	    (!(info->bitmask & XT_PHYSDEV_OP_BRIDGED) ||
	     info->invert & XT_PHYSDEV_OP_BRIDGED) &&
	    par->hook_mask & ((1 << NF_INET_LOCAL_OUT) |
	    (1 << NF_INET_FORWARD) | (1 << NF_INET_POST_ROUTING))) {
		printk(KERN_WARNING "physdev match: using --physdev-out in the "
		       "OUTPUT, FORWARD and POSTROUTING chains for non-bridged "
		       "traffic is not supported anymore.\n");
		if (par->hook_mask & (1 << NF_INET_LOCAL_OUT))
			return false;
	}
	return true;
}

static struct xt_match physdev_mt_reg[] __read_mostly = {
	{
		.name		= "physdev",
		.family		= NFPROTO_IPV4,
		.checkentry	= physdev_mt_check,
		.match		= physdev_mt,
		.matchsize	= sizeof(struct xt_physdev_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "physdev",
		.family		= NFPROTO_IPV6,
		.checkentry	= physdev_mt_check,
		.match		= physdev_mt,
		.matchsize	= sizeof(struct xt_physdev_info),
		.me		= THIS_MODULE,
	},
};

static int __init physdev_mt_init(void)
{
	return xt_register_matches(physdev_mt_reg, ARRAY_SIZE(physdev_mt_reg));
}

static void __exit physdev_mt_exit(void)
{
	xt_unregister_matches(physdev_mt_reg, ARRAY_SIZE(physdev_mt_reg));
}

module_init(physdev_mt_init);
module_exit(physdev_mt_exit);
