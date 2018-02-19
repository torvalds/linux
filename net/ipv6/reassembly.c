/*
 *	IPv6 fragment reassembly
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
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

#define pr_fmt(fmt) "IPv6: " fmt

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
#include <linux/slab.h>
#include <linux/export.h>

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
#include <net/inet_ecn.h>

static const char ip6_frag_cache_name[] = "ip6-frags";

struct ip6frag_skb_cb {
	struct inet6_skb_parm	h;
	int			offset;
};

#define FRAG6_CB(skb)	((struct ip6frag_skb_cb *)((skb)->cb))

static u8 ip6_frag_ecn(const struct ipv6hdr *ipv6h)
{
	return 1 << (ipv6_get_dsfield(ipv6h) & INET_ECN_MASK);
}

static struct inet_frags ip6_frags;

static int ip6_frag_reasm(struct frag_queue *fq, struct sk_buff *prev,
			  struct net_device *dev);

/*
 * callers should be careful not to use the hash value outside the ipfrag_lock
 * as doing so could race with ipfrag_hash_rnd being recalculated.
 */
static unsigned int inet6_hash_frag(__be32 id, const struct in6_addr *saddr,
				    const struct in6_addr *daddr)
{
	net_get_random_once(&ip6_frags.rnd, sizeof(ip6_frags.rnd));
	return jhash_3words(ipv6_addr_hash(saddr), ipv6_addr_hash(daddr),
			    (__force u32)id, ip6_frags.rnd);
}

static unsigned int ip6_hashfn(const struct inet_frag_queue *q)
{
	const struct frag_queue *fq;

	fq = container_of(q, struct frag_queue, q);
	return inet6_hash_frag(fq->id, &fq->saddr, &fq->daddr);
}

bool ip6_frag_match(const struct inet_frag_queue *q, const void *a)
{
	const struct frag_queue *fq;
	const struct ip6_create_arg *arg = a;

	fq = container_of(q, struct frag_queue, q);
	return	fq->id == arg->id &&
		fq->user == arg->user &&
		ipv6_addr_equal(&fq->saddr, arg->src) &&
		ipv6_addr_equal(&fq->daddr, arg->dst) &&
		(arg->iif == fq->iif ||
		 !(ipv6_addr_type(arg->dst) & (IPV6_ADDR_MULTICAST |
					       IPV6_ADDR_LINKLOCAL)));
}
EXPORT_SYMBOL(ip6_frag_match);

void ip6_frag_init(struct inet_frag_queue *q, const void *a)
{
	struct frag_queue *fq = container_of(q, struct frag_queue, q);
	const struct ip6_create_arg *arg = a;

	fq->id = arg->id;
	fq->user = arg->user;
	fq->saddr = *arg->src;
	fq->daddr = *arg->dst;
	fq->ecn = arg->ecn;
}
EXPORT_SYMBOL(ip6_frag_init);

void ip6_expire_frag_queue(struct net *net, struct frag_queue *fq,
			   struct inet_frags *frags)
{
	struct net_device *dev = NULL;

	spin_lock(&fq->q.lock);

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto out;

	inet_frag_kill(&fq->q, frags);

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, fq->iif);
	if (!dev)
		goto out_rcu_unlock;

	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMFAILS);

	if (inet_frag_evicting(&fq->q))
		goto out_rcu_unlock;

	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMTIMEOUT);

	/* Don't send error if the first segment did not arrive. */
	if (!(fq->q.flags & INET_FRAG_FIRST_IN) || !fq->q.fragments)
		goto out_rcu_unlock;

	/* But use as source device on which LAST ARRIVED
	 * segment was received. And do not use fq->dev
	 * pointer directly, device might already disappeared.
	 */
	fq->q.fragments->dev = dev;
	icmpv6_send(fq->q.fragments, ICMPV6_TIME_EXCEED, ICMPV6_EXC_FRAGTIME, 0);
out_rcu_unlock:
	rcu_read_unlock();
out:
	spin_unlock(&fq->q.lock);
	inet_frag_put(&fq->q, frags);
}
EXPORT_SYMBOL(ip6_expire_frag_queue);

static void ip6_frag_expire(struct timer_list *t)
{
	struct inet_frag_queue *frag = from_timer(frag, t, timer);
	struct frag_queue *fq;
	struct net *net;

	fq = container_of(frag, struct frag_queue, q);
	net = container_of(fq->q.net, struct net, ipv6.frags);

	ip6_expire_frag_queue(net, fq, &ip6_frags);
}

