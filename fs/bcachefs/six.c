// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/log2.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>

#include "six.h"

#ifdef DEBUG
#define EBUG_ON(cond)			BUG_ON(cond)
#else
#define EBUG_ON(cond)			do {} while (0)
#endif

#define six_acquire(l, t, r, ip)	lock_acquire(l, 0, t, r, 1, NULL, ip)
#define six_release(l, ip)		lock_release(l, ip)

static void do_six_unlock_type(struct six_lock *lock, enum six_lock_type type);

/*
 * bits 0-26		reader count
 * bits 26-27		write_locking (a thread is trying to get a write lock,
 *			but does not have one yet)
 * bits 27-28		held for intent
 * bits 28-29		nospin - optimistic spinning has timed out
 * bits 29-30		has read waiters
 * bits 30-31		has intent waiters
 * bits 31-32		has write waiters
 * bits 32-64		sequence number: incremented on every write lock or
 *			unlock, thus bit 33 (sequence number odd) indicates
 *			lock is currently held for write
 */

#define SIX_STATE_READ_OFFSET		0
#define SIX_STATE_READ_BITS		26

#define SIX_STATE_READ_LOCK		~(~0ULL << 26)
#define SIX_STATE_WRITE_LOCKING		(1ULL << 26)
#define SIX_STATE_INTENT_HELD		(1ULL << 27)
#define SIX_STATE_NOSPIN		(1ULL << 28)
#define SIX_STATE_WAITING_READ		(1ULL << (29 + SIX_LOCK_read))
#define SIX_STATE_WAITING_INTENT	(1ULL << (29 + SIX_LOCK_intent))
#define SIX_STATE_WAITING_WRITE		(1ULL << (29 + SIX_LOCK_write))

#define SIX_STATE_SEQ_OFFSET		32
#define SIX_STATE_SEQ_BITS		32
#define SIX_STATE_SEQ			(~0ULL << 32)

#define SIX_LOCK_HELD_read		SIX_STATE_READ_LOCK
#define SIX_LOCK_HELD_intent		SIX_STATE_INTENT_HELD
#define SIX_LOCK_HELD_write		(1ULL << SIX_STATE_SEQ_OFFSET)

struct six_lock_vals {
	/* Value we add to the lock in order to take the lock: */
	u64			lock_val;

	/* If the lock has this value (used as a mask), taking the lock fails: */
	u64			lock_fail;

	/* Value we add to the lock in order to release the lock: */
	u64			unlock_val;

	/* Mask that indicates lock is held for this type: */
	u64			held_mask;

	/* Waitlist we wakeup when releasing the lock: */
	enum six_lock_type	unlock_wakeup;
};

#define LOCK_VALS {							\
	[SIX_LOCK_read] = {						\
		.lock_val	= 1ULL << SIX_STATE_READ_OFFSET,	\
		.lock_fail	= SIX_LOCK_HELD_write|SIX_STATE_WRITE_LOCKING,\
		.unlock_val	= -(1ULL << SIX_STATE_READ_OFFSET),	\
		.held_mask	= SIX_LOCK_HELD_read,			\
		.unlock_wakeup	= SIX_LOCK_write,			\
	},								\
	[SIX_LOCK_intent] = {						\
		.lock_val	= SIX_STATE_INTENT_HELD,		\
		.lock_fail	= SIX_LOCK_HELD_intent,			\
		.unlock_val	= -SIX_STATE_INTENT_HELD,		\
		.held_mask	= SIX_LOCK_HELD_intent,			\
		.unlock_wakeup	= SIX_LOCK_intent,			\
	},								\
	[SIX_LOCK_write] = {						\
		.lock_val	= SIX_LOCK_HELD_write,			\
		.lock_fail	= SIX_LOCK_HELD_read,			\
		.unlock_val	= SIX_LOCK_HELD_write,			\
		.held_mask	= SIX_LOCK_HELD_write,			\
		.unlock_wakeup	= SIX_LOCK_read,			\
	},								\
}

