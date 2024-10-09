// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/slab.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>

#include "futex.h"
#include "../locking/rtmutex_common.h"

/*
 * PI code:
 */
int refill_pi_state_cache(void)
{
	struct futex_pi_state *pi_state;

	if (likely(current->pi_state_cache))
		return 0;

	pi_state = kzalloc(sizeof(*pi_state), GFP_KERNEL);

	if (!pi_state)
		return -ENOMEM;

	INIT_LIST_HEAD(&pi_state->list);
	/* pi_mutex gets initialized later */
	pi_state->owner = NULL;
	refcount_set(&pi_state->refcount, 1);
	pi_state->key = FUTEX_KEY_INIT;

	current->pi_state_cache = pi_state;

	return 0;
}

static struct futex_pi_state *alloc_pi_state(void)
{
	struct futex_pi_state *pi_state = current->pi_state_cache;

	WARN_ON(!pi_state);
	current->pi_state_cache = NULL;

	return pi_state;
}

static void pi_state_update_owner(struct futex_pi_state *pi_state,
				  struct task_struct *new_owner)
{
	struct task_struct *old_owner = pi_state->owner;

	lockdep_assert_held(&pi_state->pi_mutex.wait_lock);

	if (old_owner) {
		raw_spin_lock(&old_owner->pi_lock);
		WARN_ON(list_empty(&pi_state->list));
		list_del_init(&pi_state->list);
		raw_spin_unlock(&old_owner->pi_lock);
	}

	if (new_owner) {
		raw_spin_lock(&new_owner->pi_lock);
		WARN_ON(!list_empty(&pi_state->list));
		list_add(&pi_state->list, &new_owner->pi_state_list);
		pi_state->owner = new_owner;
		raw_spin_unlock(&new_owner->pi_lock);
	}
}

void get_pi_state(struct futex_pi_state *pi_state)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&pi_state->refcount));
}

/*
 * Drops a reference to the pi_state object and frees or caches it
 * when the last reference is gone.
 */
void put_pi_state(struct futex_pi_state *pi_state)
{
	if (!pi_state)
		return;

	if (!refcount_dec_and_test(&pi_state->refcount))
		return;

	/*
	 * If pi_state->owner is NULL, the owner is most probably dying
	 * and has cleaned up the pi_state already
	 */
	if (pi_state->owner) {
		unsigned long flags;

		raw_spin_lock_irqsave(&pi_state->pi_mutex.wait_lock, flags);
		pi_state_update_owner(pi_state, NULL);
		rt_mutex_proxy_unlock(&pi_state->pi_mutex);
		raw_spin_unlock_irqrestore(&pi_state->pi_mutex.wait_lock, flags);
	}

	if (current->pi_state_cache) {
		kfree(pi_state);
	} else {
		/*
		 * pi_state->list is already empty.
		 * clear pi_state->owner.
		 * refcount is at 0 - put it back to 1.
		 */
		pi_state->owner = NULL;
		refcount_set(&pi_state->refcount, 1);
		current->pi_state_cache = pi_state;
	}
}

/*
 * We need to check the following states:
 *
 *      Waiter | pi_state | pi->owner | uTID      | uODIED | ?
 *
 * [1]  NULL   | ---      | ---       | 0         | 0/1    | Valid
 * [2]  NULL   | ---      | ---       | >0        | 0/1    | Valid
 *
 * [3]  Found  | NULL     | --        | Any       | 0/1    | Invalid
 *
 * [4]  Found  | Found    | NULL      | 0         | 1      | Valid
 * [5]  Found  | Found    | NULL      | >0        | 1      | Invalid
 *
 * [6]  Found  | Found    | task      | 0         | 1      | Valid
 *
 * [7]  Found  | Found    | NULL      | Any       | 0      | Invalid
 *
 * [8]  Found  | Found    | task      | ==taskTID | 0/1    | Valid
 * [9]  Found  | Found    | task      | 0         | 0      | Invalid
 * [10] Found  | Found    | task      | !=taskTID | 0/1    | Invalid
 *
 * [1]	Indicates that the kernel can acquire the futex atomically. We
 *	came here due to a stale FUTEX_WAITERS/FUTEX_OWNER_DIED bit.
 *
 * [2]	Valid, if TID does not belong to a kernel thread. If no matching
 *      thread is found then it indicates that the owner TID has died.
 *
 * [3]	Invalid. The waiter is queued on a non PI futex
 *
 * [4]	Valid state after exit_robust_list(), which sets the user space
 *	value to FUTEX_WAITERS | FUTEX_OWNER_DIED.
 *
 * [5]	The user space value got manipulated between exit_robust_list()
 *	and exit_pi_state_list()
 *
 * [6]	Valid state after exit_pi_state_list() which sets the new owner in
 *	the pi_state but cannot access the user space value.
 *
 * [7]	pi_state->owner can only be NULL when the OWNER_DIED bit is set.
 *
 * [8]	Owner and user space value match
 *
 * [9]	There is no transient state which sets the user space TID to 0
 *	except exit_robust_list(), but this is indicated by the
 *	FUTEX_OWNER_DIED bit. See [4]
 *
 * [10] There is no transient state which leaves owner and user space
 *	TID out of sync. Except one error case where the kernel is denied
 *	write access to the user address, see fixup_pi_state_owner().
 *
 *
 * Serialization and lifetime rules:
 *
 * hb->lock:
 *
 *	hb -> futex_q, relation
 *	futex_q -> pi_state, relation
 *
 *	(cannot be raw because hb can contain arbitrary amount
 *	 of futex_q's)
 *
 * pi_mutex->wait_lock:
 *
 *	{uval, pi_state}
 *
 *	(and pi_mutex 'obviously')
 *
 * p->pi_lock:
 *
 *	p->pi_state_list -> pi_state->list, relation
 *	pi_mutex->owner -> pi_state->owner, relation
 *
 * pi_state->refcount:
 *
 *	pi_state lifetime
 *
 *
 * Lock order:
 *
 *   hb->lock
 *     pi_mutex->wait_lock
 *       p->pi_lock
 *
 */

