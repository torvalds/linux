/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IPV6_FRAG_H
#define _IPV6_FRAG_H
#include <linux/kernel.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/inet_frag.h>

enum ip6_defrag_users {
	IP6_DEFRAG_LOCAL_DELIVER,
	IP6_DEFRAG_CONNTRACK_IN,
	__IP6_DEFRAG_CONNTRACK_IN	= IP6_DEFRAG_CONNTRACK_IN + USHRT_MAX,
	IP6_DEFRAG_CONNTRACK_OUT,
	__IP6_DEFRAG_CONNTRACK_OUT	= IP6_DEFRAG_CONNTRACK_OUT + USHRT_MAX,
	IP6_DEFRAG_CONNTRACK_BRIDGE_IN,
	__IP6_DEFRAG_CONNTRACK_BRIDGE_IN = IP6_DEFRAG_CONNTRACK_BRIDGE_IN + USHRT_MAX,
};

/*
 *	Equivalent of ipv4 struct ip
 */
struct frag_queue {
	struct inet_frag_queue	q;

	int			iif;
	__u16			nhoffset;
	u8			ecn;
};

#if IS_ENABLED(CONFIG_IPV6)
static inline void ip6frag_init(struct inet_frag_queue *q, const void *a)
{
	struct frag_queue *fq = container_of(q, struct frag_queue, q);
	const struct frag_v6_compare_key *key = a;

	q->key.v6 = *key;
	fq->ecn = 0;
}

static inline u32 ip6frag_key_hashfn(const void *data, u32 len, u32 seed)
{
	return jhash2(data,
		      sizeof(struct frag_v6_compare_key) / sizeof(u32), seed);
}

static inline u32 ip6frag_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct inet_frag_queue *fq = data;

	return jhash2((const u32 *)&fq->key.v6,
		      sizeof(struct frag_v6_compare_key) / sizeof(u32), seed);
}

static inline int
ip6frag_obj_cmpfn(struct rhashtable_compare_arg *arg, const void *ptr)
{
	const struct frag_v6_compare_key *key = arg->key;
	const struct inet_frag_queue *fq = ptr;

	return !!memcmp(&fq->key, key, sizeof(*key));
}

static inline void
ip6frag_expire_frag_queue(struct net *net, struct frag_queue *fq)
{
	struct net_device *dev = NULL;
	struct sk_buff *head;

	rcu_read_lock();
	if (fq->q.fqdir->dead)
		goto out_rcu_unlock;
	spin_lock(&fq->q.lock);

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto out;

	inet_frag_kill(&fq->q);

	dev = dev_get_by_index_rcu(net, fq->iif);
	if (!dev)
		goto out;

	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMFAILS);
	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMTIMEOUT);

	/* Don't send error if the first segment did not arrive. */
	if (!(fq->q.flags & INET_FRAG_FIRST_IN))
		goto out;

	/* sk_buff::dev and sk_buff::rbnode are unionized. So we
	 * pull the head out of the tree in order to be able to
	 * deal with head->dev.
	 */
	head = inet_frag_pull_head(&fq->q);
	if (!head)
		goto out;

	head->dev = dev;
	spin_unlock(&fq->q.lock);

	icmpv6_send(head, ICMPV6_TIME_EXCEED, ICMPV6_EXC_FRAGTIME, 0);
	kfree_skb(head);
	goto out_rcu_unlock;

out:
	spin_unlock(&fq->q.lock);
out_rcu_unlock:
	rcu_read_unlock();
	inet_frag_put(&fq->q);
}

/* Check if the upper layer header is truncated in the first fragment. */
static inline bool
ipv6frag_thdr_truncated(struct sk_buff *skb, int start, u8 *nexthdrp)
{
	u8 nexthdr = *nexthdrp;
	__be16 frag_off;
	int offset;

	offset = ipv6_skip_exthdr(skb, start, &nexthdr, &frag_off);
	if (offset < 0 || (frag_off & htons(IP6_OFFSET)))
		return false;
	switch (nexthdr) {
	case NEXTHDR_TCP:
		offset += sizeof(struct tcphdr);
		break;
	case NEXTHDR_UDP:
		offset += sizeof(struct udphdr);
		break;
	case NEXTHDR_ICMP:
		offset += sizeof(struct icmp6hdr);
		break;
	default:
		offset += 1;
	}
	if (offset > skb->len)
		return true;
	return false;
}

#endif
#endif
