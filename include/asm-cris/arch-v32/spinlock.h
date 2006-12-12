#ifndef __ASM_ARCH_SPINLOCK_H
#define __ASM_ARCH_SPINLOCK_H

#include <asm/system.h>

#define RW_LOCK_BIAS 0x01000000
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 1 }
#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

#define spin_is_locked(x)	(*(volatile signed char *)(&(x)->lock) <= 0)
#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))

extern void cris_spin_unlock(void *l, int val);
extern void cris_spin_lock(void *l);
extern int cris_spin_trylock(void* l);

static inline void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ volatile ("move.d %1,%0" \
	                  : "=m" (lock->lock) \
			  : "r" (1) \
			  : "memory");
}

static inline int _raw_spin_trylock(spinlock_t *lock)
{
	return cris_spin_trylock((void*)&lock->lock);
}

static inline void _raw_spin_lock(spinlock_t *lock)
{
	cris_spin_lock((void*)&lock->lock);
}

static inline void _raw_spin_lock_flags (spinlock_t *lock, unsigned long flags)
{
  _raw_spin_lock(lock);
}

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct {
	spinlock_t lock;
	volatile int counter;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { {1}, 0 }

#define rwlock_init(lp)	do { *(lp) = RW_LOCK_UNLOCKED; } while (0)

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define read_can_lock(x) ((int)(x)->counter >= 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define write_can_lock(x) ((x)->counter == 0)

#define _raw_read_trylock(lock) generic_raw_read_trylock(lock)

/* read_lock, read_unlock are pretty straightforward.  Of course it somehow
 * sucks we end up saving/restoring flags twice for read_lock_irqsave aso. */

static  __inline__ void _raw_read_lock(rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	_raw_spin_lock(&rw->lock);

	rw->counter++;

	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

static  __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	_raw_spin_lock(&rw->lock);

	rw->counter--;

	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

/* write_lock is less trivial.  We optimistically grab the lock and check
 * if we surprised any readers.  If so we release the lock and wait till
 * they're all gone before trying again
 *
 * Also note that we don't use the _irqsave / _irqrestore suffixes here.
 * If we're called with interrupts enabled and we've got readers (or other
 * writers) in interrupt handlers someone fucked up and we'd dead-lock
 * sooner or later anyway.   prumpf */

static  __inline__ void _raw_write_lock(rwlock_t *rw)
{
retry:
	_raw_spin_lock(&rw->lock);

	if(rw->counter != 0) {
		/* this basically never happens */
		_raw_spin_unlock(&rw->lock);

		while(rw->counter != 0);

		goto retry;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
}

/* write_unlock is absolutely trivial - we don't have to wait for anything */

static  __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	rw->counter = 0;
	_raw_spin_unlock(&rw->lock);
}

static  __inline__ int _raw_write_trylock(rwlock_t *rw)
{
	_raw_spin_lock(&rw->lock);
	if (rw->counter != 0) {
		/* this basically never happens */
		_raw_spin_unlock(&rw->lock);

		return 0;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
	return 1;
}

static __inline__ int is_read_locked(rwlock_t *rw)
{
	return rw->counter > 0;
}

static __inline__ int is_write_locked(rwlock_t *rw)
{
	return rw->counter < 0;
}

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* __ASM_ARCH_SPINLOCK_H */
