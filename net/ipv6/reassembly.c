/*
 *	IPv6 fragment reassembly
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: reassembly.c,v 1.26 2001/03/07 22:00:57 davem Exp $
 *
 *	Based on: net/ipv4/ip_fragment.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Fixes:
 *	Andi Kleen	Make it work with multiple hosts.
 *			More RFC compliance.
 *
 *      Horst von Brand Add missing #include <linux/string.h>
 *	Alexey Kuznetsov	SMP races, threading, cleanup.
 *	Patrick McHardy		LRU queue of frag heads for evictor.
 *	Mitsuru KANDA @USAGI	Register inet6_protocol{}.
 *	David Stevens and
 *	YOSHIFUJI,H. @USAGI	Always remove fragment header to
 *				calculate ICV correctly.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/skbuff.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/inet_frag.h>

struct ip6frag_skb_cb
{
	struct inet6_skb_parm	h;
	int			offset;
};

#define FRAG6_CB(skb)	((struct ip6frag_skb_cb*)((skb)->cb))


/*
 *	Equivalent of ipv4 struct ipq
 */

struct frag_queue
{
	struct inet_frag_queue	q;

	__be32			id;		/* fragment id		*/
	struct in6_addr		saddr;
	struct in6_addr		daddr;

	int			iif;
	unsigned int		csum;
	__u16			nhoffset;
};

static struct inet_frags ip6_frags;

int ip6_frag_nqueues(struct net *net)
{
	return net->ipv6.frags.nqueues;
}

int ip6_frag_mem(struct net *net)
{
	return atomic_read(&net->ipv6.frags.mem);
}

static int ip6_frag_reasm(struct frag_queue *fq, struct sk_buff *prev,
			  struct net_device *dev);

/*
 * callers should be careful not to use the hash value outside the ipfrag_lock
 * as doing so could race with ipfrag_hash_rnd being recalculated.
 */
static unsigned int ip6qhashfn(__be32 id, struct in6_addr *saddr,
			       struct in6_addr *daddr)
{
	u32 a, b, c;

	a = (__force u32)saddr->s6_addr32[0];
	b = (__force u32)saddr->s6_addr32[1];
	c = (__force u32)saddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += ip6_frags.rnd;
	__jhash_mix(a, b, c);

	a += (__force u32)saddr->s6_addr32[3];
	b += (__force u32)daddr->s6_addr32[0];
	c += (__force u32)daddr->s6_addr32[1];
	__jhash_mix(a, b, c);

	a += (__force u32)daddr->s6_addr32[2];
	b += (__force u32)daddr->s6_addr32[3];
	c += (__force u32)id;
	__jhash_mix(a, b, c);

	return c & (INETFRAGS_HASHSZ - 1);
}

static unsigned int ip6_hashfn(struct inet_frag_queue *q)
{
	struct frag_queue *fq;

	fq = container_of(q, struct frag_queue, q);
	return ip6qhashfn(fq->id, &fq->saddr, &fq->daddr);
}

int ip6_frag_match(struct inet_frag_queue *q, void *a)
{
	struct frag_queue *fq;
	struct ip6_create_arg *arg = a;

	fq = container_of(q, struct frag_queue, q);
	return (fq->id == arg->id &&
			ipv6_addr_equal(&fq->saddr, arg->src) &&
			ipv6_addr_equal(&fq->daddr, arg->dst));
}
EXPORT_SYMBOL(ip6_frag_match);

/* Memory Tracking Functions. */
static inline void frag_kfree_skb(struct netns_frags *nf,
		struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &nf->mem);
	kfree_skb(skb);
}

void ip6_frag_init(struct inet_frag_queue *q, void *a)
{
	struct frag_queue *fq = container_of(q, struct frag_queue, q);
	struct ip6_create_arg *arg = a;

	fq->id = arg->id;
	ipv6_addr_copy(&fq->saddr, arg->src);
	ipv6_addr_copy(&fq->daddr, arg->dst);
}
EXPORT_SYMBOL(ip6_frag_init);

