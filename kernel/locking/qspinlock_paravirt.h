/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GEN_PV_LOCK_SLOWPATH
#error "do not include this file"
#endif

#include <linux/hash.h>
#include <linux/bootmem.h>
#include <linux/debug_locks.h>

/*
 * Implement paravirt qspinlocks; the general idea is to halt the vcpus instead
 * of spinning them.
 *
 * This relies on the architecture to provide two paravirt hypercalls:
 *
 *   pv_wait(u8 *ptr, u8 val) -- suspends the vcpu if *ptr == val
 *   pv_kick(cpu)             -- wakes a suspended vcpu
 *
 * Using these we implement __pv_queued_spin_lock_slowpath() and
 * __pv_queued_spin_unlock() to replace native_queued_spin_lock_slowpath() and
 * native_queued_spin_unlock().
 */

#define _Q_SLOW_VAL	(3U << _Q_LOCKED_OFFSET)

/*
 * Queue Node Adaptive Spinning
 *
 * A queue node vCPU will stop spinning if the vCPU in the previous node is
 * not running. The one lock stealing attempt allowed at slowpath entry
 * mitigates the slight slowdown for non-overcommitted guest with this
 * aggressive wait-early mechanism.
 *
 * The status of the previous node will be checked at fixed interval
 * controlled by PV_PREV_CHECK_MASK. This is to ensure that we won't
 * pound on the cacheline of the previous node too heavily.
 */
#define PV_PREV_CHECK_MASK	0xff

/*
 * Queue node uses: vcpu_running & vcpu_halted.
 * Queue head uses: vcpu_running & vcpu_hashed.
 */
enum vcpu_state {
	vcpu_running = 0,
	vcpu_halted,		/* Used only in pv_wait_node */
	vcpu_hashed,		/* = pv_hash'ed + vcpu_halted */
};

struct pv_node {
	struct mcs_spinlock	mcs;
	struct mcs_spinlock	__res[3];

	int			cpu;
	u8			state;
};

/*
 * Include queued spinlock statistics code
 */
#include "qspinlock_stat.h"

/*
 * By replacing the regular queued_spin_trylock() with the function below,
 * it will be called once when a lock waiter enter the PV slowpath before
 * being queued. By allowing one lock stealing attempt here when the pending
 * bit is off, it helps to reduce the performance impact of lock waiter
 * preemption without the drawback of lock starvation.
 */
#define queued_spin_trylock(l)	pv_queued_spin_steal_lock(l)
static inline bool pv_queued_spin_steal_lock(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	if (!(atomic_read(&lock->val) & _Q_LOCKED_PENDING_MASK) &&
	    (cmpxchg_acquire(&l->locked, 0, _Q_LOCKED_VAL) == 0)) {
		qstat_inc(qstat_pv_lock_stealing, true);
		return true;
	}

	return false;
}

/*
 * The pending bit is used by the queue head vCPU to indicate that it
 * is actively spinning on the lock and no lock stealing is allowed.
 */
#if _Q_PENDING_BITS == 8
static __always_inline void set_pending(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->pending, 1);
}

static __always_inline void clear_pending(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->pending, 0);
}

/*
 * The pending bit check in pv_queued_spin_steal_lock() isn't a memory
 * barrier. Therefore, an atomic cmpxchg_acquire() is used to acquire the
 * lock just to be sure that it will get it.
 */
static __always_inline int trylock_clear_pending(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	return !READ_ONCE(l->locked) &&
	       (cmpxchg_acquire(&l->locked_pending, _Q_PENDING_VAL,
				_Q_LOCKED_VAL) == _Q_PENDING_VAL);
}
#else /* _Q_PENDING_BITS == 8 */
static __always_inline void set_pending(struct qspinlock *lock)
{
	atomic_or(_Q_PENDING_VAL, &lock->val);
}

static __always_inline void clear_pending(struct qspinlock *lock)
{
	atomic_andnot(_Q_PENDING_VAL, &lock->val);
}

static __always_inline int trylock_clear_pending(struct qspinlock *lock)
{
	int val = atomic_read(&lock->val);

	for (;;) {
		int old, new;

		if (val  & _Q_LOCKED_MASK)
			break;

		/*
		 * Try to clear pending bit & set locked bit
		 */
		old = val;
		new = (val & ~_Q_PENDING_MASK) | _Q_LOCKED_VAL;
		val = atomic_cmpxchg_acquire(&lock->val, old, new);

		if (val == old)
			return 1;
	}
	return 0;
}
#endif /* _Q_PENDING_BITS == 8 */

