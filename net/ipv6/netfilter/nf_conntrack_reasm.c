/*
 * IPv6 fragment reassembly for connection tracking
 *
 * Copyright (C)2004 USAGI/WIDE Project
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv6/reassembly.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "IPv6-nf: " fmt

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
#include <linux/slab.h>

#include <net/sock.h>
#include <net/snmp.h>
#include <net/inet_frag.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/inet_ecn.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>


struct nf_ct_frag6_skb_cb
{
	struct inet6_skb_parm	h;
	int			offset;
	struct sk_buff		*orig;
};

#define NFCT_FRAG6_CB(skb)	((struct nf_ct_frag6_skb_cb*)((skb)->cb))

static struct inet_frags nf_frags;

#ifdef CONFIG_SYSCTL
static int zero;

static struct ctl_table nf_ct_frag6_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_frag6_timeout",
		.data		= &init_net.nf_frag.frags.timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_frag6_low_thresh",
		.data		= &init_net.nf_frag.frags.low_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &init_net.nf_frag.frags.high_thresh
	},
	{
		.procname	= "nf_conntrack_frag6_high_thresh",
		.data		= &init_net.nf_frag.frags.high_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &init_net.nf_frag.frags.low_thresh
	},
	{ }
};

static int nf_ct_frag6_sysctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = nf_ct_frag6_sysctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(nf_ct_frag6_sysctl_table),
				GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;

		table[0].data = &net->nf_frag.frags.timeout;
		table[1].data = &net->nf_frag.frags.low_thresh;
		table[1].extra2 = &net->nf_frag.frags.high_thresh;
		table[2].data = &net->nf_frag.frags.high_thresh;
		table[2].extra1 = &net->nf_frag.frags.low_thresh;
		table[2].extra2 = &init_net.nf_frag.frags.high_thresh;
	}

	hdr = register_net_sysctl(net, "net/netfilter", table);
	if (hdr == NULL)
		goto err_reg;

	net->nf_frag.sysctl.frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit nf_ct_frags6_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->nf_frag.sysctl.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->nf_frag.sysctl.frags_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}

#else
static int nf_ct_frag6_sysctl_register(struct net *net)
{
	return 0;
}
static void __net_exit nf_ct_frags6_sysctl_unregister(struct net *net)
{
}
#endif

static inline u8 ip6_frag_ecn(const struct ipv6hdr *ipv6h)
{
	return 1 << (ipv6_get_dsfield(ipv6h) & INET_ECN_MASK);
}

static unsigned int nf_hash_frag(__be32 id, const struct in6_addr *saddr,
				 const struct in6_addr *daddr)
{
	net_get_random_once(&nf_frags.rnd, sizeof(nf_frags.rnd));
	return jhash_3words(ipv6_addr_hash(saddr), ipv6_addr_hash(daddr),
			    (__force u32)id, nf_frags.rnd);
}


static unsigned int nf_hashfn(const struct inet_frag_queue *q)
{
	const struct frag_queue *nq;

	nq = container_of(q, struct frag_queue, q);
	return nf_hash_frag(nq->id, &nq->saddr, &nq->daddr);
}

static void nf_skb_free(struct sk_buff *skb)
{
	if (NFCT_FRAG6_CB(skb)->orig)
		kfree_skb(NFCT_FRAG6_CB(skb)->orig);
}

static void nf_ct_frag6_expire(unsigned long data)
{
	struct frag_queue *fq;
	struct net *net;

	fq = container_of((struct inet_frag_queue *)data, struct frag_queue, q);
	net = container_of(fq->q.net, struct net, nf_frag.frags);

	ip6_expire_frag_queue(net, fq, &nf_frags);
}

/* Creation primitives. */
static inline struct frag_queue *fq_find(struct net *net, __be32 id,
					 u32 user, struct in6_addr *src,
					 struct in6_addr *dst, u8 ecn)
{
	struct inet_frag_queue *q;
	struct ip6_create_arg arg;
	unsigned int hash;

	arg.id = id;
	arg.user = user;
	arg.src = src;
	arg.dst = dst;
	arg.ecn = ecn;

	local_bh_disable();
	hash = nf_hash_frag(id, src, dst);

