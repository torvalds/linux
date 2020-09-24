/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H

/*
 * seqcount_t / seqlock_t - a reader-writer consistency mechanism with
 * lockless readers (read-only retry loops), and no writer starvation.
 *
 * See Documentation/locking/seqlock.rst
 *
 * Copyrights:
 * - Based on x86_64 vsyscall gettimeofday: Keith Owens, Andrea Arcangeli
 * - Sequence counters with associated locks, (C) 2020 Linutronix GmbH
 */

#include <linux/compiler.h>
#include <linux/kcsan-checks.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/ww_mutex.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>

#include <asm/processor.h>

/*
 * The seqlock seqcount_t interface does not prescribe a precise sequence of
 * read begin/retry/end. For readers, typically there is a call to
 * read_seqcount_begin() and read_seqcount_retry(), however, there are more
 * esoteric cases which do not follow this pattern.
 *
 * As a consequence, we take the following best-effort approach for raw usage
 * via seqcount_t under KCSAN: upon beginning a seq-reader critical section,
 * pessimistically mark the next KCSAN_SEQLOCK_REGION_MAX memory accesses as
 * atomics; if there is a matching read_seqcount_retry() call, no following
 * memory operations are considered atomic. Usage of the seqlock_t interface
 * is not affected.
 */
#define KCSAN_SEQLOCK_REGION_MAX 1000

/*
 * Sequence counters (seqcount_t)
 *
 * This is the raw counting mechanism, without any writer protection.
 *
 * Write side critical sections must be serialized and non-preemptible.
 *
 * If readers can be invoked from hardirq or softirq contexts,
 * interrupts or bottom halves must also be respectively disabled before
 * entering the write section.
 *
 * This mechanism can't be used if the protected data contains pointers,
 * as the writer can invalidate a pointer that a reader is following.
 *
 * If the write serialization mechanism is one of the common kernel
 * locking primitives, use a sequence counter with associated lock
 * (seqcount_LOCKNAME_t) instead.
 *
 * If it's desired to automatically handle the sequence counter writer
 * serialization and non-preemptibility requirements, use a sequential
 * lock (seqlock_t) instead.
 *
 * See Documentation/locking/seqlock.rst
 */
typedef struct seqcount {
	unsigned sequence;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} seqcount_t;