/*
 * Validate that the existing waiter has a pi_state and sanity check
 * the pi_state against the user space value. If correct, attach to
 * it.
 */
static int attach_to_pi_state(u32 __user *uaddr, u32 uval,
			      struct futex_pi_state *pi_state,
			      struct futex_pi_state **ps)
{
	pid_t pid = uval & FUTEX_TID_MASK;
	u32 uval2;
	int ret;

	/*
	 * Userspace might have messed up non-PI and PI futexes [3]
	 */
	if (unlikely(!pi_state))
		return -EINVAL;

	/*
	 * We get here with hb->lock held, and having found a
	 * futex_top_waiter(). This means that futex_lock_pi() of said futex_q
	 * has dropped the hb->lock in between futex_queue() and futex_unqueue_pi(),
	 * which in turn means that futex_lock_pi() still has a reference on
	 * our pi_state.
	 *
	 * The waiter holding a reference on @pi_state also protects against
	 * the unlocked put_pi_state() in futex_unlock_pi(), futex_lock_pi()
	 * and futex_wait_requeue_pi() as it cannot go to 0 and consequently
	 * free pi_state before we can take a reference ourselves.
	 */
	WARN_ON(!refcount_read(&pi_state->refcount));

	/*
	 * Now that we have a pi_state, we can acquire wait_lock
	 * and do the state validation.
	 */
	raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);

	/*
	 * Since {uval, pi_state} is serialized by wait_lock, and our current
	 * uval was read without holding it, it can have changed. Verify it
	 * still is what we expect it to be, otherwise retry the entire
	 * operation.
	 */
	if (futex_get_value_locked(&uval2, uaddr))
		goto out_efault;

	if (uval != uval2)
		goto out_eagain;

	/*
	 * Handle the owner died case:
	 */
	if (uval & FUTEX_OWNER_DIED) {
		/*
		 * exit_pi_state_list sets owner to NULL and wakes the
		 * topmost waiter. The task which acquires the
		 * pi_state->rt_mutex will fixup owner.
		 */
		if (!pi_state->owner) {
			/*
			 * No pi state owner, but the user space TID
			 * is not 0. Inconsistent state. [5]
			 */
			if (pid)
				goto out_einval;
			/*
			 * Take a ref on the state and return success. [4]
			 */
			goto out_attach;
		}

		/*
		 * If TID is 0, then either the dying owner has not
		 * yet executed exit_pi_state_list() or some waiter
		 * acquired the rtmutex in the pi state, but did not
		 * yet fixup the TID in user space.
		 *
		 * Take a ref on the state and return success. [6]
		 */
		if (!pid)
			goto out_attach;
	} else {
		/*
		 * If the owner died bit is not set, then the pi_state
		 * must have an owner. [7]
		 */
		if (!pi_state->owner)
			goto out_einval;
	}

	/*
	 * Bail out if user space manipulated the futex value. If pi
	 * state exists then the owner TID must be the same as the
	 * user space TID. [9/10]
	 */
	if (pid != task_pid_vnr(pi_state->owner))
		goto out_einval;

out_attach:
	get_pi_state(pi_state);
	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
	*ps = pi_state;
	return 0;

out_einval:
	ret = -EINVAL;
	goto out_error;

out_eagain:
	ret = -EAGAIN;
	goto out_error;

out_efault:
	ret = -EFAULT;
	goto out_error;

out_error:
	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
	return ret;
}

static int handle_exit_race(u32 __user *uaddr, u32 uval,
			    struct task_struct *tsk)
{
	u32 uval2;

	/*
	 * If the futex exit state is not yet FUTEX_STATE_DEAD, tell the
	 * caller that the alleged owner is busy.
	 */
	if (tsk && tsk->futex_state != FUTEX_STATE_DEAD)
		return -EBUSY;

