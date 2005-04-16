#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic locking declarations
 */

#include <linux/config.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>

#include <asm/processor.h>	/* for cpu relax */
#include <asm/system.h>

/*
 * Must define these before including other files, inline functions need them
 */
#define LOCK_SECTION_NAME                       \
        ".text.lock." __stringify(KBUILD_BASENAME)

#define LOCK_SECTION_START(extra)               \
        ".subsection 1\n\t"                     \
        extra                                   \
        ".ifndef " LOCK_SECTION_NAME "\n\t"     \
        LOCK_SECTION_NAME ":\n\t"               \
        ".endif\n"

#define LOCK_SECTION_END                        \
        ".previous\n\t"

#define __lockfunc fastcall __attribute__((section(".spinlock.text")))

/*
 * If CONFIG_SMP is set, pull in the _raw_* definitions
 */
#ifdef CONFIG_SMP

#define assert_spin_locked(x)	BUG_ON(!spin_is_locked(x))
#include <asm/spinlock.h>

int __lockfunc _spin_trylock(spinlock_t *lock);
int __lockfunc _read_trylock(rwlock_t *lock);
int __lockfunc _write_trylock(rwlock_t *lock);

void __lockfunc _spin_lock(spinlock_t *lock)	__acquires(spinlock_t);
void __lockfunc _read_lock(rwlock_t *lock)	__acquires(rwlock_t);
void __lockfunc _write_lock(rwlock_t *lock)	__acquires(rwlock_t);

void __lockfunc _spin_unlock(spinlock_t *lock)	__releases(spinlock_t);
void __lockfunc _read_unlock(rwlock_t *lock)	__releases(rwlock_t);
void __lockfunc _write_unlock(rwlock_t *lock)	__releases(rwlock_t);

unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)	__acquires(spinlock_t);
unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)	__acquires(rwlock_t);
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)	__acquires(rwlock_t);

void __lockfunc _spin_lock_irq(spinlock_t *lock)	__acquires(spinlock_t);
void __lockfunc _spin_lock_bh(spinlock_t *lock)		__acquires(spinlock_t);
void __lockfunc _read_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _read_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);

void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)	__releases(spinlock_t);
void __lockfunc _spin_unlock_irq(spinlock_t *lock)				__releases(spinlock_t);
void __lockfunc _spin_unlock_bh(spinlock_t *lock)				__releases(spinlock_t);
void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)	__releases(rwlock_t);
void __lockfunc _read_unlock_irq(rwlock_t *lock)				__releases(rwlock_t);
void __lockfunc _read_unlock_bh(rwlock_t *lock)					__releases(rwlock_t);
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)	__releases(rwlock_t);
void __lockfunc _write_unlock_irq(rwlock_t *lock)				__releases(rwlock_t);
void __lockfunc _write_unlock_bh(rwlock_t *lock)				__releases(rwlock_t);

int __lockfunc _spin_trylock_bh(spinlock_t *lock);
int __lockfunc generic_raw_read_trylock(rwlock_t *lock);
int in_lock_functions(unsigned long addr);

#else

#define in_lock_functions(ADDR) 0

#if !defined(CONFIG_PREEMPT) && !defined(CONFIG_DEBUG_SPINLOCK)
# define _atomic_dec_and_lock(atomic,lock) atomic_dec_and_test(atomic)
# define ATOMIC_DEC_AND_LOCK
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 
#define SPINLOCK_MAGIC	0x1D244B3C
typedef struct {
	unsigned long magic;
	volatile unsigned long lock;
	volatile unsigned int babble;
	const char *module;
	char *owner;
	int oline;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { SPINLOCK_MAGIC, 0, 10, __FILE__ , NULL, 0}

#define spin_lock_init(x) \
	do { \
		(x)->magic = SPINLOCK_MAGIC; \
		(x)->lock = 0; \
		(x)->babble = 5; \
		(x)->module = __FILE__; \
		(x)->owner = NULL; \
		(x)->oline = 0; \
	} while (0)

#define CHECK_LOCK(x) \
	do { \
	 	if ((x)->magic != SPINLOCK_MAGIC) { \
			printk(KERN_ERR "%s:%d: spin_is_locked on uninitialized spinlock %p.\n", \
					__FILE__, __LINE__, (x)); \
		} \
	} while(0)

#define _raw_spin_lock(x)		\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_lock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
	} while (0)

/* without debugging, spin_is_locked on UP always says
 * FALSE. --> printk if already locked. */
