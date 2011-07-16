#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H
/*
 * Reader/writer consistent mechanism without starving writers. This type of
 * lock for data where the reader wants a consistent set of information
 * and is willing to retry if the information changes.  Readers never
 * block but they may have to retry if a writer is in
 * progress. Writers do not wait for readers. 
 *
 * This is not as cache friendly as brlock. Also, this will not work
 * for data that contains pointers, because any writer could
 * invalidate a pointer that a reader was following.
 *
 * Expected reader usage:
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
#include <asm/processor.h>

typedef struct {
	unsigned sequence;
	spinlock_t lock;
} seqlock_t;

/*
 * These macros triggered gcc-3.x compile-time problems.  We think these are
 * OK now.  Be cautious.
 */
#define __SEQLOCK_UNLOCKED(lockname) \
		 { 0, __SPIN_LOCK_UNLOCKED(lockname) }

#define seqlock_init(x)					\
	do {						\
		(x)->sequence = 0;			\
		spin_lock_init(&(x)->lock);		\
	} while (0)

#define DEFINE_SEQLOCK(x) \
		seqlock_t x = __SEQLOCK_UNLOCKED(x)

/* Lock out other writers and update the count.
 * Acts like a normal spin_lock/unlock.
 * Don't need preempt_disable() because that is in the spin_lock already.
 */
static inline void write_seqlock(seqlock_t *sl)
{
	spin_lock(&sl->lock);
	++sl->sequence;
	smp_wmb();
}

static inline void write_sequnlock(seqlock_t *sl)
{
	smp_wmb();
	sl->sequence++;
	spin_unlock(&sl->lock);
}

/* Start of read calculation -- fetch last complete writer token */
static __always_inline unsigned read_seqbegin(const seqlock_t *sl)
{
	unsigned ret;

repeat:
	ret = ACCESS_ONCE(sl->sequence);
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
	smp_rmb();

	return ret;
}

/*
 * Test if reader processed invalid data.
 *
 * If sequence value changed then writer changed data while in section.
 */
static __always_inline int read_seqretry(const seqlock_t *sl, unsigned start)
{
	smp_rmb();

	return unlikely(sl->sequence != start);
}


/*
 * Version using sequence counter only.
 * This can be used when code has its own mutex protecting the
 * updating starting before the write_seqcountbeqin() and ending
 * after the write_seqcount_end().
 */

typedef struct seqcount {
	unsigned sequence;
} seqcount_t;

#define SEQCNT_ZERO { 0 }
#define seqcount_init(x)	do { *(x) = (seqcount_t) SEQCNT_ZERO; } while (0)

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
	ret = ACCESS_ONCE(s->sequence);
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
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
	unsigned ret = __read_seqcount_begin(s);
	smp_rmb();
	return ret;
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
	unsigned ret = ACCESS_ONCE(s->sequence);
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


/*
 * Sequence counter only version assumes that callers are using their
 * own mutexing.
 */
static inline void write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

static inline void write_seqcount_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

/**
 * write_seqcount_barrier - invalidate in-progress read-side seq operations
 * @s: pointer to seqcount_t
 *
 * After write_seqcount_barrier, no read-side seq operations will complete
 * successfully and see data older than this.
 */
static inline void write_seqcount_barrier(seqcount_t *s)
{
	smp_wmb();
	s->sequence+=2;
}

/*
 * Possible sw/hw IRQ protected versions of the interfaces.
 */
#define write_seqlock_irqsave(lock, flags)				\
	do { local_irq_save(flags); write_seqlock(lock); } while (0)
#define write_seqlock_irq(lock)						\
	do { local_irq_disable();   write_seqlock(lock); } while (0)
#define write_seqlock_bh(lock)						\
        do { local_bh_disable();    write_seqlock(lock); } while (0)

#define write_sequnlock_irqrestore(lock, flags)				\
	do { write_sequnlock(lock); local_irq_restore(flags); } while(0)
#define write_sequnlock_irq(lock)					\
	do { write_sequnlock(lock); local_irq_enable(); } while(0)
#define write_sequnlock_bh(lock)					\
	do { write_sequnlock(lock); local_bh_enable(); } while(0)

#endif /* __LINUX_SEQLOCK_H */
