#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic spinlock/rwlock declarations
 *
 * here's the role of the various spinlock/rwlock related include files:
 *
 * on SMP builds:
 *
 *  asm/spinlock_types.h: contains the raw_spinlock_t/raw_rwlock_t and the
 *                        initializers
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  asm/spinlock.h:       contains the __raw_spin_*()/etc. lowlevel
 *                        implementations, mostly inline assembly code
 *
 *   (also included on UP-debug builds:)
 *
 *  linux/spinlock_api_smp.h:
 *                        contains the prototypes for the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 *
 * on UP builds:
 *
 *  linux/spinlock_type_up.h:
 *                        contains the generic, simplified UP spinlock type.
 *                        (which is an empty structure on non-debug builds)
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  linux/spinlock_up.h:
 *                        contains the __raw_spin_*()/etc. version of UP
 *                        builds. (which are NOPs on non-debug, non-preempt
 *                        builds)
 *
 *   (included on UP-non-debug builds:)
 *
 *  linux/spinlock_api_up.h:
 *                        builds the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 */

#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>

#include <asm/system.h>

/*
 * Must define these before including other files, inline functions need them
 */
#define LOCK_SECTION_NAME ".text.lock."KBUILD_BASENAME

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
 * Pull the raw_spinlock_t and raw_rwlock_t definitions:
 */
#include <linux/spinlock_types.h>

extern int __lockfunc generic__raw_read_trylock(raw_rwlock_t *lock);

/*
 * Pull the __raw*() functions/declarations (UP-nondebug doesnt need them):
 */
#ifdef CONFIG_SMP
# include <asm/spinlock.h>
#else
# include <linux/spinlock_up.h>
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __spin_lock_init(spinlock_t *lock, const char *name,
			       struct lock_class_key *key);
# define spin_lock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	__spin_lock_init((lock), #lock, &__key);		\
} while (0)

#else
# define spin_lock_init(lock)					\
	do { *(lock) = SPIN_LOCK_UNLOCKED; } while (0)
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __rwlock_init(rwlock_t *lock, const char *name,
			    struct lock_class_key *key);
# define rwlock_init(lock)					\
do {								\
	static struct lock_class_key __key;			\
								\
	__rwlock_init((lock), #lock, &__key);			\
} while (0)
#else
# define rwlock_init(lock)					\
	do { *(lock) = RW_LOCK_UNLOCKED; } while (0)
#endif

#define spin_is_locked(lock)	__raw_spin_is_locked(&(lock)->raw_lock)

/**
 * spin_unlock_wait - wait until the spinlock gets unlocked
 * @lock: the spinlock in question.
 */
#define spin_unlock_wait(lock)	__raw_spin_unlock_wait(&(lock)->raw_lock)

/*
 * Pull the _spin_*()/_read_*()/_write_*() functions/declarations:
 */
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
# include <linux/spinlock_api_smp.h>
#else
# include <linux/spinlock_api_up.h>
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 extern void _raw_spin_lock(spinlock_t *lock);
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)
 extern int _raw_spin_trylock(spinlock_t *lock);
 extern void _raw_spin_unlock(spinlock_t *lock);
 extern void _raw_read_lock(rwlock_t *lock);
 extern int _raw_read_trylock(rwlock_t *lock);
 extern void _raw_read_unlock(rwlock_t *lock);
 extern void _raw_write_lock(rwlock_t *lock);
 extern int _raw_write_trylock(rwlock_t *lock);
 extern void _raw_write_unlock(rwlock_t *lock);
#else
# define _raw_spin_lock(lock)		__raw_spin_lock(&(lock)->raw_lock)
# define _raw_spin_lock_flags(lock, flags) \
		__raw_spin_lock_flags(&(lock)->raw_lock, *(flags))
# define _raw_spin_trylock(lock)	__raw_spin_trylock(&(lock)->raw_lock)
# define _raw_spin_unlock(lock)		__raw_spin_unlock(&(lock)->raw_lock)
# define _raw_read_lock(rwlock)		__raw_read_lock(&(rwlock)->raw_lock)
# define _raw_read_trylock(rwlock)	__raw_read_trylock(&(rwlock)->raw_lock)
# define _raw_read_unlock(rwlock)	__raw_read_unlock(&(rwlock)->raw_lock)
# define _raw_write_lock(rwlock)	__raw_write_lock(&(rwlock)->raw_lock)
# define _raw_write_trylock(rwlock)	__raw_write_trylock(&(rwlock)->raw_lock)
# define _raw_write_unlock(rwlock)	__raw_write_unlock(&(rwlock)->raw_lock)
#endif

#define read_can_lock(rwlock)		__raw_read_can_lock(&(rwlock)->raw_lock)
#define write_can_lock(rwlock)		__raw_write_can_lock(&(rwlock)->raw_lock)