#define spin_is_locked(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_is_locked(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		0; \
	})

/* with debugging, assert_spin_locked() on UP does check
 * the lock value properly */
#define assert_spin_locked(x) \
	({ \
		CHECK_LOCK(x); \
		BUG_ON(!(x)->lock); \
	})

/* without debugging, spin_trylock on UP always says
 * TRUE. --> printk if already locked. */
#define _raw_spin_trylock(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_trylock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
		1; \
	})

#define spin_unlock_wait(x)	\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock_wait(%s:%p) owned by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, (x), \
					(x)->owner, (x)->oline); \
		}\
	} while (0)

#define _raw_spin_unlock(x) \
	do { \
	 	CHECK_LOCK(x); \
		if (!(x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock(%s:%p) not locked\n", \
					__FILE__,__LINE__, (x)->module, (x));\
		} \
		(x)->lock = 0; \
	} while (0)
#else
/*
 * gcc versions before ~2.95 have a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2)
  typedef struct { } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#endif

/*
 * If CONFIG_SMP is unset, declare the _raw_* definitions as nops
 */
#define spin_lock_init(lock)	do { (void)(lock); } while(0)
#define _raw_spin_lock(lock)	do { (void)(lock); } while(0)
#define spin_is_locked(lock)	((void)(lock), 0)
#define assert_spin_locked(lock)	do { (void)(lock); } while(0)
#define _raw_spin_trylock(lock)	(((void)(lock), 1))
#define spin_unlock_wait(lock)	(void)(lock)
#define _raw_spin_unlock(lock) do { (void)(lock); } while(0)
#endif /* CONFIG_DEBUG_SPINLOCK */

/* RW spinlocks: No debug version */

#if (__GNUC__ > 2)
  typedef struct { } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#endif

#define rwlock_init(lock)	do { (void)(lock); } while(0)
#define _raw_read_lock(lock)	do { (void)(lock); } while(0)
#define _raw_read_unlock(lock)	do { (void)(lock); } while(0)
#define _raw_write_lock(lock)	do { (void)(lock); } while(0)
#define _raw_write_unlock(lock)	do { (void)(lock); } while(0)
#define read_can_lock(lock)	(((void)(lock), 1))
#define write_can_lock(lock)	(((void)(lock), 1))
#define _raw_read_trylock(lock) ({ (void)(lock); (1); })
#define _raw_write_trylock(lock) ({ (void)(lock); (1); })

#define _spin_trylock(lock)	({preempt_disable(); _raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _read_trylock(lock)	({preempt_disable();_raw_read_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _write_trylock(lock)	({preempt_disable(); _raw_write_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _spin_trylock_bh(lock)	({preempt_disable(); local_bh_disable(); \
				_raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); local_bh_enable(); 0;});})

#define _spin_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while(0)

