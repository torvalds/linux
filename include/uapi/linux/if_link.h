/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_IF_LINK_H
#define _UAPI_LINUX_IF_LINK_H

#include <linux/types.h>
#include <linux/netlink.h>

/* This struct should be in sync with struct rtnl_link_stats64 */
struct rtnl_link_stats {
	__u32	rx_packets;
	__u32	tx_packets;
	__u32	rx_bytes;
	__u32	tx_bytes;
	__u32	rx_errors;
	__u32	tx_errors;
	__u32	rx_dropped;
	__u32	tx_dropped;
	__u32	multicast;
	__u32	collisions;
	/* detailed rx_errors: */
	__u32	rx_length_errors;
	__u32	rx_over_errors;
	__u32	rx_crc_errors;
	__u32	rx_frame_errors;
	__u32	rx_fifo_errors;
	__u32	rx_missed_errors;

	/* detailed tx_errors */
	__u32	tx_aborted_errors;
	__u32	tx_carrier_errors;
	__u32	tx_fifo_errors;
	__u32	tx_heartbeat_errors;
	__u32	tx_window_errors;

	/* for cslip etc */
	__u32	rx_compressed;
	__u32	tx_compressed;

	__u32	rx_nohandler;
};

/**
 * struct rtnl_link_stats64 - The main device statistics structure.
 *
 * @rx_packets: Number of good packets received by the interface.
 *   For hardware interfaces counts all good packets received from the device
 *   by the host, including packets which host had to drop at various stages
 *   of processing (even in the driver).
 *
 * @tx_packets: Number of packets successfully transmitted.
 *   For hardware interfaces counts packets which host was able to successfully
 *   hand over to the device, which does not necessarily mean that packets
 *   had been successfully transmitted out of the device, only that device
 *   acknowledged it copied them out of host memory.
 *
 * @rx_bytes: Number of good received bytes, corresponding to @rx_packets.
 *
 *   For IEEE 802.3 devices should count the length of Ethernet Frames
 *   excluding the FCS.
 *
 * @tx_bytes: Number of good transmitted bytes, corresponding to @tx_packets.
 *
 *   For IEEE 802.3 devices should count the length of Ethernet Frames
 *   excluding the FCS.
 *
 * @rx_errors: Total number of bad packets received on this network device.
 *   This counter must include events counted by @rx_length_errors,
 *   @rx_crc_errors, @rx_frame_errors and other errors not otherwise
 *   counted.
 *
 * @tx_errors: Total number of transmit problems.
 *   This counter must include events counter by @tx_aborted_errors,
 *   @tx_carrier_errors, @tx_fifo_errors, @tx_heartbeat_errors,
 *   @tx_window_errors and other errors not otherwise counted.
 *
 * @rx_dropped: Number of packets received but not processed,
 *   e.g. due to lack of resources or unsupported protocol.
 *   For hardware interfaces this counter may include packets discarded
 *   due to L2 address filtering but should not include packets dropped
 *   by the device due to buffer exhaustion which are counted separately in
 *   @rx_missed_errors (since procfs folds those two counters together).
 *
 * @tx_dropped: Number of packets dropped on their way to transmission,
 *   e.g. due to lack of resources.
 *
 * @multicast: Multicast packets received.
 *   For hardware interfaces this statistic is commonly calculated
 *   at the device level (unlike @rx_packets) and therefore may include
 *   packets which did not reach the host.
 *
 *   For IEEE 802.3 devices this counter may be equivalent to:
 *
 *    - 30.3.1.1.21 aMulticastFramesReceivedOK
 *
 * @collisions: Number of collisions during packet transmissions.
 *
 * @rx_length_errors: Number of packets dropped due to invalid length.
 *   Part of aggregate "frame" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices this counter should be equivalent to a sum
 *   of the following attributes:
 *
 *    - 30.3.1.1.23 aInRangeLengthErrors
 *    - 30.3.1.1.24 aOutOfRangeLengthField
 *    - 30.3.1.1.25 aFrameTooLongErrors
 *
 * @rx_over_errors: Receiver FIFO overflow event counter.
 *
 *   Historically the count of overflow events. Such events may be
 *   reported in the receive descriptors or via interrupts, and may
 *   not correspond one-to-one with dropped packets.
 *
 *   The recommended interpretation for high speed interfaces is -
 *   number of packets dropped because they did not fit into buffers
 *   provided by the host, e.g. packets larger than MTU or next buffer
 *   in the ring was not available for a scatter transfer.
 *
 *   Part of aggregate "frame" errors in `/proc/net/dev`.
 *
 *   This statistics was historically used interchangeably with
 *   @rx_fifo_errors.
 *
 *   This statistic corresponds to hardware events and is not commonly used
 *   on software devices.
 *
 * @rx_crc_errors: Number of packets received with a CRC error.
 *   Part of aggregate "frame" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices this counter must be equivalent to:
 *
 *    - 30.3.1.1.6 aFrameCheckSequenceErrors
 *
 * @rx_frame_errors: Receiver frame alignment errors.
 *   Part of aggregate "frame" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices this counter should be equivalent to:
 *
 *    - 30.3.1.1.7 aAlignmentErrors
 *
 * @rx_fifo_errors: Receiver FIFO error counter.
 *
 *   Historically the count of overflow events. Those events may be
 *   reported in the receive descriptors or via interrupts, and may
 *   not correspond one-to-one with dropped packets.
 *
 *   This statistics was used interchangeably with @rx_over_errors.
 *   Not recommended for use in drivers for high speed interfaces.
 *
 *   This statistic is used on software devices, e.g. to count software
 *   packet queue overflow (can) or sequencing errors (GRE).
 *
 * @rx_missed_errors: Count of packets missed by the host.
 *   Folded into the "drop" counter in `/proc/net/dev`.
 *
 *   Counts number of packets dropped by the device due to lack
 *   of buffer space. This usually indicates that the host interface
 *   is slower than the network interface, or host is not keeping up
 *   with the receive packet rate.
 *
 *   This statistic corresponds to hardware events and is not used
 *   on software devices.
 *
 * @tx_aborted_errors:
 *   Part of aggregate "carrier" errors in `/proc/net/dev`.
 *   For IEEE 802.3 devices capable of half-duplex operation this counter
 *   must be equivalent to:
 *
 *    - 30.3.1.1.11 aFramesAbortedDueToXSColls
 *
 *   High speed interfaces may use this counter as a general device
 *   discard counter.
 *
 * @tx_carrier_errors: Number of frame transmission errors due to loss
 *   of carrier during transmission.
 *   Part of aggregate "carrier" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices this counter must be equivalent to:
 *
 *    - 30.3.1.1.13 aCarrierSenseErrors
 *
 * @tx_fifo_errors: Number of frame transmission errors due to device
 *   FIFO underrun / underflow. This condition occurs when the device
 *   begins transmission of a frame but is unable to deliver the
 *   entire frame to the transmitter in time for transmission.
 *   Part of aggregate "carrier" errors in `/proc/net/dev`.
 *
 * @tx_heartbeat_errors: Number of Heartbeat / SQE Test errors for
 *   old half-duplex Ethernet.
 *   Part of aggregate "carrier" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices possibly equivalent to:
 *
 *    - 30.3.2.1.4 aSQETestErrors
 *
 * @tx_window_errors: Number of frame transmission errors due
 *   to late collisions (for Ethernet - after the first 64B of transmission).
 *   Part of aggregate "carrier" errors in `/proc/net/dev`.
 *
 *   For IEEE 802.3 devices this counter must be equivalent to:
 *
 *    - 30.3.1.1.10 aLateCollisions
 *
 * @rx_compressed: Number of correctly received compressed packets.
 *   This counters is only meaningful for interfaces which support
 *   packet compression (e.g. CSLIP, PPP).
 *
 * @tx_compressed: Number of transmitted compressed packets.
 *   This counters is only meaningful for interfaces which support
 *   packet compression (e.g. CSLIP, PPP).
 *
 * @rx_nohandler: Number of packets received on the interface
 *   but dropped by the networking stack because the device is
 *   not designated to receive packets (e.g. backup link in a bond).
 *
 * @rx_otherhost_dropped: Number of packets dropped due to mismatch
 *   in destination MAC address.
 */
