#ifndef __LINUX_BRIDGE_NETFILTER_H
#define __LINUX_BRIDGE_NETFILTER_H

#include <uapi/linux/netfilter_bridge.h>


enum nf_br_hook_priorities {
	NF_BR_PRI_FIRST = INT_MIN,
	NF_BR_PRI_NAT_DST_BRIDGED = -300,
	NF_BR_PRI_FILTER_BRIDGED = -200,
	NF_BR_PRI_BRNF = 0,
	NF_BR_PRI_NAT_DST_OTHER = 100,
	NF_BR_PRI_FILTER_OTHER = 200,
	NF_BR_PRI_NAT_SRC = 300,
	NF_BR_PRI_LAST = INT_MAX,
};

#ifdef CONFIG_BRIDGE_NETFILTER

#define BRNF_PKT_TYPE			0x01
#define BRNF_BRIDGED_DNAT		0x02
#define BRNF_BRIDGED			0x04
#define BRNF_NF_BRIDGE_PREROUTING	0x08
#define BRNF_8021Q			0x10
#define BRNF_PPPoE			0x20

/* Only used in br_forward.c */
int nf_bridge_copy_header(struct sk_buff *skb);
static inline int nf_bridge_maybe_copy_header(struct sk_buff *skb)
{
	if (skb->nf_bridge &&
	    skb->nf_bridge->mask & (BRNF_BRIDGED | BRNF_BRIDGED_DNAT))
		return nf_bridge_copy_header(skb);
  	return 0;
}

static inline unsigned int nf_bridge_encap_header_len(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __cpu_to_be16(ETH_P_8021Q):
		return VLAN_HLEN;
	case __cpu_to_be16(ETH_P_PPP_SES):
		return PPPOE_SES_HLEN;
	default:
		return 0;
	}
}

static inline unsigned int nf_bridge_mtu_reduction(const struct sk_buff *skb)
{
	if (unlikely(skb->nf_bridge->mask & BRNF_PPPoE))
		return PPPOE_SES_HLEN;
	return 0;
}

int br_handle_frame_finish(struct sk_buff *skb);
/* Only used in br_device.c */
static inline int br_nf_pre_routing_finish_bridge_slow(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;

	skb_pull(skb, ETH_HLEN);
	nf_bridge->mask ^= BRNF_BRIDGED_DNAT;
	skb_copy_to_linear_data_offset(skb, -(ETH_HLEN-ETH_ALEN),
				       skb->nf_bridge->data, ETH_HLEN-ETH_ALEN);
	skb->dev = nf_bridge->physindev;
	return br_handle_frame_finish(skb);
}

/* This is called by the IP fragmenting code and it ensures there is
 * enough room for the encapsulating header (if there is one). */
static inline unsigned int nf_bridge_pad(const struct sk_buff *skb)
{
	if (skb->nf_bridge)
		return nf_bridge_encap_header_len(skb);
	return 0;
}

struct bridge_skb_cb {
	union {
		__be32 ipv4;
	} daddr;
};

static inline void br_drop_fake_rtable(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	if (dst && (dst->flags & DST_FAKE_RTABLE))
		skb_dst_drop(skb);
}

#else
#define nf_bridge_maybe_copy_header(skb)	(0)
#define nf_bridge_pad(skb)			(0)
#define br_drop_fake_rtable(skb)	        do { } while (0)
#endif /* CONFIG_BRIDGE_NETFILTER */

#endif