	q = inet_frag_find(&net->nf_frag.frags, &nf_frags, &arg, hash);
	local_bh_enable();
	if (IS_ERR_OR_NULL(q)) {
		inet_frag_maybe_warn_overflow(q, pr_fmt());
		return NULL;
	}
	return container_of(q, struct frag_queue, q);
}


static int nf_ct_frag6_queue(struct frag_queue *fq, struct sk_buff *skb,
			     const struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	unsigned int payload_len;
	int offset, end;
	u8 ecn;

	if (fq->q.flags & INET_FRAG_COMPLETE) {
		pr_debug("Already completed\n");
		goto err;
	}

	payload_len = ntohs(ipv6_hdr(skb)->payload_len);

	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (payload_len -
			((u8 *)(fhdr + 1) - (u8 *)(ipv6_hdr(skb) + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		pr_debug("offset is too large.\n");
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
		    ((fq->q.flags & INET_FRAG_LAST_IN) && end != fq->q.len)) {
			pr_debug("already received last fragment\n");
			goto err;
		}
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
			pr_debug("end of fragment not rounded to 8 bytes.\n");
			return -1;
		}
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.flags & INET_FRAG_LAST_IN) {
				pr_debug("last packet already reached.\n");
				goto err;
			}
			fq->q.len = end;
		}
	}

	if (end == offset)
		goto err;

	/* Point into the IP datagram 'data' part. */
	if (!pskb_pull(skb, (u8 *) (fhdr + 1) - skb->data)) {
		pr_debug("queue: message is too short.\n");
		goto err;
	}
	if (pskb_trim_rcsum(skb, end - offset)) {
		pr_debug("Can't trim\n");
		goto err;
	}

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = fq->q.fragments_tail;
	if (!prev || NFCT_FRAG6_CB(prev)->offset < offset) {
		next = NULL;
		goto found;
	}
	prev = NULL;
	for (next = fq->q.fragments; next != NULL; next = next->next) {
		if (NFCT_FRAG6_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

found:
	/* RFC5722, Section 4:
	 *                                  When reassembling an IPv6 datagram, if
	 *   one or more its constituent fragments is determined to be an
	 *   overlapping fragment, the entire datagram (and any constituent
	 *   fragments, including those not yet received) MUST be silently
	 *   discarded.
	 */

	/* Check for overlap with preceding fragment. */
	if (prev &&
	    (NFCT_FRAG6_CB(prev)->offset + prev->len) > offset)
		goto discard_fq;

	/* Look for overlap with succeeding segment. */
	if (next && NFCT_FRAG6_CB(next)->offset < end)
		goto discard_fq;

	NFCT_FRAG6_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (!next)
		fq->q.fragments_tail = skb;
	if (prev)
		prev->next = skb;
	else
		fq->q.fragments = skb;

	if (skb->dev) {
		fq->iif = skb->dev->ifindex;
		skb->dev = NULL;
	}
	fq->q.stamp = skb->tstamp;
	fq->q.meat += skb->len;
	fq->ecn |= ecn;
	if (payload_len > fq->q.max_size)
		fq->q.max_size = payload_len;
	add_frag_mem_limit(&fq->q, skb->truesize);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.flags |= INET_FRAG_FIRST_IN;
	}

	return 0;

discard_fq:
	inet_frag_kill(&fq->q, &nf_frags);
err:
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
static struct sk_buff *
nf_ct_frag6_reasm(struct frag_queue *fq, struct net_device *dev)
{
	struct sk_buff *fp, *op, *head = fq->q.fragments;
	int    payload_len;
	u8 ecn;

	inet_frag_kill(&fq->q, &nf_frags);

	WARN_ON(head == NULL);
	WARN_ON(NFCT_FRAG6_CB(head)->offset != 0);

	ecn = ip_frag_ecn_table[fq->ecn];
	if (unlikely(ecn == 0xff))
		goto out_fail;

	/* Unfragmented part is taken from the first segment. */
	payload_len = ((head->data - skb_network_header(head)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN) {
		pr_debug("payload len is too large.\n");
		goto out_oversize;
	}

	/* Head of list must not be cloned. */
	if (skb_unclone(head, GFP_ATOMIC)) {
		pr_debug("skb is cloned but can't expand head");
		goto out_oom;
	}

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_has_frag_list(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		clone = alloc_skb(0, GFP_ATOMIC);
		if (clone == NULL)
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

		NFCT_FRAG6_CB(clone)->orig = NULL;
		add_frag_mem_limit(&fq->q, clone->truesize);
	}

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	skb_network_header(head)[fq->nhoffset] = skb_transport_header(head)[0];
	memmove(head->head + sizeof(struct frag_hdr), head->head,
		(head->data - head->head) - sizeof(struct frag_hdr));
	head->mac_header += sizeof(struct frag_hdr);
	head->network_header += sizeof(struct frag_hdr);

	skb_shinfo(head)->frag_list = head->next;
	skb_reset_transport_header(head);
	skb_push(head, head->data - skb_network_header(head));

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
	}
	sub_frag_mem_limit(&fq->q, head->truesize);

	head->ignore_df = 1;
	head->next = NULL;
	head->dev = dev;
	head->tstamp = fq->q.stamp;
	ipv6_hdr(head)->payload_len = htons(payload_len);
	ipv6_change_dsfield(ipv6_hdr(head), 0xff, ecn);
	IP6CB(head)->frag_max_size = sizeof(struct ipv6hdr) + fq->q.max_size;

	/* Yes, and fold redundant checksum back. 8) */
	if (head->ip_summed == CHECKSUM_COMPLETE)
		head->csum = csum_partial(skb_network_header(head),
					  skb_network_header_len(head),
					  head->csum);

	fq->q.fragments = NULL;
	fq->q.fragments_tail = NULL;

	/* all original skbs are linked into the NFCT_FRAG6_CB(head).orig */
	fp = skb_shinfo(head)->frag_list;
	if (fp && NFCT_FRAG6_CB(fp)->orig == NULL)
		/* at above code, head skb is divided into two skbs. */
		fp = fp->next;

	op = NFCT_FRAG6_CB(head)->orig;
	for (; fp; fp = fp->next) {
		struct sk_buff *orig = NFCT_FRAG6_CB(fp)->orig;

		op->next = orig;
		op = orig;
		NFCT_FRAG6_CB(fp)->orig = NULL;
	}

	return head;

out_oversize:
	net_dbg_ratelimited("nf_ct_frag6_reasm: payload len = %d\n",
			    payload_len);
	goto out_fail;
out_oom:
	net_dbg_ratelimited("nf_ct_frag6_reasm: no memory for reassembly\n");
out_fail:
	return NULL;
}

/*
 * find the header just before Fragment Header.
 *
 * if success return 0 and set ...
 * (*prevhdrp): the value of "Next Header Field" in the header
 *		just before Fragment Header.
 * (*prevhoff): the offset of "Next Header Field" in the header
 *		just before Fragment Header.
 * (*fhoff)   : the offset of Fragment Header.
 *
 * Based on ipv6_skip_hdr() in net/ipv6/exthdr.c
 *
 */
static int
find_prev_fhdr(struct sk_buff *skb, u8 *prevhdrp, int *prevhoff, int *fhoff)
{
	u8 nexthdr = ipv6_hdr(skb)->nexthdr;
	const int netoff = skb_network_offset(skb);
	u8 prev_nhoff = netoff + offsetof(struct ipv6hdr, nexthdr);
	int start = netoff + sizeof(struct ipv6hdr);
	int len = skb->len - start;
	u8 prevhdr = NEXTHDR_IPV6;

	while (nexthdr != NEXTHDR_FRAGMENT) {
		struct ipv6_opt_hdr hdr;
		int hdrlen;

		if (!ipv6_ext_hdr(nexthdr)) {
			return -1;
		}
		if (nexthdr == NEXTHDR_NONE) {
			pr_debug("next header is none\n");
			return -1;
		}
		if (len < (int)sizeof(struct ipv6_opt_hdr)) {
			pr_debug("too short\n");
			return -1;
		}
		if (skb_copy_bits(skb, start, &hdr, sizeof(hdr)))
			BUG();
		if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hdr.hdrlen+2)<<2;
		else
			hdrlen = ipv6_optlen(&hdr);

		prevhdr = nexthdr;
		prev_nhoff = start;

		nexthdr = hdr.nexthdr;
		len -= hdrlen;
		start += hdrlen;
	}

	if (len < 0)
		return -1;

	*prevhdrp = prevhdr;
	*prevhoff = prev_nhoff;
	*fhoff = start;

	return 0;
}