struct rtnl_link_stats64 {
	__u64	rx_packets;
	__u64	tx_packets;
	__u64	rx_bytes;
	__u64	tx_bytes;
	__u64	rx_errors;
	__u64	tx_errors;
	__u64	rx_dropped;
	__u64	tx_dropped;
	__u64	multicast;
	__u64	collisions;

	/* detailed rx_errors: */
	__u64	rx_length_errors;
	__u64	rx_over_errors;
	__u64	rx_crc_errors;
	__u64	rx_frame_errors;
	__u64	rx_fifo_errors;
	__u64	rx_missed_errors;

	/* detailed tx_errors */
	__u64	tx_aborted_errors;
	__u64	tx_carrier_errors;
	__u64	tx_fifo_errors;
	__u64	tx_heartbeat_errors;
	__u64	tx_window_errors;

	/* for cslip etc */
	__u64	rx_compressed;
	__u64	tx_compressed;
	__u64	rx_nohandler;

	__u64	rx_otherhost_dropped;
};

/* Subset of link stats useful for in-HW collection. Meaning of the fields is as
 * for struct rtnl_link_stats64.
 */
struct rtnl_hw_stats64 {
	__u64	rx_packets;
	__u64	tx_packets;
	__u64	rx_bytes;
	__u64	tx_bytes;
	__u64	rx_errors;
	__u64	tx_errors;
	__u64	rx_dropped;
	__u64	tx_dropped;
	__u64	multicast;
};

/* The struct should be in sync with struct ifmap */
struct rtnl_link_ifmap {
	__u64	mem_start;
	__u64	mem_end;
	__u64	base_addr;
	__u16	irq;
	__u8	dma;
	__u8	port;
};

/*
 * IFLA_AF_SPEC
 *   Contains nested attributes for address family specific attributes.
 *   Each address family may create a attribute with the address family
 *   number as type and create its own attribute structure in it.
 *
 *   Example:
 *   [IFLA_AF_SPEC] = {
 *       [AF_INET] = {
 *           [IFLA_INET_CONF] = ...,
 *       },
 *       [AF_INET6] = {
 *           [IFLA_INET6_FLAGS] = ...,
 *           [IFLA_INET6_CONF] = ...,
 *       }
 *   }
 */

enum {
	IFLA_UNSPEC,
	IFLA_ADDRESS,
	IFLA_BROADCAST,
	IFLA_IFNAME,
	IFLA_MTU,
	IFLA_LINK,
	IFLA_QDISC,
	IFLA_STATS,
	IFLA_COST,
#define IFLA_COST IFLA_COST
	IFLA_PRIORITY,
#define IFLA_PRIORITY IFLA_PRIORITY
	IFLA_MASTER,
#define IFLA_MASTER IFLA_MASTER
	IFLA_WIRELESS,		/* Wireless Extension event - see wireless.h */
#define IFLA_WIRELESS IFLA_WIRELESS
	IFLA_PROTINFO,		/* Protocol specific information for a link */
#define IFLA_PROTINFO IFLA_PROTINFO
	IFLA_TXQLEN,
#define IFLA_TXQLEN IFLA_TXQLEN
	IFLA_MAP,
#define IFLA_MAP IFLA_MAP
	IFLA_WEIGHT,
#define IFLA_WEIGHT IFLA_WEIGHT
	IFLA_OPERSTATE,
	IFLA_LINKMODE,
	IFLA_LINKINFO,
#define IFLA_LINKINFO IFLA_LINKINFO
	IFLA_NET_NS_PID,
	IFLA_IFALIAS,
	IFLA_NUM_VF,		/* Number of VFs if device is SR-IOV PF */
	IFLA_VFINFO_LIST,
	IFLA_STATS64,
	IFLA_VF_PORTS,
	IFLA_PORT_SELF,
	IFLA_AF_SPEC,
	IFLA_GROUP,		/* Group the device belongs to */
	IFLA_NET_NS_FD,
	IFLA_EXT_MASK,		/* Extended info mask, VFs, etc */
	IFLA_PROMISCUITY,	/* Promiscuity count: > 0 means acts PROMISC */
#define IFLA_PROMISCUITY IFLA_PROMISCUITY
	IFLA_NUM_TX_QUEUES,
	IFLA_NUM_RX_QUEUES,
	IFLA_CARRIER,
	IFLA_PHYS_PORT_ID,
	IFLA_CARRIER_CHANGES,
	IFLA_PHYS_SWITCH_ID,
	IFLA_LINK_NETNSID,
	IFLA_PHYS_PORT_NAME,
	IFLA_PROTO_DOWN,
	IFLA_GSO_MAX_SEGS,
	IFLA_GSO_MAX_SIZE,
	IFLA_PAD,
	IFLA_XDP,
	IFLA_EVENT,
	IFLA_NEW_NETNSID,
	IFLA_IF_NETNSID,
	IFLA_TARGET_NETNSID = IFLA_IF_NETNSID, /* new alias */
	IFLA_CARRIER_UP_COUNT,
	IFLA_CARRIER_DOWN_COUNT,
	IFLA_NEW_IFINDEX,
	IFLA_MIN_MTU,
	IFLA_MAX_MTU,
	IFLA_PROP_LIST,
	IFLA_ALT_IFNAME, /* Alternative ifname */
	IFLA_PERM_ADDRESS,
	IFLA_PROTO_DOWN_REASON,

	/* device (sysfs) name as parent, used instead
	 * of IFLA_LINK where there's no parent netdev
	 */
	IFLA_PARENT_DEV_NAME,
	IFLA_PARENT_DEV_BUS_NAME,
	IFLA_GRO_MAX_SIZE,
	IFLA_TSO_MAX_SIZE,
	IFLA_TSO_MAX_SEGS,
	IFLA_ALLMULTI,		/* Allmulti count: > 0 means acts ALLMULTI */

	IFLA_DEVLINK_PORT,

