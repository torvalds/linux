/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_LINUX_IF_BRIDGE_H
#define _UAPI_LINUX_IF_BRIDGE_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/in6.h>

#define SYSFS_BRIDGE_ATTR	"bridge"
#define SYSFS_BRIDGE_FDB	"brforward"
#define SYSFS_BRIDGE_PORT_SUBDIR "brif"
#define SYSFS_BRIDGE_PORT_ATTR	"brport"
#define SYSFS_BRIDGE_PORT_LINK	"bridge"

#define BRCTL_VERSION 1

#define BRCTL_GET_VERSION 0
#define BRCTL_GET_BRIDGES 1
#define BRCTL_ADD_BRIDGE 2
#define BRCTL_DEL_BRIDGE 3
#define BRCTL_ADD_IF 4
#define BRCTL_DEL_IF 5
#define BRCTL_GET_BRIDGE_INFO 6
#define BRCTL_GET_PORT_LIST 7
#define BRCTL_SET_BRIDGE_FORWARD_DELAY 8
#define BRCTL_SET_BRIDGE_HELLO_TIME 9
#define BRCTL_SET_BRIDGE_MAX_AGE 10
#define BRCTL_SET_AGEING_TIME 11
#define BRCTL_SET_GC_INTERVAL 12
#define BRCTL_GET_PORT_INFO 13
#define BRCTL_SET_BRIDGE_STP_STATE 14
#define BRCTL_SET_BRIDGE_PRIORITY 15
#define BRCTL_SET_PORT_PRIORITY 16
#define BRCTL_SET_PATH_COST 17
#define BRCTL_GET_FDB_ENTRIES 18

#define BR_STATE_DISABLED 0
#define BR_STATE_LISTENING 1
#define BR_STATE_LEARNING 2
#define BR_STATE_FORWARDING 3
#define BR_STATE_BLOCKING 4

struct __bridge_info {
	__u64 designated_root;
	__u64 bridge_id;
	__u32 root_path_cost;
	__u32 max_age;
	__u32 hello_time;
	__u32 forward_delay;
	__u32 bridge_max_age;
	__u32 bridge_hello_time;
	__u32 bridge_forward_delay;
	__u8 topology_change;
	__u8 topology_change_detected;
	__u8 root_port;
	__u8 stp_enabled;
	__u32 ageing_time;
	__u32 gc_interval;
	__u32 hello_timer_value;
	__u32 tcn_timer_value;
	__u32 topology_change_timer_value;
	__u32 gc_timer_value;
};

struct __port_info {
	__u64 designated_root;
	__u64 designated_bridge;
	__u16 port_id;
	__u16 designated_port;
	__u32 path_cost;
	__u32 designated_cost;
	__u8 state;
	__u8 top_change_ack;
	__u8 config_pending;
	__u8 unused0;
	__u32 message_age_timer_value;
	__u32 forward_delay_timer_value;
	__u32 hold_timer_value;
};

struct __fdb_entry {
	__u8 mac_addr[ETH_ALEN];
	__u8 port_no;
	__u8 is_local;
	__u32 ageing_timer_value;
	__u8 port_hi;
	__u8 pad0;
	__u16 unused;
};

/* Bridge Flags */
#define BRIDGE_FLAGS_MASTER	1	/* Bridge command to/from master */
#define BRIDGE_FLAGS_SELF	2	/* Bridge command to/from lowerdev */

#define BRIDGE_MODE_VEB		0	/* Default loopback mode */
#define BRIDGE_MODE_VEPA	1	/* 802.1Qbg defined VEPA mode */
#define BRIDGE_MODE_UNDEF	0xFFFF  /* mode undefined */

/* Bridge management nested attributes
 * [IFLA_AF_SPEC] = {
 *     [IFLA_BRIDGE_FLAGS]
 *     [IFLA_BRIDGE_MODE]
 *     [IFLA_BRIDGE_VLAN_INFO]
 * }
 */
enum {
	IFLA_BRIDGE_FLAGS,
	IFLA_BRIDGE_MODE,
	IFLA_BRIDGE_VLAN_INFO,
	IFLA_BRIDGE_VLAN_TUNNEL_INFO,
	__IFLA_BRIDGE_MAX,
};
#define IFLA_BRIDGE_MAX (__IFLA_BRIDGE_MAX - 1)

#define BRIDGE_VLAN_INFO_MASTER	(1<<0)	/* Operate on Bridge device as well */
#define BRIDGE_VLAN_INFO_PVID	(1<<1)	/* VLAN is PVID, ingress untagged */
#define BRIDGE_VLAN_INFO_UNTAGGED	(1<<2)	/* VLAN egresses untagged */
#define BRIDGE_VLAN_INFO_RANGE_BEGIN	(1<<3) /* VLAN is start of vlan range */
#define BRIDGE_VLAN_INFO_RANGE_END	(1<<4) /* VLAN is end of vlan range */
#define BRIDGE_VLAN_INFO_BRENTRY	(1<<5) /* Global bridge VLAN entry */

struct bridge_vlan_info {
	__u16 flags;
	__u16 vid;
};

enum {
	IFLA_BRIDGE_VLAN_TUNNEL_UNSPEC,
	IFLA_BRIDGE_VLAN_TUNNEL_ID,
	IFLA_BRIDGE_VLAN_TUNNEL_VID,
	IFLA_BRIDGE_VLAN_TUNNEL_FLAGS,
	__IFLA_BRIDGE_VLAN_TUNNEL_MAX,
};