struct sk_buff *nf_ct_frag6_gather(struct sk_buff *skb, u32 user)
{
	struct sk_buff *clone;
	struct net_device *dev = skb->dev;
	struct net *net = skb_dst(skb) ? dev_net(skb_dst(skb)->dev)
				       : dev_net(skb->dev);
	struct frag_hdr *fhdr;
	struct frag_queue *fq;
	struct ipv6hdr *hdr;
	int fhoff, nhoff;
	u8 prevhdr;
	struct sk_buff *ret_skb = NULL;

	/* Jumbo payload inhibits frag. header */
	if (ipv6_hdr(skb)->payload_len == 0) {
		pr_debug("payload len = 0\n");
		return skb;
	}

	if (find_prev_fhdr(skb, &prevhdr, &nhoff, &fhoff) < 0)
		return skb;

	clone = skb_clone(skb, GFP_ATOMIC);
	if (clone == NULL) {
		pr_debug("Can't clone skb\n");
		return skb;
	}

	NFCT_FRAG6_CB(clone)->orig = skb;

	if (!pskb_may_pull(clone, fhoff + sizeof(*fhdr))) {
		pr_debug("message is too short.\n");
		goto ret_orig;
	}

	skb_set_transport_header(clone, fhoff);
	hdr = ipv6_hdr(clone);
	fhdr = (struct frag_hdr *)skb_transport_header(clone);

