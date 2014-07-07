
#include <linux/percpu.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include "mcs_spinlock.h"

#ifdef CONFIG_SMP

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs node per CPU is safe because sleeping locks should not be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */
static DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_queue, osq_node);

/*
 * Get a stable @node->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 */
static inline struct optimistic_spin_queue *
osq_wait_next(struct optimistic_spin_queue **lock,
	      struct optimistic_spin_queue *node,
	      struct optimistic_spin_queue *prev)
{
	struct optimistic_spin_queue *next = NULL;

	for (;;) {
		if (*lock == node && cmpxchg(lock, node, prev) == node) {
			/*
			 * We were the last queued, we moved @lock back. @prev
			 * will now observe @lock and will complete its
			 * unlock()/unqueue().
			 */
			break;
		}

		/*
		 * We must xchg() the @node->next value, because if we were to
		 * leave it in, a concurrent unlock()/unqueue() from
		 * @node->next might complete Step-A and think its @prev is
		 * still valid.
		 *
		 * If the concurrent unlock()/unqueue() wins the race, we'll
		 * wait for either @lock to point to us, through its Step-B, or
		 * wait for a new @node->next from its Step-C.
		 */
		if (node->next) {
			next = xchg(&node->next, NULL);
			if (next)
				break;
		}

		arch_mutex_cpu_relax();
	}

	return next;
}

bool osq_lock(struct optimistic_spin_queue **lock)
{
	struct optimistic_spin_queue *node = this_cpu_ptr(&osq_node);
	struct optimistic_spin_queue *prev, *next;

	node->locked = 0;
	node->next = NULL;

	node->prev = prev = xchg(lock, node);
	if (likely(prev == NULL))
		return true;

	ACCESS_ONCE(prev->next) = node;

	/*
	 * Normally @prev is untouchable after the above store; because at that
	 * moment unlock can proceed and wipe the node element from stack.
	 *
	 * However, since our nodes are static per-cpu storage, we're
	 * guaranteed their existence -- this allows us to apply
	 * cmpxchg in an attempt to undo our queueing.
	 */

	while (!smp_load_acquire(&node->locked)) {
		/*
		 * If we need to reschedule bail... so we can block.
		 */
		if (need_resched())
			goto unqueue;

		arch_mutex_cpu_relax();
	}
	return true;

unqueue:
	/*
	 * Step - A  -- stabilize @prev
	 *
	 * Undo our @prev->next assignment; this will make @prev's
	 * unlock()/unqueue() wait for a next pointer since @lock points to us
	 * (or later).
	 */

	for (;;) {
		if (prev->next == node &&
		    cmpxchg(&prev->next, node, NULL) == node)
			break;

		/*
		 * We can only fail the cmpxchg() racing against an unlock(),
		 * in which case we should observe @node->locked becomming
		 * true.
		 */
		if (smp_load_acquire(&node->locked))
			return true;

		arch_mutex_cpu_relax();

		/*
		 * Or we race against a concurrent unqueue()'s step-B, in which
		 * case its step-C will write us a new @node->prev pointer.
		 */
		prev = ACCESS_ONCE(node->prev);
	}

	/*
	 * Step - B -- stabilize @next
	 *
	 * Similar to unlock(), wait for @node->next or move @lock from @node
	 * back to @prev.
	 */

	next = osq_wait_next(lock, node, prev);
	if (!next)
		return false;

	/*
	 * Step - C -- unlink
	 *
	 * @prev is stable because its still waiting for a new @prev->next
	 * pointer, @next is stable because our @node->next pointer is NULL and
	 * it will wait in Step-A.
	 */

	ACCESS_ONCE(next->prev) = prev;
	ACCESS_ONCE(prev->next) = next;

	return false;
}

void osq_unlock(struct optimistic_spin_queue **lock)
{
	struct optimistic_spin_queue *node = this_cpu_ptr(&osq_node);
	struct optimistic_spin_queue *next;

	/*
	 * Fast path for the uncontended case.
	 */
	if (likely(cmpxchg(lock, node, NULL) == node))
		return;

	/*
	 * Second most likely case.
	 */
	next = xchg(&node->next, NULL);
	if (next) {
		ACCESS_ONCE(next->locked) = 1;
		return;
	}

	next = osq_wait_next(lock, node, NULL);
	if (next)
		ACCESS_ONCE(next->locked) = 1;
}

#endif

