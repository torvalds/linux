/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H
/*
 * Reader/writer consistent mechanism without starving writers. This type of
 * lock for data where the reader wants a consistent set of information
 * and is willing to retry if the information changes. There are two types
 * of readers:
 * 1. Sequence readers which never block a writer but they may have to retry
 *    if a writer is in progress by detecting change in sequence number.
 *    Writers do not wait for a sequence reader.
 * 2. Locking readers which will wait if a writer or another locking reader
 *    is in progress. A locking reader in progress will also block a writer
 *    from going forward. Unlike the regular rwlock, the read lock here is
 *    exclusive so that only one locking reader can get it.
 *
 * This is not as cache friendly as brlock. Also, this may not work well
 * for data that contains pointers, because any writer could
 * invalidate a pointer that a reader was following.
 *
 * Expected non-blocking reader usage:
 * 	do {
 *	    seq = read_seqbegin(&foo);
 * 	...
 *      } while (read_seqretry(&foo, seq));
 *
 *
 * On non-SMP the spin locks disappear but the writer still needs
 * to increment the sequence variables because an interrupt routine could
 * change the state of the data.
 *
 * Based on x86_64 vsyscall gettimeofday 
 * by Keith Owens and Andrea Arcangeli
 */

#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <asm/processor.h>

/*
 * Version using sequence counter only.
 * This can be used when code has its own mutex protecting the
 * updating starting before the write_seqcountbeqin() and ending
 * after the write_seqcount_end().
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
# define SEQCOUNT_DEP_MAP_INIT(lockname) \
		.dep_map = { .name = #lockname } \

# define seqcount_init(s)				\
	do {						\
		static struct lock_class_key __key;	\
		__seqcount_init((s), #s, &__key);	\
	} while (0)

static inline void seqcount_lockdep_reader_access(const seqcount_t *s)
{
	seqcount_t *l = (seqcount_t *)s;
	unsigned long flags;

	local_irq_save(flags);
	seqcount_acquire_read(&l->dep_map, 0, 0, _RET_IP_);
	seqcount_release(&l->dep_map, 1, _RET_IP_);
	local_irq_restore(flags);
}

#else
# define SEQCOUNT_DEP_MAP_INIT(lockname)
# define seqcount_init(s) __seqcount_init(s, NULL, NULL)
# define seqcount_lockdep_reader_access(x)
#endif

#define SEQCNT_ZERO(lockname) { .sequence = 0, SEQCOUNT_DEP_MAP_INIT(lockname)}


/**
 * __read_seqcount_begin - begin a seq-read critical section (without barrier)
 * @s: pointer to seqcount_t
 * Returns: count to be passed to read_seqcount_retry
 *
 * __read_seqcount_begin is like read_seqcount_begin, but has no smp_rmb()
 * barrier. Callers should ensure that smp_rmb() or equivalent ordering is
 * provided before actually loading any of the variables that are to be
 * protected in this critical section.
 *
 * Use carefully, only in critical code, and comment how the barrier is
 * provided.
 */
static inline unsigned __read_seqcount_begin(const seqcount_t *s)
{
	unsigned ret;

repeat:
	ret = READ_ONCE(s->sequence);
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
	return ret;
}

/**
 * raw_read_seqcount - Read the raw seqcount
 * @s: pointer to seqcount_t
 * Returns: count to be passed to read_seqcount_retry
 *
 * raw_read_seqcount opens a read critical section of the given
 * seqcount without any lockdep checking and without checking or
 * masking the LSB. Calling code is responsible for handling that.
 */
static inline unsigned raw_read_seqcount(const seqcount_t *s)
{
	unsigned ret = READ_ONCE(s->sequence);
	smp_rmb();
	return ret;
}

/**
 * raw_read_seqcount_begin - start seq-read critical section w/o lockdep
 * @s: pointer to seqcount_t
 * Returns: count to be passed to read_seqcount_retry
 *
 * raw_read_seqcount_begin opens a read critical section of the given
 * seqcount, but without any lockdep checking. Validity of the critical
 * section is tested by checking read_seqcount_retry function.
 */
