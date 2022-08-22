// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/log2.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>

#include "six.h"

#ifdef DEBUG
#define EBUG_ON(cond)		BUG_ON(cond)
#else
#define EBUG_ON(cond)		do {} while (0)
#endif

#define six_acquire(l, t)	lock_acquire(l, 0, t, 0, 0, NULL, _RET_IP_)
#define six_release(l)		lock_release(l, _RET_IP_)

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

#define __SIX_LOCK_HELD_read	__SIX_VAL(read_lock, ~0)
#define __SIX_LOCK_HELD_intent	__SIX_VAL(intent_lock, ~0)
#define __SIX_LOCK_HELD_write	__SIX_VAL(seq, 1)

#define LOCK_VALS {							\
	[SIX_LOCK_read] = {						\
		.lock_val	= __SIX_VAL(read_lock, 1),		\
		.lock_fail	= __SIX_LOCK_HELD_write + __SIX_VAL(write_locking, 1),\
		.unlock_val	= -__SIX_VAL(read_lock, 1),		\
		.held_mask	= __SIX_LOCK_HELD_read,			\
		.unlock_wakeup	= SIX_LOCK_write,			\
	},								\
	[SIX_LOCK_intent] = {						\
		.lock_val	= __SIX_VAL(intent_lock, 1),		\
		.lock_fail	= __SIX_LOCK_HELD_intent,		\
		.unlock_val	= -__SIX_VAL(intent_lock, 1),		\
		.held_mask	= __SIX_LOCK_HELD_intent,		\
		.unlock_wakeup	= SIX_LOCK_intent,			\
	},								\
	[SIX_LOCK_write] = {						\
		.lock_val	= __SIX_VAL(seq, 1),			\
		.lock_fail	= __SIX_LOCK_HELD_read,			\
		.unlock_val	= __SIX_VAL(seq, 1),			\
		.held_mask	= __SIX_LOCK_HELD_write,		\
		.unlock_wakeup	= SIX_LOCK_read,			\
	},								\
}

static inline void six_set_owner(struct six_lock *lock, enum six_lock_type type,
				 union six_lock_state old)
{
	if (type != SIX_LOCK_intent)
		return;

	if (!old.intent_lock) {
		EBUG_ON(lock->owner);
		lock->owner = current;
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

struct six_lock_waiter {
	struct list_head	list;
	struct task_struct	*task;
};

/* This is probably up there with the more evil things I've done */
#define waitlist_bitnr(id) ilog2((((union six_lock_state) { .waiters = 1 << (id) }).l))

static inline void six_lock_wakeup(struct six_lock *lock,
				   union six_lock_state state,
				   unsigned waitlist_id)
{
	if (waitlist_id == SIX_LOCK_write) {
		if (state.write_locking && !state.read_lock) {
			struct task_struct *p = READ_ONCE(lock->owner);
			if (p)
				wake_up_process(p);
		}
	} else {
		struct list_head *wait_list = &lock->wait_list[waitlist_id];
		struct six_lock_waiter *w, *next;

		if (!(state.waiters & (1 << waitlist_id)))
			return;

		clear_bit(waitlist_bitnr(waitlist_id),
			  (unsigned long *) &lock->state.v);

		raw_spin_lock(&lock->wait_lock);

		list_for_each_entry_safe(w, next, wait_list, list) {
			list_del_init(&w->list);

			if (wake_up_process(w->task) &&
			    waitlist_id != SIX_LOCK_read) {
				if (!list_empty(wait_list))
					set_bit(waitlist_bitnr(waitlist_id),
						(unsigned long *) &lock->state.v);
				break;
			}
		}

		raw_spin_unlock(&lock->wait_lock);
	}
}

static __always_inline bool do_six_trylock_type(struct six_lock *lock,
						enum six_lock_type type,
						bool try)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	union six_lock_state old, new;
	bool ret;
	u64 v;

	EBUG_ON(type == SIX_LOCK_write && lock->owner != current);
	EBUG_ON(type == SIX_LOCK_write && (lock->state.seq & 1));

	EBUG_ON(type == SIX_LOCK_write && (try != !(lock->state.write_locking)));

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
retry:
		preempt_disable();
		this_cpu_inc(*lock->readers); /* signal that we own lock */

		smp_mb();

		old.v = READ_ONCE(lock->state.v);
		ret = !(old.v & l[type].lock_fail);

		this_cpu_sub(*lock->readers, !ret);
		preempt_enable();

		/*
		 * If we failed because a writer was trying to take the
		 * lock, issue a wakeup because we might have caused a
		 * spurious trylock failure:
		 */
		if (old.write_locking) {
			struct task_struct *p = READ_ONCE(lock->owner);

			if (p)
				wake_up_process(p);
		}

		/*
		 * If we failed from the lock path and the waiting bit wasn't
		 * set, set it:
		 */
		if (!try && !ret) {
			v = old.v;

			do {
				new.v = old.v = v;

				if (!(old.v & l[type].lock_fail))
					goto retry;

				if (new.waiters & (1 << type))
					break;

				new.waiters |= 1 << type;
			} while ((v = atomic64_cmpxchg(&lock->state.counter,
						       old.v, new.v)) != old.v);
		}
	} else if (type == SIX_LOCK_write && lock->readers) {
		if (try) {
			atomic64_add(__SIX_VAL(write_locking, 1),
				     &lock->state.counter);
			smp_mb__after_atomic();
		}

		ret = !pcpu_read_count(lock);

		/*
		 * On success, we increment lock->seq; also we clear
		 * write_locking unless we failed from the lock path:
		 */
		v = 0;
		if (ret)
			v += __SIX_VAL(seq, 1);
		if (ret || try)
			v -= __SIX_VAL(write_locking, 1);

		if (try && !ret) {
			old.v = atomic64_add_return(v, &lock->state.counter);
			six_lock_wakeup(lock, old, SIX_LOCK_read);
		} else {
			atomic64_add(v, &lock->state.counter);
		}
	} else {
		v = READ_ONCE(lock->state.v);
		do {
			new.v = old.v = v;

			if (!(old.v & l[type].lock_fail)) {
				new.v += l[type].lock_val;

				if (type == SIX_LOCK_write)
					new.write_locking = 0;
			} else if (!try && type != SIX_LOCK_write &&
				   !(new.waiters & (1 << type)))
				new.waiters |= 1 << type;
			else
				break; /* waiting bit already set */
		} while ((v = atomic64_cmpxchg_acquire(&lock->state.counter,
					old.v, new.v)) != old.v);

		ret = !(old.v & l[type].lock_fail);

		EBUG_ON(ret && !(lock->state.v & l[type].held_mask));
	}