#define IFLA_BRIDGE_VLAN_TUNNEL_MAX (__IFLA_BRIDGE_VLAN_TUNNEL_MAX - 1)

struct bridge_vlan_xstats {
	__u64 rx_bytes;
	__u64 rx_packets;
	__u64 tx_bytes;
	__u64 tx_packets;
	__u16 vid;
	__u16 flags;
	__u32 pad2;
};

/* Bridge multicast database attributes
 * [MDBA_MDB] = {
 *     [MDBA_MDB_ENTRY] = {
 *         [MDBA_MDB_ENTRY_INFO] {
 *		struct br_mdb_entry
 *		[MDBA_MDB_EATTR attributes]
 *         }
 *     }
 * }
 * [MDBA_ROUTER] = {
 *    [MDBA_ROUTER_PORT] = {
 *        u32 ifindex
 *        [MDBA_ROUTER_PATTR attributes]
 *    }
 * }
 */
enum {
	MDBA_UNSPEC,
	MDBA_MDB,
	MDBA_ROUTER,
	__MDBA_MAX,
};
#define MDBA_MAX (__MDBA_MAX - 1)

enum {
	MDBA_MDB_UNSPEC,
	MDBA_MDB_ENTRY,
	__MDBA_MDB_MAX,
};
#define MDBA_MDB_MAX (__MDBA_MDB_MAX - 1)

enum {
	MDBA_MDB_ENTRY_UNSPEC,
	MDBA_MDB_ENTRY_INFO,
	__MDBA_MDB_ENTRY_MAX,
};
#define MDBA_MDB_ENTRY_MAX (__MDBA_MDB_ENTRY_MAX - 1)

/* per mdb entry additional attributes */
enum {
	MDBA_MDB_EATTR_UNSPEC,
	MDBA_MDB_EATTR_TIMER,
	__MDBA_MDB_EATTR_MAX
};
#define MDBA_MDB_EATTR_MAX (__MDBA_MDB_EATTR_MAX - 1)

/* multicast router types */
enum {
	MDB_RTR_TYPE_DISABLED,
	MDB_RTR_TYPE_TEMP_QUERY,
	MDB_RTR_TYPE_PERM,
	MDB_RTR_TYPE_TEMP
};

enum {
	MDBA_ROUTER_UNSPEC,
	MDBA_ROUTER_PORT,
	__MDBA_ROUTER_MAX,
};
#define MDBA_ROUTER_MAX (__MDBA_ROUTER_MAX - 1)

/* router port attributes */
enum {
	MDBA_ROUTER_PATTR_UNSPEC,
	MDBA_ROUTER_PATTR_TIMER,
	MDBA_ROUTER_PATTR_TYPE,
	__MDBA_ROUTER_PATTR_MAX
};
#define MDBA_ROUTER_PATTR_MAX (__MDBA_ROUTER_PATTR_MAX - 1)

struct br_port_msg {
	__u8  family;
	__u32 ifindex;
};

struct br_mdb_entry {
	__u32 ifindex;
#define MDB_TEMPORARY 0
#define MDB_PERMANENT 1
	__u8 state;
#define MDB_FLAGS_OFFLOAD	(1 << 0)
	__u8 flags;
	__u16 vid;
	struct {
		union {
			__be32	ip4;
			struct in6_addr ip6;
		} u;
		__be16		proto;
	} addr;
};

enum {
	MDBA_SET_ENTRY_UNSPEC,
	MDBA_SET_ENTRY,
	__MDBA_SET_ENTRY_MAX,
};
#define MDBA_SET_ENTRY_MAX (__MDBA_SET_ENTRY_MAX - 1)

/* Embedded inside LINK_XSTATS_TYPE_BRIDGE */
enum {
	BRIDGE_XSTATS_UNSPEC,
	BRIDGE_XSTATS_VLAN,
	BRIDGE_XSTATS_MCAST,
	BRIDGE_XSTATS_PAD,
	__BRIDGE_XSTATS_MAX
};
#define BRIDGE_XSTATS_MAX (__BRIDGE_XSTATS_MAX - 1)

enum {
	BR_MCAST_DIR_RX,
	BR_MCAST_DIR_TX,
	BR_MCAST_DIR_SIZE
};

/* IGMP/MLD statistics */
struct br_mcast_stats {
	__u64 igmp_v1queries[BR_MCAST_DIR_SIZE];
	__u64 igmp_v2queries[BR_MCAST_DIR_SIZE];
	__u64 igmp_v3queries[BR_MCAST_DIR_SIZE];
	__u64 igmp_leaves[BR_MCAST_DIR_SIZE];
	__u64 igmp_v1reports[BR_MCAST_DIR_SIZE];
	__u64 igmp_v2reports[BR_MCAST_DIR_SIZE];
	__u64 igmp_v3reports[BR_MCAST_DIR_SIZE];
	__u64 igmp_parse_errors;

	__u64 mld_v1queries[BR_MCAST_DIR_SIZE];
	__u64 mld_v2queries[BR_MCAST_DIR_SIZE];
	__u64 mld_leaves[BR_MCAST_DIR_SIZE];
	__u64 mld_v1reports[BR_MCAST_DIR_SIZE];
	__u64 mld_v2reports[BR_MCAST_DIR_SIZE];
	__u64 mld_parse_errors;

	__u64 mcast_bytes[BR_MCAST_DIR_SIZE];
	__u64 mcast_packets[BR_MCAST_DIR_SIZE];
};
#endif /* _UAPI_LINUX_IF_BRIDGE_H */
