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

#include <net/sock.h>
#include <net/snmp.h>
#include <net/inet_frag.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define NF_CT_FRAG6_HIGH_THRESH 262144 /* == 256*1024 */
#define NF_CT_FRAG6_LOW_THRESH 196608  /* == 192*1024 */
#define NF_CT_FRAG6_TIMEOUT IPV6_FRAG_TIMEOUT

struct nf_ct_frag6_skb_cb
{
	struct inet6_skb_parm	h;
	int			offset;
	struct sk_buff		*orig;
};

#define NFCT_FRAG6_CB(skb)	((struct nf_ct_frag6_skb_cb*)((skb)->cb))

struct nf_ct_frag6_queue
{
	struct inet_frag_queue	q;

	__be32			id;		/* fragment id		*/
	u32			user;
	struct in6_addr		saddr;
	struct in6_addr		daddr;

	unsigned int		csum;
	__u16			nhoffset;
};

static struct inet_frags nf_frags;
static struct netns_frags nf_init_frags;

#ifdef CONFIG_SYSCTL
struct ctl_table nf_ct_ipv6_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_frag6_timeout",
		.data		= &nf_init_frags.timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_FRAG6_LOW_THRESH,
		.procname	= "nf_conntrack_frag6_low_thresh",
		.data		= &nf_init_frags.low_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_FRAG6_HIGH_THRESH,
		.procname	= "nf_conntrack_frag6_high_thresh",
		.data		= &nf_init_frags.high_thresh,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ .ctl_name = 0 }
};
#endif

static unsigned int nf_hashfn(struct inet_frag_queue *q)
{
	const struct nf_ct_frag6_queue *nq;

	nq = container_of(q, struct nf_ct_frag6_queue, q);
	return inet6_hash_frag(nq->id, &nq->saddr, &nq->daddr, nf_frags.rnd);
}

static void nf_skb_free(struct sk_buff *skb)
{
	if (NFCT_FRAG6_CB(skb)->orig)
		kfree_skb(NFCT_FRAG6_CB(skb)->orig);
}

/* Memory Tracking Functions. */
static inline void frag_kfree_skb(struct sk_buff *skb, unsigned int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &nf_init_frags.mem);
	nf_skb_free(skb);
	kfree_skb(skb);
}

/* Destruction primitives. */

static __inline__ void fq_put(struct nf_ct_frag6_queue *fq)
{
	inet_frag_put(&fq->q, &nf_frags);
}

/* Kill fq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static __inline__ void fq_kill(struct nf_ct_frag6_queue *fq)
{
	inet_frag_kill(&fq->q, &nf_frags);
}

static void nf_ct_frag6_evictor(void)
{
	local_bh_disable();
	inet_frag_evictor(&nf_init_frags, &nf_frags);
	local_bh_enable();
}

static void nf_ct_frag6_expire(unsigned long data)
{
	struct nf_ct_frag6_queue *fq;

	fq = container_of((struct inet_frag_queue *)data,
			struct nf_ct_frag6_queue, q);

	spin_lock(&fq->q.lock);

	if (fq->q.last_in & INET_FRAG_COMPLETE)
		goto out;

	fq_kill(fq);

out:
	spin_unlock(&fq->q.lock);
	fq_put(fq);
}

/* Creation primitives. */

static __inline__ struct nf_ct_frag6_queue *
fq_find(__be32 id, u32 user, struct in6_addr *src, struct in6_addr *dst)
{
	struct inet_frag_queue *q;
	struct ip6_create_arg arg;
	unsigned int hash;

	arg.id = id;
	arg.user = user;
	arg.src = src;
	arg.dst = dst;

	read_lock_bh(&nf_frags.lock);
	hash = inet6_hash_frag(id, src, dst, nf_frags.rnd);

	q = inet_frag_find(&nf_init_frags, &nf_frags, &arg, hash);
	local_bh_enable();
	if (q == NULL)
		goto oom;

	return container_of(q, struct nf_ct_frag6_queue, q);

oom:
	pr_debug("Can't alloc new queue\n");
	return NULL;
}


