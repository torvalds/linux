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

static DEFINE_SPINLOCK(udp_offload_lock);
static struct udp_offload_priv __rcu *udp_offload_base __read_mostly;

#define udp_deref_protected(X) rcu_dereference_protected(X, lockdep_is_held(&udp_offload_lock))

struct udp_offload_priv {
	struct udp_offload	*offload;
	struct rcu_head		rcu;
	struct udp_offload_priv __rcu *next;
};

static struct sk_buff *__skb_udp_tunnel_segment(struct sk_buff *skb,
	netdev_features_t features,
	struct sk_buff *(*gso_inner_segment)(struct sk_buff *skb,
					     netdev_features_t features),
	__be16 new_protocol, bool is_ipv6)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	u16 mac_offset = skb->mac_header;
	int mac_len = skb->mac_len;
	int tnl_hlen = skb_inner_mac_header(skb) - skb_transport_header(skb);
	__be16 protocol = skb->protocol;
	netdev_features_t enc_features;
	int udp_offset, outer_hlen;
	unsigned int oldlen;
	bool need_csum = !!(skb_shinfo(skb)->gso_type &
			    SKB_GSO_UDP_TUNNEL_CSUM);
	bool remcsum = !!(skb_shinfo(skb)->gso_type & SKB_GSO_TUNNEL_REMCSUM);
	bool offload_csum = false, dont_encap = (need_csum || remcsum);

	oldlen = (u16)~skb->len;

	if (unlikely(!pskb_may_pull(skb, tnl_hlen)))
		goto out;

	skb->encapsulation = 0;
	__skb_pull(skb, tnl_hlen);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb_inner_network_offset(skb));
	skb->mac_len = skb_inner_network_offset(skb);
	skb->protocol = new_protocol;
	skb->encap_hdr_csum = need_csum;
	skb->remcsum_offload = remcsum;

	/* Try to offload checksum if possible */
	offload_csum = !!(need_csum &&
			  (skb->dev->features &
			   (is_ipv6 ? NETIF_F_V6_CSUM : NETIF_F_V4_CSUM)));

	/* segment inner packet. */
	enc_features = skb->dev->hw_enc_features & features;
	segs = gso_inner_segment(skb, enc_features);
	if (IS_ERR_OR_NULL(segs)) {
		skb_gso_error_unwind(skb, protocol, tnl_hlen, mac_offset,
				     mac_len);
		goto out;
	}

	outer_hlen = skb_tnl_header_len(skb);
	udp_offset = outer_hlen - tnl_hlen;
	skb = segs;
	do {
		struct udphdr *uh;
		int len;
		__be32 delta;

		if (dont_encap) {
			skb->encapsulation = 0;
			skb->ip_summed = CHECKSUM_NONE;
		} else {
			/* Only set up inner headers if we might be offloading
			 * inner checksum.
			 */
			skb_reset_inner_headers(skb);
			skb->encapsulation = 1;
		}

		skb->mac_len = mac_len;
		skb->protocol = protocol;

		skb_push(skb, outer_hlen);
		skb_reset_mac_header(skb);
		skb_set_network_header(skb, mac_len);
		skb_set_transport_header(skb, udp_offset);
		len = skb->len - udp_offset;
		uh = udp_hdr(skb);
		uh->len = htons(len);

		if (!need_csum)
			continue;

		delta = htonl(oldlen + len);

		uh->check = ~csum_fold((__force __wsum)
				       ((__force u32)uh->check +
					(__force u32)delta));
		if (offload_csum) {
			skb->ip_summed = CHECKSUM_PARTIAL;
			skb->csum_start = skb_transport_header(skb) - skb->head;
			skb->csum_offset = offsetof(struct udphdr, check);
		} else if (remcsum) {
			/* Need to calculate checksum from scratch,
			 * inner checksums are never when doing
			 * remote_checksum_offload.
			 */

			skb->csum = skb_checksum(skb, udp_offset,
						 skb->len - udp_offset,
						 0);
			uh->check = csum_fold(skb->csum);
			if (uh->check == 0)
				uh->check = CSUM_MANGLED_0;
		} else {
			uh->check = gso_make_checksum(skb, ~uh->check);

			if (uh->check == 0)
				uh->check = CSUM_MANGLED_0;
		}
	} while ((skb = skb->next));
out:
	return segs;
}

