#ifndef __LINUX_SPINLOCK_API_SMP_H
#define __LINUX_SPINLOCK_API_SMP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

/*
 * include/linux/spinlock_api_smp.h
 *
 * spinlock API declarations on SMP (and debug)
 * (implemented in kernel/spinlock.c)
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

int in_lock_functions(unsigned long addr);

#define assert_spin_locked(x)	BUG_ON(!spin_is_locked(x))

void __lockfunc _spin_lock(spinlock_t *lock)		__acquires(lock);
void __lockfunc _spin_lock_nested(spinlock_t *lock, int subclass)
							__acquires(lock);
void __lockfunc _spin_lock_nest_lock(spinlock_t *lock, struct lockdep_map *map)
							__acquires(lock);
void __lockfunc _read_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _spin_lock_bh(spinlock_t *lock)		__acquires(lock);
void __lockfunc _read_lock_bh(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock_bh(rwlock_t *lock)		__acquires(lock);
void __lockfunc _spin_lock_irq(spinlock_t *lock)	__acquires(lock);
void __lockfunc _read_lock_irq(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock_irq(rwlock_t *lock)		__acquires(lock);
unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
							__acquires(lock);
unsigned long __lockfunc _spin_lock_irqsave_nested(spinlock_t *lock, int subclass)
							__acquires(lock);
unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
int __lockfunc _spin_trylock(spinlock_t *lock);
int __lockfunc _read_trylock(rwlock_t *lock);
int __lockfunc _write_trylock(rwlock_t *lock);
int __lockfunc _spin_trylock_bh(spinlock_t *lock);
void __lockfunc _spin_unlock(spinlock_t *lock)		__releases(lock);
void __lockfunc _read_unlock(rwlock_t *lock)		__releases(lock);
void __lockfunc _write_unlock(rwlock_t *lock)		__releases(lock);
void __lockfunc _spin_unlock_bh(spinlock_t *lock)	__releases(lock);
void __lockfunc _read_unlock_bh(rwlock_t *lock)		__releases(lock);
void __lockfunc _write_unlock_bh(rwlock_t *lock)	__releases(lock);
void __lockfunc _spin_unlock_irq(spinlock_t *lock)	__releases(lock);
void __lockfunc _read_unlock_irq(rwlock_t *lock)	__releases(lock);
void __lockfunc _write_unlock_irq(rwlock_t *lock)	__releases(lock);
void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
							__releases(lock);
void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);

/*
 * We inline the unlock functions in the nondebug case:
 */
#if !defined(CONFIG_DEBUG_SPINLOCK) && !defined(CONFIG_PREEMPT)
#define __always_inline__spin_unlock
#define __always_inline__read_unlock
#define __always_inline__write_unlock
#define __always_inline__spin_unlock_irq
#define __always_inline__read_unlock_irq
#define __always_inline__write_unlock_irq
#endif

#ifndef CONFIG_DEBUG_SPINLOCK
#ifndef CONFIG_GENERIC_LOCKBREAK

#ifdef __always_inline__spin_lock
#define _spin_lock(lock) __spin_lock(lock)
#endif

#ifdef __always_inline__read_lock
#define _read_lock(lock) __read_lock(lock)
#endif

#ifdef __always_inline__write_lock
#define _write_lock(lock) __write_lock(lock)
#endif

#ifdef __always_inline__spin_lock_bh
#define _spin_lock_bh(lock) __spin_lock_bh(lock)
#endif

#ifdef __always_inline__read_lock_bh
#define _read_lock_bh(lock) __read_lock_bh(lock)
#endif

#ifdef __always_inline__write_lock_bh
#define _write_lock_bh(lock) __write_lock_bh(lock)
#endif

#ifdef __always_inline__spin_lock_irq
#define _spin_lock_irq(lock) __spin_lock_irq(lock)
#endif

#ifdef __always_inline__read_lock_irq
#define _read_lock_irq(lock) __read_lock_irq(lock)
#endif

#ifdef __always_inline__write_lock_irq
#define _write_lock_irq(lock) __write_lock_irq(lock)
#endif

#ifdef __always_inline__spin_lock_irqsave
#define _spin_lock_irqsave(lock) __spin_lock_irqsave(lock)
#endif

#ifdef __always_inline__read_lock_irqsave
#define _read_lock_irqsave(lock) __read_lock_irqsave(lock)
#endif

#ifdef __always_inline__write_lock_irqsave
#define _write_lock_irqsave(lock) __write_lock_irqsave(lock)
#endif

#endif /* !CONFIG_GENERIC_LOCKBREAK */

#ifdef __always_inline__spin_trylock
#define _spin_trylock(lock) __spin_trylock(lock)
#endif

