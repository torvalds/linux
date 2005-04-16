#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.  
 *
 * Copyright (C) 2001-2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2002 Dave Engebretsen <engebret@us.ibm.com>, IBM
 *	Rework to support virtual processors
 *
 * Type of int is used as a full 64b word is not necessary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/iSeries/HvCall.h>

typedef struct {
	volatile unsigned int lock;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} spinlock_t;

typedef struct {
	volatile signed int lock;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#ifdef __KERNEL__
#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 }

#define spin_is_locked(x)	((x)->lock != 0)
#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

static __inline__ void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("lwsync	# spin_unlock": : :"memory");
	lock->lock = 0;
}

/*
 * On a system with shared processors (that is, where a physical
 * processor is multiplexed between several virtual processors),
 * there is no point spinning on a lock if the holder of the lock
 * isn't currently scheduled on a physical processor.  Instead
 * we detect this situation and ask the hypervisor to give the
 * rest of our timeslice to the lock holder.
 *
 * So that we can tell which virtual processor is holding a lock,
 * we put 0x80000000 | smp_processor_id() in the lock when it is
 * held.  Conveniently, we have a word in the paca that holds this
 * value.
 */

#if defined(CONFIG_PPC_SPLPAR) || defined(CONFIG_PPC_ISERIES)
/* We only yield to the hypervisor if we are in shared processor mode */
#define SHARED_PROCESSOR (get_paca()->lppaca.shared_proc)
extern void __spin_yield(spinlock_t *lock);
extern void __rw_yield(rwlock_t *lock);
#else /* SPLPAR || ISERIES */
#define __spin_yield(x)	barrier()
#define __rw_yield(x)	barrier()
#define SHARED_PROCESSOR	0
#endif
extern void spin_unlock_wait(spinlock_t *lock);

/*
 * This returns the old value in the lock, so we succeeded
 * in getting the lock if the return value is 0.
 */
static __inline__ unsigned long __spin_trylock(spinlock_t *lock)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__(
"	lwz		%1,%3(13)		# __spin_trylock\n\
1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&lock->lock), "i" (offsetof(struct paca_struct, lock_token))
	: "cr0", "memory");

	return tmp;
}

static int __inline__ _raw_spin_trylock(spinlock_t *lock)
{
	return __spin_trylock(lock) == 0;
}

static void __inline__ _raw_spin_lock(spinlock_t *lock)
{
	while (1) {
		if (likely(__spin_trylock(lock) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (likely(lock->lock != 0));
		HMT_medium();
	}
}

static void __inline__ _raw_spin_lock_flags(spinlock_t *lock, unsigned long flags)
{
	unsigned long flags_dis;

	while (1) {
		if (likely(__spin_trylock(lock) == 0))
			break;
		local_save_flags(flags_dis);
		local_irq_restore(flags);
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (likely(lock->lock != 0));
		HMT_medium();
		local_irq_restore(flags_dis);
	}
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
#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

#define rwlock_init(x)		do { *(x) = RW_LOCK_UNLOCKED; } while(0)

#define read_can_lock(rw)	((rw)->lock >= 0)
#define write_can_lock(rw)	(!(rw)->lock)

static __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("lwsync		# write_unlock": : :"memory");
	rw->lock = 0;
}

/*
 * This returns the old value in the lock + 1,
 * so we got a read lock if the return value is > 0.
 */
static long __inline__ __read_trylock(rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%1		# read_trylock\n\
	extsw		%0,%0\n\
	addic.		%0,%0,1\n\
	ble-		2f\n\
	stwcx.		%0,0,%1\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "xer", "memory");

	return tmp;
}

static int __inline__ _raw_read_trylock(rwlock_t *rw)
{
	return __read_trylock(rw) > 0;
}

static void __inline__ _raw_read_lock(rwlock_t *rw)
{
	while (1) {
		if (likely(__read_trylock(rw) > 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (likely(rw->lock < 0));
		HMT_medium();
	}
}

static void __inline__ _raw_read_unlock(rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
	"eieio				# read_unlock\n\
1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n\
	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

/*
 * This returns the old value in the lock,
 * so we got the write lock if the return value is 0.
 */
static __inline__ long __write_trylock(rwlock_t *rw)
{
	long tmp, tmp2;

	__asm__ __volatile__(
"	lwz		%1,%3(13)	# write_trylock\n\
1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock), "i" (offsetof(struct paca_struct, lock_token))
	: "cr0", "memory");

	return tmp;
}

static int __inline__ _raw_write_trylock(rwlock_t *rw)
{
	return __write_trylock(rw) == 0;
}

static void __inline__ _raw_write_lock(rwlock_t *rw)
{
	while (1) {
		if (likely(__write_trylock(rw) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (likely(rw->lock != 0));
		HMT_medium();
	}
}

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
