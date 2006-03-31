/*
 *  ebt_arpreply
 *
 *	Authors:
 *	Grzegorz Borowiak <grzes@gnu.univ.gda.pl>
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  August, 2003
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_arpreply.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <linux/module.h>

static int ebt_target_reply(struct sk_buff **pskb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_arpreply_info *info = (struct ebt_arpreply_info *)data;
	u32 _sip, *siptr, _dip, *diptr;
	struct arphdr _ah, *ap;
	unsigned char _sha[ETH_ALEN], *shp;
	struct sk_buff *skb = *pskb;

	ap = skb_header_pointer(skb, 0, sizeof(_ah), &_ah);
	if (ap == NULL)
		return EBT_DROP;

	if (ap->ar_op != htons(ARPOP_REQUEST) ||
	    ap->ar_hln != ETH_ALEN ||
	    ap->ar_pro != htons(ETH_P_IP) ||
	    ap->ar_pln != 4)
		return EBT_CONTINUE;

	shp = skb_header_pointer(skb, sizeof(_ah), ETH_ALEN, &_sha);
	if (shp == NULL)
		return EBT_DROP;

	siptr = skb_header_pointer(skb, sizeof(_ah) + ETH_ALEN,
				   sizeof(_sip), &_sip);
	if (siptr == NULL)
		return EBT_DROP;

	diptr = skb_header_pointer(skb,
				   sizeof(_ah) + 2 * ETH_ALEN + sizeof(_sip),
				   sizeof(_dip), &_dip);
	if (diptr == NULL)
		return EBT_DROP;

	arp_send(ARPOP_REPLY, ETH_P_ARP, *siptr, (struct net_device *)in,
	         *diptr, shp, info->mac, shp);

	return info->target;
}

static int ebt_target_reply_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_arpreply_info *info = (struct ebt_arpreply_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_arpreply_info)))
		return -EINVAL;
	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;
	if (e->ethproto != htons(ETH_P_ARP) ||
	    e->invflags & EBT_IPROTO)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if (strcmp(tablename, "nat") || hookmask & ~(1 << NF_BR_PRE_ROUTING))
		return -EINVAL;
	return 0;
}

static struct ebt_target reply_target =
{
	.name		= EBT_ARPREPLY_TARGET,
	.target		= ebt_target_reply,
	.check		= ebt_target_reply_check,
	.me		= THIS_MODULE,
};

static int __init ebt_arpreply_init(void)
{
	return ebt_register_target(&reply_target);
}

static void __exit ebt_arpreply_fini(void)
{
	ebt_unregister_target(&reply_target);
}

module_init(ebt_arpreply_init);
module_exit(ebt_arpreply_fini);
MODULE_LICENSE("GPL");
