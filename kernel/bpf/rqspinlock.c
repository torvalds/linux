// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Resilient Queued Spin Lock
 *
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 * (C) Copyright 2013-2014,2018 Red Hat, Inc.
 * (C) Copyright 2015 Intel Corp.
 * (C) Copyright 2015 Hewlett-Packard Enterprise Development LP
 * (C) Copyright 2024-2025 Meta Platforms, Inc. and affiliates.
 *
 * Authors: Waiman Long <longman@redhat.com>
 *          Peter Zijlstra <peterz@infradead.org>
 *          Kumar Kartikeya Dwivedi <memxor@gmail.com>
 */

#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/prefetch.h>
#include <asm/byteorder.h>
#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm/qspinlock.h>
#endif
#include <trace/events/lock.h>
#include <asm/rqspinlock.h>
#include <linux/timekeeping.h>

/*
 * Include queued spinlock definitions and statistics code
 */
#ifdef CONFIG_QUEUED_SPINLOCKS
#include "../locking/qspinlock.h"
#include "../locking/lock_events.h"
#include "rqspinlock.h"
#include "../locking/mcs_spinlock.h"
#endif

/*
 * The basic principle of a queue-based spinlock can best be understood
 * by studying a classic queue-based spinlock implementation called the
 * MCS lock. A copy of the original MCS lock paper ("Algorithms for Scalable
 * Synchronization on Shared-Memory Multiprocessors by Mellor-Crummey and
 * Scott") is available at
 *
 * https://bugzilla.kernel.org/show_bug.cgi?id=206115
 *
 * This queued spinlock implementation is based on the MCS lock, however to
 * make it fit the 4 bytes we assume spinlock_t to be, and preserve its
 * existing API, we must modify it somehow.
 *
 * In particular; where the traditional MCS lock consists of a tail pointer
 * (8 bytes) and needs the next pointer (another 8 bytes) of its own node to
 * unlock the next pending (next->locked), we compress both these: {tail,
 * next->locked} into a single u32 value.
 *
 * Since a spinlock disables recursion of its own context and there is a limit
 * to the contexts that can nest; namely: task, softirq, hardirq, nmi. As there
 * are at most 4 nesting levels, it can be encoded by a 2-bit number. Now
 * we can encode the tail by combining the 2-bit nesting level with the cpu
 * number. With one byte for the lock value and 3 bytes for the tail, only a
 * 32-bit word is now needed. Even though we only need 1 bit for the lock,
 * we extend it to a full byte to achieve better performance for architectures
 * that support atomic byte write.
 *
 * We also change the first spinner to spin on the lock bit instead of its
 * node; whereby avoiding the need to carry a node from lock to unlock, and
 * preserving existing lock API. This also makes the unlock code simpler and
 * faster.
 *
 * N.B. The current implementation only supports architectures that allow
 *      atomic operations on smaller 8-bit and 16-bit data types.
 *
 */

struct rqspinlock_timeout {
	u64 timeout_end;
	u64 duration;
	u64 cur;
	u16 spin;
};

#define RES_TIMEOUT_VAL	2

DEFINE_PER_CPU_ALIGNED(struct rqspinlock_held, rqspinlock_held_locks);
EXPORT_SYMBOL_GPL(rqspinlock_held_locks);

static bool is_lock_released(rqspinlock_t *lock, u32 mask, struct rqspinlock_timeout *ts)
{
	if (!(atomic_read_acquire(&lock->val) & (mask)))
		return true;
	return false;
}

static noinline int check_deadlock_AA(rqspinlock_t *lock, u32 mask,
				      struct rqspinlock_timeout *ts)
{
	struct rqspinlock_held *rqh = this_cpu_ptr(&rqspinlock_held_locks);
	int cnt = min(RES_NR_HELD, rqh->cnt);

	/*
	 * Return an error if we hold the lock we are attempting to acquire.
	 * We'll iterate over max 32 locks; no need to do is_lock_released.
	 */
	for (int i = 0; i < cnt - 1; i++) {
		if (rqh->locks[i] == lock)
			return -EDEADLK;
	}
	return 0;
}

/*
 * This focuses on the most common case of ABBA deadlocks (or ABBA involving
 * more locks, which reduce to ABBA). This is not exhaustive, and we rely on
 * timeouts as the final line of defense.
 */
static noinline int check_deadlock_ABBA(rqspinlock_t *lock, u32 mask,
					struct rqspinlock_timeout *ts)
{
	struct rqspinlock_held *rqh = this_cpu_ptr(&rqspinlock_held_locks);
	int rqh_cnt = min(RES_NR_HELD, rqh->cnt);
	void *remote_lock;
	int cpu;

