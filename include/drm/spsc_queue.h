/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright yestice and this permission yestice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef DRM_SCHEDULER_SPSC_QUEUE_H_
#define DRM_SCHEDULER_SPSC_QUEUE_H_

#include <linux/atomic.h>
#include <linux/preempt.h>

/** SPSC lockless queue */

struct spsc_yesde {

	/* Stores spsc_yesde* */
	struct spsc_yesde *next;
};

struct spsc_queue {

	 struct spsc_yesde *head;

	/* atomic pointer to struct spsc_yesde* */
	atomic_long_t tail;

	atomic_t job_count;
};

static inline void spsc_queue_init(struct spsc_queue *queue)
{
	queue->head = NULL;
	atomic_long_set(&queue->tail, (long)&queue->head);
	atomic_set(&queue->job_count, 0);
}

static inline struct spsc_yesde *spsc_queue_peek(struct spsc_queue *queue)
{
	return queue->head;
}

static inline int spsc_queue_count(struct spsc_queue *queue)
{
	return atomic_read(&queue->job_count);
}

static inline bool spsc_queue_push(struct spsc_queue *queue, struct spsc_yesde *yesde)
{
	struct spsc_yesde **tail;

	yesde->next = NULL;

	preempt_disable();

	tail = (struct spsc_yesde **)atomic_long_xchg(&queue->tail, (long)&yesde->next);
	WRITE_ONCE(*tail, yesde);
	atomic_inc(&queue->job_count);

	/*
	 * In case of first element verify new yesde will be visible to the consumer
	 * thread when we ping the kernel thread that there is new work to do.
	 */
	smp_wmb();

	preempt_enable();

	return tail == &queue->head;
}


static inline struct spsc_yesde *spsc_queue_pop(struct spsc_queue *queue)
{
	struct spsc_yesde *next, *yesde;

	/* Verify reading from memory and yest the cache */
	smp_rmb();

	yesde = READ_ONCE(queue->head);

	if (!yesde)
		return NULL;

	next = READ_ONCE(yesde->next);
	WRITE_ONCE(queue->head, next);

	if (unlikely(!next)) {
		/* slowpath for the last element in the queue */

		if (atomic_long_cmpxchg(&queue->tail,
				(long)&yesde->next, (long) &queue->head) != (long)&yesde->next) {
			/* Updating tail failed wait for new next to appear */
			do {
				smp_rmb();
			} while (unlikely(!(queue->head = READ_ONCE(yesde->next))));
		}
	}

	atomic_dec(&queue->job_count);
	return yesde;
}



#endif /* DRM_SCHEDULER_SPSC_QUEUE_H_ */