static inline u32 six_state_seq(u64 state)
{
	return state >> SIX_STATE_SEQ_OFFSET;
}

#ifdef CONFIG_GENERIC_ATOMIC64

static inline void six_set_bitmask(struct six_lock *lock, u64 mask)
{
	u64 old, new, v = atomic64_read(&lock->state);

	do {
		old = new = v;
		if ((old & mask) == mask)
			break;
		new |= mask;
	} while ((v = atomic64_cmpxchg(&lock->state, old, new)) != old);
}

static inline void six_clear_bitmask(struct six_lock *lock, u64 mask)
{
	u64 old, new, v = atomic64_read(&lock->state);

	do {
		old = new = v;
		if (!(old & mask))
			break;
		new &= ~mask;
	} while ((v = atomic64_cmpxchg(&lock->state, old, new)) != old);
}

#else

/*
 * Returns the index of the first set bit, treating @mask as an array of ulongs:
 * that is, a bit index that can be passed to test_bit()/set_bit().
 *
 * Assumes the set bit we want is in the low 4 bytes:
 */
static inline unsigned u64_mask_to_ulong_bitnr(u64 mask)
{
#if BITS_PER_LONG == 64
	return ilog2(mask);
#else
#if defined(__LITTLE_ENDIAN)
	return ilog2((u32) mask);
#elif defined(__BIG_ENDIAN)
	return ilog2((u32) mask) + 32;
#else
#error Unknown byteorder
#endif
#endif
}

static inline void six_set_bitmask(struct six_lock *lock, u64 mask)
{
	unsigned bitnr = u64_mask_to_ulong_bitnr(mask);

	if (!test_bit(bitnr, (unsigned long *) &lock->state))
		set_bit(bitnr, (unsigned long *) &lock->state);
}

static inline void six_clear_bitmask(struct six_lock *lock, u64 mask)
{
	unsigned bitnr = u64_mask_to_ulong_bitnr(mask);

	if (test_bit(bitnr, (unsigned long *) &lock->state))
		clear_bit(bitnr, (unsigned long *) &lock->state);
}

#endif

static inline void six_set_owner(struct six_lock *lock, enum six_lock_type type,
				 u64 old, struct task_struct *owner)
{
	if (type != SIX_LOCK_intent)
		return;

	if (!(old & SIX_LOCK_HELD_intent)) {
		EBUG_ON(lock->owner);
		lock->owner = owner;
	} else {
		EBUG_ON(lock->owner != current);
	}
}

static inline unsigned pcpu_read_count(struct six_lock *lock)
{
	unsigned read_count = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		read_count += *per_cpu_ptr(lock->readers, cpu);
	return read_count;
}

