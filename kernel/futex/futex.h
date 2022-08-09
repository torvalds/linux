/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FUTEX_H
#define _FUTEX_H

#include <linux/futex.h>
#include <linux/sched/wake_q.h>

#ifdef CONFIG_PREEMPT_RT
#include <linux/rcuwait.h>
#endif

#include <asm/futex.h>

/*
 * Futex flags used to encode options to functions and preserve them across
 * restarts.
 */
#ifdef CONFIG_MMU
# define FLAGS_SHARED		0x01
#else
/*
 * NOMMU does not have per process address space. Let the compiler optimize
 * code away.
 */
# define FLAGS_SHARED		0x00
#endif
#define FLAGS_CLOCKRT		0x02
#define FLAGS_HAS_TIMEOUT	0x04

#ifdef CONFIG_HAVE_FUTEX_CMPXCHG
#define futex_cmpxchg_enabled 1
#else
extern int  __read_mostly futex_cmpxchg_enabled;
#endif

#ifdef CONFIG_FAIL_FUTEX
extern bool should_fail_futex(bool fshared);
#else
static inline bool should_fail_futex(bool fshared)
{
	return false;
}
#endif

/*
 * Hash buckets are shared by all the futex_keys that hash to the same
 * location.  Each key may have multiple futex_q structures, one for each task
 * waiting on a futex.
 */
struct futex_hash_bucket {
	atomic_t waiters;
	spinlock_t lock;
	struct plist_head chain;
} ____cacheline_aligned_in_smp;

/*
 * Priority Inheritance state:
 */
struct futex_pi_state {
	/*
	 * list of 'owned' pi_state instances - these have to be
	 * cleaned up in do_exit() if the task exits prematurely:
	 */
	struct list_head list;

	/*
	 * The PI object:
	 */
	struct rt_mutex_base pi_mutex;

	struct task_struct *owner;
	refcount_t refcount;

	union futex_key key;
} __randomize_layout;

/**
 * struct futex_q - The hashed futex queue entry, one per waiting task
 * @list:		priority-sorted list of tasks waiting on this futex
 * @task:		the task waiting on the futex
 * @lock_ptr:		the hash bucket lock
 * @key:		the key the futex is hashed on
 * @pi_state:		optional priority inheritance state
 * @rt_waiter:		rt_waiter storage for use with requeue_pi
 * @requeue_pi_key:	the requeue_pi target futex key
 * @bitset:		bitset for the optional bitmasked wakeup
 * @requeue_state:	State field for futex_requeue_pi()
 * @requeue_wait:	RCU wait for futex_requeue_pi() (RT only)
 *
 * We use this hashed waitqueue, instead of a normal wait_queue_entry_t, so
 * we can wake only the relevant ones (hashed queues may be shared).
 *
 * A futex_q has a woken state, just like tasks have TASK_RUNNING.
 * It is considered woken when plist_node_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakeup is always to make the first condition true, then
 * the second.
 *
 * PI futexes are typically woken before they are removed from the hash list via
 * the rt_mutex code. See futex_unqueue_pi().
 */
struct futex_q {
	struct plist_node list;

	struct task_struct *task;
	spinlock_t *lock_ptr;
	union futex_key key;
	struct futex_pi_state *pi_state;
	struct rt_mutex_waiter *rt_waiter;
	union futex_key *requeue_pi_key;
	u32 bitset;
	atomic_t requeue_state;
#ifdef CONFIG_PREEMPT_RT
	struct rcuwait requeue_wait;
#endif
} __randomize_layout;

extern const struct futex_q futex_q_init;

enum futex_access {
	FUTEX_READ,
	FUTEX_WRITE
};

extern int get_futex_key(u32 __user *uaddr, bool fshared, union futex_key *key,
			 enum futex_access rw);

extern struct hrtimer_sleeper *
futex_setup_timer(ktime_t *time, struct hrtimer_sleeper *timeout,
		  int flags, u64 range_ns);

extern struct futex_hash_bucket *futex_hash(union futex_key *key);

/**
 * futex_match - Check whether two futex keys are equal
 * @key1:	Pointer to key1
 * @key2:	Pointer to key2
 *
 * Return 1 if two futex_keys are equal, 0 otherwise.
 */
static inline int futex_match(union futex_key *key1, union futex_key *key2)
{
	return (key1 && key2
		&& key1->both.word == key2->both.word
		&& key1->both.ptr == key2->both.ptr
		&& key1->both.offset == key2->both.offset);
}

extern int futex_wait_setup(u32 __user *uaddr, u32 val, unsigned int flags,
			    struct futex_q *q, struct futex_hash_bucket **hb);
extern void futex_wait_queue(struct futex_hash_bucket *hb, struct futex_q *q,
				   struct hrtimer_sleeper *timeout);
