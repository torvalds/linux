/* rwsem.h: R/W semaphores implemented using XADD/CMPXCHG for x86_64+
 *
 * Written by David Howells (dhowells@redhat.com).
 * Ported by Andi Kleen <ak@suse.de> to x86-64.
 *
 * Derived from asm-i386/semaphore.h and asm-i386/rwsem.h
 *
 *
 * The MSW of the count is the negated number of active writers and waiting
 * lockers, and the LSW is the total number of active locks
 *
 * The lock count is initialized to 0 (no active and no waiting lockers).
 *
 * When a writer subtracts WRITE_BIAS, it'll get 0xffff0001 for the case of an
 * uncontended lock. This can be determined because XADD returns the old value.
 * Readers increment by 1 and see a positive value when uncontended, negative
 * if there are writers (and maybe) readers waiting (in which case it goes to
 * sleep).
 *
 * The value of WAITING_BIAS supports up to 32766 waiting processes. This can
 * be extended to 65534 by manually checking the whole MSW rather than relying
 * on the S flag.
 *
 * The value of ACTIVE_BIAS supports up to 65535 active processes.
 *
 * This should be totally fair - if anything is waiting, a process that wants a
 * lock will go to the back of the queue. When the currently active lock is
 * released, if there's a writer at the front of the queue, then that and only
 * that will be woken up; if there's a bunch of consecutive readers at the
 * front, then they'll all be woken up, but no other readers will be.
 */

#ifndef _X8664_RWSEM_H
#define _X8664_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "please don't include asm/rwsem.h directly, use linux/rwsem.h instead"
#endif

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/spinlock.h>

struct rwsem_waiter;

extern struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_wake(struct rw_semaphore *);
extern struct rw_semaphore *rwsem_downgrade_wake(struct rw_semaphore *sem);

/*
 * the semaphore definition
 */
struct rw_semaphore {
	signed int		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#if RWSEM_DEBUG
	int			debug;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __RWSEM_INITIALIZER(name) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) \
	__RWSEM_DEBUG_INIT }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning down_read\n\t"
LOCK_PREFIX	"  incl      (%%rdi)\n\t" /* adds 0x00000001, returns the old value */
		"  js        2f\n\t" /* jump if we weren't granted the lock */
		"1:\n\t"
		LOCK_SECTION_START("") \
		"2:\n\t"
		"  call      rwsem_down_read_failed_thunk\n\t"
		"  jmp       1b\n"
		LOCK_SECTION_END \
		"# ending down_read\n\t"
		: "+m"(sem->count)
		: "D"(sem)
		: "memory", "cc");
}


/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	__s32 result, tmp;
	__asm__ __volatile__(
		"# beginning __down_read_trylock\n\t"
		"  movl      %0,%1\n\t"
		"1:\n\t"
		"  movl	     %1,%2\n\t"
		"  addl      %3,%2\n\t"
		"  jle	     2f\n\t"
LOCK_PREFIX	"  cmpxchgl  %2,%0\n\t"
		"  jnz	     1b\n\t"
		"2:\n\t"
		"# ending __down_read_trylock\n\t"
		: "+m"(sem->count), "=&a"(result), "=&r"(tmp)
		: "i"(RWSEM_ACTIVE_READ_BIAS)
		: "memory", "cc");
	return result>=0 ? 1 : 0;
}


/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	int tmp;

	tmp = RWSEM_ACTIVE_WRITE_BIAS;
	__asm__ __volatile__(
		"# beginning down_write\n\t"
LOCK_PREFIX	"  xaddl      %0,(%%rdi)\n\t" /* subtract 0x0000ffff, returns the old value */
		"  testl     %0,%0\n\t" /* was the count 0 before? */
		"  jnz       2f\n\t" /* jump if we weren't granted the lock */
		"1:\n\t"
		LOCK_SECTION_START("")
		"2:\n\t"
		"  call      rwsem_down_write_failed_thunk\n\t"
		"  jmp       1b\n"
		LOCK_SECTION_END
		"# ending down_write"
		: "=&r" (tmp) 
		: "0"(tmp), "D"(sem)
		: "memory", "cc");
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	signed long ret = cmpxchg(&sem->count,
				  RWSEM_UNLOCKED_VALUE, 
				  RWSEM_ACTIVE_WRITE_BIAS);
	if (ret == RWSEM_UNLOCKED_VALUE)
		return 1;
	return 0;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	__s32 tmp = -RWSEM_ACTIVE_READ_BIAS;
	__asm__ __volatile__(
		"# beginning __up_read\n\t"
LOCK_PREFIX	"  xaddl      %[tmp],(%%rdi)\n\t" /* subtracts 1, returns the old value */
		"  js        2f\n\t" /* jump if the lock is being waited upon */
		"1:\n\t"
		LOCK_SECTION_START("")
		"2:\n\t"
		"  decw      %w[tmp]\n\t" /* do nothing if still outstanding active readers */
		"  jnz       1b\n\t"
		"  call      rwsem_wake_thunk\n\t"
		"  jmp       1b\n"
		LOCK_SECTION_END
		"# ending __up_read\n"
		: "+m"(sem->count), [tmp] "+r" (tmp)
		: "D"(sem)
		: "memory", "cc");
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	unsigned tmp; 
	__asm__ __volatile__(
		"# beginning __up_write\n\t"
		"  movl     %[bias],%[tmp]\n\t"
LOCK_PREFIX	"  xaddl     %[tmp],(%%rdi)\n\t" /* tries to transition 0xffff0001 -> 0x00000000 */
		"  jnz       2f\n\t" /* jump if the lock is being waited upon */
		"1:\n\t"
		LOCK_SECTION_START("")
		"2:\n\t"
		"  decw      %w[tmp]\n\t" /* did the active count reduce to 0? */
		"  jnz       1b\n\t" /* jump back if not */
		"  call      rwsem_wake_thunk\n\t"
		"  jmp       1b\n"
		LOCK_SECTION_END
		"# ending __up_write\n"
		: "+m"(sem->count), [tmp] "=r" (tmp)
		: "D"(sem), [bias] "i"(-RWSEM_ACTIVE_WRITE_BIAS)
		: "memory", "cc");
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning __downgrade_write\n\t"
LOCK_PREFIX	"  addl      %[bias],(%%rdi)\n\t" /* transitions 0xZZZZ0001 -> 0xYYYY0001 */
		"  js        2f\n\t" /* jump if the lock is being waited upon */
		"1:\n\t"
		LOCK_SECTION_START("")
		"2:\n\t"
		"  call	     rwsem_downgrade_thunk\n"
		"  jmp       1b\n"
		LOCK_SECTION_END
		"# ending __downgrade_write\n"
		: "=m"(sem->count)
		: "D"(sem), [bias] "i"(-RWSEM_WAITING_BIAS), "m"(sem->count)
		: "memory", "cc");
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(int delta, struct rw_semaphore *sem)
{
	__asm__ __volatile__(
LOCK_PREFIX	"addl %1,%0"
		:"=m"(sem->count)
		:"ir"(delta), "m"(sem->count));
}

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int tmp = delta;

	__asm__ __volatile__(
LOCK_PREFIX	"xaddl %0,(%2)"
		: "=r"(tmp), "=m"(sem->count)
		: "r"(sem), "m"(sem->count), "0" (tmp)
		: "memory");

	return tmp+delta;
}

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return (sem->count != 0);
}

#endif /* __KERNEL__ */
#endif /* _X8664_RWSEM_H */
