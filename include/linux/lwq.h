/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LWQ_H
#define LWQ_H
/*
 * Light-weight single-linked queue built from llist
 *
 * Entries can be enqueued from any context with no locking.
 * Entries can be dequeued from process context with integrated locking.
 *
 * This is particularly suitable when work items are queued in
 * BH or IRQ context, and where work items are handled one at a time
 * by dedicated threads.
 */
#include <linux/container_of.h>
#include <linux/spinlock.h>
#include <linux/llist.h>

struct lwq_node {
	struct llist_node node;
};

struct lwq {
	spinlock_t		lock;
	struct llist_node	*ready;		/* entries to be dequeued */
	struct llist_head	new;		/* entries being enqueued */
};

/**
 * lwq_init - initialise a lwq
 * @q:	the lwq object
 */
static inline void lwq_init(struct lwq *q)
{
	spin_lock_init(&q->lock);
	q->ready = NULL;
	init_llist_head(&q->new);
}

/**
 * lwq_empty - test if lwq contains any entry
 * @q:	the lwq object
 *
 * This empty test contains an acquire barrier so that if a wakeup
 * is sent when lwq_dequeue returns true, it is safe to go to sleep after
 * a test on lwq_empty().
 */
static inline bool lwq_empty(struct lwq *q)
{
	/* acquire ensures ordering wrt lwq_enqueue() */
	return smp_load_acquire(&q->ready) == NULL && llist_empty(&q->new);
}

struct llist_node *__lwq_dequeue(struct lwq *q);
/**
 * lwq_dequeue - dequeue first (oldest) entry from lwq
 * @q:		the queue to dequeue from
 * @type:	the type of object to return
 * @member:	them member in returned object which is an lwq_node.
 *
 * Remove a single object from the lwq and return it.  This will take
 * a spinlock and so must always be called in the same context, typcially
 * process contet.
 */
#define lwq_dequeue(q, type, member)					\
	({ struct llist_node *_n = __lwq_dequeue(q);			\
	  _n ? container_of(_n, type, member.node) : NULL; })

struct llist_node *lwq_dequeue_all(struct lwq *q);

/**
 * lwq_for_each_safe - iterate over detached queue allowing deletion
 * @_n:		iterator variable
 * @_t1:	temporary struct llist_node **
 * @_t2:	temporary struct llist_node *
 * @_l:		address of llist_node pointer from lwq_dequeue_all()
 * @_member:	member in _n where lwq_node is found.
 *
 * Iterate over members in a dequeued list.  If the iterator variable
 * is set to NULL, the iterator removes that entry from the queue.
 */
#define lwq_for_each_safe(_n, _t1, _t2, _l, _member)			\
	for (_t1 = (_l);						\
	     *(_t1) ? (_n = container_of(*(_t1), typeof(*(_n)), _member.node),\
		       _t2 = ((*_t1)->next),				\
		       true)						\
	     : false;							\
	     (_n) ? (_t1 = &(_n)->_member.node.next, 0)			\
	     : ((*(_t1) = (_t2)),  0))

/**
 * lwq_enqueue - add a new item to the end of the queue
 * @n	- the lwq_node embedded in the item to be added
 * @q	- the lwq to append to.
 *
 * No locking is needed to append to the queue so this can
 * be called from any context.
 * Return %true is the list may have previously been empty.
 */
static inline bool lwq_enqueue(struct lwq_node *n, struct lwq *q)
{
	/* acquire enqures ordering wrt lwq_dequeue */
	return llist_add(&n->node, &q->new) &&
		smp_load_acquire(&q->ready) == NULL;
}

/**
 * lwq_enqueue_batch - add a list of new items to the end of the queue
 * @n	- the lwq_node embedded in the first item to be added
 * @q	- the lwq to append to.
 *
 * No locking is needed to append to the queue so this can
 * be called from any context.
 * Return %true is the list may have previously been empty.
 */
static inline bool lwq_enqueue_batch(struct llist_node *n, struct lwq *q)
{
	struct llist_node *e = n;

	/* acquire enqures ordering wrt lwq_dequeue */
	return llist_add_batch(llist_reverse_order(n), e, &q->new) &&
		smp_load_acquire(&q->ready) == NULL;
}
#endif /* LWQ_H */