/* Destruction primitives. */

static __inline__ void fq_put(struct frag_queue *fq)
{
	inet_frag_put(&fq->q, &ip6_frags);
}

/* Kill fq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static __inline__ void fq_kill(struct frag_queue *fq)
{
	inet_frag_kill(&fq->q, &ip6_frags);
}

static void ip6_evictor(struct net *net, struct inet6_dev *idev)
{
	int evicted;

	evicted = inet_frag_evictor(&net->ipv6.frags, &ip6_frags);
	if (evicted)
		IP6_ADD_STATS_BH(idev, IPSTATS_MIB_REASMFAILS, evicted);
}

static void ip6_frag_expire(unsigned long data)
{
	struct frag_queue *fq;
	struct net_device *dev = NULL;

	fq = container_of((struct inet_frag_queue *)data, struct frag_queue, q);

	spin_lock(&fq->q.lock);

	if (fq->q.last_in & INET_FRAG_COMPLETE)
		goto out;

	fq_kill(fq);

	dev = dev_get_by_index(&init_net, fq->iif);
	if (!dev)
		goto out;

	rcu_read_lock();
	IP6_INC_STATS_BH(__in6_dev_get(dev), IPSTATS_MIB_REASMTIMEOUT);
	IP6_INC_STATS_BH(__in6_dev_get(dev), IPSTATS_MIB_REASMFAILS);
	rcu_read_unlock();

	/* Don't send error if the first segment did not arrive. */
	if (!(fq->q.last_in & INET_FRAG_FIRST_IN) || !fq->q.fragments)
		goto out;

	/*
	   But use as source device on which LAST ARRIVED
	   segment was received. And do not use fq->dev
	   pointer directly, device might already disappeared.
	 */
	fq->q.fragments->dev = dev;
	icmpv6_send(fq->q.fragments, ICMPV6_TIME_EXCEED, ICMPV6_EXC_FRAGTIME, 0, dev);
out:
	if (dev)
		dev_put(dev);
	spin_unlock(&fq->q.lock);
	fq_put(fq);
}

static __inline__ struct frag_queue *
fq_find(struct net *net, __be32 id, struct in6_addr *src, struct in6_addr *dst,
	struct inet6_dev *idev)
{
	struct inet_frag_queue *q;
	struct ip6_create_arg arg;
	unsigned int hash;

	arg.id = id;
	arg.src = src;
	arg.dst = dst;
	hash = ip6qhashfn(id, src, dst);

	q = inet_frag_find(&net->ipv6.frags, &ip6_frags, &arg, hash);
	if (q == NULL)
		goto oom;

	return container_of(q, struct frag_queue, q);

oom:
	IP6_INC_STATS_BH(idev, IPSTATS_MIB_REASMFAILS);
	return NULL;
}