	/*
	 * Reread the user space value to handle the following situation:
	 *
	 * CPU0				CPU1
	 *
	 * sys_exit()			sys_futex()
	 *  do_exit()			 futex_lock_pi()
	 *                                futex_lock_pi_atomic()
	 *   exit_signals(tsk)		    No waiters:
	 *    tsk->flags |= PF_EXITING;	    *uaddr == 0x00000PID
	 *  mm_release(tsk)		    Set waiter bit
	 *   exit_robust_list(tsk) {	    *uaddr = 0x80000PID;
	 *      Set owner died		    attach_to_pi_owner() {
	 *    *uaddr = 0xC0000000;	     tsk = get_task(PID);
	 *   }				     if (!tsk->flags & PF_EXITING) {
	 *  ...				       attach();
	 *  tsk->futex_state =               } else {
	 *	FUTEX_STATE_DEAD;              if (tsk->futex_state !=
	 *					  FUTEX_STATE_DEAD)
	 *				         return -EAGAIN;
	 *				       return -ESRCH; <--- FAIL
	 *				     }
	 *
	 * Returning ESRCH unconditionally is wrong here because the
	 * user space value has been changed by the exiting task.
	 *
	 * The same logic applies to the case where the exiting task is
	 * already gone.
	 */
	if (futex_get_value_locked(&uval2, uaddr))
		return -EFAULT;

	/* If the user space value has changed, try again. */
	if (uval2 != uval)
		return -EAGAIN;

	/*
	 * The exiting task did not have a robust list, the robust list was
	 * corrupted or the user space value in *uaddr is simply bogus.
	 * Give up and tell user space.
	 */
	return -ESRCH;
}

static void __attach_to_pi_owner(struct task_struct *p, union futex_key *key,
				 struct futex_pi_state **ps)
{
	/*
	 * No existing pi state. First waiter. [2]
	 *
	 * This creates pi_state, we have hb->lock held, this means nothing can
	 * observe this state, wait_lock is irrelevant.
	 */
	struct futex_pi_state *pi_state = alloc_pi_state();

	/*
	 * Initialize the pi_mutex in locked state and make @p
	 * the owner of it:
	 */
	rt_mutex_init_proxy_locked(&pi_state->pi_mutex, p);

	/* Store the key for possible exit cleanups: */
	pi_state->key = *key;

	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &p->pi_state_list);
	/*
	 * Assignment without holding pi_state->pi_mutex.wait_lock is safe
	 * because there is no concurrency as the object is not published yet.
	 */
	pi_state->owner = p;

	*ps = pi_state;
}
/*
 * Lookup the task for the TID provided from user space and attach to
 * it after doing proper sanity checks.
 */
static int attach_to_pi_owner(u32 __user *uaddr, u32 uval, union futex_key *key,
			      struct futex_pi_state **ps,
			      struct task_struct **exiting)
{
	pid_t pid = uval & FUTEX_TID_MASK;
	struct task_struct *p;

	/*
	 * We are the first waiter - try to look up the real owner and attach
	 * the new pi_state to it, but bail out when TID = 0 [1]
	 *
	 * The !pid check is paranoid. None of the call sites should end up
	 * with pid == 0, but better safe than sorry. Let the caller retry
	 */
	if (!pid)
		return -EAGAIN;
	p = find_get_task_by_vpid(pid);
	if (!p)
		return handle_exit_race(uaddr, uval, NULL);

	if (unlikely(p->flags & PF_KTHREAD)) {
		put_task_struct(p);
		return -EPERM;
	}

	/*
	 * We need to look at the task state to figure out, whether the
	 * task is exiting. To protect against the change of the task state
	 * in futex_exit_release(), we do this protected by p->pi_lock:
	 */
	raw_spin_lock_irq(&p->pi_lock);
	if (unlikely(p->futex_state != FUTEX_STATE_OK)) {
		/*
		 * The task is on the way out. When the futex state is
		 * FUTEX_STATE_DEAD, we know that the task has finished
		 * the cleanup:
		 */
		int ret = handle_exit_race(uaddr, uval, p);

		raw_spin_unlock_irq(&p->pi_lock);
		/*
		 * If the owner task is between FUTEX_STATE_EXITING and
		 * FUTEX_STATE_DEAD then store the task pointer and keep
		 * the reference on the task struct. The calling code will
		 * drop all locks, wait for the task to reach
		 * FUTEX_STATE_DEAD and then drop the refcount. This is
		 * required to prevent a live lock when the current task
		 * preempted the exiting task between the two states.
		 */
		if (ret == -EBUSY)
			*exiting = p;
		else
			put_task_struct(p);
		return ret;
	}

	__attach_to_pi_owner(p, key, ps);
	raw_spin_unlock_irq(&p->pi_lock);

	put_task_struct(p);

	return 0;
}

static int lock_pi_update_atomic(u32 __user *uaddr, u32 uval, u32 newval)
{
	int err;
	u32 curval;

	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	err = futex_cmpxchg_value_locked(&curval, uaddr, uval, newval);
	if (unlikely(err))
		return err;

	/* If user space value changed, let the caller retry */
	return curval != uval ? -EAGAIN : 0;
}