static inline unsigned raw_read_seqcount_begin(const seqcount_t *s)
{
	unsigned ret = __read_seqcount_begin(s);
	smp_rmb();
	return ret;
}

/**
 * read_seqcount_begin - begin a seq-read critical section
 * @s: pointer to seqcount_t
 * Returns: count to be passed to read_seqcount_retry
 *
 * read_seqcount_begin opens a read critical section of the given seqcount.
 * Validity of the critical section is tested by checking read_seqcount_retry
 * function.
 */
static inline unsigned read_seqcount_begin(const seqcount_t *s)
{
	seqcount_lockdep_reader_access(s);
	return raw_read_seqcount_begin(s);
}

/**
 * raw_seqcount_begin - begin a seq-read critical section
 * @s: pointer to seqcount_t
 * Returns: count to be passed to read_seqcount_retry
 *
 * raw_seqcount_begin opens a read critical section of the given seqcount.
 * Validity of the critical section is tested by checking read_seqcount_retry
 * function.
 *
 * Unlike read_seqcount_begin(), this function will not wait for the count
 * to stabilize. If a writer is active when we begin, we will fail the
 * read_seqcount_retry() instead of stabilizing at the beginning of the
 * critical section.
 */
static inline unsigned raw_seqcount_begin(const seqcount_t *s)
{
	unsigned ret = READ_ONCE(s->sequence);
	smp_rmb();
	return ret & ~1;
}

/**
 * __read_seqcount_retry - end a seq-read critical section (without barrier)
 * @s: pointer to seqcount_t
 * @start: count, from read_seqcount_begin
 * Returns: 1 if retry is required, else 0
 *
 * __read_seqcount_retry is like read_seqcount_retry, but has no smp_rmb()
 * barrier. Callers should ensure that smp_rmb() or equivalent ordering is
 * provided before actually loading any of the variables that are to be
 * protected in this critical section.
 *
 * Use carefully, only in critical code, and comment how the barrier is
 * provided.
 */
static inline int __read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	return unlikely(s->sequence != start);
}

/**
 * read_seqcount_retry - end a seq-read critical section
 * @s: pointer to seqcount_t
 * @start: count, from read_seqcount_begin
 * Returns: 1 if retry is required, else 0
 *
 * read_seqcount_retry closes a read critical section of the given seqcount.
 * If the critical section was invalid, it must be ignored (and typically
 * retried).
 */
static inline int read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	smp_rmb();
	return __read_seqcount_retry(s, start);
}



static inline void raw_write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

static inline void raw_write_seqcount_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

/**
 * raw_write_seqcount_barrier - do a seq write barrier
 * @s: pointer to seqcount_t
 *
 * This can be used to provide an ordering guarantee instead of the
 * usual consistency guarantee. It is one wmb cheaper, because we can
 * collapse the two back-to-back wmb()s.
 *
 *      seqcount_t seq;
 *      bool X = true, Y = false;
 *
 *      void read(void)
 *      {
 *              bool x, y;
 *
 *              do {
 *                      int s = read_seqcount_begin(&seq);
 *
 *                      x = X; y = Y;
 *
 *              } while (read_seqcount_retry(&seq, s));
 *
 *              BUG_ON(!x && !y);
 *      }
 *
 *      void write(void)
 *      {
 *              Y = true;
 *
 *              raw_write_seqcount_barrier(seq);
 *
 *              X = false;
 *      }
 */
static inline void raw_write_seqcount_barrier(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
	s->sequence++;
}

static inline int raw_read_seqcount_latch(seqcount_t *s)
{
	int seq = READ_ONCE(s->sequence);
	/* Pairs with the first smp_wmb() in raw_write_seqcount_latch() */
	smp_read_barrier_depends();
	return seq;
}