	IFLA_GSO_IPV4_MAX_SIZE,
	IFLA_GRO_IPV4_MAX_SIZE,

	__IFLA_MAX
};


#define IFLA_MAX (__IFLA_MAX - 1)

enum {
	IFLA_PROTO_DOWN_REASON_UNSPEC,
	IFLA_PROTO_DOWN_REASON_MASK,	/* u32, mask for reason bits */
	IFLA_PROTO_DOWN_REASON_VALUE,   /* u32, reason bit value */

	__IFLA_PROTO_DOWN_REASON_CNT,
	IFLA_PROTO_DOWN_REASON_MAX = __IFLA_PROTO_DOWN_REASON_CNT - 1
};

/* backwards compatibility for userspace */
#ifndef __KERNEL__
#define IFLA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifinfomsg))))
#define IFLA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ifinfomsg))
#endif

enum {
	IFLA_INET_UNSPEC,
	IFLA_INET_CONF,
	__IFLA_INET_MAX,
};

#define IFLA_INET_MAX (__IFLA_INET_MAX - 1)

/* ifi_flags.

   IFF_* flags.

   The only change is:
   IFF_LOOPBACK, IFF_BROADCAST and IFF_POINTOPOINT are
   more not changeable by user. They describe link media
   characteristics and set by device driver.

   Comments:
   - Combination IFF_BROADCAST|IFF_POINTOPOINT is invalid
   - If neither of these three flags are set;
     the interface is NBMA.

   - IFF_MULTICAST does not mean anything special:
   multicasts can be used on all not-NBMA links.
   IFF_MULTICAST means that this media uses special encapsulation
   for multicast frames. Apparently, all IFF_POINTOPOINT and
   IFF_BROADCAST devices are able to use multicasts too.
 */

/* IFLA_LINK.
   For usual devices it is equal ifi_index.
   If it is a "virtual interface" (f.e. tunnel), ifi_link
   can point to real physical interface (f.e. for bandwidth calculations),
   or maybe 0, what means, that real media is unknown (usual
   for IPIP tunnels, when route to endpoint is allowed to change)
 */

/* Subtype attributes for IFLA_PROTINFO */
enum {
	IFLA_INET6_UNSPEC,
	IFLA_INET6_FLAGS,	/* link flags			*/
	IFLA_INET6_CONF,	/* sysctl parameters		*/
	IFLA_INET6_STATS,	/* statistics			*/
	IFLA_INET6_MCAST,	/* MC things. What of them?	*/
	IFLA_INET6_CACHEINFO,	/* time values and max reasm size */
	IFLA_INET6_ICMP6STATS,	/* statistics (icmpv6)		*/
	IFLA_INET6_TOKEN,	/* device token			*/
	IFLA_INET6_ADDR_GEN_MODE, /* implicit address generator mode */
	IFLA_INET6_RA_MTU,	/* mtu carried in the RA message */
	__IFLA_INET6_MAX
};

#define IFLA_INET6_MAX	(__IFLA_INET6_MAX - 1)

enum in6_addr_gen_mode {
	IN6_ADDR_GEN_MODE_EUI64,
	IN6_ADDR_GEN_MODE_NONE,
	IN6_ADDR_GEN_MODE_STABLE_PRIVACY,
	IN6_ADDR_GEN_MODE_RANDOM,
};

/* Bridge section */

enum {
	IFLA_BR_UNSPEC,
	IFLA_BR_FORWARD_DELAY,
	IFLA_BR_HELLO_TIME,
	IFLA_BR_MAX_AGE,
	IFLA_BR_AGEING_TIME,
	IFLA_BR_STP_STATE,
	IFLA_BR_PRIORITY,
	IFLA_BR_VLAN_FILTERING,
	IFLA_BR_VLAN_PROTOCOL,
	IFLA_BR_GROUP_FWD_MASK,
	IFLA_BR_ROOT_ID,
	IFLA_BR_BRIDGE_ID,
	IFLA_BR_ROOT_PORT,
	IFLA_BR_ROOT_PATH_COST,
	IFLA_BR_TOPOLOGY_CHANGE,
	IFLA_BR_TOPOLOGY_CHANGE_DETECTED,
	IFLA_BR_HELLO_TIMER,
	IFLA_BR_TCN_TIMER,
	IFLA_BR_TOPOLOGY_CHANGE_TIMER,
	IFLA_BR_GC_TIMER,
	IFLA_BR_GROUP_ADDR,
	IFLA_BR_FDB_FLUSH,
	IFLA_BR_MCAST_ROUTER,
	IFLA_BR_MCAST_SNOOPING,
	IFLA_BR_MCAST_QUERY_USE_IFADDR,
	IFLA_BR_MCAST_QUERIER,
	IFLA_BR_MCAST_HASH_ELASTICITY,
	IFLA_BR_MCAST_HASH_MAX,
	IFLA_BR_MCAST_LAST_MEMBER_CNT,
	IFLA_BR_MCAST_STARTUP_QUERY_CNT,
	IFLA_BR_MCAST_LAST_MEMBER_INTVL,
	IFLA_BR_MCAST_MEMBERSHIP_INTVL,
	IFLA_BR_MCAST_QUERIER_INTVL,
	IFLA_BR_MCAST_QUERY_INTVL,
	IFLA_BR_MCAST_QUERY_RESPONSE_INTVL,
	IFLA_BR_MCAST_STARTUP_QUERY_INTVL,
	IFLA_BR_NF_CALL_IPTABLES,
	IFLA_BR_NF_CALL_IP6TABLES,
	IFLA_BR_NF_CALL_ARPTABLES,
	IFLA_BR_VLAN_DEFAULT_PVID,
	IFLA_BR_PAD,
	IFLA_BR_VLAN_STATS_ENABLED,
	IFLA_BR_MCAST_STATS_ENABLED,
	IFLA_BR_MCAST_IGMP_VERSION,
	IFLA_BR_MCAST_MLD_VERSION,
	IFLA_BR_VLAN_STATS_PER_PORT,
	IFLA_BR_MULTI_BOOLOPT,
	IFLA_BR_MCAST_QUERIER_STATE,
	__IFLA_BR_MAX,
};

#define IFLA_BR_MAX	(__IFLA_BR_MAX - 1)

struct ifla_bridge_id {
	__u8	prio[2];
	__u8	addr[6]; /* ETH_ALEN */
};

enum {
	BRIDGE_MODE_UNSPEC,
	BRIDGE_MODE_HAIRPIN,
};

