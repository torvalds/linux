/*
 *  ebt_snat
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  June, 2002
 *
 */
#include <linux/module.h>
#include <net/sock.h>
#include <linux/if_arp.h>
#include <net/arp.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nat.h>

static unsigned int
ebt_snat_tg(struct sk_buff *skb, const struct net_device *in,
	    const struct net_device *out, unsigned int hook_nr,
	    const struct xt_target *target, const void *data)
{
	const struct ebt_nat_info *info = data;

	if (!skb_make_writable(skb, 0))
		return EBT_DROP;

	memcpy(eth_hdr(skb)->h_source, info->mac, ETH_ALEN);
	if (!(info->target & NAT_ARP_BIT) &&
	    eth_hdr(skb)->h_proto == htons(ETH_P_ARP)) {
		const struct arphdr *ap;
		struct arphdr _ah;

		ap = skb_header_pointer(skb, 0, sizeof(_ah), &_ah);
		if (ap == NULL)
			return EBT_DROP;
		if (ap->ar_hln != ETH_ALEN)
			goto out;
		if (skb_store_bits(skb, sizeof(_ah), info->mac,ETH_ALEN))
			return EBT_DROP;
	}
out:
	return info->target | ~EBT_VERDICT_BITS;
}

static bool
ebt_snat_tg_check(const char *tablename, const void *e,
		  const struct xt_target *target, void *data,
		  unsigned int hookmask)
{
	const struct ebt_nat_info *info = data;
	int tmp;

	tmp = info->target | ~EBT_VERDICT_BITS;
	if (BASE_CHAIN && tmp == EBT_RETURN)
		return false;
	CLEAR_BASE_CHAIN_BIT;
	if (strcmp(tablename, "nat"))
		return false;
	if (hookmask & ~(1 << NF_BR_POST_ROUTING))
		return false;

	if (tmp < -NUM_STANDARD_TARGETS || tmp >= 0)
		return false;
	tmp = info->target | EBT_VERDICT_BITS;
	if ((tmp & ~NAT_ARP_BIT) != ~NAT_ARP_BIT)
		return false;
	return true;
}

static struct ebt_target snat __read_mostly = {
	.name		= EBT_SNAT_TARGET,
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.target		= ebt_snat_tg,
	.checkentry	= ebt_snat_tg_check,
	.targetsize	= XT_ALIGN(sizeof(struct ebt_nat_info)),
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
MODULE_DESCRIPTION("Ebtables: Source MAC address translation");
MODULE_LICENSE("GPL");
