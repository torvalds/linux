// SPDX-License-Identifier: GPL-2.0
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 *		David S. Miller :	Begin massive cleanup...
 *		Andi Kleen	:	Add sysctls.
 *		xxxx		:	Overlapfrag bug.
 *		Ultima          :       ip_expire() kernel panic.
 *		Bill Hawes	:	Frag accounting and evictor fixes.
 *		John McDonald	:	0 length frag bug.
 *		Alexey Kuznetsov:	SMP races, threading, cleanup.
 *		Patrick McHardy :	LRU queue of frag heads for evictor.
 */

#define pr_fmt(fmt) "IPv4: " fmt

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/route.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/inetpeer.h>
#include <net/inet_frag.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>
#include <net/inet_ecn.h>
#include <net/l3mdev.h>

/* NOTE. Logic of IP defragmentation is parallel to corresponding IPv6
 * code now. If you change something here, _PLEASE_ update ipv6/reassembly.c
 * as well. Or notify me, at least. --ANK
 */
static const char ip_frag_cache_name[] = "ip4-frags";

/* Use skb->cb to track consecutive/adjacent fragments coming at
 * the end of the queue. Nodes in the rb-tree queue will
 * contain "runs" of one or more adjacent fragments.
 *
 * Invariants:
 * - next_frag is NULL at the tail of a "run";
 * - the head of a "run" has the sum of all fragment lengths in frag_run_len.
 */
struct ipfrag_skb_cb {
	struct inet_skb_parm	h;
	struct sk_buff		*next_frag;
	int			frag_run_len;
};

#define FRAG_CB(skb)		((struct ipfrag_skb_cb *)((skb)->cb))

static void ip4_frag_init_run(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ipfrag_skb_cb) > sizeof(skb->cb));

	FRAG_CB(skb)->next_frag = NULL;
	FRAG_CB(skb)->frag_run_len = skb->len;
}

/* Append skb to the last "run". */
static void ip4_frag_append_to_last_run(struct inet_frag_queue *q,
					struct sk_buff *skb)
{
	RB_CLEAR_NODE(&skb->rbnode);
	FRAG_CB(skb)->next_frag = NULL;

	FRAG_CB(q->last_run_head)->frag_run_len += skb->len;
	FRAG_CB(q->fragments_tail)->next_frag = skb;
	q->fragments_tail = skb;
}

/* Create a new "run" with the skb. */
static void ip4_frag_create_run(struct inet_frag_queue *q, struct sk_buff *skb)
{
	if (q->last_run_head)
		rb_link_node(&skb->rbnode, &q->last_run_head->rbnode,
			     &q->last_run_head->rbnode.rb_right);
	else
		rb_link_node(&skb->rbnode, NULL, &q->rb_fragments.rb_node);
	rb_insert_color(&skb->rbnode, &q->rb_fragments);

	ip4_frag_init_run(skb);
	q->fragments_tail = skb;
	q->last_run_head = skb;
}

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq {
	struct inet_frag_queue q;

	u8		ecn; /* RFC3168 support */
	u16		max_df_size; /* largest frag with DF set seen */
	int             iif;
	unsigned int    rid;
	struct inet_peer *peer;
};

static u8 ip4_frag_ecn(u8 tos)
{
	return 1 << (tos & INET_ECN_MASK);
}

static struct inet_frags ip4_frags;

static int ip_frag_reasm(struct ipq *qp, struct sk_buff *skb,
			 struct sk_buff *prev_tail, struct net_device *dev);


static void ip4_frag_init(struct inet_frag_queue *q, const void *a)
{
	struct ipq *qp = container_of(q, struct ipq, q);
	struct netns_ipv4 *ipv4 = container_of(q->net, struct netns_ipv4,
					       frags);
	struct net *net = container_of(ipv4, struct net, ipv4);

	const struct frag_v4_compare_key *key = a;

	q->key.v4 = *key;
	qp->ecn = 0;
	qp->peer = q->net->max_dist ?
		inet_getpeer_v4(net->ipv4.peers, key->saddr, key->vif, 1) :
		NULL;
}

static void ip4_frag_free(struct inet_frag_queue *q)
{
	struct ipq *qp;

	qp = container_of(q, struct ipq, q);
	if (qp->peer)
		inet_putpeer(qp->peer);
}


