/*
 *  include/asm-s390/spinlock.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/spinlock.h"
 */

#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifdef __s390x__
/*
 * Grmph, take care of %&#! user space programs that include
 * asm/spinlock.h. The diagnose is only available in kernel
 * context.
 */
#ifdef __KERNEL__
#include <asm/lowcore.h>
#define __DIAG44_INSN "ex"
#define __DIAG44_OPERAND __LC_DIAG44_OPCODE
#else
#define __DIAG44_INSN "#"
#define __DIAG44_OPERAND 0
#endif
#endif /* __s390x__ */

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned int lock;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} __attribute__ ((aligned (4))) spinlock_t;

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#define spin_lock_init(lp) do { (lp)->lock = 0; } while(0)
#define spin_unlock_wait(lp)	do { barrier(); } while(((volatile spinlock_t *)(lp))->lock)
#define spin_is_locked(x) ((x)->lock != 0)
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

extern inline void _raw_spin_lock(spinlock_t *lp)
{
#ifndef __s390x__
	unsigned int reg1, reg2;
        __asm__ __volatile__("    bras  %0,1f\n"
                           "0:  diag  0,0,68\n"
                           "1:  slr   %1,%1\n"
                           "    cs    %1,%0,0(%3)\n"
                           "    jl    0b\n"
                           : "=&d" (reg1), "=&d" (reg2), "=m" (lp->lock)
			   : "a" (&lp->lock), "m" (lp->lock)
			   : "cc", "memory" );
#else /* __s390x__ */
	unsigned long reg1, reg2;
        __asm__ __volatile__("    bras  %1,1f\n"
                           "0:  " __DIAG44_INSN " 0,%4\n"
                           "1:  slr   %0,%0\n"
                           "    cs    %0,%1,0(%3)\n"
                           "    jl    0b\n"
                           : "=&d" (reg1), "=&d" (reg2), "=m" (lp->lock)
			   : "a" (&lp->lock), "i" (__DIAG44_OPERAND),
			     "m" (lp->lock) : "cc", "memory" );
#endif /* __s390x__ */
}

extern inline int _raw_spin_trylock(spinlock_t *lp)
{
	unsigned long reg;
	unsigned int result;

	__asm__ __volatile__("    basr  %1,0\n"
			   "0:  cs    %0,%1,0(%3)"
			   : "=d" (result), "=&d" (reg), "=m" (lp->lock)
			   : "a" (&lp->lock), "m" (lp->lock), "0" (0)
			   : "cc", "memory" );
	return !result;
}