static int __do_six_trylock_type(struct six_lock *lock,
				 enum six_lock_type type,
				 struct task_struct *task,
				 bool try)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	int ret;
	u64 old, new, v;

	EBUG_ON(type == SIX_LOCK_write && lock->owner != task);
	EBUG_ON(type == SIX_LOCK_write &&
		(atomic64_read(&lock->state) & SIX_LOCK_HELD_write));
	EBUG_ON(type == SIX_LOCK_write &&
		(try != !(atomic64_read(&lock->state) & SIX_STATE_WRITE_LOCKING)));

	/*
	 * Percpu reader mode:
	 *
	 * The basic idea behind this algorithm is that you can implement a lock
	 * between two threads without any atomics, just memory barriers:
	 *
	 * For two threads you'll need two variables, one variable for "thread a
	 * has the lock" and another for "thread b has the lock".
	 *
	 * To take the lock, a thread sets its variable indicating that it holds
	 * the lock, then issues a full memory barrier, then reads from the
	 * other thread's variable to check if the other thread thinks it has
	 * the lock. If we raced, we backoff and retry/sleep.
	 */

	if (type == SIX_LOCK_read && lock->readers) {
		preempt_disable();
		this_cpu_inc(*lock->readers); /* signal that we own lock */

		smp_mb();

		old = atomic64_read(&lock->state);
		ret = !(old & l[type].lock_fail);

		this_cpu_sub(*lock->readers, !ret);
		preempt_enable();

		/*
		 * If we failed because a writer was trying to take the
		 * lock, issue a wakeup because we might have caused a
		 * spurious trylock failure:
		 */
		if (old & SIX_STATE_WRITE_LOCKING)
			ret = -1 - SIX_LOCK_write;
	} else if (type == SIX_LOCK_write && lock->readers) {
		if (try) {
			atomic64_add(SIX_STATE_WRITE_LOCKING,
				     &lock->state);
			smp_mb__after_atomic();
		}

		ret = !pcpu_read_count(lock);

		/*
		 * On success, we increment lock->seq; also we clear
		 * write_locking unless we failed from the lock path:
		 */
		v = 0;
		if (ret)
			v += SIX_LOCK_HELD_write;
		if (ret || try)
			v -= SIX_STATE_WRITE_LOCKING;

		if (try && !ret) {
			old = atomic64_add_return(v, &lock->state);
			if (old & SIX_STATE_WAITING_READ)
				ret = -1 - SIX_LOCK_read;
		} else {
			atomic64_add(v, &lock->state);
		}
	} else {
		v = atomic64_read(&lock->state);
		do {
			new = old = v;

			if (!(old & l[type].lock_fail)) {
				new += l[type].lock_val;

				if (type == SIX_LOCK_write)
					new &= ~SIX_STATE_WRITE_LOCKING;
			} else {
				break;
			}
		} while ((v = atomic64_cmpxchg_acquire(&lock->state, old, new)) != old);

		ret = !(old & l[type].lock_fail);

		EBUG_ON(ret && !(atomic64_read(&lock->state) & l[type].held_mask));
	}

	if (ret > 0)
		six_set_owner(lock, type, old, task);

	EBUG_ON(type == SIX_LOCK_write && (try || ret > 0) &&
		(atomic64_read(&lock->state) & SIX_STATE_WRITE_LOCKING));

	return ret;
}

static void __six_lock_wakeup(struct six_lock *lock, enum six_lock_type lock_type)
{
	struct six_lock_waiter *w, *next;
	struct task_struct *task;
	bool saw_one;
	int ret;
again:
	ret = 0;
	saw_one = false;
	raw_spin_lock(&lock->wait_lock);

	list_for_each_entry_safe(w, next, &lock->wait_list, list) {
		if (w->lock_want != lock_type)
			continue;

		if (saw_one && lock_type != SIX_LOCK_read)
			goto unlock;
		saw_one = true;

		ret = __do_six_trylock_type(lock, lock_type, w->task, false);
		if (ret <= 0)
			goto unlock;

		__list_del(w->list.prev, w->list.next);
		task = w->task;
		/*
		 * Do no writes to @w besides setting lock_acquired - otherwise
		 * we would need a memory barrier:
		 */
		barrier();
		w->lock_acquired = true;
		wake_up_process(task);
	}

	six_clear_bitmask(lock, SIX_STATE_WAITING_READ << lock_type);
unlock:
	raw_spin_unlock(&lock->wait_lock);

	if (ret < 0) {
		lock_type = -ret - 1;
		goto again;
	}
}

__always_inline
static void six_lock_wakeup(struct six_lock *lock, u64 state,
			    enum six_lock_type lock_type)
{
	if (lock_type == SIX_LOCK_write && (state & SIX_LOCK_HELD_read))
		return;

	if (!(state & (SIX_STATE_WAITING_READ << lock_type)))
		return;

	__six_lock_wakeup(lock, lock_type);
}