/**
 * futex_lock_pi_atomic() - Atomic work required to acquire a pi aware futex
 * @uaddr:		the pi futex user address
 * @hb:			the pi futex hash bucket
 * @key:		the futex key associated with uaddr and hb
 * @ps:			the pi_state pointer where we store the result of the
 *			lookup
 * @task:		the task to perform the atomic lock work for.  This will
 *			be "current" except in the case of requeue pi.
 * @exiting:		Pointer to store the task pointer of the owner task
 *			which is in the middle of exiting
 * @set_waiters:	force setting the FUTEX_WAITERS bit (1) or not (0)
 *
 * Return:
 *  -  0 - ready to wait;
 *  -  1 - acquired the lock;
 *  - <0 - error
 *
 * The hb->lock must be held by the caller.
 *
 * @exiting is only set when the return value is -EBUSY. If so, this holds
 * a refcount on the exiting task on return and the caller needs to drop it
 * after waiting for the exit to complete.
 */
int futex_lock_pi_atomic(u32 __user *uaddr, struct futex_hash_bucket *hb,
			 union futex_key *key,
			 struct futex_pi_state **ps,
			 struct task_struct *task,
			 struct task_struct **exiting,
			 int set_waiters)
{
	u32 uval, newval, vpid = task_pid_vnr(task);
	struct futex_q *top_waiter;
	int ret;

	/*
	 * Read the user space value first so we can validate a few
	 * things before proceeding further.
	 */
	if (futex_get_value_locked(&uval, uaddr))
		return -EFAULT;

	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	/*
	 * Detect deadlocks.
	 */
	if ((unlikely((uval & FUTEX_TID_MASK) == vpid)))
		return -EDEADLK;

	if ((unlikely(should_fail_futex(true))))
		return -EDEADLK;

	/*
	 * Lookup existing state first. If it exists, try to attach to
	 * its pi_state.
	 */
	top_waiter = futex_top_waiter(hb, key);
	if (top_waiter)
		return attach_to_pi_state(uaddr, uval, top_waiter->pi_state, ps);

	/*
	 * No waiter and user TID is 0. We are here because the
	 * waiters or the owner died bit is set or called from
	 * requeue_cmp_pi or for whatever reason something took the
	 * syscall.
	 */
	if (!(uval & FUTEX_TID_MASK)) {
		/*
		 * We take over the futex. No other waiters and the user space
		 * TID is 0. We preserve the owner died bit.
		 */
		newval = uval & FUTEX_OWNER_DIED;
		newval |= vpid;

		/* The futex requeue_pi code can enforce the waiters bit */
		if (set_waiters)
			newval |= FUTEX_WAITERS;

		ret = lock_pi_update_atomic(uaddr, uval, newval);
		if (ret)
			return ret;

		/*
		 * If the waiter bit was requested the caller also needs PI
		 * state attached to the new owner of the user space futex.
		 *
		 * @task is guaranteed to be alive and it cannot be exiting
		 * because it is either sleeping or waiting in
		 * futex_requeue_pi_wakeup_sync().
		 *
		 * No need to do the full attach_to_pi_owner() exercise
		 * because @task is known and valid.
		 */
		if (set_waiters) {
			raw_spin_lock_irq(&task->pi_lock);
			__attach_to_pi_owner(task, key, ps);
			raw_spin_unlock_irq(&task->pi_lock);
		}
		return 1;
	}

	/*
	 * First waiter. Set the waiters bit before attaching ourself to
	 * the owner. If owner tries to unlock, it will be forced into
	 * the kernel and blocked on hb->lock.
	 */
	newval = uval | FUTEX_WAITERS;
	ret = lock_pi_update_atomic(uaddr, uval, newval);
	if (ret)
		return ret;
	/*
	 * If the update of the user space value succeeded, we try to
	 * attach to the owner. If that fails, no harm done, we only
	 * set the FUTEX_WAITERS bit in the user space variable.
	 */
	return attach_to_pi_owner(uaddr, newval, key, ps, exiting);
}

/*
 * Caller must hold a reference on @pi_state.
 */
static int wake_futex_pi(u32 __user *uaddr, u32 uval,
			 struct futex_pi_state *pi_state,
			 struct rt_mutex_waiter *top_waiter)
{
	struct task_struct *new_owner;
	bool postunlock = false;
	DEFINE_RT_WAKE_Q(wqh);
	u32 curval, newval;
	int ret = 0;

	new_owner = top_waiter->task;

	/*
	 * We pass it to the next owner. The WAITERS bit is always kept
	 * enabled while there is PI state around. We cleanup the owner
	 * died bit, because we are the owner.
	 */
	newval = FUTEX_WAITERS | task_pid_vnr(new_owner);

	if (unlikely(should_fail_futex(true))) {
		ret = -EFAULT;
		goto out_unlock;
	}

