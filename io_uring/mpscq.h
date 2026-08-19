/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IOU_MPSCQ_H
#define IOU_MPSCQ_H

#include <linux/io_uring_types.h>

/*
 * mpscq - lockless multi-producer, single-consumer FIFO queue
 *
 * Unlike llist, which is LIFO ordered and hence needs an O(n)
 * llist_reverse_order() pass before entries can be processed in queue order,
 * this queue hands out nodes in the order they were pushed.
 *
 * The consumer cursor is held by the caller rather than in the queue struct
 * (see below), and with the stub reinsertion done as a single cmpxchg attempt
 * instead of an unconditional push, keeping tail == stub a reliable empty test
 * while a producer is in the middle of a push.
 *
 * Producers may run in any context (task, softirq, hardirq) and are wait-free:
 * a push is one xchg() plus one store, with no retry loops. FIFO order between
 * producers is the order in which the xchg() on ->tail serializes them.
 *
 * The price for linked-list FIFO is that a push publishes the node in two
 * steps: the xchg() makes it the new tail, and the subsequent store links it to
 * its predecessor. In between, the tail end of the queue is not yet reachable
 * from the head. mpscq_pop() detects this and returns NULL, while mpscq_empty()
 * reports false. The consumer must not treat such a NULL as "queue empty" - it
 * should retry later. The window is two instructions wide, but a producer can
 * be preempted inside it, so the consumer must not spin on it while holding
 * resources the producer might need to make progress.
 *
 * The consumer side only supports a single consumer at a time, callers must
 * provide their own serialization for it. The stub node is what allows the
 * consumer to detach the final node without racing with the link stores of
 * producers. This scheme also guarantees that the previous tail observed by
 * mpscq_push() cannot be freed by the consumer until the push has linked it,
 * which is what makes the deferred link store safe.
 *
 * The queue struct only holds the producer side. The consumer keeps its cursor
 * (the oldest not yet handed out node) externally and passes it to mpscq_pop(),
 * so that it can be placed on a different cacheline: the cursor is written for
 * every pop, and having it share a line with ->tail would have the consumer
 * invalidating the line that producers need for every push.
 */
static inline void mpscq_init(struct mpscq *q, struct llist_node **headp)
{
	q->tail = *headp = &q->stub;
	q->stub.next = NULL;
}

/*
 * Returns true if the queue holds no entries that mpscq_pop() hasn't handed out
 * yet. May be called from any context. Note that !empty doesn't guarantee that
 * mpscq_pop() will return an entry yet, see the in-flight producer window
 * above.
 */
static inline bool mpscq_empty(struct mpscq *q)
{
	return READ_ONCE(q->tail) == &q->stub;
}

/*
 * Push a node onto the queue. Safe against concurrent pushes from any context,
 * and against the (single) consumer. Returns true if the queue was empty
 * before this push.
 */
static inline bool mpscq_push(struct mpscq *q, struct llist_node *node)
{
	struct llist_node *prev;

	node->next = NULL;
	/*
	 * xchg() implies a full barrier, so the initialization of the
	 * entry (including ->next above) is visible before the node can
	 * be reached, either via ->tail or via ->next chasing from the
	 * head once the store below has linked it.
	 */
	prev = xchg(&q->tail, node);
	WRITE_ONCE(prev->next, node);
	return prev == &q->stub;
}

/*
 * Pop the oldest node off the queue, or return NULL if no node is available.
 * NULL is returned both when the queue is empty and when a producer has
 * published a node via ->tail but hasn't linked it yet; use mpscq_empty() to
 * tell the two apart. Single consumer only, with headp being the consumer
 * cursor that mpscq_init() set up.
 */
static inline struct llist_node *mpscq_pop(struct mpscq *q,
					   struct llist_node **headp)
{
	struct llist_node *head = *headp, *next;

	if (head == &q->stub) {
		head = READ_ONCE(head->next);
		if (!head)
			return NULL;
		/*
		 * The stub is now detached and stays quiescent until the
		 * consumer reinserts it as the tail, so reset its ->next here,
		 * ready for that.
		 */
		q->stub.next = NULL;
		*headp = head;
	}
	next = READ_ONCE(head->next);
	if (next) {
		*headp = next;
		return head;
	}
	/*
	 * 'head' is the last linked node, it can only be handed out once the
	 * stub has taken its place as the tail. If the cmpxchg fails, a
	 * producer has made a new node the tail but hasn't linked 'head' to
	 * it yet - bail and let the caller retry.
	 */
	if (try_cmpxchg(&q->tail, &head, &q->stub)) {
		*headp = &q->stub;
		return head;
	}
	return NULL;
}

/*
 * Returns true if the most recent mpscq_pop() that returned a node also
 * emptied the queue. Consumer must be serialized.
 */
static inline bool mpscq_pop_emptied(struct mpscq *q, struct llist_node *head)
{
	return head == &q->stub;
}

#endif /* IOU_MPSCQ_H */