__always_inline
static bool do_six_trylock_type(struct six_lock *lock,
				enum six_lock_type type,
				bool try)
{
	int ret;

	ret = __do_six_trylock_type(lock, type, current, try);
	if (ret < 0)
		__six_lock_wakeup(lock, -ret - 1);

	return ret > 0;
}

bool six_trylock_ip_type(struct six_lock *lock, enum six_lock_type type,
			 unsigned long ip)
{
	if (!do_six_trylock_type(lock, type, true))
		return false;

	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 1, type == SIX_LOCK_read, ip);
	return true;
}

bool six_relock_ip_type(struct six_lock *lock, enum six_lock_type type,
			unsigned seq, unsigned long ip)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	u64 old, v;

	EBUG_ON(type == SIX_LOCK_write);

	if (type == SIX_LOCK_read &&
	    lock->readers) {
		bool ret;

		preempt_disable();
		this_cpu_inc(*lock->readers);

		smp_mb();

		old = atomic64_read(&lock->state);
		ret = !(old & l[type].lock_fail) && six_state_seq(old) == seq;

		this_cpu_sub(*lock->readers, !ret);
		preempt_enable();

		/*
		 * Similar to the lock path, we may have caused a spurious write
		 * lock fail and need to issue a wakeup:
		 */
		if (ret)
			six_acquire(&lock->dep_map, 1, type == SIX_LOCK_read, ip);
		else if (old & SIX_STATE_WRITE_LOCKING)
			six_lock_wakeup(lock, old, SIX_LOCK_write);

		return ret;
	}

	v = atomic64_read(&lock->state);
	do {
		old = v;

		if ((old & l[type].lock_fail) || six_state_seq(old) != seq)
			return false;
	} while ((v = atomic64_cmpxchg_acquire(&lock->state,
				old,
				old + l[type].lock_val)) != old);

	six_set_owner(lock, type, old, current);
	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 1, type == SIX_LOCK_read, ip);
	return true;
}
EXPORT_SYMBOL_GPL(six_relock_ip_type);

#ifdef CONFIG_SIX_LOCK_SPIN_ON_OWNER

static inline bool six_can_spin_on_owner(struct six_lock *lock)
{
	struct task_struct *owner;
	bool ret;

	if (need_resched())
		return false;

	rcu_read_lock();
	owner = READ_ONCE(lock->owner);
	ret = !owner || owner_on_cpu(owner);
	rcu_read_unlock();

	return ret;
}

static inline bool six_spin_on_owner(struct six_lock *lock,
				     struct task_struct *owner,
				     u64 end_time)
{
	bool ret = true;
	unsigned loop = 0;

	rcu_read_lock();
	while (lock->owner == owner) {
		/*
		 * Ensure we emit the owner->on_cpu, dereference _after_
		 * checking lock->owner still matches owner. If that fails,
		 * owner might point to freed memory. If it still matches,
		 * the rcu_read_lock() ensures the memory stays valid.
		 */
		barrier();

		if (!owner_on_cpu(owner) || need_resched()) {
			ret = false;
			break;
		}

		if (!(++loop & 0xf) && (time_after64(sched_clock(), end_time))) {
			six_set_bitmask(lock, SIX_STATE_NOSPIN);
			ret = false;
			break;
		}

		cpu_relax();
	}
	rcu_read_unlock();

	return ret;
}

