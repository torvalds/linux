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

struct inet_frags_ctl ip6_frags_ctl __read_mostly = {
	.high_thresh 	 = 256 * 1024,
	.low_thresh	 = 192 * 1024,
	.timeout	 = IPV6_FRAG_TIMEOUT,
	.secret_interval = 10 * 60 * HZ,
};

static struct inet_frags ip6_frags;

int ip6_frag_nqueues(void)
{
	return ip6_frags.nqueues;
}

int ip6_frag_mem(void)
{
	return atomic_read(&ip6_frags.mem);
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

/* Memory Tracking Functions. */
static inline void frag_kfree_skb(struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &ip6_frags.mem);
	kfree_skb(skb);
}

static void ip6_frag_free(struct inet_frag_queue *fq)
{
	kfree(container_of(fq, struct frag_queue, q));
}

static inline struct frag_queue *frag_alloc_queue(void)
{
	struct frag_queue *fq = kzalloc(sizeof(struct frag_queue), GFP_ATOMIC);

	if(!fq)
		return NULL;
	atomic_add(sizeof(struct frag_queue), &ip6_frags.mem);
	return fq;
}

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

static void ip6_evictor(struct inet6_dev *idev)
{
	int evicted;

	evicted = inet_frag_evictor(&ip6_frags);
	if (evicted)
		IP6_ADD_STATS_BH(idev, IPSTATS_MIB_REASMFAILS, evicted);
}

static void ip6_frag_expire(unsigned long data)
{
	struct frag_queue *fq = (struct frag_queue *) data;
	struct net_device *dev = NULL;

	spin_lock(&fq->q.lock);

	if (fq->q.last_in & COMPLETE)
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
	if (!(fq->q.last_in&FIRST_IN) || !fq->q.fragments)
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

/* Creation primitives. */


static struct frag_queue *ip6_frag_intern(struct frag_queue *fq_in)
{
	struct frag_queue *fq;
	unsigned int hash;
#ifdef CONFIG_SMP
	struct hlist_node *n;
#endif

	write_lock(&ip6_frags.lock);
	hash = ip6qhashfn(fq_in->id, &fq_in->saddr, &fq_in->daddr);
#ifdef CONFIG_SMP
	hlist_for_each_entry(fq, n, &ip6_frags.hash[hash], q.list) {
		if (fq->id == fq_in->id &&
		    ipv6_addr_equal(&fq_in->saddr, &fq->saddr) &&
		    ipv6_addr_equal(&fq_in->daddr, &fq->daddr)) {
			atomic_inc(&fq->q.refcnt);
			write_unlock(&ip6_frags.lock);
			fq_in->q.last_in |= COMPLETE;
			fq_put(fq_in);
			return fq;
		}
	}
#endif
	fq = fq_in;

	if (!mod_timer(&fq->q.timer, jiffies + ip6_frags_ctl.timeout))
		atomic_inc(&fq->q.refcnt);

	atomic_inc(&fq->q.refcnt);
	hlist_add_head(&fq->q.list, &ip6_frags.hash[hash]);
	INIT_LIST_HEAD(&fq->q.lru_list);
	list_add_tail(&fq->q.lru_list, &ip6_frags.lru_list);
	ip6_frags.nqueues++;
	write_unlock(&ip6_frags.lock);
	return fq;
}


static struct frag_queue *
ip6_frag_create(__be32 id, struct in6_addr *src, struct in6_addr *dst,
		struct inet6_dev *idev)
{
	struct frag_queue *fq;

	if ((fq = frag_alloc_queue()) == NULL)
		goto oom;

	fq->id = id;
	ipv6_addr_copy(&fq->saddr, src);
	ipv6_addr_copy(&fq->daddr, dst);

	init_timer(&fq->q.timer);
	fq->q.timer.function = ip6_frag_expire;
	fq->q.timer.data = (long) fq;
	spin_lock_init(&fq->q.lock);
	atomic_set(&fq->q.refcnt, 1);

	return ip6_frag_intern(fq);

oom:
	IP6_INC_STATS_BH(idev, IPSTATS_MIB_REASMFAILS);
	return NULL;
}

static __inline__ struct frag_queue *
fq_find(__be32 id, struct in6_addr *src, struct in6_addr *dst,
	struct inet6_dev *idev)
{
	struct frag_queue *fq;
	struct hlist_node *n;
	unsigned int hash;

	read_lock(&ip6_frags.lock);
	hash = ip6qhashfn(id, src, dst);
	hlist_for_each_entry(fq, n, &ip6_frags.hash[hash], q.list) {
		if (fq->id == id &&
		    ipv6_addr_equal(src, &fq->saddr) &&
		    ipv6_addr_equal(dst, &fq->daddr)) {
			atomic_inc(&fq->q.refcnt);
			read_unlock(&ip6_frags.lock);
			return fq;
		}
	}
	read_unlock(&ip6_frags.lock);

	return ip6_frag_create(id, src, dst, idev);
}


static int ip6_frag_queue(struct frag_queue *fq, struct sk_buff *skb,
			   struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	struct net_device *dev;
	int offset, end;

	if (fq->q.last_in & COMPLETE)
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
		    ((fq->q.last_in & LAST_IN) && end != fq->q.len))
			goto err;
		fq->q.last_in |= LAST_IN;
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
			if (fq->q.last_in & LAST_IN)
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
			frag_kfree_skb(free_it, NULL);
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
	atomic_add(skb->truesize, &ip6_frags.mem);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.last_in |= FIRST_IN;
	}

	if (fq->q.last_in == (FIRST_IN | LAST_IN) && fq->q.meat == fq->q.len)
		return ip6_frag_reasm(fq, prev, dev);

	write_lock(&ip6_frags.lock);
	list_move_tail(&fq->q.lru_list, &ip6_frags.lru_list);
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
		atomic_add(clone->truesize, &ip6_frags.mem);
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
	atomic_sub(head->truesize, &ip6_frags.mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &ip6_frags.mem);
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

	if (atomic_read(&ip6_frags.mem) > ip6_frags_ctl.high_thresh)
		ip6_evictor(ip6_dst_idev(skb->dst));

	if ((fq = fq_find(fhdr->identification, &hdr->saddr, &hdr->daddr,
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

void __init ipv6_frag_init(void)
{
	if (inet6_add_protocol(&frag_protocol, IPPROTO_FRAGMENT) < 0)
		printk(KERN_ERR "ipv6_frag_init: Could not register protocol\n");

	ip6_frags.ctl = &ip6_frags_ctl;
	ip6_frags.hashfn = ip6_hashfn;
	ip6_frags.destructor = ip6_frag_free;
	ip6_frags.skb_free = NULL;
	ip6_frags.qsize = sizeof(struct frag_queue);
	inet_frags_init(&ip6_frags);
}
