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

void __lockfunc _spin_lock(spinlock_t *lock)		__acquires(spinlock_t);
void __lockfunc _spin_lock_nested(spinlock_t *lock, int subclass)
							__acquires(spinlock_t);
void __lockfunc _read_lock(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _spin_lock_bh(spinlock_t *lock)		__acquires(spinlock_t);
void __lockfunc _read_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _spin_lock_irq(spinlock_t *lock)	__acquires(spinlock_t);
void __lockfunc _read_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
							__acquires(spinlock_t);
unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
							__acquires(rwlock_t);
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
							__acquires(rwlock_t);
int __lockfunc _spin_trylock(spinlock_t *lock);
int __lockfunc _read_trylock(rwlock_t *lock);
int __lockfunc _write_trylock(rwlock_t *lock);
int __lockfunc _spin_trylock_bh(spinlock_t *lock);
void __lockfunc _spin_unlock(spinlock_t *lock)		__releases(spinlock_t);
void __lockfunc _read_unlock(rwlock_t *lock)		__releases(rwlock_t);
void __lockfunc _write_unlock(rwlock_t *lock)		__releases(rwlock_t);
void __lockfunc _spin_unlock_bh(spinlock_t *lock)	__releases(spinlock_t);
void __lockfunc _read_unlock_bh(rwlock_t *lock)		__releases(rwlock_t);
void __lockfunc _write_unlock_bh(rwlock_t *lock)	__releases(rwlock_t);
void __lockfunc _spin_unlock_irq(spinlock_t *lock)	__releases(spinlock_t);
void __lockfunc _read_unlock_irq(rwlock_t *lock)	__releases(rwlock_t);
void __lockfunc _write_unlock_irq(rwlock_t *lock)	__releases(rwlock_t);
void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
							__releases(spinlock_t);
void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(rwlock_t);
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(rwlock_t);

#endif /* __LINUX_SPINLOCK_API_SMP_H */