/*
 * Lock and MCS node addresses hash table for fast lookup
 *
 * Hashing is done on a per-cacheline basis to minimize the need to access
 * more than one cacheline.
 *
 * Dynamically allocate a hash table big enough to hold at least 4X the
 * number of possible cpus in the system. Allocation is done on page
 * granularity. So the minimum number of hash buckets should be at least
 * 256 (64-bit) or 512 (32-bit) to fully utilize a 4k page.
 *
 * Since we should not be holding locks from NMI context (very rare indeed) the
 * max load factor is 0.75, which is around the point where open addressing
 * breaks down.
 *
 */
struct pv_hash_entry {
	struct qspinlock *lock;
	struct pv_node   *node;
};

#define PV_HE_PER_LINE	(SMP_CACHE_BYTES / sizeof(struct pv_hash_entry))
#define PV_HE_MIN	(PAGE_SIZE / sizeof(struct pv_hash_entry))

static struct pv_hash_entry *pv_lock_hash;
static unsigned int pv_lock_hash_bits __read_mostly;

/*
 * Allocate memory for the PV qspinlock hash buckets
 *
 * This function should be called from the paravirt spinlock initialization
 * routine.
 */
void __init __pv_init_lock_hash(void)
{
	int pv_hash_size = ALIGN(4 * num_possible_cpus(), PV_HE_PER_LINE);

	if (pv_hash_size < PV_HE_MIN)
		pv_hash_size = PV_HE_MIN;

	/*
	 * Allocate space from bootmem which should be page-size aligned
	 * and hence cacheline aligned.
	 */
	pv_lock_hash = alloc_large_system_hash("PV qspinlock",
					       sizeof(struct pv_hash_entry),
					       pv_hash_size, 0,
					       HASH_EARLY | HASH_ZERO,
					       &pv_lock_hash_bits, NULL,
					       pv_hash_size, pv_hash_size);
}

#define for_each_hash_entry(he, offset, hash)						\
	for (hash &= ~(PV_HE_PER_LINE - 1), he = &pv_lock_hash[hash], offset = 0;	\
	     offset < (1 << pv_lock_hash_bits);						\
	     offset++, he = &pv_lock_hash[(hash + offset) & ((1 << pv_lock_hash_bits) - 1)])

static struct qspinlock **pv_hash(struct qspinlock *lock, struct pv_node *node)
{
	unsigned long offset, hash = hash_ptr(lock, pv_lock_hash_bits);
	struct pv_hash_entry *he;
	int hopcnt = 0;

	for_each_hash_entry(he, offset, hash) {
		hopcnt++;
		if (!cmpxchg(&he->lock, NULL, lock)) {
			WRITE_ONCE(he->node, node);
			qstat_hop(hopcnt);
			return &he->lock;
		}
	}
	/*
	 * Hard assume there is a free entry for us.
	 *
	 * This is guaranteed by ensuring every blocked lock only ever consumes
	 * a single entry, and since we only have 4 nesting levels per CPU
	 * and allocated 4*nr_possible_cpus(), this must be so.
	 *
	 * The single entry is guaranteed by having the lock owner unhash
	 * before it releases.
	 */
	BUG();
}

static struct pv_node *pv_unhash(struct qspinlock *lock)
{
	unsigned long offset, hash = hash_ptr(lock, pv_lock_hash_bits);
	struct pv_hash_entry *he;
	struct pv_node *node;

	for_each_hash_entry(he, offset, hash) {
		if (READ_ONCE(he->lock) == lock) {
			node = READ_ONCE(he->node);
			WRITE_ONCE(he->lock, NULL);
			return node;
		}
	}
	/*
	 * Hard assume we'll find an entry.
	 *
	 * This guarantees a limited lookup time and is itself guaranteed by
	 * having the lock owner do the unhash -- IFF the unlock sees the
	 * SLOW flag, there MUST be a hash entry.
	 */
	BUG();
}

/*
 * Return true if when it is time to check the previous node which is not
 * in a running state.
 */
static inline bool
pv_wait_early(struct pv_node *prev, int loop)
{
	if ((loop & PV_PREV_CHECK_MASK) != 0)
		return false;

	return READ_ONCE(prev->state) != vcpu_running || vcpu_is_preempted(prev->cpu);
}

/*
 * Initialize the PV part of the mcs_spinlock node.
 */
static void pv_init_node(struct mcs_spinlock *node)
{
	struct pv_node *pn = (struct pv_node *)node;

	BUILD_BUG_ON(sizeof(struct pv_node) > 5*sizeof(struct mcs_spinlock));

	pn->cpu = smp_processor_id();
	pn->state = vcpu_running;
}

/*
 * Wait for node->locked to become true, halt the vcpu after a short spin.
 * pv_kick_node() is used to set _Q_SLOW_VAL and fill in hash table on its
 * behalf.
 */