#ifdef __always_inline__read_trylock
#define _read_trylock(lock) __read_trylock(lock)
#endif

#ifdef __always_inline__write_trylock
#define _write_trylock(lock) __write_trylock(lock)
#endif

#ifdef __always_inline__spin_trylock_bh
#define _spin_trylock_bh(lock) __spin_trylock_bh(lock)
#endif

#ifdef __always_inline__spin_unlock
#define _spin_unlock(lock) __spin_unlock(lock)
#endif

#ifdef __always_inline__read_unlock
#define _read_unlock(lock) __read_unlock(lock)
#endif

#ifdef __always_inline__write_unlock
#define _write_unlock(lock) __write_unlock(lock)
#endif

#ifdef __always_inline__spin_unlock_bh
#define _spin_unlock_bh(lock) __spin_unlock_bh(lock)
#endif

#ifdef __always_inline__read_unlock_bh
#define _read_unlock_bh(lock) __read_unlock_bh(lock)
#endif

#ifdef __always_inline__write_unlock_bh
#define _write_unlock_bh(lock) __write_unlock_bh(lock)
#endif

#ifdef __always_inline__spin_unlock_irq
#define _spin_unlock_irq(lock) __spin_unlock_irq(lock)
#endif

#ifdef __always_inline__read_unlock_irq
#define _read_unlock_irq(lock) __read_unlock_irq(lock)
#endif

#ifdef __always_inline__write_unlock_irq
#define _write_unlock_irq(lock) __write_unlock_irq(lock)
#endif

#ifdef __always_inline__spin_unlock_irqrestore
#define _spin_unlock_irqrestore(lock, flags) __spin_unlock_irqrestore(lock, flags)
#endif

#ifdef __always_inline__read_unlock_irqrestore
#define _read_unlock_irqrestore(lock, flags) __read_unlock_irqrestore(lock, flags)
#endif

#ifdef __always_inline__write_unlock_irqrestore
#define _write_unlock_irqrestore(lock, flags) __write_unlock_irqrestore(lock, flags)
#endif

#endif /* CONFIG_DEBUG_SPINLOCK */

static inline int __spin_trylock(spinlock_t *lock)
{
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable();
	return 0;
}

static inline int __read_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_read_trylock(lock)) {
		rwlock_acquire_read(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable();
	return 0;
}

static inline int __write_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_write_trylock(lock)) {
		rwlock_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable();
	return 0;
}

/*
 * If lockdep is enabled then we use the non-preemption spin-ops
 * even on CONFIG_PREEMPT, because lockdep assumes that interrupts are
 * not re-enabled during lock-acquire (which the preempt-spin-ops do):
 */
#if !defined(CONFIG_GENERIC_LOCKBREAK) || defined(CONFIG_DEBUG_LOCK_ALLOC)

static inline void __read_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}

static inline unsigned long __spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	/*
	 * On lockdep we dont want the hand-coded irq-enable of
	 * _raw_spin_lock_flags() code, because lockdep assumes
	 * that interrupts are not re-enabled during lock-acquire:
	 */
#ifdef CONFIG_LOCKDEP
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
#else
	_raw_spin_lock_flags(lock, &flags);
#endif
	return flags;
}

static inline void __spin_lock_irq(spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}

static inline void __spin_lock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}

static inline unsigned long __read_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_read_trylock, _raw_read_lock,
			     _raw_read_lock_flags, &flags);
	return flags;
}

static inline void __read_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}

static inline void __read_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}

static inline unsigned long __write_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_write_trylock, _raw_write_lock,
			     _raw_write_lock_flags, &flags);
	return flags;
}

static inline void __write_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}

static inline void __write_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}

static inline void __spin_lock(spinlock_t *lock)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}

static inline void __write_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}

#endif /* CONFIG_PREEMPT */

static inline void __spin_unlock(spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	preempt_enable();
}

static inline void __write_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	preempt_enable();
}

static inline void __read_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	preempt_enable();
}

static inline void __spin_unlock_irqrestore(spinlock_t *lock,
					    unsigned long flags)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}

static inline void __spin_unlock_irq(spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	local_irq_enable();
	preempt_enable();
}

static inline void __spin_unlock_bh(spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}

static inline void __read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}

static inline void __read_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	local_irq_enable();
	preempt_enable();
}

static inline void __read_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}

static inline void __write_unlock_irqrestore(rwlock_t *lock,
					     unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}

static inline void __write_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	local_irq_enable();
	preempt_enable();
}

static inline void __write_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}

static inline int __spin_trylock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
	return 0;
}

#endif /* __LINUX_SPINLOCK_API_SMP_H */
