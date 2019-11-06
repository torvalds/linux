/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#include <linux/sched.h>
#include <linux/ktime.h>

#include <uapi/linux/futex.h>

struct inode;
struct mm_struct;
struct task_struct;

/*
 * Futexes are matched on equal values of this key.
 * The key type depends on whether it's a shared or private mapping.
 * Don't rearrange members without looking at hash_futex().
 *
 * offset is aligned to a multiple of sizeof(u32) (== 4) by definition.
 * We use the two low order bits of offset to tell what is the kind of key :
 *  00 : Private process futex (PTHREAD_PROCESS_PRIVATE)
 *       (no reference on an inode or mm)
 *  01 : Shared futex (PTHREAD_PROCESS_SHARED)
 *	mapped on a file (reference on the underlying inode)
 *  10 : Shared futex (PTHREAD_PROCESS_SHARED)
 *       (but private mapping on an mm, and reference taken on it)
*/

#define FUT_OFF_INODE    1 /* We set bit 0 if key has a reference on inode */
#define FUT_OFF_MMSHARED 2 /* We set bit 1 if key has a reference on mm */

union futex_key {
	struct {
		unsigned long pgoff;
		struct inode *inode;
		int offset;
	} shared;
	struct {
		unsigned long address;
		struct mm_struct *mm;
		int offset;
	} private;
	struct {
		unsigned long word;
		void *ptr;
		int offset;
	} both;
};

#define FUTEX_KEY_INIT (union futex_key) { .both = { .ptr = NULL } }

#ifdef CONFIG_FUTEX
enum {
	FUTEX_STATE_OK,
	FUTEX_STATE_DEAD,
};

static inline void futex_init_task(struct task_struct *tsk)
{
	tsk->robust_list = NULL;
#ifdef CONFIG_COMPAT
	tsk->compat_robust_list = NULL;
#endif
	INIT_LIST_HEAD(&tsk->pi_state_list);
	tsk->pi_state_cache = NULL;
	tsk->futex_state = FUTEX_STATE_OK;
}

/**
 * futex_exit_done - Sets the tasks futex state to FUTEX_STATE_DEAD
 * @tsk:	task to set the state on
 *
 * Set the futex exit state of the task lockless. The futex waiter code
 * observes that state when a task is exiting and loops until the task has
 * actually finished the futex cleanup. The worst case for this is that the
 * waiter runs through the wait loop until the state becomes visible.
 *
 * This has two callers:
 *
 * - futex_mm_release() after the futex exit cleanup has been done
 *
 * - do_exit() from the recursive fault handling path.
 *
 * In case of a recursive fault this is best effort. Either the futex exit
 * code has run already or not. If the OWNER_DIED bit has been set on the
 * futex then the waiter can take it over. If not, the problem is pushed
 * back to user space. If the futex exit code did not run yet, then an
 * already queued waiter might block forever, but there is nothing which
 * can be done about that.
 */
static inline void futex_exit_done(struct task_struct *tsk)
{
	tsk->futex_state = FUTEX_STATE_DEAD;
}

void futex_mm_release(struct task_struct *tsk);

long do_futex(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
	      u32 __user *uaddr2, u32 val2, u32 val3);
#else
static inline void futex_init_task(struct task_struct *tsk) { }
static inline void futex_mm_release(struct task_struct *tsk) { }
static inline void futex_exit_done(struct task_struct *tsk) { }
static inline long do_futex(u32 __user *uaddr, int op, u32 val,
			    ktime_t *timeout, u32 __user *uaddr2,
			    u32 val2, u32 val3)
{
	return -EINVAL;
}
#endif

#endif