/* Destruction primitives. */

static void ipq_put(struct ipq *ipq)
{
	inet_frag_put(&ipq->q);
}

/* Kill ipq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static void ipq_kill(struct ipq *ipq)
{
	inet_frag_kill(&ipq->q);
}

static bool frag_expire_skip_icmp(u32 user)
{
	return user == IP_DEFRAG_AF_PACKET ||
	       ip_defrag_user_in_between(user, IP_DEFRAG_CONNTRACK_IN,
					 __IP_DEFRAG_CONNTRACK_IN_END) ||
	       ip_defrag_user_in_between(user, IP_DEFRAG_CONNTRACK_BRIDGE_IN,
					 __IP_DEFRAG_CONNTRACK_BRIDGE_IN);
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(struct timer_list *t)
{
	struct inet_frag_queue *frag = from_timer(frag, t, timer);
	const struct iphdr *iph;
	struct sk_buff *head = NULL;
	struct net *net;
	struct ipq *qp;
	int err;

	qp = container_of(frag, struct ipq, q);
	net = container_of(qp->q.net, struct net, ipv4.frags);

	rcu_read_lock();
	spin_lock(&qp->q.lock);

	if (qp->q.flags & INET_FRAG_COMPLETE)
		goto out;

	ipq_kill(qp);
	__IP_INC_STATS(net, IPSTATS_MIB_REASMFAILS);
	__IP_INC_STATS(net, IPSTATS_MIB_REASMTIMEOUT);

	if (!(qp->q.flags & INET_FRAG_FIRST_IN))
		goto out;

	/* sk_buff::dev and sk_buff::rbnode are unionized. So we
	 * pull the head out of the tree in order to be able to
	 * deal with head->dev.
	 */
	if (qp->q.fragments) {
		head = qp->q.fragments;
		qp->q.fragments = head->next;
	} else {
		head = skb_rb_first(&qp->q.rb_fragments);
		if (!head)
			goto out;
		if (FRAG_CB(head)->next_frag)
			rb_replace_node(&head->rbnode,
					&FRAG_CB(head)->next_frag->rbnode,
					&qp->q.rb_fragments);
		else
			rb_erase(&head->rbnode, &qp->q.rb_fragments);
		memset(&head->rbnode, 0, sizeof(head->rbnode));
		barrier();
	}
	if (head == qp->q.fragments_tail)
		qp->q.fragments_tail = NULL;

	sub_frag_mem_limit(qp->q.net, head->truesize);

	head->dev = dev_get_by_index_rcu(net, qp->iif);
	if (!head->dev)
		goto out;


	/* skb has no dst, perform route lookup again */
	iph = ip_hdr(head);
	err = ip_route_input_noref(head, iph->daddr, iph->saddr,
					   iph->tos, head->dev);
	if (err)
		goto out;

	/* Only an end host needs to send an ICMP
	 * "Fragment Reassembly Timeout" message, per RFC792.
	 */
	if (frag_expire_skip_icmp(qp->q.key.v4.user) &&
	    (skb_rtable(head)->rt_type != RTN_LOCAL))
		goto out;

	spin_unlock(&qp->q.lock);
	icmp_send(head, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);
	goto out_rcu_unlock;

out:
	spin_unlock(&qp->q.lock);
out_rcu_unlock:
	rcu_read_unlock();
	if (head)
		kfree_skb(head);
	ipq_put(qp);
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and create new one, if nothing is found.
 */
static struct ipq *ip_find(struct net *net, struct iphdr *iph,
			   u32 user, int vif)
{
	struct frag_v4_compare_key key = {
		.saddr = iph->saddr,
		.daddr = iph->daddr,
		.user = user,
		.vif = vif,
		.id = iph->id,
		.protocol = iph->protocol,
	};
	struct inet_frag_queue *q;

	q = inet_frag_find(&net->ipv4.frags, &key);
	if (!q)
		return NULL;

	return container_of(q, struct ipq, q);
}