static int ip6_frag_queue(struct frag_queue *fq, struct sk_buff *skb,
			   struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	struct net_device *dev;
	int offset, end;

	if (fq->q.last_in & INET_FRAG_COMPLETE)
		goto err;

	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (ntohs(ipv6_hdr(skb)->payload_len) -
			((u8 *)(fhdr + 1) - (u8 *)(ipv6_hdr(skb) + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		IP6_INC_STATS_BH(ip6_dst_idev(skb->dst),
				 IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((u8 *)&fhdr->frag_off -
				   skb_network_header(skb)));
		return -1;
	}

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		const unsigned char *nh = skb_network_header(skb);
		skb->csum = csum_sub(skb->csum,
				     csum_partial(nh, (u8 *)(fhdr + 1) - nh,
						  0));
	}

	/* Is this the final fragment? */
	if (!(fhdr->frag_off & htons(IP6_MF))) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrupted.
		 */
		if (end < fq->q.len ||
		    ((fq->q.last_in & INET_FRAG_LAST_IN) && end != fq->q.len))
			goto err;
		fq->q.last_in |= INET_FRAG_LAST_IN;
		fq->q.len = end;
	} else {
		/* Check if the fragment is rounded to 8 bytes.
		 * Required by the RFC.
		 */
		if (end & 0x7) {
			/* RFC2460 says always send parameter problem in
			 * this case. -DaveM
			 */
			IP6_INC_STATS_BH(ip6_dst_idev(skb->dst),
					 IPSTATS_MIB_INHDRERRORS);
			icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
					  offsetof(struct ipv6hdr, payload_len));
			return -1;
		}
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.last_in & INET_FRAG_LAST_IN)
				goto err;
			fq->q.len = end;
		}
	}

	if (end == offset)
		goto err;

	/* Point into the IP datagram 'data' part. */
	if (!pskb_pull(skb, (u8 *) (fhdr + 1) - skb->data))
		goto err;

	if (pskb_trim_rcsum(skb, end - offset))
		goto err;

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for(next = fq->q.fragments; next != NULL; next = next->next) {
		if (FRAG6_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if (prev) {
		int i = (FRAG6_CB(prev)->offset + prev->len) - offset;

		if (i > 0) {
			offset += i;
			if (end <= offset)
				goto err;
			if (!pskb_pull(skb, i))
				goto err;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	/* Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */
	while (next && FRAG6_CB(next)->offset < end) {
		int i = end - FRAG6_CB(next)->offset; /* overlap is 'i' bytes */

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			if (!pskb_pull(next, i))
				goto err;
			FRAG6_CB(next)->offset += i;	/* next fragment */
			fq->q.meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)
				next->ip_summed = CHECKSUM_NONE;
			break;
		} else {
			struct sk_buff *free_it = next;

			/* Old fragment is completely overridden with
			 * new one drop it.
			 */
			next = next->next;

			if (prev)
				prev->next = next;
			else
				fq->q.fragments = next;

			fq->q.meat -= free_it->len;
			frag_kfree_skb(fq->q.net, free_it, NULL);
		}
	}

	FRAG6_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		fq->q.fragments = skb;

	dev = skb->dev;
	if (dev) {
		fq->iif = dev->ifindex;
		skb->dev = NULL;
	}
	fq->q.stamp = skb->tstamp;
	fq->q.meat += skb->len;
	atomic_add(skb->truesize, &fq->q.net->mem);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.last_in |= INET_FRAG_FIRST_IN;
	}

	if (fq->q.last_in == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len)
		return ip6_frag_reasm(fq, prev, dev);

	write_lock(&ip6_frags.lock);
	list_move_tail(&fq->q.lru_list, &fq->q.net->lru_list);
	write_unlock(&ip6_frags.lock);
	return -1;

err:
	IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -1;
}

/*
 *	Check if this packet is complete.
 *	Returns NULL on failure by any reason, and pointer
 *	to current nexthdr field in reassembled frame.
 *
 *	It is called with locked fq, and caller must check that
 *	queue is eligible for reassembly i.e. it is not COMPLETE,
 *	the last and the first frames arrived and all the bits are here.
 */
static int ip6_frag_reasm(struct frag_queue *fq, struct sk_buff *prev,
			  struct net_device *dev)
{
	struct sk_buff *fp, *head = fq->q.fragments;
	int    payload_len;
	unsigned int nhoff;

	fq_kill(fq);

	/* Make the one we just received the head. */
	if (prev) {
		head = prev->next;
		fp = skb_clone(head, GFP_ATOMIC);

		if (!fp)
			goto out_oom;

		fp->next = head->next;
		prev->next = fp;

		skb_morph(head, fq->q.fragments);
		head->next = fq->q.fragments->next;

		kfree_skb(fq->q.fragments);
		fq->q.fragments = head;
	}

	BUG_TRAP(head != NULL);
	BUG_TRAP(FRAG6_CB(head)->offset == 0);

