// SPDX-License-Identifier: GPL-2.0-only

/*
 * RT-specific reader/writer semaphores and reader/writer locks
 *
 * down_write/write_lock()
 *  1) Lock rtmutex
 *  2) Remove the reader BIAS to force readers into the slow path
 *  3) Wait until all readers have left the critical section
 *  4) Mark it write locked
 *
 * up_write/write_unlock()
 *  1) Remove the write locked marker
 *  2) Set the reader BIAS, so readers can use the fast path again
 *  3) Unlock rtmutex, to release blocked readers
 *
 * down_read/read_lock()
 *  1) Try fast path acquisition (reader BIAS is set)
 *  2) Take tmutex::wait_lock, which protects the writelocked flag
 *  3) If !writelocked, acquire it for read
 *  4) If writelocked, block on tmutex
 *  5) unlock rtmutex, goto 1)
 *
 * up_read/read_unlock()
 *  1) Try fast path release (reader count != 1)
 *  2) Wake the writer waiting in down_write()/write_lock() #3
 *
 * down_read/read_lock()#3 has the consequence, that rw semaphores and rw
 * locks on RT are not writer fair, but writers, which should be avoided in
 * RT tasks (think mmap_sem), are subject to the rtmutex priority/DL
 * inheritance mechanism.
 *
 * It's possible to make the rw primitives writer fair by keeping a list of
 * active readers. A blocked writer would force all newly incoming readers
 * to block on the rtmutex, but the rtmutex would have to be proxy locked
 * for one reader after the other. We can't use multi-reader inheritance
 * because there is no way to support that with SCHED_DEADLINE.
 * Implementing the one by one reader boosting/handover mechanism is a
 * major surgery for a very dubious value.
 *
 * The risk of writer starvation is there, but the pathological use cases
 * which trigger it are not necessarily the typical RT workloads.
 *
 * Fast-path orderings:
 * The lock/unlock of readers can run in fast paths: lock and unlock are only
 * atomic ops, and there is no inner lock to provide ACQUIRE and RELEASE
 * semantics of rwbase_rt. Atomic ops should thus provide _acquire()
 * and _release() (or stronger).
 *
 * Common code shared between RT rw_semaphore and rwlock
 */

static __always_inline int rwbase_read_trylock(struct rwbase_rt *rwb)
{
	int r;

	/*
	 * Increment reader count, if sem->readers < 0, i.e. READER_BIAS is
	 * set.
	 */
	for (r = atomic_read(&rwb->readers); r < 0;) {
		if (likely(atomic_try_cmpxchg_acquire(&rwb->readers, &r, r + 1)))
			return 1;
	}
	return 0;
}

static int __sched __rwbase_read_lock(struct rwbase_rt *rwb,
				      unsigned int state)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	int ret;

	rwbase_pre_schedule();
	raw_spin_lock_irq(&rtm->wait_lock);

	/*
	 * Call into the slow lock path with the rtmutex->wait_lock
	 * held, so this can't result in the following race:
	 *
	 * Reader1		Reader2		Writer
	 *			down_read()
	 *					down_write()
	 *					rtmutex_lock(m)
	 *					wait()
	 * down_read()
	 * unlock(m->wait_lock)
	 *			up_read()
	 *			wake(Writer)
	 *					lock(m->wait_lock)
	 *					sem->writelocked=true
	 *					unlock(m->wait_lock)
	 *
	 *					up_write()
	 *					sem->writelocked=false
	 *					rtmutex_unlock(m)
	 *			down_read()
	 *					down_write()
	 *					rtmutex_lock(m)
	 *					wait()
	 * rtmutex_lock(m)
	 *
	 * That would put Reader1 behind the writer waiting on
	 * Reader2 to call up_read(), which might be unbound.
	 */

	trace_contention_begin(rwb, LCB_F_RT | LCB_F_READ);

	/*
	 * For rwlocks this returns 0 unconditionally, so the below
	 * !ret conditionals are optimized out.
	 */
	ret = rwbase_rtmutex_slowlock_locked(rtm, state);

	/*
	 * On success the rtmutex is held, so there can't be a writer
	 * active. Increment the reader count and immediately drop the
	 * rtmutex again.
	 *
	 * rtmutex->wait_lock has to be unlocked in any case of course.
	 */
	if (!ret)
		atomic_inc(&rwb->readers);
	raw_spin_unlock_irq(&rtm->wait_lock);
	if (!ret)
		rwbase_rtmutex_unlock(rtm);

	trace_contention_end(rwb, ret);
	rwbase_post_schedule();
	return ret;
}

static __always_inline int rwbase_read_lock(struct rwbase_rt *rwb,
					    unsigned int state)
{
	lockdep_assert(!current->pi_blocked_on);

	if (rwbase_read_trylock(rwb))
		return 0;

	return __rwbase_read_lock(rwb, state);
}