/**
 * raw_write_seqcount_latch - redirect readers to even/odd copy
 * @s: pointer to seqcount_t
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
 * The basic form is a data structure like:
 *
 * struct latch_struct {
 *	seqcount_t		seq;
 *	struct data_struct	data[2];
 * };
 *
 * Where a modification, which is assumed to be externally serialized, does the
 * following:
 *
 * void latch_modify(struct latch_struct *latch, ...)
 * {
 *	smp_wmb();	<- Ensure that the last data[1] update is visible
 *	latch->seq++;
 *	smp_wmb();	<- Ensure that the seqcount update is visible
 *
 *	modify(latch->data[0], ...);
 *
 *	smp_wmb();	<- Ensure that the data[0] update is visible
 *	latch->seq++;
 *	smp_wmb();	<- Ensure that the seqcount update is visible
 *
 *	modify(latch->data[1], ...);
 * }
 *
 * The query will have a form like:
 *
 * struct entry *latch_query(struct latch_struct *latch, ...)
 * {
 *	struct entry *entry;
 *	unsigned seq, idx;
 *
 *	do {
 *		seq = raw_read_seqcount_latch(&latch->seq);
 *
 *		idx = seq & 0x01;
 *		entry = data_query(latch->data[idx], ...);
 *
 *		smp_rmb();
 *	} while (seq != latch->seq);
 *
 *	return entry;
 * }
 *
 * So during the modification, queries are first redirected to data[1]. Then we
 * modify data[0]. When that is complete, we redirect queries back to data[0]
 * and we can modify data[1].
 *
 * NOTE: The non-requirement for atomic modifications does _NOT_ include
 *       the publishing of new entries in the case where data is a dynamic
 *       data structure.
 *
 *       An iteration might start in data[0] and get suspended long enough
 *       to miss an entire modification sequence, once it resumes it might
 *       observe the new entry.
 *
 * NOTE: When data is a dynamic data structure; one should use regular RCU
 *       patterns to manage the lifetimes of the objects within.
 */
static inline void raw_write_seqcount_latch(seqcount_t *s)
{
       smp_wmb();      /* prior stores before incrementing "sequence" */
       s->sequence++;
       smp_wmb();      /* increment "sequence" before following stores */
}

/*
 * Sequence counter only version assumes that callers are using their
 * own mutexing.
 */
static inline void write_seqcount_begin_nested(seqcount_t *s, int subclass)
{
	raw_write_seqcount_begin(s);
	seqcount_acquire(&s->dep_map, subclass, 0, _RET_IP_);
}

static inline void write_seqcount_begin(seqcount_t *s)
{
	write_seqcount_begin_nested(s, 0);
}

static inline void write_seqcount_end(seqcount_t *s)
{
	seqcount_release(&s->dep_map, 1, _RET_IP_);
	raw_write_seqcount_end(s);
}

/**
 * write_seqcount_invalidate - invalidate in-progress read-side seq operations
 * @s: pointer to seqcount_t
 *
 * After write_seqcount_invalidate, no read-side seq operations will complete
 * successfully and see data older than this.
 */
static inline void write_seqcount_invalidate(seqcount_t *s)
{
	smp_wmb();
	s->sequence+=2;
}

typedef struct {
	struct seqcount seqcount;
	spinlock_t lock;
} seqlock_t;

/*
 * These macros triggered gcc-3.x compile-time problems.  We think these are
 * OK now.  Be cautious.
 */
#define __SEQLOCK_UNLOCKED(lockname)			\
	{						\
		.seqcount = SEQCNT_ZERO(lockname),	\
		.lock =	__SPIN_LOCK_UNLOCKED(lockname)	\
	}

#define seqlock_init(x)					\
	do {						\
		seqcount_init(&(x)->seqcount);		\
		spin_lock_init(&(x)->lock);		\
	} while (0)

#define DEFINE_SEQLOCK(x) \
		seqlock_t x = __SEQLOCK_UNLOCKED(x)

/*
 * Read side functions for starting and finalizing a read side section.
 */
static inline unsigned read_seqbegin(const seqlock_t *sl)
{
	return read_seqcount_begin(&sl->seqcount);
}

static inline unsigned read_seqretry(const seqlock_t *sl, unsigned start)
{
	return read_seqcount_retry(&sl->seqcount, start);
}

