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

#include <net/inet_frag.h>

static void inet_frag_secret_rebuild(unsigned long dummy)
{
	struct inet_frags *f = (struct inet_frags *)dummy;
	unsigned long now = jiffies;
	int i;

	write_lock(&f->lock);
	get_random_bytes(&f->rnd, sizeof(u32));
	for (i = 0; i < INETFRAGS_HASHSZ; i++) {
		struct inet_frag_queue *q;
		struct hlist_node *p, *n;

		hlist_for_each_entry_safe(q, p, n, &f->hash[i], list) {
			unsigned int hval = f->hashfn(q);

			if (hval != i) {
				hlist_del(&q->list);

				/* Relink to new hash chain. */
				hlist_add_head(&q->list, &f->hash[hval]);
			}
		}
	}
	write_unlock(&f->lock);

	mod_timer(&f->secret_timer, now + f->ctl->secret_interval);
}

void inet_frags_init(struct inet_frags *f)
{
	int i;

	for (i = 0; i < INETFRAGS_HASHSZ; i++)
		INIT_HLIST_HEAD(&f->hash[i]);

	INIT_LIST_HEAD(&f->lru_list);
	rwlock_init(&f->lock);

	f->rnd = (u32) ((num_physpages ^ (num_physpages>>7)) ^
				   (jiffies ^ (jiffies >> 6)));

	f->nqueues = 0;
	atomic_set(&f->mem, 0);

	init_timer(&f->secret_timer);
	f->secret_timer.function = inet_frag_secret_rebuild;
	f->secret_timer.data = (unsigned long)f;
	f->secret_timer.expires = jiffies + f->ctl->secret_interval;
	add_timer(&f->secret_timer);
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_fini(struct inet_frags *f)
{
	del_timer(&f->secret_timer);
}
EXPORT_SYMBOL(inet_frags_fini);

static inline void fq_unlink(struct inet_frag_queue *fq, struct inet_frags *f)
{
	write_lock(&f->lock);
	hlist_del(&fq->list);
	list_del(&fq->lru_list);
	f->nqueues--;
	write_unlock(&f->lock);
}

void inet_frag_kill(struct inet_frag_queue *fq, struct inet_frags *f)
{
	if (del_timer(&fq->timer))
		atomic_dec(&fq->refcnt);

	if (!(fq->last_in & COMPLETE)) {
		fq_unlink(fq, f);
		atomic_dec(&fq->refcnt);
		fq->last_in |= COMPLETE;
	}
}

EXPORT_SYMBOL(inet_frag_kill);

static inline void frag_kfree_skb(struct inet_frags *f, struct sk_buff *skb,
						int *work)
{
	if (work)
		*work -= skb->truesize;

	atomic_sub(skb->truesize, &f->mem);
	if (f->skb_free)
		f->skb_free(skb);
	kfree_skb(skb);
}

void inet_frag_destroy(struct inet_frag_queue *q, struct inet_frags *f,
					int *work)
{
	struct sk_buff *fp;

	BUG_TRAP(q->last_in & COMPLETE);
	BUG_TRAP(del_timer(&q->timer) == 0);

	/* Release all fragment data. */
	fp = q->fragments;
	while (fp) {
		struct sk_buff *xp = fp->next;

		frag_kfree_skb(f, fp, work);
		fp = xp;
	}

	if (work)
		*work -= f->qsize;
	atomic_sub(f->qsize, &f->mem);

	f->destructor(q);

}
EXPORT_SYMBOL(inet_frag_destroy);

int inet_frag_evictor(struct inet_frags *f)
{
	struct inet_frag_queue *q;
	int work, evicted = 0;

	work = atomic_read(&f->mem) - f->ctl->low_thresh;
	while (work > 0) {
		read_lock(&f->lock);
		if (list_empty(&f->lru_list)) {
			read_unlock(&f->lock);
			break;
		}

		q = list_first_entry(&f->lru_list,
				struct inet_frag_queue, lru_list);
		atomic_inc(&q->refcnt);
		read_unlock(&f->lock);

		spin_lock(&q->lock);
		if (!(q->last_in & COMPLETE))
			inet_frag_kill(q, f);
		spin_unlock(&q->lock);

		if (atomic_dec_and_test(&q->refcnt))
			inet_frag_destroy(q, f, &work);
		evicted++;
	}

	return evicted;
}
EXPORT_SYMBOL(inet_frag_evictor);