static inline bool six_optimistic_spin(struct six_lock *lock, enum six_lock_type type)
{
	struct task_struct *task = current;
	u64 end_time;

	if (type == SIX_LOCK_write)
		return false;

	preempt_disable();
	if (!six_can_spin_on_owner(lock))
		goto fail;

	if (!osq_lock(&lock->osq))
		goto fail;

	end_time = sched_clock() + 10 * NSEC_PER_USEC;

	while (1) {
		struct task_struct *owner;

		/*
		 * If there's an owner, wait for it to either
		 * release the lock or go to sleep.
		 */
		owner = READ_ONCE(lock->owner);
		if (owner && !six_spin_on_owner(lock, owner, end_time))
			break;

		if (do_six_trylock_type(lock, type, false)) {
			osq_unlock(&lock->osq);
			preempt_enable();
			return true;
		}

		/*
		 * When there's no owner, we might have preempted between the
		 * owner acquiring the lock and setting the owner field. If
		 * we're an RT task that will live-lock because we won't let
		 * the owner complete.
		 */
		if (!owner && (need_resched() || rt_task(task)))
			break;

		/*
		 * The cpu_relax() call is a compiler barrier which forces
		 * everything in this loop to be re-loaded. We don't need
		 * memory barriers as we'll eventually observe the right
		 * values at the cost of a few extra spins.
		 */
		cpu_relax();
	}

	osq_unlock(&lock->osq);
fail:
	preempt_enable();

	/*
	 * If we fell out of the spin path because of need_resched(),
	 * reschedule now, before we try-lock again. This avoids getting
	 * scheduled out right after we obtained the lock.
	 */
	if (need_resched())
		schedule();

	return false;
}

#else /* CONFIG_SIX_LOCK_SPIN_ON_OWNER */

static inline bool six_optimistic_spin(struct six_lock *lock, enum six_lock_type type)
{
	return false;
}

#endif

noinline
static int __six_lock_type_slowpath(struct six_lock *lock, enum six_lock_type type,
				    struct six_lock_waiter *wait,
				    six_lock_should_sleep_fn should_sleep_fn, void *p,
				    unsigned long ip)
{
	u64 old;
	int ret = 0;

	if (type == SIX_LOCK_write) {
		EBUG_ON(atomic64_read(&lock->state) & SIX_STATE_WRITE_LOCKING);
		atomic64_add(SIX_STATE_WRITE_LOCKING, &lock->state);
		smp_mb__after_atomic();
	}

	if (six_optimistic_spin(lock, type))
		goto out;

	lock_contended(&lock->dep_map, ip);

	wait->task		= current;
	wait->lock_want		= type;
	wait->lock_acquired	= false;

	raw_spin_lock(&lock->wait_lock);
	six_set_bitmask(lock, SIX_STATE_WAITING_READ << type);
	/*
	 * Retry taking the lock after taking waitlist lock, have raced with an
	 * unlock:
	 */
	ret = __do_six_trylock_type(lock, type, current, false);
	if (ret <= 0) {
		wait->start_time = local_clock();

		if (!list_empty(&lock->wait_list)) {
			struct six_lock_waiter *last =
				list_last_entry(&lock->wait_list,
					struct six_lock_waiter, list);

			if (time_before_eq64(wait->start_time, last->start_time))
				wait->start_time = last->start_time + 1;
		}

		list_add_tail(&wait->list, &lock->wait_list);
	}
	raw_spin_unlock(&lock->wait_lock);

	if (unlikely(ret > 0)) {
		ret = 0;
		goto out;
	}

	if (unlikely(ret < 0)) {
		__six_lock_wakeup(lock, -ret - 1);
		ret = 0;
	}

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (wait->lock_acquired)
			break;

		ret = should_sleep_fn ? should_sleep_fn(lock, p) : 0;
		if (unlikely(ret)) {
			raw_spin_lock(&lock->wait_lock);
			if (!wait->lock_acquired)
				list_del(&wait->list);
			raw_spin_unlock(&lock->wait_lock);

			if (wait->lock_acquired)
				do_six_unlock_type(lock, type);
			break;
		}

		schedule();
	}

	__set_current_state(TASK_RUNNING);
out:
	if (ret && type == SIX_LOCK_write) {
		six_clear_bitmask(lock, SIX_STATE_WRITE_LOCKING);
		six_lock_wakeup(lock, old, SIX_LOCK_read);
	}

	return ret;
}

