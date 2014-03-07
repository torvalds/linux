/*
 *	IPV4 GSO/GRO offload support
 *	Linux INET implementation
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	GRE GSO support
 */

#include <linux/skbuff.h>
#include <net/protocol.h>
#include <net/gre.h>

static int gre_gso_send_check(struct sk_buff *skb)
{
	if (!skb->encapsulation)
		return -EINVAL;
	return 0;
}

static struct sk_buff *gre_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	netdev_features_t enc_features;
	int ghl = GRE_HEADER_SECTION;
	struct gre_base_hdr *greh;
	u16 mac_offset = skb->mac_header;
	int mac_len = skb->mac_len;
	__be16 protocol = skb->protocol;
	int tnl_hlen;
	bool csum;

	if (unlikely(skb_shinfo(skb)->gso_type &
				~(SKB_GSO_TCPV4 |
				  SKB_GSO_TCPV6 |
				  SKB_GSO_UDP |
				  SKB_GSO_DODGY |
				  SKB_GSO_TCP_ECN |
				  SKB_GSO_GRE |
				  SKB_GSO_IPIP)))
		goto out;

	if (unlikely(!pskb_may_pull(skb, sizeof(*greh))))
		goto out;

	greh = (struct gre_base_hdr *)skb_transport_header(skb);

	if (greh->flags & GRE_KEY)
		ghl += GRE_HEADER_SECTION;
	if (greh->flags & GRE_SEQ)
		ghl += GRE_HEADER_SECTION;
	if (greh->flags & GRE_CSUM) {
		ghl += GRE_HEADER_SECTION;
		csum = true;
	} else
		csum = false;

	if (unlikely(!pskb_may_pull(skb, ghl)))
		goto out;

	/* setup inner skb. */
	skb->protocol = greh->protocol;
	skb->encapsulation = 0;

	__skb_pull(skb, ghl);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb_inner_network_offset(skb));
	skb->mac_len = skb_inner_network_offset(skb);

	/* segment inner packet. */
	enc_features = skb->dev->hw_enc_features & netif_skb_features(skb);
	segs = skb_mac_gso_segment(skb, enc_features);
	if (!segs || IS_ERR(segs)) {
		skb_gso_error_unwind(skb, protocol, ghl, mac_offset, mac_len);
		goto out;
	}

	skb = segs;
	tnl_hlen = skb_tnl_header_len(skb);
	do {
		__skb_push(skb, ghl);
		if (csum) {
			__be32 *pcsum;

			if (skb_has_shared_frag(skb)) {
				int err;

				err = __skb_linearize(skb);
				if (err) {
					kfree_skb_list(segs);
					segs = ERR_PTR(err);
					goto out;
				}
			}

			greh = (struct gre_base_hdr *)(skb->data);
			pcsum = (__be32 *)(greh + 1);
			*pcsum = 0;
			*(__sum16 *)pcsum = csum_fold(skb_checksum(skb, 0, skb->len, 0));
		}
		__skb_push(skb, tnl_hlen - ghl);

		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;

		skb_reset_mac_header(skb);
		skb_set_network_header(skb, mac_len);
		skb->mac_len = mac_len;
		skb->protocol = protocol;
	} while ((skb = skb->next));
out:
	return segs;
}

static const struct net_offload gre_offload = {
	.callbacks = {
		.gso_send_check = gre_gso_send_check,
		.gso_segment = gre_gso_segment,
	},
};

int __init gre_offload_init(void)
{
	return inet_add_offload(&gre_offload, IPPROTO_GRE);
}

void __exit gre_offload_exit(void)
{
	inet_del_offload(&gre_offload, IPPROTO_GRE);
}