	/*
	 * Find the CPU holding the lock that we want to acquire. If there is a
	 * deadlock scenario, we will read a stable set on the remote CPU and
	 * find the target. This would be a constant time operation instead of
	 * O(NR_CPUS) if we could determine the owning CPU from a lock value, but
	 * that requires increasing the size of the lock word.
	 */
	for_each_possible_cpu(cpu) {
		struct rqspinlock_held *rqh_cpu = per_cpu_ptr(&rqspinlock_held_locks, cpu);
		int real_cnt = READ_ONCE(rqh_cpu->cnt);
		int cnt = min(RES_NR_HELD, real_cnt);

		/*
		 * Let's ensure to break out of this loop if the lock is available for
		 * us to potentially acquire.
		 */
		if (is_lock_released(lock, mask, ts))
			return 0;

		/*
		 * Skip ourselves, and CPUs whose count is less than 2, as they need at
		 * least one held lock and one acquisition attempt (reflected as top
		 * most entry) to participate in an ABBA deadlock.
		 *
		 * If cnt is more than RES_NR_HELD, it means the current lock being
		 * acquired won't appear in the table, and other locks in the table are
		 * already held, so we can't determine ABBA.
		 */
		if (cpu == smp_processor_id() || real_cnt < 2 || real_cnt > RES_NR_HELD)
			continue;

		/*
		 * Obtain the entry at the top, this corresponds to the lock the
		 * remote CPU is attempting to acquire in a deadlock situation,
		 * and would be one of the locks we hold on the current CPU.
		 */
		remote_lock = READ_ONCE(rqh_cpu->locks[cnt - 1]);
		/*
		 * If it is NULL, we've raced and cannot determine a deadlock
		 * conclusively, skip this CPU.
		 */
		if (!remote_lock)
			continue;
		/*
		 * Find if the lock we're attempting to acquire is held by this CPU.
		 * Don't consider the topmost entry, as that must be the latest lock
		 * being held or acquired.  For a deadlock, the target CPU must also
		 * attempt to acquire a lock we hold, so for this search only 'cnt - 1'
		 * entries are important.
		 */
		for (int i = 0; i < cnt - 1; i++) {
			if (READ_ONCE(rqh_cpu->locks[i]) != lock)
				continue;
			/*
			 * We found our lock as held on the remote CPU.  Is the
			 * acquisition attempt on the remote CPU for a lock held
			 * by us?  If so, we have a deadlock situation, and need
			 * to recover.
			 */
			for (int i = 0; i < rqh_cnt - 1; i++) {
				if (rqh->locks[i] == remote_lock)
					return -EDEADLK;
			}
			/*
			 * Inconclusive; retry again later.
			 */
			return 0;
		}
	}
	return 0;
}

static noinline int check_deadlock(rqspinlock_t *lock, u32 mask,
				   struct rqspinlock_timeout *ts)
{
	int ret;

	ret = check_deadlock_AA(lock, mask, ts);
	if (ret)
		return ret;
	ret = check_deadlock_ABBA(lock, mask, ts);
	if (ret)
		return ret;

	return 0;
}

static noinline int check_timeout(rqspinlock_t *lock, u32 mask,
				  struct rqspinlock_timeout *ts)
{
	u64 time = ktime_get_mono_fast_ns();
	u64 prev = ts->cur;

	if (!ts->timeout_end) {
		ts->cur = time;
		ts->timeout_end = time + ts->duration;
		return 0;
	}

	if (time > ts->timeout_end)
		return -ETIMEDOUT;

	/*
	 * A millisecond interval passed from last time? Trigger deadlock
	 * checks.
	 */
	if (prev + NSEC_PER_MSEC < time) {
		ts->cur = time;
		return check_deadlock(lock, mask, ts);
	}

	return 0;
}

/*
 * Do not amortize with spins when res_smp_cond_load_acquire is defined,
 * as the macro does internal amortization for us.
 */
#ifndef res_smp_cond_load_acquire
#define RES_CHECK_TIMEOUT(ts, ret, mask)                              \
	({                                                            \
		if (!(ts).spin++)                                     \
			(ret) = check_timeout((lock), (mask), &(ts)); \
		(ret);                                                \
	})
#else
#define RES_CHECK_TIMEOUT(ts, ret, mask)			      \
	({ (ret) = check_timeout((lock), (mask), &(ts)); })
#endif

/*
 * Initialize the 'spin' member.
 * Set spin member to 0 to trigger AA/ABBA checks immediately.
 */