int six_lock_type_ip_waiter(struct six_lock *lock, enum six_lock_type type,
			    struct six_lock_waiter *wait,
			    six_lock_should_sleep_fn should_sleep_fn, void *p,
			    unsigned long ip)
{
	int ret;

	wait->start_time = 0;

	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 0, type == SIX_LOCK_read, ip);

	ret = do_six_trylock_type(lock, type, true) ? 0
		: __six_lock_type_slowpath(lock, type, wait, should_sleep_fn, p, ip);

	if (ret && type != SIX_LOCK_write)
		six_release(&lock->dep_map, ip);
	if (!ret)
		lock_acquired(&lock->dep_map, ip);

	return ret;
}
EXPORT_SYMBOL_GPL(six_lock_type_ip_waiter);

__always_inline
static void do_six_unlock_type(struct six_lock *lock, enum six_lock_type type)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	u64 state;

	if (type == SIX_LOCK_intent)
		lock->owner = NULL;

	if (type == SIX_LOCK_read &&
	    lock->readers) {
		smp_mb(); /* unlock barrier */
		this_cpu_dec(*lock->readers);
		smp_mb(); /* between unlocking and checking for waiters */
		state = atomic64_read(&lock->state);
	} else {
		u64 v = l[type].unlock_val;

		if (type != SIX_LOCK_read)
			v -= atomic64_read(&lock->state) & SIX_STATE_NOSPIN;

		EBUG_ON(!(atomic64_read(&lock->state) & l[type].held_mask));
		state = atomic64_add_return_release(v, &lock->state);
	}

	six_lock_wakeup(lock, state, l[type].unlock_wakeup);
}

void six_unlock_ip_type(struct six_lock *lock, enum six_lock_type type, unsigned long ip)
{
	EBUG_ON(type == SIX_LOCK_write &&
		!(atomic64_read(&lock->state) & SIX_LOCK_HELD_intent));
	EBUG_ON((type == SIX_LOCK_write ||
		 type == SIX_LOCK_intent) &&
		lock->owner != current);

	if (type != SIX_LOCK_write)
		six_release(&lock->dep_map, ip);

	if (type == SIX_LOCK_intent &&
	    lock->intent_lock_recurse) {
		--lock->intent_lock_recurse;
		return;
	}

	do_six_unlock_type(lock, type);
}
EXPORT_SYMBOL_GPL(six_unlock_ip_type);

/* Convert from intent to read: */
void six_lock_downgrade(struct six_lock *lock)
{
	six_lock_increment(lock, SIX_LOCK_read);
	six_unlock_intent(lock);
}
EXPORT_SYMBOL_GPL(six_lock_downgrade);

bool six_lock_tryupgrade(struct six_lock *lock)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	u64 old, new, v = atomic64_read(&lock->state);

	do {
		new = old = v;

		if (new & SIX_LOCK_HELD_intent)
			return false;

		if (!lock->readers) {
			EBUG_ON(!(new & SIX_LOCK_HELD_read));
			new += l[SIX_LOCK_read].unlock_val;
		}

		new |= SIX_LOCK_HELD_intent;
	} while ((v = atomic64_cmpxchg_acquire(&lock->state, old, new)) != old);

	if (lock->readers)
		this_cpu_dec(*lock->readers);

	six_set_owner(lock, SIX_LOCK_intent, old, current);

	return true;
}
EXPORT_SYMBOL_GPL(six_lock_tryupgrade);

bool six_trylock_convert(struct six_lock *lock,
			 enum six_lock_type from,
			 enum six_lock_type to)
{
	EBUG_ON(to == SIX_LOCK_write || from == SIX_LOCK_write);

	if (to == from)
		return true;

	if (to == SIX_LOCK_read) {
		six_lock_downgrade(lock);
		return true;
	} else {
		return six_lock_tryupgrade(lock);
	}
}
EXPORT_SYMBOL_GPL(six_trylock_convert);

/*
 * Increment read/intent lock count, assuming we already have it read or intent
 * locked:
 */
