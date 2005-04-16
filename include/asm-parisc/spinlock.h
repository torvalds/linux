#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>

/* Note that PA-RISC has to use `1' to mean unlocked and `0' to mean locked
 * since it only has load-and-zero. Moreover, at least on some PA processors,
 * the semaphore address has to be 16-byte aligned.
 */

#ifndef CONFIG_DEBUG_SPINLOCK

#define __SPIN_LOCK_UNLOCKED	{ { 1, 1, 1, 1 } }
#undef SPIN_LOCK_UNLOCKED
#define SPIN_LOCK_UNLOCKED (spinlock_t) __SPIN_LOCK_UNLOCKED

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

static inline int spin_is_locked(spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	return *a == 0;
}

#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

static inline void _raw_spin_lock(spinlock_t *x)
{
	volatile unsigned int *a;

	mb();
	a = __ldcw_align(x);
	while (__ldcw(a) == 0)
		while (*a == 0);
	mb();
}

static inline void _raw_spin_unlock(spinlock_t *x)
{
	volatile unsigned int *a;
	mb();
	a = __ldcw_align(x);
	*a = 1;
	mb();
}

static inline int _raw_spin_trylock(spinlock_t *x)
{
	volatile unsigned int *a;
	int ret;

	mb();
	a = __ldcw_align(x);
        ret = __ldcw(a) != 0;
	mb();

	return ret;
}
	
#define spin_lock_own(LOCK, LOCATION)	((void)0)

#else /* !(CONFIG_DEBUG_SPINLOCK) */

#define SPINLOCK_MAGIC	0x1D244B3C

#define __SPIN_LOCK_UNLOCKED	{ { 1, 1, 1, 1 }, SPINLOCK_MAGIC, 10, __FILE__ , NULL, 0, -1, NULL, NULL }
#undef SPIN_LOCK_UNLOCKED
#define SPIN_LOCK_UNLOCKED (spinlock_t) __SPIN_LOCK_UNLOCKED

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

#define CHECK_LOCK(x)							\
	do {								\
	 	if (unlikely((x)->magic != SPINLOCK_MAGIC)) {			\
			printk(KERN_ERR "%s:%d: spin_is_locked"		\
			" on uninitialized spinlock %p.\n",		\
				__FILE__, __LINE__, (x)); 		\
		} 							\
	} while(0)

#define spin_is_locked(x)						\
	({								\
	 	CHECK_LOCK(x);						\
		volatile unsigned int *a = __ldcw_align(x);		\
		if (unlikely((*a == 0) && (x)->babble)) {				\
			(x)->babble--;					\
			printk("KERN_WARNING				\
				%s:%d: spin_is_locked(%s/%p) already"	\
				" locked by %s:%d in %s at %p(%d)\n",	\
				__FILE__,__LINE__, (x)->module,	(x),	\
				(x)->bfile, (x)->bline, (x)->task->comm,\
				(x)->previous, (x)->oncpu);		\
		}							\
		*a == 0;						\
	})

#define spin_unlock_wait(x)						\
	do {								\
	 	CHECK_LOCK(x);						\
		volatile unsigned int *a = __ldcw_align(x);		\
		if (unlikely((*a == 0) && (x)->babble)) {				\
			(x)->babble--;					\
			printk("KERN_WARNING				\
				%s:%d: spin_unlock_wait(%s/%p)"		\
				" owned by %s:%d in %s at %p(%d)\n",	\
				__FILE__,__LINE__, (x)->module, (x),	\
				(x)->bfile, (x)->bline, (x)->task->comm,\
				(x)->previous, (x)->oncpu);		\
		}							\
		barrier();						\
	} while (*((volatile unsigned char *)(__ldcw_align(x))) == 0)

extern void _dbg_spin_lock(spinlock_t *lock, const char *base_file, int line_no);
extern void _dbg_spin_unlock(spinlock_t *lock, const char *, int);
extern int _dbg_spin_trylock(spinlock_t * lock, const char *, int);

#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

#define _raw_spin_unlock(lock)	_dbg_spin_unlock(lock, __FILE__, __LINE__)
#define _raw_spin_lock(lock) _dbg_spin_lock(lock, __FILE__, __LINE__)
#define _raw_spin_trylock(lock) _dbg_spin_trylock(lock, __FILE__, __LINE__)

/* just in case we need it */
#define spin_lock_own(LOCK, LOCATION)					\
do {									\
	volatile unsigned int *a = __ldcw_align(LOCK);			\
	if (!((*a == 0) && ((LOCK)->oncpu == smp_processor_id())))	\
		printk("KERN_WARNING					\
			%s: called on %d from %p but lock %s on %d\n",	\
			LOCATION, smp_processor_id(),			\
			__builtin_return_address(0),			\
			(*a == 0) ? "taken" : "freed", (LOCK)->on_cpu);	\
} while (0)

#endif /* !(CONFIG_DEBUG_SPINLOCK) */

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 */
typedef struct {
	spinlock_t lock;
	volatile int counter;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { __SPIN_LOCK_UNLOCKED, 0 }

#define rwlock_init(lp)	do { *(lp) = RW_LOCK_UNLOCKED; } while (0)

#define _raw_read_trylock(lock) generic_raw_read_trylock(lock)

/* read_lock, read_unlock are pretty straightforward.  Of course it somehow
 * sucks we end up saving/restoring flags twice for read_lock_irqsave aso. */

#ifdef CONFIG_DEBUG_RWLOCK
extern void _dbg_read_lock(rwlock_t * rw, const char *bfile, int bline);
#define _raw_read_lock(rw) _dbg_read_lock(rw, __FILE__, __LINE__)
#else
static  __inline__ void _raw_read_lock(rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	_raw_spin_lock(&rw->lock); 

	rw->counter++;

	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}
#endif	/* CONFIG_DEBUG_RWLOCK */

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

#ifdef CONFIG_DEBUG_RWLOCK
extern void _dbg_write_lock(rwlock_t * rw, const char *bfile, int bline);
#define _raw_write_lock(rw) _dbg_write_lock(rw, __FILE__, __LINE__)
#else
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
#endif /* CONFIG_DEBUG_RWLOCK */

/* write_unlock is absolutely trivial - we don't have to wait for anything */

static  __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	rw->counter = 0;
	_raw_spin_unlock(&rw->lock);
}

#ifdef CONFIG_DEBUG_RWLOCK
extern int _dbg_write_trylock(rwlock_t * rw, const char *bfile, int bline);
#define _raw_write_trylock(rw) _dbg_write_trylock(rw, __FILE__, __LINE__)
#else
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
#endif /* CONFIG_DEBUG_RWLOCK */

static __inline__ int is_read_locked(rwlock_t *rw)
{
	return rw->counter > 0;
}

static __inline__ int is_write_locked(rwlock_t *rw)
{
	return rw->counter < 0;
}

#endif /* __ASM_SPINLOCK_H */
