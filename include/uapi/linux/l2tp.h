/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * L2TP-over-IP socket for L2TPv3.
 *
 * Author: James Chapman <jchapman@katalix.com>
 */

#ifndef _UAPI_LINUX_L2TP_H_
#define _UAPI_LINUX_L2TP_H_

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>

#define IPPROTO_L2TP		115

/**
 * struct sockaddr_l2tpip - the sockaddr structure for L2TP-over-IP sockets
 * @l2tp_family:  address family number AF_L2TPIP.
 * @l2tp_addr:    protocol specific address information
 * @l2tp_conn_id: connection id of tunnel
 */
#define __SOCK_SIZE__	16		/* sizeof(struct sockaddr)	*/
struct sockaddr_l2tpip {
	/* The first fields must match struct sockaddr_in */
	__kernel_sa_family_t l2tp_family; /* AF_INET */
	__be16		l2tp_unused;	/* INET port number (unused) */
	struct in_addr	l2tp_addr;	/* Internet address */

	__u32		l2tp_conn_id;	/* Connection ID of tunnel */

	/* Pad to size of `struct sockaddr'. */
	unsigned char	__pad[__SOCK_SIZE__ -
			      sizeof(__kernel_sa_family_t) -
			      sizeof(__be16) - sizeof(struct in_addr) -
			      sizeof(__u32)];
};

/**
 * struct sockaddr_l2tpip6 - the sockaddr structure for L2TP-over-IPv6 sockets
 * @l2tp_family:  address family number AF_L2TPIP.
 * @l2tp_addr:    protocol specific address information
 * @l2tp_conn_id: connection id of tunnel
 */
struct sockaddr_l2tpip6 {
	/* The first fields must match struct sockaddr_in6 */
	__kernel_sa_family_t l2tp_family; /* AF_INET6 */
	__be16		l2tp_unused;	/* INET port number (unused) */
	__be32		l2tp_flowinfo;	/* IPv6 flow information */
	struct in6_addr	l2tp_addr;	/* IPv6 address */
	__u32		l2tp_scope_id;	/* scope id (new in RFC2553) */
	__u32		l2tp_conn_id;	/* Connection ID of tunnel */
};

/*****************************************************************************
 *  NETLINK_GENERIC netlink family.
 *****************************************************************************/

/*
 * Commands.
 * Valid TLVs of each command are:-
 * TUNNEL_CREATE	- CONN_ID, pw_type, netns, ifname, ipinfo, udpinfo, udpcsum, vlanid
 * TUNNEL_DELETE	- CONN_ID
 * TUNNEL_MODIFY	- CONN_ID, udpcsum
 * TUNNEL_GETSTATS	- CONN_ID, (stats)
 * TUNNEL_GET		- CONN_ID, (...)
 * SESSION_CREATE	- SESSION_ID, PW_TYPE, offset, data_seq, cookie, peer_cookie, offset, l2spec
 * SESSION_DELETE	- SESSION_ID
 * SESSION_MODIFY	- SESSION_ID, data_seq
 * SESSION_GET		- SESSION_ID, (...)
 * SESSION_GETSTATS	- SESSION_ID, (stats)
 *
 */
enum {
	L2TP_CMD_NOOP,
	L2TP_CMD_TUNNEL_CREATE,
	L2TP_CMD_TUNNEL_DELETE,
	L2TP_CMD_TUNNEL_MODIFY,
	L2TP_CMD_TUNNEL_GET,
	L2TP_CMD_SESSION_CREATE,
	L2TP_CMD_SESSION_DELETE,
	L2TP_CMD_SESSION_MODIFY,
	L2TP_CMD_SESSION_GET,
	__L2TP_CMD_MAX,
};

#define L2TP_CMD_MAX			(__L2TP_CMD_MAX - 1)

/*
 * ATTR types defined for L2TP
 */
