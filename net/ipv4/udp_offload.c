/*
 *	IPV4 GSO/GRO offload support
 *	Linux INET implementation
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	UDPv4 GSO support
 */

#include <linux/skbuff.h>
#include <net/udp.h>
#include <net/protocol.h>

static int udp4_ufo_send_check(struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		return -EINVAL;

	if (likely(!skb->encapsulation)) {
		const struct iphdr *iph;
		struct udphdr *uh;

		iph = ip_hdr(skb);
		uh = udp_hdr(skb);

		uh->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr, skb->len,
				IPPROTO_UDP, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct udphdr, check);
		skb->ip_summed = CHECKSUM_PARTIAL;
	}

	return 0;
}

static struct sk_buff *udp4_ufo_fragment(struct sk_buff *skb,
					 netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	unsigned int mss;

	mss = skb_shinfo(skb)->gso_size;
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */
		int type = skb_shinfo(skb)->gso_type;

		if (unlikely(type & ~(SKB_GSO_UDP | SKB_GSO_DODGY |
				      SKB_GSO_UDP_TUNNEL |
				      SKB_GSO_IPIP |
				      SKB_GSO_GRE | SKB_GSO_MPLS) ||
			     !(type & (SKB_GSO_UDP))))
			goto out;

		skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(skb->len, mss);

		segs = NULL;
		goto out;
	}

	/* Fragment the skb. IP headers of the fragments are updated in
	 * inet_gso_segment()
	 */
	if (skb->encapsulation && skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL)
		segs = skb_udp_tunnel_segment(skb, features);
	else {
		int offset;
		__wsum csum;

		/* Do software UFO. Complete and fill in the UDP checksum as
		 * HW cannot do checksum of UDP packets sent as multiple
		 * IP fragments.
		 */
		offset = skb_checksum_start_offset(skb);
		csum = skb_checksum(skb, offset, skb->len - offset, 0);
		offset += skb->csum_offset;
		*(__sum16 *)(skb->data + offset) = csum_fold(csum);
		skb->ip_summed = CHECKSUM_NONE;

		segs = skb_segment(skb, features);
	}
out:
	return segs;
}

static const struct net_offload udpv4_offload = {
	.callbacks = {
		.gso_send_check = udp4_ufo_send_check,
		.gso_segment = udp4_ufo_fragment,
	},
};

int __init udpv4_offload_init(void)
{
	return inet_add_offload(&udpv4_offload, IPPROTO_UDP);
}
