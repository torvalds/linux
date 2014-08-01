/*
 * inet fragments management
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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

#include <net/sock.h>
#include <net/inet_frag.h>
#include <net/inet_ecn.h>

#define INETFRAGS_EVICT_BUCKETS   128
#define INETFRAGS_EVICT_MAX	  512

/* don't rebuild inetfrag table with new secret more often than this */
#define INETFRAGS_MIN_REBUILD_INTERVAL (5 * HZ)

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

static unsigned int
inet_frag_hashfn(const struct inet_frags *f, const struct inet_frag_queue *q)
{
	return f->hashfn(q) & (INETFRAGS_HASHSZ - 1);
}

static bool inet_frag_may_rebuild(struct inet_frags *f)
{
	return time_after(jiffies,
	       f->last_rebuild_jiffies + INETFRAGS_MIN_REBUILD_INTERVAL);
}

static void inet_frag_secret_rebuild(struct inet_frags *f)
{
	int i;

	write_seqlock_bh(&f->rnd_seqlock);

	if (!inet_frag_may_rebuild(f))
		goto out;

	get_random_bytes(&f->rnd, sizeof(u32));

	for (i = 0; i < INETFRAGS_HASHSZ; i++) {
		struct inet_frag_bucket *hb;
		struct inet_frag_queue *q;
		struct hlist_node *n;

		hb = &f->hash[i];
		spin_lock(&hb->chain_lock);

		hlist_for_each_entry_safe(q, n, &hb->chain, list) {
			unsigned int hval = inet_frag_hashfn(f, q);

			if (hval != i) {
				struct inet_frag_bucket *hb_dest;

				hlist_del(&q->list);

				/* Relink to new hash chain. */
				hb_dest = &f->hash[hval];

				/* This is the only place where we take
				 * another chain_lock while already holding
				 * one.  As this will not run concurrently,
				 * we cannot deadlock on hb_dest lock below, if its
				 * already locked it will be released soon since
				 * other caller cannot be waiting for hb lock
				 * that we've taken above.
				 */
				spin_lock_nested(&hb_dest->chain_lock,
						 SINGLE_DEPTH_NESTING);
				hlist_add_head(&q->list, &hb_dest->chain);
				spin_unlock(&hb_dest->chain_lock);
			}
		}
		spin_unlock(&hb->chain_lock);
	}

	f->rebuild = false;
	f->last_rebuild_jiffies = jiffies;
out:
	write_sequnlock_bh(&f->rnd_seqlock);
}

static bool inet_fragq_should_evict(const struct inet_frag_queue *q)
{
	return q->net->low_thresh == 0 ||
	       frag_mem_limit(q->net) >= q->net->low_thresh;
}

static unsigned int
inet_evict_bucket(struct inet_frags *f, struct inet_frag_bucket *hb)
{
	struct inet_frag_queue *fq;
	struct hlist_node *n;
	unsigned int evicted = 0;
	HLIST_HEAD(expired);

evict_again:
	spin_lock(&hb->chain_lock);

	hlist_for_each_entry_safe(fq, n, &hb->chain, list) {
		if (!inet_fragq_should_evict(fq))
			continue;

		if (!del_timer(&fq->timer)) {
			/* q expiring right now thus increment its refcount so
			 * it won't be freed under us and wait until the timer
			 * has finished executing then destroy it
			 */
			atomic_inc(&fq->refcnt);
			spin_unlock(&hb->chain_lock);
			del_timer_sync(&fq->timer);
			WARN_ON(atomic_read(&fq->refcnt) != 1);
			inet_frag_put(fq, f);
			goto evict_again;
		}

		/* suppress xmit of (icmp) error packet */
		fq->flags &= ~INET_FRAG_FIRST_IN;
		fq->flags |= INET_FRAG_EVICTED;
		hlist_del(&fq->list);
		hlist_add_head(&fq->list, &expired);
		++evicted;
	}

	spin_unlock(&hb->chain_lock);

	hlist_for_each_entry_safe(fq, n, &expired, list)
		f->frag_expire((unsigned long) fq);

	return evicted;
}

