/*	6LoWPAN fragment reassembly
 *
 *
 *	Authors:
 *	Alexander Aring		<aar@pengutronix.de>
 *
 *	Based on: net/ipv6/reassembly.c
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "6LoWPAN: " fmt

#include <linux/net.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/ieee802154_netdev.h>
#include <net/6lowpan.h>
#include <net/ipv6.h>
#include <net/inet_frag.h>

#include "6lowpan_i.h"

static const char lowpan_frags_cache_name[] = "lowpan-frags";

static struct inet_frags lowpan_frags;

static int lowpan_frag_reasm(struct lowpan_frag_queue *fq,
			     struct sk_buff *prev, struct net_device *ldev);

static void lowpan_frag_init(struct inet_frag_queue *q, const void *a)
{
	const struct frag_lowpan_compare_key *key = a;
	struct lowpan_frag_queue *fq;

	fq = container_of(q, struct lowpan_frag_queue, q);

	BUILD_BUG_ON(sizeof(*key) > sizeof(q->key));
	memcpy(&q->key, key, sizeof(*key));
}

static void lowpan_frag_expire(struct timer_list *t)
{
	struct inet_frag_queue *frag = from_timer(frag, t, timer);
	struct frag_queue *fq;
	struct net *net;

	fq = container_of(frag, struct frag_queue, q);
	net = container_of(fq->q.net, struct net, ieee802154_lowpan.frags);

	spin_lock(&fq->q.lock);

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto out;

	inet_frag_kill(&fq->q);
out:
	spin_unlock(&fq->q.lock);
	inet_frag_put(&fq->q);
}

static inline struct lowpan_frag_queue *
fq_find(struct net *net, const struct lowpan_802154_cb *cb,
	const struct ieee802154_addr *src,
	const struct ieee802154_addr *dst)
{
	struct netns_ieee802154_lowpan *ieee802154_lowpan =
		net_ieee802154_lowpan(net);
	struct frag_lowpan_compare_key key = {
		.tag = cb->d_tag,
		.d_size = cb->d_size,
		.src = *src,
		.dst = *dst,
	};
	struct inet_frag_queue *q;

	q = inet_frag_find(&ieee802154_lowpan->frags, &key);
	if (IS_ERR_OR_NULL(q)) {
		inet_frag_maybe_warn_overflow(q, pr_fmt());
		return NULL;
	}
	return container_of(q, struct lowpan_frag_queue, q);
}

static int lowpan_frag_queue(struct lowpan_frag_queue *fq,
			     struct sk_buff *skb, u8 frag_type)
{
	struct sk_buff *prev, *next;
	struct net_device *ldev;
	int end, offset;

	if (fq->q.flags & INET_FRAG_COMPLETE)
		goto err;

	offset = lowpan_802154_cb(skb)->d_offset << 3;
	end = lowpan_802154_cb(skb)->d_size;

	/* Is this the final fragment? */
	if (offset + skb->len == end) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrupted.
		 */
		if (end < fq->q.len ||
		    ((fq->q.flags & INET_FRAG_LAST_IN) && end != fq->q.len))
			goto err;
		fq->q.flags |= INET_FRAG_LAST_IN;
		fq->q.len = end;
	} else {
		if (end > fq->q.len) {
			/* Some bits beyond end -> corruption. */
			if (fq->q.flags & INET_FRAG_LAST_IN)
				goto err;
			fq->q.len = end;
		}
	}

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = fq->q.fragments_tail;
	if (!prev ||
	    lowpan_802154_cb(prev)->d_offset <
	    lowpan_802154_cb(skb)->d_offset) {
		next = NULL;
		goto found;
	}
	prev = NULL;
	for (next = fq->q.fragments; next != NULL; next = next->next) {
		if (lowpan_802154_cb(next)->d_offset >=
		    lowpan_802154_cb(skb)->d_offset)
			break;	/* bingo! */
		prev = next;
	}