static void __sched __rwbase_read_unlock(struct rwbase_rt *rwb,
					 unsigned int state)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	struct task_struct *owner;
	DEFINE_RT_WAKE_Q(wqh);

	raw_spin_lock_irq(&rtm->wait_lock);
	/*
	 * Wake the writer, i.e. the rtmutex owner. It might release the
	 * rtmutex concurrently in the fast path (due to a signal), but to
	 * clean up rwb->readers it needs to acquire rtm->wait_lock. The
	 * worst case which can happen is a spurious wakeup.
	 */
	owner = rt_mutex_owner(rtm);
	if (owner)
		rt_mutex_wake_q_add_task(&wqh, owner, state);

	/* Pairs with the preempt_enable in rt_mutex_wake_up_q() */
	preempt_disable();
	raw_spin_unlock_irq(&rtm->wait_lock);
	rt_mutex_wake_up_q(&wqh);
}

static __always_inline void rwbase_read_unlock(struct rwbase_rt *rwb,
					       unsigned int state)
{
	/*
	 * rwb->readers can only hit 0 when a writer is waiting for the
	 * active readers to leave the critical section.
	 *
	 * dec_and_test() is fully ordered, provides RELEASE.
	 */
	if (unlikely(atomic_dec_and_test(&rwb->readers)))
		__rwbase_read_unlock(rwb, state);
}

static inline void __rwbase_write_unlock(struct rwbase_rt *rwb, int bias,
					 unsigned long flags)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;

	/*
	 * _release() is needed in case that reader is in fast path, pairing
	 * with atomic_try_cmpxchg_acquire() in rwbase_read_trylock().
	 */
	(void)atomic_add_return_release(READER_BIAS - bias, &rwb->readers);
	raw_spin_unlock_irqrestore(&rtm->wait_lock, flags);
	rwbase_rtmutex_unlock(rtm);
}

static inline void rwbase_write_unlock(struct rwbase_rt *rwb)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	unsigned long flags;

	raw_spin_lock_irqsave(&rtm->wait_lock, flags);
	__rwbase_write_unlock(rwb, WRITER_BIAS, flags);
}

static inline void rwbase_write_downgrade(struct rwbase_rt *rwb)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	unsigned long flags;

	raw_spin_lock_irqsave(&rtm->wait_lock, flags);
	/* Release it and account current as reader */
	__rwbase_write_unlock(rwb, WRITER_BIAS - 1, flags);
}

static inline bool __rwbase_write_trylock(struct rwbase_rt *rwb)
{
	/* Can do without CAS because we're serialized by wait_lock. */
	lockdep_assert_held(&rwb->rtmutex.wait_lock);

	/*
	 * _acquire is needed in case the reader is in the fast path, pairing
	 * with rwbase_read_unlock(), provides ACQUIRE.
	 */
	if (!atomic_read_acquire(&rwb->readers)) {
		atomic_set(&rwb->readers, WRITER_BIAS);
		return 1;
	}

	return 0;
}

static int __sched rwbase_write_lock(struct rwbase_rt *rwb,
				     unsigned int state)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	unsigned long flags;

	/* Take the rtmutex as a first step */
	if (rwbase_rtmutex_lock_state(rtm, state))
		return -EINTR;

	/* Force readers into slow path */
	atomic_sub(READER_BIAS, &rwb->readers);

	rwbase_pre_schedule();

	raw_spin_lock_irqsave(&rtm->wait_lock, flags);
	if (__rwbase_write_trylock(rwb))
		goto out_unlock;

	rwbase_set_and_save_current_state(state);
	trace_contention_begin(rwb, LCB_F_RT | LCB_F_WRITE);
	for (;;) {
		/* Optimized out for rwlocks */
		if (rwbase_signal_pending_state(state, current)) {
			rwbase_restore_current_state();
			__rwbase_write_unlock(rwb, 0, flags);
			rwbase_post_schedule();
			trace_contention_end(rwb, -EINTR);
			return -EINTR;
		}

		if (__rwbase_write_trylock(rwb))
			break;

		raw_spin_unlock_irqrestore(&rtm->wait_lock, flags);
		rwbase_schedule();
		raw_spin_lock_irqsave(&rtm->wait_lock, flags);

		set_current_state(state);
	}
	rwbase_restore_current_state();
	trace_contention_end(rwb, 0);

out_unlock:
	raw_spin_unlock_irqrestore(&rtm->wait_lock, flags);
	rwbase_post_schedule();
	return 0;
}

static inline int rwbase_write_trylock(struct rwbase_rt *rwb)
{
	struct rt_mutex_base *rtm = &rwb->rtmutex;
	unsigned long flags;

	if (!rwbase_rtmutex_trylock(rtm))
		return 0;

	atomic_sub(READER_BIAS, &rwb->readers);

	raw_spin_lock_irqsave(&rtm->wait_lock, flags);
	if (__rwbase_write_trylock(rwb)) {
		raw_spin_unlock_irqrestore(&rtm->wait_lock, flags);
		return 1;
	}
	__rwbase_write_unlock(rwb, 0, flags);
	return 0;
}
