#ifndef __LINUX_RWLOCK_API_SMP_H
#define __LINUX_RWLOCK_API_SMP_H

#ifndef __LINUX_SPINLOCK_API_SMP_H
# error "please don't include this file directly"
#endif

/*
 * include/linux/rwlock_api_smp.h
 *
 * spinlock API declarations on SMP (and debug)
 * (implemented in kernel/spinlock.c)
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

void __lockfunc _raw_read_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _raw_write_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _raw_read_lock_bh(rwlock_t *lock)	__acquires(lock);
void __lockfunc _raw_write_lock_bh(rwlock_t *lock)	__acquires(lock);
void __lockfunc _raw_read_lock_irq(rwlock_t *lock)	__acquires(lock);
void __lockfunc _raw_write_lock_irq(rwlock_t *lock)	__acquires(lock);
unsigned long __lockfunc _raw_read_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
unsigned long __lockfunc _raw_write_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
int __lockfunc _raw_read_trylock(rwlock_t *lock);
int __lockfunc _raw_write_trylock(rwlock_t *lock);
void __lockfunc _raw_read_unlock(rwlock_t *lock)	__releases(lock);
void __lockfunc _raw_write_unlock(rwlock_t *lock)	__releases(lock);
void __lockfunc _raw_read_unlock_bh(rwlock_t *lock)	__releases(lock);
void __lockfunc _raw_write_unlock_bh(rwlock_t *lock)	__releases(lock);
void __lockfunc _raw_read_unlock_irq(rwlock_t *lock)	__releases(lock);
void __lockfunc _raw_write_unlock_irq(rwlock_t *lock)	__releases(lock);
void __lockfunc
_raw_read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);
void __lockfunc
_raw_write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);

#ifdef CONFIG_INLINE_READ_LOCK
#define _raw_read_lock(lock) __raw_read_lock(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_LOCK
#define _raw_write_lock(lock) __raw_write_lock(lock)
#endif

#ifdef CONFIG_INLINE_READ_LOCK_BH
#define _raw_read_lock_bh(lock) __raw_read_lock_bh(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_LOCK_BH
#define _raw_write_lock_bh(lock) __raw_write_lock_bh(lock)
#endif

#ifdef CONFIG_INLINE_READ_LOCK_IRQ
#define _raw_read_lock_irq(lock) __raw_read_lock_irq(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_LOCK_IRQ
#define _raw_write_lock_irq(lock) __raw_write_lock_irq(lock)
#endif

#ifdef CONFIG_INLINE_READ_LOCK_IRQSAVE
#define _raw_read_lock_irqsave(lock) __raw_read_lock_irqsave(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_LOCK_IRQSAVE
#define _raw_write_lock_irqsave(lock) __raw_write_lock_irqsave(lock)
#endif

#ifdef CONFIG_INLINE_READ_TRYLOCK
#define _raw_read_trylock(lock) __raw_read_trylock(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_TRYLOCK
#define _raw_write_trylock(lock) __raw_write_trylock(lock)
#endif

#ifdef CONFIG_INLINE_READ_UNLOCK
#define _raw_read_unlock(lock) __raw_read_unlock(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_UNLOCK
#define _raw_write_unlock(lock) __raw_write_unlock(lock)
#endif

#ifdef CONFIG_INLINE_READ_UNLOCK_BH
#define _raw_read_unlock_bh(lock) __raw_read_unlock_bh(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_UNLOCK_BH
#define _raw_write_unlock_bh(lock) __raw_write_unlock_bh(lock)
#endif

#ifdef CONFIG_INLINE_READ_UNLOCK_IRQ
#define _raw_read_unlock_irq(lock) __raw_read_unlock_irq(lock)
#endif

#ifdef CONFIG_INLINE_WRITE_UNLOCK_IRQ
#define _raw_write_unlock_irq(lock) __raw_write_unlock_irq(lock)
#endif

#ifdef CONFIG_INLINE_READ_UNLOCK_IRQRESTORE
#define _raw_read_unlock_irqrestore(lock, flags) \
	__raw_read_unlock_irqrestore(lock, flags)
#endif

#ifdef CONFIG_INLINE_WRITE_UNLOCK_IRQRESTORE
#define _raw_write_unlock_irqrestore(lock, flags) \
	__raw_write_unlock_irqrestore(lock, flags)
#endif

static inline int __raw_read_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (do_raw_read_trylock(lock)) {
		rwlock_acquire_read(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable();
	return 0;
}

static inline int __raw_write_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (do_raw_write_trylock(lock)) {
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

static inline void __raw_read_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_read_trylock, do_raw_read_lock);
}

static inline unsigned long __raw_read_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, do_raw_read_trylock, do_raw_read_lock,
			     do_raw_read_lock_flags, &flags);
	return flags;
}

static inline void __raw_read_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_read_trylock, do_raw_read_lock);
}

static inline void __raw_read_lock_bh(rwlock_t *lock)
{
	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_LOCK_OFFSET);
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_read_trylock, do_raw_read_lock);
}

static inline unsigned long __raw_write_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, do_raw_write_trylock, do_raw_write_lock,
			     do_raw_write_lock_flags, &flags);
	return flags;
}

static inline void __raw_write_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_write_trylock, do_raw_write_lock);
}

static inline void __raw_write_lock_bh(rwlock_t *lock)
{
	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_LOCK_OFFSET);
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_write_trylock, do_raw_write_lock);
}

static inline void __raw_write_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_write_trylock, do_raw_write_lock);
}

#endif /* !CONFIG_GENERIC_LOCKBREAK || CONFIG_DEBUG_LOCK_ALLOC */

static inline void __raw_write_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_write_unlock(lock);
	preempt_enable();
}

static inline void __raw_read_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_read_unlock(lock);
	preempt_enable();
}

static inline void
__raw_read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_read_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}

static inline void __raw_read_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_read_unlock(lock);
	local_irq_enable();
	preempt_enable();
}

static inline void __raw_read_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_read_unlock(lock);
	__local_bh_enable_ip(_RET_IP_, SOFTIRQ_LOCK_OFFSET);
}

static inline void __raw_write_unlock_irqrestore(rwlock_t *lock,
					     unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_write_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}

static inline void __raw_write_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_write_unlock(lock);
	local_irq_enable();
	preempt_enable();
}

static inline void __raw_write_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	do_raw_write_unlock(lock);
	__local_bh_enable_ip(_RET_IP_, SOFTIRQ_LOCK_OFFSET);
}

#endif /* __LINUX_RWLOCK_API_SMP_H */