enum {
	IFLA_BRPORT_UNSPEC,
	IFLA_BRPORT_STATE,	/* Spanning tree state     */
	IFLA_BRPORT_PRIORITY,	/* "             priority  */
	IFLA_BRPORT_COST,	/* "             cost      */
	IFLA_BRPORT_MODE,	/* mode (hairpin)          */
	IFLA_BRPORT_GUARD,	/* bpdu guard              */
	IFLA_BRPORT_PROTECT,	/* root port protection    */
	IFLA_BRPORT_FAST_LEAVE,	/* multicast fast leave    */
	IFLA_BRPORT_LEARNING,	/* mac learning */
	IFLA_BRPORT_UNICAST_FLOOD, /* flood unicast traffic */
	IFLA_BRPORT_PROXYARP,	/* proxy ARP */
	IFLA_BRPORT_LEARNING_SYNC, /* mac learning sync from device */
	IFLA_BRPORT_PROXYARP_WIFI, /* proxy ARP for Wi-Fi */
	IFLA_BRPORT_ROOT_ID,	/* designated root */
	IFLA_BRPORT_BRIDGE_ID,	/* designated bridge */
	IFLA_BRPORT_DESIGNATED_PORT,
	IFLA_BRPORT_DESIGNATED_COST,
	IFLA_BRPORT_ID,
	IFLA_BRPORT_NO,
	IFLA_BRPORT_TOPOLOGY_CHANGE_ACK,
	IFLA_BRPORT_CONFIG_PENDING,
	IFLA_BRPORT_MESSAGE_AGE_TIMER,
	IFLA_BRPORT_FORWARD_DELAY_TIMER,
	IFLA_BRPORT_HOLD_TIMER,
	IFLA_BRPORT_FLUSH,
	IFLA_BRPORT_MULTICAST_ROUTER,
	IFLA_BRPORT_PAD,
	IFLA_BRPORT_MCAST_FLOOD,
	IFLA_BRPORT_MCAST_TO_UCAST,
	IFLA_BRPORT_VLAN_TUNNEL,
	IFLA_BRPORT_BCAST_FLOOD,
	IFLA_BRPORT_GROUP_FWD_MASK,
	IFLA_BRPORT_NEIGH_SUPPRESS,
	IFLA_BRPORT_ISOLATED,
	IFLA_BRPORT_BACKUP_PORT,
	IFLA_BRPORT_MRP_RING_OPEN,
	IFLA_BRPORT_MRP_IN_OPEN,
	IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT,
	IFLA_BRPORT_MCAST_EHT_HOSTS_CNT,
	IFLA_BRPORT_LOCKED,
	IFLA_BRPORT_MAB,
	IFLA_BRPORT_MCAST_N_GROUPS,
	IFLA_BRPORT_MCAST_MAX_GROUPS,
	__IFLA_BRPORT_MAX
};
#define IFLA_BRPORT_MAX (__IFLA_BRPORT_MAX - 1)

struct ifla_cacheinfo {
	__u32	max_reasm_len;
	__u32	tstamp;		/* ipv6InterfaceTable updated timestamp */
	__u32	reachable_time;
	__u32	retrans_time;
};

enum {
	IFLA_INFO_UNSPEC,
	IFLA_INFO_KIND,
	IFLA_INFO_DATA,
	IFLA_INFO_XSTATS,
	IFLA_INFO_SLAVE_KIND,
	IFLA_INFO_SLAVE_DATA,
	__IFLA_INFO_MAX,
};

#define IFLA_INFO_MAX	(__IFLA_INFO_MAX - 1)

/* VLAN section */

enum {
	IFLA_VLAN_UNSPEC,
	IFLA_VLAN_ID,
	IFLA_VLAN_FLAGS,
	IFLA_VLAN_EGRESS_QOS,
	IFLA_VLAN_INGRESS_QOS,
	IFLA_VLAN_PROTOCOL,
	__IFLA_VLAN_MAX,
};

#define IFLA_VLAN_MAX	(__IFLA_VLAN_MAX - 1)

struct ifla_vlan_flags {
	__u32	flags;
	__u32	mask;
};

enum {
	IFLA_VLAN_QOS_UNSPEC,
	IFLA_VLAN_QOS_MAPPING,
	__IFLA_VLAN_QOS_MAX
};

#define IFLA_VLAN_QOS_MAX	(__IFLA_VLAN_QOS_MAX - 1)

struct ifla_vlan_qos_mapping {
	__u32 from;
	__u32 to;
};

/* MACVLAN section */
enum {
	IFLA_MACVLAN_UNSPEC,
	IFLA_MACVLAN_MODE,
	IFLA_MACVLAN_FLAGS,
	IFLA_MACVLAN_MACADDR_MODE,
	IFLA_MACVLAN_MACADDR,
	IFLA_MACVLAN_MACADDR_DATA,
	IFLA_MACVLAN_MACADDR_COUNT,
	IFLA_MACVLAN_BC_QUEUE_LEN,
	IFLA_MACVLAN_BC_QUEUE_LEN_USED,
	__IFLA_MACVLAN_MAX,
};

#define IFLA_MACVLAN_MAX (__IFLA_MACVLAN_MAX - 1)

enum macvlan_mode {
	MACVLAN_MODE_PRIVATE = 1, /* don't talk to other macvlans */
	MACVLAN_MODE_VEPA    = 2, /* talk to other ports through ext bridge */
	MACVLAN_MODE_BRIDGE  = 4, /* talk to bridge ports directly */
	MACVLAN_MODE_PASSTHRU = 8,/* take over the underlying device */
	MACVLAN_MODE_SOURCE  = 16,/* use source MAC address list to assign */
};

enum macvlan_macaddr_mode {
	MACVLAN_MACADDR_ADD,
	MACVLAN_MACADDR_DEL,
	MACVLAN_MACADDR_FLUSH,
	MACVLAN_MACADDR_SET,
};

#define MACVLAN_FLAG_NOPROMISC	1
#define MACVLAN_FLAG_NODST	2 /* skip dst macvlan if matching src macvlan */

/* VRF section */
enum {
	IFLA_VRF_UNSPEC,
	IFLA_VRF_TABLE,
	__IFLA_VRF_MAX
};

#define IFLA_VRF_MAX (__IFLA_VRF_MAX - 1)

enum {
	IFLA_VRF_PORT_UNSPEC,
	IFLA_VRF_PORT_TABLE,
	__IFLA_VRF_PORT_MAX
};

#define IFLA_VRF_PORT_MAX (__IFLA_VRF_PORT_MAX - 1)

/* MACSEC section */
enum {
	IFLA_MACSEC_UNSPEC,
	IFLA_MACSEC_SCI,
	IFLA_MACSEC_PORT,
	IFLA_MACSEC_ICV_LEN,
	IFLA_MACSEC_CIPHER_SUITE,
	IFLA_MACSEC_WINDOW,
	IFLA_MACSEC_ENCODING_SA,
	IFLA_MACSEC_ENCRYPT,
	IFLA_MACSEC_PROTECT,
	IFLA_MACSEC_INC_SCI,
	IFLA_MACSEC_ES,
	IFLA_MACSEC_SCB,
	IFLA_MACSEC_REPLAY_PROTECT,
	IFLA_MACSEC_VALIDATION,
	IFLA_MACSEC_PAD,
	IFLA_MACSEC_OFFLOAD,
	__IFLA_MACSEC_MAX,
};

#define IFLA_MACSEC_MAX (__IFLA_MACSEC_MAX - 1)

/* XFRM section */
enum {
	IFLA_XFRM_UNSPEC,
	IFLA_XFRM_LINK,
	IFLA_XFRM_IF_ID,
	IFLA_XFRM_COLLECT_METADATA,
	__IFLA_XFRM_MAX
};