static struct frag_queue *
fq_find(struct net *net, __be32 id, const struct in6_addr *src,
	const struct in6_addr *dst, int iif, u8 ecn)
{
	struct inet_frag_queue *q;
	struct ip6_create_arg arg;
	unsigned int hash;

	arg.id = id;
	arg.user = IP6_DEFRAG_LOCAL_DELIVER;
	arg.src = src;
	arg.dst = dst;
	arg.iif = iif;
	arg.ecn = ecn;

	hash = inet6_hash_frag(id, src, dst);

	q = inet_frag_find(&net->ipv6.frags, &ip6_frags, &arg, hash);
	if (IS_ERR_OR_NULL(q)) {
		inet_frag_maybe_warn_overflow(q, pr_fmt());
		return NULL;
	}
	return container_of(q, struct frag_queue, q);
}

static int ip6_frag_queue(struct frag_queue *fq, struct sk_buff *skb,
			   struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	struct net_device *dev;
	int offset, end, fragsize;
	struct net *net = dev_net(skb_dst(skb)->dev);
	u8 ecn;

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto err;

	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (ntohs(ipv6_hdr(skb)->payload_len) -
			((u8 *)(fhdr + 1) - (u8 *)(ipv6_hdr(skb) + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
				IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((u8 *)&fhdr->frag_off -
				   skb_network_header(skb)));
		return -1;
	}

	ecn = ip6_frag_ecn(ipv6_hdr(skb));

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
		    ((fq->q.flags & INET_FRAG_LAST_IN) && end != fq->q.len))
			goto err;
		fq->q.flags |= INET_FRAG_LAST_IN;
		fq->q.len = end;
	} else {
		/* Check if the fragment is rounded to 8 bytes.
		 * Required by the RFC.
		 */
		if (end & 0x7) {
			/* RFC2460 says always send parameter problem in
			 * this case. -DaveM
			 */
			__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
					IPSTATS_MIB_INHDRERRORS);
			icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
					  offsetof(struct ipv6hdr, payload_len));
			return -1;
		}
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.flags & INET_FRAG_LAST_IN)
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
	prev = fq->q.fragments_tail;
	if (!prev || FRAG6_CB(prev)->offset < offset) {
		next = NULL;
		goto found;
	}
	prev = NULL;
	for (next = fq->q.fragments; next != NULL; next = next->next) {
		if (FRAG6_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

found:
	/* RFC5722, Section 4, amended by Errata ID : 3089
	 *                          When reassembling an IPv6 datagram, if
	 *   one or more its constituent fragments is determined to be an
	 *   overlapping fragment, the entire datagram (and any constituent
	 *   fragments) MUST be silently discarded.
	 */

	/* Check for overlap with preceding fragment. */
	if (prev &&
	    (FRAG6_CB(prev)->offset + prev->len) > offset)
		goto discard_fq;

	/* Look for overlap with succeeding segment. */
	if (next && FRAG6_CB(next)->offset < end)
		goto discard_fq;

	FRAG6_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (!next)
		fq->q.fragments_tail = skb;
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
	fq->ecn |= ecn;
	add_frag_mem_limit(fq->q.net, skb->truesize);

	fragsize = -skb_network_offset(skb) + skb->len;
	if (fragsize > fq->q.max_size)
		fq->q.max_size = fragsize;

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.flags |= INET_FRAG_FIRST_IN;
	}

	if (fq->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		int res;
		unsigned long orefdst = skb->_skb_refdst;

		skb->_skb_refdst = 0UL;
		res = ip6_frag_reasm(fq, prev, dev);
		skb->_skb_refdst = orefdst;
		return res;
	}

	skb_dst_drop(skb);
	return -1;

discard_fq:
	inet_frag_kill(&fq->q, &ip6_frags);
err:
	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
			IPSTATS_MIB_REASMFAILS);
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
	struct net *net = container_of(fq->q.net, struct net, ipv6.frags);
	struct sk_buff *fp, *head = fq->q.fragments;
	int    payload_len;
	unsigned int nhoff;
	int sum_truesize;
	u8 ecn;

	inet_frag_kill(&fq->q, &ip6_frags);

	ecn = ip_frag_ecn_table[fq->ecn];
	if (unlikely(ecn == 0xff))
		goto out_fail;

	/* Make the one we just received the head. */
	if (prev) {
		head = prev->next;
		fp = skb_clone(head, GFP_ATOMIC);

		if (!fp)
			goto out_oom;

		fp->next = head->next;
		if (!fp->next)
			fq->q.fragments_tail = fp;
		prev->next = fp;

		skb_morph(head, fq->q.fragments);
		head->next = fq->q.fragments->next;

		consume_skb(fq->q.fragments);
		fq->q.fragments = head;
	}

	WARN_ON(head == NULL);
	WARN_ON(FRAG6_CB(head)->offset != 0);

	/* Unfragmented part is taken from the first segment. */
	payload_len = ((head->data - skb_network_header(head)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN)
		goto out_oversize;

	/* Head of list must not be cloned. */
	if (skb_unclone(head, GFP_ATOMIC))
		goto out_oom;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_has_frag_list(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		clone = alloc_skb(0, GFP_ATOMIC);
		if (!clone)
			goto out_oom;
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_frag_list_init(head);
		for (i = 0; i < skb_shinfo(head)->nr_frags; i++)
			plen += skb_frag_size(&skb_shinfo(head)->frags[i]);
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		add_frag_mem_limit(fq->q.net, clone->truesize);
	}

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	nhoff = fq->nhoffset;
	skb_network_header(head)[nhoff] = skb_transport_header(head)[0];
	memmove(head->head + sizeof(struct frag_hdr), head->head,
		(head->data - head->head) - sizeof(struct frag_hdr));
	if (skb_mac_header_was_set(head))
		head->mac_header += sizeof(struct frag_hdr);
	head->network_header += sizeof(struct frag_hdr);

	skb_reset_transport_header(head);
	skb_push(head, head->data - skb_network_header(head));

	sum_truesize = head->truesize;
	for (fp = head->next; fp;) {
		bool headstolen;
		int delta;
		struct sk_buff *next = fp->next;

		sum_truesize += fp->truesize;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);

		if (skb_try_coalesce(head, fp, &headstolen, &delta)) {
			kfree_skb_partial(fp, headstolen);
		} else {
			if (!skb_shinfo(head)->frag_list)
				skb_shinfo(head)->frag_list = fp;
			head->data_len += fp->len;
			head->len += fp->len;
			head->truesize += fp->truesize;
		}
		fp = next;
	}
	sub_frag_mem_limit(fq->q.net, sum_truesize);

	head->next = NULL;
	head->dev = dev;
	head->tstamp = fq->q.stamp;
	ipv6_hdr(head)->payload_len = htons(payload_len);
	ipv6_change_dsfield(ipv6_hdr(head), 0xff, ecn);
	IP6CB(head)->nhoff = nhoff;
	IP6CB(head)->flags |= IP6SKB_FRAGMENTED;
	IP6CB(head)->frag_max_size = fq->q.max_size;

	/* Yes, and fold redundant checksum back. 8) */
	skb_postpush_rcsum(head, skb_network_header(head),
			   skb_network_header_len(head));

	rcu_read_lock();
	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMOKS);
	rcu_read_unlock();
	fq->q.fragments = NULL;
	fq->q.fragments_tail = NULL;
	return 1;