	/* Unfragmented part is taken from the first segment. */
	payload_len = ((head->data - skb_network_header(head)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN)
		goto out_oversize;

	/* Head of list must not be cloned. */
	if (skb_cloned(head) && pskb_expand_head(head, 0, 0, GFP_ATOMIC))
		goto out_oom;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_shinfo(head)->frag_list) {
		struct sk_buff *clone;
		int i, plen = 0;

		if ((clone = alloc_skb(0, GFP_ATOMIC)) == NULL)
			goto out_oom;
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_shinfo(head)->frag_list = NULL;
		for (i=0; i<skb_shinfo(head)->nr_frags; i++)
			plen += skb_shinfo(head)->frags[i].size;
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		atomic_add(clone->truesize, &fq->q.net->mem);
	}

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	nhoff = fq->nhoffset;
	skb_network_header(head)[nhoff] = skb_transport_header(head)[0];
	memmove(head->head + sizeof(struct frag_hdr), head->head,
		(head->data - head->head) - sizeof(struct frag_hdr));
	head->mac_header += sizeof(struct frag_hdr);
	head->network_header += sizeof(struct frag_hdr);

	skb_shinfo(head)->frag_list = head->next;
	skb_reset_transport_header(head);
	skb_push(head, head->data - skb_network_header(head));
	atomic_sub(head->truesize, &fq->q.net->mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &fq->q.net->mem);
	}

	head->next = NULL;
	head->dev = dev;
	head->tstamp = fq->q.stamp;
	ipv6_hdr(head)->payload_len = htons(payload_len);
	IP6CB(head)->nhoff = nhoff;

	/* Yes, and fold redundant checksum back. 8) */
	if (head->ip_summed == CHECKSUM_COMPLETE)
		head->csum = csum_partial(skb_network_header(head),
					  skb_network_header_len(head),
					  head->csum);

	rcu_read_lock();
	IP6_INC_STATS_BH(__in6_dev_get(dev), IPSTATS_MIB_REASMOKS);
	rcu_read_unlock();
	fq->q.fragments = NULL;
	return 1;

out_oversize:
	if (net_ratelimit())
		printk(KERN_DEBUG "ip6_frag_reasm: payload len = %d\n", payload_len);
	goto out_fail;
out_oom:
	if (net_ratelimit())
		printk(KERN_DEBUG "ip6_frag_reasm: no memory for reassembly\n");
out_fail:
	rcu_read_lock();
	IP6_INC_STATS_BH(__in6_dev_get(dev), IPSTATS_MIB_REASMFAILS);
	rcu_read_unlock();
	return -1;
}

static int ipv6_frag_rcv(struct sk_buff *skb)
{
	struct frag_hdr *fhdr;
	struct frag_queue *fq;
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct net *net;

	IP6_INC_STATS_BH(ip6_dst_idev(skb->dst), IPSTATS_MIB_REASMREQDS);

	/* Jumbo payload inhibits frag. header */
	if (hdr->payload_len==0) {
		IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  skb_network_header_len(skb));
		return -1;
	}
	if (!pskb_may_pull(skb, (skb_transport_offset(skb) +
				 sizeof(struct frag_hdr)))) {
		IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  skb_network_header_len(skb));
		return -1;
	}

	hdr = ipv6_hdr(skb);
	fhdr = (struct frag_hdr *)skb_transport_header(skb);

	if (!(fhdr->frag_off & htons(0xFFF9))) {
		/* It is not a fragmented frame */
		skb->transport_header += sizeof(struct frag_hdr);
		IP6_INC_STATS_BH(ip6_dst_idev(skb->dst), IPSTATS_MIB_REASMOKS);

		IP6CB(skb)->nhoff = (u8 *)fhdr - skb_network_header(skb);
		return 1;
	}

	net = dev_net(skb->dev);
	if (atomic_read(&net->ipv6.frags.mem) > net->ipv6.frags.high_thresh)
		ip6_evictor(net, ip6_dst_idev(skb->dst));

	if ((fq = fq_find(net, fhdr->identification, &hdr->saddr, &hdr->daddr,
			  ip6_dst_idev(skb->dst))) != NULL) {
		int ret;

		spin_lock(&fq->q.lock);

		ret = ip6_frag_queue(fq, skb, fhdr, IP6CB(skb)->nhoff);

		spin_unlock(&fq->q.lock);
		fq_put(fq);
		return ret;
	}

	IP6_INC_STATS_BH(ip6_dst_idev(skb->dst), IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -1;
}

