#ifndef _ASM_IA64_SPINLOCK_H
#define _ASM_IA64_SPINLOCK_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * This file is used for SMP configurations only.
 */

#include <linux/compiler.h>
#include <linux/kernel.h>

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/intrinsics.h>
#include <asm/system.h>

#define __raw_spin_lock_init(x)			((x)->lock = 0)

#ifdef ASM_SUPPORTED
/*
 * Try to get the lock.  If we fail to get the lock, make a non-standard call to
 * ia64_spinlock_contention().  We do not use a normal call because that would force all
 * callers of __raw_spin_lock() to be non-leaf routines.  Instead, ia64_spinlock_contention() is
 * carefully coded to touch only those registers that __raw_spin_lock() marks "clobbered".
 */

#define IA64_SPINLOCK_CLOBBERS "ar.ccv", "ar.pfs", "p14", "p15", "r27", "r28", "r29", "r30", "b6", "memory"

static inline void
__raw_spin_lock_flags (raw_spinlock_t *lock, unsigned long flags)
{
	register volatile unsigned int *ptr asm ("r31") = &lock->lock;

#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
# ifdef CONFIG_ITANIUM
	/* don't use brl on Itanium... */
	asm volatile ("{\n\t"
		      "  mov ar.ccv = r0\n\t"
		      "  mov r28 = ip\n\t"
		      "  mov r30 = 1;;\n\t"
		      "}\n\t"
		      "cmpxchg4.acq r30 = [%1], r30, ar.ccv\n\t"
		      "movl r29 = ia64_spinlock_contention_pre3_4;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "mov b6 = r29;;\n\t"
		      "mov r27=%2\n\t"
		      "(p14) br.cond.spnt.many b6"
		      : "=r"(ptr) : "r"(ptr), "r" (flags) : IA64_SPINLOCK_CLOBBERS);
# else
	asm volatile ("{\n\t"
		      "  mov ar.ccv = r0\n\t"
		      "  mov r28 = ip\n\t"
		      "  mov r30 = 1;;\n\t"
		      "}\n\t"
		      "cmpxchg4.acq r30 = [%1], r30, ar.ccv;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "mov r27=%2\n\t"
		      "(p14) brl.cond.spnt.many ia64_spinlock_contention_pre3_4;;"
		      : "=r"(ptr) : "r"(ptr), "r" (flags) : IA64_SPINLOCK_CLOBBERS);
# endif /* CONFIG_MCKINLEY */
#else
# ifdef CONFIG_ITANIUM
	/* don't use brl on Itanium... */
	/* mis-declare, so we get the entry-point, not it's function descriptor: */
	asm volatile ("mov r30 = 1\n\t"
		      "mov r27=%2\n\t"
		      "mov ar.ccv = r0;;\n\t"
		      "cmpxchg4.acq r30 = [%0], r30, ar.ccv\n\t"
		      "movl r29 = ia64_spinlock_contention;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "mov b6 = r29;;\n\t"
		      "(p14) br.call.spnt.many b6 = b6"
		      : "=r"(ptr) : "r"(ptr), "r" (flags) : IA64_SPINLOCK_CLOBBERS);
# else
	asm volatile ("mov r30 = 1\n\t"
		      "mov r27=%2\n\t"
		      "mov ar.ccv = r0;;\n\t"
		      "cmpxchg4.acq r30 = [%0], r30, ar.ccv;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "(p14) brl.call.spnt.many b6=ia64_spinlock_contention;;"
		      : "=r"(ptr) : "r"(ptr), "r" (flags) : IA64_SPINLOCK_CLOBBERS);
# endif /* CONFIG_MCKINLEY */
#endif
}

#define __raw_spin_lock(lock) __raw_spin_lock_flags(lock, 0)

/* Unlock by doing an ordered store and releasing the cacheline with nta */
static inline void __raw_spin_unlock(raw_spinlock_t *x) {
	barrier();
	asm volatile ("st4.rel.nta [%0] = r0\n\t" :: "r"(x));
}

