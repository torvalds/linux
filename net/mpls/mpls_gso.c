/*
 *	MPLS GSO Support
 *
 *	Authors: Simon Horman (horms@verge.net.au)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on: GSO portions of net/ipv4/gre.c
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/module.h>
#include <linux/netdev_features.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

static struct sk_buff *mpls_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	netdev_features_t mpls_features;
	__be16 mpls_protocol;

	if (unlikely(skb_shinfo(skb)->gso_type &
				~(SKB_GSO_TCPV4 |
				  SKB_GSO_TCPV6 |
				  SKB_GSO_UDP |
				  SKB_GSO_DODGY |
				  SKB_GSO_TCP_ECN)))
		goto out;

	/* Setup inner SKB. */
	mpls_protocol = skb->protocol;
	skb->protocol = skb->inner_protocol;

	/* Push back the mac header that skb_mac_gso_segment() has pulled.
	 * It will be re-pulled by the call to skb_mac_gso_segment() below
	 */
	__skb_push(skb, skb->mac_len);

	/* Segment inner packet. */
	mpls_features = skb->dev->mpls_features & features;
	segs = skb_mac_gso_segment(skb, mpls_features);


	/* Restore outer protocol. */
	skb->protocol = mpls_protocol;

	/* Re-pull the mac header that the call to skb_mac_gso_segment()
	 * above pulled.  It will be re-pushed after returning
	 * skb_mac_gso_segment(), an indirect caller of this function.
	 */
	__skb_pull(skb, skb->data - skb_mac_header(skb));
out:
	return segs;
}

static struct packet_offload mpls_mc_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_MPLS_MC),
	.callbacks = {
		.gso_segment    =	mpls_gso_segment,
	},
};

static struct packet_offload mpls_uc_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_MPLS_UC),
	.callbacks = {
		.gso_segment    =	mpls_gso_segment,
	},
};

static int __init mpls_gso_init(void)
{
	pr_info("MPLS GSO support\n");

	dev_add_offload(&mpls_uc_offload);
	dev_add_offload(&mpls_mc_offload);

	return 0;
}

static void __exit mpls_gso_exit(void)
{
	dev_remove_offload(&mpls_uc_offload);
	dev_remove_offload(&mpls_mc_offload);
}

module_init(mpls_gso_init);
module_exit(mpls_gso_exit);

MODULE_DESCRIPTION("MPLS GSO support");
MODULE_AUTHOR("Simon Horman (horms@verge.net.au)");
MODULE_LICENSE("GPL");