enum {
	L2TP_ATTR_NONE,			/* no data */
	L2TP_ATTR_PW_TYPE,		/* u16, enum l2tp_pwtype */
	L2TP_ATTR_ENCAP_TYPE,		/* u16, enum l2tp_encap_type */
	L2TP_ATTR_OFFSET,		/* u16 (not used) */
	L2TP_ATTR_DATA_SEQ,		/* u16 */
	L2TP_ATTR_L2SPEC_TYPE,		/* u8, enum l2tp_l2spec_type */
	L2TP_ATTR_L2SPEC_LEN,		/* u8, enum l2tp_l2spec_type */
	L2TP_ATTR_PROTO_VERSION,	/* u8 */
	L2TP_ATTR_IFNAME,		/* string */
	L2TP_ATTR_CONN_ID,		/* u32 */
	L2TP_ATTR_PEER_CONN_ID,		/* u32 */
	L2TP_ATTR_SESSION_ID,		/* u32 */
	L2TP_ATTR_PEER_SESSION_ID,	/* u32 */
	L2TP_ATTR_UDP_CSUM,		/* u8 */
	L2TP_ATTR_VLAN_ID,		/* u16 */
	L2TP_ATTR_COOKIE,		/* 0, 4 or 8 bytes */
	L2TP_ATTR_PEER_COOKIE,		/* 0, 4 or 8 bytes */
	L2TP_ATTR_DEBUG,		/* u32, enum l2tp_debug_flags */
	L2TP_ATTR_RECV_SEQ,		/* u8 */
	L2TP_ATTR_SEND_SEQ,		/* u8 */
	L2TP_ATTR_LNS_MODE,		/* u8 */
	L2TP_ATTR_USING_IPSEC,		/* u8 */
	L2TP_ATTR_RECV_TIMEOUT,		/* msec */
	L2TP_ATTR_FD,			/* int */
	L2TP_ATTR_IP_SADDR,		/* u32 */
	L2TP_ATTR_IP_DADDR,		/* u32 */
	L2TP_ATTR_UDP_SPORT,		/* u16 */
	L2TP_ATTR_UDP_DPORT,		/* u16 */
	L2TP_ATTR_MTU,			/* u16 */
	L2TP_ATTR_MRU,			/* u16 */
	L2TP_ATTR_STATS,		/* nested */
	L2TP_ATTR_IP6_SADDR,		/* struct in6_addr */
	L2TP_ATTR_IP6_DADDR,		/* struct in6_addr */
	L2TP_ATTR_UDP_ZERO_CSUM6_TX,	/* flag */
	L2TP_ATTR_UDP_ZERO_CSUM6_RX,	/* flag */
	L2TP_ATTR_PAD,
	__L2TP_ATTR_MAX,
};

#define L2TP_ATTR_MAX			(__L2TP_ATTR_MAX - 1)

/* Nested in L2TP_ATTR_STATS */
enum {
	L2TP_ATTR_STATS_NONE,		/* no data */
	L2TP_ATTR_TX_PACKETS,		/* u64 */
	L2TP_ATTR_TX_BYTES,		/* u64 */
	L2TP_ATTR_TX_ERRORS,		/* u64 */
	L2TP_ATTR_RX_PACKETS,		/* u64 */
	L2TP_ATTR_RX_BYTES,		/* u64 */
	L2TP_ATTR_RX_SEQ_DISCARDS,	/* u64 */
	L2TP_ATTR_RX_OOS_PACKETS,	/* u64 */
	L2TP_ATTR_RX_ERRORS,		/* u64 */
	L2TP_ATTR_STATS_PAD,
	__L2TP_ATTR_STATS_MAX,
};

#define L2TP_ATTR_STATS_MAX		(__L2TP_ATTR_STATS_MAX - 1)

enum l2tp_pwtype {
	L2TP_PWTYPE_NONE = 0x0000,
	L2TP_PWTYPE_ETH_VLAN = 0x0004,
	L2TP_PWTYPE_ETH = 0x0005,
	L2TP_PWTYPE_PPP = 0x0007,
	L2TP_PWTYPE_PPP_AC = 0x0008,
	L2TP_PWTYPE_IP = 0x000b,
	__L2TP_PWTYPE_MAX
};

enum l2tp_l2spec_type {
	L2TP_L2SPECTYPE_NONE,
	L2TP_L2SPECTYPE_DEFAULT,
};

enum l2tp_encap_type {
	L2TP_ENCAPTYPE_UDP,
	L2TP_ENCAPTYPE_IP,
};

enum l2tp_seqmode {
	L2TP_SEQ_NONE = 0,
	L2TP_SEQ_IP = 1,
	L2TP_SEQ_ALL = 2,
};

/**
 * enum l2tp_debug_flags - debug message categories for L2TP tunnels/sessions
 *
 * @L2TP_MSG_DEBUG: verbose debug (if compiled in)
 * @L2TP_MSG_CONTROL: userspace - kernel interface
 * @L2TP_MSG_SEQ: sequence numbers
 * @L2TP_MSG_DATA: data packets
 */
enum l2tp_debug_flags {
	L2TP_MSG_DEBUG		= (1 << 0),
	L2TP_MSG_CONTROL	= (1 << 1),
	L2TP_MSG_SEQ		= (1 << 2),
	L2TP_MSG_DATA		= (1 << 3),
};

/*
 * NETLINK_GENERIC related info
 */
#define L2TP_GENL_NAME		"l2tp"
#define L2TP_GENL_VERSION	0x1
#define L2TP_GENL_MCGROUP       "l2tp"

#endif /* _UAPI_LINUX_L2TP_H_ */