	ret = futex_cmpxchg_value_locked(&curval, uaddr, uval, newval);
	if (!ret && (curval != uval)) {
		/*
		 * If a unconditional UNLOCK_PI operation (user space did not
		 * try the TID->0 transition) raced with a waiter setting the
		 * FUTEX_WAITERS flag between get_user() and locking the hash
		 * bucket lock, retry the operation.
		 */
		if ((FUTEX_TID_MASK & curval) == uval)
			ret = -EAGAIN;
		else
			ret = -EINVAL;
	}

	if (!ret) {
		/*
		 * This is a point of no return; once we modified the uval
		 * there is no going back and subsequent operations must
		 * not fail.
		 */
		pi_state_update_owner(pi_state, new_owner);
		postunlock = __rt_mutex_futex_unlock(&pi_state->pi_mutex, &wqh);
	}

out_unlock:
	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);

	if (postunlock)
		rt_mutex_postunlock(&wqh);

	return ret;
}

static int __fixup_pi_state_owner(u32 __user *uaddr, struct futex_q *q,
				  struct task_struct *argowner)
{
	struct futex_pi_state *pi_state = q->pi_state;
	struct task_struct *oldowner, *newowner;
	u32 uval, curval, newval, newtid;
	int err = 0;

	oldowner = pi_state->owner;

	/*
	 * We are here because either:
	 *
	 *  - we stole the lock and pi_state->owner needs updating to reflect
	 *    that (@argowner == current),
	 *
	 * or:
	 *
	 *  - someone stole our lock and we need to fix things to point to the
	 *    new owner (@argowner == NULL).
	 *
	 * Either way, we have to replace the TID in the user space variable.
	 * This must be atomic as we have to preserve the owner died bit here.
	 *
	 * Note: We write the user space value _before_ changing the pi_state
	 * because we can fault here. Imagine swapped out pages or a fork
	 * that marked all the anonymous memory readonly for cow.
	 *
	 * Modifying pi_state _before_ the user space value would leave the
	 * pi_state in an inconsistent state when we fault here, because we
	 * need to drop the locks to handle the fault. This might be observed
	 * in the PID checks when attaching to PI state .
	 */
retry:
	if (!argowner) {
		if (oldowner != current) {
			/*
			 * We raced against a concurrent self; things are
			 * already fixed up. Nothing to do.
			 */
			return 0;
		}

		if (__rt_mutex_futex_trylock(&pi_state->pi_mutex)) {
			/* We got the lock. pi_state is correct. Tell caller. */
			return 1;
		}

		/*
		 * The trylock just failed, so either there is an owner or
		 * there is a higher priority waiter than this one.
		 */
		newowner = rt_mutex_owner(&pi_state->pi_mutex);
		/*
		 * If the higher priority waiter has not yet taken over the
		 * rtmutex then newowner is NULL. We can't return here with
		 * that state because it's inconsistent vs. the user space
		 * state. So drop the locks and try again. It's a valid
		 * situation and not any different from the other retry
		 * conditions.
		 */
		if (unlikely(!newowner)) {
			err = -EAGAIN;
			goto handle_err;
		}
	} else {
		WARN_ON_ONCE(argowner != current);
		if (oldowner == current) {
			/*
			 * We raced against a concurrent self; things are
			 * already fixed up. Nothing to do.
			 */
			return 1;
		}
		newowner = argowner;
	}

	newtid = task_pid_vnr(newowner) | FUTEX_WAITERS;
	/* Owner died? */
	if (!pi_state->owner)
		newtid |= FUTEX_OWNER_DIED;

	err = futex_get_value_locked(&uval, uaddr);
	if (err)
		goto handle_err;

	for (;;) {
		newval = (uval & FUTEX_OWNER_DIED) | newtid;

		err = futex_cmpxchg_value_locked(&curval, uaddr, uval, newval);
		if (err)
			goto handle_err;

		if (curval == uval)
			break;
		uval = curval;
	}

	/*
	 * We fixed up user space. Now we need to fix the pi_state
	 * itself.
	 */
	pi_state_update_owner(pi_state, newowner);

	return argowner == current;

	/*
	 * In order to reschedule or handle a page fault, we need to drop the
	 * locks here. In the case of a fault, this gives the other task
	 * (either the highest priority waiter itself or the task which stole
	 * the rtmutex) the chance to try the fixup of the pi_state. So once we
	 * are back from handling the fault we need to check the pi_state after
	 * reacquiring the locks and before trying to do another fixup. When
	 * the fixup has been done already we simply return.
	 *
	 * Note: we hold both hb->lock and pi_mutex->wait_lock. We can safely
	 * drop hb->lock since the caller owns the hb -> futex_q relation.
	 * Dropping the pi_mutex->wait_lock requires the state revalidate.
	 */
handle_err:
	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
	spin_unlock(q->lock_ptr);

	switch (err) {
	case -EFAULT:
		err = fault_in_user_writeable(uaddr);
		break;

	case -EAGAIN:
		cond_resched();
		err = 0;
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}