found:
	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (!next)
		fq->q.fragments_tail = skb;
	if (prev)
		prev->next = skb;
	else
		fq->q.fragments = skb;

	ldev = skb->dev;
	if (ldev)
		skb->dev = NULL;

	fq->q.stamp = skb->tstamp;
	if (frag_type == LOWPAN_DISPATCH_FRAG1)
		fq->q.flags |= INET_FRAG_FIRST_IN;

	fq->q.meat += skb->len;
	add_frag_mem_limit(fq->q.net, skb->truesize);

	if (fq->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    fq->q.meat == fq->q.len) {
		int res;
		unsigned long orefdst = skb->_skb_refdst;

		skb->_skb_refdst = 0UL;
		res = lowpan_frag_reasm(fq, prev, ldev);
		skb->_skb_refdst = orefdst;
		return res;
	}

	return -1;
err:
	kfree_skb(skb);
	return -1;
}

/*	Check if this packet is complete.
 *	Returns NULL on failure by any reason, and pointer
 *	to current nexthdr field in reassembled frame.
 *
 *	It is called with locked fq, and caller must check that
 *	queue is eligible for reassembly i.e. it is not COMPLETE,
 *	the last and the first frames arrived and all the bits are here.
 */
static int lowpan_frag_reasm(struct lowpan_frag_queue *fq, struct sk_buff *prev,
			     struct net_device *ldev)
{
	struct sk_buff *fp, *head = fq->q.fragments;
	int sum_truesize;

	inet_frag_kill(&fq->q);

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

	/* Head of list must not be cloned. */
	if (skb_unclone(head, GFP_ATOMIC))
		goto out_oom;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments.
	 */
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
		clone->len = head->data_len - plen;
		clone->data_len = clone->len;
		head->data_len -= clone->len;
		head->len -= clone->len;
		add_frag_mem_limit(fq->q.net, clone->truesize);
	}

	WARN_ON(head == NULL);

	sum_truesize = head->truesize;
	for (fp = head->next; fp;) {
		bool headstolen;
		int delta;
		struct sk_buff *next = fp->next;

		sum_truesize += fp->truesize;
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
	head->dev = ldev;
	head->tstamp = fq->q.stamp;

	fq->q.fragments = NULL;
	fq->q.fragments_tail = NULL;

	return 1;
out_oom:
	net_dbg_ratelimited("lowpan_frag_reasm: no memory for reassembly\n");
	return -1;
}

static int lowpan_frag_rx_handlers_result(struct sk_buff *skb,
					  lowpan_rx_result res)
{
	switch (res) {
	case RX_QUEUED:
		return NET_RX_SUCCESS;
	case RX_CONTINUE:
		/* nobody cared about this packet */
		net_warn_ratelimited("%s: received unknown dispatch\n",
				     __func__);

		/* fall-through */
	default:
		/* all others failure */
		return NET_RX_DROP;
	}
}

static lowpan_rx_result lowpan_frag_rx_h_iphc(struct sk_buff *skb)
{
	int ret;

	if (!lowpan_is_iphc(*skb_network_header(skb)))
		return RX_CONTINUE;

	ret = lowpan_iphc_decompress(skb);
	if (ret < 0)
		return RX_DROP;

	return RX_QUEUED;
}

static int lowpan_invoke_frag_rx_handlers(struct sk_buff *skb)
{
	lowpan_rx_result res;

#define CALL_RXH(rxh)			\
	do {				\
		res = rxh(skb);	\
		if (res != RX_CONTINUE)	\
			goto rxh_next;	\
	} while (0)

	/* likely at first */
	CALL_RXH(lowpan_frag_rx_h_iphc);
	CALL_RXH(lowpan_rx_h_ipv6);

rxh_next:
	return lowpan_frag_rx_handlers_result(skb, res);
#undef CALL_RXH
}

#define LOWPAN_FRAG_DGRAM_SIZE_HIGH_MASK	0x07
#define LOWPAN_FRAG_DGRAM_SIZE_HIGH_SHIFT	8

static int lowpan_get_cb(struct sk_buff *skb, u8 frag_type,
			 struct lowpan_802154_cb *cb)
{
	bool fail;
	u8 high = 0, low = 0;
	__be16 d_tag = 0;

	fail = lowpan_fetch_skb(skb, &high, 1);
	fail |= lowpan_fetch_skb(skb, &low, 1);
	/* remove the dispatch value and use first three bits as high value
	 * for the datagram size
	 */
	cb->d_size = (high & LOWPAN_FRAG_DGRAM_SIZE_HIGH_MASK) <<
		LOWPAN_FRAG_DGRAM_SIZE_HIGH_SHIFT | low;
	fail |= lowpan_fetch_skb(skb, &d_tag, 2);
	cb->d_tag = ntohs(d_tag);

