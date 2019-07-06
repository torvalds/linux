// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * inet fragments management
 *
 * 		Authors:	Pavel Emelyanov <xemul@openvz.org>
 *				Started as consolidation of ipv4/ip_fragment.c,
 *				ipv6/reassembly. and ipv6 nf conntrack reassembly
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/rhashtable.h>

#include <net/sock.h>
#include <net/inet_frag.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/ipv6.h>

/* Use skb->cb to track consecutive/adjacent fragments coming at
 * the end of the queue. Nodes in the rb-tree queue will
 * contain "runs" of one or more adjacent fragments.
 *
 * Invariants:
 * - next_frag is NULL at the tail of a "run";
 * - the head of a "run" has the sum of all fragment lengths in frag_run_len.
 */
struct ipfrag_skb_cb {
	union {
		struct inet_skb_parm	h4;
		struct inet6_skb_parm	h6;
	};
	struct sk_buff		*next_frag;
	int			frag_run_len;
};

#define FRAG_CB(skb)		((struct ipfrag_skb_cb *)((skb)->cb))

static void fragcb_clear(struct sk_buff *skb)
{
	RB_CLEAR_NODE(&skb->rbnode);
	FRAG_CB(skb)->next_frag = NULL;
	FRAG_CB(skb)->frag_run_len = skb->len;
}

/* Append skb to the last "run". */
static void fragrun_append_to_last(struct inet_frag_queue *q,
				   struct sk_buff *skb)
{
	fragcb_clear(skb);

	FRAG_CB(q->last_run_head)->frag_run_len += skb->len;
	FRAG_CB(q->fragments_tail)->next_frag = skb;
	q->fragments_tail = skb;
}

/* Create a new "run" with the skb. */
static void fragrun_create(struct inet_frag_queue *q, struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ipfrag_skb_cb) > sizeof(skb->cb));
	fragcb_clear(skb);

	if (q->last_run_head)
		rb_link_node(&skb->rbnode, &q->last_run_head->rbnode,
			     &q->last_run_head->rbnode.rb_right);
	else
		rb_link_node(&skb->rbnode, NULL, &q->rb_fragments.rb_node);
	rb_insert_color(&skb->rbnode, &q->rb_fragments);

	q->fragments_tail = skb;
	q->last_run_head = skb;
}

/* Given the OR values of all fragments, apply RFC 3168 5.3 requirements
 * Value : 0xff if frame should be dropped.
 *         0 or INET_ECN_CE value, to be ORed in to final iph->tos field
 */
const u8 ip_frag_ecn_table[16] = {
	/* at least one fragment had CE, and others ECT_0 or ECT_1 */
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0]			= INET_ECN_CE,
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_1]			= INET_ECN_CE,
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1]	= INET_ECN_CE,

	/* invalid combinations : drop frame */
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_0] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1] = 0xff,
};
EXPORT_SYMBOL(ip_frag_ecn_table);

int inet_frags_init(struct inet_frags *f)
{
	f->frags_cachep = kmem_cache_create(f->frags_cache_name, f->qsize, 0, 0,
					    NULL);
	if (!f->frags_cachep)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_fini(struct inet_frags *f)
{
	/* We must wait that all inet_frag_destroy_rcu() have completed. */
	rcu_barrier();

	kmem_cache_destroy(f->frags_cachep);
	f->frags_cachep = NULL;
}
EXPORT_SYMBOL(inet_frags_fini);

static void inet_frags_free_cb(void *ptr, void *arg)
{
	struct inet_frag_queue *fq = ptr;

	/* If we can not cancel the timer, it means this frag_queue
	 * is already disappearing, we have nothing to do.
	 * Otherwise, we own a refcount until the end of this function.
	 */
	if (!del_timer(&fq->timer))
		return;

	spin_lock_bh(&fq->lock);
	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		fq->flags |= INET_FRAG_COMPLETE;
		refcount_dec(&fq->refcnt);
	}
	spin_unlock_bh(&fq->lock);

	inet_frag_put(fq);
}

void inet_frags_exit_net(struct netns_frags *nf)
{
	nf->high_thresh = 0; /* prevent creation of new frags */

	rhashtable_free_and_destroy(&nf->rhashtable, inet_frags_free_cb, NULL);
}
EXPORT_SYMBOL(inet_frags_exit_net);

void inet_frag_kill(struct inet_frag_queue *fq)
{
	if (del_timer(&fq->timer))
		refcount_dec(&fq->refcnt);

	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		struct netns_frags *nf = fq->net;

		fq->flags |= INET_FRAG_COMPLETE;
		rhashtable_remove_fast(&nf->rhashtable, &fq->node, nf->f->rhash_params);
		refcount_dec(&fq->refcnt);
	}
}
EXPORT_SYMBOL(inet_frag_kill);

