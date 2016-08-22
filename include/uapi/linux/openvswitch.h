
/*
 * Copyright (c) 2007-2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _UAPI__LINUX_OPENVSWITCH_H
#define _UAPI__LINUX_OPENVSWITCH_H 1

#include <linux/types.h>
#include <linux/if_ether.h>

/**
 * struct ovs_header - header for OVS Generic Netlink messages.
 * @dp_ifindex: ifindex of local port for datapath (0 to make a request not
 * specific to a datapath).
 *
 * Attributes following the header are specific to a particular OVS Generic
 * Netlink family, but all of the OVS families use this header.
 */

struct ovs_header {
	int dp_ifindex;
};

/* Datapaths. */

#define OVS_DATAPATH_FAMILY  "ovs_datapath"
#define OVS_DATAPATH_MCGROUP "ovs_datapath"

/* V2:
 *   - API users are expected to provide OVS_DP_ATTR_USER_FEATURES
 *     when creating the datapath.
 */
#define OVS_DATAPATH_VERSION 2

/* First OVS datapath version to support features */
#define OVS_DP_VER_FEATURES 2

enum ovs_datapath_cmd {
	OVS_DP_CMD_UNSPEC,
	OVS_DP_CMD_NEW,
	OVS_DP_CMD_DEL,
	OVS_DP_CMD_GET,
	OVS_DP_CMD_SET
};

/**
 * enum ovs_datapath_attr - attributes for %OVS_DP_* commands.
 * @OVS_DP_ATTR_NAME: Name of the network device that serves as the "local
 * port".  This is the name of the network device whose dp_ifindex is given in
 * the &struct ovs_header.  Always present in notifications.  Required in
 * %OVS_DP_NEW requests.  May be used as an alternative to specifying
 * dp_ifindex in other requests (with a dp_ifindex of 0).
 * @OVS_DP_ATTR_UPCALL_PID: The Netlink socket in userspace that is initially
 * set on the datapath port (for OVS_ACTION_ATTR_MISS).  Only valid on
 * %OVS_DP_CMD_NEW requests. A value of zero indicates that upcalls should
 * not be sent.
 * @OVS_DP_ATTR_STATS: Statistics about packets that have passed through the
 * datapath.  Always present in notifications.
 * @OVS_DP_ATTR_MEGAFLOW_STATS: Statistics about mega flow masks usage for the
 * datapath. Always present in notifications.
 *
 * These attributes follow the &struct ovs_header within the Generic Netlink
 * payload for %OVS_DP_* commands.
 */
enum ovs_datapath_attr {
	OVS_DP_ATTR_UNSPEC,
	OVS_DP_ATTR_NAME,		/* name of dp_ifindex netdev */
	OVS_DP_ATTR_UPCALL_PID,		/* Netlink PID to receive upcalls */
	OVS_DP_ATTR_STATS,		/* struct ovs_dp_stats */
	OVS_DP_ATTR_MEGAFLOW_STATS,	/* struct ovs_dp_megaflow_stats */
	OVS_DP_ATTR_USER_FEATURES,	/* OVS_DP_F_*  */
	OVS_DP_ATTR_PAD,
	__OVS_DP_ATTR_MAX
};

#define OVS_DP_ATTR_MAX (__OVS_DP_ATTR_MAX - 1)

struct ovs_dp_stats {
	__u64 n_hit;             /* Number of flow table matches. */
	__u64 n_missed;          /* Number of flow table misses. */
	__u64 n_lost;            /* Number of misses not sent to userspace. */
	__u64 n_flows;           /* Number of flows present */
};

struct ovs_dp_megaflow_stats {
	__u64 n_mask_hit;	 /* Number of masks used for flow lookups. */
	__u32 n_masks;		 /* Number of masks for the datapath. */
	__u32 pad0;		 /* Pad for future expension. */
	__u64 pad1;		 /* Pad for future expension. */
	__u64 pad2;		 /* Pad for future expension. */
};

struct ovs_vport_stats {
	__u64   rx_packets;		/* total packets received       */
	__u64   tx_packets;		/* total packets transmitted    */
	__u64   rx_bytes;		/* total bytes received         */
	__u64   tx_bytes;		/* total bytes transmitted      */
	__u64   rx_errors;		/* bad packets received         */
	__u64   tx_errors;		/* packet transmit problems     */
	__u64   rx_dropped;		/* no space in linux buffers    */
	__u64   tx_dropped;		/* no space available in linux  */
};

/* Allow last Netlink attribute to be unaligned */
#define OVS_DP_F_UNALIGNED	(1 << 0)

/* Allow datapath to associate multiple Netlink PIDs to each vport */
#define OVS_DP_F_VPORT_PIDS	(1 << 1)

/* Fixed logical ports. */
#define OVSP_LOCAL      ((__u32)0)

/* Packet transfer. */

#define OVS_PACKET_FAMILY "ovs_packet"
#define OVS_PACKET_VERSION 0x1

enum ovs_packet_cmd {
	OVS_PACKET_CMD_UNSPEC,

	/* Kernel-to-user notifications. */
	OVS_PACKET_CMD_MISS,    /* Flow table miss. */
	OVS_PACKET_CMD_ACTION,  /* OVS_ACTION_ATTR_USERSPACE action. */

