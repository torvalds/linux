/*
 *  ebt_among
 *
 *	Authors:
 *	Grzegorz Borowiak <grzes@gnu.univ.gda.pl>
 *
 *  August, 2003
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_among.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/module.h>

static bool ebt_mac_wormhash_contains(const struct ebt_mac_wormhash *wh,
				      const char *mac, __be32 ip)
{
	/* You may be puzzled as to how this code works.
	 * Some tricks were used, refer to
	 * 	include/linux/netfilter_bridge/ebt_among.h
	 * as there you can find a solution of this mystery.
	 */
	const struct ebt_mac_wormhash_tuple *p;
	int start, limit, i;
	uint32_t cmp[2] = { 0, 0 };
	int key = ((const unsigned char *)mac)[5];

	memcpy(((char *) cmp) + 2, mac, 6);
	start = wh->table[key];
	limit = wh->table[key + 1];
	if (ip) {
		for (i = start; i < limit; i++) {
			p = &wh->pool[i];
			if (cmp[1] == p->cmp[1] && cmp[0] == p->cmp[0])
				if (p->ip == 0 || p->ip == ip)
					return true;
		}
	} else {
		for (i = start; i < limit; i++) {
			p = &wh->pool[i];
			if (cmp[1] == p->cmp[1] && cmp[0] == p->cmp[0])
				if (p->ip == 0)
					return true;
		}
	}
	return false;
}

static int ebt_mac_wormhash_check_integrity(const struct ebt_mac_wormhash
					    *wh)
{
	int i;

	for (i = 0; i < 256; i++) {
		if (wh->table[i] > wh->table[i + 1])
			return -0x100 - i;
		if (wh->table[i] < 0)
			return -0x200 - i;
		if (wh->table[i] > wh->poolsize)
			return -0x300 - i;
	}
	if (wh->table[256] > wh->poolsize)
		return -0xc00;
	return 0;
}

static int get_ip_dst(const struct sk_buff *skb, __be32 *addr)
{
	if (eth_hdr(skb)->h_proto == htons(ETH_P_IP)) {
		const struct iphdr *ih;
		struct iphdr _iph;

		ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
		if (ih == NULL)
			return -1;
		*addr = ih->daddr;
	} else if (eth_hdr(skb)->h_proto == htons(ETH_P_ARP)) {
		const struct arphdr *ah;
		struct arphdr _arph;
		const __be32 *bp;
		__be32 buf;

		ah = skb_header_pointer(skb, 0, sizeof(_arph), &_arph);
		if (ah == NULL ||
		    ah->ar_pln != sizeof(__be32) ||
		    ah->ar_hln != ETH_ALEN)
			return -1;
		bp = skb_header_pointer(skb, sizeof(struct arphdr) +
					2 * ETH_ALEN + sizeof(__be32),
					sizeof(__be32), &buf);
		if (bp == NULL)
			return -1;
		*addr = *bp;
	}
	return 0;
}

static int get_ip_src(const struct sk_buff *skb, __be32 *addr)
{
	if (eth_hdr(skb)->h_proto == htons(ETH_P_IP)) {
		const struct iphdr *ih;
		struct iphdr _iph;

		ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
		if (ih == NULL)
			return -1;
		*addr = ih->saddr;
	} else if (eth_hdr(skb)->h_proto == htons(ETH_P_ARP)) {
		const struct arphdr *ah;
		struct arphdr _arph;
		const __be32 *bp;
		__be32 buf;

		ah = skb_header_pointer(skb, 0, sizeof(_arph), &_arph);
		if (ah == NULL ||
		    ah->ar_pln != sizeof(__be32) ||
		    ah->ar_hln != ETH_ALEN)
			return -1;
		bp = skb_header_pointer(skb, sizeof(struct arphdr) +
					ETH_ALEN, sizeof(__be32), &buf);
		if (bp == NULL)
			return -1;
		*addr = *bp;
	}
	return 0;
}

static bool ebt_filter_among(const struct sk_buff *skb,
			     const struct net_device *in,
			     const struct net_device *out, const void *data,
			     unsigned int datalen)
{
	const struct ebt_among_info *info = data;
	const char *dmac, *smac;
	const struct ebt_mac_wormhash *wh_dst, *wh_src;
	__be32 dip = 0, sip = 0;

	wh_dst = ebt_among_wh_dst(info);
	wh_src = ebt_among_wh_src(info);

	if (wh_src) {
		smac = eth_hdr(skb)->h_source;
		if (get_ip_src(skb, &sip))
			return false;
		if (!(info->bitmask & EBT_AMONG_SRC_NEG)) {
			/* we match only if it contains */
			if (!ebt_mac_wormhash_contains(wh_src, smac, sip))
				return false;
		} else {
			/* we match only if it DOES NOT contain */
			if (ebt_mac_wormhash_contains(wh_src, smac, sip))
				return false;
		}
	}

	if (wh_dst) {
		dmac = eth_hdr(skb)->h_dest;
		if (get_ip_dst(skb, &dip))
			return false;
		if (!(info->bitmask & EBT_AMONG_DST_NEG)) {
			/* we match only if it contains */
			if (!ebt_mac_wormhash_contains(wh_dst, dmac, dip))
				return false;
		} else {
			/* we match only if it DOES NOT contain */
			if (ebt_mac_wormhash_contains(wh_dst, dmac, dip))
				return false;
		}
	}

	return true;
}

static bool
ebt_among_check(const char *tablename, unsigned int hookmask,
		const struct ebt_entry *e, void *data,
		unsigned int datalen)
{
	const struct ebt_among_info *info = data;
	int expected_length = sizeof(struct ebt_among_info);
	const struct ebt_mac_wormhash *wh_dst, *wh_src;
	int err;

	wh_dst = ebt_among_wh_dst(info);
	wh_src = ebt_among_wh_src(info);
	expected_length += ebt_mac_wormhash_size(wh_dst);
	expected_length += ebt_mac_wormhash_size(wh_src);

	if (datalen != EBT_ALIGN(expected_length)) {
		printk(KERN_WARNING
		       "ebtables: among: wrong size: %d "
		       "against expected %d, rounded to %Zd\n",
		       datalen, expected_length,
		       EBT_ALIGN(expected_length));
		return false;
	}
	if (wh_dst && (err = ebt_mac_wormhash_check_integrity(wh_dst))) {
		printk(KERN_WARNING
		       "ebtables: among: dst integrity fail: %x\n", -err);
		return false;
	}
	if (wh_src && (err = ebt_mac_wormhash_check_integrity(wh_src))) {
		printk(KERN_WARNING
		       "ebtables: among: src integrity fail: %x\n", -err);
		return false;
	}
	return true;
}

static struct ebt_match filter_among __read_mostly = {
	.name		= EBT_AMONG_MATCH,
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_filter_among,
	.check		= ebt_among_check,
	.matchsize	= -1, /* special case */
	.me		= THIS_MODULE,
};

static int __init ebt_among_init(void)
{
	return ebt_register_match(&filter_among);
}

static void __exit ebt_among_fini(void)
{
	ebt_unregister_match(&filter_among);
}

module_init(ebt_among_init);
module_exit(ebt_among_fini);
MODULE_DESCRIPTION("Ebtables: Combined MAC/IP address list matching");
MODULE_LICENSE("GPL");