#define _write_lock(lock) \
do { \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while(0)
 
#define _read_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while(0)

#define _spin_unlock(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while(0)

#define _read_unlock(lock) \
do { \
	_raw_read_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while(0)

#define _spin_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_unlock_irqrestore(lock, flags) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _spin_unlock_irq(lock) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_enable(); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _spin_unlock_bh(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock_bh(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
	__release(lock); \
} while (0)

#define _read_unlock_irqrestore(lock, flags) \
do { \
	_raw_read_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock_irqrestore(lock, flags) \
do { \
	_raw_write_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _read_unlock_irq(lock)	\
do { \
	_raw_read_unlock(lock);	\
	local_irq_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#define _read_unlock_bh(lock)	\
do { \
	_raw_read_unlock(lock);	\
	local_bh_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#define _write_unlock_irq(lock)	\
do { \
	_raw_write_unlock(lock);	\
	local_irq_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#endif /* !SMP */

/*
 * Define the various spin_lock and rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
#define spin_trylock(lock)	__cond_lock(_spin_trylock(lock))
#define read_trylock(lock)	__cond_lock(_read_trylock(lock))
#define write_trylock(lock)	__cond_lock(_write_trylock(lock))

#define spin_lock(lock)		_spin_lock(lock)
#define write_lock(lock)	_write_lock(lock)
#define read_lock(lock)		_read_lock(lock)

#ifdef CONFIG_SMP
#define spin_lock_irqsave(lock, flags)	flags = _spin_lock_irqsave(lock)
#define read_lock_irqsave(lock, flags)	flags = _read_lock_irqsave(lock)
#define write_lock_irqsave(lock, flags)	flags = _write_lock_irqsave(lock)
#else
#define spin_lock_irqsave(lock, flags)	_spin_lock_irqsave(lock, flags)
#define read_lock_irqsave(lock, flags)	_read_lock_irqsave(lock, flags)
#define write_lock_irqsave(lock, flags)	_write_lock_irqsave(lock, flags)
#endif

#define spin_lock_irq(lock)		_spin_lock_irq(lock)
#define spin_lock_bh(lock)		_spin_lock_bh(lock)

#define read_lock_irq(lock)		_read_lock_irq(lock)
#define read_lock_bh(lock)		_read_lock_bh(lock)

#define write_lock_irq(lock)		_write_lock_irq(lock)
#define write_lock_bh(lock)		_write_lock_bh(lock)

#define spin_unlock(lock)	_spin_unlock(lock)
#define write_unlock(lock)	_write_unlock(lock)
#define read_unlock(lock)	_read_unlock(lock)

#define spin_unlock_irqrestore(lock, flags)	_spin_unlock_irqrestore(lock, flags)
#define spin_unlock_irq(lock)		_spin_unlock_irq(lock)
#define spin_unlock_bh(lock)		_spin_unlock_bh(lock)

#define read_unlock_irqrestore(lock, flags)	_read_unlock_irqrestore(lock, flags)
#define read_unlock_irq(lock)			_read_unlock_irq(lock)
#define read_unlock_bh(lock)			_read_unlock_bh(lock)

#define write_unlock_irqrestore(lock, flags)	_write_unlock_irqrestore(lock, flags)
#define write_unlock_irq(lock)			_write_unlock_irq(lock)
#define write_unlock_bh(lock)			_write_unlock_bh(lock)

#define spin_trylock_bh(lock)			__cond_lock(_spin_trylock_bh(lock))

#define spin_trylock_irq(lock) \
({ \
	local_irq_disable(); \
	_spin_trylock(lock) ? \
	1 : ({local_irq_enable(); 0; }); \
})

#define spin_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	_spin_trylock(lock) ? \
	1 : ({local_irq_restore(flags); 0;}); \
})

#ifdef CONFIG_LOCKMETER
extern void _metered_spin_lock   (spinlock_t *lock);
extern void _metered_spin_unlock (spinlock_t *lock);
extern int  _metered_spin_trylock(spinlock_t *lock);
extern void _metered_read_lock    (rwlock_t *lock);
extern void _metered_read_unlock  (rwlock_t *lock);
extern void _metered_write_lock   (rwlock_t *lock);
extern void _metered_write_unlock (rwlock_t *lock);
extern int  _metered_read_trylock (rwlock_t *lock);
extern int  _metered_write_trylock(rwlock_t *lock);
#endif

/* "lock on reference count zero" */
#ifndef ATOMIC_DEC_AND_LOCK
#include <asm/atomic.h>
extern int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
#endif

#define atomic_dec_and_lock(atomic,lock) __cond_lock(_atomic_dec_and_lock(atomic,lock))

/*
 *  bit-based spin_lock()
 *
 * Don't use this unless you really need to: spin_lock() and spin_unlock()
 * are significantly faster.
 */
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
	preempt_disable();
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	while (test_and_set_bit(bitnum, addr)) {
		while (test_bit(bitnum, addr)) {
			preempt_enable();
			cpu_relax();
			preempt_disable();
		}
	}
#endif
	__acquire(bitlock);
}

/*
 * Return true if it was acquired
 */
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
	preempt_disable();	
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	if (test_and_set_bit(bitnum, addr)) {
		preempt_enable();
		return 0;
	}
#endif
	__acquire(bitlock);
	return 1;
}

/*
 *  bit-based spin_unlock()
 */
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	BUG_ON(!test_bit(bitnum, addr));
	smp_mb__before_clear_bit();
	clear_bit(bitnum, addr);
#endif
	preempt_enable();
	__release(bitlock);
}

/*
 * Return true if the lock is held.
 */
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return test_bit(bitnum, addr);
#elif defined CONFIG_PREEMPT
	return preempt_count();
#else
	return 1;
#endif
}

#define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED
#define DEFINE_RWLOCK(x) rwlock_t x = RW_LOCK_UNLOCKED

/**
 * spin_can_lock - would spin_trylock() succeed?
 * @lock: the spinlock in question.
 */
#define spin_can_lock(lock)		(!spin_is_locked(lock))

#endif /* __LINUX_SPINLOCK_H */