	/* Userspace commands. */
	OVS_PACKET_CMD_EXECUTE  /* Apply actions to a packet. */
};

/**
 * enum ovs_packet_attr - attributes for %OVS_PACKET_* commands.
 * @OVS_PACKET_ATTR_PACKET: Present for all notifications.  Contains the entire
 * packet as received, from the start of the Ethernet header onward.  For
 * %OVS_PACKET_CMD_ACTION, %OVS_PACKET_ATTR_PACKET reflects changes made by
 * actions preceding %OVS_ACTION_ATTR_USERSPACE, but %OVS_PACKET_ATTR_KEY is
 * the flow key extracted from the packet as originally received.
 * @OVS_PACKET_ATTR_KEY: Present for all notifications.  Contains the flow key
 * extracted from the packet as nested %OVS_KEY_ATTR_* attributes.  This allows
 * userspace to adapt its flow setup strategy by comparing its notion of the
 * flow key against the kernel's.
 * @OVS_PACKET_ATTR_ACTIONS: Contains actions for the packet.  Used
 * for %OVS_PACKET_CMD_EXECUTE.  It has nested %OVS_ACTION_ATTR_* attributes.
 * Also used in upcall when %OVS_ACTION_ATTR_USERSPACE has optional
 * %OVS_USERSPACE_ATTR_ACTIONS attribute.
 * @OVS_PACKET_ATTR_USERDATA: Present for an %OVS_PACKET_CMD_ACTION
 * notification if the %OVS_ACTION_ATTR_USERSPACE action specified an
 * %OVS_USERSPACE_ATTR_USERDATA attribute, with the same length and content
 * specified there.
 * @OVS_PACKET_ATTR_EGRESS_TUN_KEY: Present for an %OVS_PACKET_CMD_ACTION
 * notification if the %OVS_ACTION_ATTR_USERSPACE action specified an
 * %OVS_USERSPACE_ATTR_EGRESS_TUN_PORT attribute, which is sent only if the
 * output port is actually a tunnel port. Contains the output tunnel key
 * extracted from the packet as nested %OVS_TUNNEL_KEY_ATTR_* attributes.
 * @OVS_PACKET_ATTR_MRU: Present for an %OVS_PACKET_CMD_ACTION and
 * @OVS_PACKET_ATTR_LEN: Packet size before truncation.
 * %OVS_PACKET_ATTR_USERSPACE action specify the Maximum received fragment
 * size.
 *
 * These attributes follow the &struct ovs_header within the Generic Netlink
 * payload for %OVS_PACKET_* commands.
 */
enum ovs_packet_attr {
	OVS_PACKET_ATTR_UNSPEC,
	OVS_PACKET_ATTR_PACKET,      /* Packet data. */
	OVS_PACKET_ATTR_KEY,         /* Nested OVS_KEY_ATTR_* attributes. */
	OVS_PACKET_ATTR_ACTIONS,     /* Nested OVS_ACTION_ATTR_* attributes. */
	OVS_PACKET_ATTR_USERDATA,    /* OVS_ACTION_ATTR_USERSPACE arg. */
	OVS_PACKET_ATTR_EGRESS_TUN_KEY,  /* Nested OVS_TUNNEL_KEY_ATTR_*
					    attributes. */
	OVS_PACKET_ATTR_UNUSED1,
	OVS_PACKET_ATTR_UNUSED2,
	OVS_PACKET_ATTR_PROBE,      /* Packet operation is a feature probe,
				       error logging should be suppressed. */
	OVS_PACKET_ATTR_MRU,	    /* Maximum received IP fragment size. */
	OVS_PACKET_ATTR_LEN,		/* Packet size before truncation. */
	__OVS_PACKET_ATTR_MAX
};

#define OVS_PACKET_ATTR_MAX (__OVS_PACKET_ATTR_MAX - 1)

/* Virtual ports. */

#define OVS_VPORT_FAMILY  "ovs_vport"
#define OVS_VPORT_MCGROUP "ovs_vport"
#define OVS_VPORT_VERSION 0x1

enum ovs_vport_cmd {
	OVS_VPORT_CMD_UNSPEC,
	OVS_VPORT_CMD_NEW,
	OVS_VPORT_CMD_DEL,
	OVS_VPORT_CMD_GET,
	OVS_VPORT_CMD_SET
};

enum ovs_vport_type {
	OVS_VPORT_TYPE_UNSPEC,
	OVS_VPORT_TYPE_NETDEV,   /* network device */
	OVS_VPORT_TYPE_INTERNAL, /* network device implemented by datapath */
	OVS_VPORT_TYPE_GRE,      /* GRE tunnel. */
	OVS_VPORT_TYPE_VXLAN,	 /* VXLAN tunnel. */
	OVS_VPORT_TYPE_GENEVE,	 /* Geneve tunnel. */
	__OVS_VPORT_TYPE_MAX
};

#define OVS_VPORT_TYPE_MAX (__OVS_VPORT_TYPE_MAX - 1)