/*
 * Lock out other writers and update the count.
 * Acts like a normal spin_lock/unlock.
 * Don't need preempt_disable() because that is in the spin_lock already.
 */
static inline void write_seqlock(seqlock_t *sl)
{
	spin_lock(&sl->lock);
	write_seqcount_begin(&sl->seqcount);
}

static inline void write_sequnlock(seqlock_t *sl)
{
	write_seqcount_end(&sl->seqcount);
	spin_unlock(&sl->lock);
}

static inline void write_seqlock_bh(seqlock_t *sl)
{
	spin_lock_bh(&sl->lock);
	write_seqcount_begin(&sl->seqcount);
}

static inline void write_sequnlock_bh(seqlock_t *sl)
{
	write_seqcount_end(&sl->seqcount);
	spin_unlock_bh(&sl->lock);
}

static inline void write_seqlock_irq(seqlock_t *sl)
{
	spin_lock_irq(&sl->lock);
	write_seqcount_begin(&sl->seqcount);
}

static inline void write_sequnlock_irq(seqlock_t *sl)
{
	write_seqcount_end(&sl->seqcount);
	spin_unlock_irq(&sl->lock);
}

static inline unsigned long __write_seqlock_irqsave(seqlock_t *sl)
{
	unsigned long flags;

	spin_lock_irqsave(&sl->lock, flags);
	write_seqcount_begin(&sl->seqcount);
	return flags;
}

#define write_seqlock_irqsave(lock, flags)				\
	do { flags = __write_seqlock_irqsave(lock); } while (0)

static inline void
write_sequnlock_irqrestore(seqlock_t *sl, unsigned long flags)
{
	write_seqcount_end(&sl->seqcount);
	spin_unlock_irqrestore(&sl->lock, flags);
}

/*
 * A locking reader exclusively locks out other writers and locking readers,
 * but doesn't update the sequence number. Acts like a normal spin_lock/unlock.
 * Don't need preempt_disable() because that is in the spin_lock already.
 */
static inline void read_seqlock_excl(seqlock_t *sl)
{
	spin_lock(&sl->lock);
}

static inline void read_sequnlock_excl(seqlock_t *sl)
{
	spin_unlock(&sl->lock);
}

/**
 * read_seqbegin_or_lock - begin a sequence number check or locking block
 * @lock: sequence lock
 * @seq : sequence number to be checked
 *
 * First try it once optimistically without taking the lock. If that fails,
 * take the lock. The sequence number is also used as a marker for deciding
 * whether to be a reader (even) or writer (odd).
 * N.B. seq must be initialized to an even number to begin with.
 */
static inline void read_seqbegin_or_lock(seqlock_t *lock, int *seq)
{
	if (!(*seq & 1))	/* Even */
		*seq = read_seqbegin(lock);
	else			/* Odd */
		read_seqlock_excl(lock);
}

static inline int need_seqretry(seqlock_t *lock, int seq)
{
	return !(seq & 1) && read_seqretry(lock, seq);
}

static inline void done_seqretry(seqlock_t *lock, int seq)
{
	if (seq & 1)
		read_sequnlock_excl(lock);
}

static inline void read_seqlock_excl_bh(seqlock_t *sl)
{
	spin_lock_bh(&sl->lock);
}

static inline void read_sequnlock_excl_bh(seqlock_t *sl)
{
	spin_unlock_bh(&sl->lock);
}

static inline void read_seqlock_excl_irq(seqlock_t *sl)
{
	spin_lock_irq(&sl->lock);
}

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

#define read_seqlock_excl_irqsave(lock, flags)				\
	do { flags = __read_seqlock_excl_irqsave(lock); } while (0)

static inline void
read_sequnlock_excl_irqrestore(seqlock_t *sl, unsigned long flags)
{
	spin_unlock_irqrestore(&sl->lock, flags);
}

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

static inline void
done_seqretry_irqrestore(seqlock_t *lock, int seq, unsigned long flags)
{
	if (seq & 1)
		read_sequnlock_excl_irqrestore(lock, flags);
}
#endif /* __LINUX_SEQLOCK_H */