	spin_lock(q->lock_ptr);
	raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);

	/*
	 * Check if someone else fixed it for us:
	 */
	if (pi_state->owner != oldowner)
		return argowner == current;

	/* Retry if err was -EAGAIN or the fault in succeeded */
	if (!err)
		goto retry;

	/*
	 * fault_in_user_writeable() failed so user state is immutable. At
	 * best we can make the kernel state consistent but user state will
	 * be most likely hosed and any subsequent unlock operation will be
	 * rejected due to PI futex rule [10].
	 *
	 * Ensure that the rtmutex owner is also the pi_state owner despite
	 * the user space value claiming something different. There is no
	 * point in unlocking the rtmutex if current is the owner as it
	 * would need to wait until the next waiter has taken the rtmutex
	 * to guarantee consistent state. Keep it simple. Userspace asked
	 * for this wreckaged state.
	 *
	 * The rtmutex has an owner - either current or some other
	 * task. See the EAGAIN loop above.
	 */
	pi_state_update_owner(pi_state, rt_mutex_owner(&pi_state->pi_mutex));

	return err;
}

static int fixup_pi_state_owner(u32 __user *uaddr, struct futex_q *q,
				struct task_struct *argowner)
{
	struct futex_pi_state *pi_state = q->pi_state;
	int ret;

	lockdep_assert_held(q->lock_ptr);

	raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);
	ret = __fixup_pi_state_owner(uaddr, q, argowner);
	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
	return ret;
}

/**
 * fixup_pi_owner() - Post lock pi_state and corner case management
 * @uaddr:	user address of the futex
 * @q:		futex_q (contains pi_state and access to the rt_mutex)
 * @locked:	if the attempt to take the rt_mutex succeeded (1) or not (0)
 *
 * After attempting to lock an rt_mutex, this function is called to cleanup
 * the pi_state owner as well as handle race conditions that may allow us to
 * acquire the lock. Must be called with the hb lock held.
 *
 * Return:
 *  -  1 - success, lock taken;
 *  -  0 - success, lock not taken;
 *  - <0 - on error (-EFAULT)
 */
int fixup_pi_owner(u32 __user *uaddr, struct futex_q *q, int locked)
{
	if (locked) {
		/*
		 * Got the lock. We might not be the anticipated owner if we
		 * did a lock-steal - fix up the PI-state in that case:
		 *
		 * Speculative pi_state->owner read (we don't hold wait_lock);
		 * since we own the lock pi_state->owner == current is the
		 * stable state, anything else needs more attention.
		 */
		if (q->pi_state->owner != current)
			return fixup_pi_state_owner(uaddr, q, current);
		return 1;
	}

	/*
	 * If we didn't get the lock; check if anybody stole it from us. In
	 * that case, we need to fix up the uval to point to them instead of
	 * us, otherwise bad things happen. [10]
	 *
	 * Another speculative read; pi_state->owner == current is unstable
	 * but needs our attention.
	 */
	if (q->pi_state->owner == current)
		return fixup_pi_state_owner(uaddr, q, NULL);

	/*
	 * Paranoia check. If we did not take the lock, then we should not be
	 * the owner of the rt_mutex. Warn and establish consistent state.
	 */
	if (WARN_ON_ONCE(rt_mutex_owner(&q->pi_state->pi_mutex) == current))
		return fixup_pi_state_owner(uaddr, q, current);

	return 0;
}

/*
 * Userspace tried a 0 -> TID atomic transition of the futex value
 * and failed. The kernel side here does the whole locking operation:
 * if there are waiters then it will block as a consequence of relying
 * on rt-mutexes, it does PI, etc. (Due to races the kernel might see
 * a 0 value of the futex too.).
 *
 * Also serves as futex trylock_pi()'ing, and due semantics.
 */