extern void futex_wake_mark(struct wake_q_head *wake_q, struct futex_q *q);

extern int fault_in_user_writeable(u32 __user *uaddr);
extern int futex_cmpxchg_value_locked(u32 *curval, u32 __user *uaddr, u32 uval, u32 newval);
extern int futex_get_value_locked(u32 *dest, u32 __user *from);
extern struct futex_q *futex_top_waiter(struct futex_hash_bucket *hb, union futex_key *key);

extern void __futex_unqueue(struct futex_q *q);
extern void __futex_queue(struct futex_q *q, struct futex_hash_bucket *hb);
extern int futex_unqueue(struct futex_q *q);

/**
 * futex_queue() - Enqueue the futex_q on the futex_hash_bucket
 * @q:	The futex_q to enqueue
 * @hb:	The destination hash bucket
 *
 * The hb->lock must be held by the caller, and is released here. A call to
 * futex_queue() is typically paired with exactly one call to futex_unqueue().  The
 * exceptions involve the PI related operations, which may use futex_unqueue_pi()
 * or nothing if the unqueue is done as part of the wake process and the unqueue
 * state is implicit in the state of woken task (see futex_wait_requeue_pi() for
 * an example).
 */
static inline void futex_queue(struct futex_q *q, struct futex_hash_bucket *hb)
	__releases(&hb->lock)
{
	__futex_queue(q, hb);
	spin_unlock(&hb->lock);
}

extern void futex_unqueue_pi(struct futex_q *q);

extern void wait_for_owner_exiting(int ret, struct task_struct *exiting);

/*
 * Reflects a new waiter being added to the waitqueue.
 */
static inline void futex_hb_waiters_inc(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	atomic_inc(&hb->waiters);
	/*
	 * Full barrier (A), see the ordering comment above.
	 */
	smp_mb__after_atomic();
#endif
}

/*
 * Reflects a waiter being removed from the waitqueue by wakeup
 * paths.
 */
static inline void futex_hb_waiters_dec(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	atomic_dec(&hb->waiters);
#endif
}

static inline int futex_hb_waiters_pending(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	/*
	 * Full barrier (B), see the ordering comment above.
	 */
	smp_mb();
	return atomic_read(&hb->waiters);
#else
	return 1;
#endif
}

extern struct futex_hash_bucket *futex_q_lock(struct futex_q *q);
extern void futex_q_unlock(struct futex_hash_bucket *hb);


extern int futex_lock_pi_atomic(u32 __user *uaddr, struct futex_hash_bucket *hb,
				union futex_key *key,
				struct futex_pi_state **ps,
				struct task_struct *task,
				struct task_struct **exiting,
				int set_waiters);

extern int refill_pi_state_cache(void);
extern void get_pi_state(struct futex_pi_state *pi_state);
extern void put_pi_state(struct futex_pi_state *pi_state);
extern int fixup_pi_owner(u32 __user *uaddr, struct futex_q *q, int locked);

/*
 * Express the locking dependencies for lockdep:
 */
static inline void
double_lock_hb(struct futex_hash_bucket *hb1, struct futex_hash_bucket *hb2)
{
	if (hb1 > hb2)
		swap(hb1, hb2);

	spin_lock(&hb1->lock);
	if (hb1 != hb2)
		spin_lock_nested(&hb2->lock, SINGLE_DEPTH_NESTING);
}

static inline void
double_unlock_hb(struct futex_hash_bucket *hb1, struct futex_hash_bucket *hb2)
{
	spin_unlock(&hb1->lock);
	if (hb1 != hb2)
		spin_unlock(&hb2->lock);
}

/* syscalls */

extern int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags, u32
				 val, ktime_t *abs_time, u32 bitset, u32 __user
				 *uaddr2);

extern int futex_requeue(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_requeue,
			 u32 *cmpval, int requeue_pi);

extern int futex_wait(u32 __user *uaddr, unsigned int flags, u32 val,
		      ktime_t *abs_time, u32 bitset);

/**
 * struct futex_vector - Auxiliary struct for futex_waitv()
 * @w: Userspace provided data
 * @q: Kernel side data
 *
 * Struct used to build an array with all data need for futex_waitv()
 */
struct futex_vector {
	struct futex_waitv w;
	struct futex_q q;
};

extern int futex_wait_multiple(struct futex_vector *vs, unsigned int count,
			       struct hrtimer_sleeper *to);

extern int futex_wake(u32 __user *uaddr, unsigned int flags, int nr_wake, u32 bitset);

extern int futex_wake_op(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_wake2, int op);

extern int futex_unlock_pi(u32 __user *uaddr, unsigned int flags);

extern int futex_lock_pi(u32 __user *uaddr, unsigned int flags, ktime_t *time, int trylock);

#endif /* _FUTEX_H */
