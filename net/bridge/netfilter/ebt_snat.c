/*
 *  ebt_snat
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  June, 2002
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nat.h>
#include <linux/module.h>
#include <net/sock.h>
#include <linux/if_arp.h>
#include <net/arp.h>

static int ebt_target_snat(struct sk_buff **pskb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_nat_info *info = (struct ebt_nat_info *) data;

	if (skb_shared(*pskb) || skb_cloned(*pskb)) {
		struct sk_buff *nskb;

		nskb = skb_copy(*pskb, GFP_ATOMIC);
		if (!nskb)
			return NF_DROP;
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}
	memcpy(eth_hdr(*pskb)->h_source, info->mac, ETH_ALEN);
	if (!(info->target & NAT_ARP_BIT) &&
	    eth_hdr(*pskb)->h_proto == htons(ETH_P_ARP)) {
		struct arphdr _ah, *ap;

		ap = skb_header_pointer(*pskb, 0, sizeof(_ah), &_ah);
		if (ap == NULL)
			return EBT_DROP;
		if (ap->ar_hln != ETH_ALEN)
			goto out;
		if (skb_store_bits(*pskb, sizeof(_ah), info->mac,ETH_ALEN))
			return EBT_DROP;
	}
out:
	return info->target | ~EBT_VERDICT_BITS;
}

static int ebt_target_snat_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_nat_info *info = (struct ebt_nat_info *) data;
	int tmp;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_nat_info)))
		return -EINVAL;
	tmp = info->target | ~EBT_VERDICT_BITS;
	if (BASE_CHAIN && tmp == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if (strcmp(tablename, "nat"))
		return -EINVAL;
	if (hookmask & ~(1 << NF_BR_POST_ROUTING))
		return -EINVAL;

	if (tmp < -NUM_STANDARD_TARGETS || tmp >= 0)
		return -EINVAL;
	tmp = info->target | EBT_VERDICT_BITS;
	if ((tmp & ~NAT_ARP_BIT) != ~NAT_ARP_BIT)
		return -EINVAL;
	return 0;
}

static struct ebt_target snat =
{
	.name		= EBT_SNAT_TARGET,
	.target		= ebt_target_snat,
	.check		= ebt_target_snat_check,
	.me		= THIS_MODULE,
};

static int __init ebt_snat_init(void)
{
	return ebt_register_target(&snat);
}

static void __exit ebt_snat_fini(void)
{
	ebt_unregister_target(&snat);
}

module_init(ebt_snat_init);
module_exit(ebt_snat_fini);
MODULE_LICENSE("GPL");