void six_lock_increment(struct six_lock *lock, enum six_lock_type type)
{
	const struct six_lock_vals l[] = LOCK_VALS;

	six_acquire(&lock->dep_map, 0, type == SIX_LOCK_read, _RET_IP_);

	/* XXX: assert already locked, and that we don't overflow: */

	switch (type) {
	case SIX_LOCK_read:
		if (lock->readers) {
			this_cpu_inc(*lock->readers);
		} else {
			EBUG_ON(!(atomic64_read(&lock->state) &
				  (SIX_LOCK_HELD_read|
				   SIX_LOCK_HELD_intent)));
			atomic64_add(l[type].lock_val, &lock->state);
		}
		break;
	case SIX_LOCK_intent:
		EBUG_ON(!(atomic64_read(&lock->state) & SIX_LOCK_HELD_intent));
		lock->intent_lock_recurse++;
		break;
	case SIX_LOCK_write:
		BUG();
		break;
	}
}
EXPORT_SYMBOL_GPL(six_lock_increment);

void six_lock_wakeup_all(struct six_lock *lock)
{
	u64 state = atomic64_read(&lock->state);
	struct six_lock_waiter *w;

	six_lock_wakeup(lock, state, SIX_LOCK_read);
	six_lock_wakeup(lock, state, SIX_LOCK_intent);
	six_lock_wakeup(lock, state, SIX_LOCK_write);

	raw_spin_lock(&lock->wait_lock);
	list_for_each_entry(w, &lock->wait_list, list)
		wake_up_process(w->task);
	raw_spin_unlock(&lock->wait_lock);
}
EXPORT_SYMBOL_GPL(six_lock_wakeup_all);

/*
 * Returns lock held counts, for both read and intent
 */
struct six_lock_count six_lock_counts(struct six_lock *lock)
{
	struct six_lock_count ret;

	ret.n[SIX_LOCK_read]	= !lock->readers
		? atomic64_read(&lock->state) & SIX_STATE_READ_LOCK
		: pcpu_read_count(lock);
	ret.n[SIX_LOCK_intent]	= !!(atomic64_read(&lock->state) & SIX_LOCK_HELD_intent) +
		lock->intent_lock_recurse;
	ret.n[SIX_LOCK_write]	= !!(atomic64_read(&lock->state) & SIX_LOCK_HELD_write);

	return ret;
}
EXPORT_SYMBOL_GPL(six_lock_counts);

void six_lock_readers_add(struct six_lock *lock, int nr)
{
	if (lock->readers)
		this_cpu_add(*lock->readers, nr);
	else /* reader count starts at bit 0 */
		atomic64_add(nr, &lock->state);
}
EXPORT_SYMBOL_GPL(six_lock_readers_add);

void six_lock_exit(struct six_lock *lock)
{
	WARN_ON(lock->readers && pcpu_read_count(lock));
	WARN_ON(atomic64_read(&lock->state) & SIX_LOCK_HELD_read);

	free_percpu(lock->readers);
	lock->readers = NULL;
}
EXPORT_SYMBOL_GPL(six_lock_exit);

void __six_lock_init(struct six_lock *lock, const char *name,
		     struct lock_class_key *key, enum six_lock_init_flags flags)
{
	atomic64_set(&lock->state, 0);
	raw_spin_lock_init(&lock->wait_lock);
	INIT_LIST_HEAD(&lock->wait_list);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	debug_check_no_locks_freed((void *) lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif

	if (flags & SIX_LOCK_INIT_PCPU) {
		/*
		 * We don't return an error here on memory allocation failure
		 * since percpu is an optimization, and locks will work with the
		 * same semantics in non-percpu mode: callers can check for
		 * failure if they wish by checking lock->readers, but generally
		 * will not want to treat it as an error.
		 */
		lock->readers = alloc_percpu(unsigned);
	}
}
EXPORT_SYMBOL_GPL(__six_lock_init);
