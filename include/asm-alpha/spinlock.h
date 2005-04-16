#ifndef _ALPHA_SPINLOCK_H
#define _ALPHA_SPINLOCK_H

#include <linux/config.h>
#include <asm/system.h>
#include <linux/kernel.h>
#include <asm/current.h>


/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned int lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	int on_cpu;
	int line_no;
	void *previous;
	struct task_struct * task;
	const char *base_file;
#endif
} spinlock_t;

#ifdef CONFIG_DEBUG_SPINLOCK
#define SPIN_LOCK_UNLOCKED	(spinlock_t){ 0, -1, 0, NULL, NULL, NULL }
#else
#define SPIN_LOCK_UNLOCKED	(spinlock_t){ 0 }
#endif

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)
#define spin_is_locked(x)	((x)->lock != 0)
#define spin_unlock_wait(x)	do { barrier(); } while ((x)->lock)

#ifdef CONFIG_DEBUG_SPINLOCK
extern void _raw_spin_unlock(spinlock_t * lock);
extern void debug_spin_lock(spinlock_t * lock, const char *, int);
extern int debug_spin_trylock(spinlock_t * lock, const char *, int);
#define _raw_spin_lock(LOCK) \
	debug_spin_lock(LOCK, __BASE_FILE__, __LINE__)
#define _raw_spin_trylock(LOCK) \
	debug_spin_trylock(LOCK, __BASE_FILE__, __LINE__)
#else
static inline void _raw_spin_unlock(spinlock_t * lock)
{
	mb();
	lock->lock = 0;
}

static inline void _raw_spin_lock(spinlock_t * lock)
{
	long tmp;

	__asm__ __volatile__(
	"1:	ldl_l	%0,%1\n"
	"	bne	%0,2f\n"
	"	lda	%0,1\n"
	"	stl_c	%0,%1\n"
	"	beq	%0,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	ldl	%0,%1\n"
	"	bne	%0,2b\n"
	"	br	1b\n"
	".previous"
	: "=&r" (tmp), "=m" (lock->lock)
	: "m"(lock->lock) : "memory");
}

static inline int _raw_spin_trylock(spinlock_t *lock)
{
	return !test_and_set_bit(0, &lock->lock);
}
#endif /* CONFIG_DEBUG_SPINLOCK */

#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

/***********************************************************/

typedef struct {
	volatile unsigned int lock;
} rwlock_t;

#define RW_LOCK_UNLOCKED	(rwlock_t){ 0 }

#define rwlock_init(x)		do { *(x) = RW_LOCK_UNLOCKED; } while(0)

static inline int read_can_lock(rwlock_t *lock)
{
	return (lock->lock & 1) == 0;
}

static inline int write_can_lock(rwlock_t *lock)
{
	return lock->lock == 0;
}

#ifdef CONFIG_DEBUG_RWLOCK
extern void _raw_write_lock(rwlock_t * lock);
extern void _raw_read_lock(rwlock_t * lock);
#else
static inline void _raw_write_lock(rwlock_t * lock)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	bne	%1,6f\n"
	"	lda	%1,1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"	mb\n"
	".subsection 2\n"
	"6:	ldl	%1,%0\n"
	"	bne	%1,6b\n"
	"	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}

static inline void _raw_read_lock(rwlock_t * lock)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	blbs	%1,6f\n"
	"	subl	%1,2,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	"	mb\n"
	".subsection 2\n"
	"6:	ldl	%1,%0\n"
	"	blbs	%1,6b\n"
	"	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}
#endif /* CONFIG_DEBUG_RWLOCK */

static inline int _raw_read_trylock(rwlock_t * lock)
{
	long regx;
	int success;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	lda	%2,0\n"
	"	blbs	%1,2f\n"
	"	subl	%1,2,%2\n"
	"	stl_c	%2,%0\n"
	"	beq	%2,6f\n"
	"2:	mb\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx), "=&r" (success)
	: "m" (*lock) : "memory");

	return success;
}

static inline int _raw_write_trylock(rwlock_t * lock)
{
	long regx;
	int success;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0\n"
	"	lda	%2,0\n"
	"	bne	%1,2f\n"
	"	lda	%2,1\n"
	"	stl_c	%2,%0\n"
	"	beq	%2,6f\n"
	"2:	mb\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx), "=&r" (success)
	: "m" (*lock) : "memory");

	return success;
}

static inline void _raw_write_unlock(rwlock_t * lock)
{
	mb();
	lock->lock = 0;
}

static inline void _raw_read_unlock(rwlock_t * lock)
{
	long regx;
	__asm__ __volatile__(
	"	mb\n"
	"1:	ldl_l	%1,%0\n"
	"	addl	%1,2,%1\n"
	"	stl_c	%1,%0\n"
	"	beq	%1,6f\n"
	".subsection 2\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (*lock), "=&r" (regx)
	: "m" (*lock) : "memory");
}

#endif /* _ALPHA_SPINLOCK_H */
