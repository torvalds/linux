// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IPv6 fragment reassembly for connection tracking
 *
 * Copyright (C)2004 USAGI/WIDE Project
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv6/reassembly.c
 */

#define pr_fmt(fmt) "IPv6-nf: " fmt

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/slab.h>

#include <net/ipv6_frag.h>

#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include <net/netns/generic.h>

static const char nf_frags_cache_name[] = "nf-frags";

unsigned int nf_frag_pernet_id __read_mostly;
static struct inet_frags nf_frags;

static struct nft_ct_frag6_pernet *nf_frag_pernet(struct net *net)
{
	return net_generic(net, nf_frag_pernet_id);
}

#ifdef CONFIG_SYSCTL

static struct ctl_table nf_ct_frag6_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_frag6_timeout",
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_frag6_low_thresh",
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "nf_conntrack_frag6_high_thresh",
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{ }
};

static int nf_ct_frag6_sysctl_register(struct net *net)
{
	struct nft_ct_frag6_pernet *nf_frag;
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = nf_ct_frag6_sysctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(nf_ct_frag6_sysctl_table),
				GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;
	}

	nf_frag = nf_frag_pernet(net);

	table[0].data	= &nf_frag->fqdir->timeout;
	table[1].data	= &nf_frag->fqdir->low_thresh;
	table[1].extra2	= &nf_frag->fqdir->high_thresh;
	table[2].data	= &nf_frag->fqdir->high_thresh;
	table[2].extra1	= &nf_frag->fqdir->low_thresh;
	table[2].extra2	= &nf_frag->fqdir->high_thresh;

	hdr = register_net_sysctl(net, "net/netfilter", table);
	if (hdr == NULL)
		goto err_reg;

	nf_frag->nf_frag_frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit nf_ct_frags6_sysctl_unregister(struct net *net)
{
	struct nft_ct_frag6_pernet *nf_frag = nf_frag_pernet(net);
	struct ctl_table *table;

	table = nf_frag->nf_frag_frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(nf_frag->nf_frag_frags_hdr);
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

static int nf_ct_frag6_reasm(struct frag_queue *fq, struct sk_buff *skb,
			     struct sk_buff *prev_tail, struct net_device *dev);

static inline u8 ip6_frag_ecn(const struct ipv6hdr *ipv6h)
{
	return 1 << (ipv6_get_dsfield(ipv6h) & INET_ECN_MASK);
}

static void nf_ct_frag6_expire(struct timer_list *t)
{
	struct inet_frag_queue *frag = from_timer(frag, t, timer);
	struct frag_queue *fq;

	fq = container_of(frag, struct frag_queue, q);

	ip6frag_expire_frag_queue(fq->q.fqdir->net, fq);
}

/* Creation primitives. */
static struct frag_queue *fq_find(struct net *net, __be32 id, u32 user,
				  const struct ipv6hdr *hdr, int iif)
{
	struct nft_ct_frag6_pernet *nf_frag = nf_frag_pernet(net);
	struct frag_v6_compare_key key = {
		.id = id,
		.saddr = hdr->saddr,
		.daddr = hdr->daddr,
		.user = user,
		.iif = iif,
	};
	struct inet_frag_queue *q;

	q = inet_frag_find(nf_frag->fqdir, &key);
	if (!q)
		return NULL;

	return container_of(q, struct frag_queue, q);
}


static int nf_ct_frag6_queue(struct frag_queue *fq, struct sk_buff *skb,
			     const struct frag_hdr *fhdr, int nhoff)
{
	unsigned int payload_len;
	struct net_device *dev;
	struct sk_buff *prev;
	int offset, end, err;
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
		return -EINVAL;
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
			inet_frag_kill(&fq->q);
			return -EPROTO;
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

	/* Note : skb->rbnode and skb->dev share the same location. */
	dev = skb->dev;
	/* Makes sure compiler wont do silly aliasing games */
	barrier();

	prev = fq->q.fragments_tail;
	err = inet_frag_queue_insert(&fq->q, skb, offset, end);
	if (err) {
		if (err == IPFRAG_DUP) {
			/* No error for duplicates, pretend they got queued. */
			kfree_skb(skb);
			return -EINPROGRESS;
		}
		goto insert_error;
	}

	if (dev)
		fq->iif = dev->ifindex;

	fq->q.stamp = skb->tstamp;
	fq->q.meat += skb->len;
	fq->ecn |= ecn;
	if (payload_len > fq->q.max_size)
		fq->q.max_size = payload_len;
	add_frag_mem_limit(fq->q.fqdir, skb->truesize);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->q.flags |= INET_FRAG_FIRST_IN;
	}

	if (fq->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		unsigned long orefdst = skb->_skb_refdst;

		skb->_skb_refdst = 0UL;
		err = nf_ct_frag6_reasm(fq, skb, prev, dev);
		skb->_skb_refdst = orefdst;

		/* After queue has assumed skb ownership, only 0 or
		 * -EINPROGRESS must be returned.
		 */
		return err ? -EINPROGRESS : 0;
	}

	skb_dst_drop(skb);
	return -EINPROGRESS;

insert_error:
	inet_frag_kill(&fq->q);
err:
	skb_dst_drop(skb);
	return -EINVAL;
}

