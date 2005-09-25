#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>

/*
 * Simple spin lock operations.
 *
 * (the type definitions are in asm/raw_spinlock_types.h)
 */

#define __raw_spin_is_locked(x)		((x)->lock != 0)
#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) cpu_relax(); } while (0)
#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__(
	"b	1f		# __raw_spin_lock\n\
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

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__("eieio	# __raw_spin_unlock": : :"memory");
	lock->lock = 0;
}

#define __raw_spin_trylock(l) (!test_and_set_bit(0,&(l)->lock))

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

#define __raw_read_can_lock(rw)	((rw)->lock >= 0)
#define __raw_write_can_lock(rw)	(!(rw)->lock)

static __inline__ int __raw_read_trylock(raw_rwlock_t *rw)
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

static __inline__ void __raw_read_lock(raw_rwlock_t *rw)
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

static __inline__ void __raw_read_unlock(raw_rwlock_t *rw)
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

static __inline__ int __raw_write_trylock(raw_rwlock_t *rw)
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

static __inline__ void __raw_write_lock(raw_rwlock_t *rw)
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

static __inline__ void __raw_write_unlock(raw_rwlock_t *rw)
{
	__asm__ __volatile__("eieio		# write_unlock": : :"memory");
	rw->lock = 0;
}

#endif /* __ASM_SPINLOCK_H */