	if (ret)
		six_set_owner(lock, type, old);

	EBUG_ON(type == SIX_LOCK_write && (try || ret) && (lock->state.write_locking));

	return ret;
}

__always_inline __flatten
static bool __six_trylock_type(struct six_lock *lock, enum six_lock_type type)
{
	if (!do_six_trylock_type(lock, type, true))
		return false;

	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 1);
	return true;
}

__always_inline __flatten
static bool __six_relock_type(struct six_lock *lock, enum six_lock_type type,
			      unsigned seq)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	union six_lock_state old;
	u64 v;

	EBUG_ON(type == SIX_LOCK_write);

	if (type == SIX_LOCK_read &&
	    lock->readers) {
		bool ret;

		preempt_disable();
		this_cpu_inc(*lock->readers);

		smp_mb();

		old.v = READ_ONCE(lock->state.v);
		ret = !(old.v & l[type].lock_fail) && old.seq == seq;

		this_cpu_sub(*lock->readers, !ret);
		preempt_enable();

		/*
		 * Similar to the lock path, we may have caused a spurious write
		 * lock fail and need to issue a wakeup:
		 */
		if (old.write_locking) {
			struct task_struct *p = READ_ONCE(lock->owner);

			if (p)
				wake_up_process(p);
		}

		if (ret)
			six_acquire(&lock->dep_map, 1);

		return ret;
	}

	v = READ_ONCE(lock->state.v);
	do {
		old.v = v;

		if (old.seq != seq || old.v & l[type].lock_fail)
			return false;
	} while ((v = atomic64_cmpxchg_acquire(&lock->state.counter,
				old.v,
				old.v + l[type].lock_val)) != old.v);

	six_set_owner(lock, type, old);
	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 1);
	return true;
}

#ifdef CONFIG_SIX_LOCK_SPIN_ON_OWNER

static inline int six_can_spin_on_owner(struct six_lock *lock)
{
	struct task_struct *owner;
	int retval = 1;

	if (need_resched())
		return 0;

	rcu_read_lock();
	owner = READ_ONCE(lock->owner);
	if (owner)
		retval = owner->on_cpu;
	rcu_read_unlock();
	/*
	 * if lock->owner is not set, the mutex owner may have just acquired
	 * it and not set the owner yet or the mutex has been released.
	 */
	return retval;
}

static inline bool six_spin_on_owner(struct six_lock *lock,
				     struct task_struct *owner)
{
	bool ret = true;

