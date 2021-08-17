/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _NFNETLINK_QUEUE_H
#define _NFNETLINK_QUEUE_H

#include <linux/types.h>
#include <linux/netfilter/nfnetlink.h>

enum nfqnl_msg_types {
	NFQNL_MSG_PACKET,		/* packet from kernel to userspace */
	NFQNL_MSG_VERDICT,		/* verdict from userspace to kernel */
	NFQNL_MSG_CONFIG,		/* connect to a particular queue */
	NFQNL_MSG_VERDICT_BATCH,	/* batchv from userspace to kernel */

	NFQNL_MSG_MAX
};

struct nfqnl_msg_packet_hdr {
	__be32		packet_id;	/* unique ID of packet in queue */
	__be16		hw_protocol;	/* hw protocol (network order) */
	__u8	hook;		/* netfilter hook */
} __attribute__ ((packed));

struct nfqnl_msg_packet_hw {
	__be16		hw_addrlen;
	__u16	_pad;
	__u8	hw_addr[8];
};

struct nfqnl_msg_packet_timestamp {
	__aligned_be64	sec;
	__aligned_be64	usec;
};

enum nfqnl_vlan_attr {
	NFQA_VLAN_UNSPEC,
	NFQA_VLAN_PROTO,		/* __be16 skb vlan_proto */
	NFQA_VLAN_TCI,			/* __be16 skb htons(vlan_tci) */
	__NFQA_VLAN_MAX,
};
#define NFQA_VLAN_MAX (__NFQA_VLAN_MAX - 1)

enum nfqnl_attr_type {
	NFQA_UNSPEC,
	NFQA_PACKET_HDR,
	NFQA_VERDICT_HDR,		/* nfqnl_msg_verdict_hrd */
	NFQA_MARK,			/* __u32 nfmark */
	NFQA_TIMESTAMP,			/* nfqnl_msg_packet_timestamp */
	NFQA_IFINDEX_INDEV,		/* __u32 ifindex */
	NFQA_IFINDEX_OUTDEV,		/* __u32 ifindex */
	NFQA_IFINDEX_PHYSINDEV,		/* __u32 ifindex */
	NFQA_IFINDEX_PHYSOUTDEV,	/* __u32 ifindex */
	NFQA_HWADDR,			/* nfqnl_msg_packet_hw */
	NFQA_PAYLOAD,			/* opaque data payload */
	NFQA_CT,			/* nfnetlink_conntrack.h */
	NFQA_CT_INFO,			/* enum ip_conntrack_info */
	NFQA_CAP_LEN,			/* __u32 length of captured packet */
	NFQA_SKB_INFO,			/* __u32 skb meta information */
	NFQA_EXP,			/* nfnetlink_conntrack.h */
	NFQA_UID,			/* __u32 sk uid */
	NFQA_GID,			/* __u32 sk gid */
	NFQA_SECCTX,			/* security context string */
	NFQA_VLAN,			/* nested attribute: packet vlan info */
	NFQA_L2HDR,			/* full L2 header */

	__NFQA_MAX
};
#define NFQA_MAX (__NFQA_MAX - 1)

struct nfqnl_msg_verdict_hdr {
	__be32 verdict;
	__be32 id;
};


enum nfqnl_msg_config_cmds {
	NFQNL_CFG_CMD_NONE,
	NFQNL_CFG_CMD_BIND,
	NFQNL_CFG_CMD_UNBIND,
	NFQNL_CFG_CMD_PF_BIND,
	NFQNL_CFG_CMD_PF_UNBIND,
};

struct nfqnl_msg_config_cmd {
	__u8	command;	/* nfqnl_msg_config_cmds */
	__u8	_pad;
	__be16		pf;		/* AF_xxx for PF_[UN]BIND */
};

enum nfqnl_config_mode {
	NFQNL_COPY_NONE,
	NFQNL_COPY_META,
	NFQNL_COPY_PACKET,
};

struct nfqnl_msg_config_params {
	__be32		copy_range;
	__u8	copy_mode;	/* enum nfqnl_config_mode */
} __attribute__ ((packed));


enum nfqnl_attr_config {
	NFQA_CFG_UNSPEC,
	NFQA_CFG_CMD,			/* nfqnl_msg_config_cmd */
	NFQA_CFG_PARAMS,		/* nfqnl_msg_config_params */
	NFQA_CFG_QUEUE_MAXLEN,		/* __u32 */
	NFQA_CFG_MASK,			/* identify which flags to change */
	NFQA_CFG_FLAGS,			/* value of these flags (__u32) */
	__NFQA_CFG_MAX
};
#define NFQA_CFG_MAX (__NFQA_CFG_MAX-1)

/* Flags for NFQA_CFG_FLAGS */
#define NFQA_CFG_F_FAIL_OPEN			(1 << 0)
#define NFQA_CFG_F_CONNTRACK			(1 << 1)
#define NFQA_CFG_F_GSO				(1 << 2)
#define NFQA_CFG_F_UID_GID			(1 << 3)
#define NFQA_CFG_F_SECCTX			(1 << 4)
#define NFQA_CFG_F_MAX				(1 << 5)

/* flags for NFQA_SKB_INFO */
/* packet appears to have wrong checksums, but they are ok */
#define NFQA_SKB_CSUMNOTREADY (1 << 0)
/* packet is GSO (i.e., exceeds device mtu) */
#define NFQA_SKB_GSO (1 << 1)
/* csum not validated (incoming device doesn't support hw checksum, etc.) */
#define NFQA_SKB_CSUM_NOTVERIFIED (1 << 2)

#endif /* _NFNETLINK_QUEUE_H */