struct sk_buff *skb_udp_tunnel_segment(struct sk_buff *skb,
				       netdev_features_t features,
				       bool is_ipv6)
{
	__be16 protocol = skb->protocol;
	const struct net_offload **offloads;
	const struct net_offload *ops;
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct sk_buff *(*gso_inner_segment)(struct sk_buff *skb,
					     netdev_features_t features);

	rcu_read_lock();

	switch (skb->inner_protocol_type) {
	case ENCAP_TYPE_ETHER:
		protocol = skb->inner_protocol;
		gso_inner_segment = skb_mac_gso_segment;
		break;
	case ENCAP_TYPE_IPPROTO:
		offloads = is_ipv6 ? inet6_offloads : inet_offloads;
		ops = rcu_dereference(offloads[skb->inner_ipproto]);
		if (!ops || !ops->callbacks.gso_segment)
			goto out_unlock;
		gso_inner_segment = ops->callbacks.gso_segment;
		break;
	default:
		goto out_unlock;
	}

	segs = __skb_udp_tunnel_segment(skb, features, gso_inner_segment,
					protocol, is_ipv6);

out_unlock:
	rcu_read_unlock();

	return segs;
}

static struct sk_buff *udp4_ufo_fragment(struct sk_buff *skb,
					 netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	unsigned int mss;
	__wsum csum;
	struct udphdr *uh;
	struct iphdr *iph;

	if (skb->encapsulation &&
	    (skb_shinfo(skb)->gso_type &
	     (SKB_GSO_UDP_TUNNEL|SKB_GSO_UDP_TUNNEL_CSUM))) {
		segs = skb_udp_tunnel_segment(skb, features, false);
		goto out;
	}

	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto out;

	mss = skb_shinfo(skb)->gso_size;
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */
		int type = skb_shinfo(skb)->gso_type;

		if (unlikely(type & ~(SKB_GSO_UDP | SKB_GSO_DODGY |
				      SKB_GSO_UDP_TUNNEL |
				      SKB_GSO_UDP_TUNNEL_CSUM |
				      SKB_GSO_TUNNEL_REMCSUM |
				      SKB_GSO_IPIP |
				      SKB_GSO_GRE | SKB_GSO_GRE_CSUM) ||
			     !(type & (SKB_GSO_UDP))))
			goto out;

		skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(skb->len, mss);

		segs = NULL;
		goto out;
	}

	/* Do software UFO. Complete and fill in the UDP checksum as
	 * HW cannot do checksum of UDP packets sent as multiple
	 * IP fragments.
	 */

	uh = udp_hdr(skb);
	iph = ip_hdr(skb);

	uh->check = 0;
	csum = skb_checksum(skb, 0, skb->len, 0);
	uh->check = udp_v4_check(skb->len, iph->saddr, iph->daddr, csum);
	if (uh->check == 0)
		uh->check = CSUM_MANGLED_0;

	skb->ip_summed = CHECKSUM_NONE;

	/* Fragment the skb. IP headers of the fragments are updated in
	 * inet_gso_segment()
	 */
	segs = skb_segment(skb, features);
out:
	return segs;
}

int udp_add_offload(struct udp_offload *uo)
{
	struct udp_offload_priv *new_offload = kzalloc(sizeof(*new_offload), GFP_ATOMIC);

	if (!new_offload)
		return -ENOMEM;

	new_offload->offload = uo;

	spin_lock(&udp_offload_lock);
	new_offload->next = udp_offload_base;
	rcu_assign_pointer(udp_offload_base, new_offload);
	spin_unlock(&udp_offload_lock);

	return 0;
}
EXPORT_SYMBOL(udp_add_offload);

static void udp_offload_free_routine(struct rcu_head *head)
{
	struct udp_offload_priv *ou_priv = container_of(head, struct udp_offload_priv, rcu);
	kfree(ou_priv);
}

void udp_del_offload(struct udp_offload *uo)
{
	struct udp_offload_priv __rcu **head = &udp_offload_base;
	struct udp_offload_priv *uo_priv;

	spin_lock(&udp_offload_lock);

	uo_priv = udp_deref_protected(*head);
	for (; uo_priv != NULL;
	     uo_priv = udp_deref_protected(*head)) {
		if (uo_priv->offload == uo) {
			rcu_assign_pointer(*head,
					   udp_deref_protected(uo_priv->next));
			goto unlock;
		}
		head = &uo_priv->next;
	}
	pr_warn("udp_del_offload: didn't find offload for port %d\n", ntohs(uo->port));
unlock:
	spin_unlock(&udp_offload_lock);
	if (uo_priv != NULL)
		call_rcu(&uo_priv->rcu, udp_offload_free_routine);
}
EXPORT_SYMBOL(udp_del_offload);