	rcu_read_lock();
	while (lock->owner == owner) {
		/*
		 * Ensure we emit the owner->on_cpu, dereference _after_
		 * checking lock->owner still matches owner. If that fails,
		 * owner might point to freed memory. If it still matches,
		 * the rcu_read_lock() ensures the memory stays valid.
		 */
		barrier();

		if (!owner->on_cpu || need_resched()) {
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

	if (type == SIX_LOCK_write)
		return false;

	preempt_disable();
	if (!six_can_spin_on_owner(lock))
		goto fail;

	if (!osq_lock(&lock->osq))
		goto fail;

	while (1) {
		struct task_struct *owner;

		/*
		 * If there's an owner, wait for it to either
		 * release the lock or go to sleep.
		 */
		owner = READ_ONCE(lock->owner);
		if (owner && !six_spin_on_owner(lock, owner))
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
				    six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	union six_lock_state old;
	struct six_lock_waiter wait;
	int ret = 0;

	if (type == SIX_LOCK_write) {
		EBUG_ON(lock->state.write_locking);
		atomic64_add(__SIX_VAL(write_locking, 1), &lock->state.counter);
		smp_mb__after_atomic();
	}

	ret = should_sleep_fn ? should_sleep_fn(lock, p) : 0;
	if (ret)
		goto out_before_sleep;

	if (six_optimistic_spin(lock, type))
		goto out_before_sleep;

	lock_contended(&lock->dep_map, _RET_IP_);

	INIT_LIST_HEAD(&wait.list);
	wait.task = current;

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (type == SIX_LOCK_write)
			EBUG_ON(lock->owner != current);
		else if (list_empty_careful(&wait.list)) {
			raw_spin_lock(&lock->wait_lock);
			list_add_tail(&wait.list, &lock->wait_list[type]);
			raw_spin_unlock(&lock->wait_lock);
		}

		if (do_six_trylock_type(lock, type, false))
			break;

		ret = should_sleep_fn ? should_sleep_fn(lock, p) : 0;
		if (ret)
			break;

		schedule();
	}

	__set_current_state(TASK_RUNNING);

	if (!list_empty_careful(&wait.list)) {
		raw_spin_lock(&lock->wait_lock);
		list_del_init(&wait.list);
		raw_spin_unlock(&lock->wait_lock);
	}
out_before_sleep:
	if (ret && type == SIX_LOCK_write) {
		old.v = atomic64_sub_return(__SIX_VAL(write_locking, 1),
					    &lock->state.counter);
		six_lock_wakeup(lock, old, SIX_LOCK_read);
	}

	return ret;
}

__always_inline
static int __six_lock_type(struct six_lock *lock, enum six_lock_type type,
			   six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	int ret;

	if (type != SIX_LOCK_write)
		six_acquire(&lock->dep_map, 0);

	ret = do_six_trylock_type(lock, type, true) ? 0
		: __six_lock_type_slowpath(lock, type, should_sleep_fn, p);

	if (ret && type != SIX_LOCK_write)
		six_release(&lock->dep_map);
	if (!ret)
		lock_acquired(&lock->dep_map, _RET_IP_);

	return ret;
}

__always_inline __flatten
static void __six_unlock_type(struct six_lock *lock, enum six_lock_type type)
{
	const struct six_lock_vals l[] = LOCK_VALS;
	union six_lock_state state;

	EBUG_ON(type == SIX_LOCK_write &&
		!(lock->state.v & __SIX_LOCK_HELD_intent));

	if (type != SIX_LOCK_write)
		six_release(&lock->dep_map);

	if (type == SIX_LOCK_intent) {
		EBUG_ON(lock->owner != current);

		if (lock->intent_lock_recurse) {
			--lock->intent_lock_recurse;
			return;
		}

		lock->owner = NULL;
	}

	if (type == SIX_LOCK_read &&
	    lock->readers) {
		smp_mb(); /* unlock barrier */
		this_cpu_dec(*lock->readers);
		smp_mb(); /* between unlocking and checking for waiters */
		state.v = READ_ONCE(lock->state.v);
	} else {
		EBUG_ON(!(lock->state.v & l[type].held_mask));
		state.v = atomic64_add_return_release(l[type].unlock_val,
						      &lock->state.counter);
	}

	six_lock_wakeup(lock, state, l[type].unlock_wakeup);
}

#define __SIX_LOCK(type)						\
bool six_trylock_##type(struct six_lock *lock)				\
{									\
	return __six_trylock_type(lock, SIX_LOCK_##type);		\
}									\
EXPORT_SYMBOL_GPL(six_trylock_##type);					\
									\
bool six_relock_##type(struct six_lock *lock, u32 seq)			\
{									\
	return __six_relock_type(lock, SIX_LOCK_##type, seq);		\
}									\
EXPORT_SYMBOL_GPL(six_relock_##type);					\
									\
int six_lock_##type(struct six_lock *lock,				\
		    six_lock_should_sleep_fn should_sleep_fn, void *p)	\
{									\
	return __six_lock_type(lock, SIX_LOCK_##type, should_sleep_fn, p);\
}									\
EXPORT_SYMBOL_GPL(six_lock_##type);					\
									\
void six_unlock_##type(struct six_lock *lock)				\
{									\
	__six_unlock_type(lock, SIX_LOCK_##type);			\
}									\
EXPORT_SYMBOL_GPL(six_unlock_##type);

__SIX_LOCK(read)
__SIX_LOCK(intent)
__SIX_LOCK(write)

#undef __SIX_LOCK

/* Convert from intent to read: */
void six_lock_downgrade(struct six_lock *lock)
{
	six_lock_increment(lock, SIX_LOCK_read);
	six_unlock_intent(lock);
}
EXPORT_SYMBOL_GPL(six_lock_downgrade);

bool six_lock_tryupgrade(struct six_lock *lock)
{
	union six_lock_state old, new;
	u64 v = READ_ONCE(lock->state.v);

	do {
		new.v = old.v = v;

		if (new.intent_lock)
			return false;

		if (!lock->readers) {
			EBUG_ON(!new.read_lock);
			new.read_lock--;
		}

		new.intent_lock = 1;
	} while ((v = atomic64_cmpxchg_acquire(&lock->state.counter,
				old.v, new.v)) != old.v);

	if (lock->readers)
		this_cpu_dec(*lock->readers);

	six_set_owner(lock, SIX_LOCK_intent, old);

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

	six_acquire(&lock->dep_map, 0);

	/* XXX: assert already locked, and that we don't overflow: */

	switch (type) {
	case SIX_LOCK_read:
		if (lock->readers) {
			this_cpu_inc(*lock->readers);
		} else {
			EBUG_ON(!lock->state.read_lock &&
				!lock->state.intent_lock);
			atomic64_add(l[type].lock_val, &lock->state.counter);
		}
		break;
	case SIX_LOCK_intent:
		EBUG_ON(!lock->state.intent_lock);
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
	struct six_lock_waiter *w;

	raw_spin_lock(&lock->wait_lock);

	list_for_each_entry(w, &lock->wait_list[0], list)
		wake_up_process(w->task);
	list_for_each_entry(w, &lock->wait_list[1], list)
		wake_up_process(w->task);

	raw_spin_unlock(&lock->wait_lock);
}
EXPORT_SYMBOL_GPL(six_lock_wakeup_all);

struct free_pcpu_rcu {
	struct rcu_head		rcu;
	void __percpu		*p;
};

static void free_pcpu_rcu_fn(struct rcu_head *_rcu)
{
	struct free_pcpu_rcu *rcu =
		container_of(_rcu, struct free_pcpu_rcu, rcu);

	free_percpu(rcu->p);
	kfree(rcu);
}

void six_lock_pcpu_free_rcu(struct six_lock *lock)
{
	struct free_pcpu_rcu *rcu = kzalloc(sizeof(*rcu), GFP_KERNEL);

	if (!rcu)
		return;

	rcu->p = lock->readers;
	lock->readers = NULL;

	call_rcu(&rcu->rcu, free_pcpu_rcu_fn);
}
EXPORT_SYMBOL_GPL(six_lock_pcpu_free_rcu);

void six_lock_pcpu_free(struct six_lock *lock)
{
	BUG_ON(lock->readers && pcpu_read_count(lock));
	BUG_ON(lock->state.read_lock);

	free_percpu(lock->readers);
	lock->readers = NULL;
}
EXPORT_SYMBOL_GPL(six_lock_pcpu_free);

void six_lock_pcpu_alloc(struct six_lock *lock)
{
#ifdef __KERNEL__
	if (!lock->readers)
		lock->readers = alloc_percpu(unsigned);
#endif
}
EXPORT_SYMBOL_GPL(six_lock_pcpu_alloc);

/*
 * Returns lock held counts, for both read and intent
 */
struct six_lock_count six_lock_counts(struct six_lock *lock)
{
	struct six_lock_count ret;

	ret.n[SIX_LOCK_read]	= 0;
	ret.n[SIX_LOCK_intent]	= lock->state.intent_lock + lock->intent_lock_recurse;
	ret.n[SIX_LOCK_write]	= lock->state.seq & 1;

	if (!lock->readers)
		ret.n[SIX_LOCK_read] += lock->state.read_lock;
	else {
		int cpu;

		for_each_possible_cpu(cpu)
			ret.n[SIX_LOCK_read] += *per_cpu_ptr(lock->readers, cpu);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(six_lock_counts);