static void inet_frag_worker(struct work_struct *work)
{
	unsigned int budget = INETFRAGS_EVICT_BUCKETS;
	unsigned int i, evicted = 0;
	struct inet_frags *f;

	f = container_of(work, struct inet_frags, frags_work);

	BUILD_BUG_ON(INETFRAGS_EVICT_BUCKETS >= INETFRAGS_HASHSZ);

	local_bh_disable();

	for (i = ACCESS_ONCE(f->next_bucket); budget; --budget) {
		evicted += inet_evict_bucket(f, &f->hash[i]);
		i = (i + 1) & (INETFRAGS_HASHSZ - 1);
		if (evicted > INETFRAGS_EVICT_MAX)
			break;
	}

	f->next_bucket = i;

	local_bh_enable();

	if (f->rebuild && inet_frag_may_rebuild(f))
		inet_frag_secret_rebuild(f);
}

static void inet_frag_schedule_worker(struct inet_frags *f)
{
	if (unlikely(!work_pending(&f->frags_work)))
		schedule_work(&f->frags_work);
}

void inet_frags_init(struct inet_frags *f)
{
	int i;

	INIT_WORK(&f->frags_work, inet_frag_worker);

	for (i = 0; i < INETFRAGS_HASHSZ; i++) {
		struct inet_frag_bucket *hb = &f->hash[i];

		spin_lock_init(&hb->chain_lock);
		INIT_HLIST_HEAD(&hb->chain);
	}

	seqlock_init(&f->rnd_seqlock);
	f->last_rebuild_jiffies = 0;
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_init_net(struct netns_frags *nf)
{
	init_frag_mem_limit(nf);
}
EXPORT_SYMBOL(inet_frags_init_net);

void inet_frags_fini(struct inet_frags *f)
{
	cancel_work_sync(&f->frags_work);
}
EXPORT_SYMBOL(inet_frags_fini);

void inet_frags_exit_net(struct netns_frags *nf, struct inet_frags *f)
{
	unsigned int seq;
	int i;

	nf->low_thresh = 0;
	local_bh_disable();

evict_again:
	seq = read_seqbegin(&f->rnd_seqlock);

	for (i = 0; i < INETFRAGS_HASHSZ ; i++)
		inet_evict_bucket(f, &f->hash[i]);

	if (read_seqretry(&f->rnd_seqlock, seq))
		goto evict_again;

	local_bh_enable();

	percpu_counter_destroy(&nf->mem);
}
EXPORT_SYMBOL(inet_frags_exit_net);

static struct inet_frag_bucket *
get_frag_bucket_locked(struct inet_frag_queue *fq, struct inet_frags *f)
__acquires(hb->chain_lock)
{
	struct inet_frag_bucket *hb;
	unsigned int seq, hash;

 restart:
	seq = read_seqbegin(&f->rnd_seqlock);

	hash = inet_frag_hashfn(f, fq);
	hb = &f->hash[hash];

	spin_lock(&hb->chain_lock);
	if (read_seqretry(&f->rnd_seqlock, seq)) {
		spin_unlock(&hb->chain_lock);
		goto restart;
	}

	return hb;
}

static inline void fq_unlink(struct inet_frag_queue *fq, struct inet_frags *f)
{
	struct inet_frag_bucket *hb;

	hb = get_frag_bucket_locked(fq, f);
	hlist_del(&fq->list);
	spin_unlock(&hb->chain_lock);
}

void inet_frag_kill(struct inet_frag_queue *fq, struct inet_frags *f)
{
	if (del_timer(&fq->timer))
		atomic_dec(&fq->refcnt);

	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		fq_unlink(fq, f);
		atomic_dec(&fq->refcnt);
		fq->flags |= INET_FRAG_COMPLETE;
	}
}
EXPORT_SYMBOL(inet_frag_kill);

static inline void frag_kfree_skb(struct netns_frags *nf, struct inet_frags *f,
				  struct sk_buff *skb)
{
	if (f->skb_free)
		f->skb_free(skb);
	kfree_skb(skb);
}