/*
 * Define the various spin_lock and rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
#define spin_trylock(lock)		__cond_lock(lock, _spin_trylock(lock))
#define read_trylock(lock)		__cond_lock(lock, _read_trylock(lock))
#define write_trylock(lock)		__cond_lock(lock, _write_trylock(lock))

#define spin_lock(lock)			_spin_lock(lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define spin_lock_nested(lock, subclass) _spin_lock_nested(lock, subclass)
#else
# define spin_lock_nested(lock, subclass) _spin_lock(lock)
#endif

#define write_lock(lock)		_write_lock(lock)
#define read_lock(lock)			_read_lock(lock)

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)

#define spin_lock_irqsave(lock, flags)	flags = _spin_lock_irqsave(lock)
#define read_lock_irqsave(lock, flags)	flags = _read_lock_irqsave(lock)
#define write_lock_irqsave(lock, flags)	flags = _write_lock_irqsave(lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define spin_lock_irqsave_nested(lock, flags, subclass) \
	flags = _spin_lock_irqsave_nested(lock, subclass)
#else
#define spin_lock_irqsave_nested(lock, flags, subclass) \
	flags = _spin_lock_irqsave(lock)
#endif

#else

#define spin_lock_irqsave(lock, flags)	_spin_lock_irqsave(lock, flags)
#define read_lock_irqsave(lock, flags)	_read_lock_irqsave(lock, flags)
#define write_lock_irqsave(lock, flags)	_write_lock_irqsave(lock, flags)
#define spin_lock_irqsave_nested(lock, flags, subclass)	\
	spin_lock_irqsave(lock, flags)

#endif

#define spin_lock_irq(lock)		_spin_lock_irq(lock)
#define spin_lock_bh(lock)		_spin_lock_bh(lock)

#define read_lock_irq(lock)		_read_lock_irq(lock)
#define read_lock_bh(lock)		_read_lock_bh(lock)

#define write_lock_irq(lock)		_write_lock_irq(lock)
#define write_lock_bh(lock)		_write_lock_bh(lock)

/*
 * We inline the unlock functions in the nondebug case:
 */
#if defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT) || \
	!defined(CONFIG_SMP)
# define spin_unlock(lock)		_spin_unlock(lock)
# define read_unlock(lock)		_read_unlock(lock)
# define write_unlock(lock)		_write_unlock(lock)
# define spin_unlock_irq(lock)		_spin_unlock_irq(lock)
# define read_unlock_irq(lock)		_read_unlock_irq(lock)
# define write_unlock_irq(lock)		_write_unlock_irq(lock)
#else
# define spin_unlock(lock)		__raw_spin_unlock(&(lock)->raw_lock)
# define read_unlock(lock)		__raw_read_unlock(&(lock)->raw_lock)
# define write_unlock(lock)		__raw_write_unlock(&(lock)->raw_lock)
# define spin_unlock_irq(lock) \
    do { __raw_spin_unlock(&(lock)->raw_lock); local_irq_enable(); } while (0)
# define read_unlock_irq(lock) \
    do { __raw_read_unlock(&(lock)->raw_lock); local_irq_enable(); } while (0)
# define write_unlock_irq(lock) \
    do { __raw_write_unlock(&(lock)->raw_lock); local_irq_enable(); } while (0)
#endif

#define spin_unlock_irqrestore(lock, flags) \
					_spin_unlock_irqrestore(lock, flags)
#define spin_unlock_bh(lock)		_spin_unlock_bh(lock)

#define read_unlock_irqrestore(lock, flags) \
					_read_unlock_irqrestore(lock, flags)
#define read_unlock_bh(lock)		_read_unlock_bh(lock)

#define write_unlock_irqrestore(lock, flags) \
					_write_unlock_irqrestore(lock, flags)
#define write_unlock_bh(lock)		_write_unlock_bh(lock)

#define spin_trylock_bh(lock)	__cond_lock(lock, _spin_trylock_bh(lock))

#define spin_trylock_irq(lock) \
({ \
	local_irq_disable(); \
	spin_trylock(lock) ? \
	1 : ({ local_irq_enable(); 0;  }); \
})

#define spin_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	spin_trylock(lock) ? \
	1 : ({ local_irq_restore(flags); 0; }); \
})

/*
 * Pull the atomic_t declaration:
 * (asm-mips/atomic.h needs above definitions)
 */
#include <asm/atomic.h>
/**
 * atomic_dec_and_lock - lock on reaching reference count zero
 * @atomic: the atomic counter
 * @lock: the spinlock in question
 */
extern int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
#define atomic_dec_and_lock(atomic, lock) \
		__cond_lock(lock, _atomic_dec_and_lock(atomic, lock))

/**
 * spin_can_lock - would spin_trylock() succeed?
 * @lock: the spinlock in question.
 */
#define spin_can_lock(lock)	(!spin_is_locked(lock))

#endif /* __LINUX_SPINLOCK_H */
