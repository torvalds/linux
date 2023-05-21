/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_SIX_H
#define _LINUX_SIX_H

/*
 * Shared/intent/exclusive locks: sleepable read/write locks, much like rw
 * semaphores, except with a third intermediate state, intent. Basic operations
 * are:
 *
 * six_lock_read(&foo->lock);
 * six_unlock_read(&foo->lock);
 *
 * six_lock_intent(&foo->lock);
 * six_unlock_intent(&foo->lock);
 *
 * six_lock_write(&foo->lock);
 * six_unlock_write(&foo->lock);
 *
 * Intent locks block other intent locks, but do not block read locks, and you
 * must have an intent lock held before taking a write lock, like so:
 *
 * six_lock_intent(&foo->lock);
 * six_lock_write(&foo->lock);
 * six_unlock_write(&foo->lock);
 * six_unlock_intent(&foo->lock);
 *
 * Other operations:
 *
 *   six_trylock_read()
 *   six_trylock_intent()
 *   six_trylock_write()
 *
 *   six_lock_downgrade():	convert from intent to read
 *   six_lock_tryupgrade():	attempt to convert from read to intent
 *
 * Locks also embed a sequence number, which is incremented when the lock is
 * locked or unlocked for write. The current sequence number can be grabbed
 * while a lock is held from lock->state.seq; then, if you drop the lock you can
 * use six_relock_(read|intent_write)(lock, seq) to attempt to retake the lock
 * iff it hasn't been locked for write in the meantime.
 *
 * There are also operations that take the lock type as a parameter, where the
 * type is one of SIX_LOCK_read, SIX_LOCK_intent, or SIX_LOCK_write:
 *
 *   six_lock_type(lock, type)
 *   six_unlock_type(lock, type)
 *   six_relock(lock, type, seq)
 *   six_trylock_type(lock, type)
 *   six_trylock_convert(lock, from, to)
 *
 * A lock may be held multiple times by the same thread (for read or intent,
 * not write). However, the six locks code does _not_ implement the actual
 * recursive checks itself though - rather, if your code (e.g. btree iterator
 * code) knows that the current thread already has a lock held, and for the
 * correct type, six_lock_increment() may be used to bump up the counter for
 * that type - the only effect is that one more call to unlock will be required
 * before the lock is unlocked.
 */

#include <linux/lockdep.h>
#include <linux/sched.h>
#include <linux/types.h>

#ifdef CONFIG_SIX_LOCK_SPIN_ON_OWNER
#include <linux/osq_lock.h>
#endif

#define SIX_LOCK_SEPARATE_LOCKFNS

union six_lock_state {
	struct {
		atomic64_t	counter;
	};

	struct {
		u64		v;
	};

	struct {
		/* for waitlist_bitnr() */
		unsigned long	l;
	};

	struct {
		unsigned	read_lock:26;
		unsigned	write_locking:1;
		unsigned	intent_lock:1;
		unsigned	nospin:1;
		unsigned	waiters:3;
		/*
		 * seq works much like in seqlocks: it's incremented every time
		 * we lock and unlock for write.
		 *
		 * If it's odd write lock is held, even unlocked.
		 *
		 * Thus readers can unlock, and then lock again later iff it
		 * hasn't been modified in the meantime.
		 */
		u32		seq;
	};
};

enum six_lock_type {
	SIX_LOCK_read,
	SIX_LOCK_intent,
	SIX_LOCK_write,
};