struct sk_buff **udp_gro_receive(struct sk_buff **head, struct sk_buff *skb,
				 struct udphdr *uh)
{
	struct udp_offload_priv *uo_priv;
	struct sk_buff *p, **pp = NULL;
	struct udphdr *uh2;
	unsigned int off = skb_gro_offset(skb);
	int flush = 1;

	if (NAPI_GRO_CB(skb)->udp_mark ||
	    (skb->ip_summed != CHECKSUM_PARTIAL &&
	     NAPI_GRO_CB(skb)->csum_cnt == 0 &&
	     !NAPI_GRO_CB(skb)->csum_valid))
		goto out;

	/* mark that this skb passed once through the udp gro layer */
	NAPI_GRO_CB(skb)->udp_mark = 1;

	rcu_read_lock();
	uo_priv = rcu_dereference(udp_offload_base);
	for (; uo_priv != NULL; uo_priv = rcu_dereference(uo_priv->next)) {
		if (uo_priv->offload->port == uh->dest &&
		    uo_priv->offload->callbacks.gro_receive)
			goto unflush;
	}
	goto out_unlock;

unflush:
	flush = 0;

	for (p = *head; p; p = p->next) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		uh2 = (struct udphdr   *)(p->data + off);

		/* Match ports and either checksums are either both zero
		 * or nonzero.
		 */
		if ((*(u32 *)&uh->source != *(u32 *)&uh2->source) ||
		    (!uh->check ^ !uh2->check)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	skb_gro_pull(skb, sizeof(struct udphdr)); /* pull encapsulating udp header */
	skb_gro_postpull_rcsum(skb, uh, sizeof(struct udphdr));
	NAPI_GRO_CB(skb)->proto = uo_priv->offload->ipproto;
	pp = uo_priv->offload->callbacks.gro_receive(head, skb,
						     uo_priv->offload);

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;
	return pp;
}

static struct sk_buff **udp4_gro_receive(struct sk_buff **head,
					 struct sk_buff *skb)
{
	struct udphdr *uh = udp_gro_udphdr(skb);

	if (unlikely(!uh))
		goto flush;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (NAPI_GRO_CB(skb)->flush)
		goto skip;

	if (skb_gro_checksum_validate_zero_check(skb, IPPROTO_UDP, uh->check,
						 inet_gro_compute_pseudo))
		goto flush;
	else if (uh->check)
		skb_gro_checksum_try_convert(skb, IPPROTO_UDP, uh->check,
					     inet_gro_compute_pseudo);
skip:
	NAPI_GRO_CB(skb)->is_ipv6 = 0;
	return udp_gro_receive(head, skb, uh);

flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}

int udp_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct udp_offload_priv *uo_priv;
	__be16 newlen = htons(skb->len - nhoff);
	struct udphdr *uh = (struct udphdr *)(skb->data + nhoff);
	int err = -ENOSYS;

	uh->len = newlen;

	rcu_read_lock();

	uo_priv = rcu_dereference(udp_offload_base);
	for (; uo_priv != NULL; uo_priv = rcu_dereference(uo_priv->next)) {
		if (uo_priv->offload->port == uh->dest &&
		    uo_priv->offload->callbacks.gro_complete)
			break;
	}

	if (uo_priv != NULL) {
		NAPI_GRO_CB(skb)->proto = uo_priv->offload->ipproto;
		err = uo_priv->offload->callbacks.gro_complete(skb,
				nhoff + sizeof(struct udphdr),
				uo_priv->offload);
	}

	rcu_read_unlock();

	if (skb->remcsum_offload)
		skb_shinfo(skb)->gso_type |= SKB_GSO_TUNNEL_REMCSUM;

	skb->encapsulation = 1;
	skb_set_inner_mac_header(skb, nhoff + sizeof(struct udphdr));

	return err;
}

static int udp4_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr *uh = (struct udphdr *)(skb->data + nhoff);

	if (uh->check) {
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
		uh->check = ~udp_v4_check(skb->len - nhoff, iph->saddr,
					  iph->daddr, 0);
	} else {
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
	}

	return udp_gro_complete(skb, nhoff);
}

static const struct net_offload udpv4_offload = {
	.callbacks = {
		.gso_segment = udp4_ufo_fragment,
		.gro_receive  =	udp4_gro_receive,
		.gro_complete =	udp4_gro_complete,
	},
};

int __init udpv4_offload_init(void)
{
	return inet_add_offload(&udpv4_offload, IPPROTO_UDP);
}