#else /* !ASM_SUPPORTED */
#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)
# define __raw_spin_lock(x)								\
do {											\
	__u32 *ia64_spinlock_ptr = (__u32 *) (x);					\
	__u64 ia64_spinlock_val;							\
	ia64_spinlock_val = ia64_cmpxchg4_acq(ia64_spinlock_ptr, 1, 0);			\
	if (unlikely(ia64_spinlock_val)) {						\
		do {									\
			while (*ia64_spinlock_ptr)					\
				ia64_barrier();						\
			ia64_spinlock_val = ia64_cmpxchg4_acq(ia64_spinlock_ptr, 1, 0);	\
		} while (ia64_spinlock_val);						\
	}										\
} while (0)
#define __raw_spin_unlock(x)	do { barrier(); ((raw_spinlock_t *) x)->lock = 0; } while (0)
#endif /* !ASM_SUPPORTED */

#define __raw_spin_is_locked(x)		((x)->lock != 0)
#define __raw_spin_trylock(x)		(cmpxchg_acq(&(x)->lock, 0, 1) == 0)
#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) cpu_relax(); } while (0)

#define __raw_read_can_lock(rw)		(*(volatile int *)(rw) >= 0)
#define __raw_write_can_lock(rw)	(*(volatile int *)(rw) == 0)

#define __raw_read_lock(rw)								\
do {											\
	raw_rwlock_t *__read_lock_ptr = (rw);						\
											\
	while (unlikely(ia64_fetchadd(1, (int *) __read_lock_ptr, acq) < 0)) {		\
		ia64_fetchadd(-1, (int *) __read_lock_ptr, rel);			\
		while (*(volatile int *)__read_lock_ptr < 0)				\
			cpu_relax();							\
	}										\
} while (0)

#define __raw_read_unlock(rw)					\
do {								\
	raw_rwlock_t *__read_lock_ptr = (rw);			\
	ia64_fetchadd(-1, (int *) __read_lock_ptr, rel);	\
} while (0)

#ifdef ASM_SUPPORTED
#define __raw_write_lock(rw)							\
do {										\
 	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1;;\n"					\
		"1:\n"								\
		"ld4 r2 = [%0];;\n"						\
		"cmp4.eq p0,p7 = r0,r2\n"					\
		"(p7) br.cond.spnt.few 1b \n"					\
		"cmpxchg4.acq r2 = [%0], r29, ar.ccv;;\n"			\
		"cmp4.eq p0,p7 = r0, r2\n"					\
		"(p7) br.cond.spnt.few 1b;;\n"					\
		:: "r"(rw) : "ar.ccv", "p7", "r2", "r29", "memory");		\
} while(0)

#define __raw_write_trylock(rw)							\
({										\
	register long result;							\
										\
	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1;;\n"					\
		"cmpxchg4.acq %0 = [%1], r29, ar.ccv\n"				\
		: "=r"(result) : "r"(rw) : "ar.ccv", "r29", "memory");		\
	(result == 0);								\
})

static inline void __raw_write_unlock(raw_rwlock_t *x)
{
	u8 *y = (u8 *)x;
	barrier();
	asm volatile ("st1.rel.nta [%0] = r0\n\t" :: "r"(y+3) : "memory" );
}

#else /* !ASM_SUPPORTED */

#define __raw_write_lock(l)								\
({											\
	__u64 ia64_val, ia64_set_val = ia64_dep_mi(-1, 0, 31, 1);			\
	__u32 *ia64_write_lock_ptr = (__u32 *) (l);					\
	do {										\
		while (*ia64_write_lock_ptr)						\
			ia64_barrier();							\
		ia64_val = ia64_cmpxchg4_acq(ia64_write_lock_ptr, ia64_set_val, 0);	\
	} while (ia64_val);								\
})

#define __raw_write_trylock(rw)						\
({									\
	__u64 ia64_val;							\
	__u64 ia64_set_val = ia64_dep_mi(-1, 0, 31,1);			\
	ia64_val = ia64_cmpxchg4_acq((__u32 *)(rw), ia64_set_val, 0);	\
	(ia64_val == 0);						\
})

static inline void __raw_write_unlock(raw_rwlock_t *x)
{
	barrier();
	x->write_lock = 0;
}

#endif /* !ASM_SUPPORTED */

#define __raw_read_trylock(lock) generic__raw_read_trylock(lock)

#endif /*  _ASM_IA64_SPINLOCK_H */
