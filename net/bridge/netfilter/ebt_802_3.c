/*
 * 802_3
 *
 * Author:
 * Chris Vitale csv@bluetail.com
 *
 * May 2003
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_802_3.h>
#include <linux/module.h>

static int ebt_filter_802_3(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data, unsigned int datalen)
{
	struct ebt_802_3_info *info = (struct ebt_802_3_info *)data;
	struct ebt_802_3_hdr *hdr = ebt_802_3_hdr(skb);
	__be16 type = hdr->llc.ui.ctrl & IS_UI ? hdr->llc.ui.type : hdr->llc.ni.type;

	if (info->bitmask & EBT_802_3_SAP) {
		if (FWINV(info->sap != hdr->llc.ui.ssap, EBT_802_3_SAP))
				return EBT_NOMATCH;
		if (FWINV(info->sap != hdr->llc.ui.dsap, EBT_802_3_SAP))
				return EBT_NOMATCH;
	}

	if (info->bitmask & EBT_802_3_TYPE) {
		if (!(hdr->llc.ui.dsap == CHECK_TYPE && hdr->llc.ui.ssap == CHECK_TYPE))
			return EBT_NOMATCH;
		if (FWINV(info->type != type, EBT_802_3_TYPE))
			return EBT_NOMATCH;
	}

	return EBT_MATCH;
}

static struct ebt_match filter_802_3;
static int ebt_802_3_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_802_3_info *info = (struct ebt_802_3_info *)data;

	if (datalen < sizeof(struct ebt_802_3_info))
		return -EINVAL;
	if (info->bitmask & ~EBT_802_3_MASK || info->invflags & ~EBT_802_3_MASK)
		return -EINVAL;

	return 0;
}

static struct ebt_match filter_802_3 =
{
	.name		= EBT_802_3_MATCH,
	.match		= ebt_filter_802_3,
	.check		= ebt_802_3_check,
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
MODULE_LICENSE("GPL");
