#ifndef _NF_QUEUE_H
#define _NF_QUEUE_H

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>

/* Each queued (to userspace) skbuff has one of these. */
struct nf_queue_entry {
	struct list_head	list;
	struct sk_buff		*skb;
	unsigned int		id;

	struct nf_hook_ops	*elem;
	struct nf_hook_state	state;
	u16			size; /* sizeof(entry) + saved route keys */

	/* extra space to store route keys */
};

#define nf_queue_entry_reroute(x) ((void *)x + sizeof(struct nf_queue_entry))

/* Packet queuing */
struct nf_queue_handler {
	int			(*outfn)(struct nf_queue_entry *entry,
					 unsigned int queuenum);
	void			(*nf_hook_drop)(struct net *net,
						struct nf_hook_ops *ops);
};

void nf_register_queue_handler(const struct nf_queue_handler *qh);
void nf_unregister_queue_handler(void);
void nf_reinject(struct nf_queue_entry *entry, unsigned int verdict);

bool nf_queue_entry_get_refs(struct nf_queue_entry *entry);
void nf_queue_entry_release_refs(struct nf_queue_entry *entry);

static inline void init_hashrandom(u32 *jhash_initval)
{
	while (*jhash_initval == 0)
		*jhash_initval = prandom_u32();
}

static inline u32 hash_v4(const struct sk_buff *skb, u32 jhash_initval)
{
	const struct iphdr *iph = ip_hdr(skb);

	/* packets in either direction go into same queue */
	if ((__force u32)iph->saddr < (__force u32)iph->daddr)
		return jhash_3words((__force u32)iph->saddr,
			(__force u32)iph->daddr, iph->protocol, jhash_initval);

	return jhash_3words((__force u32)iph->daddr,
			(__force u32)iph->saddr, iph->protocol, jhash_initval);
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static inline u32 hash_v6(const struct sk_buff *skb, u32 jhash_initval)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
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

	return jhash_3words(a, b, c, jhash_initval);
}
#endif

static inline u32
nfqueue_hash(const struct sk_buff *skb, u16 queue, u16 queues_total, u8 family,
	     u32 jhash_initval)
{
	if (family == NFPROTO_IPV4)
		queue += ((u64) hash_v4(skb, jhash_initval) * queues_total) >> 32;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	else if (family == NFPROTO_IPV6)
		queue += ((u64) hash_v6(skb, jhash_initval) * queues_total) >> 32;
#endif

	return queue;
}

#endif /* _NF_QUEUE_H */
