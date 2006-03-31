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
	u_int16_t	hw_protocol;	/* hw protocol (network order) */
	u_int8_t	hook;		/* netfilter hook */
	u_int8_t	_pad;
} __attribute__ ((packed));

struct nfulnl_msg_packet_hw {
	u_int16_t	hw_addrlen;
	u_int16_t	_pad;
	u_int8_t	hw_addr[8];
} __attribute__ ((packed));

struct nfulnl_msg_packet_timestamp {
	aligned_u64	sec;
	aligned_u64	usec;
} __attribute__ ((packed));

#define NFULNL_PREFIXLEN	30	/* just like old log target */

enum nfulnl_attr_type {
	NFULA_UNSPEC,
	NFULA_PACKET_HDR,
	NFULA_MARK,			/* u_int32_t nfmark */
	NFULA_TIMESTAMP,		/* nfulnl_msg_packet_timestamp */
	NFULA_IFINDEX_INDEV,		/* u_int32_t ifindex */
	NFULA_IFINDEX_OUTDEV,		/* u_int32_t ifindex */
	NFULA_IFINDEX_PHYSINDEV,	/* u_int32_t ifindex */
	NFULA_IFINDEX_PHYSOUTDEV,	/* u_int32_t ifindex */
	NFULA_HWADDR,			/* nfulnl_msg_packet_hw */
	NFULA_PAYLOAD,			/* opaque data payload */
	NFULA_PREFIX,			/* string prefix */
	NFULA_UID,			/* user id of socket */
	NFULA_SEQ,			/* instance-local sequence number */
	NFULA_SEQ_GLOBAL,		/* global sequence number */

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
	u_int8_t	command;	/* nfulnl_msg_config_cmds */
} __attribute__ ((packed));

struct nfulnl_msg_config_mode {
	u_int32_t	copy_range;
	u_int8_t	copy_mode;
	u_int8_t	_pad;
} __attribute__ ((packed));

enum nfulnl_attr_config {
	NFULA_CFG_UNSPEC,
	NFULA_CFG_CMD,			/* nfulnl_msg_config_cmd */
	NFULA_CFG_MODE,			/* nfulnl_msg_config_mode */
	NFULA_CFG_NLBUFSIZ,		/* u_int32_t buffer size */
	NFULA_CFG_TIMEOUT,		/* u_int32_t in 1/100 s */
	NFULA_CFG_QTHRESH,		/* u_int32_t */
	NFULA_CFG_FLAGS,		/* u_int16_t */
	__NFULA_CFG_MAX
};
#define NFULA_CFG_MAX (__NFULA_CFG_MAX -1)

#define NFULNL_COPY_NONE	0x00
#define NFULNL_COPY_META	0x01
#define NFULNL_COPY_PACKET	0x02

#define NFULNL_CFG_F_SEQ	0x0001
#define NFULNL_CFG_F_SEQ_GLOBAL	0x0002

#endif /* _NFNETLINK_LOG_H */
