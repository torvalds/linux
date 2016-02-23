#ifndef _NFNETLINK_LOG_H
#define _NFNETLINK_LOG_H

/* This file describes the netlink messages (i.e. 'protocol packets'),
 * and not any kind of function definitions.  It is shared between kernel and
 * userspace.  Don't put kernel specific stuff in here */

#include <linux/types.h>
#include <linux/netfilter/nfnetlink.h>

enum nfulnl_msg_types {
	NFULNL_MSG_PACKET,		/* packet from kernel to userspace */
	NFULNL_MSG_CONFIG,		/* connect to a particular queue */

	NFULNL_MSG_MAX
};

struct nfulnl_msg_packet_hdr {
	__be16		hw_protocol;	/* hw protocol (network order) */
	__u8	hook;		/* netfilter hook */
	__u8	_pad;
};

struct nfulnl_msg_packet_hw {
	__be16		hw_addrlen;
	__u16	_pad;
	__u8	hw_addr[8];
};

struct nfulnl_msg_packet_timestamp {
	__aligned_be64	sec;
	__aligned_be64	usec;
};

enum nfulnl_attr_type {
	NFULA_UNSPEC,
	NFULA_PACKET_HDR,
	NFULA_MARK,			/* __u32 nfmark */
	NFULA_TIMESTAMP,		/* nfulnl_msg_packet_timestamp */
	NFULA_IFINDEX_INDEV,		/* __u32 ifindex */
	NFULA_IFINDEX_OUTDEV,		/* __u32 ifindex */
	NFULA_IFINDEX_PHYSINDEV,	/* __u32 ifindex */
	NFULA_IFINDEX_PHYSOUTDEV,	/* __u32 ifindex */
	NFULA_HWADDR,			/* nfulnl_msg_packet_hw */
	NFULA_PAYLOAD,			/* opaque data payload */
	NFULA_PREFIX,			/* string prefix */
	NFULA_UID,			/* user id of socket */
	NFULA_SEQ,			/* instance-local sequence number */
	NFULA_SEQ_GLOBAL,		/* global sequence number */
	NFULA_GID,			/* group id of socket */
	NFULA_HWTYPE,			/* hardware type */
	NFULA_HWHEADER,			/* hardware header */
	NFULA_HWLEN,			/* hardware header length */
	NFULA_CT,                       /* nf_conntrack_netlink.h */
	NFULA_CT_INFO,                  /* enum ip_conntrack_info */

	__NFULA_MAX
};
#define NFULA_MAX (__NFULA_MAX - 1)

enum nfulnl_msg_config_cmds {
	NFULNL_CFG_CMD_NONE,
	NFULNL_CFG_CMD_BIND,
	NFULNL_CFG_CMD_UNBIND,
	NFULNL_CFG_CMD_PF_BIND,
	NFULNL_CFG_CMD_PF_UNBIND,
};

struct nfulnl_msg_config_cmd {
	__u8	command;	/* nfulnl_msg_config_cmds */
} __attribute__ ((packed));

struct nfulnl_msg_config_mode {
	__be32		copy_range;
	__u8	copy_mode;
	__u8	_pad;
} __attribute__ ((packed));

enum nfulnl_attr_config {
	NFULA_CFG_UNSPEC,
	NFULA_CFG_CMD,			/* nfulnl_msg_config_cmd */
	NFULA_CFG_MODE,			/* nfulnl_msg_config_mode */
	NFULA_CFG_NLBUFSIZ,		/* __u32 buffer size */
	NFULA_CFG_TIMEOUT,		/* __u32 in 1/100 s */
	NFULA_CFG_QTHRESH,		/* __u32 */
	NFULA_CFG_FLAGS,		/* __u16 */
	__NFULA_CFG_MAX
};
#define NFULA_CFG_MAX (__NFULA_CFG_MAX -1)

#define NFULNL_COPY_NONE	0x00
#define NFULNL_COPY_META	0x01
#define NFULNL_COPY_PACKET	0x02
/* 0xff is reserved, don't use it for new copy modes. */

#define NFULNL_CFG_F_SEQ	0x0001
#define NFULNL_CFG_F_SEQ_GLOBAL	0x0002
#define NFULNL_CFG_F_CONNTRACK	0x0004

#endif /* _NFNETLINK_LOG_H */
