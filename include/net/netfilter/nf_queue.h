/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_QUEUE_H
#define _NF_QUEUE_H

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>

/* Each queued (to userspace) skbuff has one of these. */
struct nf_queue_entry {
	struct list_head	list;
	struct sk_buff		*skb;
	unsigned int		id;
	unsigned int		hook_index;	/* index in hook_entries->hook[] */
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	struct net_device	*physin;
	struct net_device	*physout;
#endif
	struct nf_hook_state	state;
	u16			size; /* sizeof(entry) + saved route keys */

	/* extra space to store route keys */
};

#define nf_queue_entry_reroute(x) ((void *)x + sizeof(struct nf_queue_entry))

/* Packet queuing */
struct nf_queue_handler {
	int		(*outfn)(struct nf_queue_entry *entry,
				 unsigned int queuenum);
	void		(*nf_hook_drop)(struct net *net);
};

void nf_register_queue_handler(const struct nf_queue_handler *qh);
void nf_unregister_queue_handler(void);
void nf_reinject(struct nf_queue_entry *entry, unsigned int verdict);

void nf_queue_entry_get_refs(struct nf_queue_entry *entry);
void nf_queue_entry_free(struct nf_queue_entry *entry);

static inline void init_hashrandom(u32 *jhash_initval)
{
	while (*jhash_initval == 0)
		*jhash_initval = prandom_u32();
}

static inline u32 hash_v4(const struct iphdr *iph, u32 initval)
{
	/* packets in either direction go into same queue */
	if ((__force u32)iph->saddr < (__force u32)iph->daddr)
		return jhash_3words((__force u32)iph->saddr,
			(__force u32)iph->daddr, iph->protocol, initval);

	return jhash_3words((__force u32)iph->daddr,
			(__force u32)iph->saddr, iph->protocol, initval);
}

static inline u32 hash_v6(const struct ipv6hdr *ip6h, u32 initval)
{
	u32 a, b, c;

	if ((__force u32)ip6h->saddr.s6_addr32[3] <
	    (__force u32)ip6h->daddr.s6_addr32[3]) {
		a = (__force u32) ip6h->saddr.s6_addr32[3];
		b = (__force u32) ip6h->daddr.s6_addr32[3];
	} else {
		b = (__force u32) ip6h->saddr.s6_addr32[3];
		a = (__force u32) ip6h->daddr.s6_addr32[3];
	}

	if ((__force u32)ip6h->saddr.s6_addr32[1] <
	    (__force u32)ip6h->daddr.s6_addr32[1])
		c = (__force u32) ip6h->saddr.s6_addr32[1];
	else
		c = (__force u32) ip6h->daddr.s6_addr32[1];

	return jhash_3words(a, b, c, initval);
}

static inline u32 hash_bridge(const struct sk_buff *skb, u32 initval)
{
	struct ipv6hdr *ip6h, _ip6h;
	struct iphdr *iph, _iph;

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		iph = skb_header_pointer(skb, skb_network_offset(skb),
					 sizeof(*iph), &_iph);
		if (iph)
			return hash_v4(iph, initval);
		break;
	case htons(ETH_P_IPV6):
		ip6h = skb_header_pointer(skb, skb_network_offset(skb),
					  sizeof(*ip6h), &_ip6h);
		if (ip6h)
			return hash_v6(ip6h, initval);
		break;
	}

	return 0;
}

static inline u32
nfqueue_hash(const struct sk_buff *skb, u16 queue, u16 queues_total, u8 family,
	     u32 initval)
{
	switch (family) {
	case NFPROTO_IPV4:
		queue += reciprocal_scale(hash_v4(ip_hdr(skb), initval),
					  queues_total);
		break;
	case NFPROTO_IPV6:
		queue += reciprocal_scale(hash_v6(ipv6_hdr(skb), initval),
					  queues_total);
		break;
	case NFPROTO_BRIDGE:
		queue += reciprocal_scale(hash_bridge(skb, initval),
					  queues_total);
		break;
	}

	return queue;
}

int nf_queue(struct sk_buff *skb, struct nf_hook_state *state,
	     unsigned int index, unsigned int verdict);

#endif /* _NF_QUEUE_H */
