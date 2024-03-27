/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PFCP_H_
#define _PFCP_H_

#include <uapi/linux/if_ether.h>
#include <net/dst_metadata.h>
#include <linux/netdevice.h>
#include <uapi/linux/ipv6.h>
#include <net/udp_tunnel.h>
#include <uapi/linux/udp.h>
#include <uapi/linux/ip.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bits.h>

#define PFCP_PORT 8805

/* PFCP protocol header */
struct pfcphdr {
	u8	flags;
	u8	message_type;
	__be16	message_length;
};

/* PFCP header flags */
#define PFCP_SEID_FLAG		BIT(0)
#define PFCP_MP_FLAG		BIT(1)

#define PFCP_VERSION_MASK	GENMASK(4, 0)

#define PFCP_HLEN (sizeof(struct udphdr) + sizeof(struct pfcphdr))

/* PFCP node related messages */
struct pfcphdr_node {
	u8	seq_number[3];
	u8	reserved;
};

/* PFCP session related messages */
struct pfcphdr_session {
	__be64	seid;
	u8	seq_number[3];
#ifdef __LITTLE_ENDIAN_BITFIELD
	u8	message_priority:4,
		reserved:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	reserved:4,
		message_priprity:4;
#else
#error "Please fix <asm/byteorder>"
#endif
};

struct pfcp_metadata {
	u8 type;
	__be64 seid;
} __packed;

enum {
	PFCP_TYPE_NODE		= 0,
	PFCP_TYPE_SESSION	= 1,
};

#define PFCP_HEADROOM (sizeof(struct iphdr) + sizeof(struct udphdr) + \
		       sizeof(struct pfcphdr) + sizeof(struct ethhdr))
#define PFCP6_HEADROOM (sizeof(struct ipv6hdr) + sizeof(struct udphdr) + \
			sizeof(struct pfcphdr) + sizeof(struct ethhdr))

static inline struct pfcphdr *pfcp_hdr(struct sk_buff *skb)
{
	return (struct pfcphdr *)(udp_hdr(skb) + 1);
}

static inline struct pfcphdr_node *pfcp_hdr_node(struct sk_buff *skb)
{
	return (struct pfcphdr_node *)(pfcp_hdr(skb) + 1);
}

static inline struct pfcphdr_session *pfcp_hdr_session(struct sk_buff *skb)
{
	return (struct pfcphdr_session *)(pfcp_hdr(skb) + 1);
}

static inline bool netif_is_pfcp(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "pfcp");
}

#endif