#define IFLA_XFRM_MAX (__IFLA_XFRM_MAX - 1)

enum macsec_validation_type {
	MACSEC_VALIDATE_DISABLED = 0,
	MACSEC_VALIDATE_CHECK = 1,
	MACSEC_VALIDATE_STRICT = 2,
	__MACSEC_VALIDATE_END,
	MACSEC_VALIDATE_MAX = __MACSEC_VALIDATE_END - 1,
};

enum macsec_offload {
	MACSEC_OFFLOAD_OFF = 0,
	MACSEC_OFFLOAD_PHY = 1,
	MACSEC_OFFLOAD_MAC = 2,
	__MACSEC_OFFLOAD_END,
	MACSEC_OFFLOAD_MAX = __MACSEC_OFFLOAD_END - 1,
};

/* IPVLAN section */
enum {
	IFLA_IPVLAN_UNSPEC,
	IFLA_IPVLAN_MODE,
	IFLA_IPVLAN_FLAGS,
	__IFLA_IPVLAN_MAX
};

#define IFLA_IPVLAN_MAX (__IFLA_IPVLAN_MAX - 1)

enum ipvlan_mode {
	IPVLAN_MODE_L2 = 0,
	IPVLAN_MODE_L3,
	IPVLAN_MODE_L3S,
	IPVLAN_MODE_MAX
};

#define IPVLAN_F_PRIVATE	0x01
#define IPVLAN_F_VEPA		0x02

/* Tunnel RTM header */
struct tunnel_msg {
	__u8 family;
	__u8 flags;
	__u16 reserved2;
	__u32 ifindex;
};

/* VXLAN section */

/* include statistics in the dump */
#define TUNNEL_MSG_FLAG_STATS	0x01

#define TUNNEL_MSG_VALID_USER_FLAGS TUNNEL_MSG_FLAG_STATS

/* Embedded inside VXLAN_VNIFILTER_ENTRY_STATS */
enum {
	VNIFILTER_ENTRY_STATS_UNSPEC,
	VNIFILTER_ENTRY_STATS_RX_BYTES,
	VNIFILTER_ENTRY_STATS_RX_PKTS,
	VNIFILTER_ENTRY_STATS_RX_DROPS,
	VNIFILTER_ENTRY_STATS_RX_ERRORS,
	VNIFILTER_ENTRY_STATS_TX_BYTES,
	VNIFILTER_ENTRY_STATS_TX_PKTS,
	VNIFILTER_ENTRY_STATS_TX_DROPS,
	VNIFILTER_ENTRY_STATS_TX_ERRORS,
	VNIFILTER_ENTRY_STATS_PAD,
	__VNIFILTER_ENTRY_STATS_MAX
};
#define VNIFILTER_ENTRY_STATS_MAX (__VNIFILTER_ENTRY_STATS_MAX - 1)

enum {
	VXLAN_VNIFILTER_ENTRY_UNSPEC,
	VXLAN_VNIFILTER_ENTRY_START,
	VXLAN_VNIFILTER_ENTRY_END,
	VXLAN_VNIFILTER_ENTRY_GROUP,
	VXLAN_VNIFILTER_ENTRY_GROUP6,
	VXLAN_VNIFILTER_ENTRY_STATS,
	__VXLAN_VNIFILTER_ENTRY_MAX
};
#define VXLAN_VNIFILTER_ENTRY_MAX	(__VXLAN_VNIFILTER_ENTRY_MAX - 1)

enum {
	VXLAN_VNIFILTER_UNSPEC,
	VXLAN_VNIFILTER_ENTRY,
	__VXLAN_VNIFILTER_MAX
};
#define VXLAN_VNIFILTER_MAX	(__VXLAN_VNIFILTER_MAX - 1)

enum {
	IFLA_VXLAN_UNSPEC,
	IFLA_VXLAN_ID,
	IFLA_VXLAN_GROUP,	/* group or remote address */
	IFLA_VXLAN_LINK,
	IFLA_VXLAN_LOCAL,
	IFLA_VXLAN_TTL,
	IFLA_VXLAN_TOS,
	IFLA_VXLAN_LEARNING,
	IFLA_VXLAN_AGEING,
	IFLA_VXLAN_LIMIT,
	IFLA_VXLAN_PORT_RANGE,	/* source port */
	IFLA_VXLAN_PROXY,
	IFLA_VXLAN_RSC,
	IFLA_VXLAN_L2MISS,
	IFLA_VXLAN_L3MISS,
	IFLA_VXLAN_PORT,	/* destination port */
	IFLA_VXLAN_GROUP6,
	IFLA_VXLAN_LOCAL6,
	IFLA_VXLAN_UDP_CSUM,
	IFLA_VXLAN_UDP_ZERO_CSUM6_TX,
	IFLA_VXLAN_UDP_ZERO_CSUM6_RX,
	IFLA_VXLAN_REMCSUM_TX,
	IFLA_VXLAN_REMCSUM_RX,
	IFLA_VXLAN_GBP,
	IFLA_VXLAN_REMCSUM_NOPARTIAL,
	IFLA_VXLAN_COLLECT_METADATA,
	IFLA_VXLAN_LABEL,
	IFLA_VXLAN_GPE,
	IFLA_VXLAN_TTL_INHERIT,
	IFLA_VXLAN_DF,
	IFLA_VXLAN_VNIFILTER, /* only applicable with COLLECT_METADATA mode */
	__IFLA_VXLAN_MAX
};
#define IFLA_VXLAN_MAX	(__IFLA_VXLAN_MAX - 1)

struct ifla_vxlan_port_range {
	__be16	low;
	__be16	high;
};

enum ifla_vxlan_df {
	VXLAN_DF_UNSET = 0,
	VXLAN_DF_SET,
	VXLAN_DF_INHERIT,
	__VXLAN_DF_END,
	VXLAN_DF_MAX = __VXLAN_DF_END - 1,
};

/* GENEVE section */
enum {
	IFLA_GENEVE_UNSPEC,
	IFLA_GENEVE_ID,
	IFLA_GENEVE_REMOTE,
	IFLA_GENEVE_TTL,
	IFLA_GENEVE_TOS,
	IFLA_GENEVE_PORT,	/* destination port */
	IFLA_GENEVE_COLLECT_METADATA,
	IFLA_GENEVE_REMOTE6,
	IFLA_GENEVE_UDP_CSUM,
	IFLA_GENEVE_UDP_ZERO_CSUM6_TX,
	IFLA_GENEVE_UDP_ZERO_CSUM6_RX,
	IFLA_GENEVE_LABEL,
	IFLA_GENEVE_TTL_INHERIT,
	IFLA_GENEVE_DF,
	IFLA_GENEVE_INNER_PROTO_INHERIT,
	__IFLA_GENEVE_MAX
};
#define IFLA_GENEVE_MAX	(__IFLA_GENEVE_MAX - 1)

enum ifla_geneve_df {
	GENEVE_DF_UNSET = 0,
	GENEVE_DF_SET,
	GENEVE_DF_INHERIT,
	__GENEVE_DF_END,
	GENEVE_DF_MAX = __GENEVE_DF_END - 1,
};