/* Is the fragment too far ahead to be part of ipq? */
static int ip_frag_too_far(struct ipq *qp)
{
	struct inet_peer *peer = qp->peer;
	unsigned int max = qp->q.net->max_dist;
	unsigned int start, end;

	int rc;

	if (!peer || !max)
		return 0;

	start = qp->rid;
	end = atomic_inc_return(&peer->rid);
	qp->rid = end;

	rc = qp->q.fragments_tail && (end - start) > max;

	if (rc) {
		struct net *net;

		net = container_of(qp->q.net, struct net, ipv4.frags);
		__IP_INC_STATS(net, IPSTATS_MIB_REASMFAILS);
	}

	return rc;
}

static int ip_frag_reinit(struct ipq *qp)
{
	unsigned int sum_truesize = 0;

	if (!mod_timer(&qp->q.timer, jiffies + qp->q.net->timeout)) {
		refcount_inc(&qp->q.refcnt);
		return -ETIMEDOUT;
	}

	sum_truesize = inet_frag_rbtree_purge(&qp->q.rb_fragments);
	sub_frag_mem_limit(qp->q.net, sum_truesize);

	qp->q.flags = 0;
	qp->q.len = 0;
	qp->q.meat = 0;
	qp->q.fragments = NULL;
	qp->q.rb_fragments = RB_ROOT;
	qp->q.fragments_tail = NULL;
	qp->q.last_run_head = NULL;
	qp->iif = 0;
	qp->ecn = 0;

	return 0;
}