out_oversize:
	net_dbg_ratelimited("ip6_frag_reasm: payload len = %d\n", payload_len);
	goto out_fail;
out_oom:
	net_dbg_ratelimited("ip6_frag_reasm: no memory for reassembly\n");
out_fail:
	rcu_read_lock();
	__IP6_INC_STATS(net, __in6_dev_get(dev), IPSTATS_MIB_REASMFAILS);
	rcu_read_unlock();
	return -1;
}

static int ipv6_frag_rcv(struct sk_buff *skb)
{
	struct frag_hdr *fhdr;
	struct frag_queue *fq;
	const struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct net *net = dev_net(skb_dst(skb)->dev);

	if (IP6CB(skb)->flags & IP6SKB_FRAGMENTED)
		goto fail_hdr;

	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMREQDS);

	/* Jumbo payload inhibits frag. header */
	if (hdr->payload_len == 0)
		goto fail_hdr;

	if (!pskb_may_pull(skb, (skb_transport_offset(skb) +
				 sizeof(struct frag_hdr))))
		goto fail_hdr;

	hdr = ipv6_hdr(skb);
	fhdr = (struct frag_hdr *)skb_transport_header(skb);

	if (!(fhdr->frag_off & htons(0xFFF9))) {
		/* It is not a fragmented frame */
		skb->transport_header += sizeof(struct frag_hdr);
		__IP6_INC_STATS(net,
				ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMOKS);

		IP6CB(skb)->nhoff = (u8 *)fhdr - skb_network_header(skb);
		IP6CB(skb)->flags |= IP6SKB_FRAGMENTED;
		return 1;
	}

	fq = fq_find(net, fhdr->identification, &hdr->saddr, &hdr->daddr,
		     skb->dev ? skb->dev->ifindex : 0, ip6_frag_ecn(hdr));
	if (fq) {
		int ret;

		spin_lock(&fq->q.lock);

		ret = ip6_frag_queue(fq, skb, fhdr, IP6CB(skb)->nhoff);

		spin_unlock(&fq->q.lock);
		inet_frag_put(&fq->q, &ip6_frags);
		return ret;
	}

	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -1;

fail_hdr:
	__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
			IPSTATS_MIB_INHDRERRORS);
	icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, skb_network_header_len(skb));
	return -1;
}