/**
 * enum ovs_vport_attr - attributes for %OVS_VPORT_* commands.
 * @OVS_VPORT_ATTR_PORT_NO: 32-bit port number within datapath.
 * @OVS_VPORT_ATTR_TYPE: 32-bit %OVS_VPORT_TYPE_* constant describing the type
 * of vport.
 * @OVS_VPORT_ATTR_NAME: Name of vport.  For a vport based on a network device
 * this is the name of the network device.  Maximum length %IFNAMSIZ-1 bytes
 * plus a null terminator.
 * @OVS_VPORT_ATTR_OPTIONS: Vport-specific configuration information.
 * @OVS_VPORT_ATTR_UPCALL_PID: The array of Netlink socket pids in userspace
 * among which OVS_PACKET_CMD_MISS upcalls will be distributed for packets
 * received on this port.  If this is a single-element array of value 0,
 * upcalls should not be sent.
 * @OVS_VPORT_ATTR_STATS: A &struct ovs_vport_stats giving statistics for
 * packets sent or received through the vport.
 *
 * These attributes follow the &struct ovs_header within the Generic Netlink
 * payload for %OVS_VPORT_* commands.
 *
 * For %OVS_VPORT_CMD_NEW requests, the %OVS_VPORT_ATTR_TYPE and
 * %OVS_VPORT_ATTR_NAME attributes are required.  %OVS_VPORT_ATTR_PORT_NO is
 * optional; if not specified a free port number is automatically selected.
 * Whether %OVS_VPORT_ATTR_OPTIONS is required or optional depends on the type
 * of vport.
 *
 * For other requests, if %OVS_VPORT_ATTR_NAME is specified then it is used to
 * look up the vport to operate on; otherwise dp_idx from the &struct
 * ovs_header plus %OVS_VPORT_ATTR_PORT_NO determine the vport.
 */
enum ovs_vport_attr {
	OVS_VPORT_ATTR_UNSPEC,
	OVS_VPORT_ATTR_PORT_NO,	/* u32 port number within datapath */
	OVS_VPORT_ATTR_TYPE,	/* u32 OVS_VPORT_TYPE_* constant. */
	OVS_VPORT_ATTR_NAME,	/* string name, up to IFNAMSIZ bytes long */
	OVS_VPORT_ATTR_OPTIONS, /* nested attributes, varies by vport type */
	OVS_VPORT_ATTR_UPCALL_PID, /* array of u32 Netlink socket PIDs for */
				/* receiving upcalls */
	OVS_VPORT_ATTR_STATS,	/* struct ovs_vport_stats */
	OVS_VPORT_ATTR_PAD,
	__OVS_VPORT_ATTR_MAX
};

#define OVS_VPORT_ATTR_MAX (__OVS_VPORT_ATTR_MAX - 1)

enum {
	OVS_VXLAN_EXT_UNSPEC,
	OVS_VXLAN_EXT_GBP,	/* Flag or __u32 */
	__OVS_VXLAN_EXT_MAX,
};

#define OVS_VXLAN_EXT_MAX (__OVS_VXLAN_EXT_MAX - 1)


/* OVS_VPORT_ATTR_OPTIONS attributes for tunnels.
 */
enum {
	OVS_TUNNEL_ATTR_UNSPEC,
	OVS_TUNNEL_ATTR_DST_PORT, /* 16-bit UDP port, used by L4 tunnels. */
	OVS_TUNNEL_ATTR_EXTENSION,
	__OVS_TUNNEL_ATTR_MAX
};

#define OVS_TUNNEL_ATTR_MAX (__OVS_TUNNEL_ATTR_MAX - 1)

/* Flows. */

#define OVS_FLOW_FAMILY  "ovs_flow"
#define OVS_FLOW_MCGROUP "ovs_flow"
#define OVS_FLOW_VERSION 0x1

enum ovs_flow_cmd {
	OVS_FLOW_CMD_UNSPEC,
	OVS_FLOW_CMD_NEW,
	OVS_FLOW_CMD_DEL,
	OVS_FLOW_CMD_GET,
	OVS_FLOW_CMD_SET
};

struct ovs_flow_stats {
	__u64 n_packets;         /* Number of matched packets. */
	__u64 n_bytes;           /* Number of matched bytes. */
};

