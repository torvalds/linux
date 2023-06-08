// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/skbuff.h>
#include <linux/sctp.h>
#include <net/gso.h>
#include <net/gro.h>

/**
 *	skb_eth_gso_segment - segmentation handler for ethernet protocols.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 *	@type: Ethernet Protocol ID
 */
struct sk_buff *skb_eth_gso_segment(struct sk_buff *skb,
				    netdev_features_t features, __be16 type)
{
	struct sk_buff *segs = ERR_PTR(-EPROTONOSUPPORT);
	struct packet_offload *ptype;

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &offload_base, list) {
		if (ptype->type == type && ptype->callbacks.gso_segment) {
			segs = ptype->callbacks.gso_segment(skb, features);
			break;
		}
	}
	rcu_read_unlock();

	return segs;
}
EXPORT_SYMBOL(skb_eth_gso_segment);

/**
 *	skb_mac_gso_segment - mac layer segmentation handler.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 */
struct sk_buff *skb_mac_gso_segment(struct sk_buff *skb,
				    netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EPROTONOSUPPORT);
	struct packet_offload *ptype;
	int vlan_depth = skb->mac_len;
	__be16 type = skb_network_protocol(skb, &vlan_depth);

	if (unlikely(!type))
		return ERR_PTR(-EINVAL);

	__skb_pull(skb, vlan_depth);

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &offload_base, list) {
		if (ptype->type == type && ptype->callbacks.gso_segment) {
			segs = ptype->callbacks.gso_segment(skb, features);
			break;
		}
	}
	rcu_read_unlock();

	__skb_push(skb, skb->data - skb_mac_header(skb));

	return segs;
}
EXPORT_SYMBOL(skb_mac_gso_segment);
/* openvswitch calls this on rx path, so we need a different check.
 */
static bool skb_needs_check(const struct sk_buff *skb, bool tx_path)
{
	if (tx_path)
		return skb->ip_summed != CHECKSUM_PARTIAL &&
		       skb->ip_summed != CHECKSUM_UNNECESSARY;

	return skb->ip_summed == CHECKSUM_NONE;
}

/**
 *	__skb_gso_segment - Perform segmentation on skb.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 *	@tx_path: whether it is called in TX path
 *
 *	This function segments the given skb and returns a list of segments.
 *
 *	It may return NULL if the skb requires no segmentation.  This is
 *	only possible when GSO is used for verifying header integrity.
 *
 *	Segmentation preserves SKB_GSO_CB_OFFSET bytes of previous skb cb.
 */
struct sk_buff *__skb_gso_segment(struct sk_buff *skb,
				  netdev_features_t features, bool tx_path)
{
	struct sk_buff *segs;

	if (unlikely(skb_needs_check(skb, tx_path))) {
		int err;

		/* We're going to init ->check field in TCP or UDP header */
		err = skb_cow_head(skb, 0);
		if (err < 0)
			return ERR_PTR(err);
	}

	/* Only report GSO partial support if it will enable us to
	 * support segmentation on this frame without needing additional
	 * work.
	 */
	if (features & NETIF_F_GSO_PARTIAL) {
		netdev_features_t partial_features = NETIF_F_GSO_ROBUST;
		struct net_device *dev = skb->dev;

		partial_features |= dev->features & dev->gso_partial_features;
		if (!skb_gso_ok(skb, features | partial_features))
			features &= ~NETIF_F_GSO_PARTIAL;
	}

	BUILD_BUG_ON(SKB_GSO_CB_OFFSET +
		     sizeof(*SKB_GSO_CB(skb)) > sizeof(skb->cb));

	SKB_GSO_CB(skb)->mac_offset = skb_headroom(skb);
	SKB_GSO_CB(skb)->encap_level = 0;

	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	segs = skb_mac_gso_segment(skb, features);

	if (segs != skb && unlikely(skb_needs_check(skb, tx_path) && !IS_ERR(segs)))
		skb_warn_bad_offload(skb);

	return segs;
}
EXPORT_SYMBOL(__skb_gso_segment);

/**
 * skb_gso_transport_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_transport_seglen is used to determine the real size of the
 * individual segments, including Layer4 headers (TCP/UDP).
 *
 * The MAC/L2 or network (IP, IPv6) headers are not accounted for.
 */
