#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/spinlock_types.h>

static inline int __raw_spin_is_locked(raw_spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	return *a == 0;
}

#define __raw_spin_lock(lock) __raw_spin_lock_flags(lock, 0)
#define __raw_spin_unlock_wait(x) \
		do { cpu_relax(); } while (__raw_spin_is_locked(x))

static inline void __raw_spin_lock_flags(raw_spinlock_t *x,
					 unsigned long flags)
{
	volatile unsigned int *a;

	mb();
	a = __ldcw_align(x);
	while (__ldcw(a) == 0)
		while (*a == 0)
			if (flags & PSW_SM_I) {
				local_irq_enable();
				cpu_relax();
				local_irq_disable();
			} else
				cpu_relax();
	mb();
}

static inline void __raw_spin_unlock(raw_spinlock_t *x)
{
	volatile unsigned int *a;
	mb();
	a = __ldcw_align(x);
	*a = 1;
	mb();
}

static inline int __raw_spin_trylock(raw_spinlock_t *x)
{
	volatile unsigned int *a;
	int ret;

	mb();
	a = __ldcw_align(x);
        ret = __ldcw(a) != 0;
	mb();

	return ret;
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 */

#define __raw_read_trylock(lock) generic__raw_read_trylock(lock)

/* read_lock, read_unlock are pretty straightforward.  Of course it somehow
 * sucks we end up saving/restoring flags twice for read_lock_irqsave aso. */

static  __inline__ void __raw_read_lock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);

	rw->counter++;

	__raw_spin_unlock(&rw->lock);
}

static  __inline__ void __raw_read_unlock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);

	rw->counter--;

	__raw_spin_unlock(&rw->lock);
}

/* write_lock is less trivial.  We optimistically grab the lock and check
 * if we surprised any readers.  If so we release the lock and wait till
 * they're all gone before trying again
 *
 * Also note that we don't use the _irqsave / _irqrestore suffixes here.
 * If we're called with interrupts enabled and we've got readers (or other
 * writers) in interrupt handlers someone fucked up and we'd dead-lock
 * sooner or later anyway.   prumpf */

static  __inline__ void __raw_write_lock(raw_rwlock_t *rw)
{
retry:
	__raw_spin_lock(&rw->lock);

	if(rw->counter != 0) {
		/* this basically never happens */
		__raw_spin_unlock(&rw->lock);

		while (rw->counter != 0)
			cpu_relax();

		goto retry;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
}

/* write_unlock is absolutely trivial - we don't have to wait for anything */

static  __inline__ void __raw_write_unlock(raw_rwlock_t *rw)
{
	rw->counter = 0;
	__raw_spin_unlock(&rw->lock);
}

static  __inline__ int __raw_write_trylock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);
	if (rw->counter != 0) {
		/* this basically never happens */
		__raw_spin_unlock(&rw->lock);

		return 0;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
	return 1;
}

/*
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
static __inline__ int __raw_read_can_lock(raw_rwlock_t *rw)
{
	return rw->counter >= 0;
}

/*
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
static __inline__ int __raw_write_can_lock(raw_rwlock_t *rw)
{
	return !rw->counter;
}

#endif /* __ASM_SPINLOCK_H */