struct six_lock {
	union six_lock_state	state;
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

#define six_lock_init(lock, flags)					\
do {									\
	static struct lock_class_key __key;				\
									\
	__six_lock_init((lock), #lock, &__key, flags);			\
} while (0)

bool six_trylock_ip_type(struct six_lock *lock, enum six_lock_type type,
			 unsigned long ip);

static inline bool six_trylock_type(struct six_lock *lock, enum six_lock_type type)
{
	return six_trylock_ip_type(lock, type, _THIS_IP_);
}

int six_lock_type_ip_waiter(struct six_lock *lock, enum six_lock_type type,
			    struct six_lock_waiter *wait,
			    six_lock_should_sleep_fn should_sleep_fn, void *p,
			    unsigned long ip);

static inline int six_lock_type_waiter(struct six_lock *lock, enum six_lock_type type,
				struct six_lock_waiter *wait,
				six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	return six_lock_type_ip_waiter(lock, type, wait, should_sleep_fn, p, _THIS_IP_);
}

static inline int six_lock_ip_type(struct six_lock *lock, enum six_lock_type type,
				six_lock_should_sleep_fn should_sleep_fn, void *p,
				unsigned long ip)
{
	struct six_lock_waiter wait;

	return six_lock_type_ip_waiter(lock, type, &wait, should_sleep_fn, p, ip);
}

static inline int six_lock_type(struct six_lock *lock, enum six_lock_type type,
				six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	struct six_lock_waiter wait;

	return six_lock_type_ip_waiter(lock, type, &wait, should_sleep_fn, p, _THIS_IP_);
}

bool six_relock_ip_type(struct six_lock *lock, enum six_lock_type type,
			unsigned seq, unsigned long ip);

static inline bool six_relock_type(struct six_lock *lock, enum six_lock_type type,
				   unsigned seq)
{
	return six_relock_ip_type(lock, type, seq, _THIS_IP_);
}

void six_unlock_ip_type(struct six_lock *lock, enum six_lock_type type, unsigned long ip);

static inline void six_unlock_type(struct six_lock *lock, enum six_lock_type type)
{
	six_unlock_ip_type(lock, type, _THIS_IP_);
}

#define __SIX_LOCK(type)						\
static inline bool six_trylock_ip_##type(struct six_lock *lock, unsigned long ip)\
{									\
	return six_trylock_ip_type(lock, SIX_LOCK_##type, ip);		\
}									\
									\
static inline bool six_trylock_##type(struct six_lock *lock)		\
{									\
	return six_trylock_ip_type(lock, SIX_LOCK_##type, _THIS_IP_);	\
}									\
									\
static inline int six_lock_ip_waiter_##type(struct six_lock *lock,	\
			   struct six_lock_waiter *wait,		\
			   six_lock_should_sleep_fn should_sleep_fn, void *p,\
			   unsigned long ip)				\
{									\
	return six_lock_type_ip_waiter(lock, SIX_LOCK_##type, wait, should_sleep_fn, p, ip);\
}									\
									\
static inline int six_lock_ip_##type(struct six_lock *lock,		\
		    six_lock_should_sleep_fn should_sleep_fn, void *p,	\
		    unsigned long ip)					\
{									\
	return six_lock_ip_type(lock, SIX_LOCK_##type, should_sleep_fn, p, ip);\
}									\
									\
static inline bool six_relock_ip_##type(struct six_lock *lock, u32 seq, unsigned long ip)\
{									\
	return six_relock_ip_type(lock, SIX_LOCK_##type, seq, ip);	\
}									\
									\
static inline bool six_relock_##type(struct six_lock *lock, u32 seq)	\
{									\
	return six_relock_ip_type(lock, SIX_LOCK_##type, seq, _THIS_IP_);\
}									\
									\
static inline int six_lock_##type(struct six_lock *lock,		\
				  six_lock_should_sleep_fn fn, void *p)\
{									\
	return six_lock_ip_##type(lock, fn, p, _THIS_IP_);		\
}									\
									\
static inline int six_lock_waiter_##type(struct six_lock *lock,		\
			struct six_lock_waiter *wait,			\
			six_lock_should_sleep_fn fn, void *p)		\
{									\
	return six_lock_ip_waiter_##type(lock, wait, fn, p, _THIS_IP_);	\
}									\
									\
static inline void six_unlock_ip_##type(struct six_lock *lock, unsigned long ip)	\
{									\
	six_unlock_ip_type(lock, SIX_LOCK_##type, ip);			\
}									\
									\
static inline void six_unlock_##type(struct six_lock *lock)		\
{									\
	six_unlock_ip_type(lock, SIX_LOCK_##type, _THIS_IP_);		\
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