enum ovs_key_attr {
	OVS_KEY_ATTR_UNSPEC,
	OVS_KEY_ATTR_ENCAP,	/* Nested set of encapsulated attributes. */
	OVS_KEY_ATTR_PRIORITY,  /* u32 skb->priority */
	OVS_KEY_ATTR_IN_PORT,   /* u32 OVS dp port number */
	OVS_KEY_ATTR_ETHERNET,  /* struct ovs_key_ethernet */
	OVS_KEY_ATTR_VLAN,	/* be16 VLAN TCI */
	OVS_KEY_ATTR_ETHERTYPE,	/* be16 Ethernet type */
	OVS_KEY_ATTR_IPV4,      /* struct ovs_key_ipv4 */
	OVS_KEY_ATTR_IPV6,      /* struct ovs_key_ipv6 */
	OVS_KEY_ATTR_TCP,       /* struct ovs_key_tcp */
	OVS_KEY_ATTR_UDP,       /* struct ovs_key_udp */
	OVS_KEY_ATTR_ICMP,      /* struct ovs_key_icmp */
	OVS_KEY_ATTR_ICMPV6,    /* struct ovs_key_icmpv6 */
	OVS_KEY_ATTR_ARP,       /* struct ovs_key_arp */
	OVS_KEY_ATTR_ND,        /* struct ovs_key_nd */
	OVS_KEY_ATTR_SKB_MARK,  /* u32 skb mark */
	OVS_KEY_ATTR_TUNNEL,    /* Nested set of ovs_tunnel attributes */
	OVS_KEY_ATTR_SCTP,      /* struct ovs_key_sctp */
	OVS_KEY_ATTR_TCP_FLAGS,	/* be16 TCP flags. */
	OVS_KEY_ATTR_DP_HASH,      /* u32 hash value. Value 0 indicates the hash
				   is not computed by the datapath. */
	OVS_KEY_ATTR_RECIRC_ID, /* u32 recirc id */
	OVS_KEY_ATTR_MPLS,      /* array of struct ovs_key_mpls.
				 * The implementation may restrict
				 * the accepted length of the array. */
	OVS_KEY_ATTR_CT_STATE,	/* u32 bitmask of OVS_CS_F_* */
	OVS_KEY_ATTR_CT_ZONE,	/* u16 connection tracking zone. */
	OVS_KEY_ATTR_CT_MARK,	/* u32 connection tracking mark */
	OVS_KEY_ATTR_CT_LABELS,	/* 16-octet connection tracking label */

#ifdef __KERNEL__
	OVS_KEY_ATTR_TUNNEL_INFO,  /* struct ip_tunnel_info */
#endif
	__OVS_KEY_ATTR_MAX
};

#define OVS_KEY_ATTR_MAX (__OVS_KEY_ATTR_MAX - 1)

enum ovs_tunnel_key_attr {
	OVS_TUNNEL_KEY_ATTR_ID,                 /* be64 Tunnel ID */
	OVS_TUNNEL_KEY_ATTR_IPV4_SRC,           /* be32 src IP address. */
	OVS_TUNNEL_KEY_ATTR_IPV4_DST,           /* be32 dst IP address. */
	OVS_TUNNEL_KEY_ATTR_TOS,                /* u8 Tunnel IP ToS. */
	OVS_TUNNEL_KEY_ATTR_TTL,                /* u8 Tunnel IP TTL. */
	OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT,      /* No argument, set DF. */
	OVS_TUNNEL_KEY_ATTR_CSUM,               /* No argument. CSUM packet. */
	OVS_TUNNEL_KEY_ATTR_OAM,                /* No argument. OAM frame.  */
	OVS_TUNNEL_KEY_ATTR_GENEVE_OPTS,        /* Array of Geneve options. */
	OVS_TUNNEL_KEY_ATTR_TP_SRC,		/* be16 src Transport Port. */
	OVS_TUNNEL_KEY_ATTR_TP_DST,		/* be16 dst Transport Port. */
	OVS_TUNNEL_KEY_ATTR_VXLAN_OPTS,		/* Nested OVS_VXLAN_EXT_* */
	OVS_TUNNEL_KEY_ATTR_IPV6_SRC,		/* struct in6_addr src IPv6 address. */
	OVS_TUNNEL_KEY_ATTR_IPV6_DST,		/* struct in6_addr dst IPv6 address. */
	OVS_TUNNEL_KEY_ATTR_PAD,
	__OVS_TUNNEL_KEY_ATTR_MAX
};

#define OVS_TUNNEL_KEY_ATTR_MAX (__OVS_TUNNEL_KEY_ATTR_MAX - 1)

/**
 * enum ovs_frag_type - IPv4 and IPv6 fragment type
 * @OVS_FRAG_TYPE_NONE: Packet is not a fragment.
 * @OVS_FRAG_TYPE_FIRST: Packet is a fragment with offset 0.
 * @OVS_FRAG_TYPE_LATER: Packet is a fragment with nonzero offset.
 *
 * Used as the @ipv4_frag in &struct ovs_key_ipv4 and as @ipv6_frag &struct
 * ovs_key_ipv6.
 */
enum ovs_frag_type {
	OVS_FRAG_TYPE_NONE,
	OVS_FRAG_TYPE_FIRST,
	OVS_FRAG_TYPE_LATER,
	__OVS_FRAG_TYPE_MAX
};

#define OVS_FRAG_TYPE_MAX (__OVS_FRAG_TYPE_MAX - 1)

struct ovs_key_ethernet {
	__u8	 eth_src[ETH_ALEN];
	__u8	 eth_dst[ETH_ALEN];
};

struct ovs_key_mpls {
	__be32 mpls_lse;
};

struct ovs_key_ipv4 {
	__be32 ipv4_src;
	__be32 ipv4_dst;
	__u8   ipv4_proto;
	__u8   ipv4_tos;
	__u8   ipv4_ttl;
	__u8   ipv4_frag;	/* One of OVS_FRAG_TYPE_*. */
};

struct ovs_key_ipv6 {
	__be32 ipv6_src[4];
	__be32 ipv6_dst[4];
	__be32 ipv6_label;	/* 20-bits in least-significant bits. */
	__u8   ipv6_proto;
	__u8   ipv6_tclass;
	__u8   ipv6_hlimit;
	__u8   ipv6_frag;	/* One of OVS_FRAG_TYPE_*. */
};

