/*
 * Queued spinlock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 * (C) Copyright 2013-2014 Red Hat, Inc.
 * (C) Copyright 2015 Intel Corp.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 *          Peter Zijlstra <peterz@infradead.org>
 */
#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <asm/qspinlock.h>

/*
 * The basic principle of a queue-based spinlock can best be understood
 * by studying a classic queue-based spinlock implementation called the
 * MCS lock. The paper below provides a good description for this kind
 * of lock.
 *
 * http://www.cise.ufl.edu/tr/DOC/REP-1992-71.pdf
 *
 * This queued spinlock implementation is based on the MCS lock, however to make
 * it fit the 4 bytes we assume spinlock_t to be, and preserve its existing
 * API, we must modify it somehow.
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
 */

#include "mcs_spinlock.h"

/*
 * Per-CPU queue node structures; we can never have more than 4 nested
 * contexts: task, softirq, hardirq, nmi.
 *
 * Exactly fits one 64-byte cacheline on a 64-bit architecture.
 */
static DEFINE_PER_CPU_ALIGNED(struct mcs_spinlock, mcs_nodes[4]);

/*
 * We must be able to distinguish between no-tail and the tail at 0:0,
 * therefore increment the cpu number by one.
 */

static inline u32 encode_tail(int cpu, int idx)
{
	u32 tail;

#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(idx > 3);
#endif
	tail  = (cpu + 1) << _Q_TAIL_CPU_OFFSET;
	tail |= idx << _Q_TAIL_IDX_OFFSET; /* assume < 4 */

	return tail;
}

static inline struct mcs_spinlock *decode_tail(u32 tail)
{
	int cpu = (tail >> _Q_TAIL_CPU_OFFSET) - 1;
	int idx = (tail &  _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return per_cpu_ptr(&mcs_nodes[idx], cpu);
}

/**
 * queued_spin_lock_slowpath - acquire the queued spinlock
 * @lock: Pointer to queued spinlock structure
 * @val: Current value of the queued spinlock 32-bit word
 *
 * (queue tail, lock value)
 *
 *              fast      :    slow                                  :    unlock
 *                        :                                          :
 * uncontended  (0,0)   --:--> (0,1) --------------------------------:--> (*,0)
 *                        :       | ^--------.                    /  :
 *                        :       v           \                   |  :
 * uncontended            :    (n,x) --+--> (n,0)                 |  :
 *   queue                :       | ^--'                          |  :
 *                        :       v                               |  :
 * contended              :    (*,x) --+--> (*,0) -----> (*,1) ---'  :
 *   queue                :         ^--'                             :
 *
 */
void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 new, old, tail;
	int idx;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	node = this_cpu_ptr(&mcs_nodes[0]);
	idx = node->count++;
	tail = encode_tail(smp_processor_id(), idx);

	node += idx;
	node->locked = 0;
	node->next = NULL;

	/*
	 * trylock || xchg(lock, node)
	 *
	 * 0,0 -> 0,1 ; no tail, not locked -> no tail, locked.
	 * p,x -> n,x ; tail was p -> tail is n; preserving locked.
	 */
	for (;;) {
		new = _Q_LOCKED_VAL;
		if (val)
			new = tail | (val & _Q_LOCKED_MASK);

		old = atomic_cmpxchg(&lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}

	/*
	 * we won the trylock; forget about queueing.
	 */
	if (new == _Q_LOCKED_VAL)
		goto release;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & ~_Q_LOCKED_MASK) {
		prev = decode_tail(old);
		WRITE_ONCE(prev->next, node);

		arch_mcs_spin_lock_contended(&node->locked);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner to go away.
	 *
	 * *,x -> *,0
	 */
	while ((val = atomic_read(&lock->val)) & _Q_LOCKED_MASK)
		cpu_relax();

	/*
	 * claim the lock:
	 *
	 * n,0 -> 0,1 : lock, uncontended
	 * *,0 -> *,1 : lock, contended
	 */
	for (;;) {
		new = _Q_LOCKED_VAL;
		if (val != tail)
			new |= val;

		old = atomic_cmpxchg(&lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}

	/*
	 * contended path; wait for next, release.
	 */
	if (new != _Q_LOCKED_VAL) {
		while (!(next = READ_ONCE(node->next)))
			cpu_relax();

		arch_mcs_spin_unlock_contended(&next->locked);
	}

release:
	/*
	 * release the node
	 */
	this_cpu_dec(mcs_nodes[0].count);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);