/* Add new segment to existing queue. */
static int ip_frag_queue(struct ipq *qp, struct sk_buff *skb)
{
	struct net *net = container_of(qp->q.net, struct net, ipv4.frags);
	struct rb_node **rbn, *parent;
	struct sk_buff *skb1, *prev_tail;
	struct net_device *dev;
	unsigned int fragsize;
	int flags, offset;
	int ihl, end;
	int err = -ENOENT;
	u8 ecn;

	if (qp->q.flags & INET_FRAG_COMPLETE)
		goto err;

	if (!(IPCB(skb)->flags & IPSKB_FRAG_COMPLETE) &&
	    unlikely(ip_frag_too_far(qp)) &&
	    unlikely(err = ip_frag_reinit(qp))) {
		ipq_kill(qp);
		goto err;
	}

	ecn = ip4_frag_ecn(ip_hdr(skb)->tos);
	offset = ntohs(ip_hdr(skb)->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	offset <<= 3;		/* offset is in 8-byte chunks */
	ihl = ip_hdrlen(skb);

	/* Determine the position of this fragment. */
	end = offset + skb->len - skb_network_offset(skb) - ihl;
	err = -EINVAL;

	/* Is this the final fragment? */
	if ((flags & IP_MF) == 0) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrupted.
		 */
		if (end < qp->q.len ||
		    ((qp->q.flags & INET_FRAG_LAST_IN) && end != qp->q.len))
			goto err;
		qp->q.flags |= INET_FRAG_LAST_IN;
		qp->q.len = end;
	} else {
		if (end&7) {
			end &= ~7;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
		if (end > qp->q.len) {
			/* Some bits beyond end -> corruption. */
			if (qp->q.flags & INET_FRAG_LAST_IN)
				goto err;
			qp->q.len = end;
		}
	}
	if (end == offset)
		goto err;

	err = -ENOMEM;
	if (!pskb_pull(skb, skb_network_offset(skb) + ihl))
		goto err;

	err = pskb_trim_rcsum(skb, end - offset);
	if (err)
		goto err;

	/* Note : skb->rbnode and skb->dev share the same location. */
	dev = skb->dev;
	/* Makes sure compiler wont do silly aliasing games */
	barrier();

	/* RFC5722, Section 4, amended by Errata ID : 3089
	 *                          When reassembling an IPv6 datagram, if
	 *   one or more its constituent fragments is determined to be an
	 *   overlapping fragment, the entire datagram (and any constituent
	 *   fragments) MUST be silently discarded.
	 *
	 * We do the same here for IPv4 (and increment an snmp counter).
	 */

	/* Find out where to put this fragment.  */
	prev_tail = qp->q.fragments_tail;
	if (!prev_tail)
		ip4_frag_create_run(&qp->q, skb);  /* First fragment. */
	else if (prev_tail->ip_defrag_offset + prev_tail->len < end) {
		/* This is the common case: skb goes to the end. */
		/* Detect and discard overlaps. */
		if (offset < prev_tail->ip_defrag_offset + prev_tail->len)
			goto discard_qp;
		if (offset == prev_tail->ip_defrag_offset + prev_tail->len)
			ip4_frag_append_to_last_run(&qp->q, skb);
		else
			ip4_frag_create_run(&qp->q, skb);
	} else {
		/* Binary search. Note that skb can become the first fragment,
		 * but not the last (covered above).
		 */
		rbn = &qp->q.rb_fragments.rb_node;
		do {
			parent = *rbn;
			skb1 = rb_to_skb(parent);
			if (end <= skb1->ip_defrag_offset)
				rbn = &parent->rb_left;
			else if (offset >= skb1->ip_defrag_offset +
						FRAG_CB(skb1)->frag_run_len)
				rbn = &parent->rb_right;
			else /* Found an overlap with skb1. */
				goto discard_qp;
		} while (*rbn);
		/* Here we have parent properly set, and rbn pointing to
		 * one of its NULL left/right children. Insert skb.
		 */
		ip4_frag_init_run(skb);
		rb_link_node(&skb->rbnode, parent, rbn);
		rb_insert_color(&skb->rbnode, &qp->q.rb_fragments);
	}

	if (dev)
		qp->iif = dev->ifindex;
	skb->ip_defrag_offset = offset;

	qp->q.stamp = skb->tstamp;
	qp->q.meat += skb->len;
	qp->ecn |= ecn;
	add_frag_mem_limit(qp->q.net, skb->truesize);
	if (offset == 0)
		qp->q.flags |= INET_FRAG_FIRST_IN;

	fragsize = skb->len + ihl;

	if (fragsize > qp->q.max_size)
		qp->q.max_size = fragsize;

	if (ip_hdr(skb)->frag_off & htons(IP_DF) &&
	    fragsize > qp->max_df_size)
		qp->max_df_size = fragsize;

	if (qp->q.flags == (INET_FRAG_FIRST_IN | INET_FRAG_LAST_IN) &&
	    qp->q.meat == qp->q.len) {
		unsigned long orefdst = skb->_skb_refdst;

		skb->_skb_refdst = 0UL;
		err = ip_frag_reasm(qp, skb, prev_tail, dev);
		skb->_skb_refdst = orefdst;
		return err;
	}

	skb_dst_drop(skb);
	return -EINPROGRESS;

discard_qp:
	inet_frag_kill(&qp->q);
	err = -EINVAL;
	__IP_INC_STATS(net, IPSTATS_MIB_REASM_OVERLAPS);
err:
	kfree_skb(skb);
	return err;
}

/* Build a new IP datagram from all its fragments. */
static int ip_frag_reasm(struct ipq *qp, struct sk_buff *skb,
			 struct sk_buff *prev_tail, struct net_device *dev)
{
	struct net *net = container_of(qp->q.net, struct net, ipv4.frags);
	struct iphdr *iph;
	struct sk_buff *fp, *head = skb_rb_first(&qp->q.rb_fragments);
	struct sk_buff **nextp; /* To build frag_list. */
	struct rb_node *rbn;
	int len;
	int ihlen;
	int err;
	u8 ecn;

	ipq_kill(qp);

	ecn = ip_frag_ecn_table[qp->ecn];
	if (unlikely(ecn == 0xff)) {
		err = -EINVAL;
		goto out_fail;
	}
	/* Make the one we just received the head. */
	if (head != skb) {
		fp = skb_clone(skb, GFP_ATOMIC);
		if (!fp)
			goto out_nomem;
		FRAG_CB(fp)->next_frag = FRAG_CB(skb)->next_frag;
		if (RB_EMPTY_NODE(&skb->rbnode))
			FRAG_CB(prev_tail)->next_frag = fp;
		else
			rb_replace_node(&skb->rbnode, &fp->rbnode,
					&qp->q.rb_fragments);
		if (qp->q.fragments_tail == skb)
			qp->q.fragments_tail = fp;
		skb_morph(skb, head);
		FRAG_CB(skb)->next_frag = FRAG_CB(head)->next_frag;
		rb_replace_node(&head->rbnode, &skb->rbnode,
				&qp->q.rb_fragments);
		consume_skb(head);
		head = skb;
	}

	WARN_ON(head->ip_defrag_offset != 0);

	/* Allocate a new buffer for the datagram. */
	ihlen = ip_hdrlen(head);
	len = ihlen + qp->q.len;

	err = -E2BIG;
	if (len > 65535)
		goto out_oversize;

	/* Head of list must not be cloned. */
	if (skb_unclone(head, GFP_ATOMIC))
		goto out_nomem;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_has_frag_list(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		clone = alloc_skb(0, GFP_ATOMIC);
		if (!clone)
			goto out_nomem;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_frag_list_init(head);
		for (i = 0; i < skb_shinfo(head)->nr_frags; i++)
			plen += skb_frag_size(&skb_shinfo(head)->frags[i]);
		clone->len = clone->data_len = head->data_len - plen;
		head->truesize += clone->truesize;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		add_frag_mem_limit(qp->q.net, clone->truesize);
		skb_shinfo(head)->frag_list = clone;
		nextp = &clone->next;
	} else {
		nextp = &skb_shinfo(head)->frag_list;
	}

	skb_push(head, head->data - skb_network_header(head));

	/* Traverse the tree in order, to build frag_list. */
	fp = FRAG_CB(head)->next_frag;
	rbn = rb_next(&head->rbnode);
	rb_erase(&head->rbnode, &qp->q.rb_fragments);
	while (rbn || fp) {
		/* fp points to the next sk_buff in the current run;
		 * rbn points to the next run.
		 */
		/* Go through the current run. */
		while (fp) {
			*nextp = fp;
			nextp = &fp->next;
			fp->prev = NULL;
			memset(&fp->rbnode, 0, sizeof(fp->rbnode));
			fp->sk = NULL;
			head->data_len += fp->len;
			head->len += fp->len;
			if (head->ip_summed != fp->ip_summed)
				head->ip_summed = CHECKSUM_NONE;
			else if (head->ip_summed == CHECKSUM_COMPLETE)
				head->csum = csum_add(head->csum, fp->csum);
			head->truesize += fp->truesize;
			fp = FRAG_CB(fp)->next_frag;
		}
		/* Move to the next run. */
		if (rbn) {
			struct rb_node *rbnext = rb_next(rbn);

			fp = rb_to_skb(rbn);
			rb_erase(rbn, &qp->q.rb_fragments);
			rbn = rbnext;
		}
	}
	sub_frag_mem_limit(qp->q.net, head->truesize);

	*nextp = NULL;
	head->next = NULL;
	head->prev = NULL;
	head->dev = dev;
	head->tstamp = qp->q.stamp;
	IPCB(head)->frag_max_size = max(qp->max_df_size, qp->q.max_size);

	iph = ip_hdr(head);
	iph->tot_len = htons(len);
	iph->tos |= ecn;

	/* When we set IP_DF on a refragmented skb we must also force a
	 * call to ip_fragment to avoid forwarding a DF-skb of size s while
	 * original sender only sent fragments of size f (where f < s).
	 *
	 * We only set DF/IPSKB_FRAG_PMTU if such DF fragment was the largest
	 * frag seen to avoid sending tiny DF-fragments in case skb was built
	 * from one very small df-fragment and one large non-df frag.
	 */
	if (qp->max_df_size == qp->q.max_size) {
		IPCB(head)->flags |= IPSKB_FRAG_PMTU;
		iph->frag_off = htons(IP_DF);
	} else {
		iph->frag_off = 0;
	}

	ip_send_check(iph);

	__IP_INC_STATS(net, IPSTATS_MIB_REASMOKS);
	qp->q.fragments = NULL;
	qp->q.rb_fragments = RB_ROOT;
	qp->q.fragments_tail = NULL;
	qp->q.last_run_head = NULL;
	return 0;

out_nomem:
	net_dbg_ratelimited("queue_glue: no memory for gluing queue %p\n", qp);
	err = -ENOMEM;
	goto out_fail;
out_oversize:
	net_info_ratelimited("Oversized IP packet from %pI4\n", &qp->q.key.v4.saddr);
out_fail:
	__IP_INC_STATS(net, IPSTATS_MIB_REASMFAILS);
	return err;
}

/* Process an incoming IP datagram fragment. */
int ip_defrag(struct net *net, struct sk_buff *skb, u32 user)
{
	struct net_device *dev = skb->dev ? : skb_dst(skb)->dev;
	int vif = l3mdev_master_ifindex_rcu(dev);
	struct ipq *qp;

	__IP_INC_STATS(net, IPSTATS_MIB_REASMREQDS);
	skb_orphan(skb);

	/* Lookup (or create) queue header */
	qp = ip_find(net, ip_hdr(skb), user, vif);
	if (qp) {
		int ret;

		spin_lock(&qp->q.lock);

		ret = ip_frag_queue(qp, skb);

		spin_unlock(&qp->q.lock);
		ipq_put(qp);
		return ret;
	}

	__IP_INC_STATS(net, IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -ENOMEM;
}
EXPORT_SYMBOL(ip_defrag);

struct sk_buff *ip_check_defrag(struct net *net, struct sk_buff *skb, u32 user)
{
	struct iphdr iph;
	int netoff;
	u32 len;

	if (skb->protocol != htons(ETH_P_IP))
		return skb;

	netoff = skb_network_offset(skb);

	if (skb_copy_bits(skb, netoff, &iph, sizeof(iph)) < 0)
		return skb;

	if (iph.ihl < 5 || iph.version != 4)
		return skb;

	len = ntohs(iph.tot_len);
	if (skb->len < netoff + len || len < (iph.ihl * 4))
		return skb;

	if (ip_is_fragment(&iph)) {
		skb = skb_share_check(skb, GFP_ATOMIC);
		if (skb) {
			if (!pskb_may_pull(skb, netoff + iph.ihl * 4)) {
				kfree_skb(skb);
				return NULL;
			}
			if (pskb_trim_rcsum(skb, netoff + len)) {
				kfree_skb(skb);
				return NULL;
			}
			memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
			if (ip_defrag(net, skb, user))
				return NULL;
			skb_clear_hash(skb);
		}
	}
	return skb;
}
EXPORT_SYMBOL(ip_check_defrag);

unsigned int inet_frag_rbtree_purge(struct rb_root *root)
{
	struct rb_node *p = rb_first(root);
	unsigned int sum = 0;

	while (p) {
		struct sk_buff *skb = rb_entry(p, struct sk_buff, rbnode);

		p = rb_next(p);
		rb_erase(&skb->rbnode, root);
		while (skb) {
			struct sk_buff *next = FRAG_CB(skb)->next_frag;

			sum += skb->truesize;
			kfree_skb(skb);
			skb = next;
		}
	}
	return sum;
}
EXPORT_SYMBOL(inet_frag_rbtree_purge);

#ifdef CONFIG_SYSCTL
static int dist_min;

static struct ctl_table ip4_frags_ns_ctl_table[] = {
	{
		.procname	= "ipfrag_high_thresh",
		.data		= &init_net.ipv4.frags.high_thresh,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &init_net.ipv4.frags.low_thresh
	},
	{
		.procname	= "ipfrag_low_thresh",
		.data		= &init_net.ipv4.frags.low_thresh,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra2		= &init_net.ipv4.frags.high_thresh
	},
	{
		.procname	= "ipfrag_time",
		.data		= &init_net.ipv4.frags.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "ipfrag_max_dist",
		.data		= &init_net.ipv4.frags.max_dist,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &dist_min,
	},
	{ }
};

/* secret interval has been deprecated */
static int ip4_frags_secret_interval_unused;
static struct ctl_table ip4_frags_ctl_table[] = {
	{
		.procname	= "ipfrag_secret_interval",
		.data		= &ip4_frags_secret_interval_unused,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};

static int __net_init ip4_frags_ns_ctl_register(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = ip4_frags_ns_ctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(ip4_frags_ns_ctl_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->ipv4.frags.high_thresh;
		table[0].extra1 = &net->ipv4.frags.low_thresh;
		table[0].extra2 = &init_net.ipv4.frags.high_thresh;
		table[1].data = &net->ipv4.frags.low_thresh;
		table[1].extra2 = &net->ipv4.frags.high_thresh;
		table[2].data = &net->ipv4.frags.timeout;
		table[3].data = &net->ipv4.frags.max_dist;
	}

	hdr = register_net_sysctl(net, "net/ipv4", table);
	if (!hdr)
		goto err_reg;

	net->ipv4.frags_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit ip4_frags_ns_ctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->ipv4.frags_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.frags_hdr);
	kfree(table);
}

static void __init ip4_frags_ctl_register(void)
{
	register_net_sysctl(&init_net, "net/ipv4", ip4_frags_ctl_table);
}
#else
static int ip4_frags_ns_ctl_register(struct net *net)
{
	return 0;
}

static void ip4_frags_ns_ctl_unregister(struct net *net)
{
}

static void __init ip4_frags_ctl_register(void)
{
}
#endif

static int __net_init ipv4_frags_init_net(struct net *net)
{
	int res;

	/* Fragment cache limits.
	 *
	 * The fragment memory accounting code, (tries to) account for
	 * the real memory usage, by measuring both the size of frag
	 * queue struct (inet_frag_queue (ipv4:ipq/ipv6:frag_queue))
	 * and the SKB's truesize.
	 *
	 * A 64K fragment consumes 129736 bytes (44*2944)+200
	 * (1500 truesize == 2944, sizeof(struct ipq) == 200)
	 *
	 * We will commit 4MB at one time. Should we cross that limit
	 * we will prune down to 3MB, making room for approx 8 big 64K
	 * fragments 8x128k.
	 */
	net->ipv4.frags.high_thresh = 4 * 1024 * 1024;
	net->ipv4.frags.low_thresh  = 3 * 1024 * 1024;
	/*
	 * Important NOTE! Fragment queue must be destroyed before MSL expires.
	 * RFC791 is wrong proposing to prolongate timer each fragment arrival
	 * by TTL.
	 */
	net->ipv4.frags.timeout = IP_FRAG_TIME;

	net->ipv4.frags.max_dist = 64;
	net->ipv4.frags.f = &ip4_frags;

	res = inet_frags_init_net(&net->ipv4.frags);
	if (res < 0)
		return res;
	res = ip4_frags_ns_ctl_register(net);
	if (res < 0)
		inet_frags_exit_net(&net->ipv4.frags);
	return res;
}

static void __net_exit ipv4_frags_exit_net(struct net *net)
{
	ip4_frags_ns_ctl_unregister(net);
	inet_frags_exit_net(&net->ipv4.frags);
}

static struct pernet_operations ip4_frags_ops = {
	.init = ipv4_frags_init_net,
	.exit = ipv4_frags_exit_net,
};


static u32 ip4_key_hashfn(const void *data, u32 len, u32 seed)
{
	return jhash2(data,
		      sizeof(struct frag_v4_compare_key) / sizeof(u32), seed);
}

static u32 ip4_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct inet_frag_queue *fq = data;

	return jhash2((const u32 *)&fq->key.v4,
		      sizeof(struct frag_v4_compare_key) / sizeof(u32), seed);
}

static int ip4_obj_cmpfn(struct rhashtable_compare_arg *arg, const void *ptr)
{
	const struct frag_v4_compare_key *key = arg->key;
	const struct inet_frag_queue *fq = ptr;

	return !!memcmp(&fq->key, key, sizeof(*key));
}

static const struct rhashtable_params ip4_rhash_params = {
	.head_offset		= offsetof(struct inet_frag_queue, node),
	.key_offset		= offsetof(struct inet_frag_queue, key),
	.key_len		= sizeof(struct frag_v4_compare_key),
	.hashfn			= ip4_key_hashfn,
	.obj_hashfn		= ip4_obj_hashfn,
	.obj_cmpfn		= ip4_obj_cmpfn,
	.automatic_shrinking	= true,
};

void __init ipfrag_init(void)
{
	ip4_frags.constructor = ip4_frag_init;
	ip4_frags.destructor = ip4_frag_free;
	ip4_frags.qsize = sizeof(struct ipq);
	ip4_frags.frag_expire = ip_expire;
	ip4_frags.frags_cache_name = ip_frag_cache_name;
	ip4_frags.rhash_params = ip4_rhash_params;
	if (inet_frags_init(&ip4_frags))
		panic("IP: failed to allocate ip4_frags cache\n");
	ip4_frags_ctl_register();
	register_pernet_subsys(&ip4_frags_ops);
}
