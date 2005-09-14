/*
 * Copyright (2004) Linus Torvalds
 *
 * Author: Zwane Mwaikambo <zwane@fsmlabs.com>
 *
 * Copyright (2004, 2005) Ingo Molnar
 *
 * This file contains the spinlock/rwlock implementations for the
 * SMP and the DEBUG_SPINLOCK cases. (UP-nondebug inlines them)
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

/*
 * Generic declaration of the raw read_trylock() function,
 * architectures are supposed to optimize this:
 */
int __lockfunc generic__raw_read_trylock(raw_rwlock_t *lock)
{
	__raw_read_lock(lock);
	return 1;
}
EXPORT_SYMBOL(generic__raw_read_trylock);

int __lockfunc _spin_trylock(spinlock_t *lock)
{
	preempt_disable();
	if (_raw_spin_trylock(lock))
		return 1;
	
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_spin_trylock);

int __lockfunc _read_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_read_trylock(lock))
		return 1;

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_read_trylock);

int __lockfunc _write_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_write_trylock(lock))
		return 1;

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_write_trylock);

#if !defined(CONFIG_PREEMPT) || !defined(CONFIG_SMP)

void __lockfunc _read_lock(rwlock_t *lock)
{
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock);

unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_spin_lock_flags(lock, &flags);
	return flags;
}
EXPORT_SYMBOL(_spin_lock_irqsave);

void __lockfunc _spin_lock_irq(spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_spin_lock(lock);
}
EXPORT_SYMBOL(_spin_lock_irq);

void __lockfunc _spin_lock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_spin_lock(lock);
}
EXPORT_SYMBOL(_spin_lock_bh);

unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_read_lock(lock);
	return flags;
}
EXPORT_SYMBOL(_read_lock_irqsave);

void __lockfunc _read_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock_irq);

void __lockfunc _read_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock_bh);

unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_write_lock(lock);
	return flags;
}
EXPORT_SYMBOL(_write_lock_irqsave);

void __lockfunc _write_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_write_lock(lock);
}
EXPORT_SYMBOL(_write_lock_irq);

void __lockfunc _write_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_write_lock(lock);
}
EXPORT_SYMBOL(_write_lock_bh);

void __lockfunc _spin_lock(spinlock_t *lock)
{
	preempt_disable();
	_raw_spin_lock(lock);
}

EXPORT_SYMBOL(_spin_lock);

void __lockfunc _write_lock(rwlock_t *lock)
{
	preempt_disable();
	_raw_write_lock(lock);
}

EXPORT_SYMBOL(_write_lock);

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
	preempt_disable();						\
	for (;;) {							\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		preempt_enable();					\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
			cpu_relax();					\
		preempt_disable();					\
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
	preempt_disable();						\
	for (;;) {							\
		local_irq_save(flags);					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		local_irq_restore(flags);				\
									\
		preempt_enable();					\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
			cpu_relax();					\
		preempt_disable();					\
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

void __lockfunc _spin_unlock(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock);

void __lockfunc _write_unlock(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock);

void __lockfunc _read_unlock(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock);

void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	_raw_spin_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock_irqrestore);

void __lockfunc _spin_unlock_irq(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock_irq);

void __lockfunc _spin_unlock_bh(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable();
}
EXPORT_SYMBOL(_spin_unlock_bh);

void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	_raw_read_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irqrestore);

void __lockfunc _read_unlock_irq(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irq);

void __lockfunc _read_unlock_bh(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable();
}
EXPORT_SYMBOL(_read_unlock_bh);

void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	_raw_write_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irqrestore);

void __lockfunc _write_unlock_irq(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irq);

void __lockfunc _write_unlock_bh(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	preempt_enable_no_resched();
	local_bh_enable();
}
EXPORT_SYMBOL(_write_unlock_bh);

int __lockfunc _spin_trylock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	if (_raw_spin_trylock(lock))
		return 1;

	preempt_enable_no_resched();
	local_bh_enable();
	return 0;
}
EXPORT_SYMBOL(_spin_trylock_bh);

int in_lock_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __lockfunc functions */
	extern char __lock_text_start[], __lock_text_end[];

	return addr >= (unsigned long)__lock_text_start
	&& addr < (unsigned long)__lock_text_end;
}
EXPORT_SYMBOL(in_lock_functions);
