/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resilient Queued Spin Lock
 *
 * (C) Copyright 2024-2025 Meta Platforms, Inc. and affiliates.
 *
 * Authors: Kumar Kartikeya Dwivedi <memxor@gmail.com>
 */
#ifndef __ASM_GENERIC_RQSPINLOCK_H
#define __ASM_GENERIC_RQSPINLOCK_H

#include <linux/types.h>
#include <vdso/time64.h>
#include <linux/percpu.h>
#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm/qspinlock.h>
#endif

struct rqspinlock {
	union {
		atomic_t val;
		u32 locked;
	};
};

/* Even though this is same as struct rqspinlock, we need to emit a distinct
 * type in BTF for BPF programs.
 */
struct bpf_res_spin_lock {
	u32 val;
};

struct qspinlock;
#ifdef CONFIG_QUEUED_SPINLOCKS
typedef struct qspinlock rqspinlock_t;
#else
typedef struct rqspinlock rqspinlock_t;
#endif

extern int resilient_tas_spin_lock(rqspinlock_t *lock);
#ifdef CONFIG_QUEUED_SPINLOCKS
extern int resilient_queued_spin_lock_slowpath(rqspinlock_t *lock, u32 val);
#endif

#ifndef resilient_virt_spin_lock_enabled
static __always_inline bool resilient_virt_spin_lock_enabled(void)
{
	return false;
}
#endif

#ifndef resilient_virt_spin_lock
static __always_inline int resilient_virt_spin_lock(rqspinlock_t *lock)
{
	return 0;
}
#endif

/*
 * Default timeout for waiting loops is 0.25 seconds
 */
#define RES_DEF_TIMEOUT (NSEC_PER_SEC / 4)

/*
 * Choose 31 as it makes rqspinlock_held cacheline-aligned.
 */
#define RES_NR_HELD 31

struct rqspinlock_held {
	int cnt;
	void *locks[RES_NR_HELD];
};

DECLARE_PER_CPU_ALIGNED(struct rqspinlock_held, rqspinlock_held_locks);

static __always_inline void grab_held_lock_entry(void *lock)
{
	int cnt = this_cpu_inc_return(rqspinlock_held_locks.cnt);

	if (unlikely(cnt > RES_NR_HELD)) {
		/* Still keep the inc so we decrement later. */
		return;
	}

	/*
	 * Implied compiler barrier in per-CPU operations; otherwise we can have
	 * the compiler reorder inc with write to table, allowing interrupts to
	 * overwrite and erase our write to the table (as on interrupt exit it
	 * will be reset to NULL).
	 *
	 * It is fine for cnt inc to be reordered wrt remote readers though,
	 * they won't observe our entry until the cnt update is visible, that's
	 * all.
	 */
	this_cpu_write(rqspinlock_held_locks.locks[cnt - 1], lock);
}

/*
 * We simply don't support out-of-order unlocks, and keep the logic simple here.
 * The verifier prevents BPF programs from unlocking out-of-order, and the same
 * holds for in-kernel users.
 *
 * It is possible to run into misdetection scenarios of AA deadlocks on the same
 * CPU, and missed ABBA deadlocks on remote CPUs if this function pops entries
 * out of order (due to lock A, lock B, unlock A, unlock B) pattern. The correct
 * logic to preserve right entries in the table would be to walk the array of
 * held locks and swap and clear out-of-order entries, but that's too
 * complicated and we don't have a compelling use case for out of order unlocking.
 */