static void inet_frag_destroy_rcu(struct rcu_head *head)
{
	struct inet_frag_queue *q = container_of(head, struct inet_frag_queue,
						 rcu);
	struct inet_frags *f = q->net->f;

	if (f->destructor)
		f->destructor(q);
	kmem_cache_free(f->frags_cachep, q);
}

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

void inet_frag_destroy(struct inet_frag_queue *q)
{
	struct netns_frags *nf;
	unsigned int sum, sum_truesize = 0;
	struct inet_frags *f;

	WARN_ON(!(q->flags & INET_FRAG_COMPLETE));
	WARN_ON(del_timer(&q->timer) != 0);

	/* Release all fragment data. */
	nf = q->net;
	f = nf->f;
	sum_truesize = inet_frag_rbtree_purge(&q->rb_fragments);
	sum = sum_truesize + f->qsize;

	call_rcu(&q->rcu, inet_frag_destroy_rcu);

	sub_frag_mem_limit(nf, sum);
}
EXPORT_SYMBOL(inet_frag_destroy);

static struct inet_frag_queue *inet_frag_alloc(struct netns_frags *nf,
					       struct inet_frags *f,
					       void *arg)
{
	struct inet_frag_queue *q;

	q = kmem_cache_zalloc(f->frags_cachep, GFP_ATOMIC);
	if (!q)
		return NULL;

	q->net = nf;
	f->constructor(q, arg);
	add_frag_mem_limit(nf, f->qsize);

	timer_setup(&q->timer, f->frag_expire, 0);
	spin_lock_init(&q->lock);
	refcount_set(&q->refcnt, 3);

	return q;
}

static struct inet_frag_queue *inet_frag_create(struct netns_frags *nf,
						void *arg,
						struct inet_frag_queue **prev)
{
	struct inet_frags *f = nf->f;
	struct inet_frag_queue *q;

	q = inet_frag_alloc(nf, f, arg);
	if (!q) {
		*prev = ERR_PTR(-ENOMEM);
		return NULL;
	}
	mod_timer(&q->timer, jiffies + nf->timeout);

	*prev = rhashtable_lookup_get_insert_key(&nf->rhashtable, &q->key,
						 &q->node, f->rhash_params);
	if (*prev) {
		q->flags |= INET_FRAG_COMPLETE;
		inet_frag_kill(q);
		inet_frag_destroy(q);
		return NULL;
	}
	return q;
}

/* TODO : call from rcu_read_lock() and no longer use refcount_inc_not_zero() */
struct inet_frag_queue *inet_frag_find(struct netns_frags *nf, void *key)
{
	struct inet_frag_queue *fq = NULL, *prev;

	if (!nf->high_thresh || frag_mem_limit(nf) > nf->high_thresh)
		return NULL;

	rcu_read_lock();

	prev = rhashtable_lookup(&nf->rhashtable, key, nf->f->rhash_params);
	if (!prev)
		fq = inet_frag_create(nf, key, &prev);
	if (prev && !IS_ERR(prev)) {
		fq = prev;
		if (!refcount_inc_not_zero(&fq->refcnt))
			fq = NULL;
	}
	rcu_read_unlock();
	return fq;
}
EXPORT_SYMBOL(inet_frag_find);

int inet_frag_queue_insert(struct inet_frag_queue *q, struct sk_buff *skb,
			   int offset, int end)
{
	struct sk_buff *last = q->fragments_tail;

	/* RFC5722, Section 4, amended by Errata ID : 3089
	 *                          When reassembling an IPv6 datagram, if
	 *   one or more its constituent fragments is determined to be an
	 *   overlapping fragment, the entire datagram (and any constituent
	 *   fragments) MUST be silently discarded.
	 *
	 * Duplicates, however, should be ignored (i.e. skb dropped, but the
	 * queue/fragments kept for later reassembly).
	 */
	if (!last)
		fragrun_create(q, skb);  /* First fragment. */
	else if (last->ip_defrag_offset + last->len < end) {
		/* This is the common case: skb goes to the end. */
		/* Detect and discard overlaps. */
		if (offset < last->ip_defrag_offset + last->len)
			return IPFRAG_OVERLAP;
		if (offset == last->ip_defrag_offset + last->len)
			fragrun_append_to_last(q, skb);
		else
			fragrun_create(q, skb);
	} else {
		/* Binary search. Note that skb can become the first fragment,
		 * but not the last (covered above).
		 */
		struct rb_node **rbn, *parent;

		rbn = &q->rb_fragments.rb_node;
		do {
			struct sk_buff *curr;
			int curr_run_end;

			parent = *rbn;
			curr = rb_to_skb(parent);
			curr_run_end = curr->ip_defrag_offset +
					FRAG_CB(curr)->frag_run_len;
			if (end <= curr->ip_defrag_offset)
				rbn = &parent->rb_left;
			else if (offset >= curr_run_end)
				rbn = &parent->rb_right;
			else if (offset >= curr->ip_defrag_offset &&
				 end <= curr_run_end)
				return IPFRAG_DUP;
			else
				return IPFRAG_OVERLAP;
		} while (*rbn);
		/* Here we have parent properly set, and rbn pointing to
		 * one of its NULL left/right children. Insert skb.
		 */
		fragcb_clear(skb);
		rb_link_node(&skb->rbnode, parent, rbn);
		rb_insert_color(&skb->rbnode, &q->rb_fragments);
	}

	skb->ip_defrag_offset = offset;

	return IPFRAG_OK;
}
EXPORT_SYMBOL(inet_frag_queue_insert);

