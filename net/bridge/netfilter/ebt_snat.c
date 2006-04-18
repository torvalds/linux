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
	return info->target;
}

static int ebt_target_snat_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_nat_info *info = (struct ebt_nat_info *) data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_nat_info)))
		return -EINVAL;
	if (BASE_CHAIN && info->target == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
	if (strcmp(tablename, "nat"))
		return -EINVAL;
	if (hookmask & ~(1 << NF_BR_POST_ROUTING))
		return -EINVAL;
	if (INVALID_TARGET)
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