/* Bareudp section  */
enum {
	IFLA_BAREUDP_UNSPEC,
	IFLA_BAREUDP_PORT,
	IFLA_BAREUDP_ETHERTYPE,
	IFLA_BAREUDP_SRCPORT_MIN,
	IFLA_BAREUDP_MULTIPROTO_MODE,
	__IFLA_BAREUDP_MAX
};

#define IFLA_BAREUDP_MAX (__IFLA_BAREUDP_MAX - 1)

/* PPP section */
enum {
	IFLA_PPP_UNSPEC,
	IFLA_PPP_DEV_FD,
	__IFLA_PPP_MAX
};
#define IFLA_PPP_MAX (__IFLA_PPP_MAX - 1)

/* GTP section */

enum ifla_gtp_role {
	GTP_ROLE_GGSN = 0,
	GTP_ROLE_SGSN,
};

enum {
	IFLA_GTP_UNSPEC,
	IFLA_GTP_FD0,
	IFLA_GTP_FD1,
	IFLA_GTP_PDP_HASHSIZE,
	IFLA_GTP_ROLE,
	IFLA_GTP_CREATE_SOCKETS,
	IFLA_GTP_RESTART_COUNT,
	__IFLA_GTP_MAX,
};
#define IFLA_GTP_MAX (__IFLA_GTP_MAX - 1)

/* Bonding section */

enum {
	IFLA_BOND_UNSPEC,
	IFLA_BOND_MODE,
	IFLA_BOND_ACTIVE_SLAVE,
	IFLA_BOND_MIIMON,
	IFLA_BOND_UPDELAY,
	IFLA_BOND_DOWNDELAY,
	IFLA_BOND_USE_CARRIER,
	IFLA_BOND_ARP_INTERVAL,
	IFLA_BOND_ARP_IP_TARGET,
	IFLA_BOND_ARP_VALIDATE,
	IFLA_BOND_ARP_ALL_TARGETS,
	IFLA_BOND_PRIMARY,
	IFLA_BOND_PRIMARY_RESELECT,
	IFLA_BOND_FAIL_OVER_MAC,
	IFLA_BOND_XMIT_HASH_POLICY,
	IFLA_BOND_RESEND_IGMP,
	IFLA_BOND_NUM_PEER_NOTIF,
	IFLA_BOND_ALL_SLAVES_ACTIVE,
	IFLA_BOND_MIN_LINKS,
	IFLA_BOND_LP_INTERVAL,
	IFLA_BOND_PACKETS_PER_SLAVE,
	IFLA_BOND_AD_LACP_RATE,
	IFLA_BOND_AD_SELECT,
	IFLA_BOND_AD_INFO,
	IFLA_BOND_AD_ACTOR_SYS_PRIO,
	IFLA_BOND_AD_USER_PORT_KEY,
	IFLA_BOND_AD_ACTOR_SYSTEM,
	IFLA_BOND_TLB_DYNAMIC_LB,
	IFLA_BOND_PEER_NOTIF_DELAY,
	IFLA_BOND_AD_LACP_ACTIVE,
	IFLA_BOND_MISSED_MAX,
	IFLA_BOND_NS_IP6_TARGET,
	__IFLA_BOND_MAX,
};

#define IFLA_BOND_MAX	(__IFLA_BOND_MAX - 1)

enum {
	IFLA_BOND_AD_INFO_UNSPEC,
	IFLA_BOND_AD_INFO_AGGREGATOR,
	IFLA_BOND_AD_INFO_NUM_PORTS,
	IFLA_BOND_AD_INFO_ACTOR_KEY,
	IFLA_BOND_AD_INFO_PARTNER_KEY,
	IFLA_BOND_AD_INFO_PARTNER_MAC,
	__IFLA_BOND_AD_INFO_MAX,
};

#define IFLA_BOND_AD_INFO_MAX	(__IFLA_BOND_AD_INFO_MAX - 1)

enum {
	IFLA_BOND_SLAVE_UNSPEC,
	IFLA_BOND_SLAVE_STATE,
	IFLA_BOND_SLAVE_MII_STATUS,
	IFLA_BOND_SLAVE_LINK_FAILURE_COUNT,
	IFLA_BOND_SLAVE_PERM_HWADDR,
	IFLA_BOND_SLAVE_QUEUE_ID,
	IFLA_BOND_SLAVE_AD_AGGREGATOR_ID,
	IFLA_BOND_SLAVE_AD_ACTOR_OPER_PORT_STATE,
	IFLA_BOND_SLAVE_AD_PARTNER_OPER_PORT_STATE,
	IFLA_BOND_SLAVE_PRIO,
	__IFLA_BOND_SLAVE_MAX,
};

#define IFLA_BOND_SLAVE_MAX	(__IFLA_BOND_SLAVE_MAX - 1)

/* SR-IOV virtual function management section */

enum {
	IFLA_VF_INFO_UNSPEC,
	IFLA_VF_INFO,
	__IFLA_VF_INFO_MAX,
};

#define IFLA_VF_INFO_MAX (__IFLA_VF_INFO_MAX - 1)

enum {
	IFLA_VF_UNSPEC,
	IFLA_VF_MAC,		/* Hardware queue specific attributes */
	IFLA_VF_VLAN,		/* VLAN ID and QoS */
	IFLA_VF_TX_RATE,	/* Max TX Bandwidth Allocation */
	IFLA_VF_SPOOFCHK,	/* Spoof Checking on/off switch */
	IFLA_VF_LINK_STATE,	/* link state enable/disable/auto switch */
	IFLA_VF_RATE,		/* Min and Max TX Bandwidth Allocation */
	IFLA_VF_RSS_QUERY_EN,	/* RSS Redirection Table and Hash Key query
				 * on/off switch
				 */
	IFLA_VF_STATS,		/* network device statistics */
	IFLA_VF_TRUST,		/* Trust VF */
	IFLA_VF_IB_NODE_GUID,	/* VF Infiniband node GUID */
	IFLA_VF_IB_PORT_GUID,	/* VF Infiniband port GUID */
	IFLA_VF_VLAN_LIST,	/* nested list of vlans, option for QinQ */
	IFLA_VF_BROADCAST,	/* VF broadcast */
	__IFLA_VF_MAX,
};

#define IFLA_VF_MAX (__IFLA_VF_MAX - 1)

struct ifla_vf_mac {
	__u32 vf;
	__u8 mac[32]; /* MAX_ADDR_LEN */
};

struct ifla_vf_broadcast {
	__u8 broadcast[32];
};

struct ifla_vf_vlan {
	__u32 vf;
	__u32 vlan; /* 0 - 4095, 0 disables VLAN filter */
	__u32 qos;
};

enum {
	IFLA_VF_VLAN_INFO_UNSPEC,
	IFLA_VF_VLAN_INFO,	/* VLAN ID, QoS and VLAN protocol */
	__IFLA_VF_VLAN_INFO_MAX,
};

#define IFLA_VF_VLAN_INFO_MAX (__IFLA_VF_VLAN_INFO_MAX - 1)
#define MAX_VLAN_LIST_LEN 1