static const struct inet6_protocol frag_protocol = {
	.handler	=	ipv6_frag_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

#ifdef CONFIG_SYSCTL
static int zero;

static struct ctl_table ip6_frags_ns_ctl_table[] = {
	{
		.procname	= "ip6frag_high_thresh",
		.data		= &init_net.ipv6.frags.high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &init_net.ipv6.frags.low_thresh
	},
	{
		.procname	= "ip6frag_low_thresh",
		.data		= &init_net.ipv6.frags.low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &init_net.ipv6.frags.high_thresh
	},
	{
		.procname	= "ip6frag_time",
		.data		= &init_net.ipv6.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

/* secret interval has been deprecated */
static int ip6_frags_secret_interval_unused;
static struct ctl_table ip6_frags_ctl_table[] = {
	{
		.procname	= "ip6frag_secret_interval",
		.data		= &ip6_frags_secret_interval_unused,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

static int __net_init ip6_frags_ns_sysctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = ip6_frags_ns_ctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(ip6_frags_ns_ctl_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->ipv6.frags.high_thresh;
		table[0].extra1 = &net->ipv6.frags.low_thresh;
		table[0].extra2 = &init_net.ipv6.frags.high_thresh;
		table[1].data = &net->ipv6.frags.low_thresh;
		table[1].extra2 = &net->ipv6.frags.high_thresh;
		table[2].data = &net->ipv6.frags.timeout;

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			table[0].procname = NULL;
	}

	hdr = register_net_sysctl(net, "net/ipv6", table);
	if (!hdr)
		goto err_reg;

	net->ipv6.sysctl.frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit ip6_frags_ns_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->ipv6.sysctl.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv6.sysctl.frags_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}

static struct ctl_table_header *ip6_ctl_header;

static int ip6_frags_sysctl_register(void)
{
	ip6_ctl_header = register_net_sysctl(&init_net, "net/ipv6",
			ip6_frags_ctl_table);
	return ip6_ctl_header == NULL ? -ENOMEM : 0;
}

static void ip6_frags_sysctl_unregister(void)
{
	unregister_net_sysctl_table(ip6_ctl_header);
}
#else
static int ip6_frags_ns_sysctl_register(struct net *net)
{
	return 0;
}

static void ip6_frags_ns_sysctl_unregister(struct net *net)
{
}

static int ip6_frags_sysctl_register(void)
{
	return 0;
}

static void ip6_frags_sysctl_unregister(void)
{
}
#endif

static int __net_init ipv6_frags_init_net(struct net *net)
{
	net->ipv6.frags.high_thresh = IPV6_FRAG_HIGH_THRESH;
	net->ipv6.frags.low_thresh = IPV6_FRAG_LOW_THRESH;
	net->ipv6.frags.timeout = IPV6_FRAG_TIMEOUT;

	inet_frags_init_net(&net->ipv6.frags);

	return ip6_frags_ns_sysctl_register(net);
}

static void __net_exit ipv6_frags_exit_net(struct net *net)
{
	ip6_frags_ns_sysctl_unregister(net);
	inet_frags_exit_net(&net->ipv6.frags, &ip6_frags);
}

static struct pernet_operations ip6_frags_ops = {
	.init = ipv6_frags_init_net,
	.exit = ipv6_frags_exit_net,
	.async = true,
};

int __init ipv6_frag_init(void)
{
	int ret;

	ret = inet6_add_protocol(&frag_protocol, IPPROTO_FRAGMENT);
	if (ret)
		goto out;

	ret = ip6_frags_sysctl_register();
	if (ret)
		goto err_sysctl;

	ret = register_pernet_subsys(&ip6_frags_ops);
	if (ret)
		goto err_pernet;

	ip6_frags.hashfn = ip6_hashfn;
	ip6_frags.constructor = ip6_frag_init;
	ip6_frags.destructor = NULL;
	ip6_frags.qsize = sizeof(struct frag_queue);
	ip6_frags.match = ip6_frag_match;
	ip6_frags.frag_expire = ip6_frag_expire;
	ip6_frags.frags_cache_name = ip6_frag_cache_name;
	ret = inet_frags_init(&ip6_frags);
	if (ret)
		goto err_pernet;
out:
	return ret;

err_pernet:
	ip6_frags_sysctl_unregister();
err_sysctl:
	inet6_del_protocol(&frag_protocol, IPPROTO_FRAGMENT);
	goto out;
}

void ipv6_frag_exit(void)
{
	inet_frags_fini(&ip6_frags);
	ip6_frags_sysctl_unregister();
	unregister_pernet_subsys(&ip6_frags_ops);
	inet6_del_protocol(&frag_protocol, IPPROTO_FRAGMENT);
}
