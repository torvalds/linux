/*
 * Copyright (2004) Linus Torvalds
 *
 * Author: Zwane Mwaikambo <zwane@fsmlabs.com>
 *
 * Copyright (2004, 2005) Ingo Molnar
 *
 * This file contains the spinlock/rwlock implementations for the
 * SMP and the DEBUG_SPINLOCK cases. (UP-nondebug inlines them)
 *
 * Note that some architectures have special knowledge about the
 * stack frames of these functions in their profile_pc. If you
 * change anything significant here that could change the stack
 * frame contact the architecture maintainers.
 */

#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/module.h>

#ifndef _spin_trylock
int __lockfunc _spin_trylock(spinlock_t *lock)
{
	return __spin_trylock(lock);
}
EXPORT_SYMBOL(_spin_trylock);
#endif

#ifndef _read_trylock
int __lockfunc _read_trylock(rwlock_t *lock)
{
	return __read_trylock(lock);
}
EXPORT_SYMBOL(_read_trylock);
#endif

#ifndef _write_trylock
int __lockfunc _write_trylock(rwlock_t *lock)
{
	return __write_trylock(lock);
}
EXPORT_SYMBOL(_write_trylock);
#endif

/*
 * If lockdep is enabled then we use the non-preemption spin-ops
 * even on CONFIG_PREEMPT, because lockdep assumes that interrupts are
 * not re-enabled during lock-acquire (which the preempt-spin-ops do):
 */
#if !defined(CONFIG_GENERIC_LOCKBREAK) || defined(CONFIG_DEBUG_LOCK_ALLOC)

#ifndef _read_lock
void __lockfunc _read_lock(rwlock_t *lock)
{
	__read_lock(lock);
}
EXPORT_SYMBOL(_read_lock);
#endif

#ifndef _spin_lock_irqsave
unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
{
	return __spin_lock_irqsave(lock);
}
EXPORT_SYMBOL(_spin_lock_irqsave);
#endif

#ifndef _spin_lock_irq
void __lockfunc _spin_lock_irq(spinlock_t *lock)
{
	__spin_lock_irq(lock);
}
EXPORT_SYMBOL(_spin_lock_irq);
#endif

#ifndef _spin_lock_bh
void __lockfunc _spin_lock_bh(spinlock_t *lock)
{
	__spin_lock_bh(lock);
}
EXPORT_SYMBOL(_spin_lock_bh);
#endif

#ifndef _read_lock_irqsave
unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
{
	return __read_lock_irqsave(lock);
}
EXPORT_SYMBOL(_read_lock_irqsave);
#endif

#ifndef _read_lock_irq
void __lockfunc _read_lock_irq(rwlock_t *lock)
{
	__read_lock_irq(lock);
}
EXPORT_SYMBOL(_read_lock_irq);
#endif

#ifndef _read_lock_bh
void __lockfunc _read_lock_bh(rwlock_t *lock)
{
	__read_lock_bh(lock);
}
EXPORT_SYMBOL(_read_lock_bh);
#endif

#ifndef _write_lock_irqsave
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
{
	return __write_lock_irqsave(lock);
}
EXPORT_SYMBOL(_write_lock_irqsave);
#endif

#ifndef _write_lock_irq
void __lockfunc _write_lock_irq(rwlock_t *lock)
{
	__write_lock_irq(lock);
}
EXPORT_SYMBOL(_write_lock_irq);
#endif

#ifndef _write_lock_bh
void __lockfunc _write_lock_bh(rwlock_t *lock)
{
	__write_lock_bh(lock);
}
EXPORT_SYMBOL(_write_lock_bh);
#endif

#ifndef _spin_lock
void __lockfunc _spin_lock(spinlock_t *lock)
{
	__spin_lock(lock);
}
EXPORT_SYMBOL(_spin_lock);
#endif

#ifndef _write_lock
void __lockfunc _write_lock(rwlock_t *lock)
{
	__write_lock(lock);
}
EXPORT_SYMBOL(_write_lock);
#endif

#else /* CONFIG_PREEMPT: */

/*
 * This could be a long-held lock. We both prepare to spin for a long
 * time (making _this_ CPU preemptable if possible), and we also signal
 * towards that other CPU that it should break the lock ASAP.
 *
 * (We do this in a function because inlining it would be excessive.)
 */

#define BUILD_LOCK_OPS(op, locktype)					\
void __lockfunc _##op##_lock(locktype##_t *lock)			\
{									\
	for (;;) {							\
		preempt_disable();					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
			_raw_##op##_relax(&lock->raw_lock);		\
	}								\
	(lock)->break_lock = 0;						\
}									\
									\