static inline void __seqcount_init(seqcount_t *s, const char *name,
					  struct lock_class_key *key)
{
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	lockdep_init_map(&s->dep_map, name, key, 0);
	s->sequence = 0;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

# define SEQCOUNT_DEP_MAP_INIT(lockname)				\
		.dep_map = { .name = #lockname }

/**
 * seqcount_init() - runtime initializer for seqcount_t
 * @s: Pointer to the seqcount_t instance
 */
# define seqcount_init(s)						\
	do {								\
		static struct lock_class_key __key;			\
		__seqcount_init((s), #s, &__key);			\
	} while (0)

static inline void seqcount_lockdep_reader_access(const seqcount_t *s)
{
	seqcount_t *l = (seqcount_t *)s;
	unsigned long flags;

	local_irq_save(flags);
	seqcount_acquire_read(&l->dep_map, 0, 0, _RET_IP_);
	seqcount_release(&l->dep_map, _RET_IP_);
	local_irq_restore(flags);
}

#else
# define SEQCOUNT_DEP_MAP_INIT(lockname)
# define seqcount_init(s) __seqcount_init(s, NULL, NULL)
# define seqcount_lockdep_reader_access(x)
#endif

/**
 * SEQCNT_ZERO() - static initializer for seqcount_t
 * @name: Name of the seqcount_t instance
 */
#define SEQCNT_ZERO(name) { .sequence = 0, SEQCOUNT_DEP_MAP_INIT(name) }

/*
 * Sequence counters with associated locks (seqcount_LOCKNAME_t)
 *
 * A sequence counter which associates the lock used for writer
 * serialization at initialization time. This enables lockdep to validate
 * that the write side critical section is properly serialized.
 *
 * For associated locks which do not implicitly disable preemption,
 * preemption protection is enforced in the write side function.
 *
 * Lockdep is never used in any for the raw write variants.
 *
 * See Documentation/locking/seqlock.rst
 */

/*
 * For PREEMPT_RT, seqcount_LOCKNAME_t write side critical sections cannot
 * disable preemption. It can lead to higher latencies, and the write side
 * sections will not be able to acquire locks which become sleeping locks
 * (e.g. spinlock_t).
 *
 * To remain preemptible while avoiding a possible livelock caused by the
 * reader preempting the writer, use a different technique: let the reader
 * detect if a seqcount_LOCKNAME_t writer is in progress. If that is the
 * case, acquire then release the associated LOCKNAME writer serialization
 * lock. This will allow any possibly-preempted writer to make progress
 * until the end of its writer serialization lock critical section.
 *
 * This lock-unlock technique must be implemented for all of PREEMPT_RT
 * sleeping locks.  See Documentation/locking/locktypes.rst
 */
#if defined(CONFIG_LOCKDEP) || defined(CONFIG_PREEMPT_RT)
#define __SEQ_LOCK(expr)	expr
#else
#define __SEQ_LOCK(expr)
#endif

/**
 * typedef seqcount_LOCKNAME_t - sequence counter with LOCKNAME associated
 * @seqcount:	The real sequence counter
 * @lock:	Pointer to the associated lock
 *
 * A plain sequence counter with external writer synchronization by
 * LOCKNAME @lock. The lock is associated to the sequence counter in the
 * static initializer or init function. This enables lockdep to validate
 * that the write side critical section is properly serialized.
 *
 * LOCKNAME:	raw_spinlock, spinlock, rwlock, mutex, or ww_mutex.
 */

/*
 * seqcount_LOCKNAME_init() - runtime initializer for seqcount_LOCKNAME_t
 * @s:		Pointer to the seqcount_LOCKNAME_t instance
 * @lock:	Pointer to the associated lock
 */

#define seqcount_LOCKNAME_init(s, _lock, lockname)			\
	do {								\
		seqcount_##lockname##_t *____s = (s);			\
		seqcount_init(&____s->seqcount);			\
		__SEQ_LOCK(____s->lock = (_lock));			\
	} while (0)

#define seqcount_raw_spinlock_init(s, lock)	seqcount_LOCKNAME_init(s, lock, raw_spinlock)
#define seqcount_spinlock_init(s, lock)		seqcount_LOCKNAME_init(s, lock, spinlock)
#define seqcount_rwlock_init(s, lock)		seqcount_LOCKNAME_init(s, lock, rwlock);
#define seqcount_mutex_init(s, lock)		seqcount_LOCKNAME_init(s, lock, mutex);
#define seqcount_ww_mutex_init(s, lock)		seqcount_LOCKNAME_init(s, lock, ww_mutex);

/*
 * SEQCOUNT_LOCKNAME()	- Instantiate seqcount_LOCKNAME_t and helpers
 * seqprop_LOCKNAME_*()	- Property accessors for seqcount_LOCKNAME_t
 *
 * @lockname:		"LOCKNAME" part of seqcount_LOCKNAME_t
 * @locktype:		LOCKNAME canonical C data type
 * @preemptible:	preemptibility of above locktype
 * @lockmember:		argument for lockdep_assert_held()
 * @lockbase:		associated lock release function (prefix only)
 * @lock_acquire:	associated lock acquisition function (full call)
 */
#define SEQCOUNT_LOCKNAME(lockname, locktype, preemptible, lockmember, lockbase, lock_acquire) \
typedef struct seqcount_##lockname {					\
	seqcount_t		seqcount;				\
	__SEQ_LOCK(locktype	*lock);					\
} seqcount_##lockname##_t;						\
									\
static __always_inline seqcount_t *					\
__seqprop_##lockname##_ptr(seqcount_##lockname##_t *s)			\
{									\
	return &s->seqcount;						\
}									\
									\
static __always_inline unsigned						\
__seqprop_##lockname##_sequence(const seqcount_##lockname##_t *s)	\
{									\
	unsigned seq = READ_ONCE(s->seqcount.sequence);			\
									\
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))				\
		return seq;						\
									\
	if (preemptible && unlikely(seq & 1)) {				\
		__SEQ_LOCK(lock_acquire);				\
		__SEQ_LOCK(lockbase##_unlock(s->lock));			\
									\
		/*							\
		 * Re-read the sequence counter since the (possibly	\
		 * preempted) writer made progress.			\
		 */							\
		seq = READ_ONCE(s->seqcount.sequence);			\
	}								\
									\
	return seq;							\
}									\
									\
static __always_inline bool						\
__seqprop_##lockname##_preemptible(const seqcount_##lockname##_t *s)	\
{									\
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))				\
		return preemptible;					\
									\
	/* PREEMPT_RT relies on the above LOCK+UNLOCK */		\
	return false;							\
}									\
									\
static __always_inline void						\
__seqprop_##lockname##_assert(const seqcount_##lockname##_t *s)		\
{									\
	__SEQ_LOCK(lockdep_assert_held(lockmember));			\
}

/*
 * __seqprop() for seqcount_t
 */

static inline seqcount_t *__seqprop_ptr(seqcount_t *s)
{
	return s;
}

static inline unsigned __seqprop_sequence(const seqcount_t *s)
{
	return READ_ONCE(s->sequence);
}

static inline bool __seqprop_preemptible(const seqcount_t *s)
{
	return false;
}

static inline void __seqprop_assert(const seqcount_t *s)
{
	lockdep_assert_preemption_disabled();
}

#define __SEQ_RT	IS_ENABLED(CONFIG_PREEMPT_RT)

SEQCOUNT_LOCKNAME(raw_spinlock, raw_spinlock_t,  false,    s->lock,        raw_spin, raw_spin_lock(s->lock))
SEQCOUNT_LOCKNAME(spinlock,     spinlock_t,      __SEQ_RT, s->lock,        spin,     spin_lock(s->lock))
SEQCOUNT_LOCKNAME(rwlock,       rwlock_t,        __SEQ_RT, s->lock,        read,     read_lock(s->lock))
SEQCOUNT_LOCKNAME(mutex,        struct mutex,    true,     s->lock,        mutex,    mutex_lock(s->lock))
SEQCOUNT_LOCKNAME(ww_mutex,     struct ww_mutex, true,     &s->lock->base, ww_mutex, ww_mutex_lock(s->lock, NULL))

/*
 * SEQCNT_LOCKNAME_ZERO - static initializer for seqcount_LOCKNAME_t
 * @name:	Name of the seqcount_LOCKNAME_t instance
 * @lock:	Pointer to the associated LOCKNAME
 */