int futex_lock_pi(u32 __user *uaddr, unsigned int flags, ktime_t *time, int trylock)
{
	struct hrtimer_sleeper timeout, *to;
	struct task_struct *exiting = NULL;
	struct rt_mutex_waiter rt_waiter;
	struct futex_hash_bucket *hb;
	struct futex_q q = futex_q_init;
	DEFINE_WAKE_Q(wake_q);
	int res, ret;

	if (!IS_ENABLED(CONFIG_FUTEX_PI))
		return -ENOSYS;

	if (refill_pi_state_cache())
		return -ENOMEM;

	to = futex_setup_timer(time, &timeout, flags, 0);

retry:
	ret = get_futex_key(uaddr, flags, &q.key, FUTEX_WRITE);
	if (unlikely(ret != 0))
		goto out;

retry_private:
	hb = futex_q_lock(&q);

	ret = futex_lock_pi_atomic(uaddr, hb, &q.key, &q.pi_state, current,
				   &exiting, 0);
	if (unlikely(ret)) {
		/*
		 * Atomic work succeeded and we got the lock,
		 * or failed. Either way, we do _not_ block.
		 */
		switch (ret) {
		case 1:
			/* We got the lock. */
			ret = 0;
			goto out_unlock_put_key;
		case -EFAULT:
			goto uaddr_faulted;
		case -EBUSY:
		case -EAGAIN:
			/*
			 * Two reasons for this:
			 * - EBUSY: Task is exiting and we just wait for the
			 *   exit to complete.
			 * - EAGAIN: The user space value changed.
			 */
			futex_q_unlock(hb);
			/*
			 * Handle the case where the owner is in the middle of
			 * exiting. Wait for the exit to complete otherwise
			 * this task might loop forever, aka. live lock.
			 */
			wait_for_owner_exiting(ret, exiting);
			cond_resched();
			goto retry;
		default:
			goto out_unlock_put_key;
		}
	}

	WARN_ON(!q.pi_state);

	/*
	 * Only actually queue now that the atomic ops are done:
	 */
	__futex_queue(&q, hb);

	if (trylock) {
		ret = rt_mutex_futex_trylock(&q.pi_state->pi_mutex);
		/* Fixup the trylock return value: */
		ret = ret ? 0 : -EWOULDBLOCK;
		goto no_block;
	}

	/*
	 * Must be done before we enqueue the waiter, here is unfortunately
	 * under the hb lock, but that *should* work because it does nothing.
	 */
	rt_mutex_pre_schedule();

	rt_mutex_init_waiter(&rt_waiter);

	/*
	 * On PREEMPT_RT, when hb->lock becomes an rt_mutex, we must not
	 * hold it while doing rt_mutex_start_proxy(), because then it will
	 * include hb->lock in the blocking chain, even through we'll not in
	 * fact hold it while blocking. This will lead it to report -EDEADLK
	 * and BUG when futex_unlock_pi() interleaves with this.
	 *
	 * Therefore acquire wait_lock while holding hb->lock, but drop the
	 * latter before calling __rt_mutex_start_proxy_lock(). This
	 * interleaves with futex_unlock_pi() -- which does a similar lock
	 * handoff -- such that the latter can observe the futex_q::pi_state
	 * before __rt_mutex_start_proxy_lock() is done.
	 */
	raw_spin_lock_irq(&q.pi_state->pi_mutex.wait_lock);
	spin_unlock(q.lock_ptr);
	/*
	 * __rt_mutex_start_proxy_lock() unconditionally enqueues the @rt_waiter
	 * such that futex_unlock_pi() is guaranteed to observe the waiter when
	 * it sees the futex_q::pi_state.
	 */
	ret = __rt_mutex_start_proxy_lock(&q.pi_state->pi_mutex, &rt_waiter, current, &wake_q);
	preempt_disable();
	raw_spin_unlock_irq(&q.pi_state->pi_mutex.wait_lock);
	wake_up_q(&wake_q);
	preempt_enable();

	if (ret) {
		if (ret == 1)
			ret = 0;
		goto cleanup;
	}

	if (unlikely(to))
		hrtimer_sleeper_start_expires(to, HRTIMER_MODE_ABS);

	ret = rt_mutex_wait_proxy_lock(&q.pi_state->pi_mutex, to, &rt_waiter);

cleanup:
	/*
	 * If we failed to acquire the lock (deadlock/signal/timeout), we must
	 * must unwind the above, however we canont lock hb->lock because
	 * rt_mutex already has a waiter enqueued and hb->lock can itself try
	 * and enqueue an rt_waiter through rtlock.
	 *
	 * Doing the cleanup without holding hb->lock can cause inconsistent
	 * state between hb and pi_state, but only in the direction of not
	 * seeing a waiter that is leaving.
	 *
	 * See futex_unlock_pi(), it deals with this inconsistency.
	 *
	 * There be dragons here, since we must deal with the inconsistency on
	 * the way out (here), it is impossible to detect/warn about the race
	 * the other way around (missing an incoming waiter).
	 *
	 * What could possibly go wrong...
	 */
	if (ret && !rt_mutex_cleanup_proxy_lock(&q.pi_state->pi_mutex, &rt_waiter))
		ret = 0;

	/*
	 * Now that the rt_waiter has been dequeued, it is safe to use
	 * spinlock/rtlock (which might enqueue its own rt_waiter) and fix up
	 * the
	 */
	spin_lock(q.lock_ptr);
	/*
	 * Waiter is unqueued.
	 */
	rt_mutex_post_schedule();
no_block:
	/*
	 * Fixup the pi_state owner and possibly acquire the lock if we
	 * haven't already.
	 */
	res = fixup_pi_owner(uaddr, &q, !ret);
	/*
	 * If fixup_pi_owner() returned an error, propagate that.  If it acquired
	 * the lock, clear our -ETIMEDOUT or -EINTR.
	 */
	if (res)
		ret = (res < 0) ? res : 0;

	futex_unqueue_pi(&q);
	spin_unlock(q.lock_ptr);
	goto out;

out_unlock_put_key:
	futex_q_unlock(hb);

out:
	if (to) {
		hrtimer_cancel(&to->timer);
		destroy_hrtimer_on_stack(&to->timer);
	}
	return ret != -EINTR ? ret : -ERESTARTNOINTR;

uaddr_faulted:
	futex_q_unlock(hb);

	ret = fault_in_user_writeable(uaddr);
	if (ret)
		goto out;

	if (!(flags & FLAGS_SHARED))
		goto retry_private;

	goto retry;
}

