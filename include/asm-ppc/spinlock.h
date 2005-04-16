#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>

/*
 * Simple spin lock operations.
 */

typedef struct {
	volatile unsigned long lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	volatile unsigned long owner_pc;
	volatile unsigned long owner_cpu;
#endif
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} spinlock_t;

#ifdef __KERNEL__
#ifdef CONFIG_DEBUG_SPINLOCK
#define SPINLOCK_DEBUG_INIT     , 0, 0
#else
#define SPINLOCK_DEBUG_INIT     /* */
#endif

#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 SPINLOCK_DEBUG_INIT }

#define spin_lock_init(x) 	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)
#define spin_is_locked(x)	((x)->lock != 0)
#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

#ifndef CONFIG_DEBUG_SPINLOCK

static inline void _raw_spin_lock(spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
	"b	1f		# spin_lock\n\
2:	lwzx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne+	2b\n\
1:	lwarx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne-	2b\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%2,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&lock->lock), "r"(1)
	: "cr0", "memory");
}

static inline void _raw_spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("eieio		# spin_unlock": : :"memory");
	lock->lock = 0;
}

#define _raw_spin_trylock(l) (!test_and_set_bit(0,&(l)->lock))

#else

extern void _raw_spin_lock(spinlock_t *lock);
extern void _raw_spin_unlock(spinlock_t *lock);
extern int _raw_spin_trylock(spinlock_t *lock);

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
typedef struct {
	volatile signed int lock;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#define rwlock_init(lp) do { *(lp) = RW_LOCK_UNLOCKED; } while(0)

#define read_can_lock(rw)	((rw)->lock >= 0)
#define write_can_lock(rw)	(!(rw)->lock)

#ifndef CONFIG_DEBUG_SPINLOCK

static __inline__ int _raw_read_trylock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
"2:	lwarx	%0,0,%1		# read_trylock\n\
	addic.	%0,%0,1\n\
	ble-	1f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	2b\n\
	isync\n\
1:"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");

	return tmp > 0;
}

static __inline__ void _raw_read_lock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
	"b	2f		# read_lock\n\
1:	lwzx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	blt+	1b\n\
2:	lwarx	%0,0,%1\n\
	addic.	%0,%0,1\n\
	ble-	1b\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

static __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
	"eieio			# read_unlock\n\
1:	lwarx	%0,0,%1\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

static __inline__ int _raw_write_trylock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
"2:	lwarx	%0,0,%1		# write_trylock\n\
	cmpwi	0,%0,0\n\
	bne-	1f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%2,0,%1\n\
	bne-	2b\n\
	isync\n\
1:"
	: "=&r"(tmp)
	: "r"(&rw->lock), "r"(-1)
	: "cr0", "memory");

	return tmp == 0;
}

static __inline__ void _raw_write_lock(rwlock_t *rw)
{
	signed int tmp;

	__asm__ __volatile__(
	"b	2f		# write_lock\n\
1:  	lwzx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne+	1b\n\
2:	lwarx	%0,0,%1\n\
	cmpwi	0,%0,0\n\
	bne-	1b\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%2,0,%1\n\
	bne-	2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&rw->lock), "r"(-1)
	: "cr0", "memory");
}

static __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	__asm__ __volatile__("eieio		# write_unlock": : :"memory");
	rw->lock = 0;
}

#else

extern void _raw_read_lock(rwlock_t *rw);
extern void _raw_read_unlock(rwlock_t *rw);
extern void _raw_write_lock(rwlock_t *rw);
extern void _raw_write_unlock(rwlock_t *rw);
extern int _raw_read_trylock(rwlock_t *rw);
extern int _raw_write_trylock(rwlock_t *rw);

#endif

#endif /* __ASM_SPINLOCK_H */
#endif /* __KERNEL__ */