void inet_frag_destroy(struct inet_frag_queue *q, struct inet_frags *f)
{
	struct sk_buff *fp;
	struct netns_frags *nf;
	unsigned int sum, sum_truesize = 0;

	WARN_ON(!(q->flags & INET_FRAG_COMPLETE));
	WARN_ON(del_timer(&q->timer) != 0);

	/* Release all fragment data. */
	fp = q->fragments;
	nf = q->net;
	while (fp) {
		struct sk_buff *xp = fp->next;

		sum_truesize += fp->truesize;
		frag_kfree_skb(nf, f, fp);
		fp = xp;
	}
	sum = sum_truesize + f->qsize;
	sub_frag_mem_limit(q, sum);

	if (f->destructor)
		f->destructor(q);
	kfree(q);
}
EXPORT_SYMBOL(inet_frag_destroy);

static struct inet_frag_queue *inet_frag_intern(struct netns_frags *nf,
		struct inet_frag_queue *qp_in, struct inet_frags *f,
		void *arg)
{
	struct inet_frag_bucket *hb = get_frag_bucket_locked(qp_in, f);
	struct inet_frag_queue *qp;

#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could have been created on other cpu before
	 * we acquired hash bucket lock.
	 */
	hlist_for_each_entry(qp, &hb->chain, list) {
		if (qp->net == nf && f->match(qp, arg)) {
			atomic_inc(&qp->refcnt);
			spin_unlock(&hb->chain_lock);
			qp_in->flags |= INET_FRAG_COMPLETE;
			inet_frag_put(qp_in, f);
			return qp;
		}
	}
#endif
	qp = qp_in;
	if (!mod_timer(&qp->timer, jiffies + nf->timeout))
		atomic_inc(&qp->refcnt);

	atomic_inc(&qp->refcnt);
	hlist_add_head(&qp->list, &hb->chain);

	spin_unlock(&hb->chain_lock);

	return qp;
}

static struct inet_frag_queue *inet_frag_alloc(struct netns_frags *nf,
		struct inet_frags *f, void *arg)
{
	struct inet_frag_queue *q;

	if (frag_mem_limit(nf) > nf->high_thresh) {
		inet_frag_schedule_worker(f);
		return NULL;
	}

	q = kzalloc(f->qsize, GFP_ATOMIC);
	if (q == NULL)
		return NULL;

	q->net = nf;
	f->constructor(q, arg);
	add_frag_mem_limit(q, f->qsize);

	setup_timer(&q->timer, f->frag_expire, (unsigned long)q);
	spin_lock_init(&q->lock);
	atomic_set(&q->refcnt, 1);

	return q;
}

static struct inet_frag_queue *inet_frag_create(struct netns_frags *nf,
		struct inet_frags *f, void *arg)
{
	struct inet_frag_queue *q;

	q = inet_frag_alloc(nf, f, arg);
	if (q == NULL)
		return NULL;

	return inet_frag_intern(nf, q, f, arg);
}

struct inet_frag_queue *inet_frag_find(struct netns_frags *nf,
		struct inet_frags *f, void *key, unsigned int hash)
{
	struct inet_frag_bucket *hb;
	struct inet_frag_queue *q;
	int depth = 0;

	if (frag_mem_limit(nf) > nf->low_thresh)
		inet_frag_schedule_worker(f);

	hash &= (INETFRAGS_HASHSZ - 1);
	hb = &f->hash[hash];

	spin_lock(&hb->chain_lock);
	hlist_for_each_entry(q, &hb->chain, list) {
		if (q->net == nf && f->match(q, key)) {
			atomic_inc(&q->refcnt);
			spin_unlock(&hb->chain_lock);
			return q;
		}
		depth++;
	}
	spin_unlock(&hb->chain_lock);

	if (depth <= INETFRAGS_MAXDEPTH)
		return inet_frag_create(nf, f, key);

	if (inet_frag_may_rebuild(f)) {
		if (!f->rebuild)
			f->rebuild = true;
		inet_frag_schedule_worker(f);
	}

	return ERR_PTR(-ENOBUFS);
}
EXPORT_SYMBOL(inet_frag_find);

void inet_frag_maybe_warn_overflow(struct inet_frag_queue *q,
				   const char *prefix)
{
	static const char msg[] = "inet_frag_find: Fragment hash bucket"
		" list length grew over limit " __stringify(INETFRAGS_MAXDEPTH)
		". Dropping fragment.\n";

	if (PTR_ERR(q) == -ENOBUFS)
		LIMIT_NETDEBUG(KERN_WARNING "%s%s", prefix, msg);
}
EXPORT_SYMBOL(inet_frag_maybe_warn_overflow);