/*
 * Userspace attempted a TID -> 0 atomic transition, and failed.
 * This is the in-kernel slowpath: we look up the PI state (if any),
 * and do the rt-mutex unlock.
 */
int futex_unlock_pi(u32 __user *uaddr, unsigned int flags)
{
	u32 curval, uval, vpid = task_pid_vnr(current);
	union futex_key key = FUTEX_KEY_INIT;
	struct futex_hash_bucket *hb;
	struct futex_q *top_waiter;
	int ret;

	if (!IS_ENABLED(CONFIG_FUTEX_PI))
		return -ENOSYS;

retry:
	if (get_user(uval, uaddr))
		return -EFAULT;
	/*
	 * We release only a lock we actually own:
	 */
	if ((uval & FUTEX_TID_MASK) != vpid)
		return -EPERM;

	ret = get_futex_key(uaddr, flags, &key, FUTEX_WRITE);
	if (ret)
		return ret;

	hb = futex_hash(&key);
	spin_lock(&hb->lock);
retry_hb:

	/*
	 * Check waiters first. We do not trust user space values at
	 * all and we at least want to know if user space fiddled
	 * with the futex value instead of blindly unlocking.
	 */
	top_waiter = futex_top_waiter(hb, &key);
	if (top_waiter) {
		struct futex_pi_state *pi_state = top_waiter->pi_state;
		struct rt_mutex_waiter *rt_waiter;

		ret = -EINVAL;
		if (!pi_state)
			goto out_unlock;

		/*
		 * If current does not own the pi_state then the futex is
		 * inconsistent and user space fiddled with the futex value.
		 */
		if (pi_state->owner != current)
			goto out_unlock;

		/*
		 * By taking wait_lock while still holding hb->lock, we ensure
		 * there is no point where we hold neither; and thereby
		 * wake_futex_pi() must observe any new waiters.
		 *
		 * Since the cleanup: case in futex_lock_pi() removes the
		 * rt_waiter without holding hb->lock, it is possible for
		 * wake_futex_pi() to not find a waiter while the above does,
		 * in this case the waiter is on the way out and it can be
		 * ignored.
		 *
		 * In particular; this forces __rt_mutex_start_proxy() to
		 * complete such that we're guaranteed to observe the
		 * rt_waiter.
		 */
		raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);

		/*
		 * Futex vs rt_mutex waiter state -- if there are no rt_mutex
		 * waiters even though futex thinks there are, then the waiter
		 * is leaving. The entry needs to be removed from the list so a
		 * new futex_lock_pi() is not using this stale PI-state while
		 * the futex is available in user space again.
		 * There can be more than one task on its way out so it needs
		 * to retry.
		 */
		rt_waiter = rt_mutex_top_waiter(&pi_state->pi_mutex);
		if (!rt_waiter) {
			__futex_unqueue(top_waiter);
			raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
			goto retry_hb;
		}

		get_pi_state(pi_state);
		spin_unlock(&hb->lock);

		/* drops pi_state->pi_mutex.wait_lock */
		ret = wake_futex_pi(uaddr, uval, pi_state, rt_waiter);

		put_pi_state(pi_state);

		/*
		 * Success, we're done! No tricky corner cases.
		 */
		if (!ret)
			return ret;
		/*
		 * The atomic access to the futex value generated a
		 * pagefault, so retry the user-access and the wakeup:
		 */
		if (ret == -EFAULT)
			goto pi_faulted;
		/*
		 * A unconditional UNLOCK_PI op raced against a waiter
		 * setting the FUTEX_WAITERS bit. Try again.
		 */
		if (ret == -EAGAIN)
			goto pi_retry;
		/*
		 * wake_futex_pi has detected invalid state. Tell user
		 * space.
		 */
		return ret;
	}

	/*
	 * We have no kernel internal state, i.e. no waiters in the
	 * kernel. Waiters which are about to queue themselves are stuck
	 * on hb->lock. So we can safely ignore them. We do neither
	 * preserve the WAITERS bit not the OWNER_DIED one. We are the
	 * owner.
	 */
	if ((ret = futex_cmpxchg_value_locked(&curval, uaddr, uval, 0))) {
		spin_unlock(&hb->lock);
		switch (ret) {
		case -EFAULT:
			goto pi_faulted;

		case -EAGAIN:
			goto pi_retry;

		default:
			WARN_ON_ONCE(1);
			return ret;
		}
	}

	/*
	 * If uval has changed, let user space handle it.
	 */
	ret = (curval == uval) ? 0 : -EAGAIN;

out_unlock:
	spin_unlock(&hb->lock);
	return ret;

pi_retry:
	cond_resched();
	goto retry;

pi_faulted:

	ret = fault_in_user_writeable(uaddr);
	if (!ret)
		goto retry;

	return ret;
}