struct ovs_key_tcp {
	__be16 tcp_src;
	__be16 tcp_dst;
};

struct ovs_key_udp {
	__be16 udp_src;
	__be16 udp_dst;
};

struct ovs_key_sctp {
	__be16 sctp_src;
	__be16 sctp_dst;
};

struct ovs_key_icmp {
	__u8 icmp_type;
	__u8 icmp_code;
};

struct ovs_key_icmpv6 {
	__u8 icmpv6_type;
	__u8 icmpv6_code;
};

struct ovs_key_arp {
	__be32 arp_sip;
	__be32 arp_tip;
	__be16 arp_op;
	__u8   arp_sha[ETH_ALEN];
	__u8   arp_tha[ETH_ALEN];
};

struct ovs_key_nd {
	__be32	nd_target[4];
	__u8	nd_sll[ETH_ALEN];
	__u8	nd_tll[ETH_ALEN];
};

#define OVS_CT_LABELS_LEN	16
struct ovs_key_ct_labels {
	__u8	ct_labels[OVS_CT_LABELS_LEN];
};

/* OVS_KEY_ATTR_CT_STATE flags */
#define OVS_CS_F_NEW               0x01 /* Beginning of a new connection. */
#define OVS_CS_F_ESTABLISHED       0x02 /* Part of an existing connection. */
#define OVS_CS_F_RELATED           0x04 /* Related to an established
					 * connection. */
#define OVS_CS_F_REPLY_DIR         0x08 /* Flow is in the reply direction. */
#define OVS_CS_F_INVALID           0x10 /* Could not track connection. */
#define OVS_CS_F_TRACKED           0x20 /* Conntrack has occurred. */
#define OVS_CS_F_SRC_NAT           0x40 /* Packet's source address/port was
					 * mangled by NAT.
					 */
#define OVS_CS_F_DST_NAT           0x80 /* Packet's destination address/port
					 * was mangled by NAT.
					 */

#define OVS_CS_F_NAT_MASK (OVS_CS_F_SRC_NAT | OVS_CS_F_DST_NAT)

/**
 * enum ovs_flow_attr - attributes for %OVS_FLOW_* commands.
 * @OVS_FLOW_ATTR_KEY: Nested %OVS_KEY_ATTR_* attributes specifying the flow
 * key.  Always present in notifications.  Required for all requests (except
 * dumps).
 * @OVS_FLOW_ATTR_ACTIONS: Nested %OVS_ACTION_ATTR_* attributes specifying
 * the actions to take for packets that match the key.  Always present in
 * notifications.  Required for %OVS_FLOW_CMD_NEW requests, optional for
 * %OVS_FLOW_CMD_SET requests.  An %OVS_FLOW_CMD_SET without
 * %OVS_FLOW_ATTR_ACTIONS will not modify the actions.  To clear the actions,
 * an %OVS_FLOW_ATTR_ACTIONS without any nested attributes must be given.
 * @OVS_FLOW_ATTR_STATS: &struct ovs_flow_stats giving statistics for this
 * flow.  Present in notifications if the stats would be nonzero.  Ignored in
 * requests.
 * @OVS_FLOW_ATTR_TCP_FLAGS: An 8-bit value giving the OR'd value of all of the
 * TCP flags seen on packets in this flow.  Only present in notifications for
 * TCP flows, and only if it would be nonzero.  Ignored in requests.
 * @OVS_FLOW_ATTR_USED: A 64-bit integer giving the time, in milliseconds on
 * the system monotonic clock, at which a packet was last processed for this
 * flow.  Only present in notifications if a packet has been processed for this
 * flow.  Ignored in requests.
 * @OVS_FLOW_ATTR_CLEAR: If present in a %OVS_FLOW_CMD_SET request, clears the
 * last-used time, accumulated TCP flags, and statistics for this flow.
 * Otherwise ignored in requests.  Never present in notifications.
 * @OVS_FLOW_ATTR_MASK: Nested %OVS_KEY_ATTR_* attributes specifying the
 * mask bits for wildcarded flow match. Mask bit value '1' specifies exact
 * match with corresponding flow key bit, while mask bit value '0' specifies
 * a wildcarded match. Omitting attribute is treated as wildcarding all
 * corresponding fields. Optional for all requests. If not present,
 * all flow key bits are exact match bits.
 * @OVS_FLOW_ATTR_UFID: A value between 1-16 octets specifying a unique
 * identifier for the flow. Causes the flow to be indexed by this value rather
 * than the value of the %OVS_FLOW_ATTR_KEY attribute. Optional for all
 * requests. Present in notifications if the flow was created with this
 * attribute.
 * @OVS_FLOW_ATTR_UFID_FLAGS: A 32-bit value of OR'd %OVS_UFID_F_*
 * flags that provide alternative semantics for flow installation and
 * retrieval. Optional for all requests.
 *
 * These attributes follow the &struct ovs_header within the Generic Netlink
 * payload for %OVS_FLOW_* commands.
 */