extern inline void _raw_spin_unlock(spinlock_t *lp)
{
	unsigned int old;

	__asm__ __volatile__("cs %0,%3,0(%4)"
			   : "=d" (old), "=m" (lp->lock)
			   : "0" (lp->lock), "d" (0), "a" (lp)
			   : "cc", "memory" );
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
typedef struct {
	volatile unsigned long lock;
	volatile unsigned long owner_pc;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

#define rwlock_init(x)	do { *(x) = RW_LOCK_UNLOCKED; } while(0)

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define read_can_lock(x) ((int)(x)->lock >= 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define write_can_lock(x) ((x)->lock == 0)

#ifndef __s390x__
#define _raw_read_lock(rw)   \
        asm volatile("   l     2,0(%1)\n"   \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: la    2,0(2)\n"     /* clear high (=write) bit */ \
                     "   la    3,1(2)\n"     /* one more reader */ \
                     "   cs    2,3,0(%1)\n"  /* try to write new value */ \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) : "a" (&(rw)->lock), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#else /* __s390x__ */
#define _raw_read_lock(rw)   \
        asm volatile("   lg    2,0(%1)\n"   \
                     "   j     1f\n"     \
                     "0: " __DIAG44_INSN " 0,%2\n" \
                     "1: nihh  2,0x7fff\n" /* clear high (=write) bit */ \
                     "   la    3,1(2)\n"   /* one more reader */  \
                     "   csg   2,3,0(%1)\n" /* try to write new value */ \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) \
		     : "a" (&(rw)->lock), "i" (__DIAG44_OPERAND), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#endif /* __s390x__ */

#ifndef __s390x__
#define _raw_read_unlock(rw) \
        asm volatile("   l     2,0(%1)\n"   \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: lr    3,2\n"    \
                     "   ahi   3,-1\n"    /* one less reader */ \
                     "   cs    2,3,0(%1)\n" \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) : "a" (&(rw)->lock), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#else /* __s390x__ */
#define _raw_read_unlock(rw) \
        asm volatile("   lg    2,0(%1)\n"   \
                     "   j     1f\n"     \
                     "0: " __DIAG44_INSN " 0,%2\n" \
                     "1: lgr   3,2\n"    \
                     "   bctgr 3,0\n"    /* one less reader */ \
                     "   csg   2,3,0(%1)\n" \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) \
		     : "a" (&(rw)->lock), "i" (__DIAG44_OPERAND), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#endif /* __s390x__ */

#ifndef __s390x__
#define _raw_write_lock(rw) \
        asm volatile("   lhi   3,1\n"    \
                     "   sll   3,31\n"    /* new lock value = 0x80000000 */ \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: slr   2,2\n"     /* old lock value must be 0 */ \
                     "   cs    2,3,0(%1)\n" \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) : "a" (&(rw)->lock), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#else /* __s390x__ */
#define _raw_write_lock(rw) \
        asm volatile("   llihh 3,0x8000\n" /* new lock value = 0x80...0 */ \
                     "   j     1f\n"       \
                     "0: " __DIAG44_INSN " 0,%2\n"   \
                     "1: slgr  2,2\n"      /* old lock value must be 0 */ \
                     "   csg   2,3,0(%1)\n" \
                     "   jl    0b"         \
                     : "=m" ((rw)->lock) \
		     : "a" (&(rw)->lock), "i" (__DIAG44_OPERAND), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#endif /* __s390x__ */

#ifndef __s390x__
#define _raw_write_unlock(rw) \
        asm volatile("   slr   3,3\n"     /* new lock value = 0 */ \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: lhi   2,1\n"    \
                     "   sll   2,31\n"    /* old lock value must be 0x80000000 */ \
                     "   cs    2,3,0(%1)\n" \
                     "   jl    0b"       \
                     : "=m" ((rw)->lock) : "a" (&(rw)->lock), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#else /* __s390x__ */
#define _raw_write_unlock(rw) \
        asm volatile("   slgr  3,3\n"      /* new lock value = 0 */ \
                     "   j     1f\n"       \
                     "0: " __DIAG44_INSN " 0,%2\n"   \
                     "1: llihh 2,0x8000\n" /* old lock value must be 0x8..0 */\
                     "   csg   2,3,0(%1)\n"   \
                     "   jl    0b"         \
                     : "=m" ((rw)->lock) \
		     : "a" (&(rw)->lock), "i" (__DIAG44_OPERAND), \
		       "m" ((rw)->lock) : "2", "3", "cc", "memory" )
#endif /* __s390x__ */

#define _raw_read_trylock(lock) generic_raw_read_trylock(lock)

extern inline int _raw_write_trylock(rwlock_t *rw)
{
	unsigned long result, reg;
	
	__asm__ __volatile__(
#ifndef __s390x__
			     "   lhi  %1,1\n"
			     "   sll  %1,31\n"
			     "   cs   %0,%1,0(%3)"
#else /* __s390x__ */
			     "   llihh %1,0x8000\n"
			     "0: csg %0,%1,0(%3)\n"
#endif /* __s390x__ */
			     : "=d" (result), "=&d" (reg), "=m" (rw->lock)
			     : "a" (&rw->lock), "m" (rw->lock), "0" (0UL)
			     : "cc", "memory" );
	return result == 0;
}

#endif /* __ASM_SPINLOCK_H */