/*
 *	Check if this packet is complete.
 *
 *	It is called with locked fq, and caller must check that
 *	queue is eligible for reassembly i.e. it is not COMPLETE,
 *	the last and the first frames arrived and all the bits are here.
 */
static int nf_ct_frag6_reasm(struct frag_queue *fq, struct sk_buff *skb,
			     struct sk_buff *prev_tail, struct net_device *dev)
{
	void *reasm_data;
	int payload_len;
	u8 ecn;

	inet_frag_kill(&fq->q);

	ecn = ip_frag_ecn_table[fq->ecn];
	if (unlikely(ecn == 0xff))
		goto err;

	reasm_data = inet_frag_reasm_prepare(&fq->q, skb, prev_tail);
	if (!reasm_data)
		goto err;

	payload_len = ((skb->data - skb_network_header(skb)) -
		       sizeof(struct ipv6hdr) + fq->q.len -
		       sizeof(struct frag_hdr));
	if (payload_len > IPV6_MAXPLEN) {
		net_dbg_ratelimited("nf_ct_frag6_reasm: payload len = %d\n",
				    payload_len);
		goto err;
	}

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	skb_network_header(skb)[fq->nhoffset] = skb_transport_header(skb)[0];
	memmove(skb->head + sizeof(struct frag_hdr), skb->head,
		(skb->data - skb->head) - sizeof(struct frag_hdr));
	skb->mac_header += sizeof(struct frag_hdr);
	skb->network_header += sizeof(struct frag_hdr);

	skb_reset_transport_header(skb);

	inet_frag_reasm_finish(&fq->q, skb, reasm_data, false);

	skb->ignore_df = 1;
	skb->dev = dev;
	ipv6_hdr(skb)->payload_len = htons(payload_len);
	ipv6_change_dsfield(ipv6_hdr(skb), 0xff, ecn);
	IP6CB(skb)->frag_max_size = sizeof(struct ipv6hdr) + fq->q.max_size;
	IP6CB(skb)->flags |= IP6SKB_FRAGMENTED;

	/* Yes, and fold redundant checksum back. 8) */
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->csum = csum_partial(skb_network_header(skb),
					 skb_network_header_len(skb),
					 skb->csum);

	fq->q.rb_fragments = RB_ROOT;
	fq->q.fragments_tail = NULL;
	fq->q.last_run_head = NULL;

