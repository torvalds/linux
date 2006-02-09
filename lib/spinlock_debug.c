/*
 * Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 *
 * This file contains the spinlock/rwlock implementations for
 * DEBUG_SPINLOCK.
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

static void spin_bug(spinlock_t *lock, const char *msg)
{
	static long print_once = 1;
	struct task_struct *owner = NULL;

	if (xchg(&print_once, 0)) {
		if (lock->owner && lock->owner != SPINLOCK_OWNER_INIT)
			owner = lock->owner;
		printk(KERN_EMERG "BUG: spinlock %s on CPU#%d, %s/%d\n",
			msg, raw_smp_processor_id(),
			current->comm, current->pid);
		printk(KERN_EMERG " lock: %p, .magic: %08x, .owner: %s/%d, "
				".owner_cpu: %d\n",
			lock, lock->magic,
			owner ? owner->comm : "<none>",
			owner ? owner->pid : -1,
			lock->owner_cpu);
		dump_stack();
#ifdef CONFIG_SMP
		/*
		 * We cannot continue on SMP:
		 */
//		panic("bad locking");
#endif
	}
}

#define SPIN_BUG_ON(cond, lock, msg) if (unlikely(cond)) spin_bug(lock, msg)

static inline void debug_spin_lock_before(spinlock_t *lock)
{
	SPIN_BUG_ON(lock->magic != SPINLOCK_MAGIC, lock, "bad magic");
	SPIN_BUG_ON(lock->owner == current, lock, "recursion");
	SPIN_BUG_ON(lock->owner_cpu == raw_smp_processor_id(),
							lock, "cpu recursion");
}

static inline void debug_spin_lock_after(spinlock_t *lock)
{
	lock->owner_cpu = raw_smp_processor_id();
	lock->owner = current;
}

static inline void debug_spin_unlock(spinlock_t *lock)
{
	SPIN_BUG_ON(lock->magic != SPINLOCK_MAGIC, lock, "bad magic");
	SPIN_BUG_ON(!spin_is_locked(lock), lock, "already unlocked");
	SPIN_BUG_ON(lock->owner != current, lock, "wrong owner");
	SPIN_BUG_ON(lock->owner_cpu != raw_smp_processor_id(),
							lock, "wrong CPU");
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
}

static void __spin_lock_debug(spinlock_t *lock)
{
	int print_once = 1;
	u64 i;

	for (;;) {
		for (i = 0; i < loops_per_jiffy * HZ; i++) {
			if (__raw_spin_trylock(&lock->raw_lock))
				return;
			__delay(1);
		}
		/* lockup suspected: */
		if (print_once) {
			print_once = 0;
			printk(KERN_EMERG "BUG: spinlock lockup on CPU#%d, "
					"%s/%d, %p\n",
				raw_smp_processor_id(), current->comm,
				current->pid, lock);
			dump_stack();
		}
	}
}

void _raw_spin_lock(spinlock_t *lock)
{
	debug_spin_lock_before(lock);
	if (unlikely(!__raw_spin_trylock(&lock->raw_lock)))
		__spin_lock_debug(lock);
	debug_spin_lock_after(lock);
}

int _raw_spin_trylock(spinlock_t *lock)
{
	int ret = __raw_spin_trylock(&lock->raw_lock);

	if (ret)
		debug_spin_lock_after(lock);
#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	SPIN_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void _raw_spin_unlock(spinlock_t *lock)
{
	debug_spin_unlock(lock);
	__raw_spin_unlock(&lock->raw_lock);
}

static void rwlock_bug(rwlock_t *lock, const char *msg)
{
	static long print_once = 1;

	if (xchg(&print_once, 0)) {
		printk(KERN_EMERG "BUG: rwlock %s on CPU#%d, %s/%d, %p\n",
			msg, raw_smp_processor_id(), current->comm,
			current->pid, lock);
		dump_stack();
#ifdef CONFIG_SMP
		/*
		 * We cannot continue on SMP:
		 */
		panic("bad locking");
#endif
	}
}

#define RWLOCK_BUG_ON(cond, lock, msg) if (unlikely(cond)) rwlock_bug(lock, msg)

static void __read_lock_debug(rwlock_t *lock)
{
	int print_once = 1;
	u64 i;

	for (;;) {
		for (i = 0; i < loops_per_jiffy * HZ; i++) {
			if (__raw_read_trylock(&lock->raw_lock))
				return;
			__delay(1);
		}
		/* lockup suspected: */
		if (print_once) {
			print_once = 0;
			printk(KERN_EMERG "BUG: read-lock lockup on CPU#%d, "
					"%s/%d, %p\n",
				raw_smp_processor_id(), current->comm,
				current->pid, lock);
			dump_stack();
		}
	}
}

void _raw_read_lock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	if (unlikely(!__raw_read_trylock(&lock->raw_lock)))
		__read_lock_debug(lock);
}

int _raw_read_trylock(rwlock_t *lock)
{
	int ret = __raw_read_trylock(&lock->raw_lock);

#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	RWLOCK_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void _raw_read_unlock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	__raw_read_unlock(&lock->raw_lock);
}

static inline void debug_write_lock_before(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	RWLOCK_BUG_ON(lock->owner == current, lock, "recursion");
	RWLOCK_BUG_ON(lock->owner_cpu == raw_smp_processor_id(),
							lock, "cpu recursion");
}

static inline void debug_write_lock_after(rwlock_t *lock)
{
	lock->owner_cpu = raw_smp_processor_id();
	lock->owner = current;
}

static inline void debug_write_unlock(rwlock_t *lock)
{
	RWLOCK_BUG_ON(lock->magic != RWLOCK_MAGIC, lock, "bad magic");
	RWLOCK_BUG_ON(lock->owner != current, lock, "wrong owner");
	RWLOCK_BUG_ON(lock->owner_cpu != raw_smp_processor_id(),
							lock, "wrong CPU");
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
}

static void __write_lock_debug(rwlock_t *lock)
{
	int print_once = 1;
	u64 i;

	for (;;) {
		for (i = 0; i < loops_per_jiffy * HZ; i++) {
			if (__raw_write_trylock(&lock->raw_lock))
				return;
			__delay(1);
		}
		/* lockup suspected: */
		if (print_once) {
			print_once = 0;
			printk(KERN_EMERG "BUG: write-lock lockup on CPU#%d, "
					"%s/%d, %p\n",
				raw_smp_processor_id(), current->comm,
				current->pid, lock);
			dump_stack();
		}
	}
}

void _raw_write_lock(rwlock_t *lock)
{
	debug_write_lock_before(lock);
	if (unlikely(!__raw_write_trylock(&lock->raw_lock)))
		__write_lock_debug(lock);
	debug_write_lock_after(lock);
}

int _raw_write_trylock(rwlock_t *lock)
{
	int ret = __raw_write_trylock(&lock->raw_lock);

	if (ret)
		debug_write_lock_after(lock);
#ifndef CONFIG_SMP
	/*
	 * Must not happen on UP:
	 */
	RWLOCK_BUG_ON(!ret, lock, "trylock failure on UP");
#endif
	return ret;
}

void _raw_write_unlock(rwlock_t *lock)
{
	debug_write_unlock(lock);
	__raw_write_unlock(&lock->raw_lock);
}