	if (frag_type == LOWPAN_DISPATCH_FRAGN) {
		fail |= lowpan_fetch_skb(skb, &cb->d_offset, 1);
	} else {
		skb_reset_network_header(skb);
		cb->d_offset = 0;
		/* check if datagram_size has ipv6hdr on FRAG1 */
		fail |= cb->d_size < sizeof(struct ipv6hdr);
		/* check if we can dereference the dispatch value */
		fail |= !skb->len;
	}

	if (unlikely(fail))
		return -EIO;

	return 0;
}

int lowpan_frag_rcv(struct sk_buff *skb, u8 frag_type)
{
	struct lowpan_frag_queue *fq;
	struct net *net = dev_net(skb->dev);
	struct lowpan_802154_cb *cb = lowpan_802154_cb(skb);
	struct ieee802154_hdr hdr;
	int err;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0)
		goto err;

	err = lowpan_get_cb(skb, frag_type, cb);
	if (err < 0)
		goto err;

	if (frag_type == LOWPAN_DISPATCH_FRAG1) {
		err = lowpan_invoke_frag_rx_handlers(skb);
		if (err == NET_RX_DROP)
			goto err;
	}

	if (cb->d_size > IPV6_MIN_MTU) {
		net_warn_ratelimited("lowpan_frag_rcv: datagram size exceeds MTU\n");
		goto err;
	}

	fq = fq_find(net, cb, &hdr.source, &hdr.dest);
	if (fq != NULL) {
		int ret;

		spin_lock(&fq->q.lock);
		ret = lowpan_frag_queue(fq, skb, frag_type);
		spin_unlock(&fq->q.lock);

		inet_frag_put(&fq->q);
		return ret;
	}

err:
	kfree_skb(skb);
	return -1;
}

#ifdef CONFIG_SYSCTL
static int zero;

