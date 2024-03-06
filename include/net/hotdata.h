/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_HOTDATA_H
#define _NET_HOTDATA_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <net/protocol.h>

/* Read mostly data used in network fast paths. */
struct net_hotdata {
#if IS_ENABLED(CONFIG_INET)
	struct packet_offload	ip_packet_offload;
	struct net_offload	tcpv4_offload;
	struct packet_offload	ipv6_packet_offload;
	struct net_offload	tcpv6_offload;
#endif
	struct list_head	offload_base;
	struct list_head	ptype_all;
	struct kmem_cache	*skbuff_cache;
	struct kmem_cache	*skbuff_fclone_cache;
	struct kmem_cache	*skb_small_head_cache;
	int			gro_normal_batch;
	int			netdev_budget;
	int			netdev_budget_usecs;
	int			tstamp_prequeue;
	int			max_backlog;
	int			dev_tx_weight;
	int			dev_rx_weight;
};

extern struct net_hotdata net_hotdata;

#endif /* _NET_HOTDATA_H */
