// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV4 GSO/GRO offload support
 *	Linux INET implementation
 *
 *	GRE GSO support
 */

#include <linux/skbuff.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <net/gre.h>
#include <net/gro.h>

static struct sk_buff *gre_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	int tnl_hlen = skb_inner_mac_header(skb) - skb_transport_header(skb);
	bool need_csum, offload_csum, gso_partial, need_ipsec;
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	u16 mac_offset = skb->mac_header;
	__be16 protocol = skb->protocol;
	u16 mac_len = skb->mac_len;
	int gre_offset, outer_hlen;

	if (!skb->encapsulation)
		goto out;

	if (unlikely(tnl_hlen < sizeof(struct gre_base_hdr)))
		goto out;

	if (unlikely(!pskb_may_pull(skb, tnl_hlen)))
		goto out;

	/* setup inner skb. */
	skb->encapsulation = 0;
	SKB_GSO_CB(skb)->encap_level = 0;
	__skb_pull(skb, tnl_hlen);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb_inner_network_offset(skb));
	skb->mac_len = skb_inner_network_offset(skb);
	skb->protocol = skb->inner_protocol;

	need_csum = !!(skb_shinfo(skb)->gso_type & SKB_GSO_GRE_CSUM);
	skb->encap_hdr_csum = need_csum;

	features &= skb->dev->hw_enc_features;
	if (need_csum)
		features &= ~NETIF_F_SCTP_CRC;

	need_ipsec = skb_dst(skb) && dst_xfrm(skb_dst(skb));
	/* Try to offload checksum if possible */
	offload_csum = !!(need_csum && !need_ipsec &&
			  (skb->dev->features & NETIF_F_HW_CSUM));

	/* segment inner packet. */
	segs = skb_mac_gso_segment(skb, features);
	if (IS_ERR_OR_NULL(segs)) {
		skb_gso_error_unwind(skb, protocol, tnl_hlen, mac_offset,
				     mac_len);
		goto out;
	}

	gso_partial = !!(skb_shinfo(segs)->gso_type & SKB_GSO_PARTIAL);

	outer_hlen = skb_tnl_header_len(skb);
	gre_offset = outer_hlen - tnl_hlen;
	skb = segs;
	do {
		struct gre_base_hdr *greh;
		__sum16 *pcsum;

		/* Set up inner headers if we are offloading inner checksum */
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb_reset_inner_headers(skb);
			skb->encapsulation = 1;
		}

		skb->mac_len = mac_len;
		skb->protocol = protocol;

		__skb_push(skb, outer_hlen);
		skb_reset_mac_header(skb);
		skb_set_network_header(skb, mac_len);
		skb_set_transport_header(skb, gre_offset);

		if (!need_csum)
			continue;

		greh = (struct gre_base_hdr *)skb_transport_header(skb);
		pcsum = (__sum16 *)(greh + 1);

		if (gso_partial && skb_is_gso(skb)) {
			unsigned int partial_adj;

			/* Adjust checksum to account for the fact that
			 * the partial checksum is based on actual size
			 * whereas headers should be based on MSS size.
			 */
			partial_adj = skb->len + skb_headroom(skb) -
				      SKB_GSO_CB(skb)->data_offset -
				      skb_shinfo(skb)->gso_size;
			*pcsum = ~csum_fold((__force __wsum)htonl(partial_adj));
		} else {
			*pcsum = 0;
		}

		*(pcsum + 1) = 0;
		if (skb->encapsulation || !offload_csum) {
			*pcsum = gso_make_checksum(skb, 0);
		} else {
			skb->ip_summed = CHECKSUM_PARTIAL;
			skb->csum_start = skb_transport_header(skb) - skb->head;
			skb->csum_offset = sizeof(*greh);
		}
	} while ((skb = skb->next));
out:
	return segs;
}

static struct sk_buff *gre_gro_receive(struct list_head *head,
				       struct sk_buff *skb)
{
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	const struct gre_base_hdr *greh;
	unsigned int hlen, grehlen;
	unsigned int off;
	int flush = 1;
	struct packet_offload *ptype;
	__be16 type;

	if (NAPI_GRO_CB(skb)->encap_mark)
		goto out;

	NAPI_GRO_CB(skb)->encap_mark = 1;

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

	/* We can only support GRE_CSUM if we can track the location of
	 * the GRE header.  In the case of FOU/GUE we cannot because the
	 * outer UDP header displaces the GRE header leaving us in a state
	 * of limbo.
	 */
	if ((greh->flags & GRE_CSUM) && NAPI_GRO_CB(skb)->is_fou)
		goto out;

	type = greh->protocol;

	rcu_read_lock();
	ptype = gro_find_receive_by_type(type);
	if (!ptype)
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

		skb_gro_checksum_try_convert(skb, IPPROTO_GRE,
					     null_compute_pseudo);
	}

	list_for_each_entry(p, head, list) {
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

	pp = call_gro_receive(ptype->callbacks.gro_receive, head, skb);
	flush = 0;

out_unlock:
	rcu_read_unlock();
out:
	skb_gro_flush_final(skb, pp, flush);

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
	if (ptype)
		err = ptype->callbacks.gro_complete(skb, nhoff + grehlen);

	rcu_read_unlock();

	skb_set_inner_mac_header(skb, nhoff + grehlen);

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
	int err;

	err = inet_add_offload(&gre_offload, IPPROTO_GRE);
#if IS_ENABLED(CONFIG_IPV6)
	if (err)
		return err;

	err = inet6_add_offload(&gre_offload, IPPROTO_GRE);
	if (err)
		inet_del_offload(&gre_offload, IPPROTO_GRE);
#endif

	return err;
}
device_initcall(gre_offload_init);
