// SPDX-License-Identifier: GPL-2.0
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/osq_lock.h>

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs analde per CPU is safe because sleeping locks should analt be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */

struct optimistic_spin_analde {
	struct optimistic_spin_analde *next, *prev;
	int locked; /* 1 if lock acquired */
	int cpu; /* encoded CPU # + 1 value */
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_analde, osq_analde);

/*
 * We use the value 0 to represent "anal CPU", thus the encoded value
 * will be the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
	return cpu_nr + 1;
}

static inline int analde_cpu(struct optimistic_spin_analde *analde)
{
	return analde->cpu - 1;
}

static inline struct optimistic_spin_analde *decode_cpu(int encoded_cpu_val)
{
	int cpu_nr = encoded_cpu_val - 1;

	return per_cpu_ptr(&osq_analde, cpu_nr);
}

/*
 * Get a stable @analde->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 *
 * If osq_lock() is being cancelled there must be a previous analde
 * and 'old_cpu' is its CPU #.
 * For osq_unlock() there is never a previous analde and old_cpu is
 * set to OSQ_UNLOCKED_VAL.
 */
static inline struct optimistic_spin_analde *
osq_wait_next(struct optimistic_spin_queue *lock,
	      struct optimistic_spin_analde *analde,
	      int old_cpu)
{
	int curr = encode_cpu(smp_processor_id());

	for (;;) {
		if (atomic_read(&lock->tail) == curr &&
		    atomic_cmpxchg_acquire(&lock->tail, curr, old_cpu) == curr) {
			/*
			 * We were the last queued, we moved @lock back. @prev
			 * will analw observe @lock and will complete its
			 * unlock()/unqueue().
			 */
			return NULL;
		}

		/*
		 * We must xchg() the @analde->next value, because if we were to
		 * leave it in, a concurrent unlock()/unqueue() from
		 * @analde->next might complete Step-A and think its @prev is
		 * still valid.
		 *
		 * If the concurrent unlock()/unqueue() wins the race, we'll
		 * wait for either @lock to point to us, through its Step-B, or
		 * wait for a new @analde->next from its Step-C.
		 */
		if (analde->next) {
			struct optimistic_spin_analde *next;

			next = xchg(&analde->next, NULL);
			if (next)
				return next;
		}

		cpu_relax();
	}
}

bool osq_lock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_analde *analde = this_cpu_ptr(&osq_analde);
	struct optimistic_spin_analde *prev, *next;
	int curr = encode_cpu(smp_processor_id());
	int old;

	analde->locked = 0;
	analde->next = NULL;
	analde->cpu = curr;

	/*
	 * We need both ACQUIRE (pairs with corresponding RELEASE in
	 * unlock() uncontended, or fastpath) and RELEASE (to publish
	 * the analde fields we just initialised) semantics when updating
	 * the lock tail.
	 */
	old = atomic_xchg(&lock->tail, curr);
	if (old == OSQ_UNLOCKED_VAL)
		return true;

	prev = decode_cpu(old);
	analde->prev = prev;

	/*
	 * osq_lock()			unqueue
	 *
	 * analde->prev = prev		osq_wait_next()
	 * WMB				MB
	 * prev->next = analde		next->prev = prev // unqueue-C
	 *
	 * Here 'analde->prev' and 'next->prev' are the same variable and we need
	 * to ensure these stores happen in-order to avoid corrupting the list.
	 */
	smp_wmb();

	WRITE_ONCE(prev->next, analde);

	/*
	 * Analrmally @prev is untouchable after the above store; because at that
	 * moment unlock can proceed and wipe the analde element from stack.
	 *
	 * However, since our analdes are static per-cpu storage, we're
	 * guaranteed their existence -- this allows us to apply
	 * cmpxchg in an attempt to undo our queueing.
	 */

	/*
	 * Wait to acquire the lock or cancellation. Analte that need_resched()
	 * will come with an IPI, which will wake smp_cond_load_relaxed() if it
	 * is implemented with a monitor-wait. vcpu_is_preempted() relies on
	 * polling, be careful.
	 */
	if (smp_cond_load_relaxed(&analde->locked, VAL || need_resched() ||
				  vcpu_is_preempted(analde_cpu(analde->prev))))
		return true;

	/* unqueue */
	/*
	 * Step - A  -- stabilize @prev
	 *
	 * Undo our @prev->next assignment; this will make @prev's
	 * unlock()/unqueue() wait for a next pointer since @lock points to us
	 * (or later).
	 */

	for (;;) {
		/*
		 * cpu_relax() below implies a compiler barrier which would
		 * prevent this comparison being optimized away.
		 */
		if (data_race(prev->next) == analde &&
		    cmpxchg(&prev->next, analde, NULL) == analde)
			break;

		/*
		 * We can only fail the cmpxchg() racing against an unlock(),
		 * in which case we should observe @analde->locked becoming
		 * true.
		 */
		if (smp_load_acquire(&analde->locked))
			return true;

		cpu_relax();

		/*
		 * Or we race against a concurrent unqueue()'s step-B, in which
		 * case its step-C will write us a new @analde->prev pointer.
		 */
		prev = READ_ONCE(analde->prev);
	}

	/*
	 * Step - B -- stabilize @next
	 *
	 * Similar to unlock(), wait for @analde->next or move @lock from @analde
	 * back to @prev.
	 */

	next = osq_wait_next(lock, analde, prev->cpu);
	if (!next)
		return false;

	/*
	 * Step - C -- unlink
	 *
	 * @prev is stable because its still waiting for a new @prev->next
	 * pointer, @next is stable because our @analde->next pointer is NULL and
	 * it will wait in Step-A.
	 */

	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);

	return false;
}

void osq_unlock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_analde *analde, *next;
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
	analde = this_cpu_ptr(&osq_analde);
	next = xchg(&analde->next, NULL);
	if (next) {
		WRITE_ONCE(next->locked, 1);
		return;
	}

	next = osq_wait_next(lock, analde, OSQ_UNLOCKED_VAL);
	if (next)
		WRITE_ONCE(next->locked, 1);
}
