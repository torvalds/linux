/*
 * 802_3
 *
 * Author:
 * Chris Vitale csv@bluetail.com
 *
 * May 2003
 *
 */
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_802_3.h>

static bool
ebt_802_3_mt(const struct sk_buff *skb, const struct net_device *in,
	     const struct net_device *out, const struct xt_match *match,
	     const void *data, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct ebt_802_3_info *info = data;
	const struct ebt_802_3_hdr *hdr = ebt_802_3_hdr(skb);
	__be16 type = hdr->llc.ui.ctrl & IS_UI ? hdr->llc.ui.type : hdr->llc.ni.type;

	if (info->bitmask & EBT_802_3_SAP) {
		if (FWINV(info->sap != hdr->llc.ui.ssap, EBT_802_3_SAP))
			return false;
		if (FWINV(info->sap != hdr->llc.ui.dsap, EBT_802_3_SAP))
			return false;
	}

	if (info->bitmask & EBT_802_3_TYPE) {
		if (!(hdr->llc.ui.dsap == CHECK_TYPE && hdr->llc.ui.ssap == CHECK_TYPE))
			return false;
		if (FWINV(info->type != type, EBT_802_3_TYPE))
			return false;
	}

	return true;
}

static bool
ebt_802_3_mt_check(const char *table, const void *entry,
		   const struct xt_match *match, void *data,
		   unsigned int hook_mask)
{
	const struct ebt_802_3_info *info = data;

	if (info->bitmask & ~EBT_802_3_MASK || info->invflags & ~EBT_802_3_MASK)
		return false;

	return true;
}

static struct ebt_match filter_802_3 __read_mostly = {
	.name		= EBT_802_3_MATCH,
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_802_3_mt,
	.checkentry	= ebt_802_3_mt_check,
	.matchsize	= XT_ALIGN(sizeof(struct ebt_802_3_info)),
	.me		= THIS_MODULE,
};

static int __init ebt_802_3_init(void)
{
	return ebt_register_match(&filter_802_3);
}

static void __exit ebt_802_3_fini(void)
{
	ebt_unregister_match(&filter_802_3);
}

module_init(ebt_802_3_init);
module_exit(ebt_802_3_fini);
MODULE_DESCRIPTION("Ebtables: DSAP/SSAP field and SNAP type matching");
MODULE_LICENSE("GPL");
