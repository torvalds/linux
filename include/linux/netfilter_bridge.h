#ifndef __LINUX_BRIDGE_NETFILTER_H
#define __LINUX_BRIDGE_NETFILTER_H

/* bridge-specific defines for netfilter. 
 */

#include <linux/netfilter.h>
#if defined(__KERNEL__) && defined(CONFIG_BRIDGE_NETFILTER)
#include <linux/if_ether.h>
#endif

/* Bridge Hooks */
/* After promisc drops, checksum checks. */
#define NF_BR_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_BR_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_BR_FORWARD		2
/* Packets coming from a local process. */
#define NF_BR_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_BR_POST_ROUTING	4
/* Not really a hook, but used for the ebtables broute table */
#define NF_BR_BROUTING		5
#define NF_BR_NUMHOOKS		6

#ifdef __KERNEL__

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
#define BRNF_DONT_TAKE_PARENT		0x04
#define BRNF_BRIDGED			0x08
#define BRNF_NF_BRIDGE_PREROUTING	0x10


/* Only used in br_forward.c */
static inline
int nf_bridge_maybe_copy_header(struct sk_buff *skb)
{
	int err;

	if (skb->nf_bridge) {
		if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
			err = skb_cow(skb, 18);
			if (err)
				return err;
			memcpy(skb->data - 18, skb->nf_bridge->data, 18);
			skb_push(skb, 4);
		} else {
			err = skb_cow(skb, 16);
			if (err)
				return err;
			memcpy(skb->data - 16, skb->nf_bridge->data, 16);
		}
	}
	return 0;
}

/* This is called by the IP fragmenting code and it ensures there is
 * enough room for the encapsulating header (if there is one). */
static inline
int nf_bridge_pad(struct sk_buff *skb)
{
	if (skb->protocol == __constant_htons(ETH_P_IP))
		return 0;
	if (skb->nf_bridge) {
		if (skb->protocol == __constant_htons(ETH_P_8021Q))
			return 4;
	}
	return 0;
}

struct bridge_skb_cb {
	union {
		__u32 ipv4;
	} daddr;
};

extern int brnf_deferred_hooks;
#endif /* CONFIG_BRIDGE_NETFILTER */

#endif /* __KERNEL__ */
#endif