#define RES_INIT_TIMEOUT(ts) ({ (ts).spin = 0; })

/*
 * We only need to reset 'timeout_end', 'spin' will just wrap around as necessary.
 * Duration is defined for each spin attempt, so set it here.
 */
#define RES_RESET_TIMEOUT(ts, _duration) ({ (ts).timeout_end = 0; (ts).duration = _duration; })

/*
 * Provide a test-and-set fallback for cases when queued spin lock support is
 * absent from the architecture.
 */
int __lockfunc resilient_tas_spin_lock(rqspinlock_t *lock)
{
	struct rqspinlock_timeout ts;
	int val, ret = 0;

	RES_INIT_TIMEOUT(ts);
	grab_held_lock_entry(lock);

	/*
	 * Since the waiting loop's time is dependent on the amount of
	 * contention, a short timeout unlike rqspinlock waiting loops
	 * isn't enough. Choose a second as the timeout value.
	 */
	RES_RESET_TIMEOUT(ts, NSEC_PER_SEC);
retry:
	val = atomic_read(&lock->val);

	if (val || !atomic_try_cmpxchg(&lock->val, &val, 1)) {
		if (RES_CHECK_TIMEOUT(ts, ret, ~0u))
			goto out;
		cpu_relax();
		goto retry;
	}

	return 0;
out:
	release_held_lock_entry();
	return ret;
}
EXPORT_SYMBOL_GPL(resilient_tas_spin_lock);

#ifdef CONFIG_QUEUED_SPINLOCKS

/*
 * Per-CPU queue node structures; we can never have more than 4 nested
 * contexts: task, softirq, hardirq, nmi.
 *
 * Exactly fits one 64-byte cacheline on a 64-bit architecture.
 */
static DEFINE_PER_CPU_ALIGNED(struct qnode, rqnodes[_Q_MAX_NODES]);

#ifndef res_smp_cond_load_acquire
#define res_smp_cond_load_acquire(v, c) smp_cond_load_acquire(v, c)
#endif

#define res_atomic_cond_read_acquire(v, c) res_smp_cond_load_acquire(&(v)->counter, (c))

/**
 * resilient_queued_spin_lock_slowpath - acquire the queued spinlock
 * @lock: Pointer to queued spinlock structure
 * @val: Current value of the queued spinlock 32-bit word
 *
 * Return:
 * * 0		- Lock was acquired successfully.
 * * -EDEADLK	- Lock acquisition failed because of AA/ABBA deadlock.
 * * -ETIMEDOUT - Lock acquisition failed because of timeout.
 *
 * (queue tail, pending bit, lock value)
 *
 *              fast     :    slow                                  :    unlock
 *                       :                                          :
 * uncontended  (0,0,0) -:--> (0,0,1) ------------------------------:--> (*,*,0)
 *                       :       | ^--------.------.             /  :
 *                       :       v           \      \            |  :
 * pending               :    (0,1,1) +--> (0,1,0)   \           |  :
 *                       :       | ^--'              |           |  :
 *                       :       v                   |           |  :
 * uncontended           :    (n,x,y) +--> (n,0,0) --'           |  :
 *   queue               :       | ^--'                          |  :
 *                       :       v                               |  :
 * contended             :    (*,x,y) +--> (*,0,0) ---> (*,0,1) -'  :
 *   queue               :         ^--'                             :
 */