enum ovs_flow_attr {
	OVS_FLOW_ATTR_UNSPEC,
	OVS_FLOW_ATTR_KEY,       /* Sequence of OVS_KEY_ATTR_* attributes. */
	OVS_FLOW_ATTR_ACTIONS,   /* Nested OVS_ACTION_ATTR_* attributes. */
	OVS_FLOW_ATTR_STATS,     /* struct ovs_flow_stats. */
	OVS_FLOW_ATTR_TCP_FLAGS, /* 8-bit OR'd TCP flags. */
	OVS_FLOW_ATTR_USED,      /* u64 msecs last used in monotonic time. */
	OVS_FLOW_ATTR_CLEAR,     /* Flag to clear stats, tcp_flags, used. */
	OVS_FLOW_ATTR_MASK,      /* Sequence of OVS_KEY_ATTR_* attributes. */
	OVS_FLOW_ATTR_PROBE,     /* Flow operation is a feature probe, error
				  * logging should be suppressed. */
	OVS_FLOW_ATTR_UFID,      /* Variable length unique flow identifier. */
	OVS_FLOW_ATTR_UFID_FLAGS,/* u32 of OVS_UFID_F_*. */
	OVS_FLOW_ATTR_PAD,
	__OVS_FLOW_ATTR_MAX
};

#define OVS_FLOW_ATTR_MAX (__OVS_FLOW_ATTR_MAX - 1)

/**
 * Omit attributes for notifications.
 *
 * If a datapath request contains an %OVS_UFID_F_OMIT_* flag, then the datapath
 * may omit the corresponding %OVS_FLOW_ATTR_* from the response.
 */
#define OVS_UFID_F_OMIT_KEY      (1 << 0)
#define OVS_UFID_F_OMIT_MASK     (1 << 1)
#define OVS_UFID_F_OMIT_ACTIONS  (1 << 2)

/**
 * enum ovs_sample_attr - Attributes for %OVS_ACTION_ATTR_SAMPLE action.
 * @OVS_SAMPLE_ATTR_PROBABILITY: 32-bit fraction of packets to sample with
 * @OVS_ACTION_ATTR_SAMPLE.  A value of 0 samples no packets, a value of
 * %UINT32_MAX samples all packets and intermediate values sample intermediate
 * fractions of packets.
 * @OVS_SAMPLE_ATTR_ACTIONS: Set of actions to execute in sampling event.
 * Actions are passed as nested attributes.
 *
 * Executes the specified actions with the given probability on a per-packet
 * basis.
 */
enum ovs_sample_attr {
	OVS_SAMPLE_ATTR_UNSPEC,
	OVS_SAMPLE_ATTR_PROBABILITY, /* u32 number */
	OVS_SAMPLE_ATTR_ACTIONS,     /* Nested OVS_ACTION_ATTR_* attributes. */
	__OVS_SAMPLE_ATTR_MAX,
};

#define OVS_SAMPLE_ATTR_MAX (__OVS_SAMPLE_ATTR_MAX - 1)

/**
 * enum ovs_userspace_attr - Attributes for %OVS_ACTION_ATTR_USERSPACE action.
 * @OVS_USERSPACE_ATTR_PID: u32 Netlink PID to which the %OVS_PACKET_CMD_ACTION
 * message should be sent.  Required.
 * @OVS_USERSPACE_ATTR_USERDATA: If present, its variable-length argument is
 * copied to the %OVS_PACKET_CMD_ACTION message as %OVS_PACKET_ATTR_USERDATA.
 * @OVS_USERSPACE_ATTR_EGRESS_TUN_PORT: If present, u32 output port to get
 * tunnel info.
 * @OVS_USERSPACE_ATTR_ACTIONS: If present, send actions with upcall.
 */
enum ovs_userspace_attr {
	OVS_USERSPACE_ATTR_UNSPEC,
	OVS_USERSPACE_ATTR_PID,	      /* u32 Netlink PID to receive upcalls. */
	OVS_USERSPACE_ATTR_USERDATA,  /* Optional user-specified cookie. */
	OVS_USERSPACE_ATTR_EGRESS_TUN_PORT,  /* Optional, u32 output port
					      * to get tunnel info. */
	OVS_USERSPACE_ATTR_ACTIONS,   /* Optional flag to get actions. */
	__OVS_USERSPACE_ATTR_MAX
};

#define OVS_USERSPACE_ATTR_MAX (__OVS_USERSPACE_ATTR_MAX - 1)

struct ovs_action_trunc {
	uint32_t max_len; /* Max packet size in bytes. */
};

/**
 * struct ovs_action_push_mpls - %OVS_ACTION_ATTR_PUSH_MPLS action argument.
 * @mpls_lse: MPLS label stack entry to push.
 * @mpls_ethertype: Ethertype to set in the encapsulating ethernet frame.
 *
 * The only values @mpls_ethertype should ever be given are %ETH_P_MPLS_UC and
 * %ETH_P_MPLS_MC, indicating MPLS unicast or multicast. Other are rejected.
 */
struct ovs_action_push_mpls {
	__be32 mpls_lse;
	__be16 mpls_ethertype; /* Either %ETH_P_MPLS_UC or %ETH_P_MPLS_MC */
};