static __always_inline void release_held_lock_entry(void)
{
	struct rqspinlock_held *rqh = this_cpu_ptr(&rqspinlock_held_locks);

	if (unlikely(rqh->cnt > RES_NR_HELD))
		goto dec;
	WRITE_ONCE(rqh->locks[rqh->cnt - 1], NULL);
dec:
	/*
	 * Reordering of clearing above with inc and its write in
	 * grab_held_lock_entry that came before us (in same acquisition
	 * attempt) is ok, we either see a valid entry or NULL when it's
	 * visible.
	 *
	 * But this helper is invoked when we unwind upon failing to acquire the
	 * lock. Unlike the unlock path which constitutes a release store after
	 * we clear the entry, we need to emit a write barrier here. Otherwise,
	 * we may have a situation as follows:
	 *
	 * <error> for lock B
	 * release_held_lock_entry
	 *
	 * try_cmpxchg_acquire for lock A
	 * grab_held_lock_entry
	 *
	 * Lack of any ordering means reordering may occur such that dec, inc
	 * are done before entry is overwritten. This permits a remote lock
	 * holder of lock B (which this CPU failed to acquire) to now observe it
	 * as being attempted on this CPU, and may lead to misdetection (if this
	 * CPU holds a lock it is attempting to acquire, leading to false ABBA
	 * diagnosis).
	 *
	 * In case of unlock, we will always do a release on the lock word after
	 * releasing the entry, ensuring that other CPUs cannot hold the lock
	 * (and make conclusions about deadlocks) until the entry has been
	 * cleared on the local CPU, preventing any anomalies. Reordering is
	 * still possible there, but a remote CPU cannot observe a lock in our
	 * table which it is already holding, since visibility entails our
	 * release store for the said lock has not retired.
	 *
	 * In theory we don't have a problem if the dec and WRITE_ONCE above get
	 * reordered with each other, we either notice an empty NULL entry on
	 * top (if dec succeeds WRITE_ONCE), or a potentially stale entry which
	 * cannot be observed (if dec precedes WRITE_ONCE).
	 *
	 * Emit the write barrier _before_ the dec, this permits dec-inc
	 * reordering but that is harmless as we'd have new entry set to NULL
	 * already, i.e. they cannot precede the NULL store above.
	 */
	smp_wmb();
	this_cpu_dec(rqspinlock_held_locks.cnt);
}

#ifdef CONFIG_QUEUED_SPINLOCKS

/**
 * res_spin_lock - acquire a queued spinlock
 * @lock: Pointer to queued spinlock structure
 *
 * Return:
 * * 0		- Lock was acquired successfully.
 * * -EDEADLK	- Lock acquisition failed because of AA/ABBA deadlock.
 * * -ETIMEDOUT - Lock acquisition failed because of timeout.
 */
static __always_inline int res_spin_lock(rqspinlock_t *lock)
{
	int val = 0;

	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL))) {
		grab_held_lock_entry(lock);
		return 0;
	}
	return resilient_queued_spin_lock_slowpath(lock, val);
}

#else

#define res_spin_lock(lock) resilient_tas_spin_lock(lock)

#endif /* CONFIG_QUEUED_SPINLOCKS */

static __always_inline void res_spin_unlock(rqspinlock_t *lock)
{
	struct rqspinlock_held *rqh = this_cpu_ptr(&rqspinlock_held_locks);

	if (unlikely(rqh->cnt > RES_NR_HELD))
		goto unlock;
	WRITE_ONCE(rqh->locks[rqh->cnt - 1], NULL);
unlock:
	/*
	 * Release barrier, ensures correct ordering. See release_held_lock_entry
	 * for details.  Perform release store instead of queued_spin_unlock,
	 * since we use this function for test-and-set fallback as well. When we
	 * have CONFIG_QUEUED_SPINLOCKS=n, we clear the full 4-byte lockword.
	 *
	 * Like release_held_lock_entry, we can do the release before the dec.
	 * We simply care about not seeing the 'lock' in our table from a remote
	 * CPU once the lock has been released, which doesn't rely on the dec.
	 *
	 * Unlike smp_wmb(), release is not a two way fence, hence it is
	 * possible for a inc to move up and reorder with our clearing of the
	 * entry. This isn't a problem however, as for a misdiagnosis of ABBA,
	 * the remote CPU needs to hold this lock, which won't be released until
	 * the store below is done, which would ensure the entry is overwritten
	 * to NULL, etc.
	 */
	smp_store_release(&lock->locked, 0);
	this_cpu_dec(rqspinlock_held_locks.cnt);
}

#ifdef CONFIG_QUEUED_SPINLOCKS
#define raw_res_spin_lock_init(lock) ({ *(lock) = (rqspinlock_t)__ARCH_SPIN_LOCK_UNLOCKED; })
#else
#define raw_res_spin_lock_init(lock) ({ *(lock) = (rqspinlock_t){0}; })
#endif

#define raw_res_spin_lock(lock)                    \
	({                                         \
		int __ret;                         \
		preempt_disable();                 \
		__ret = res_spin_lock(lock);	   \
		if (__ret)                         \
			preempt_enable();          \
		__ret;                             \
	})

#define raw_res_spin_unlock(lock) ({ res_spin_unlock(lock); preempt_enable(); })

#define raw_res_spin_lock_irqsave(lock, flags)    \
	({                                        \
		int __ret;                        \
		local_irq_save(flags);            \
		__ret = raw_res_spin_lock(lock);  \
		if (__ret)                        \
			local_irq_restore(flags); \
		__ret;                            \
	})

#define raw_res_spin_unlock_irqrestore(lock, flags) ({ raw_res_spin_unlock(lock); local_irq_restore(flags); })

#endif /* __ASM_GENERIC_RQSPINLOCK_H */