#define SEQCOUNT_LOCKNAME_ZERO(seq_name, assoc_lock) {			\
	.seqcount		= SEQCNT_ZERO(seq_name.seqcount),	\
	__SEQ_LOCK(.lock	= (assoc_lock))				\
}

#define SEQCNT_RAW_SPINLOCK_ZERO(name, lock)	SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_SPINLOCK_ZERO(name, lock)	SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_RWLOCK_ZERO(name, lock)		SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_MUTEX_ZERO(name, lock)		SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_WW_MUTEX_ZERO(name, lock) 	SEQCOUNT_LOCKNAME_ZERO(name, lock)

#define __seqprop_case(s, lockname, prop)				\
	seqcount_##lockname##_t: __seqprop_##lockname##_##prop((void *)(s))

#define __seqprop(s, prop) _Generic(*(s),				\
	seqcount_t:		__seqprop_##prop((void *)(s)),		\
	__seqprop_case((s),	raw_spinlock,	prop),			\
	__seqprop_case((s),	spinlock,	prop),			\
	__seqprop_case((s),	rwlock,		prop),			\
	__seqprop_case((s),	mutex,		prop),			\
	__seqprop_case((s),	ww_mutex,	prop))

#define __seqcount_ptr(s)		__seqprop(s, ptr)
#define __seqcount_sequence(s)		__seqprop(s, sequence)
#define __seqcount_lock_preemptible(s)	__seqprop(s, preemptible)
#define __seqcount_assert_lock_held(s)	__seqprop(s, assert)

/**
 * __read_seqcount_begin() - begin a seqcount_t read section w/o barrier
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * __read_seqcount_begin is like read_seqcount_begin, but has no smp_rmb()
 * barrier. Callers should ensure that smp_rmb() or equivalent ordering is
 * provided before actually loading any of the variables that are to be
 * protected in this critical section.
 *
 * Use carefully, only in critical code, and comment how the barrier is
 * provided.
 *
 * Return: count to be passed to read_seqcount_retry()
 */
#define __read_seqcount_begin(s)					\
({									\
	unsigned seq;							\
									\
	while ((seq = __seqcount_sequence(s)) & 1)			\
		cpu_relax();						\
									\
	kcsan_atomic_next(KCSAN_SEQLOCK_REGION_MAX);			\
	seq;								\
})

/**
 * raw_read_seqcount_begin() - begin a seqcount_t read section w/o lockdep
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * Return: count to be passed to read_seqcount_retry()
 */
#define raw_read_seqcount_begin(s)					\
({									\
	unsigned seq = __read_seqcount_begin(s);			\
									\
	smp_rmb();							\
	seq;								\
})

/**
 * read_seqcount_begin() - begin a seqcount_t read critical section
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * Return: count to be passed to read_seqcount_retry()
 */
#define read_seqcount_begin(s)						\
({									\
	seqcount_lockdep_reader_access(__seqcount_ptr(s));		\
	raw_read_seqcount_begin(s);					\
})

/**
 * raw_read_seqcount() - read the raw seqcount_t counter value
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * raw_read_seqcount opens a read critical section of the given
 * seqcount_t, without any lockdep checking, and without checking or
 * masking the sequence counter LSB. Calling code is responsible for
 * handling that.
 *
 * Return: count to be passed to read_seqcount_retry()
 */
#define raw_read_seqcount(s)						\
({									\
	unsigned seq = __seqcount_sequence(s);				\
									\
	smp_rmb();							\
	kcsan_atomic_next(KCSAN_SEQLOCK_REGION_MAX);			\
	seq;								\
})

/**
 * raw_seqcount_begin() - begin a seqcount_t read critical section w/o
 *                        lockdep and w/o counter stabilization
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * raw_seqcount_begin opens a read critical section of the given
 * seqcount_t. Unlike read_seqcount_begin(), this function will not wait
 * for the count to stabilize. If a writer is active when it begins, it
 * will fail the read_seqcount_retry() at the end of the read critical
 * section instead of stabilizing at the beginning of it.
 *
 * Use this only in special kernel hot paths where the read section is
 * small and has a high probability of success through other external
 * means. It will save a single branching instruction.
 *
 * Return: count to be passed to read_seqcount_retry()
 */
#define raw_seqcount_begin(s)						\
({									\
	/*								\
	 * If the counter is odd, let read_seqcount_retry() fail	\
	 * by decrementing the counter.					\
	 */								\
	raw_read_seqcount(s) & ~1;					\
})

/**
 * __read_seqcount_retry() - end a seqcount_t read section w/o barrier
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 * @start: count, from read_seqcount_begin()
 *
 * __read_seqcount_retry is like read_seqcount_retry, but has no smp_rmb()
 * barrier. Callers should ensure that smp_rmb() or equivalent ordering is
 * provided before actually loading any of the variables that are to be
 * protected in this critical section.
 *
 * Use carefully, only in critical code, and comment how the barrier is
 * provided.
 *
 * Return: true if a read section retry is required, else false
 */
#define __read_seqcount_retry(s, start)					\
	__read_seqcount_t_retry(__seqcount_ptr(s), start)

static inline int __read_seqcount_t_retry(const seqcount_t *s, unsigned start)
{
	kcsan_atomic_next(0);
	return unlikely(READ_ONCE(s->sequence) != start);
}