static unsigned int skb_gso_transport_seglen(const struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	unsigned int thlen = 0;

	if (skb->encapsulation) {
		thlen = skb_inner_transport_header(skb) -
			skb_transport_header(skb);

		if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)))
			thlen += inner_tcp_hdrlen(skb);
	} else if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))) {
		thlen = tcp_hdrlen(skb);
	} else if (unlikely(skb_is_gso_sctp(skb))) {
		thlen = sizeof(struct sctphdr);
	} else if (shinfo->gso_type & SKB_GSO_UDP_L4) {
		thlen = sizeof(struct udphdr);
	}
	/* UFO sets gso_size to the size of the fragmentation
	 * payload, i.e. the size of the L4 (UDP) header is already
	 * accounted for.
	 */
	return thlen + shinfo->gso_size;
}

/**
 * skb_gso_network_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_network_seglen is used to determine the real size of the
 * individual segments, including Layer3 (IP, IPv6) and L4 headers (TCP/UDP).
 *
 * The MAC/L2 header is not accounted for.
 */
static unsigned int skb_gso_network_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) -
			       skb_network_header(skb);

	return hdr_len + skb_gso_transport_seglen(skb);
}

/**
 * skb_gso_mac_seglen - Return length of individual segments of a gso packet
 *
 * @skb: GSO skb
 *
 * skb_gso_mac_seglen is used to determine the real size of the
 * individual segments, including MAC/L2, Layer3 (IP, IPv6) and L4
 * headers (TCP/UDP).
 */
static unsigned int skb_gso_mac_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) - skb_mac_header(skb);

	return hdr_len + skb_gso_transport_seglen(skb);
}

/**
 * skb_gso_size_check - check the skb size, considering GSO_BY_FRAGS
 *
 * There are a couple of instances where we have a GSO skb, and we
 * want to determine what size it would be after it is segmented.
 *
 * We might want to check:
 * -    L3+L4+payload size (e.g. IP forwarding)
 * - L2+L3+L4+payload size (e.g. sanity check before passing to driver)
 *
 * This is a helper to do that correctly considering GSO_BY_FRAGS.
 *
 * @skb: GSO skb
 *
 * @seg_len: The segmented length (from skb_gso_*_seglen). In the
 *           GSO_BY_FRAGS case this will be [header sizes + GSO_BY_FRAGS].
 *
 * @max_len: The maximum permissible length.
 *
 * Returns true if the segmented length <= max length.
 */
static inline bool skb_gso_size_check(const struct sk_buff *skb,
				      unsigned int seg_len,
				      unsigned int max_len) {
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	const struct sk_buff *iter;

	if (shinfo->gso_size != GSO_BY_FRAGS)
		return seg_len <= max_len;

	/* Undo this so we can re-use header sizes */
	seg_len -= GSO_BY_FRAGS;

	skb_walk_frags(skb, iter) {
		if (seg_len + skb_headlen(iter) > max_len)
			return false;
	}

	return true;
}

/**
 * skb_gso_validate_network_len - Will a split GSO skb fit into a given MTU?
 *
 * @skb: GSO skb
 * @mtu: MTU to validate against
 *
 * skb_gso_validate_network_len validates if a given skb will fit a
 * wanted MTU once split. It considers L3 headers, L4 headers, and the
 * payload.
 */
bool skb_gso_validate_network_len(const struct sk_buff *skb, unsigned int mtu)
{
	return skb_gso_size_check(skb, skb_gso_network_seglen(skb), mtu);
}
EXPORT_SYMBOL_GPL(skb_gso_validate_network_len);

/**
 * skb_gso_validate_mac_len - Will a split GSO skb fit in a given length?
 *
 * @skb: GSO skb
 * @len: length to validate against
 *
 * skb_gso_validate_mac_len validates if a given skb will fit a wanted
 * length once split, including L2, L3 and L4 headers and the payload.
 */
bool skb_gso_validate_mac_len(const struct sk_buff *skb, unsigned int len)
{
	return skb_gso_size_check(skb, skb_gso_mac_seglen(skb), len);
}
EXPORT_SYMBOL_GPL(skb_gso_validate_mac_len);

