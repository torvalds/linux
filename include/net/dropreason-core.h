/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _LINUX_DROPREASON_CORE_H
#define _LINUX_DROPREASON_CORE_H

#define DEFINE_DROP_REASON(FN, FNe)	\
	FN(NOT_SPECIFIED)		\
	FN(NO_SOCKET)			\
	FN(PKT_TOO_SMALL)		\
	FN(TCP_CSUM)			\
	FN(SOCKET_FILTER)		\
	FN(UDP_CSUM)			\
	FN(NETFILTER_DROP)		\
	FN(OTHERHOST)			\
	FN(IP_CSUM)			\
	FN(IP_INHDR)			\
	FN(IP_RPFILTER)			\
	FN(UNICAST_IN_L2_MULTICAST)	\
	FN(XFRM_POLICY)			\
	FN(IP_NOPROTO)			\
	FN(SOCKET_RCVBUFF)		\
	FN(PROTO_MEM)			\
	FN(TCP_AUTH_HDR)		\
	FN(TCP_MD5NOTFOUND)		\
	FN(TCP_MD5UNEXPECTED)		\
	FN(TCP_MD5FAILURE)		\
	FN(TCP_AONOTFOUND)		\
	FN(TCP_AOUNEXPECTED)		\
	FN(TCP_AOKEYNOTFOUND)		\
	FN(TCP_AOFAILURE)		\
	FN(SOCKET_BACKLOG)		\
	FN(TCP_FLAGS)			\
	FN(TCP_ABORT_ON_DATA)		\
	FN(TCP_ZEROWINDOW)		\
	FN(TCP_OLD_DATA)		\
	FN(TCP_OVERWINDOW)		\
	FN(TCP_OFOMERGE)		\
	FN(TCP_RFC7323_PAWS)		\
	FN(TCP_OLD_SEQUENCE)		\
	FN(TCP_INVALID_SEQUENCE)	\
	FN(TCP_INVALID_ACK_SEQUENCE)	\
	FN(TCP_RESET)			\
	FN(TCP_INVALID_SYN)		\
	FN(TCP_CLOSE)			\
	FN(TCP_FASTOPEN)		\
	FN(TCP_OLD_ACK)			\
	FN(TCP_TOO_OLD_ACK)		\
	FN(TCP_ACK_UNSENT_DATA)		\
	FN(TCP_OFO_QUEUE_PRUNE)		\
	FN(TCP_OFO_DROP)		\
	FN(IP_OUTNOROUTES)		\
	FN(BPF_CGROUP_EGRESS)		\
	FN(IPV6DISABLED)		\
	FN(NEIGH_CREATEFAIL)		\
	FN(NEIGH_FAILED)		\
	FN(NEIGH_QUEUEFULL)		\
	FN(NEIGH_DEAD)			\
	FN(TC_EGRESS)			\
	FN(SECURITY_HOOK)		\
	FN(QDISC_DROP)			\
	FN(QDISC_OVERLIMIT)		\
	FN(QDISC_CONGESTED)		\
	FN(CAKE_FLOOD)			\
	FN(FQ_BAND_LIMIT)		\
	FN(FQ_HORIZON_LIMIT)		\
	FN(FQ_FLOW_LIMIT)		\
	FN(CPU_BACKLOG)			\
	FN(XDP)				\
	FN(TC_INGRESS)			\
	FN(UNHANDLED_PROTO)		\
	FN(SKB_CSUM)			\
	FN(SKB_GSO_SEG)			\
	FN(SKB_UCOPY_FAULT)		\
	FN(DEV_HDR)			\
	FN(DEV_READY)			\
	FN(FULL_RING)			\
	FN(NOMEM)			\
	FN(HDR_TRUNC)			\
	FN(TAP_FILTER)			\
	FN(TAP_TXFILTER)		\
	FN(ICMP_CSUM)			\
	FN(INVALID_PROTO)		\
	FN(IP_INADDRERRORS)		\
	FN(IP_INNOROUTES)		\
	FN(IP_LOCAL_SOURCE)		\
	FN(IP_INVALID_SOURCE)		\
	FN(IP_LOCALNET)			\
	FN(IP_INVALID_DEST)		\
	FN(PKT_TOO_BIG)			\
	FN(DUP_FRAG)			\
	FN(FRAG_REASM_TIMEOUT)		\
	FN(FRAG_TOO_FAR)		\
	FN(TCP_MINTTL)			\
	FN(IPV6_BAD_EXTHDR)		\
	FN(IPV6_NDISC_FRAG)		\
	FN(IPV6_NDISC_HOP_LIMIT)	\
	FN(IPV6_NDISC_BAD_CODE)		\
	FN(IPV6_NDISC_BAD_OPTIONS)	\
	FN(IPV6_NDISC_NS_OTHERHOST)	\
	FN(QUEUE_PURGE)			\
	FN(TC_COOKIE_ERROR)		\
	FN(PACKET_SOCK_ERROR)		\
	FN(TC_CHAIN_NOTFOUND)		\
	FN(TC_RECLASSIFY_LOOP)		\
	FN(VXLAN_INVALID_HDR)		\
	FN(VXLAN_VNI_NOT_FOUND)		\
	FN(MAC_INVALID_SOURCE)		\
	FN(VXLAN_ENTRY_EXISTS)		\
	FN(VXLAN_NO_REMOTE)		\
	FN(IP_TUNNEL_ECN)		\
	FN(TUNNEL_TXINFO)		\
	FN(LOCAL_MAC)			\
	FN(ARP_PVLAN_DISABLE)		\
	FNe(MAX)

