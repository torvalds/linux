/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 Nicira, Inc.
 */

#ifndef _NET_MPLS_H
#define _NET_MPLS_H 1

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/mpls.h>

#define MPLS_HLEN 4

struct mpls_shim_hdr {
	__be32 label_stack_entry;
};

static inline bool eth_p_mpls(__be16 eth_type)
{
	return eth_type == htons(ETH_P_MPLS_UC) ||
		eth_type == htons(ETH_P_MPLS_MC);
}

static inline struct mpls_shim_hdr *mpls_hdr(const struct sk_buff *skb)
{
	return (struct mpls_shim_hdr *)skb_network_header(skb);
}

static inline struct mpls_shim_hdr mpls_entry_encode(u32 label,
						     unsigned int ttl,
						     unsigned int tc,
						     bool bos)
{
	struct mpls_shim_hdr result;

	result.label_stack_entry =
		cpu_to_be32((label << MPLS_LS_LABEL_SHIFT) |
			    (tc << MPLS_LS_TC_SHIFT) |
			    (bos ? (1 << MPLS_LS_S_SHIFT) : 0) |
			    (ttl << MPLS_LS_TTL_SHIFT));
	return result;
}

#endif
