/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_SIX_H
#define _LINUX_SIX_H

/**
 * DOC: SIX locks overview
 *
 * Shared/intent/exclusive locks: sleepable read/write locks, like rw semaphores
 * but with an additional state: read/shared, intent, exclusive/write
 *
 * The purpose of the intent state is to allow for greater concurrency on tree
 * structures without deadlocking. In general, a read can't be upgraded to a
 * write lock without deadlocking, so an operation that updates multiple nodes
 * will have to take write locks for the full duration of the operation.
 *
 * But by adding an intent state, which is exclusive with other intent locks but
 * not with readers, we can take intent locks at thte start of the operation,
 * and then take write locks only for the actual update to each individual
 * nodes, without deadlocking.
 *
 * Example usage:
 *   six_lock_read(&foo->lock);
 *   six_unlock_read(&foo->lock);
 *
 * An intent lock must be held before taking a write lock:
 *   six_lock_intent(&foo->lock);
 *   six_lock_write(&foo->lock);
 *   six_unlock_write(&foo->lock);
 *   six_unlock_intent(&foo->lock);
 *
 * Other operations:
 *   six_trylock_read()
 *   six_trylock_intent()
 *   six_trylock_write()
 *
 *   six_lock_downgrade()	convert from intent to read
 *   six_lock_tryupgrade()	attempt to convert from read to intent, may fail
 *
 * There are also interfaces that take the lock type as an enum:
 *
 *   six_lock_type(&foo->lock, SIX_LOCK_read);
 *   six_trylock_convert(&foo->lock, SIX_LOCK_read, SIX_LOCK_intent)
 *   six_lock_type(&foo->lock, SIX_LOCK_write);
 *   six_unlock_type(&foo->lock, SIX_LOCK_write);
 *   six_unlock_type(&foo->lock, SIX_LOCK_intent);
 *
 * Lock sequence numbers - unlock(), relock():
 *
 *   Locks embed sequences numbers, which are incremented on write lock/unlock.
 *   This allows locks to be dropped and the retaken iff the state they protect
 *   hasn't changed; this makes it much easier to avoid holding locks while e.g.
 *   doing IO or allocating memory.
 *
 *   Example usage:
 *     six_lock_read(&foo->lock);
 *     u32 seq = six_lock_seq(&foo->lock);
 *     six_unlock_read(&foo->lock);
 *
 *     some_operation_that_may_block();
 *
 *     if (six_relock_read(&foo->lock, seq)) { ... }
 *
 *   If the relock operation succeeds, it is as if the lock was never unlocked.
 *
 * Reentrancy:
 *
 *   Six locks are not by themselves reentrent, but have counters for both the
 *   read and intent states that can be used to provide reentrency by an upper
 *   layer that tracks held locks. If a lock is known to already be held in the
 *   read or intent state, six_lock_increment() can be used to bump the "lock
 *   held in this state" counter, increasing the number of unlock calls that
 *   will be required to fully unlock it.
 *
 *   Example usage:
 *     six_lock_read(&foo->lock);
 *     six_lock_increment(&foo->lock, SIX_LOCK_read);
 *     six_unlock_read(&foo->lock);
 *     six_unlock_read(&foo->lock);
 *   foo->lock is now fully unlocked.
 *
 *   Since the intent state supercedes read, it's legal to increment the read
 *   counter when holding an intent lock, but not the reverse.
 *
 *   A lock may only be held once for write: six_lock_increment(.., SIX_LOCK_write)
 *   is not legal.
 *
 * should_sleep_fn:
 *
 *   There is a six_lock() variant that takes a function pointer that is called
 *   immediately prior to schedule() when blocking, and may return an error to
 *   abort.
 *
 *   One possible use for this feature is when objects being locked are part of
 *   a cache and may reused, and lock ordering is based on a property of the
 *   object that will change when the object is reused - i.e. logical key order.
 *
 *   If looking up an object in the cache may race with object reuse, and lock
 *   ordering is required to prevent deadlock, object reuse may change the
 *   correct lock order for that object and cause a deadlock. should_sleep_fn
 *   can be used to check if the object is still the object we want and avoid
 *   this deadlock.
 *
 * Wait list entry interface:
 *
 *   There is a six_lock() variant, six_lock_waiter(), that takes a pointer to a
 *   wait list entry. By embedding six_lock_waiter into another object, and by
 *   traversing lock waitlists, it is then possible for an upper layer to
 *   implement full cycle detection for deadlock avoidance.
 *
 *   should_sleep_fn should be used for invoking the cycle detector, walking the
 *   graph of held locks to check for a deadlock. The upper layer must track
 *   held locks for each thread, and each thread's held locks must be reachable
 *   from its six_lock_waiter object.
 *
 *   six_lock_waiter() will add the wait object to the waitlist re-trying taking
 *   the lock, and before calling should_sleep_fn, and the wait object will not
 *   be removed from the waitlist until either the lock has been successfully
 *   acquired, or we aborted because should_sleep_fn returned an error.
 *
 *   Also, six_lock_waiter contains a timestamp, and waiters on a waitlist will
 *   have timestamps in strictly ascending order - this is so the timestamp can
 *   be used as a cursor for lock graph traverse.
 */

