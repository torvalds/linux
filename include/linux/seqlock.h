#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H
/*
 * Reader/writer consistent mechanism without starving writers. This type of
 * lock for data where the reader wants a consitent set of information
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

typedef struct {
	unsigned sequence;
	spinlock_t lock;
} seqlock_t;

/*
 * These macros triggered gcc-3.x compile-time problems.  We think these are
 * OK now.  Be cautious.
 */
#define SEQLOCK_UNLOCKED { 0, SPIN_LOCK_UNLOCKED }
#define seqlock_init(x)	do { *(x) = (seqlock_t) SEQLOCK_UNLOCKED; } while (0)


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

static inline int write_tryseqlock(seqlock_t *sl)
{
	int ret = spin_trylock(&sl->lock);

	if (ret) {
		++sl->sequence;
		smp_wmb();			
	}
	return ret;
}

/* Start of read calculation -- fetch last complete writer token */
static __always_inline unsigned read_seqbegin(const seqlock_t *sl)
{
	unsigned ret = sl->sequence;
	smp_rmb();
	return ret;
}

/* Test if reader processed invalid data.
 * If initial values is odd, 
 *	then writer had already started when section was entered
 * If sequence value changed
 *	then writer changed data while in section
 *    
 * Using xor saves one conditional branch.
 */
static __always_inline int read_seqretry(const seqlock_t *sl, unsigned iv)
{
	smp_rmb();
	return (iv & 1) | (sl->sequence ^ iv);
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

/* Start of read using pointer to a sequence counter only.  */
static inline unsigned read_seqcount_begin(const seqcount_t *s)
{
	unsigned ret = s->sequence;
	smp_rmb();
	return ret;
}

/* Test if reader processed invalid data.
 * Equivalent to: iv is odd or sequence number has changed.
 *                (iv & 1) || (*s != iv)
 * Using xor saves one conditional branch.
 */
static inline int read_seqcount_retry(const seqcount_t *s, unsigned iv)
{
	smp_rmb();
	return (iv & 1) | (s->sequence ^ iv);
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

#define read_seqbegin_irqsave(lock, flags)				\
	({ local_irq_save(flags);   read_seqbegin(lock); })

#define read_seqretry_irqrestore(lock, iv, flags)			\
	({								\
		int ret = read_seqretry(lock, iv);			\
		local_irq_restore(flags);				\
		ret;							\
	})

#endif /* __LINUX_SEQLOCK_H */
