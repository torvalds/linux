/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FUTEX_H
#define _FUTEX_H

#include <linux/futex.h>
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

extern struct futex_hash_bucket *futex_hash(union futex_key *key);

extern struct hrtimer_sleeper *
futex_setup_timer(ktime_t *time, struct hrtimer_sleeper *timeout,
		  int flags, u64 range_ns);

extern int fault_in_user_writeable(u32 __user *uaddr);
extern int futex_cmpxchg_value_locked(u32 *curval, u32 __user *uaddr, u32 uval, u32 newval);
extern int futex_get_value_locked(u32 *dest, u32 __user *from);
extern struct futex_q *futex_top_waiter(struct futex_hash_bucket *hb, union futex_key *key);

extern void __futex_queue(struct futex_q *q, struct futex_hash_bucket *hb);
extern void futex_unqueue_pi(struct futex_q *q);

extern void wait_for_owner_exiting(int ret, struct task_struct *exiting);

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

/* syscalls */

extern int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags, u32
				 val, ktime_t *abs_time, u32 bitset, u32 __user
				 *uaddr2);

extern int futex_requeue(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_requeue,
			 u32 *cmpval, int requeue_pi);

extern int futex_wait(u32 __user *uaddr, unsigned int flags, u32 val,
		      ktime_t *abs_time, u32 bitset);

extern int futex_wake(u32 __user *uaddr, unsigned int flags, int nr_wake, u32 bitset);

extern int futex_wake_op(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_wake2, int op);

extern int futex_unlock_pi(u32 __user *uaddr, unsigned int flags);

extern int futex_lock_pi(u32 __user *uaddr, unsigned int flags, ktime_t *time, int trylock);

#endif /* _FUTEX_H */