EXPORT_SYMBOL(_##op##_lock);						\
									\
unsigned long __lockfunc _##op##_lock_irqsave(locktype##_t *lock)	\
{									\
	unsigned long flags;						\
									\
	for (;;) {							\
		preempt_disable();					\
		local_irq_save(flags);					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		local_irq_restore(flags);				\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
			_raw_##op##_relax(&lock->raw_lock);		\
	}								\
	(lock)->break_lock = 0;						\
	return flags;							\
}									\
									\
EXPORT_SYMBOL(_##op##_lock_irqsave);					\
									\
void __lockfunc _##op##_lock_irq(locktype##_t *lock)			\
{									\
	_##op##_lock_irqsave(lock);					\
}									\
									\
EXPORT_SYMBOL(_##op##_lock_irq);					\
									\
void __lockfunc _##op##_lock_bh(locktype##_t *lock)			\
{									\
	unsigned long flags;						\
									\
	/*							*/	\
	/* Careful: we must exclude softirqs too, hence the	*/	\
	/* irq-disabling. We use the generic preemption-aware	*/	\
	/* function:						*/	\
	/**/								\
	flags = _##op##_lock_irqsave(lock);				\
	local_bh_disable();						\
	local_irq_restore(flags);					\
}									\
									\
EXPORT_SYMBOL(_##op##_lock_bh)

/*
 * Build preemption-friendly versions of the following
 * lock-spinning functions:
 *
 *         _[spin|read|write]_lock()
 *         _[spin|read|write]_lock_irq()
 *         _[spin|read|write]_lock_irqsave()
 *         _[spin|read|write]_lock_bh()
 */
BUILD_LOCK_OPS(spin, spinlock);
BUILD_LOCK_OPS(read, rwlock);
BUILD_LOCK_OPS(write, rwlock);

#endif /* CONFIG_PREEMPT */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

void __lockfunc _spin_lock_nested(spinlock_t *lock, int subclass)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_spin_lock_nested);

unsigned long __lockfunc _spin_lock_irqsave_nested(spinlock_t *lock, int subclass)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_spin_trylock, _raw_spin_lock,
				_raw_spin_lock_flags, &flags);
	return flags;
}
EXPORT_SYMBOL(_spin_lock_irqsave_nested);

void __lockfunc _spin_lock_nest_lock(spinlock_t *lock,
				     struct lockdep_map *nest_lock)
{
	preempt_disable();
	spin_acquire_nest(&lock->dep_map, 0, 0, nest_lock, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_spin_lock_nest_lock);

#endif

#ifndef _spin_unlock
void __lockfunc _spin_unlock(spinlock_t *lock)
{
	__spin_unlock(lock);
}
EXPORT_SYMBOL(_spin_unlock);
#endif

#ifndef _write_unlock
void __lockfunc _write_unlock(rwlock_t *lock)
{
	__write_unlock(lock);
}
EXPORT_SYMBOL(_write_unlock);
#endif

#ifndef _read_unlock
void __lockfunc _read_unlock(rwlock_t *lock)
{
	__read_unlock(lock);
}
EXPORT_SYMBOL(_read_unlock);
#endif

#ifndef _spin_unlock_irqrestore
void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	__spin_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(_spin_unlock_irqrestore);
#endif

#ifndef _spin_unlock_irq
void __lockfunc _spin_unlock_irq(spinlock_t *lock)
{
	__spin_unlock_irq(lock);
}
EXPORT_SYMBOL(_spin_unlock_irq);
#endif

#ifndef _spin_unlock_bh
void __lockfunc _spin_unlock_bh(spinlock_t *lock)
{
	__spin_unlock_bh(lock);
}
EXPORT_SYMBOL(_spin_unlock_bh);
#endif

#ifndef _read_unlock_irqrestore
void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	__read_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(_read_unlock_irqrestore);
#endif

#ifndef _read_unlock_irq
void __lockfunc _read_unlock_irq(rwlock_t *lock)
{
	__read_unlock_irq(lock);
}
EXPORT_SYMBOL(_read_unlock_irq);
#endif

#ifndef _read_unlock_bh
void __lockfunc _read_unlock_bh(rwlock_t *lock)
{
	__read_unlock_bh(lock);
}
EXPORT_SYMBOL(_read_unlock_bh);
#endif

#ifndef _write_unlock_irqrestore
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	__write_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(_write_unlock_irqrestore);
#endif

#ifndef _write_unlock_irq
void __lockfunc _write_unlock_irq(rwlock_t *lock)
{
	__write_unlock_irq(lock);
}
EXPORT_SYMBOL(_write_unlock_irq);
#endif

#ifndef _write_unlock_bh
void __lockfunc _write_unlock_bh(rwlock_t *lock)
{
	__write_unlock_bh(lock);
}
EXPORT_SYMBOL(_write_unlock_bh);
#endif

#ifndef _spin_trylock_bh
int __lockfunc _spin_trylock_bh(spinlock_t *lock)
{
	return __spin_trylock_bh(lock);
}
EXPORT_SYMBOL(_spin_trylock_bh);
#endif

notrace int in_lock_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __lockfunc functions */
	extern char __lock_text_start[], __lock_text_end[];

	return addr >= (unsigned long)__lock_text_start
	&& addr < (unsigned long)__lock_text_end;
}
EXPORT_SYMBOL(in_lock_functions);