/**
 * read_seqcount_retry() - end a seqcount_t read critical section
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 * @start: count, from read_seqcount_begin()
 *
 * read_seqcount_retry closes the read critical section of given
 * seqcount_t.  If the critical section was invalid, it must be ignored
 * (and typically retried).
 *
 * Return: true if a read section retry is required, else false
 */
#define read_seqcount_retry(s, start)					\
	read_seqcount_t_retry(__seqcount_ptr(s), start)

static inline int read_seqcount_t_retry(const seqcount_t *s, unsigned start)
{
	smp_rmb();
	return __read_seqcount_t_retry(s, start);
}

/**
 * raw_write_seqcount_begin() - start a seqcount_t write section w/o lockdep
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 */
#define raw_write_seqcount_begin(s)					\
do {									\
	if (__seqcount_lock_preemptible(s))				\
		preempt_disable();					\
									\
	raw_write_seqcount_t_begin(__seqcount_ptr(s));			\
} while (0)

static inline void raw_write_seqcount_t_begin(seqcount_t *s)
{
	kcsan_nestable_atomic_begin();
	s->sequence++;
	smp_wmb();
}

/**
 * raw_write_seqcount_end() - end a seqcount_t write section w/o lockdep
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 */
#define raw_write_seqcount_end(s)					\
do {									\
	raw_write_seqcount_t_end(__seqcount_ptr(s));			\
									\
	if (__seqcount_lock_preemptible(s))				\
		preempt_enable();					\
} while (0)

static inline void raw_write_seqcount_t_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
	kcsan_nestable_atomic_end();
}

/**
 * write_seqcount_begin_nested() - start a seqcount_t write section with
 *                                 custom lockdep nesting level
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 * @subclass: lockdep nesting level
 *
 * See Documentation/locking/lockdep-design.rst
 */
#define write_seqcount_begin_nested(s, subclass)			\
do {									\
	__seqcount_assert_lock_held(s);					\
									\
	if (__seqcount_lock_preemptible(s))				\
		preempt_disable();					\
									\
	write_seqcount_t_begin_nested(__seqcount_ptr(s), subclass);	\
} while (0)

static inline void write_seqcount_t_begin_nested(seqcount_t *s, int subclass)
{
	raw_write_seqcount_t_begin(s);
	seqcount_acquire(&s->dep_map, subclass, 0, _RET_IP_);
}

/**
 * write_seqcount_begin() - start a seqcount_t write side critical section
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * write_seqcount_begin opens a write side critical section of the given
 * seqcount_t.
 *
 * Context: seqcount_t write side critical sections must be serialized and
 * non-preemptible. If readers can be invoked from hardirq or softirq
 * context, interrupts or bottom halves must be respectively disabled.
 */
#define write_seqcount_begin(s)						\
do {									\
	__seqcount_assert_lock_held(s);					\
									\
	if (__seqcount_lock_preemptible(s))				\
		preempt_disable();					\
									\
	write_seqcount_t_begin(__seqcount_ptr(s));			\
} while (0)

static inline void write_seqcount_t_begin(seqcount_t *s)
{
	write_seqcount_t_begin_nested(s, 0);
}

/**
 * write_seqcount_end() - end a seqcount_t write side critical section
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * The write section must've been opened with write_seqcount_begin().
 */
#define write_seqcount_end(s)						\
do {									\
	write_seqcount_t_end(__seqcount_ptr(s));			\
									\
	if (__seqcount_lock_preemptible(s))				\
		preempt_enable();					\
} while (0)

static inline void write_seqcount_t_end(seqcount_t *s)
{
	seqcount_release(&s->dep_map, _RET_IP_);
	raw_write_seqcount_t_end(s);
}

/**
 * raw_write_seqcount_barrier() - do a seqcount_t write barrier
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * This can be used to provide an ordering guarantee instead of the usual
 * consistency guarantee. It is one wmb cheaper, because it can collapse
 * the two back-to-back wmb()s.
 *
 * Note that writes surrounding the barrier should be declared atomic (e.g.
 * via WRITE_ONCE): a) to ensure the writes become visible to other threads
 * atomically, avoiding compiler optimizations; b) to document which writes are
 * meant to propagate to the reader critical section. This is necessary because
 * neither writes before and after the barrier are enclosed in a seq-writer
 * critical section that would ensure readers are aware of ongoing writes::
 *
 *	seqcount_t seq;
 *	bool X = true, Y = false;
 *
 *	void read(void)
 *	{
 *		bool x, y;
 *
 *		do {
 *			int s = read_seqcount_begin(&seq);
 *
 *			x = X; y = Y;
 *
 *		} while (read_seqcount_retry(&seq, s));
 *
 *		BUG_ON(!x && !y);
 *      }
 *
 *      void write(void)
 *      {
 *		WRITE_ONCE(Y, true);
 *
 *		raw_write_seqcount_barrier(seq);
 *
 *		WRITE_ONCE(X, false);
 *      }
 */
#define raw_write_seqcount_barrier(s)					\
	raw_write_seqcount_t_barrier(__seqcount_ptr(s))

static inline void raw_write_seqcount_t_barrier(seqcount_t *s)
{
	kcsan_nestable_atomic_begin();
	s->sequence++;
	smp_wmb();
	s->sequence++;
	kcsan_nestable_atomic_end();
}