void *inet_frag_reasm_prepare(struct inet_frag_queue *q, struct sk_buff *skb,
			      struct sk_buff *parent)
{
	struct sk_buff *fp, *head = skb_rb_first(&q->rb_fragments);
	struct sk_buff **nextp;
	int delta;

	if (head != skb) {
		fp = skb_clone(skb, GFP_ATOMIC);
		if (!fp)
			return NULL;
		FRAG_CB(fp)->next_frag = FRAG_CB(skb)->next_frag;
		if (RB_EMPTY_NODE(&skb->rbnode))
			FRAG_CB(parent)->next_frag = fp;
		else
			rb_replace_node(&skb->rbnode, &fp->rbnode,
					&q->rb_fragments);
		if (q->fragments_tail == skb)
			q->fragments_tail = fp;
		skb_morph(skb, head);
		FRAG_CB(skb)->next_frag = FRAG_CB(head)->next_frag;
		rb_replace_node(&head->rbnode, &skb->rbnode,
				&q->rb_fragments);
		consume_skb(head);
		head = skb;
	}
	WARN_ON(head->ip_defrag_offset != 0);

	delta = -head->truesize;

	/* Head of list must not be cloned. */
	if (skb_unclone(head, GFP_ATOMIC))
		return NULL;

	delta += head->truesize;
	if (delta)
		add_frag_mem_limit(q->net, delta);

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments.
	 */
	if (skb_has_frag_list(head)) {
		struct sk_buff *clone;
		int i, plen = 0;

		clone = alloc_skb(0, GFP_ATOMIC);
		if (!clone)
			return NULL;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_frag_list_init(head);
		for (i = 0; i < skb_shinfo(head)->nr_frags; i++)
			plen += skb_frag_size(&skb_shinfo(head)->frags[i]);
		clone->data_len = head->data_len - plen;
		clone->len = clone->data_len;
		head->truesize += clone->truesize;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		add_frag_mem_limit(q->net, clone->truesize);
		skb_shinfo(head)->frag_list = clone;
		nextp = &clone->next;
	} else {
		nextp = &skb_shinfo(head)->frag_list;
	}

	return nextp;
}
EXPORT_SYMBOL(inet_frag_reasm_prepare);

void inet_frag_reasm_finish(struct inet_frag_queue *q, struct sk_buff *head,
			    void *reasm_data)
{
	struct sk_buff **nextp = (struct sk_buff **)reasm_data;
	struct rb_node *rbn;
	struct sk_buff *fp;

	skb_push(head, head->data - skb_network_header(head));

	/* Traverse the tree in order, to build frag_list. */
	fp = FRAG_CB(head)->next_frag;
	rbn = rb_next(&head->rbnode);
	rb_erase(&head->rbnode, &q->rb_fragments);
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
			rb_erase(rbn, &q->rb_fragments);
			rbn = rbnext;
		}
	}
	sub_frag_mem_limit(q->net, head->truesize);

	*nextp = NULL;
	skb_mark_not_on_list(head);
	head->prev = NULL;
	head->tstamp = q->stamp;
}
EXPORT_SYMBOL(inet_frag_reasm_finish);

struct sk_buff *inet_frag_pull_head(struct inet_frag_queue *q)
{
	struct sk_buff *head, *skb;

	head = skb_rb_first(&q->rb_fragments);
	if (!head)
		return NULL;
	skb = FRAG_CB(head)->next_frag;
	if (skb)
		rb_replace_node(&head->rbnode, &skb->rbnode,
				&q->rb_fragments);
	else
		rb_erase(&head->rbnode, &q->rb_fragments);
	memset(&head->rbnode, 0, sizeof(head->rbnode));
	barrier();

	if (head == q->fragments_tail)
		q->fragments_tail = NULL;

	sub_frag_mem_limit(q->net, head->truesize);

	return head;
}
EXPORT_SYMBOL(inet_frag_pull_head);
