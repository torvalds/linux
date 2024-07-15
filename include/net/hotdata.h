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
	struct net_protocol	tcp_protocol;
	struct net_offload 	udpv4_offload;
	struct net_protocol	udp_protocol;
	struct packet_offload	ipv6_packet_offload;
	struct net_offload	tcpv6_offload;
#if IS_ENABLED(CONFIG_IPV6)
	struct inet6_protocol	tcpv6_protocol;
	struct inet6_protocol	udpv6_protocol;
#endif
	struct net_offload	udpv6_offload;
#endif
	struct list_head	offload_base;
	struct list_head	ptype_all;
	struct kmem_cache	*skbuff_cache;
	struct kmem_cache	*skbuff_fclone_cache;
	struct kmem_cache	*skb_small_head_cache;
#ifdef CONFIG_RPS
	struct rps_sock_flow_table __rcu *rps_sock_flow_table;
	u32			rps_cpu_mask;
#endif
	int			gro_normal_batch;
	int			netdev_budget;
	int			netdev_budget_usecs;
	int			tstamp_prequeue;
	int			max_backlog;
	int			dev_tx_weight;
	int			dev_rx_weight;
	int			sysctl_max_skb_frags;
	int			sysctl_skb_defer_max;
	int			sysctl_mem_pcpu_rsv;
};

#define inet_ehash_secret	net_hotdata.tcp_protocol.secret
#define udp_ehash_secret	net_hotdata.udp_protocol.secret
#define inet6_ehash_secret	net_hotdata.tcpv6_protocol.secret
#define tcp_ipv6_hash_secret	net_hotdata.tcpv6_offload.secret
#define udp6_ehash_secret	net_hotdata.udpv6_protocol.secret
#define udp_ipv6_hash_secret	net_hotdata.udpv6_offload.secret

extern struct net_hotdata net_hotdata;

#endif /* _NET_HOTDATA_H */