/**
 * write_seqcount_invalidate() - invalidate in-progress seqcount_t read
 *                               side operations
 * @s: Pointer to seqcount_t or any of the seqcount_LOCKNAME_t variants
 *
 * After write_seqcount_invalidate, no seqcount_t read side operations
 * will complete successfully and see data older than this.
 */
#define write_seqcount_invalidate(s)					\
	write_seqcount_t_invalidate(__seqcount_ptr(s))

static inline void write_seqcount_t_invalidate(seqcount_t *s)
{
	smp_wmb();
	kcsan_nestable_atomic_begin();
	s->sequence+=2;
	kcsan_nestable_atomic_end();
}

/*
 * Latch sequence counters (seqcount_latch_t)
 *
 * A sequence counter variant where the counter even/odd value is used to
 * switch between two copies of protected data. This allows the read path,
 * typically NMIs, to safely interrupt the write side critical section.
 *
 * As the write sections are fully preemptible, no special handling for
 * PREEMPT_RT is needed.
 */
typedef struct {
	seqcount_t seqcount;
} seqcount_latch_t;

/**
 * SEQCNT_LATCH_ZERO() - static initializer for seqcount_latch_t
 * @seq_name: Name of the seqcount_latch_t instance
 */
#define SEQCNT_LATCH_ZERO(seq_name) {					\
	.seqcount		= SEQCNT_ZERO(seq_name.seqcount),	\
}

/**
 * seqcount_latch_init() - runtime initializer for seqcount_latch_t
 * @s: Pointer to the seqcount_latch_t instance
 */
static inline void seqcount_latch_init(seqcount_latch_t *s)
{
	seqcount_init(&s->seqcount);
}

/**
 * raw_read_seqcount_latch() - pick even/odd latch data copy
 * @s: Pointer to seqcount_latch_t
 *
 * See raw_write_seqcount_latch() for details and a full reader/writer
 * usage example.
 *
 * Return: sequence counter raw value. Use the lowest bit as an index for
 * picking which data copy to read. The full counter must then be checked
 * with read_seqcount_latch_retry().
 */
static inline unsigned raw_read_seqcount_latch(const seqcount_latch_t *s)
{
	/*
	 * Pairs with the first smp_wmb() in raw_write_seqcount_latch().
	 * Due to the dependent load, a full smp_rmb() is not needed.
	 */
	return READ_ONCE(s->seqcount.sequence);
}

/**
 * read_seqcount_latch_retry() - end a seqcount_latch_t read section
 * @s:		Pointer to seqcount_latch_t
 * @start:	count, from raw_read_seqcount_latch()
 *
 * Return: true if a read section retry is required, else false
 */
static inline int
read_seqcount_latch_retry(const seqcount_latch_t *s, unsigned start)
{
	return read_seqcount_retry(&s->seqcount, start);
}

/**
 * raw_write_seqcount_latch() - redirect latch readers to even/odd copy
 * @s: Pointer to seqcount_latch_t
 *
 * The latch technique is a multiversion concurrency control method that allows
 * queries during non-atomic modifications. If you can guarantee queries never
 * interrupt the modification -- e.g. the concurrency is strictly between CPUs
 * -- you most likely do not need this.
 *
 * Where the traditional RCU/lockless data structures rely on atomic
 * modifications to ensure queries observe either the old or the new state the
 * latch allows the same for non-atomic updates. The trade-off is doubling the
 * cost of storage; we have to maintain two copies of the entire data
 * structure.
 *
 * Very simply put: we first modify one copy and then the other. This ensures
 * there is always one copy in a stable state, ready to give us an answer.
 *
 * The basic form is a data structure like::
 *
 *	struct latch_struct {
 *		seqcount_latch_t	seq;
 *		struct data_struct	data[2];
 *	};
 *
 * Where a modification, which is assumed to be externally serialized, does the
 * following::
 *
 *	void latch_modify(struct latch_struct *latch, ...)
 *	{
 *		smp_wmb();	// Ensure that the last data[1] update is visible
 *		latch->seq.sequence++;
 *		smp_wmb();	// Ensure that the seqcount update is visible
 *
 *		modify(latch->data[0], ...);
 *
 *		smp_wmb();	// Ensure that the data[0] update is visible
 *		latch->seq.sequence++;
 *		smp_wmb();	// Ensure that the seqcount update is visible
 *
 *		modify(latch->data[1], ...);
 *	}
 *
 * The query will have a form like::
 *
 *	struct entry *latch_query(struct latch_struct *latch, ...)
 *	{
 *		struct entry *entry;
 *		unsigned seq, idx;
 *
 *		do {
 *			seq = raw_read_seqcount_latch(&latch->seq);
 *
 *			idx = seq & 0x01;
 *			entry = data_query(latch->data[idx], ...);
 *
 *		// This includes needed smp_rmb()
 *		} while (read_seqcount_latch_retry(&latch->seq, seq));
 *
 *		return entry;
 *	}
 *
 * So during the modification, queries are first redirected to data[1]. Then we
 * modify data[0]. When that is complete, we redirect queries back to data[0]
 * and we can modify data[1].
 *
 * NOTE:
 *
 *	The non-requirement for atomic modifications does _NOT_ include
 *	the publishing of new entries in the case where data is a dynamic
 *	data structure.
 *
 *	An iteration might start in data[0] and get suspended long enough
 *	to miss an entire modification sequence, once it resumes it might
 *	observe the new entry.
 *
 * NOTE2:
 *
 *	When data is a dynamic data structure; one should use regular RCU
 *	patterns to manage the lifetimes of the objects within.
 */
