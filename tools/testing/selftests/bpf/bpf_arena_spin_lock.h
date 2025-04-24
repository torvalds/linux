// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#ifndef BPF_ARENA_SPIN_LOCK_H
#define BPF_ARENA_SPIN_LOCK_H

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_atomic.h"

#define arch_mcs_spin_lock_contended_label(l, label) smp_cond_load_acquire_label(l, VAL, label)
#define arch_mcs_spin_unlock_contended(l) smp_store_release((l), 1)

#if defined(ENABLE_ATOMICS_TESTS) && defined(__BPF_FEATURE_ADDR_SPACE_CAST)

#define EBUSY 16
#define EOPNOTSUPP 95
#define ETIMEDOUT 110

#ifndef __arena
#define __arena __attribute__((address_space(1)))
#endif

extern unsigned long CONFIG_NR_CPUS __kconfig;

/*
 * Typically, we'd just rely on the definition in vmlinux.h for qspinlock, but
 * PowerPC overrides the definition to define lock->val as u32 instead of
 * atomic_t, leading to compilation errors.  Import a local definition below so
 * that we don't depend on the vmlinux.h version.
 */

struct __qspinlock {
	union {
		atomic_t val;
		struct {
			u8 locked;
			u8 pending;
		};
		struct {
			u16 locked_pending;
			u16 tail;
		};
	};
};

#define arena_spinlock_t struct __qspinlock
/* FIXME: Using typedef causes CO-RE relocation error */
/* typedef struct qspinlock arena_spinlock_t; */

struct arena_mcs_spinlock {
	struct arena_mcs_spinlock __arena *next;
	int locked;
	int count;
};

struct arena_qnode {
	struct arena_mcs_spinlock mcs;
};

#define _Q_MAX_NODES		4
#define _Q_PENDING_LOOPS	1

/*
 * Bitfields in the atomic value:
 *
 *  0- 7: locked byte
 *     8: pending
 *  9-15: not used
 * 16-17: tail index
 * 18-31: tail cpu (+1)
 */
#define _Q_MAX_CPUS		1024

#define	_Q_SET_MASK(type)	(((1U << _Q_ ## type ## _BITS) - 1)\
				      << _Q_ ## type ## _OFFSET)
#define _Q_LOCKED_OFFSET	0
#define _Q_LOCKED_BITS		8
#define _Q_LOCKED_MASK		_Q_SET_MASK(LOCKED)

#define _Q_PENDING_OFFSET	(_Q_LOCKED_OFFSET + _Q_LOCKED_BITS)
#define _Q_PENDING_BITS		8
#define _Q_PENDING_MASK		_Q_SET_MASK(PENDING)

#define _Q_TAIL_IDX_OFFSET	(_Q_PENDING_OFFSET + _Q_PENDING_BITS)
#define _Q_TAIL_IDX_BITS	2
#define _Q_TAIL_IDX_MASK	_Q_SET_MASK(TAIL_IDX)

#define _Q_TAIL_CPU_OFFSET	(_Q_TAIL_IDX_OFFSET + _Q_TAIL_IDX_BITS)
#define _Q_TAIL_CPU_BITS	(32 - _Q_TAIL_CPU_OFFSET)
#define _Q_TAIL_CPU_MASK	_Q_SET_MASK(TAIL_CPU)

#define _Q_TAIL_OFFSET		_Q_TAIL_IDX_OFFSET
#define _Q_TAIL_MASK		(_Q_TAIL_IDX_MASK | _Q_TAIL_CPU_MASK)

#define _Q_LOCKED_VAL		(1U << _Q_LOCKED_OFFSET)
#define _Q_PENDING_VAL		(1U << _Q_PENDING_OFFSET)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

struct arena_qnode __arena qnodes[_Q_MAX_CPUS][_Q_MAX_NODES];

static inline u32 encode_tail(int cpu, int idx)
{
	u32 tail;

	tail  = (cpu + 1) << _Q_TAIL_CPU_OFFSET;
	tail |= idx << _Q_TAIL_IDX_OFFSET; /* assume < 4 */

	return tail;
}