/**
 * enum skb_drop_reason - the reasons of skb drops
 *
 * The reason of skb drop, which is used in kfree_skb_reason().
 */
enum skb_drop_reason {
	/**
	 * @SKB_NOT_DROPPED_YET: skb is not dropped yet (used for no-drop case)
	 */
	SKB_NOT_DROPPED_YET = 0,
	/** @SKB_CONSUMED: packet has been consumed */
	SKB_CONSUMED,
	/** @SKB_DROP_REASON_NOT_SPECIFIED: drop reason is not specified */
	SKB_DROP_REASON_NOT_SPECIFIED,
	/**
	 * @SKB_DROP_REASON_NO_SOCKET: no valid socket that can be used.
	 * Reason could be one of three cases:
	 * 1) no established/listening socket found during lookup process
	 * 2) no valid request socket during 3WHS process
	 * 3) no valid child socket during 3WHS process
	 */
	SKB_DROP_REASON_NO_SOCKET,
	/** @SKB_DROP_REASON_PKT_TOO_SMALL: packet size is too small */
	SKB_DROP_REASON_PKT_TOO_SMALL,
	/** @SKB_DROP_REASON_TCP_CSUM: TCP checksum error */
	SKB_DROP_REASON_TCP_CSUM,
	/** @SKB_DROP_REASON_SOCKET_FILTER: dropped by socket filter */
	SKB_DROP_REASON_SOCKET_FILTER,
	/** @SKB_DROP_REASON_UDP_CSUM: UDP checksum error */
	SKB_DROP_REASON_UDP_CSUM,
	/** @SKB_DROP_REASON_NETFILTER_DROP: dropped by netfilter */
	SKB_DROP_REASON_NETFILTER_DROP,
	/**
	 * @SKB_DROP_REASON_OTHERHOST: packet don't belong to current host
	 * (interface is in promisc mode)
	 */
	SKB_DROP_REASON_OTHERHOST,
	/** @SKB_DROP_REASON_IP_CSUM: IP checksum error */
	SKB_DROP_REASON_IP_CSUM,
	/**
	 * @SKB_DROP_REASON_IP_INHDR: there is something wrong with IP header (see
	 * IPSTATS_MIB_INHDRERRORS)
	 */
	SKB_DROP_REASON_IP_INHDR,
	/**
	 * @SKB_DROP_REASON_IP_RPFILTER: IP rpfilter validate failed. see the
	 * document for rp_filter in ip-sysctl.rst for more information
	 */
	SKB_DROP_REASON_IP_RPFILTER,
	/**
	 * @SKB_DROP_REASON_UNICAST_IN_L2_MULTICAST: destination address of L2 is
	 * multicast, but L3 is unicast.
	 */
	SKB_DROP_REASON_UNICAST_IN_L2_MULTICAST,
	/** @SKB_DROP_REASON_XFRM_POLICY: xfrm policy check failed */
	SKB_DROP_REASON_XFRM_POLICY,
	/** @SKB_DROP_REASON_IP_NOPROTO: no support for IP protocol */
	SKB_DROP_REASON_IP_NOPROTO,
	/** @SKB_DROP_REASON_SOCKET_RCVBUFF: socket receive buff is full */
	SKB_DROP_REASON_SOCKET_RCVBUFF,
	/**
	 * @SKB_DROP_REASON_PROTO_MEM: proto memory limitation, such as
	 * udp packet drop out of udp_memory_allocated.
	 */
	SKB_DROP_REASON_PROTO_MEM,
	/**
	 * @SKB_DROP_REASON_TCP_AUTH_HDR: TCP-MD5 or TCP-AO hashes are met
	 * twice or set incorrectly.
	 */
	SKB_DROP_REASON_TCP_AUTH_HDR,
	/**
	 * @SKB_DROP_REASON_TCP_MD5NOTFOUND: no MD5 hash and one expected,
	 * corresponding to LINUX_MIB_TCPMD5NOTFOUND
	 */
	SKB_DROP_REASON_TCP_MD5NOTFOUND,
	/**
	 * @SKB_DROP_REASON_TCP_MD5UNEXPECTED: MD5 hash and we're not expecting
	 * one, corresponding to LINUX_MIB_TCPMD5UNEXPECTED
	 */
	SKB_DROP_REASON_TCP_MD5UNEXPECTED,
	/**
	 * @SKB_DROP_REASON_TCP_MD5FAILURE: MD5 hash and its wrong, corresponding
	 * to LINUX_MIB_TCPMD5FAILURE
	 */
	SKB_DROP_REASON_TCP_MD5FAILURE,
	/**
	 * @SKB_DROP_REASON_TCP_AONOTFOUND: no TCP-AO hash and one was expected,
	 * corresponding to LINUX_MIB_TCPAOREQUIRED
	 */
	SKB_DROP_REASON_TCP_AONOTFOUND,
	/**
	 * @SKB_DROP_REASON_TCP_AOUNEXPECTED: TCP-AO hash is present and it
	 * was not expected, corresponding to LINUX_MIB_TCPAOKEYNOTFOUND
	 */
	SKB_DROP_REASON_TCP_AOUNEXPECTED,
	/**
	 * @SKB_DROP_REASON_TCP_AOKEYNOTFOUND: TCP-AO key is unknown,
	 * corresponding to LINUX_MIB_TCPAOKEYNOTFOUND
	 */
	SKB_DROP_REASON_TCP_AOKEYNOTFOUND,
	/**
	 * @SKB_DROP_REASON_TCP_AOFAILURE: TCP-AO hash is wrong,
	 * corresponding to LINUX_MIB_TCPAOBAD
	 */
	SKB_DROP_REASON_TCP_AOFAILURE,
	/**
	 * @SKB_DROP_REASON_SOCKET_BACKLOG: failed to add skb to socket backlog (
	 * see LINUX_MIB_TCPBACKLOGDROP)
	 */
	SKB_DROP_REASON_SOCKET_BACKLOG,
	/** @SKB_DROP_REASON_TCP_FLAGS: TCP flags invalid */
	SKB_DROP_REASON_TCP_FLAGS,
	/**
	 * @SKB_DROP_REASON_TCP_ABORT_ON_DATA: abort on data, corresponding to
	 * LINUX_MIB_TCPABORTONDATA
	 */
	SKB_DROP_REASON_TCP_ABORT_ON_DATA,
	/**
	 * @SKB_DROP_REASON_TCP_ZEROWINDOW: TCP receive window size is zero,
	 * see LINUX_MIB_TCPZEROWINDOWDROP
	 */
	SKB_DROP_REASON_TCP_ZEROWINDOW,
	/**
	 * @SKB_DROP_REASON_TCP_OLD_DATA: the TCP data received is already
	 * received before (spurious retrans may happened), see
	 * LINUX_MIB_DELAYEDACKLOST
	 */
	SKB_DROP_REASON_TCP_OLD_DATA,
	/**
	 * @SKB_DROP_REASON_TCP_OVERWINDOW: the TCP data is out of window,
	 * the seq of the first byte exceed the right edges of receive
	 * window
	 */
	SKB_DROP_REASON_TCP_OVERWINDOW,
	/**
	 * @SKB_DROP_REASON_TCP_OFOMERGE: the data of skb is already in the ofo
	 * queue, corresponding to LINUX_MIB_TCPOFOMERGE
	 */
	SKB_DROP_REASON_TCP_OFOMERGE,
	/**
	 * @SKB_DROP_REASON_TCP_RFC7323_PAWS: PAWS check, corresponding to
	 * LINUX_MIB_PAWSESTABREJECTED, LINUX_MIB_PAWSACTIVEREJECTED
	 */
	SKB_DROP_REASON_TCP_RFC7323_PAWS,
	/** @SKB_DROP_REASON_TCP_OLD_SEQUENCE: Old SEQ field (duplicate packet) */
	SKB_DROP_REASON_TCP_OLD_SEQUENCE,
	/** @SKB_DROP_REASON_TCP_INVALID_SEQUENCE: Not acceptable SEQ field */
	SKB_DROP_REASON_TCP_INVALID_SEQUENCE,
	/**
	 * @SKB_DROP_REASON_TCP_INVALID_ACK_SEQUENCE: Not acceptable ACK SEQ
	 * field because ack sequence is not in the window between snd_una
	 * and snd_nxt
	 */
	SKB_DROP_REASON_TCP_INVALID_ACK_SEQUENCE,
	/** @SKB_DROP_REASON_TCP_RESET: Invalid RST packet */
	SKB_DROP_REASON_TCP_RESET,
	/**
	 * @SKB_DROP_REASON_TCP_INVALID_SYN: Incoming packet has unexpected
	 * SYN flag
	 */
	SKB_DROP_REASON_TCP_INVALID_SYN,
	/** @SKB_DROP_REASON_TCP_CLOSE: TCP socket in CLOSE state */
	SKB_DROP_REASON_TCP_CLOSE,
	/** @SKB_DROP_REASON_TCP_FASTOPEN: dropped by FASTOPEN request socket */
	SKB_DROP_REASON_TCP_FASTOPEN,
	/** @SKB_DROP_REASON_TCP_OLD_ACK: TCP ACK is old, but in window */
	SKB_DROP_REASON_TCP_OLD_ACK,
	/** @SKB_DROP_REASON_TCP_TOO_OLD_ACK: TCP ACK is too old */
	SKB_DROP_REASON_TCP_TOO_OLD_ACK,
	/**
	 * @SKB_DROP_REASON_TCP_ACK_UNSENT_DATA: TCP ACK for data we haven't
	 * sent yet
	 */
	SKB_DROP_REASON_TCP_ACK_UNSENT_DATA,
	/** @SKB_DROP_REASON_TCP_OFO_QUEUE_PRUNE: pruned from TCP OFO queue */
	SKB_DROP_REASON_TCP_OFO_QUEUE_PRUNE,
	/** @SKB_DROP_REASON_TCP_OFO_DROP: data already in receive queue */
	SKB_DROP_REASON_TCP_OFO_DROP,
	/** @SKB_DROP_REASON_IP_OUTNOROUTES: route lookup failed */
	SKB_DROP_REASON_IP_OUTNOROUTES,
	/**
	 * @SKB_DROP_REASON_BPF_CGROUP_EGRESS: dropped by BPF_PROG_TYPE_CGROUP_SKB
	 * eBPF program
	 */
	SKB_DROP_REASON_BPF_CGROUP_EGRESS,
	/** @SKB_DROP_REASON_IPV6DISABLED: IPv6 is disabled on the device */
	SKB_DROP_REASON_IPV6DISABLED,
	/** @SKB_DROP_REASON_NEIGH_CREATEFAIL: failed to create neigh entry */
	SKB_DROP_REASON_NEIGH_CREATEFAIL,
	/** @SKB_DROP_REASON_NEIGH_FAILED: neigh entry in failed state */
	SKB_DROP_REASON_NEIGH_FAILED,
	/** @SKB_DROP_REASON_NEIGH_QUEUEFULL: arp_queue for neigh entry is full */
	SKB_DROP_REASON_NEIGH_QUEUEFULL,
	/** @SKB_DROP_REASON_NEIGH_DEAD: neigh entry is dead */
	SKB_DROP_REASON_NEIGH_DEAD,
	/** @SKB_DROP_REASON_TC_EGRESS: dropped in TC egress HOOK */
	SKB_DROP_REASON_TC_EGRESS,
	/** @SKB_DROP_REASON_SECURITY_HOOK: dropped due to security HOOK */
	SKB_DROP_REASON_SECURITY_HOOK,
	/**
	 * @SKB_DROP_REASON_QDISC_DROP: dropped by qdisc when packet outputting (
	 * failed to enqueue to current qdisc)
	 */
	SKB_DROP_REASON_QDISC_DROP,
	/**
	 * @SKB_DROP_REASON_QDISC_OVERLIMIT: dropped by qdisc when a qdisc
	 * instance exceeds its total buffer size limit.
	 */
	SKB_DROP_REASON_QDISC_OVERLIMIT,
	/**
	 * @SKB_DROP_REASON_QDISC_CONGESTED: dropped by a qdisc AQM algorithm
	 * due to congestion.
	 */
	SKB_DROP_REASON_QDISC_CONGESTED,
	/**
	 * @SKB_DROP_REASON_CAKE_FLOOD: dropped by the flood protection part of
	 * CAKE qdisc AQM algorithm (BLUE).
	 */
	SKB_DROP_REASON_CAKE_FLOOD,
	/**
	 * @SKB_DROP_REASON_FQ_BAND_LIMIT: dropped by fq qdisc when per band
	 * limit is reached.
	 */
	SKB_DROP_REASON_FQ_BAND_LIMIT,
	/**
	 * @SKB_DROP_REASON_FQ_HORIZON_LIMIT: dropped by fq qdisc when packet
	 * timestamp is too far in the future.
	 */
	SKB_DROP_REASON_FQ_HORIZON_LIMIT,
	/**
	 * @SKB_DROP_REASON_FQ_FLOW_LIMIT: dropped by fq qdisc when a flow
	 * exceeds its limits.
	 */
	SKB_DROP_REASON_FQ_FLOW_LIMIT,
	/**
	 * @SKB_DROP_REASON_CPU_BACKLOG: failed to enqueue the skb to the per CPU
	 * backlog queue. This can be caused by backlog queue full (see
	 * netdev_max_backlog in net.rst) or RPS flow limit
	 */
	SKB_DROP_REASON_CPU_BACKLOG,
	/** @SKB_DROP_REASON_XDP: dropped by XDP in input path */
	SKB_DROP_REASON_XDP,
	/** @SKB_DROP_REASON_TC_INGRESS: dropped in TC ingress HOOK */
	SKB_DROP_REASON_TC_INGRESS,
	/** @SKB_DROP_REASON_UNHANDLED_PROTO: protocol not implemented or not supported */
	SKB_DROP_REASON_UNHANDLED_PROTO,
	/** @SKB_DROP_REASON_SKB_CSUM: sk_buff checksum computation error */
	SKB_DROP_REASON_SKB_CSUM,
	/** @SKB_DROP_REASON_SKB_GSO_SEG: gso segmentation error */
	SKB_DROP_REASON_SKB_GSO_SEG,
	/**
	 * @SKB_DROP_REASON_SKB_UCOPY_FAULT: failed to copy data from user space,
	 * e.g., via zerocopy_sg_from_iter() or skb_orphan_frags_rx()
	 */
	SKB_DROP_REASON_SKB_UCOPY_FAULT,
	/** @SKB_DROP_REASON_DEV_HDR: device driver specific header/metadata is invalid */
	SKB_DROP_REASON_DEV_HDR,
	/**
	 * @SKB_DROP_REASON_DEV_READY: the device is not ready to xmit/recv due to
	 * any of its data structure that is not up/ready/initialized,
	 * e.g., the IFF_UP is not set, or driver specific tun->tfiles[txq]
	 * is not initialized
	 */
	SKB_DROP_REASON_DEV_READY,
	/** @SKB_DROP_REASON_FULL_RING: ring buffer is full */
	SKB_DROP_REASON_FULL_RING,
	/** @SKB_DROP_REASON_NOMEM: error due to OOM */
	SKB_DROP_REASON_NOMEM,
	/**
	 * @SKB_DROP_REASON_HDR_TRUNC: failed to trunc/extract the header from
	 * networking data, e.g., failed to pull the protocol header from
	 * frags via pskb_may_pull()
	 */
	SKB_DROP_REASON_HDR_TRUNC,
	/**
	 * @SKB_DROP_REASON_TAP_FILTER: dropped by (ebpf) filter directly attached
	 * to tun/tap, e.g., via TUNSETFILTEREBPF
	 */
	SKB_DROP_REASON_TAP_FILTER,
	/**
	 * @SKB_DROP_REASON_TAP_TXFILTER: dropped by tx filter implemented at
	 * tun/tap, e.g., check_filter()
	 */
	SKB_DROP_REASON_TAP_TXFILTER,
	/** @SKB_DROP_REASON_ICMP_CSUM: ICMP checksum error */
	SKB_DROP_REASON_ICMP_CSUM,
	/**
	 * @SKB_DROP_REASON_INVALID_PROTO: the packet doesn't follow RFC 2211,
	 * such as a broadcasts ICMP_TIMESTAMP
	 */
	SKB_DROP_REASON_INVALID_PROTO,
	/**
	 * @SKB_DROP_REASON_IP_INADDRERRORS: host unreachable, corresponding to
	 * IPSTATS_MIB_INADDRERRORS
	 */
	SKB_DROP_REASON_IP_INADDRERRORS,
	/**
	 * @SKB_DROP_REASON_IP_INNOROUTES: network unreachable, corresponding to
	 * IPSTATS_MIB_INADDRERRORS
	 */
	SKB_DROP_REASON_IP_INNOROUTES,
	/** @SKB_DROP_REASON_IP_LOCAL_SOURCE: the source ip is local */
	SKB_DROP_REASON_IP_LOCAL_SOURCE,
	/**
	 * @SKB_DROP_REASON_IP_INVALID_SOURCE: the source ip is invalid:
	 * 1) source ip is multicast or limited broadcast
	 * 2) source ip is zero and not IGMP
	 */
	SKB_DROP_REASON_IP_INVALID_SOURCE,
	/** @SKB_DROP_REASON_IP_LOCALNET: source or dest ip is local net */
	SKB_DROP_REASON_IP_LOCALNET,
	/**
	 * @SKB_DROP_REASON_IP_INVALID_DEST: the dest ip is invalid:
	 * 1) dest ip is 0
	 */
	SKB_DROP_REASON_IP_INVALID_DEST,
	/**
	 * @SKB_DROP_REASON_PKT_TOO_BIG: packet size is too big (maybe exceed the
	 * MTU)
	 */
	SKB_DROP_REASON_PKT_TOO_BIG,
	/** @SKB_DROP_REASON_DUP_FRAG: duplicate fragment */
	SKB_DROP_REASON_DUP_FRAG,
	/** @SKB_DROP_REASON_FRAG_REASM_TIMEOUT: fragment reassembly timeout */
	SKB_DROP_REASON_FRAG_REASM_TIMEOUT,
	/**
	 * @SKB_DROP_REASON_FRAG_TOO_FAR: ipv4 fragment too far.
	 * (/proc/sys/net/ipv4/ipfrag_max_dist)
	 */
	SKB_DROP_REASON_FRAG_TOO_FAR,
	/**
	 * @SKB_DROP_REASON_TCP_MINTTL: ipv4 ttl or ipv6 hoplimit below
	 * the threshold (IP_MINTTL or IPV6_MINHOPCOUNT).
	 */
	SKB_DROP_REASON_TCP_MINTTL,
	/** @SKB_DROP_REASON_IPV6_BAD_EXTHDR: Bad IPv6 extension header. */
	SKB_DROP_REASON_IPV6_BAD_EXTHDR,
	/** @SKB_DROP_REASON_IPV6_NDISC_FRAG: invalid frag (suppress_frag_ndisc). */
	SKB_DROP_REASON_IPV6_NDISC_FRAG,
	/** @SKB_DROP_REASON_IPV6_NDISC_HOP_LIMIT: invalid hop limit. */
	SKB_DROP_REASON_IPV6_NDISC_HOP_LIMIT,
	/** @SKB_DROP_REASON_IPV6_NDISC_BAD_CODE: invalid NDISC icmp6 code. */
	SKB_DROP_REASON_IPV6_NDISC_BAD_CODE,
	/** @SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS: invalid NDISC options. */
	SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS,
	/**
	 * @SKB_DROP_REASON_IPV6_NDISC_NS_OTHERHOST: NEIGHBOUR SOLICITATION
	 * for another host.
	 */
	SKB_DROP_REASON_IPV6_NDISC_NS_OTHERHOST,
	/** @SKB_DROP_REASON_QUEUE_PURGE: bulk free. */
	SKB_DROP_REASON_QUEUE_PURGE,
	/**
	 * @SKB_DROP_REASON_TC_COOKIE_ERROR: An error occurred whilst
	 * processing a tc ext cookie.
	 */
	SKB_DROP_REASON_TC_COOKIE_ERROR,
	/**
	 * @SKB_DROP_REASON_PACKET_SOCK_ERROR: generic packet socket errors
	 * after its filter matches an incoming packet.
	 */
	SKB_DROP_REASON_PACKET_SOCK_ERROR,
	/** @SKB_DROP_REASON_TC_CHAIN_NOTFOUND: tc chain lookup failed. */
	SKB_DROP_REASON_TC_CHAIN_NOTFOUND,
	/**
	 * @SKB_DROP_REASON_TC_RECLASSIFY_LOOP: tc exceeded max reclassify loop
	 * iterations.
	 */
	SKB_DROP_REASON_TC_RECLASSIFY_LOOP,
	/**
	 * @SKB_DROP_REASON_VXLAN_INVALID_HDR: VXLAN header is invalid. E.g.:
	 * 1) reserved fields are not zero
	 * 2) "I" flag is not set
	 */
	SKB_DROP_REASON_VXLAN_INVALID_HDR,
	/** @SKB_DROP_REASON_VXLAN_VNI_NOT_FOUND: no VXLAN device found for VNI */
	SKB_DROP_REASON_VXLAN_VNI_NOT_FOUND,
	/** @SKB_DROP_REASON_MAC_INVALID_SOURCE: source mac is invalid */
	SKB_DROP_REASON_MAC_INVALID_SOURCE,
	/**
	 * @SKB_DROP_REASON_VXLAN_ENTRY_EXISTS: trying to migrate a static
	 * entry or an entry pointing to a nexthop.
	 */
	SKB_DROP_REASON_VXLAN_ENTRY_EXISTS,
	/** @SKB_DROP_REASON_VXLAN_NO_REMOTE: no remote found for xmit */
	SKB_DROP_REASON_VXLAN_NO_REMOTE,
	/**
	 * @SKB_DROP_REASON_IP_TUNNEL_ECN: skb is dropped according to
	 * RFC 6040 4.2, see __INET_ECN_decapsulate() for detail.
	 */
	SKB_DROP_REASON_IP_TUNNEL_ECN,
	/**
	 * @SKB_DROP_REASON_TUNNEL_TXINFO: packet without necessary metadata
	 * reached a device which is in "external" mode.
	 */
	SKB_DROP_REASON_TUNNEL_TXINFO,
	/**
	 * @SKB_DROP_REASON_LOCAL_MAC: the source MAC address is equal to
	 * the MAC address of the local netdev.
	 */
	SKB_DROP_REASON_LOCAL_MAC,
	/**
	 * @SKB_DROP_REASON_ARP_PVLAN_DISABLE: packet which is not IP is
	 * forwarded to the in_dev, and the proxy_arp_pvlan is not
	 * enabled.
	 */
	SKB_DROP_REASON_ARP_PVLAN_DISABLE,
	/**
	 * @SKB_DROP_REASON_MAX: the maximum of core drop reasons, which
	 * shouldn't be used as a real 'reason' - only for tracing code gen
	 */
	SKB_DROP_REASON_MAX,

	/**
	 * @SKB_DROP_REASON_SUBSYS_MASK: subsystem mask in drop reasons,
	 * see &enum skb_drop_reason_subsys
	 */
	SKB_DROP_REASON_SUBSYS_MASK = 0xffff0000,
};

#define SKB_DROP_REASON_SUBSYS_SHIFT	16

#define SKB_DR_INIT(name, reason)				\
	enum skb_drop_reason name = SKB_DROP_REASON_##reason
#define SKB_DR(name)						\
	SKB_DR_INIT(name, NOT_SPECIFIED)
#define SKB_DR_SET(name, reason)				\
	(name = SKB_DROP_REASON_##reason)
#define SKB_DR_OR(name, reason)					\
	do {							\
		if (name == SKB_DROP_REASON_NOT_SPECIFIED ||	\
		    name == SKB_NOT_DROPPED_YET)		\
			SKB_DR_SET(name, reason);		\
	} while (0)

#endif