static void pv_wait_node(struct mcs_spinlock *node, struct mcs_spinlock *prev)
{
	struct pv_node *pn = (struct pv_node *)node;
	struct pv_node *pp = (struct pv_node *)prev;
	int loop;
	bool wait_early;

	for (;;) {
		for (wait_early = false, loop = SPIN_THRESHOLD; loop; loop--) {
			if (READ_ONCE(node->locked))
				return;
			if (pv_wait_early(pp, loop)) {
				wait_early = true;
				break;
			}
			cpu_relax();
		}

		/*
		 * Order pn->state vs pn->locked thusly:
		 *
		 * [S] pn->state = vcpu_halted	  [S] next->locked = 1
		 *     MB			      MB
		 * [L] pn->locked		[RmW] pn->state = vcpu_hashed
		 *
		 * Matches the cmpxchg() from pv_kick_node().
		 */
		smp_store_mb(pn->state, vcpu_halted);

		if (!READ_ONCE(node->locked)) {
			qstat_inc(qstat_pv_wait_node, true);
			qstat_inc(qstat_pv_wait_early, wait_early);
			pv_wait(&pn->state, vcpu_halted);
		}

		/*
		 * If pv_kick_node() changed us to vcpu_hashed, retain that
		 * value so that pv_wait_head_or_lock() knows to not also try
		 * to hash this lock.
		 */
		cmpxchg(&pn->state, vcpu_halted, vcpu_running);

		/*
		 * If the locked flag is still not set after wakeup, it is a
		 * spurious wakeup and the vCPU should wait again. However,
		 * there is a pretty high overhead for CPU halting and kicking.
		 * So it is better to spin for a while in the hope that the
		 * MCS lock will be released soon.
		 */
		qstat_inc(qstat_pv_spurious_wakeup, !READ_ONCE(node->locked));
	}

	/*
	 * By now our node->locked should be 1 and our caller will not actually
	 * spin-wait for it. We do however rely on our caller to do a
	 * load-acquire for us.
	 */
}

/*
 * Called after setting next->locked = 1 when we're the lock owner.
 *
 * Instead of waking the waiters stuck in pv_wait_node() advance their state
 * such that they're waiting in pv_wait_head_or_lock(), this avoids a
 * wake/sleep cycle.
 */
static void pv_kick_node(struct qspinlock *lock, struct mcs_spinlock *node)
{
	struct pv_node *pn = (struct pv_node *)node;
	struct __qspinlock *l = (void *)lock;

	/*
	 * If the vCPU is indeed halted, advance its state to match that of
	 * pv_wait_node(). If OTOH this fails, the vCPU was running and will
	 * observe its next->locked value and advance itself.
	 *
	 * Matches with smp_store_mb() and cmpxchg() in pv_wait_node()
	 *
	 * The write to next->locked in arch_mcs_spin_unlock_contended()
	 * must be ordered before the read of pn->state in the cmpxchg()
	 * below for the code to work correctly. To guarantee full ordering
	 * irrespective of the success or failure of the cmpxchg(),
	 * a relaxed version with explicit barrier is used. The control
	 * dependency will order the reading of pn->state before any
	 * subsequent writes.
	 */
	smp_mb__before_atomic();
	if (cmpxchg_relaxed(&pn->state, vcpu_halted, vcpu_hashed)
	    != vcpu_halted)
		return;

	/*
	 * Put the lock into the hash table and set the _Q_SLOW_VAL.
	 *
	 * As this is the same vCPU that will check the _Q_SLOW_VAL value and
	 * the hash table later on at unlock time, no atomic instruction is
	 * needed.
	 */
	WRITE_ONCE(l->locked, _Q_SLOW_VAL);
	(void)pv_hash(lock, pn);
}

/*
 * Wait for l->locked to become clear and acquire the lock;
 * halt the vcpu after a short spin.
 * __pv_queued_spin_unlock() will wake us.
 *
 * The current value of the lock will be returned for additional processing.
 */
