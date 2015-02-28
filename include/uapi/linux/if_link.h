#ifndef _UAPI_LINUX_IF_LINK_H
#define _UAPI_LINUX_IF_LINK_H

#include <linux/types.h>
#include <linux/netlink.h>

/* This struct should be in sync with struct rtnl_link_stats64 */
struct rtnl_link_stats {
	__u32	rx_packets;		/* total packets received	*/
	__u32	tx_packets;		/* total packets transmitted	*/
	__u32	rx_bytes;		/* total bytes received 	*/
	__u32	tx_bytes;		/* total bytes transmitted	*/
	__u32	rx_errors;		/* bad packets received		*/
	__u32	tx_errors;		/* packet transmit problems	*/
	__u32	rx_dropped;		/* no space in linux buffers	*/
	__u32	tx_dropped;		/* no space available in linux	*/
	__u32	multicast;		/* multicast packets received	*/
	__u32	collisions;

	/* detailed rx_errors: */
	__u32	rx_length_errors;
	__u32	rx_over_errors;		/* receiver ring buff overflow	*/
	__u32	rx_crc_errors;		/* recved pkt with crc error	*/
	__u32	rx_frame_errors;	/* recv'd frame alignment error */
	__u32	rx_fifo_errors;		/* recv'r fifo overrun		*/
	__u32	rx_missed_errors;	/* receiver missed packet	*/

	/* detailed tx_errors */
	__u32	tx_aborted_errors;
	__u32	tx_carrier_errors;
	__u32	tx_fifo_errors;
	__u32	tx_heartbeat_errors;
	__u32	tx_window_errors;

	/* for cslip etc */
	__u32	rx_compressed;
	__u32	tx_compressed;
};

/* The main device statistics structure */
struct rtnl_link_stats64 {
	__u64	rx_packets;		/* total packets received	*/
	__u64	tx_packets;		/* total packets transmitted	*/
	__u64	rx_bytes;		/* total bytes received 	*/
	__u64	tx_bytes;		/* total bytes transmitted	*/
	__u64	rx_errors;		/* bad packets received		*/
	__u64	tx_errors;		/* packet transmit problems	*/
	__u64	rx_dropped;		/* no space in linux buffers	*/
	__u64	tx_dropped;		/* no space available in linux	*/
	__u64	multicast;		/* multicast packets received	*/
	__u64	collisions;

	/* detailed rx_errors: */
	__u64	rx_length_errors;
	__u64	rx_over_errors;		/* receiver ring buff overflow	*/
	__u64	rx_crc_errors;		/* recved pkt with crc error	*/
	__u64	rx_frame_errors;	/* recv'd frame alignment error */
	__u64	rx_fifo_errors;		/* recv'r fifo overrun		*/
	__u64	rx_missed_errors;	/* receiver missed packet	*/

	/* detailed tx_errors */
	__u64	tx_aborted_errors;
	__u64	tx_carrier_errors;
	__u64	tx_fifo_errors;
	__u64	tx_heartbeat_errors;
	__u64	tx_window_errors;

	/* for cslip etc */
	__u64	rx_compressed;
	__u64	tx_compressed;
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
	__IFLA_MAX
};


#define IFLA_MAX (__IFLA_MAX - 1)

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
	__IFLA_INET6_MAX
};

#define IFLA_INET6_MAX	(__IFLA_INET6_MAX - 1)

enum in6_addr_gen_mode {
	IN6_ADDR_GEN_MODE_EUI64,
	IN6_ADDR_GEN_MODE_NONE,
};

/* Bridge section */

enum {
	IFLA_BR_UNSPEC,
	IFLA_BR_FORWARD_DELAY,
	IFLA_BR_HELLO_TIME,
	IFLA_BR_MAX_AGE,
	__IFLA_BR_MAX,
};

#define IFLA_BR_MAX	(__IFLA_BR_MAX - 1)

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

/* IPVLAN section */
enum {
	IFLA_IPVLAN_UNSPEC,
	IFLA_IPVLAN_MODE,
	__IFLA_IPVLAN_MAX
};

#define IFLA_IPVLAN_MAX (__IFLA_IPVLAN_MAX - 1)

enum ipvlan_mode {
	IPVLAN_MODE_L2 = 0,
	IPVLAN_MODE_L3,
	IPVLAN_MODE_MAX
};

/* VXLAN section */
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
	__IFLA_VXLAN_MAX
};
#define IFLA_VXLAN_MAX	(__IFLA_VXLAN_MAX - 1)

struct ifla_vxlan_port_range {
	__be16	low;
	__be16	high;
};

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
	IFLA_VF_VLAN,
	IFLA_VF_TX_RATE,	/* Max TX Bandwidth Allocation */
	IFLA_VF_SPOOFCHK,	/* Spoof Checking on/off switch */
	IFLA_VF_LINK_STATE,	/* link state enable/disable/auto switch */
	IFLA_VF_RATE,		/* Min and Max TX Bandwidth Allocation */
	__IFLA_VF_MAX,
};

#define IFLA_VF_MAX (__IFLA_VF_MAX - 1)

struct ifla_vf_mac {
	__u32 vf;
	__u8 mac[32]; /* MAX_ADDR_LEN */
};

struct ifla_vf_vlan {
	__u32 vf;
	__u32 vlan; /* 0 - 4095, 0 disables VLAN filter */
	__u32 qos;
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


/* HSR section */

enum {
	IFLA_HSR_UNSPEC,
	IFLA_HSR_SLAVE1,
	IFLA_HSR_SLAVE2,
	IFLA_HSR_MULTICAST_SPEC,	/* Last byte of supervision addr */
	IFLA_HSR_SUPERVISION_ADDR,	/* Supervision frame multicast addr */
	IFLA_HSR_SEQ_NR,
	__IFLA_HSR_MAX,
};

#define IFLA_HSR_MAX (__IFLA_HSR_MAX - 1)

#endif /* _UAPI_LINUX_IF_LINK_H */
