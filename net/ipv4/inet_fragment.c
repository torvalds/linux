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

	refcount_set(&f->refcnt, 1);
	init_completion(&f->completion);
	return 0;
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_fini(struct inet_frags *f)
{
	if (refcount_dec_and_test(&f->refcnt))
		complete(&f->completion);

	wait_for_completion(&f->completion);

	kmem_cache_destroy(f->frags_cachep);
	f->frags_cachep = NULL;
}
EXPORT_SYMBOL(inet_frags_fini);

/* called from rhashtable_free_and_destroy() at netns_frags dismantle */
static void inet_frags_free_cb(void *ptr, void *arg)
{
	struct inet_frag_queue *fq = ptr;
	int count;

	count = del_timer_sync(&fq->timer) ? 1 : 0;

	spin_lock_bh(&fq->lock);
	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		fq->flags |= INET_FRAG_COMPLETE;
		count++;
	} else if (fq->flags & INET_FRAG_HASH_DEAD) {
		count++;
	}
	spin_unlock_bh(&fq->lock);

	if (refcount_sub_and_test(count, &fq->refcnt))
		inet_frag_destroy(fq);
}

static void fqdir_work_fn(struct work_struct *work)
{
	struct fqdir *fqdir = container_of(work, struct fqdir, destroy_work);
	struct inet_frags *f = fqdir->f;

	rhashtable_free_and_destroy(&fqdir->rhashtable, inet_frags_free_cb, NULL);

	/* We need to make sure all ongoing call_rcu(..., inet_frag_destroy_rcu)
	 * have completed, since they need to dereference fqdir.
	 * Would it not be nice to have kfree_rcu_barrier() ? :)
	 */
	rcu_barrier();

	if (refcount_dec_and_test(&f->refcnt))
		complete(&f->completion);

	kfree(fqdir);
}

int fqdir_init(struct fqdir **fqdirp, struct inet_frags *f, struct net *net)
{
	struct fqdir *fqdir = kzalloc(sizeof(*fqdir), GFP_KERNEL);
	int res;

	if (!fqdir)
		return -ENOMEM;
	fqdir->f = f;
	fqdir->net = net;
	res = rhashtable_init(&fqdir->rhashtable, &fqdir->f->rhash_params);
	if (res < 0) {
		kfree(fqdir);
		return res;
	}
	refcount_inc(&f->refcnt);
	*fqdirp = fqdir;
	return 0;
}
EXPORT_SYMBOL(fqdir_init);

void fqdir_exit(struct fqdir *fqdir)
{
	INIT_WORK(&fqdir->destroy_work, fqdir_work_fn);
	queue_work(system_wq, &fqdir->destroy_work);
}
EXPORT_SYMBOL(fqdir_exit);