static u32
pv_wait_head_or_lock(struct qspinlock *lock, struct mcs_spinlock *node)
{
	struct pv_node *pn = (struct pv_node *)node;
	struct __qspinlock *l = (void *)lock;
	struct qspinlock **lp = NULL;
	int waitcnt = 0;
	int loop;

	/*
	 * If pv_kick_node() already advanced our state, we don't need to
	 * insert ourselves into the hash table anymore.
	 */
	if (READ_ONCE(pn->state) == vcpu_hashed)
		lp = (struct qspinlock **)1;

	/*
	 * Tracking # of slowpath locking operations
	 */
	qstat_inc(qstat_pv_lock_slowpath, true);

	for (;; waitcnt++) {
		/*
		 * Set correct vCPU state to be used by queue node wait-early
		 * mechanism.
		 */
		WRITE_ONCE(pn->state, vcpu_running);

		/*
		 * Set the pending bit in the active lock spinning loop to
		 * disable lock stealing before attempting to acquire the lock.
		 */
		set_pending(lock);
		for (loop = SPIN_THRESHOLD; loop; loop--) {
			if (trylock_clear_pending(lock))
				goto gotlock;
			cpu_relax();
		}
		clear_pending(lock);


		if (!lp) { /* ONCE */
			lp = pv_hash(lock, pn);

			/*
			 * We must hash before setting _Q_SLOW_VAL, such that
			 * when we observe _Q_SLOW_VAL in __pv_queued_spin_unlock()
			 * we'll be sure to be able to observe our hash entry.
			 *
			 *   [S] <hash>                 [Rmw] l->locked == _Q_SLOW_VAL
			 *       MB                           RMB
			 * [RmW] l->locked = _Q_SLOW_VAL  [L] <unhash>
			 *
			 * Matches the smp_rmb() in __pv_queued_spin_unlock().
			 */
			if (xchg(&l->locked, _Q_SLOW_VAL) == 0) {
				/*
				 * The lock was free and now we own the lock.
				 * Change the lock value back to _Q_LOCKED_VAL
				 * and unhash the table.
				 */
				WRITE_ONCE(l->locked, _Q_LOCKED_VAL);
				WRITE_ONCE(*lp, NULL);
				goto gotlock;
			}
		}
		WRITE_ONCE(pn->state, vcpu_hashed);
		qstat_inc(qstat_pv_wait_head, true);
		qstat_inc(qstat_pv_wait_again, waitcnt);
		pv_wait(&l->locked, _Q_SLOW_VAL);

		/*
		 * Because of lock stealing, the queue head vCPU may not be
		 * able to acquire the lock before it has to wait again.
		 */
	}

	/*
	 * The cmpxchg() or xchg() call before coming here provides the
	 * acquire semantics for locking. The dummy ORing of _Q_LOCKED_VAL
	 * here is to indicate to the compiler that the value will always
	 * be nozero to enable better code optimization.
	 */
gotlock:
	return (u32)(atomic_read(&lock->val) | _Q_LOCKED_VAL);
}

/*
 * PV versions of the unlock fastpath and slowpath functions to be used
 * instead of queued_spin_unlock().
 */
__visible void
__pv_queued_spin_unlock_slowpath(struct qspinlock *lock, u8 locked)
{
	struct __qspinlock *l = (void *)lock;
	struct pv_node *node;

	if (unlikely(locked != _Q_SLOW_VAL)) {
		WARN(!debug_locks_silent,
		     "pvqspinlock: lock 0x%lx has corrupted value 0x%x!\n",
		     (unsigned long)lock, atomic_read(&lock->val));
		return;
	}

	/*
	 * A failed cmpxchg doesn't provide any memory-ordering guarantees,
	 * so we need a barrier to order the read of the node data in
	 * pv_unhash *after* we've read the lock being _Q_SLOW_VAL.
	 *
	 * Matches the cmpxchg() in pv_wait_head_or_lock() setting _Q_SLOW_VAL.
	 */
	smp_rmb();

	/*
	 * Since the above failed to release, this must be the SLOW path.
	 * Therefore start by looking up the blocked node and unhashing it.
	 */
	node = pv_unhash(lock);

	/*
	 * Now that we have a reference to the (likely) blocked pv_node,
	 * release the lock.
	 */
	smp_store_release(&l->locked, 0);

	/*
	 * At this point the memory pointed at by lock can be freed/reused,
	 * however we can still use the pv_node to kick the CPU.
	 * The other vCPU may not really be halted, but kicking an active
	 * vCPU is harmless other than the additional latency in completing
	 * the unlock.
	 */
	qstat_inc(qstat_pv_kick_unlock, true);
	pv_kick(node->cpu);
}

/*
 * Include the architecture specific callee-save thunk of the
 * __pv_queued_spin_unlock(). This thunk is put together with
 * __pv_queued_spin_unlock() to make the callee-save thunk and the real unlock
 * function close to each other sharing consecutive instruction cachelines.
 * Alternatively, architecture specific version of __pv_queued_spin_unlock()
 * can be defined.
 */
#include <asm/qspinlock_paravirt.h>

#ifndef __pv_queued_spin_unlock
__visible void __pv_queued_spin_unlock(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;
	u8 locked;

	/*
	 * We must not unlock if SLOW, because in that case we must first
	 * unhash. Otherwise it would be possible to have multiple @lock
	 * entries, which would be BAD.
	 */
	locked = cmpxchg_release(&l->locked, _Q_LOCKED_VAL, 0);
	if (likely(locked == _Q_LOCKED_VAL))
		return;

	__pv_queued_spin_unlock_slowpath(lock, locked);
}
#endif /* __pv_queued_spin_unlock */