static inline void raw_write_seqcount_latch(seqcount_latch_t *s)
{
	smp_wmb();	/* prior stores before incrementing "sequence" */
	s->seqcount.sequence++;
	smp_wmb();      /* increment "sequence" before following stores */
}

/*
 * Sequential locks (seqlock_t)
 *
 * Sequence counters with an embedded spinlock for writer serialization
 * and non-preemptibility.
 *
 * For more info, see:
 *    - Comments on top of seqcount_t
 *    - Documentation/locking/seqlock.rst
 */
typedef struct {
	/*
	 * Make sure that readers don't starve writers on PREEMPT_RT: use
	 * seqcount_spinlock_t instead of seqcount_t. Check __SEQ_LOCK().
	 */
	seqcount_spinlock_t seqcount;
	spinlock_t lock;
} seqlock_t;

#define __SEQLOCK_UNLOCKED(lockname)					\
	{								\
		.seqcount = SEQCNT_SPINLOCK_ZERO(lockname, &(lockname).lock), \
		.lock =	__SPIN_LOCK_UNLOCKED(lockname)			\
	}

/**
 * seqlock_init() - dynamic initializer for seqlock_t
 * @sl: Pointer to the seqlock_t instance
 */
#define seqlock_init(sl)						\
	do {								\
		spin_lock_init(&(sl)->lock);				\
		seqcount_spinlock_init(&(sl)->seqcount, &(sl)->lock);	\
	} while (0)

/**
 * DEFINE_SEQLOCK(sl) - Define a statically allocated seqlock_t
 * @sl: Name of the seqlock_t instance
 */
#define DEFINE_SEQLOCK(sl) \
		seqlock_t sl = __SEQLOCK_UNLOCKED(sl)

/**
 * read_seqbegin() - start a seqlock_t read side critical section
 * @sl: Pointer to seqlock_t
 *
 * Return: count, to be passed to read_seqretry()
 */
static inline unsigned read_seqbegin(const seqlock_t *sl)
{
	unsigned ret = read_seqcount_begin(&sl->seqcount);

	kcsan_atomic_next(0);  /* non-raw usage, assume closing read_seqretry() */
	kcsan_flat_atomic_begin();
	return ret;
}

/**
 * read_seqretry() - end a seqlock_t read side section
 * @sl: Pointer to seqlock_t
 * @start: count, from read_seqbegin()
 *
 * read_seqretry closes the read side critical section of given seqlock_t.
 * If the critical section was invalid, it must be ignored (and typically
 * retried).
 *
 * Return: true if a read section retry is required, else false
 */
static inline unsigned read_seqretry(const seqlock_t *sl, unsigned start)
{
	/*
	 * Assume not nested: read_seqretry() may be called multiple times when
	 * completing read critical section.
	 */
	kcsan_flat_atomic_end();

	return read_seqcount_retry(&sl->seqcount, start);
}

/*
 * For all seqlock_t write side functions, use write_seqcount_*t*_begin()
 * instead of the generic write_seqcount_begin(). This way, no redundant
 * lockdep_assert_held() checks are added.
 */

/**
 * write_seqlock() - start a seqlock_t write side critical section
 * @sl: Pointer to seqlock_t
 *
 * write_seqlock opens a write side critical section for the given
 * seqlock_t.  It also implicitly acquires the spinlock_t embedded inside
 * that sequential lock. All seqlock_t write side sections are thus
 * automatically serialized and non-preemptible.
 *
 * Context: if the seqlock_t read section, or other write side critical
 * sections, can be invoked from hardirq or softirq contexts, use the
 * _irqsave or _bh variants of this function instead.
 */
static inline void write_seqlock(seqlock_t *sl)
{
	spin_lock(&sl->lock);
	write_seqcount_t_begin(&sl->seqcount.seqcount);
}

/**
 * write_sequnlock() - end a seqlock_t write side critical section
 * @sl: Pointer to seqlock_t
 *
 * write_sequnlock closes the (serialized and non-preemptible) write side
 * critical section of given seqlock_t.
 */
static inline void write_sequnlock(seqlock_t *sl)
{
	write_seqcount_t_end(&sl->seqcount.seqcount);
	spin_unlock(&sl->lock);
}

/**
 * write_seqlock_bh() - start a softirqs-disabled seqlock_t write section
 * @sl: Pointer to seqlock_t
 *
 * _bh variant of write_seqlock(). Use only if the read side section, or
 * other write side sections, can be invoked from softirq contexts.
 */
static inline void write_seqlock_bh(seqlock_t *sl)
{
	spin_lock_bh(&sl->lock);
	write_seqcount_t_begin(&sl->seqcount.seqcount);
}

/**
 * write_sequnlock_bh() - end a softirqs-disabled seqlock_t write section
 * @sl: Pointer to seqlock_t
 *
 * write_sequnlock_bh closes the serialized, non-preemptible, and
 * softirqs-disabled, seqlock_t write side critical section opened with
 * write_seqlock_bh().
 */
static inline void write_sequnlock_bh(seqlock_t *sl)
{
	write_seqcount_t_end(&sl->seqcount.seqcount);
	spin_unlock_bh(&sl->lock);
}