#include <linux/lockdep.h>
#include <linux/sched.h>
#include <linux/types.h>

#ifdef CONFIG_SIX_LOCK_SPIN_ON_OWNER
#include <linux/osq_lock.h>
#endif

enum six_lock_type {
	SIX_LOCK_read,
	SIX_LOCK_intent,
	SIX_LOCK_write,
};

struct six_lock {
	atomic64_t		state;
	unsigned		intent_lock_recurse;
	struct task_struct	*owner;
	unsigned __percpu	*readers;
#ifdef CONFIG_SIX_LOCK_SPIN_ON_OWNER
	struct optimistic_spin_queue osq;
#endif
	raw_spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

struct six_lock_waiter {
	struct list_head	list;
	struct task_struct	*task;
	enum six_lock_type	lock_want;
	bool			lock_acquired;
	u64			start_time;
};

typedef int (*six_lock_should_sleep_fn)(struct six_lock *lock, void *);

void six_lock_exit(struct six_lock *lock);

enum six_lock_init_flags {
	SIX_LOCK_INIT_PCPU	= 1U << 0,
};

void __six_lock_init(struct six_lock *lock, const char *name,
		     struct lock_class_key *key, enum six_lock_init_flags flags);

/**
 * six_lock_init - initialize a six lock
 * @lock:	lock to initialize
 * @flags:	optional flags, i.e. SIX_LOCK_INIT_PCPU
 */
#define six_lock_init(lock, flags)					\
do {									\
	static struct lock_class_key __key;				\
									\
	__six_lock_init((lock), #lock, &__key, flags);			\
} while (0)

/**
 * six_lock_seq - obtain current lock sequence number
 * @lock:	six_lock to obtain sequence number for
 *
 * @lock should be held for read or intent, and not write
 *
 * By saving the lock sequence number, we can unlock @lock and then (typically
 * after some blocking operation) attempt to relock it: the relock will succeed
 * if the sequence number hasn't changed, meaning no write locks have been taken
 * and state corresponding to what @lock protects is still valid.
 */
static inline u32 six_lock_seq(const struct six_lock *lock)
{
	return atomic64_read(&lock->state) >> 32;
}

bool six_trylock_ip(struct six_lock *lock, enum six_lock_type type, unsigned long ip);

/**
 * six_trylock_type - attempt to take a six lock without blocking
 * @lock:	lock to take
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 *
 * Return: true on success, false on failure.
 */
static inline bool six_trylock_type(struct six_lock *lock, enum six_lock_type type)
{
	return six_trylock_ip(lock, type, _THIS_IP_);
}

int six_lock_ip_waiter(struct six_lock *lock, enum six_lock_type type,
		       struct six_lock_waiter *wait,
		       six_lock_should_sleep_fn should_sleep_fn, void *p,
		       unsigned long ip);

/**
 * six_lock_waiter - take a lock, with full waitlist interface
 * @lock:	lock to take
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 * @wait:	pointer to wait object, which will be added to lock's waitlist
 * @should_sleep_fn: callback run after adding to waitlist, immediately prior
 *		to scheduling
 * @p:		passed through to @should_sleep_fn
 *
 * This is a convenience wrapper around six_lock_ip_waiter(), see that function
 * for full documentation.
 *
 * Return: 0 on success, or the return code from @should_sleep_fn on failure.
 */
static inline int six_lock_waiter(struct six_lock *lock, enum six_lock_type type,
				  struct six_lock_waiter *wait,
				  six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	return six_lock_ip_waiter(lock, type, wait, should_sleep_fn, p, _THIS_IP_);
}

/**
 * six_lock_ip - take a six lock lock
 * @lock:	lock to take
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 * @should_sleep_fn: callback run after adding to waitlist, immediately prior
 *		to scheduling
 * @p:		passed through to @should_sleep_fn
 * @ip:		ip parameter for lockdep/lockstat, i.e. _THIS_IP_
 *
 * Return: 0 on success, or the return code from @should_sleep_fn on failure.
 */
static inline int six_lock_ip(struct six_lock *lock, enum six_lock_type type,
			      six_lock_should_sleep_fn should_sleep_fn, void *p,
			      unsigned long ip)
{
	struct six_lock_waiter wait;

	return six_lock_ip_waiter(lock, type, &wait, should_sleep_fn, p, ip);
}

/**
 * six_lock_type - take a six lock lock
 * @lock:	lock to take
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 * @should_sleep_fn: callback run after adding to waitlist, immediately prior
 *		to scheduling
 * @p:		passed through to @should_sleep_fn
 *
 * Return: 0 on success, or the return code from @should_sleep_fn on failure.
 */
static inline int six_lock_type(struct six_lock *lock, enum six_lock_type type,
				six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	struct six_lock_waiter wait;

	return six_lock_ip_waiter(lock, type, &wait, should_sleep_fn, p, _THIS_IP_);
}

bool six_relock_ip(struct six_lock *lock, enum six_lock_type type,
		   unsigned seq, unsigned long ip);

/**
 * six_relock_type - attempt to re-take a lock that was held previously
 * @lock:	lock to take
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 * @seq:	lock sequence number obtained from six_lock_seq() while lock was
 *		held previously
 *
 * Return: true on success, false on failure.
 */
static inline bool six_relock_type(struct six_lock *lock, enum six_lock_type type,
				   unsigned seq)
{
	return six_relock_ip(lock, type, seq, _THIS_IP_);
}

void six_unlock_ip(struct six_lock *lock, enum six_lock_type type, unsigned long ip);

/**
 * six_unlock_type - drop a six lock
 * @lock:	lock to unlock
 * @type:	SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write
 *
 * When a lock is held multiple times (because six_lock_incement()) was used),
 * this decrements the 'lock held' counter by one.
 *
 * For example:
 * six_lock_read(&foo->lock);				read count 1
 * six_lock_increment(&foo->lock, SIX_LOCK_read);	read count 2
 * six_lock_unlock(&foo->lock, SIX_LOCK_read);		read count 1
 * six_lock_unlock(&foo->lock, SIX_LOCK_read);		read count 0
 */
static inline void six_unlock_type(struct six_lock *lock, enum six_lock_type type)
{
	six_unlock_ip(lock, type, _THIS_IP_);
}

#define __SIX_LOCK(type)						\
static inline bool six_trylock_ip_##type(struct six_lock *lock, unsigned long ip)\
{									\
	return six_trylock_ip(lock, SIX_LOCK_##type, ip);		\
}									\
									\
static inline bool six_trylock_##type(struct six_lock *lock)		\
{									\
	return six_trylock_ip(lock, SIX_LOCK_##type, _THIS_IP_);	\
}									\
									\
static inline int six_lock_ip_waiter_##type(struct six_lock *lock,	\
			   struct six_lock_waiter *wait,		\
			   six_lock_should_sleep_fn should_sleep_fn, void *p,\
			   unsigned long ip)				\
{									\
	return six_lock_ip_waiter(lock, SIX_LOCK_##type, wait, should_sleep_fn, p, ip);\
}									\
									\
static inline int six_lock_ip_##type(struct six_lock *lock,		\
		    six_lock_should_sleep_fn should_sleep_fn, void *p,	\
		    unsigned long ip)					\
{									\
	return six_lock_ip(lock, SIX_LOCK_##type, should_sleep_fn, p, ip);\
}									\
									\
static inline bool six_relock_ip_##type(struct six_lock *lock, u32 seq, unsigned long ip)\
{									\
	return six_relock_ip(lock, SIX_LOCK_##type, seq, ip);		\
}									\
									\
static inline bool six_relock_##type(struct six_lock *lock, u32 seq)	\
{									\
	return six_relock_ip(lock, SIX_LOCK_##type, seq, _THIS_IP_);	\
}									\
									\
static inline int six_lock_##type(struct six_lock *lock,		\
				  six_lock_should_sleep_fn fn, void *p)\
{									\
	return six_lock_ip_##type(lock, fn, p, _THIS_IP_);		\
}									\
									\
static inline void six_unlock_ip_##type(struct six_lock *lock, unsigned long ip)	\
{									\
	six_unlock_ip(lock, SIX_LOCK_##type, ip);			\
}									\
									\
static inline void six_unlock_##type(struct six_lock *lock)		\
{									\
	six_unlock_ip(lock, SIX_LOCK_##type, _THIS_IP_);		\
}

__SIX_LOCK(read)
__SIX_LOCK(intent)
__SIX_LOCK(write)
#undef __SIX_LOCK

void six_lock_downgrade(struct six_lock *);
bool six_lock_tryupgrade(struct six_lock *);
bool six_trylock_convert(struct six_lock *, enum six_lock_type,
			 enum six_lock_type);

void six_lock_increment(struct six_lock *, enum six_lock_type);

void six_lock_wakeup_all(struct six_lock *);

struct six_lock_count {
	unsigned n[3];
};

struct six_lock_count six_lock_counts(struct six_lock *);
void six_lock_readers_add(struct six_lock *, int);

#endif /* _LINUX_SIX_H */