static inline struct arena_mcs_spinlock __arena *decode_tail(u32 tail)
{
	u32 cpu = (tail >> _Q_TAIL_CPU_OFFSET) - 1;
	u32 idx = (tail &  _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return &qnodes[cpu][idx].mcs;
}

static inline
struct arena_mcs_spinlock __arena *grab_mcs_node(struct arena_mcs_spinlock __arena *base, int idx)
{
	return &((struct arena_qnode __arena *)base + idx)->mcs;
}

#define _Q_LOCKED_PENDING_MASK (_Q_LOCKED_MASK | _Q_PENDING_MASK)

/**
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(arena_spinlock_t __arena *lock, u32 tail)
{
	u32 old, new;

	old = atomic_read(&lock->val);
	do {
		new = (old & _Q_LOCKED_PENDING_MASK) | tail;
		/*
		 * We can use relaxed semantics since the caller ensures that
		 * the MCS node is properly initialized before updating the
		 * tail.
		 */
		/* These loops are not expected to stall, but we still need to
		 * prove to the verifier they will terminate eventually.
		 */
		cond_break_label(out);
	} while (!atomic_try_cmpxchg_relaxed(&lock->val, &old, new));

	return old;
out:
	bpf_printk("RUNTIME ERROR: %s unexpected cond_break exit!!!", __func__);
	return old;
}

/**
 * clear_pending - clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,* -> *,0,*
 */
static __always_inline void clear_pending(arena_spinlock_t __arena *lock)
{
	WRITE_ONCE(lock->pending, 0);
}

/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 *
 * Lock stealing is not allowed if this function is used.
 */
static __always_inline void clear_pending_set_locked(arena_spinlock_t __arena *lock)
{
	WRITE_ONCE(lock->locked_pending, _Q_LOCKED_VAL);
}

/**
 * set_locked - Set the lock bit and own the lock
 * @lock: Pointer to queued spinlock structure
 *
 * *,*,0 -> *,0,1
 */
static __always_inline void set_locked(arena_spinlock_t __arena *lock)
{
	WRITE_ONCE(lock->locked, _Q_LOCKED_VAL);
}

static __always_inline
u32 arena_fetch_set_pending_acquire(arena_spinlock_t __arena *lock)
{
	u32 old, new;

	old = atomic_read(&lock->val);
	do {
		new = old | _Q_PENDING_VAL;
		/*
		 * These loops are not expected to stall, but we still need to
		 * prove to the verifier they will terminate eventually.
		 */
		cond_break_label(out);
	} while (!atomic_try_cmpxchg_acquire(&lock->val, &old, new));

	return old;
out:
	bpf_printk("RUNTIME ERROR: %s unexpected cond_break exit!!!", __func__);
	return old;
}

/**
 * arena_spin_trylock - try to acquire the queued spinlock
 * @lock : Pointer to queued spinlock structure
 * Return: 1 if lock acquired, 0 if failed
 */
static __always_inline int arena_spin_trylock(arena_spinlock_t __arena *lock)
{
	int val = atomic_read(&lock->val);

	if (unlikely(val))
		return 0;

	return likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL));
}