static struct ctl_table lowpan_frags_ns_ctl_table[] = {
	{
		.procname	= "6lowpanfrag_high_thresh",
		.data		= &init_net.ieee802154_lowpan.frags.high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &init_net.ieee802154_lowpan.frags.low_thresh
	},
	{
		.procname	= "6lowpanfrag_low_thresh",
		.data		= &init_net.ieee802154_lowpan.frags.low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &init_net.ieee802154_lowpan.frags.high_thresh
	},
	{
		.procname	= "6lowpanfrag_time",
		.data		= &init_net.ieee802154_lowpan.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

/* secret interval has been deprecated */
static int lowpan_frags_secret_interval_unused;
static struct ctl_table lowpan_frags_ctl_table[] = {
	{
		.procname	= "6lowpanfrag_secret_interval",
		.data		= &lowpan_frags_secret_interval_unused,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

static int __net_init lowpan_frags_ns_sysctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;
	struct netns_ieee802154_lowpan *ieee802154_lowpan =
		net_ieee802154_lowpan(net);

	table = lowpan_frags_ns_ctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(lowpan_frags_ns_ctl_table),
				GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;

		table[0].data = &ieee802154_lowpan->frags.high_thresh;
		table[0].extra1 = &ieee802154_lowpan->frags.low_thresh;
		table[0].extra2 = &init_net.ieee802154_lowpan.frags.high_thresh;
		table[1].data = &ieee802154_lowpan->frags.low_thresh;
		table[1].extra2 = &ieee802154_lowpan->frags.high_thresh;
		table[2].data = &ieee802154_lowpan->frags.timeout;

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			table[0].procname = NULL;
	}

	hdr = register_net_sysctl(net, "net/ieee802154/6lowpan", table);
	if (hdr == NULL)
		goto err_reg;

	ieee802154_lowpan->sysctl.frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit lowpan_frags_ns_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;
	struct netns_ieee802154_lowpan *ieee802154_lowpan =
		net_ieee802154_lowpan(net);

	table = ieee802154_lowpan->sysctl.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(ieee802154_lowpan->sysctl.frags_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}

static struct ctl_table_header *lowpan_ctl_header;

static int __init lowpan_frags_sysctl_register(void)
{
	lowpan_ctl_header = register_net_sysctl(&init_net,
						"net/ieee802154/6lowpan",
						lowpan_frags_ctl_table);
	return lowpan_ctl_header == NULL ? -ENOMEM : 0;
}

static void lowpan_frags_sysctl_unregister(void)
{
	unregister_net_sysctl_table(lowpan_ctl_header);
}
#else
static inline int lowpan_frags_ns_sysctl_register(struct net *net)
{
	return 0;
}

static inline void lowpan_frags_ns_sysctl_unregister(struct net *net)
{
}

static inline int __init lowpan_frags_sysctl_register(void)
{
	return 0;
}

static inline void lowpan_frags_sysctl_unregister(void)
{
}
#endif

static int __net_init lowpan_frags_init_net(struct net *net)
{
	struct netns_ieee802154_lowpan *ieee802154_lowpan =
		net_ieee802154_lowpan(net);
	int res;

	ieee802154_lowpan->frags.high_thresh = IPV6_FRAG_HIGH_THRESH;
	ieee802154_lowpan->frags.low_thresh = IPV6_FRAG_LOW_THRESH;
	ieee802154_lowpan->frags.timeout = IPV6_FRAG_TIMEOUT;
	ieee802154_lowpan->frags.f = &lowpan_frags;

	res = inet_frags_init_net(&ieee802154_lowpan->frags);
	if (res < 0)
		return res;
	res = lowpan_frags_ns_sysctl_register(net);
	if (res < 0)
		inet_frags_exit_net(&ieee802154_lowpan->frags);
	return res;
}

static void __net_exit lowpan_frags_exit_net(struct net *net)
{
	struct netns_ieee802154_lowpan *ieee802154_lowpan =
		net_ieee802154_lowpan(net);

	lowpan_frags_ns_sysctl_unregister(net);
	inet_frags_exit_net(&ieee802154_lowpan->frags);
}

static struct pernet_operations lowpan_frags_ops = {
	.init = lowpan_frags_init_net,
	.exit = lowpan_frags_exit_net,
};

static u32 lowpan_key_hashfn(const void *data, u32 len, u32 seed)
{
	return jhash2(data,
		      sizeof(struct frag_lowpan_compare_key) / sizeof(u32), seed);
}

static u32 lowpan_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct inet_frag_queue *fq = data;

	return jhash2((const u32 *)&fq->key,
		      sizeof(struct frag_lowpan_compare_key) / sizeof(u32), seed);
}

static int lowpan_obj_cmpfn(struct rhashtable_compare_arg *arg, const void *ptr)
{
	const struct frag_lowpan_compare_key *key = arg->key;
	const struct inet_frag_queue *fq = ptr;

	return !!memcmp(&fq->key, key, sizeof(*key));
}

static const struct rhashtable_params lowpan_rhash_params = {
	.head_offset		= offsetof(struct inet_frag_queue, node),
	.hashfn			= lowpan_key_hashfn,
	.obj_hashfn		= lowpan_obj_hashfn,
	.obj_cmpfn		= lowpan_obj_cmpfn,
	.automatic_shrinking	= true,
};

int __init lowpan_net_frag_init(void)
{
	int ret;

	lowpan_frags.constructor = lowpan_frag_init;
	lowpan_frags.destructor = NULL;
	lowpan_frags.qsize = sizeof(struct frag_queue);
	lowpan_frags.frag_expire = lowpan_frag_expire;
	lowpan_frags.frags_cache_name = lowpan_frags_cache_name;
	lowpan_frags.rhash_params = lowpan_rhash_params;
	ret = inet_frags_init(&lowpan_frags);
	if (ret)
		goto out;

	ret = lowpan_frags_sysctl_register();
	if (ret)
		goto err_sysctl;

	ret = register_pernet_subsys(&lowpan_frags_ops);
	if (ret)
		goto err_pernet;
out:
	return ret;
err_pernet:
	lowpan_frags_sysctl_unregister();
err_sysctl:
	inet_frags_fini(&lowpan_frags);
	return ret;
}

void lowpan_net_frag_exit(void)
{
	inet_frags_fini(&lowpan_frags);
	lowpan_frags_sysctl_unregister();
	unregister_pernet_subsys(&lowpan_frags_ops);
}