	return 0;

err:
	inet_frag_kill(&fq->q);
	return -EINVAL;
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
			hdrlen = ipv6_authlen(&hdr);
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

int nf_ct_frag6_gather(struct net *net, struct sk_buff *skb, u32 user)
{
	u16 savethdr = skb->transport_header;
	u8 nexthdr = NEXTHDR_FRAGMENT;
	int fhoff, nhoff, ret;
	struct frag_hdr *fhdr;
	struct frag_queue *fq;
	struct ipv6hdr *hdr;
	u8 prevhdr;

	/* Jumbo payload inhibits frag. header */
	if (ipv6_hdr(skb)->payload_len == 0) {
		pr_debug("payload len = 0\n");
		return 0;
	}

	if (find_prev_fhdr(skb, &prevhdr, &nhoff, &fhoff) < 0)
		return 0;

	/* Discard the first fragment if it does not include all headers
	 * RFC 8200, Section 4.5
	 */
	if (ipv6frag_thdr_truncated(skb, fhoff, &nexthdr)) {
		pr_debug("Drop incomplete fragment\n");
		return 0;
	}

	if (!pskb_may_pull(skb, fhoff + sizeof(*fhdr)))
		return -ENOMEM;

	skb_set_transport_header(skb, fhoff);
	hdr = ipv6_hdr(skb);
	fhdr = (struct frag_hdr *)skb_transport_header(skb);

	skb_orphan(skb);
	fq = fq_find(net, fhdr->identification, user, hdr,
		     skb->dev ? skb->dev->ifindex : 0);
	if (fq == NULL) {
		pr_debug("Can't find and can't create new queue\n");
		return -ENOMEM;
	}

	spin_lock_bh(&fq->q.lock);

	ret = nf_ct_frag6_queue(fq, skb, fhdr, nhoff);
	if (ret == -EPROTO) {
		skb->transport_header = savethdr;
		ret = 0;
	}

	spin_unlock_bh(&fq->q.lock);
	inet_frag_put(&fq->q);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_frag6_gather);

static int nf_ct_net_init(struct net *net)
{
	struct nft_ct_frag6_pernet *nf_frag  = nf_frag_pernet(net);
	int res;

	res = fqdir_init(&nf_frag->fqdir, &nf_frags, net);
	if (res < 0)
		return res;

	nf_frag->fqdir->high_thresh = IPV6_FRAG_HIGH_THRESH;
	nf_frag->fqdir->low_thresh = IPV6_FRAG_LOW_THRESH;
	nf_frag->fqdir->timeout = IPV6_FRAG_TIMEOUT;

	res = nf_ct_frag6_sysctl_register(net);
	if (res < 0)
		fqdir_exit(nf_frag->fqdir);
	return res;
}

static void nf_ct_net_pre_exit(struct net *net)
{
	struct nft_ct_frag6_pernet *nf_frag  = nf_frag_pernet(net);

	fqdir_pre_exit(nf_frag->fqdir);
}

static void nf_ct_net_exit(struct net *net)
{
	struct nft_ct_frag6_pernet *nf_frag  = nf_frag_pernet(net);

	nf_ct_frags6_sysctl_unregister(net);
	fqdir_exit(nf_frag->fqdir);
}

static struct pernet_operations nf_ct_net_ops = {
	.init		= nf_ct_net_init,
	.pre_exit	= nf_ct_net_pre_exit,
	.exit		= nf_ct_net_exit,
	.id		= &nf_frag_pernet_id,
	.size		= sizeof(struct nft_ct_frag6_pernet),
};

static const struct rhashtable_params nfct_rhash_params = {
	.head_offset		= offsetof(struct inet_frag_queue, node),
	.hashfn			= ip6frag_key_hashfn,
	.obj_hashfn		= ip6frag_obj_hashfn,
	.obj_cmpfn		= ip6frag_obj_cmpfn,
	.automatic_shrinking	= true,
};

int nf_ct_frag6_init(void)
{
	int ret = 0;

	nf_frags.constructor = ip6frag_init;
	nf_frags.destructor = NULL;
	nf_frags.qsize = sizeof(struct frag_queue);
	nf_frags.frag_expire = nf_ct_frag6_expire;
	nf_frags.frags_cache_name = nf_frags_cache_name;
	nf_frags.rhash_params = nfct_rhash_params;
	ret = inet_frags_init(&nf_frags);
	if (ret)
		goto out;
	ret = register_pernet_subsys(&nf_ct_net_ops);
	if (ret)
		inet_frags_fini(&nf_frags);

out:
	return ret;
}

void nf_ct_frag6_cleanup(void)
{
	unregister_pernet_subsys(&nf_ct_net_ops);
	inet_frags_fini(&nf_frags);
}