/**
 * write_seqlock_irq() - start a non-interruptible seqlock_t write section
 * @sl: Pointer to seqlock_t
 *
 * _irq variant of write_seqlock(). Use only if the read side section, or
 * other write sections, can be invoked from hardirq contexts.
 */
static inline void write_seqlock_irq(seqlock_t *sl)
{
	spin_lock_irq(&sl->lock);
	write_seqcount_t_begin(&sl->seqcount.seqcount);
}

/**
 * write_sequnlock_irq() - end a non-interruptible seqlock_t write section
 * @sl: Pointer to seqlock_t
 *
 * write_sequnlock_irq closes the serialized and non-interruptible
 * seqlock_t write side section opened with write_seqlock_irq().
 */
static inline void write_sequnlock_irq(seqlock_t *sl)
{
	write_seqcount_t_end(&sl->seqcount.seqcount);
	spin_unlock_irq(&sl->lock);
}

static inline unsigned long __write_seqlock_irqsave(seqlock_t *sl)
{
	unsigned long flags;

	spin_lock_irqsave(&sl->lock, flags);
	write_seqcount_t_begin(&sl->seqcount.seqcount);
	return flags;
}

/**
 * write_seqlock_irqsave() - start a non-interruptible seqlock_t write
 *                           section
 * @lock:  Pointer to seqlock_t
 * @flags: Stack-allocated storage for saving caller's local interrupt
 *         state, to be passed to write_sequnlock_irqrestore().
 *
 * _irqsave variant of write_seqlock(). Use it only if the read side
 * section, or other write sections, can be invoked from hardirq context.
 */
#define write_seqlock_irqsave(lock, flags)				\
	do { flags = __write_seqlock_irqsave(lock); } while (0)

/**
 * write_sequnlock_irqrestore() - end non-interruptible seqlock_t write
 *                                section
 * @sl:    Pointer to seqlock_t
 * @flags: Caller's saved interrupt state, from write_seqlock_irqsave()
 *
 * write_sequnlock_irqrestore closes the serialized and non-interruptible
 * seqlock_t write section previously opened with write_seqlock_irqsave().
 */
static inline void
write_sequnlock_irqrestore(seqlock_t *sl, unsigned long flags)
{
	write_seqcount_t_end(&sl->seqcount.seqcount);
	spin_unlock_irqrestore(&sl->lock, flags);
}

/**
 * read_seqlock_excl() - begin a seqlock_t locking reader section
 * @sl:	Pointer to seqlock_t
 *
 * read_seqlock_excl opens a seqlock_t locking reader critical section.  A
 * locking reader exclusively locks out *both* other writers *and* other
 * locking readers, but it does not update the embedded sequence number.
 *
 * Locking readers act like a normal spin_lock()/spin_unlock().
 *
 * Context: if the seqlock_t write section, *or other read sections*, can
 * be invoked from hardirq or softirq contexts, use the _irqsave or _bh
 * variant of this function instead.
 *
 * The opened read section must be closed with read_sequnlock_excl().
 */
static inline void read_seqlock_excl(seqlock_t *sl)
{
	spin_lock(&sl->lock);
}

/**
 * read_sequnlock_excl() - end a seqlock_t locking reader critical section
 * @sl: Pointer to seqlock_t
 */
static inline void read_sequnlock_excl(seqlock_t *sl)
{
	spin_unlock(&sl->lock);
}

/**
 * read_seqlock_excl_bh() - start a seqlock_t locking reader section with
 *			    softirqs disabled
 * @sl: Pointer to seqlock_t
 *
 * _bh variant of read_seqlock_excl(). Use this variant only if the
 * seqlock_t write side section, *or other read sections*, can be invoked
 * from softirq contexts.
 */
static inline void read_seqlock_excl_bh(seqlock_t *sl)
{
	spin_lock_bh(&sl->lock);
}

/**
 * read_sequnlock_excl_bh() - stop a seqlock_t softirq-disabled locking
 *			      reader section
 * @sl: Pointer to seqlock_t
 */
static inline void read_sequnlock_excl_bh(seqlock_t *sl)
{
	spin_unlock_bh(&sl->lock);
}

/**
 * read_seqlock_excl_irq() - start a non-interruptible seqlock_t locking
 *			     reader section
 * @sl: Pointer to seqlock_t
 *
 * _irq variant of read_seqlock_excl(). Use this only if the seqlock_t
 * write side section, *or other read sections*, can be invoked from a
 * hardirq context.
 */
static inline void read_seqlock_excl_irq(seqlock_t *sl)
{
	spin_lock_irq(&sl->lock);
}

/**
 * read_sequnlock_excl_irq() - end an interrupts-disabled seqlock_t
 *                             locking reader section
 * @sl: Pointer to seqlock_t
 */
static inline void read_sequnlock_excl_irq(seqlock_t *sl)
{
	spin_unlock_irq(&sl->lock);
}

static inline unsigned long __read_seqlock_excl_irqsave(seqlock_t *sl)
{
	unsigned long flags;

	spin_lock_irqsave(&sl->lock, flags);
	return flags;
}

/**
 * read_seqlock_excl_irqsave() - start a non-interruptible seqlock_t
 *				 locking reader section
 * @lock:  Pointer to seqlock_t
 * @flags: Stack-allocated storage for saving caller's local interrupt
 *         state, to be passed to read_sequnlock_excl_irqrestore().
 *
 * _irqsave variant of read_seqlock_excl(). Use this only if the seqlock_t
 * write side section, *or other read sections*, can be invoked from a
 * hardirq context.
 */