__noinline
int arena_spin_lock_slowpath(arena_spinlock_t __arena __arg_arena *lock, u32 val)
{
	struct arena_mcs_spinlock __arena *prev, *next, *node0, *node;
	int ret = -ETIMEDOUT;
	u32 old, tail;
	int idx;

	/*
	 * Wait for in-progress pending->locked hand-overs with a bounded
	 * number of spins so that we guarantee forward progress.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL) {
		int cnt = _Q_PENDING_LOOPS;
		val = atomic_cond_read_relaxed_label(&lock->val,
						     (VAL != _Q_PENDING_VAL) || !cnt--,
						     release_err);
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
	val = arena_fetch_set_pending_acquire(lock);

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
	if (val & _Q_LOCKED_MASK)
		smp_cond_load_acquire_label(&lock->locked, !VAL, release_err);

	/*
	 * take ownership and clear the pending bit.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	clear_pending_set_locked(lock);
	return 0;

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	node0 = &(qnodes[bpf_get_smp_processor_id()])[0].mcs;
	idx = node0->count++;
	tail = encode_tail(bpf_get_smp_processor_id(), idx);

	/*
	 * 4 nodes are allocated based on the assumption that there will not be
	 * nested NMIs taking spinlocks. That may not be true in some
	 * architectures even though the chance of needing more than 4 nodes
	 * will still be extremely unlikely. When that happens, we simply return
	 * an error. Original qspinlock has a trylock fallback in this case.
	 */
	if (unlikely(idx >= _Q_MAX_NODES)) {
		ret = -EBUSY;
		goto release_node_err;
	}

	node = grab_mcs_node(node0, idx);

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
	if (arena_spin_trylock(lock))
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
		prev = decode_tail(old);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		arch_mcs_spin_lock_contended_label(&node->locked, release_node_err);

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We cannot prefetch here
		 * due to lack of equivalent instruction in BPF ISA.
		 */
		next = READ_ONCE(node->next);
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
	 */
	val = atomic_cond_read_acquire_label(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK),
					     release_node_err);

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
	 * In the PV case we might already have _Q_LOCKED_VAL set, because
	 * of lock stealing; therefore we must also allow:
	 *
	 * n,0,1 -> 0,0,1
	 *
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
		next = smp_cond_load_relaxed_label(&node->next, (VAL), release_node_err);

	arch_mcs_spin_unlock_contended(&next->locked);

release:;
	/*
	 * release the node
	 *
	 * Doing a normal dec vs this_cpu_dec is fine. An upper context always
	 * decrements count it incremented before returning, thus we're fine.
	 * For contexts interrupting us, they either observe our dec or not.
	 * Just ensure the compiler doesn't reorder this statement, as a
	 * this_cpu_dec implicitly implied that.
	 */
	barrier();
	node0->count--;
	return 0;
release_node_err:
	barrier();
	node0->count--;
	goto release_err;
release_err:
	return ret;
}

/**
 * arena_spin_lock - acquire a queued spinlock
 * @lock: Pointer to queued spinlock structure
 *
 * On error, returned value will be negative.
 * On success, zero is returned.
 *
 * The return value _must_ be tested against zero for success,
 * instead of checking it against negative, for passing the
 * BPF verifier.
 *
 * The user should do:
 *	if (arena_spin_lock(...) != 0) // failure
 *		or
 *	if (arena_spin_lock(...) == 0) // success
 *		or
 *	if (arena_spin_lock(...)) // failure
 *		or
 *	if (!arena_spin_lock(...)) // success
 * instead of:
 *	if (arena_spin_lock(...) < 0) // failure
 *
 * The return value can still be inspected later.
 */
static __always_inline int arena_spin_lock(arena_spinlock_t __arena *lock)
{
	int val = 0;

	if (CONFIG_NR_CPUS > 1024)
		return -EOPNOTSUPP;

	bpf_preempt_disable();
	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL)))
		return 0;

	val = arena_spin_lock_slowpath(lock, val);
	/* FIXME: bpf_assert_range(-MAX_ERRNO, 0) once we have it working for all cases. */
	if (val)
		bpf_preempt_enable();
	return val;
}

/**
 * arena_spin_unlock - release a queued spinlock
 * @lock : Pointer to queued spinlock structure
 */
static __always_inline void arena_spin_unlock(arena_spinlock_t __arena *lock)
{
	/*
	 * unlock() needs release semantics:
	 */
	smp_store_release(&lock->locked, 0);
	bpf_preempt_enable();
}

#define arena_spin_lock_irqsave(lock, flags)             \
	({                                               \
		int __ret;                               \
		bpf_local_irq_save(&(flags));            \
		__ret = arena_spin_lock((lock));         \
		if (__ret)                               \
			bpf_local_irq_restore(&(flags)); \
		(__ret);                                 \
	})

#define arena_spin_unlock_irqrestore(lock, flags) \
	({                                        \
		arena_spin_unlock((lock));        \
		bpf_local_irq_restore(&(flags));  \
	})

#endif

#endif /* BPF_ARENA_SPIN_LOCK_H */
