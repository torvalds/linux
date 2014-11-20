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
#include <linux/init.h>
#include <net/protocol.h>
#include <net/gre.h>

static struct sk_buff *gre_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	netdev_features_t enc_features;
	int ghl;
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
				  SKB_GSO_GRE_CSUM |
				  SKB_GSO_IPIP)))
		goto out;

	if (!skb->encapsulation)
		goto out;

	if (unlikely(!pskb_may_pull(skb, sizeof(*greh))))
		goto out;

	greh = (struct gre_base_hdr *)skb_transport_header(skb);

	ghl = skb_inner_mac_header(skb) - skb_transport_header(skb);
	if (unlikely(ghl < sizeof(*greh)))
		goto out;

	csum = !!(greh->flags & GRE_CSUM);
	if (csum)
		skb->encap_hdr_csum = 1;

	/* setup inner skb. */
	skb->protocol = greh->protocol;
	skb->encapsulation = 0;

	if (unlikely(!pskb_may_pull(skb, ghl)))
		goto out;

	__skb_pull(skb, ghl);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb_inner_network_offset(skb));
	skb->mac_len = skb_inner_network_offset(skb);

	/* segment inner packet. */
	enc_features = skb->dev->hw_enc_features & features;
	segs = skb_mac_gso_segment(skb, enc_features);
	if (IS_ERR_OR_NULL(segs)) {
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

			skb_reset_transport_header(skb);

			greh = (struct gre_base_hdr *)
			    skb_transport_header(skb);
			pcsum = (__be32 *)(greh + 1);
			*pcsum = 0;
			*(__sum16 *)pcsum = gso_make_checksum(skb, 0);
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

static struct sk_buff **gre_gro_receive(struct sk_buff **head,
					struct sk_buff *skb)
{
	struct sk_buff **pp = NULL;
	struct sk_buff *p;
	const struct gre_base_hdr *greh;
	unsigned int hlen, grehlen;
	unsigned int off;
	int flush = 1;
	struct packet_offload *ptype;
	__be16 type;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*greh);
	greh = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, hlen)) {
		greh = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!greh))
			goto out;
	}

	/* Only support version 0 and K (key), C (csum) flags. Note that
	 * although the support for the S (seq#) flag can be added easily
	 * for GRO, this is problematic for GSO hence can not be enabled
	 * here because a GRO pkt may end up in the forwarding path, thus
	 * requiring GSO support to break it up correctly.
	 */
	if ((greh->flags & ~(GRE_KEY|GRE_CSUM)) != 0)
		goto out;

	type = greh->protocol;

	rcu_read_lock();
	ptype = gro_find_receive_by_type(type);
	if (ptype == NULL)
		goto out_unlock;

	grehlen = GRE_HEADER_SECTION;

	if (greh->flags & GRE_KEY)
		grehlen += GRE_HEADER_SECTION;

	if (greh->flags & GRE_CSUM)
		grehlen += GRE_HEADER_SECTION;

	hlen = off + grehlen;
	if (skb_gro_header_hard(skb, hlen)) {
		greh = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!greh))
			goto out_unlock;
	}

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if ((greh->flags & GRE_CSUM) && !NAPI_GRO_CB(skb)->flush) {
		if (skb_gro_checksum_simple_validate(skb))
			goto out_unlock;

		skb_gro_checksum_try_convert(skb, IPPROTO_GRE, 0,
					     null_compute_pseudo);
	}

	flush = 0;

	for (p = *head; p; p = p->next) {
		const struct gre_base_hdr *greh2;

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		/* The following checks are needed to ensure only pkts
		 * from the same tunnel are considered for aggregation.
		 * The criteria for "the same tunnel" includes:
		 * 1) same version (we only support version 0 here)
		 * 2) same protocol (we only support ETH_P_IP for now)
		 * 3) same set of flags
		 * 4) same key if the key field is present.
		 */
		greh2 = (struct gre_base_hdr *)(p->data + off);

		if (greh2->flags != greh->flags ||
		    greh2->protocol != greh->protocol) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
		if (greh->flags & GRE_KEY) {
			/* compare keys */
			if (*(__be32 *)(greh2+1) != *(__be32 *)(greh+1)) {
				NAPI_GRO_CB(p)->same_flow = 0;
				continue;
			}
		}
	}

	skb_gro_pull(skb, grehlen);

	/* Adjusted NAPI_GRO_CB(skb)->csum after skb_gro_pull()*/
	skb_gro_postpull_rcsum(skb, greh, grehlen);

	pp = ptype->callbacks.gro_receive(head, skb);

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}

static int gre_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct gre_base_hdr *greh = (struct gre_base_hdr *)(skb->data + nhoff);
	struct packet_offload *ptype;
	unsigned int grehlen = sizeof(*greh);
	int err = -ENOENT;
	__be16 type;

	skb->encapsulation = 1;
	skb_shinfo(skb)->gso_type = SKB_GSO_GRE;

	type = greh->protocol;
	if (greh->flags & GRE_KEY)
		grehlen += GRE_HEADER_SECTION;

	if (greh->flags & GRE_CSUM)
		grehlen += GRE_HEADER_SECTION;

	rcu_read_lock();
	ptype = gro_find_complete_by_type(type);
	if (ptype != NULL)
		err = ptype->callbacks.gro_complete(skb, nhoff + grehlen);

	rcu_read_unlock();
	return err;
}

static const struct net_offload gre_offload = {
	.callbacks = {
		.gso_segment = gre_gso_segment,
		.gro_receive = gre_gro_receive,
		.gro_complete = gre_gro_complete,
	},
};

static int __init gre_offload_init(void)
{
	return inet_add_offload(&gre_offload, IPPROTO_GRE);
}
device_initcall(gre_offload_init);