struct ifla_vf_vlan_info {
	__u32 vf;
	__u32 vlan; /* 0 - 4095, 0 disables VLAN filter */
	__u32 qos;
	__be16 vlan_proto; /* VLAN protocol either 802.1Q or 802.1ad */
};

struct ifla_vf_tx_rate {
	__u32 vf;
	__u32 rate; /* Max TX bandwidth in Mbps, 0 disables throttling */
};

struct ifla_vf_rate {
	__u32 vf;
	__u32 min_tx_rate; /* Min Bandwidth in Mbps */
	__u32 max_tx_rate; /* Max Bandwidth in Mbps */
};

struct ifla_vf_spoofchk {
	__u32 vf;
	__u32 setting;
};

struct ifla_vf_guid {
	__u32 vf;
	__u64 guid;
};

enum {
	IFLA_VF_LINK_STATE_AUTO,	/* link state of the uplink */
	IFLA_VF_LINK_STATE_ENABLE,	/* link always up */
	IFLA_VF_LINK_STATE_DISABLE,	/* link always down */
	__IFLA_VF_LINK_STATE_MAX,
};

struct ifla_vf_link_state {
	__u32 vf;
	__u32 link_state;
};

struct ifla_vf_rss_query_en {
	__u32 vf;
	__u32 setting;
};

enum {
	IFLA_VF_STATS_RX_PACKETS,
	IFLA_VF_STATS_TX_PACKETS,
	IFLA_VF_STATS_RX_BYTES,
	IFLA_VF_STATS_TX_BYTES,
	IFLA_VF_STATS_BROADCAST,
	IFLA_VF_STATS_MULTICAST,
	IFLA_VF_STATS_PAD,
	IFLA_VF_STATS_RX_DROPPED,
	IFLA_VF_STATS_TX_DROPPED,
	__IFLA_VF_STATS_MAX,
};

#define IFLA_VF_STATS_MAX (__IFLA_VF_STATS_MAX - 1)

struct ifla_vf_trust {
	__u32 vf;
	__u32 setting;
};

/* VF ports management section
 *
 *	Nested layout of set/get msg is:
 *
 *		[IFLA_NUM_VF]
 *		[IFLA_VF_PORTS]
 *			[IFLA_VF_PORT]
 *				[IFLA_PORT_*], ...
 *			[IFLA_VF_PORT]
 *				[IFLA_PORT_*], ...
 *			...
 *		[IFLA_PORT_SELF]
 *			[IFLA_PORT_*], ...
 */

enum {
	IFLA_VF_PORT_UNSPEC,
	IFLA_VF_PORT,			/* nest */
	__IFLA_VF_PORT_MAX,
};

#define IFLA_VF_PORT_MAX (__IFLA_VF_PORT_MAX - 1)

enum {
	IFLA_PORT_UNSPEC,
	IFLA_PORT_VF,			/* __u32 */
	IFLA_PORT_PROFILE,		/* string */
	IFLA_PORT_VSI_TYPE,		/* 802.1Qbg (pre-)standard VDP */
	IFLA_PORT_INSTANCE_UUID,	/* binary UUID */
	IFLA_PORT_HOST_UUID,		/* binary UUID */
	IFLA_PORT_REQUEST,		/* __u8 */
	IFLA_PORT_RESPONSE,		/* __u16, output only */
	__IFLA_PORT_MAX,
};

#define IFLA_PORT_MAX (__IFLA_PORT_MAX - 1)

#define PORT_PROFILE_MAX	40
#define PORT_UUID_MAX		16
#define PORT_SELF_VF		-1

enum {
	PORT_REQUEST_PREASSOCIATE = 0,
	PORT_REQUEST_PREASSOCIATE_RR,
	PORT_REQUEST_ASSOCIATE,
	PORT_REQUEST_DISASSOCIATE,
};

enum {
	PORT_VDP_RESPONSE_SUCCESS = 0,
	PORT_VDP_RESPONSE_INVALID_FORMAT,
	PORT_VDP_RESPONSE_INSUFFICIENT_RESOURCES,
	PORT_VDP_RESPONSE_UNUSED_VTID,
	PORT_VDP_RESPONSE_VTID_VIOLATION,
	PORT_VDP_RESPONSE_VTID_VERSION_VIOALTION,
	PORT_VDP_RESPONSE_OUT_OF_SYNC,
	/* 0x08-0xFF reserved for future VDP use */
	PORT_PROFILE_RESPONSE_SUCCESS = 0x100,
	PORT_PROFILE_RESPONSE_INPROGRESS,
	PORT_PROFILE_RESPONSE_INVALID,
	PORT_PROFILE_RESPONSE_BADSTATE,
	PORT_PROFILE_RESPONSE_INSUFFICIENT_RESOURCES,
	PORT_PROFILE_RESPONSE_ERROR,
};

struct ifla_port_vsi {
	__u8 vsi_mgr_id;
	__u8 vsi_type_id[3];
	__u8 vsi_type_version;
	__u8 pad[3];
};


/* IPoIB section */

enum {
	IFLA_IPOIB_UNSPEC,
	IFLA_IPOIB_PKEY,
	IFLA_IPOIB_MODE,
	IFLA_IPOIB_UMCAST,
	__IFLA_IPOIB_MAX
};

enum {
	IPOIB_MODE_DATAGRAM  = 0, /* using unreliable datagram QPs */
	IPOIB_MODE_CONNECTED = 1, /* using connected QPs */
};

#define IFLA_IPOIB_MAX (__IFLA_IPOIB_MAX - 1)


/* HSR/PRP section, both uses same interface */

/* Different redundancy protocols for hsr device */
enum {
	HSR_PROTOCOL_HSR,
	HSR_PROTOCOL_PRP,
	HSR_PROTOCOL_MAX,
};

enum {
	IFLA_HSR_UNSPEC,
	IFLA_HSR_SLAVE1,
	IFLA_HSR_SLAVE2,
	IFLA_HSR_MULTICAST_SPEC,	/* Last byte of supervision addr */
	IFLA_HSR_SUPERVISION_ADDR,	/* Supervision frame multicast addr */
	IFLA_HSR_SEQ_NR,
	IFLA_HSR_VERSION,		/* HSR version */
	IFLA_HSR_PROTOCOL,		/* Indicate different protocol than
					 * HSR. For example PRP.
					 */
	__IFLA_HSR_MAX,
};

#define IFLA_HSR_MAX (__IFLA_HSR_MAX - 1)

/* STATS section */

struct if_stats_msg {
	__u8  family;
	__u8  pad1;
	__u16 pad2;
	__u32 ifindex;
	__u32 filter_mask;
};

/* A stats attribute can be netdev specific or a global stat.
 * For netdev stats, lets use the prefix IFLA_STATS_LINK_*
 */
enum {
	IFLA_STATS_UNSPEC, /* also used as 64bit pad attribute */
	IFLA_STATS_LINK_64,
	IFLA_STATS_LINK_XSTATS,
	IFLA_STATS_LINK_XSTATS_SLAVE,
	IFLA_STATS_LINK_OFFLOAD_XSTATS,
	IFLA_STATS_AF_SPEC,
	__IFLA_STATS_MAX,
};