int __lockfunc resilient_queued_spin_lock_slowpath(rqspinlock_t *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	struct rqspinlock_timeout ts;
	int idx, ret = 0;
	u32 old, tail;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	if (resilient_virt_spin_lock_enabled())
		return resilient_virt_spin_lock(lock);

	RES_INIT_TIMEOUT(ts);

	/*
	 * Wait for in-progress pending->locked hand-overs with a bounded
	 * number of spins so that we guarantee forward progress.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL) {
		int cnt = _Q_PENDING_LOOPS;
		val = atomic_cond_read_relaxed(&lock->val,
					       (VAL != _Q_PENDING_VAL) || !cnt--);
	}

	/*
	 * If we observe any contention; queue.
	 */
	if (val & ~_Q_LOCKED_MASK)
		goto queue;

	/*
	 * trylock || pending
	 *
	 * 0,0,* -> 0,1,* -> 0,0,1 pending, trylock
	 */
	val = queued_fetch_set_pending_acquire(lock);

	/*
	 * If we observe contention, there is a concurrent locker.
	 *
	 * Undo and queue; our setting of PENDING might have made the
	 * n,0,0 -> 0,0,0 transition fail and it will now be waiting
	 * on @next to become !NULL.
	 */
	if (unlikely(val & ~_Q_LOCKED_MASK)) {

		/* Undo PENDING if we set it. */
		if (!(val & _Q_PENDING_MASK))
			clear_pending(lock);

		goto queue;
	}

	/*
	 * Grab an entry in the held locks array, to enable deadlock detection.
	 */
	grab_held_lock_entry(lock);

	/*
	 * We're pending, wait for the owner to go away.
	 *
	 * 0,1,1 -> *,1,0
	 *
	 * this wait loop must be a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because not all
	 * clear_pending_set_locked() implementations imply full
	 * barriers.
	 */
	if (val & _Q_LOCKED_MASK) {
		RES_RESET_TIMEOUT(ts, RES_DEF_TIMEOUT);
		res_smp_cond_load_acquire(&lock->locked, !VAL || RES_CHECK_TIMEOUT(ts, ret, _Q_LOCKED_MASK));
	}

	if (ret) {
		/*
		 * We waited for the locked bit to go back to 0, as the pending
		 * waiter, but timed out. We need to clear the pending bit since
		 * we own it. Once a stuck owner has been recovered, the lock
		 * must be restored to a valid state, hence removing the pending
		 * bit is necessary.
		 *
		 * *,1,* -> *,0,*
		 */
		clear_pending(lock);
		lockevent_inc(rqspinlock_lock_timeout);
		goto err_release_entry;
	}

	/*
	 * take ownership and clear the pending bit.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	clear_pending_set_locked(lock);
	lockevent_inc(lock_pending);
	return 0;

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	lockevent_inc(lock_slowpath);
	/*
	 * Grab deadlock detection entry for the queue path.
	 */
	grab_held_lock_entry(lock);

	node = this_cpu_ptr(&rqnodes[0].mcs);
	idx = node->count++;
	tail = encode_tail(smp_processor_id(), idx);

	trace_contention_begin(lock, LCB_F_SPIN);

	/*
	 * 4 nodes are allocated based on the assumption that there will
	 * not be nested NMIs taking spinlocks. That may not be true in
	 * some architectures even though the chance of needing more than
	 * 4 nodes will still be extremely unlikely. When that happens,
	 * we fall back to spinning on the lock directly without using
	 * any MCS node. This is not the most elegant solution, but is
	 * simple enough.
	 */
	if (unlikely(idx >= _Q_MAX_NODES)) {
		lockevent_inc(lock_no_node);
		RES_RESET_TIMEOUT(ts, RES_DEF_TIMEOUT);
		while (!queued_spin_trylock(lock)) {
			if (RES_CHECK_TIMEOUT(ts, ret, ~0u)) {
				lockevent_inc(rqspinlock_lock_timeout);
				goto err_release_node;
			}
			cpu_relax();
		}
		goto release;
	}

	node = grab_mcs_node(node, idx);

	/*
	 * Keep counts of non-zero index values:
	 */
	lockevent_cond_inc(lock_use_node2 + idx - 1, idx);

	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
	barrier();

	node->locked = 0;
	node->next = NULL;

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * Ensure that the initialisation of @node is complete before we
	 * publish the updated tail via xchg_tail() and potentially link
	 * @node into the waitqueue via WRITE_ONCE(prev->next, node) below.
	 */
	smp_wmb();

	/*
	 * Publish the updated tail.
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) {
		int val;

		prev = decode_tail(old, rqnodes);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		val = arch_mcs_spin_lock_contended(&node->locked);
		if (val == RES_TIMEOUT_VAL) {
			ret = -EDEADLK;
			goto waitq_timeout;
		}

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * We use RES_DEF_TIMEOUT * 2 as the duration, as RES_DEF_TIMEOUT is
	 * meant to span maximum allowed time per critical section, and we may
	 * have both the owner of the lock and the pending bit waiter ahead of
	 * us.
	 */
	RES_RESET_TIMEOUT(ts, RES_DEF_TIMEOUT * 2);
	val = res_atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK) ||
					   RES_CHECK_TIMEOUT(ts, ret, _Q_LOCKED_PENDING_MASK));

