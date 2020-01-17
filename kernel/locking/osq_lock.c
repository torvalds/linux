// SPDX-License-Identifier: GPL-2.0
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/osq_lock.h>

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs yesde per CPU is safe because sleeping locks should yest be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */
static DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_yesde, osq_yesde);

/*
 * We use the value 0 to represent "yes CPU", thus the encoded value
 * will be the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
	return cpu_nr + 1;
}

static inline int yesde_cpu(struct optimistic_spin_yesde *yesde)
{
	return yesde->cpu - 1;
}

static inline struct optimistic_spin_yesde *decode_cpu(int encoded_cpu_val)
{
	int cpu_nr = encoded_cpu_val - 1;

	return per_cpu_ptr(&osq_yesde, cpu_nr);
}

/*
 * Get a stable @yesde->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 */
static inline struct optimistic_spin_yesde *
osq_wait_next(struct optimistic_spin_queue *lock,
	      struct optimistic_spin_yesde *yesde,
	      struct optimistic_spin_yesde *prev)
{
	struct optimistic_spin_yesde *next = NULL;
	int curr = encode_cpu(smp_processor_id());
	int old;

	/*
	 * If there is a prev yesde in queue, then the 'old' value will be
	 * the prev yesde's CPU #, else it's set to OSQ_UNLOCKED_VAL since if
	 * we're currently last in queue, then the queue will then become empty.
	 */
	old = prev ? prev->cpu : OSQ_UNLOCKED_VAL;

	for (;;) {
		if (atomic_read(&lock->tail) == curr &&
		    atomic_cmpxchg_acquire(&lock->tail, curr, old) == curr) {
			/*
			 * We were the last queued, we moved @lock back. @prev
			 * will yesw observe @lock and will complete its
			 * unlock()/unqueue().
			 */
			break;
		}

		/*
		 * We must xchg() the @yesde->next value, because if we were to
		 * leave it in, a concurrent unlock()/unqueue() from
		 * @yesde->next might complete Step-A and think its @prev is
		 * still valid.
		 *
		 * If the concurrent unlock()/unqueue() wins the race, we'll
		 * wait for either @lock to point to us, through its Step-B, or
		 * wait for a new @yesde->next from its Step-C.
		 */
		if (yesde->next) {
			next = xchg(&yesde->next, NULL);
			if (next)
				break;
		}

		cpu_relax();
	}

	return next;
}

bool osq_lock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_yesde *yesde = this_cpu_ptr(&osq_yesde);
	struct optimistic_spin_yesde *prev, *next;
	int curr = encode_cpu(smp_processor_id());
	int old;

	yesde->locked = 0;
	yesde->next = NULL;
	yesde->cpu = curr;

	/*
	 * We need both ACQUIRE (pairs with corresponding RELEASE in
	 * unlock() uncontended, or fastpath) and RELEASE (to publish
	 * the yesde fields we just initialised) semantics when updating
	 * the lock tail.
	 */
	old = atomic_xchg(&lock->tail, curr);
	if (old == OSQ_UNLOCKED_VAL)
		return true;

	prev = decode_cpu(old);
	yesde->prev = prev;

	/*
	 * osq_lock()			unqueue
	 *
	 * yesde->prev = prev		osq_wait_next()
	 * WMB				MB
	 * prev->next = yesde		next->prev = prev // unqueue-C
	 *
	 * Here 'yesde->prev' and 'next->prev' are the same variable and we need
	 * to ensure these stores happen in-order to avoid corrupting the list.
	 */
	smp_wmb();

	WRITE_ONCE(prev->next, yesde);

	/*
	 * Normally @prev is untouchable after the above store; because at that
	 * moment unlock can proceed and wipe the yesde element from stack.
	 *
	 * However, since our yesdes are static per-cpu storage, we're
	 * guaranteed their existence -- this allows us to apply
	 * cmpxchg in an attempt to undo our queueing.
	 */

	while (!READ_ONCE(yesde->locked)) {
		/*
		 * If we need to reschedule bail... so we can block.
		 * Use vcpu_is_preempted() to avoid waiting for a preempted
		 * lock holder:
		 */
		if (need_resched() || vcpu_is_preempted(yesde_cpu(yesde->prev)))
			goto unqueue;

		cpu_relax();
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
		if (prev->next == yesde &&
		    cmpxchg(&prev->next, yesde, NULL) == yesde)
			break;

		/*
		 * We can only fail the cmpxchg() racing against an unlock(),
		 * in which case we should observe @yesde->locked becomming
		 * true.
		 */
		if (smp_load_acquire(&yesde->locked))
			return true;

		cpu_relax();

		/*
		 * Or we race against a concurrent unqueue()'s step-B, in which
		 * case its step-C will write us a new @yesde->prev pointer.
		 */
		prev = READ_ONCE(yesde->prev);
	}

	/*
	 * Step - B -- stabilize @next
	 *
	 * Similar to unlock(), wait for @yesde->next or move @lock from @yesde
	 * back to @prev.
	 */

	next = osq_wait_next(lock, yesde, prev);
	if (!next)
		return false;

	/*
	 * Step - C -- unlink
	 *
	 * @prev is stable because its still waiting for a new @prev->next
	 * pointer, @next is stable because our @yesde->next pointer is NULL and
	 * it will wait in Step-A.
	 */

	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);

	return false;
}

void osq_unlock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_yesde *yesde, *next;
	int curr = encode_cpu(smp_processor_id());

	/*
	 * Fast path for the uncontended case.
	 */
	if (likely(atomic_cmpxchg_release(&lock->tail, curr,
					  OSQ_UNLOCKED_VAL) == curr))
		return;

	/*
	 * Second most likely case.
	 */
	yesde = this_cpu_ptr(&osq_yesde);
	next = xchg(&yesde->next, NULL);
	if (next) {
		WRITE_ONCE(next->locked, 1);
		return;
	}

	next = osq_wait_next(lock, yesde, NULL);
	if (next)
		WRITE_ONCE(next->locked, 1);
}