#define read_seqlock_excl_irqsave(lock, flags)				\
	do { flags = __read_seqlock_excl_irqsave(lock); } while (0)

/**
 * read_sequnlock_excl_irqrestore() - end non-interruptible seqlock_t
 *				      locking reader section
 * @sl:    Pointer to seqlock_t
 * @flags: Caller saved interrupt state, from read_seqlock_excl_irqsave()
 */
static inline void
read_sequnlock_excl_irqrestore(seqlock_t *sl, unsigned long flags)
{
	spin_unlock_irqrestore(&sl->lock, flags);
}

/**
 * read_seqbegin_or_lock() - begin a seqlock_t lockless or locking reader
 * @lock: Pointer to seqlock_t
 * @seq : Marker and return parameter. If the passed value is even, the
 * reader will become a *lockless* seqlock_t reader as in read_seqbegin().
 * If the passed value is odd, the reader will become a *locking* reader
 * as in read_seqlock_excl().  In the first call to this function, the
 * caller *must* initialize and pass an even value to @seq; this way, a
 * lockless read can be optimistically tried first.
 *
 * read_seqbegin_or_lock is an API designed to optimistically try a normal
 * lockless seqlock_t read section first.  If an odd counter is found, the
 * lockless read trial has failed, and the next read iteration transforms
 * itself into a full seqlock_t locking reader.
 *
 * This is typically used to avoid seqlock_t lockless readers starvation
 * (too much retry loops) in the case of a sharp spike in write side
 * activity.
 *
 * Context: if the seqlock_t write section, *or other read sections*, can
 * be invoked from hardirq or softirq contexts, use the _irqsave or _bh
 * variant of this function instead.
 *
 * Check Documentation/locking/seqlock.rst for template example code.
 *
 * Return: the encountered sequence counter value, through the @seq
 * parameter, which is overloaded as a return parameter. This returned
 * value must be checked with need_seqretry(). If the read section need to
 * be retried, this returned value must also be passed as the @seq
 * parameter of the next read_seqbegin_or_lock() iteration.
 */
static inline void read_seqbegin_or_lock(seqlock_t *lock, int *seq)
{
	if (!(*seq & 1))	/* Even */
		*seq = read_seqbegin(lock);
	else			/* Odd */
		read_seqlock_excl(lock);
}

/**
 * need_seqretry() - validate seqlock_t "locking or lockless" read section
 * @lock: Pointer to seqlock_t
 * @seq: sequence count, from read_seqbegin_or_lock()
 *
 * Return: true if a read section retry is required, false otherwise
 */
static inline int need_seqretry(seqlock_t *lock, int seq)
{
	return !(seq & 1) && read_seqretry(lock, seq);
}

/**
 * done_seqretry() - end seqlock_t "locking or lockless" reader section
 * @lock: Pointer to seqlock_t
 * @seq: count, from read_seqbegin_or_lock()
 *
 * done_seqretry finishes the seqlock_t read side critical section started
 * with read_seqbegin_or_lock() and validated by need_seqretry().
 */
static inline void done_seqretry(seqlock_t *lock, int seq)
{
	if (seq & 1)
		read_sequnlock_excl(lock);
}

/**
 * read_seqbegin_or_lock_irqsave() - begin a seqlock_t lockless reader, or
 *                                   a non-interruptible locking reader
 * @lock: Pointer to seqlock_t
 * @seq:  Marker and return parameter. Check read_seqbegin_or_lock().
 *
 * This is the _irqsave variant of read_seqbegin_or_lock(). Use it only if
 * the seqlock_t write section, *or other read sections*, can be invoked
 * from hardirq context.
 *
 * Note: Interrupts will be disabled only for "locking reader" mode.
 *
 * Return:
 *
 *   1. The saved local interrupts state in case of a locking reader, to
 *      be passed to done_seqretry_irqrestore().
 *
 *   2. The encountered sequence counter value, returned through @seq
 *      overloaded as a return parameter. Check read_seqbegin_or_lock().
 */
static inline unsigned long
read_seqbegin_or_lock_irqsave(seqlock_t *lock, int *seq)
{
	unsigned long flags = 0;

	if (!(*seq & 1))	/* Even */
		*seq = read_seqbegin(lock);
	else			/* Odd */
		read_seqlock_excl_irqsave(lock, flags);

	return flags;
}

/**
 * done_seqretry_irqrestore() - end a seqlock_t lockless reader, or a
 *				non-interruptible locking reader section
 * @lock:  Pointer to seqlock_t
 * @seq:   Count, from read_seqbegin_or_lock_irqsave()
 * @flags: Caller's saved local interrupt state in case of a locking
 *	   reader, also from read_seqbegin_or_lock_irqsave()
 *
 * This is the _irqrestore variant of done_seqretry(). The read section
 * must've been opened with read_seqbegin_or_lock_irqsave(), and validated
 * by need_seqretry().
 */
static inline void
done_seqretry_irqrestore(seqlock_t *lock, int seq, unsigned long flags)
{
	if (seq & 1)
		read_sequnlock_excl_irqrestore(lock, flags);
}
#endif /* __LINUX_SEQLOCK_H */
