#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H
#ifdef __KERNEL__

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
 *
 * (the type definitions are in asm/spinlock_types.h)
 */
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/iseries/hv_call.h>
#endif
#include <asm/asm-compat.h>
#include <asm/synch.h>

#define __raw_spin_is_locked(x)		((x)->slock != 0)

#ifdef CONFIG_PPC64
/* use 0x800000yy when locked, where yy == CPU number */
#define LOCK_TOKEN	(*(u32 *)(&get_paca()->lock_token))
#else
#define LOCK_TOKEN	1
#endif

/*
 * This returns the old value in the lock, so we succeeded
 * in getting the lock if the return value is 0.
 */
static __inline__ unsigned long __spin_trylock(raw_spinlock_t *lock)
{
	unsigned long tmp, token;

	token = LOCK_TOKEN;
	__asm__ __volatile__(
"1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (token), "r" (&lock->slock)
	: "cr0", "memory");

	return tmp;
}

static int __inline__ __raw_spin_trylock(raw_spinlock_t *lock)
{
	return __spin_trylock(lock) == 0;
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
#define SHARED_PROCESSOR (get_lppaca()->shared_proc)
extern void __spin_yield(raw_spinlock_t *lock);
extern void __rw_yield(raw_rwlock_t *lock);
#else /* SPLPAR || ISERIES */
#define __spin_yield(x)	barrier()
#define __rw_yield(x)	barrier()
#define SHARED_PROCESSOR	0
#endif

static void __inline__ __raw_spin_lock(raw_spinlock_t *lock)
{
	while (1) {
		if (likely(__spin_trylock(lock) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (unlikely(lock->slock != 0));
		HMT_medium();
	}
}

static void __inline__ __raw_spin_lock_flags(raw_spinlock_t *lock, unsigned long flags)
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
		} while (unlikely(lock->slock != 0));
		HMT_medium();
		local_irq_restore(flags_dis);
	}
}

static __inline__ void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__("# __raw_spin_unlock\n\t"
				LWSYNC_ON_SMP: : :"memory");
	lock->slock = 0;
}

#ifdef CONFIG_PPC64
extern void __raw_spin_unlock_wait(raw_spinlock_t *lock);
#else
#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) cpu_relax(); } while (0)
#endif

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

#define __raw_read_can_lock(rw)		((rw)->lock >= 0)
#define __raw_write_can_lock(rw)	(!(rw)->lock)

#ifdef CONFIG_PPC64
#define __DO_SIGN_EXTEND	"extsw	%0,%0\n"
#define WRLOCK_TOKEN		LOCK_TOKEN	/* it's negative */
#else
#define __DO_SIGN_EXTEND
#define WRLOCK_TOKEN		(-1)
#endif

/*
 * This returns the old value in the lock + 1,
 * so we got a read lock if the return value is > 0.
 */
static long __inline__ __read_trylock(raw_rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%1\n"
	__DO_SIGN_EXTEND
"	addic.		%0,%0,1\n\
	ble-		2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%0,0,%1\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "xer", "memory");

	return tmp;
}

/*
 * This returns the old value in the lock,
 * so we got the write lock if the return value is 0.
 */
static __inline__ long __write_trylock(raw_rwlock_t *rw)
{
	long tmp, token;

	token = WRLOCK_TOKEN;
	__asm__ __volatile__(
"1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (token), "r" (&rw->lock)
	: "cr0", "memory");

	return tmp;
}

static void __inline__ __raw_read_lock(raw_rwlock_t *rw)
{
	while (1) {
		if (likely(__read_trylock(rw) > 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (unlikely(rw->lock < 0));
		HMT_medium();
	}
}

static void __inline__ __raw_write_lock(raw_rwlock_t *rw)
{
	while (1) {
		if (likely(__write_trylock(rw) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (unlikely(rw->lock != 0));
		HMT_medium();
	}
}

static int __inline__ __raw_read_trylock(raw_rwlock_t *rw)
{
	return __read_trylock(rw) > 0;
}

static int __inline__ __raw_write_trylock(raw_rwlock_t *rw)
{
	return __write_trylock(rw) == 0;
}

static void __inline__ __raw_read_unlock(raw_rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
	"# read_unlock\n\t"
	LWSYNC_ON_SMP
"1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

static __inline__ void __raw_write_unlock(raw_rwlock_t *rw)
{
	__asm__ __volatile__("# write_unlock\n\t"
				LWSYNC_ON_SMP: : :"memory");
	rw->lock = 0;
}

#endif /* __KERNEL__ */
#endif /* __ASM_SPINLOCK_H */
