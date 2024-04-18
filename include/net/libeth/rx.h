/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBETH_RX_H
#define __LIBETH_RX_H

#include <net/xdp.h>

/* Converting abstract packet type numbers into a software structure with
 * the packet parameters to do O(1) lookup on Rx.
 */

enum {
	LIBETH_RX_PT_OUTER_L2			= 0U,
	LIBETH_RX_PT_OUTER_IPV4,
	LIBETH_RX_PT_OUTER_IPV6,
};

enum {
	LIBETH_RX_PT_NOT_FRAG			= 0U,
	LIBETH_RX_PT_FRAG,
};

enum {
	LIBETH_RX_PT_TUNNEL_IP_NONE		= 0U,
	LIBETH_RX_PT_TUNNEL_IP_IP,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC_VLAN,
};

enum {
	LIBETH_RX_PT_TUNNEL_END_NONE		= 0U,
	LIBETH_RX_PT_TUNNEL_END_IPV4,
	LIBETH_RX_PT_TUNNEL_END_IPV6,
};

enum {
	LIBETH_RX_PT_INNER_NONE			= 0U,
	LIBETH_RX_PT_INNER_UDP,
	LIBETH_RX_PT_INNER_TCP,
	LIBETH_RX_PT_INNER_SCTP,
	LIBETH_RX_PT_INNER_ICMP,
	LIBETH_RX_PT_INNER_TIMESYNC,
};

#define LIBETH_RX_PT_PAYLOAD_NONE		PKT_HASH_TYPE_NONE
#define LIBETH_RX_PT_PAYLOAD_L2			PKT_HASH_TYPE_L2
#define LIBETH_RX_PT_PAYLOAD_L3			PKT_HASH_TYPE_L3
#define LIBETH_RX_PT_PAYLOAD_L4			PKT_HASH_TYPE_L4

struct libeth_rx_pt {
	u32					outer_ip:2;
	u32					outer_frag:1;
	u32					tunnel_type:3;
	u32					tunnel_end_prot:2;
	u32					tunnel_end_frag:1;
	u32					inner_prot:3;
	enum pkt_hash_types			payload_layer:2;

	u32					pad:2;
	enum xdp_rss_hash_type			hash_type:16;
};

void libeth_rx_pt_gen_hash_type(struct libeth_rx_pt *pt);

/**
 * libeth_rx_pt_get_ip_ver - get IP version from a packet type structure
 * @pt: packet type params
 *
 * Wrapper to compile out the IPv6 code from the drivers when not supported
 * by the kernel.
 *
 * Return: @pt.outer_ip or stub for IPv6 when not compiled-in.
 */
static inline u32 libeth_rx_pt_get_ip_ver(struct libeth_rx_pt pt)
{
#if !IS_ENABLED(CONFIG_IPV6)
	switch (pt.outer_ip) {
	case LIBETH_RX_PT_OUTER_IPV4:
		return LIBETH_RX_PT_OUTER_IPV4;
	default:
		return LIBETH_RX_PT_OUTER_L2;
	}
#else
	return pt.outer_ip;
#endif
}

/* libeth_has_*() can be used to quickly check whether the HW metadata is
 * available to avoid further expensive processing such as descriptor reads.
 * They already check for the corresponding netdev feature to be enabled,
 * thus can be used as drop-in replacements.
 */

static inline bool libeth_rx_pt_has_checksum(const struct net_device *dev,
					     struct libeth_rx_pt pt)
{
	/* Non-zero _INNER* is only possible when _OUTER_IPV* is set,
	 * it is enough to check only for the L4 type.
	 */
	return likely(pt.inner_prot > LIBETH_RX_PT_INNER_NONE &&
		      (dev->features & NETIF_F_RXCSUM));
}

static inline bool libeth_rx_pt_has_hash(const struct net_device *dev,
					 struct libeth_rx_pt pt)
{
	return likely(pt.payload_layer > LIBETH_RX_PT_PAYLOAD_NONE &&
		      (dev->features & NETIF_F_RXHASH));
}

/**
 * libeth_rx_pt_set_hash - fill in skb hash value basing on the PT
 * @skb: skb to fill the hash in
 * @hash: 32-bit hash value from the descriptor
 * @pt: packet type
 */
static inline void libeth_rx_pt_set_hash(struct sk_buff *skb, u32 hash,
					 struct libeth_rx_pt pt)
{
	skb_set_hash(skb, hash, pt.payload_layer);
}

#endif /* __LIBETH_RX_H */