	fq = fq_find(net, fhdr->identification, user, &hdr->saddr, &hdr->daddr,
		     ip6_frag_ecn(hdr));
	if (fq == NULL) {
		pr_debug("Can't find and can't create new queue\n");
		goto ret_orig;
	}

	spin_lock_bh(&fq->q.lock);

	if (nf_ct_frag6_queue(fq, clone, fhdr, nhoff) < 0) {
		spin_unlock_bh(&fq->q.lock);
		pr_debug("Can't insert skb to queue\n");
		inet_frag_put(&fq->q, &nf_frags);
		goto ret_orig;
	}

	if (fq->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		ret_skb = nf_ct_frag6_reasm(fq, dev);
		if (ret_skb == NULL)
			pr_debug("Can't reassemble fragmented packets\n");
	}
	spin_unlock_bh(&fq->q.lock);

	inet_frag_put(&fq->q, &nf_frags);
	return ret_skb;

ret_orig:
	kfree_skb(clone);
	return skb;
}

void nf_ct_frag6_consume_orig(struct sk_buff *skb)
{
	struct sk_buff *s, *s2;

	for (s = NFCT_FRAG6_CB(skb)->orig; s;) {
		s2 = s->next;
		s->next = NULL;
		consume_skb(s);
		s = s2;
	}
}

static int nf_ct_net_init(struct net *net)
{
	net->nf_frag.frags.high_thresh = IPV6_FRAG_HIGH_THRESH;
	net->nf_frag.frags.low_thresh = IPV6_FRAG_LOW_THRESH;
	net->nf_frag.frags.timeout = IPV6_FRAG_TIMEOUT;
	inet_frags_init_net(&net->nf_frag.frags);

	return nf_ct_frag6_sysctl_register(net);
}

static void nf_ct_net_exit(struct net *net)
{
	nf_ct_frags6_sysctl_unregister(net);
	inet_frags_exit_net(&net->nf_frag.frags, &nf_frags);
}

static struct pernet_operations nf_ct_net_ops = {
	.init = nf_ct_net_init,
	.exit = nf_ct_net_exit,
};

int nf_ct_frag6_init(void)
{
	int ret = 0;

	nf_frags.hashfn = nf_hashfn;
	nf_frags.constructor = ip6_frag_init;
	nf_frags.destructor = NULL;
	nf_frags.skb_free = nf_skb_free;
	nf_frags.qsize = sizeof(struct frag_queue);
	nf_frags.match = ip6_frag_match;
	nf_frags.frag_expire = nf_ct_frag6_expire;
	inet_frags_init(&nf_frags);

	ret = register_pernet_subsys(&nf_ct_net_ops);
	if (ret)
		inet_frags_fini(&nf_frags);

	return ret;
}

void nf_ct_frag6_cleanup(void)
{
	unregister_pernet_subsys(&nf_ct_net_ops);
	inet_frags_fini(&nf_frags);
}