waitq_timeout:
	if (ret) {
		/*
		 * If the tail is still pointing to us, then we are the final waiter,
		 * and are responsible for resetting the tail back to 0. Otherwise, if
		 * the cmpxchg operation fails, we signal the next waiter to take exit
		 * and try the same. For a waiter with tail node 'n':
		 *
		 * n,*,* -> 0,*,*
		 *
		 * When performing cmpxchg for the whole word (NR_CPUS > 16k), it is
		 * possible locked/pending bits keep changing and we see failures even
		 * when we remain the head of wait queue. However, eventually,
		 * pending bit owner will unset the pending bit, and new waiters
		 * will queue behind us. This will leave the lock owner in
		 * charge, and it will eventually either set locked bit to 0, or
		 * leave it as 1, allowing us to make progress.
		 *
		 * We terminate the whole wait queue for two reasons. Firstly,
		 * we eschew per-waiter timeouts with one applied at the head of
		 * the wait queue.  This allows everyone to break out faster
		 * once we've seen the owner / pending waiter not responding for
		 * the timeout duration from the head.  Secondly, it avoids
		 * complicated synchronization, because when not leaving in FIFO
		 * order, prev's next pointer needs to be fixed up etc.
		 */
		if (!try_cmpxchg_tail(lock, tail, 0)) {
			next = smp_cond_load_relaxed(&node->next, VAL);
			WRITE_ONCE(next->locked, RES_TIMEOUT_VAL);
		}
		lockevent_inc(rqspinlock_lock_timeout);
		goto err_release_node;
	}

	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,*,0 -> *,*,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail)
	 * and nobody is pending, clear the tail code and grab the lock.
	 * Otherwise, we only need to grab the lock.
	 */

	/*
	 * Note: at this point: (val & _Q_PENDING_MASK) == 0, because of the
	 *       above wait condition, therefore any concurrent setting of
	 *       PENDING will make the uncontended transition fail.
	 */
	if ((val & _Q_TAIL_MASK) == tail) {
		if (atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL))
			goto release; /* No contention */
	}

	/*
	 * Either somebody is queued behind us or _Q_PENDING_VAL got set
	 * which will then detect the remaining tail and queue behind us
	 * ensuring we'll see a @next.
	 */
	set_locked(lock);

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));

	arch_mcs_spin_unlock_contended(&next->locked);

release:
	trace_contention_end(lock, 0);

	/*
	 * release the node
	 */
	__this_cpu_dec(rqnodes[0].mcs.count);
	return ret;
err_release_node:
	trace_contention_end(lock, ret);
	__this_cpu_dec(rqnodes[0].mcs.count);
err_release_entry:
	release_held_lock_entry();
	return ret;
}
EXPORT_SYMBOL_GPL(resilient_queued_spin_lock_slowpath);

#endif /* CONFIG_QUEUED_SPINLOCKS */

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_res_spin_lock(struct bpf_res_spin_lock *lock)
{
	int ret;

	BUILD_BUG_ON(sizeof(rqspinlock_t) != sizeof(struct bpf_res_spin_lock));
	BUILD_BUG_ON(__alignof__(rqspinlock_t) != __alignof__(struct bpf_res_spin_lock));

	preempt_disable();
	ret = res_spin_lock((rqspinlock_t *)lock);
	if (unlikely(ret)) {
		preempt_enable();
		return ret;
	}
	return 0;
}

__bpf_kfunc void bpf_res_spin_unlock(struct bpf_res_spin_lock *lock)
{
	res_spin_unlock((rqspinlock_t *)lock);
	preempt_enable();
}

__bpf_kfunc int bpf_res_spin_lock_irqsave(struct bpf_res_spin_lock *lock, unsigned long *flags__irq_flag)
{
	u64 *ptr = (u64 *)flags__irq_flag;
	unsigned long flags;
	int ret;

	preempt_disable();
	local_irq_save(flags);
	ret = res_spin_lock((rqspinlock_t *)lock);
	if (unlikely(ret)) {
		local_irq_restore(flags);
		preempt_enable();
		return ret;
	}
	*ptr = flags;
	return 0;
}

__bpf_kfunc void bpf_res_spin_unlock_irqrestore(struct bpf_res_spin_lock *lock, unsigned long *flags__irq_flag)
{
	u64 *ptr = (u64 *)flags__irq_flag;
	unsigned long flags = *ptr;

	res_spin_unlock((rqspinlock_t *)lock);
	local_irq_restore(flags);
	preempt_enable();
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(rqspinlock_kfunc_ids)
BTF_ID_FLAGS(func, bpf_res_spin_lock, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_res_spin_unlock)
BTF_ID_FLAGS(func, bpf_res_spin_lock_irqsave, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_res_spin_unlock_irqrestore)
BTF_KFUNCS_END(rqspinlock_kfunc_ids)

static const struct btf_kfunc_id_set rqspinlock_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &rqspinlock_kfunc_ids,
};

static __init int rqspinlock_register_kfuncs(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &rqspinlock_kfunc_set);
}
late_initcall(rqspinlock_register_kfuncs);