/**
 * struct ovs_action_push_vlan - %OVS_ACTION_ATTR_PUSH_VLAN action argument.
 * @vlan_tpid: Tag protocol identifier (TPID) to push.
 * @vlan_tci: Tag control identifier (TCI) to push.  The CFI bit must be set
 * (but it will not be set in the 802.1Q header that is pushed).
 *
 * The @vlan_tpid value is typically %ETH_P_8021Q.  The only acceptable TPID
 * values are those that the kernel module also parses as 802.1Q headers, to
 * prevent %OVS_ACTION_ATTR_PUSH_VLAN followed by %OVS_ACTION_ATTR_POP_VLAN
 * from having surprising results.
 */
struct ovs_action_push_vlan {
	__be16 vlan_tpid;	/* 802.1Q TPID. */
	__be16 vlan_tci;	/* 802.1Q TCI (VLAN ID and priority). */
};

/* Data path hash algorithm for computing Datapath hash.
 *
 * The algorithm type only specifies the fields in a flow
 * will be used as part of the hash. Each datapath is free
 * to use its own hash algorithm. The hash value will be
 * opaque to the user space daemon.
 */
enum ovs_hash_alg {
	OVS_HASH_ALG_L4,
};

/*
 * struct ovs_action_hash - %OVS_ACTION_ATTR_HASH action argument.
 * @hash_alg: Algorithm used to compute hash prior to recirculation.
 * @hash_basis: basis used for computing hash.
 */
struct ovs_action_hash {
	__u32  hash_alg;     /* One of ovs_hash_alg. */
	__u32  hash_basis;
};

/**
 * enum ovs_ct_attr - Attributes for %OVS_ACTION_ATTR_CT action.
 * @OVS_CT_ATTR_COMMIT: If present, commits the connection to the conntrack
 * table. This allows future packets for the same connection to be identified
 * as 'established' or 'related'. The flow key for the current packet will
 * retain the pre-commit connection state.
 * @OVS_CT_ATTR_ZONE: u16 connection tracking zone.
 * @OVS_CT_ATTR_MARK: u32 value followed by u32 mask. For each bit set in the
 * mask, the corresponding bit in the value is copied to the connection
 * tracking mark field in the connection.
 * @OVS_CT_ATTR_LABELS: %OVS_CT_LABELS_LEN value followed by %OVS_CT_LABELS_LEN
 * mask. For each bit set in the mask, the corresponding bit in the value is
 * copied to the connection tracking label field in the connection.
 * @OVS_CT_ATTR_HELPER: variable length string defining conntrack ALG.
 * @OVS_CT_ATTR_NAT: Nested OVS_NAT_ATTR_* for performing L3 network address
 * translation (NAT) on the packet.
 */
enum ovs_ct_attr {
	OVS_CT_ATTR_UNSPEC,
	OVS_CT_ATTR_COMMIT,     /* No argument, commits connection. */
	OVS_CT_ATTR_ZONE,       /* u16 zone id. */
	OVS_CT_ATTR_MARK,       /* mark to associate with this connection. */
	OVS_CT_ATTR_LABELS,     /* labels to associate with this connection. */
	OVS_CT_ATTR_HELPER,     /* netlink helper to assist detection of
				   related connections. */
	OVS_CT_ATTR_NAT,        /* Nested OVS_NAT_ATTR_* */
	__OVS_CT_ATTR_MAX
};

#define OVS_CT_ATTR_MAX (__OVS_CT_ATTR_MAX - 1)

/**
 * enum ovs_nat_attr - Attributes for %OVS_CT_ATTR_NAT.
 *
 * @OVS_NAT_ATTR_SRC: Flag for Source NAT (mangle source address/port).
 * @OVS_NAT_ATTR_DST: Flag for Destination NAT (mangle destination
 * address/port).  Only one of (@OVS_NAT_ATTR_SRC, @OVS_NAT_ATTR_DST) may be
 * specified.  Effective only for packets for ct_state NEW connections.
 * Packets of committed connections are mangled by the NAT action according to
 * the committed NAT type regardless of the flags specified.  As a corollary, a
 * NAT action without a NAT type flag will only mangle packets of committed
 * connections.  The following NAT attributes only apply for NEW
 * (non-committed) connections, and they may be included only when the CT
 * action has the @OVS_CT_ATTR_COMMIT flag and either @OVS_NAT_ATTR_SRC or
 * @OVS_NAT_ATTR_DST is also included.
 * @OVS_NAT_ATTR_IP_MIN: struct in_addr or struct in6_addr
 * @OVS_NAT_ATTR_IP_MAX: struct in_addr or struct in6_addr
 * @OVS_NAT_ATTR_PROTO_MIN: u16 L4 protocol specific lower boundary (port)
 * @OVS_NAT_ATTR_PROTO_MAX: u16 L4 protocol specific upper boundary (port)
 * @OVS_NAT_ATTR_PERSISTENT: Flag for persistent IP mapping across reboots
 * @OVS_NAT_ATTR_PROTO_HASH: Flag for pseudo random L4 port mapping (MD5)
 * @OVS_NAT_ATTR_PROTO_RANDOM: Flag for fully randomized L4 port mapping
 */