static int nf_ct_frag6_queue(struct nf_ct_frag6_queue *fq, struct sk_buff *skb,
			     const struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	int offset, end;

	if (fq->q.last_in & INET_FRAG_COMPLETE) {
		pr_debug("Allready completed\n");
		goto err;
	}

	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (ntohs(ipv6_hdr(skb)->payload_len) -
			((u8 *)(fhdr + 1) - (u8 *)(ipv6_hdr(skb) + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		pr_debug("offset is too large.\n");
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
		    ((fq->q.last_in & INET_FRAG_LAST_IN) && end != fq->q.len)) {
			pr_debug("already received last fragment\n");
			goto err;
		}
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
			pr_debug("end of fragment not rounded to 8 bytes.\n");
			return -1;
		}
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.last_in & INET_FRAG_LAST_IN) {
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
	prev = NULL;
	for (next = fq->q.fragments; next != NULL; next = next->next) {
		if (NFCT_FRAG6_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if (prev) {
		int i = (NFCT_FRAG6_CB(prev)->offset + prev->len) - offset;

		if (i > 0) {
			offset += i;
			if (end <= offset) {
				pr_debug("overlap\n");
				goto err;
			}
			if (!pskb_pull(skb, i)) {
				pr_debug("Can't pull\n");
				goto err;
			}
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	/* Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */
	while (next && NFCT_FRAG6_CB(next)->offset < end) {
		/* overlap is 'i' bytes */
		int i = end - NFCT_FRAG6_CB(next)->offset;

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			pr_debug("Eat head of the overlapped parts.: %d", i);
			if (!pskb_pull(next, i))
				goto err;

			/* next fragment */
			NFCT_FRAG6_CB(next)->offset += i;
			fq->q.meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)
				next->ip_summed = CHECKSUM_NONE;
			break;
		} else {
			struct sk_buff *free_it = next;

			/* Old fragmnet is completely overridden with
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

	NFCT_FRAG6_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		fq->q.fragments = skb;

	skb->dev = NULL;
	fq->q.stamp = skb->tstamp;
	fq->q.meat += skb->len;
	atomic_add(skb->truesize, &nf_init_frags.mem);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.last_in |= INET_FRAG_FIRST_IN;
	}
	write_lock(&nf_frags.lock);
	list_move_tail(&fq->q.lru_list, &nf_init_frags.lru_list);
	write_unlock(&nf_frags.lock);
	return 0;

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
nf_ct_frag6_reasm(struct nf_ct_frag6_queue *fq, struct net_device *dev)
{
	struct sk_buff *fp, *op, *head = fq->q.fragments;
	int    payload_len;

	fq_kill(fq);

	WARN_ON(head == NULL);
	WARN_ON(NFCT_FRAG6_CB(head)->offset != 0);

	/* Unfragmented part is taken from the first segment. */
	payload_len = ((head->data - skb_network_header(head)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN) {
		pr_debug("payload len is too large.\n");
		goto out_oversize;
	}

	/* Head of list must not be cloned. */
	if (skb_cloned(head) && pskb_expand_head(head, 0, 0, GFP_ATOMIC)) {
		pr_debug("skb is cloned but can't expand head");
		goto out_oom;
	}

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_has_frags(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		if ((clone = alloc_skb(0, GFP_ATOMIC)) == NULL) {
			pr_debug("Can't alloc skb\n");
			goto out_oom;
		}
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_frag_list_init(head);
		for (i=0; i<skb_shinfo(head)->nr_frags; i++)
			plen += skb_shinfo(head)->frags[i].size;
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;

		NFCT_FRAG6_CB(clone)->orig = NULL;
		atomic_add(clone->truesize, &nf_init_frags.mem);
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
	atomic_sub(head->truesize, &nf_init_frags.mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &nf_init_frags.mem);
	}

	head->next = NULL;
	head->dev = dev;
	head->tstamp = fq->q.stamp;
	ipv6_hdr(head)->payload_len = htons(payload_len);

	/* Yes, and fold redundant checksum back. 8) */
	if (head->ip_summed == CHECKSUM_COMPLETE)
		head->csum = csum_partial(skb_network_header(head),
					  skb_network_header_len(head),
					  head->csum);

	fq->q.fragments = NULL;

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
	if (net_ratelimit())
		printk(KERN_DEBUG "nf_ct_frag6_reasm: payload len = %d\n", payload_len);
	goto out_fail;
out_oom:
	if (net_ratelimit())
		printk(KERN_DEBUG "nf_ct_frag6_reasm: no memory for reassembly\n");
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
	struct frag_hdr *fhdr;
	struct nf_ct_frag6_queue *fq;
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

	if (atomic_read(&nf_init_frags.mem) > nf_init_frags.high_thresh)
		nf_ct_frag6_evictor();

	fq = fq_find(fhdr->identification, user, &hdr->saddr, &hdr->daddr);
	if (fq == NULL) {
		pr_debug("Can't find and can't create new queue\n");
		goto ret_orig;
	}

	spin_lock_bh(&fq->q.lock);

	if (nf_ct_frag6_queue(fq, clone, fhdr, nhoff) < 0) {
		spin_unlock_bh(&fq->q.lock);
		pr_debug("Can't insert skb to queue\n");
		fq_put(fq);
		goto ret_orig;
	}

	if (fq->q.last_in == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		ret_skb = nf_ct_frag6_reasm(fq, dev);
		if (ret_skb == NULL)
			pr_debug("Can't reassemble fragmented packets\n");
	}
	spin_unlock_bh(&fq->q.lock);

	fq_put(fq);
	return ret_skb;

ret_orig:
	kfree_skb(clone);
	return skb;
}

void nf_ct_frag6_output(unsigned int hooknum, struct sk_buff *skb,
			struct net_device *in, struct net_device *out,
			int (*okfn)(struct sk_buff *))
{
	struct sk_buff *s, *s2;

	for (s = NFCT_FRAG6_CB(skb)->orig; s;) {
		nf_conntrack_put_reasm(s->nfct_reasm);
		nf_conntrack_get_reasm(skb);
		s->nfct_reasm = skb;

		s2 = s->next;
		s->next = NULL;

		NF_HOOK_THRESH(PF_INET6, hooknum, s, in, out, okfn,
			       NF_IP6_PRI_CONNTRACK_DEFRAG + 1);
		s = s2;
	}
	nf_conntrack_put_reasm(skb);
}

int nf_ct_frag6_init(void)
{
	nf_frags.hashfn = nf_hashfn;
	nf_frags.constructor = ip6_frag_init;
	nf_frags.destructor = NULL;
	nf_frags.skb_free = nf_skb_free;
	nf_frags.qsize = sizeof(struct nf_ct_frag6_queue);
	nf_frags.match = ip6_frag_match;
	nf_frags.frag_expire = nf_ct_frag6_expire;
	nf_frags.secret_interval = 10 * 60 * HZ;
	nf_init_frags.timeout = IPV6_FRAG_TIMEOUT;
	nf_init_frags.high_thresh = 256 * 1024;
	nf_init_frags.low_thresh = 192 * 1024;
	inet_frags_init_net(&nf_init_frags);
	inet_frags_init(&nf_frags);

	return 0;
}

void nf_ct_frag6_cleanup(void)
{
	inet_frags_fini(&nf_frags);

	nf_init_frags.low_thresh = 0;
	nf_ct_frag6_evictor();
}