static struct inet6_protocol frag_protocol =
{
	.handler	=	ipv6_frag_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

#ifdef CONFIG_SYSCTL
static struct ctl_table ip6_frags_ctl_table[] = {
	{
		.ctl_name	= NET_IPV6_IP6FRAG_HIGH_THRESH,
		.procname	= "ip6frag_high_thresh",
		.data		= &init_net.ipv6.frags.high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_LOW_THRESH,
		.procname	= "ip6frag_low_thresh",
		.data		= &init_net.ipv6.frags.low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_TIME,
		.procname	= "ip6frag_time",
		.data		= &init_net.ipv6.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_SECRET_INTERVAL,
		.procname	= "ip6frag_secret_interval",
		.data		= &ip6_frags.secret_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{ }
};

static int ip6_frags_sysctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = ip6_frags_ctl_table;
	if (net != &init_net) {
		table = kmemdup(table, sizeof(ip6_frags_ctl_table), GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;

		table[0].data = &net->ipv6.frags.high_thresh;
		table[1].data = &net->ipv6.frags.low_thresh;
		table[2].data = &net->ipv6.frags.timeout;
		table[3].mode &= ~0222;
	}

	hdr = register_net_sysctl_table(net, net_ipv6_ctl_path, table);
	if (hdr == NULL)
		goto err_reg;

	net->ipv6.sysctl.frags_hdr = hdr;
	return 0;

err_reg:
	if (net != &init_net)
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void ip6_frags_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->ipv6.sysctl.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv6.sysctl.frags_hdr);
	kfree(table);
}
#else
static inline int ip6_frags_sysctl_register(struct net *net)
{
	return 0;
}

static inline void ip6_frags_sysctl_unregister(struct net *net)
{
}
#endif

static int ipv6_frags_init_net(struct net *net)
{
	net->ipv6.frags.high_thresh = 256 * 1024;
	net->ipv6.frags.low_thresh = 192 * 1024;
	net->ipv6.frags.timeout = IPV6_FRAG_TIMEOUT;

	inet_frags_init_net(&net->ipv6.frags);

	return ip6_frags_sysctl_register(net);
}

static void ipv6_frags_exit_net(struct net *net)
{
	ip6_frags_sysctl_unregister(net);
	inet_frags_exit_net(&net->ipv6.frags, &ip6_frags);
}

static struct pernet_operations ip6_frags_ops = {
	.init = ipv6_frags_init_net,
	.exit = ipv6_frags_exit_net,
};

int __init ipv6_frag_init(void)
{
	int ret;

	ret = inet6_add_protocol(&frag_protocol, IPPROTO_FRAGMENT);
	if (ret)
		goto out;

	register_pernet_subsys(&ip6_frags_ops);

	ip6_frags.hashfn = ip6_hashfn;
	ip6_frags.constructor = ip6_frag_init;
	ip6_frags.destructor = NULL;
	ip6_frags.skb_free = NULL;
	ip6_frags.qsize = sizeof(struct frag_queue);
	ip6_frags.match = ip6_frag_match;
	ip6_frags.frag_expire = ip6_frag_expire;
	ip6_frags.secret_interval = 10 * 60 * HZ;
	inet_frags_init(&ip6_frags);
out:
	return ret;
}

void ipv6_frag_exit(void)
{
	inet_frags_fini(&ip6_frags);
	unregister_pernet_subsys(&ip6_frags_ops);
	inet6_del_protocol(&frag_protocol, IPPROTO_FRAGMENT);
}