enum ovs_nat_attr {
	OVS_NAT_ATTR_UNSPEC,
	OVS_NAT_ATTR_SRC,
	OVS_NAT_ATTR_DST,
	OVS_NAT_ATTR_IP_MIN,
	OVS_NAT_ATTR_IP_MAX,
	OVS_NAT_ATTR_PROTO_MIN,
	OVS_NAT_ATTR_PROTO_MAX,
	OVS_NAT_ATTR_PERSISTENT,
	OVS_NAT_ATTR_PROTO_HASH,
	OVS_NAT_ATTR_PROTO_RANDOM,
	__OVS_NAT_ATTR_MAX,
};

#define OVS_NAT_ATTR_MAX (__OVS_NAT_ATTR_MAX - 1)

/**
 * enum ovs_action_attr - Action types.
 *
 * @OVS_ACTION_ATTR_OUTPUT: Output packet to port.
 * @OVS_ACTION_ATTR_TRUNC: Output packet to port with truncated packet size.
 * @OVS_ACTION_ATTR_USERSPACE: Send packet to userspace according to nested
 * %OVS_USERSPACE_ATTR_* attributes.
 * @OVS_ACTION_ATTR_SET: Replaces the contents of an existing header.  The
 * single nested %OVS_KEY_ATTR_* attribute specifies a header to modify and its
 * value.
 * @OVS_ACTION_ATTR_SET_MASKED: Replaces the contents of an existing header.  A
 * nested %OVS_KEY_ATTR_* attribute specifies a header to modify, its value,
 * and a mask.  For every bit set in the mask, the corresponding bit value
 * is copied from the value to the packet header field, rest of the bits are
 * left unchanged.  The non-masked value bits must be passed in as zeroes.
 * Masking is not supported for the %OVS_KEY_ATTR_TUNNEL attribute.
 * @OVS_ACTION_ATTR_PUSH_VLAN: Push a new outermost 802.1Q header onto the
 * packet.
 * @OVS_ACTION_ATTR_POP_VLAN: Pop the outermost 802.1Q header off the packet.
 * @OVS_ACTION_ATTR_SAMPLE: Probabilitically executes actions, as specified in
 * the nested %OVS_SAMPLE_ATTR_* attributes.
 * @OVS_ACTION_ATTR_PUSH_MPLS: Push a new MPLS label stack entry onto the
 * top of the packets MPLS label stack.  Set the ethertype of the
 * encapsulating frame to either %ETH_P_MPLS_UC or %ETH_P_MPLS_MC to
 * indicate the new packet contents.
 * @OVS_ACTION_ATTR_POP_MPLS: Pop an MPLS label stack entry off of the
 * packet's MPLS label stack.  Set the encapsulating frame's ethertype to
 * indicate the new packet contents. This could potentially still be
 * %ETH_P_MPLS if the resulting MPLS label stack is not empty.  If there
 * is no MPLS label stack, as determined by ethertype, no action is taken.
 * @OVS_ACTION_ATTR_CT: Track the connection. Populate the conntrack-related
 * entries in the flow key.
 *
 * Only a single header can be set with a single %OVS_ACTION_ATTR_SET.  Not all
 * fields within a header are modifiable, e.g. the IPv4 protocol and fragment
 * type may not be changed.
 *
 * @OVS_ACTION_ATTR_SET_TO_MASKED: Kernel internal masked set action translated
 * from the @OVS_ACTION_ATTR_SET.
 */

enum ovs_action_attr {
	OVS_ACTION_ATTR_UNSPEC,
	OVS_ACTION_ATTR_OUTPUT,	      /* u32 port number. */
	OVS_ACTION_ATTR_USERSPACE,    /* Nested OVS_USERSPACE_ATTR_*. */
	OVS_ACTION_ATTR_SET,          /* One nested OVS_KEY_ATTR_*. */
	OVS_ACTION_ATTR_PUSH_VLAN,    /* struct ovs_action_push_vlan. */
	OVS_ACTION_ATTR_POP_VLAN,     /* No argument. */
	OVS_ACTION_ATTR_SAMPLE,       /* Nested OVS_SAMPLE_ATTR_*. */
	OVS_ACTION_ATTR_RECIRC,       /* u32 recirc_id. */
	OVS_ACTION_ATTR_HASH,	      /* struct ovs_action_hash. */
	OVS_ACTION_ATTR_PUSH_MPLS,    /* struct ovs_action_push_mpls. */
	OVS_ACTION_ATTR_POP_MPLS,     /* __be16 ethertype. */
	OVS_ACTION_ATTR_SET_MASKED,   /* One nested OVS_KEY_ATTR_* including
				       * data immediately followed by a mask.
				       * The data must be zero for the unmasked
				       * bits. */
	OVS_ACTION_ATTR_CT,           /* Nested OVS_CT_ATTR_* . */
	OVS_ACTION_ATTR_TRUNC,        /* u32 struct ovs_action_trunc. */

	__OVS_ACTION_ATTR_MAX,	      /* Nothing past this will be accepted
				       * from userspace. */

#ifdef __KERNEL__
	OVS_ACTION_ATTR_SET_TO_MASKED, /* Kernel module internal masked
					* set action converted from
					* OVS_ACTION_ATTR_SET. */
#endif
};

#define OVS_ACTION_ATTR_MAX (__OVS_ACTION_ATTR_MAX - 1)

#endif /* _LINUX_OPENVSWITCH_H */
