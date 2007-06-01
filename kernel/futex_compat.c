/*
 * linux/kernel/futex_compat.c
 *
 * Futex compatibililty routines.
 *
 * Copyright 2006, Red Hat, Inc., Ingo Molnar
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/futex.h>

#include <asm/uaccess.h>


/*
 * Fetch a robust-list pointer. Bit 0 signals PI futexes:
 */
static inline int
fetch_robust_entry(compat_uptr_t *uentry, struct robust_list __user **entry,
		   compat_uptr_t __user *head, int *pi)
{
	if (get_user(*uentry, head))
		return -EFAULT;

	*entry = compat_ptr((*uentry) & ~1);
	*pi = (unsigned int)(*uentry) & 1;

	return 0;
}

/*
 * Walk curr->robust_list (very carefully, it's a userspace list!)
 * and mark any locks found there dead, and notify any waiters.
 *
 * We silently return on any sign of list-walking problem.
 */
void compat_exit_robust_list(struct task_struct *curr)
{
	struct compat_robust_list_head __user *head = curr->compat_robust_list;
	struct robust_list __user *entry, *pending;
	unsigned int limit = ROBUST_LIST_LIMIT, pi, pip;
	compat_uptr_t uentry, upending;
	compat_long_t futex_offset;

	/*
	 * Fetch the list head (which was registered earlier, via
	 * sys_set_robust_list()):
	 */
	if (fetch_robust_entry(&uentry, &entry, &head->list.next, &pi))
		return;
	/*
	 * Fetch the relative futex offset:
	 */
	if (get_user(futex_offset, &head->futex_offset))
		return;
	/*
	 * Fetch any possibly pending lock-add first, and handle it
	 * if it exists:
	 */
	if (fetch_robust_entry(&upending, &pending,
			       &head->list_op_pending, &pip))
		return;
	if (upending)
		handle_futex_death((void __user *)pending + futex_offset, curr, pip);

	while (compat_ptr(uentry) != &head->list) {
		/*
		 * A pending lock might already be on the list, so
		 * dont process it twice:
		 */
		if (entry != pending)
			if (handle_futex_death((void __user *)entry + futex_offset,
						curr, pi))
				return;

		/*
		 * Fetch the next entry in the list:
		 */
		if (fetch_robust_entry(&uentry, &entry,
				       (compat_uptr_t __user *)&entry->next, &pi))
			return;
		/*
		 * Avoid excessively long or circular lists:
		 */
		if (!--limit)
			break;

		cond_resched();
	}
}

asmlinkage long
compat_sys_set_robust_list(struct compat_robust_list_head __user *head,
			   compat_size_t len)
{
	if (unlikely(len != sizeof(*head)))
		return -EINVAL;

	current->compat_robust_list = head;

	return 0;
}

asmlinkage long
compat_sys_get_robust_list(int pid, compat_uptr_t __user *head_ptr,
			   compat_size_t __user *len_ptr)
{
	struct compat_robust_list_head __user *head;
	unsigned long ret;

	if (!pid)
		head = current->compat_robust_list;
	else {
		struct task_struct *p;

		ret = -ESRCH;
		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);
		if (!p)
			goto err_unlock;
		ret = -EPERM;
		if ((current->euid != p->euid) && (current->euid != p->uid) &&
				!capable(CAP_SYS_PTRACE))
			goto err_unlock;
		head = p->compat_robust_list;
		read_unlock(&tasklist_lock);
	}

	if (put_user(sizeof(*head), len_ptr))
		return -EFAULT;
	return put_user(ptr_to_compat(head), head_ptr);

err_unlock:
	read_unlock(&tasklist_lock);

	return ret;
}

asmlinkage long compat_sys_futex(u32 __user *uaddr, int op, u32 val,
		struct compat_timespec __user *utime, u32 __user *uaddr2,
		u32 val3)
{
	struct timespec ts;
	ktime_t t, *tp = NULL;
	int val2 = 0;
	int cmd = op & FUTEX_CMD_MASK;

	if (utime && (cmd == FUTEX_WAIT || cmd == FUTEX_LOCK_PI)) {
		if (get_compat_timespec(&ts, utime))
			return -EFAULT;
		if (!timespec_valid(&ts))
			return -EINVAL;

		t = timespec_to_ktime(ts);
		if (cmd == FUTEX_WAIT)
			t = ktime_add(ktime_get(), t);
		tp = &t;
	}
	if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE
	    || cmd == FUTEX_CMP_REQUEUE_PI)
		val2 = (int) (unsigned long) utime;

	return do_futex(uaddr, op, val, tp, uaddr2, val2, val3);
}