#define IFLA_STATS_MAX (__IFLA_STATS_MAX - 1)

#define IFLA_STATS_FILTER_BIT(ATTR)	(1 << (ATTR - 1))

enum {
	IFLA_STATS_GETSET_UNSPEC,
	IFLA_STATS_GET_FILTERS, /* Nest of IFLA_STATS_LINK_xxx, each a u32 with
				 * a filter mask for the corresponding group.
				 */
	IFLA_STATS_SET_OFFLOAD_XSTATS_L3_STATS, /* 0 or 1 as u8 */
	__IFLA_STATS_GETSET_MAX,
};

#define IFLA_STATS_GETSET_MAX (__IFLA_STATS_GETSET_MAX - 1)

/* These are embedded into IFLA_STATS_LINK_XSTATS:
 * [IFLA_STATS_LINK_XSTATS]
 * -> [LINK_XSTATS_TYPE_xxx]
 *    -> [rtnl link type specific attributes]
 */
enum {
	LINK_XSTATS_TYPE_UNSPEC,
	LINK_XSTATS_TYPE_BRIDGE,
	LINK_XSTATS_TYPE_BOND,
	__LINK_XSTATS_TYPE_MAX
};
#define LINK_XSTATS_TYPE_MAX (__LINK_XSTATS_TYPE_MAX - 1)

/* These are stats embedded into IFLA_STATS_LINK_OFFLOAD_XSTATS */
enum {
	IFLA_OFFLOAD_XSTATS_UNSPEC,
	IFLA_OFFLOAD_XSTATS_CPU_HIT, /* struct rtnl_link_stats64 */
	IFLA_OFFLOAD_XSTATS_HW_S_INFO,	/* HW stats info. A nest */
	IFLA_OFFLOAD_XSTATS_L3_STATS,	/* struct rtnl_hw_stats64 */
	__IFLA_OFFLOAD_XSTATS_MAX
};
#define IFLA_OFFLOAD_XSTATS_MAX (__IFLA_OFFLOAD_XSTATS_MAX - 1)

enum {
	IFLA_OFFLOAD_XSTATS_HW_S_INFO_UNSPEC,
	IFLA_OFFLOAD_XSTATS_HW_S_INFO_REQUEST,		/* u8 */
	IFLA_OFFLOAD_XSTATS_HW_S_INFO_USED,		/* u8 */
	__IFLA_OFFLOAD_XSTATS_HW_S_INFO_MAX,
};
#define IFLA_OFFLOAD_XSTATS_HW_S_INFO_MAX \
	(__IFLA_OFFLOAD_XSTATS_HW_S_INFO_MAX - 1)

/* XDP section */

#define XDP_FLAGS_UPDATE_IF_NOEXIST	(1U << 0)
#define XDP_FLAGS_SKB_MODE		(1U << 1)
#define XDP_FLAGS_DRV_MODE		(1U << 2)
#define XDP_FLAGS_HW_MODE		(1U << 3)
#define XDP_FLAGS_REPLACE		(1U << 4)
#define XDP_FLAGS_MODES			(XDP_FLAGS_SKB_MODE | \
					 XDP_FLAGS_DRV_MODE | \
					 XDP_FLAGS_HW_MODE)
#define XDP_FLAGS_MASK			(XDP_FLAGS_UPDATE_IF_NOEXIST | \
					 XDP_FLAGS_MODES | XDP_FLAGS_REPLACE)

/* These are stored into IFLA_XDP_ATTACHED on dump. */
enum {
	XDP_ATTACHED_NONE = 0,
	XDP_ATTACHED_DRV,
	XDP_ATTACHED_SKB,
	XDP_ATTACHED_HW,
	XDP_ATTACHED_MULTI,
};

enum {
	IFLA_XDP_UNSPEC,
	IFLA_XDP_FD,
	IFLA_XDP_ATTACHED,
	IFLA_XDP_FLAGS,
	IFLA_XDP_PROG_ID,
	IFLA_XDP_DRV_PROG_ID,
	IFLA_XDP_SKB_PROG_ID,
	IFLA_XDP_HW_PROG_ID,
	IFLA_XDP_EXPECTED_FD,
	__IFLA_XDP_MAX,
};

#define IFLA_XDP_MAX (__IFLA_XDP_MAX - 1)

enum {
	IFLA_EVENT_NONE,
	IFLA_EVENT_REBOOT,		/* internal reset / reboot */
	IFLA_EVENT_FEATURES,		/* change in offload features */
	IFLA_EVENT_BONDING_FAILOVER,	/* change in active slave */
	IFLA_EVENT_NOTIFY_PEERS,	/* re-sent grat. arp/ndisc */
	IFLA_EVENT_IGMP_RESEND,		/* re-sent IGMP JOIN */
	IFLA_EVENT_BONDING_OPTIONS,	/* change in bonding options */
};

/* tun section */

enum {
	IFLA_TUN_UNSPEC,
	IFLA_TUN_OWNER,
	IFLA_TUN_GROUP,
	IFLA_TUN_TYPE,
	IFLA_TUN_PI,
	IFLA_TUN_VNET_HDR,
	IFLA_TUN_PERSIST,
	IFLA_TUN_MULTI_QUEUE,
	IFLA_TUN_NUM_QUEUES,
	IFLA_TUN_NUM_DISABLED_QUEUES,
	__IFLA_TUN_MAX,
};

#define IFLA_TUN_MAX (__IFLA_TUN_MAX - 1)

/* rmnet section */

#define RMNET_FLAGS_INGRESS_DEAGGREGATION         (1U << 0)
#define RMNET_FLAGS_INGRESS_MAP_COMMANDS          (1U << 1)
#define RMNET_FLAGS_INGRESS_MAP_CKSUMV4           (1U << 2)
#define RMNET_FLAGS_EGRESS_MAP_CKSUMV4            (1U << 3)
#define RMNET_FLAGS_INGRESS_MAP_CKSUMV5           (1U << 4)
#define RMNET_FLAGS_EGRESS_MAP_CKSUMV5            (1U << 5)

enum {
	IFLA_RMNET_UNSPEC,
	IFLA_RMNET_MUX_ID,
	IFLA_RMNET_FLAGS,
	__IFLA_RMNET_MAX,
};

#define IFLA_RMNET_MAX	(__IFLA_RMNET_MAX - 1)

struct ifla_rmnet_flags {
	__u32	flags;
	__u32	mask;
};

/* MCTP section */

enum {
	IFLA_MCTP_UNSPEC,
	IFLA_MCTP_NET,
	__IFLA_MCTP_MAX,
};

#define IFLA_MCTP_MAX (__IFLA_MCTP_MAX - 1)

/* DSA section */

enum {
	IFLA_DSA_UNSPEC,
	IFLA_DSA_MASTER,
	__IFLA_DSA_MAX,
};

#define IFLA_DSA_MAX	(__IFLA_DSA_MAX - 1)

#endif /* _UAPI_LINUX_IF_LINK_H */