void inet_frag_kill(struct inet_frag_queue *fq)
{
	if (del_timer(&fq->timer))
		refcount_dec(&fq->refcnt);

	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		struct fqdir *fqdir = fq->fqdir;

		fq->flags |= INET_FRAG_COMPLETE;
		rcu_read_lock();
		/* The RCU read lock provides a memory barrier
		 * guaranteeing that if fqdir->dead is false then
		 * the hash table destruction will not start until
		 * after we unlock.  Paired with fqdir_pre_exit().
		 */
		if (!READ_ONCE(fqdir->dead)) {
			rhashtable_remove_fast(&fqdir->rhashtable, &fq->node,
					       fqdir->f->rhash_params);
			refcount_dec(&fq->refcnt);
		} else {
			fq->flags |= INET_FRAG_HASH_DEAD;
		}
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(inet_frag_kill);

static void inet_frag_destroy_rcu(struct rcu_head *head)
{
	struct inet_frag_queue *q = container_of(head, struct inet_frag_queue,
						 rcu);
	struct inet_frags *f = q->fqdir->f;

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
	struct fqdir *fqdir;
	unsigned int sum, sum_truesize = 0;
	struct inet_frags *f;

	WARN_ON(!(q->flags & INET_FRAG_COMPLETE));
	WARN_ON(del_timer(&q->timer) != 0);

	/* Release all fragment data. */
	fqdir = q->fqdir;
	f = fqdir->f;
	sum_truesize = inet_frag_rbtree_purge(&q->rb_fragments);
	sum = sum_truesize + f->qsize;

	call_rcu(&q->rcu, inet_frag_destroy_rcu);

	sub_frag_mem_limit(fqdir, sum);
}
EXPORT_SYMBOL(inet_frag_destroy);

static struct inet_frag_queue *inet_frag_alloc(struct fqdir *fqdir,
					       struct inet_frags *f,
					       void *arg)
{
	struct inet_frag_queue *q;

	q = kmem_cache_zalloc(f->frags_cachep, GFP_ATOMIC);
	if (!q)
		return NULL;

	q->fqdir = fqdir;
	f->constructor(q, arg);
	add_frag_mem_limit(fqdir, f->qsize);

	timer_setup(&q->timer, f->frag_expire, 0);
	spin_lock_init(&q->lock);
	refcount_set(&q->refcnt, 3);

	return q;
}

static struct inet_frag_queue *inet_frag_create(struct fqdir *fqdir,
						void *arg,
						struct inet_frag_queue **prev)
{
	struct inet_frags *f = fqdir->f;
	struct inet_frag_queue *q;

	q = inet_frag_alloc(fqdir, f, arg);
	if (!q) {
		*prev = ERR_PTR(-ENOMEM);
		return NULL;
	}
	mod_timer(&q->timer, jiffies + fqdir->timeout);

	*prev = rhashtable_lookup_get_insert_key(&fqdir->rhashtable, &q->key,
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
struct inet_frag_queue *inet_frag_find(struct fqdir *fqdir, void *key)
{
	/* This pairs with WRITE_ONCE() in fqdir_pre_exit(). */
	long high_thresh = READ_ONCE(fqdir->high_thresh);
	struct inet_frag_queue *fq = NULL, *prev;

	if (!high_thresh || frag_mem_limit(fqdir) > high_thresh)
		return NULL;

	rcu_read_lock();

	prev = rhashtable_lookup(&fqdir->rhashtable, key, fqdir->f->rhash_params);
	if (!prev)
		fq = inet_frag_create(fqdir, key, &prev);
	if (!IS_ERR_OR_NULL(prev)) {
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
		add_frag_mem_limit(q->fqdir, delta);

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
		add_frag_mem_limit(q->fqdir, clone->truesize);
		skb_shinfo(head)->frag_list = clone;
		nextp = &clone->next;
	} else {
		nextp = &skb_shinfo(head)->frag_list;
	}

	return nextp;
}
EXPORT_SYMBOL(inet_frag_reasm_prepare);

void inet_frag_reasm_finish(struct inet_frag_queue *q, struct sk_buff *head,
			    void *reasm_data, bool try_coalesce)
{
	struct sk_buff **nextp = (struct sk_buff **)reasm_data;
	struct rb_node *rbn;
	struct sk_buff *fp;
	int sum_truesize;

	skb_push(head, head->data - skb_network_header(head));

	/* Traverse the tree in order, to build frag_list. */
	fp = FRAG_CB(head)->next_frag;
	rbn = rb_next(&head->rbnode);
	rb_erase(&head->rbnode, &q->rb_fragments);

	sum_truesize = head->truesize;
	while (rbn || fp) {
		/* fp points to the next sk_buff in the current run;
		 * rbn points to the next run.
		 */
		/* Go through the current run. */
		while (fp) {
			struct sk_buff *next_frag = FRAG_CB(fp)->next_frag;
			bool stolen;
			int delta;

			sum_truesize += fp->truesize;
			if (head->ip_summed != fp->ip_summed)
				head->ip_summed = CHECKSUM_NONE;
			else if (head->ip_summed == CHECKSUM_COMPLETE)
				head->csum = csum_add(head->csum, fp->csum);

			if (try_coalesce && skb_try_coalesce(head, fp, &stolen,
							     &delta)) {
				kfree_skb_partial(fp, stolen);
			} else {
				fp->prev = NULL;
				memset(&fp->rbnode, 0, sizeof(fp->rbnode));
				fp->sk = NULL;

				head->data_len += fp->len;
				head->len += fp->len;
				head->truesize += fp->truesize;

				*nextp = fp;
				nextp = &fp->next;
			}

			fp = next_frag;
		}
		/* Move to the next run. */
		if (rbn) {
			struct rb_node *rbnext = rb_next(rbn);

			fp = rb_to_skb(rbn);
			rb_erase(rbn, &q->rb_fragments);
			rbn = rbnext;
		}
	}
	sub_frag_mem_limit(q->fqdir, sum_truesize);

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

	sub_frag_mem_limit(q->fqdir, head->truesize);

	return head;
}
EXPORT_SYMBOL(inet_frag_pull_head);
