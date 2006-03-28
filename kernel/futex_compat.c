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
 * Walk curr->robust_list (very carefully, it's a userspace list!)
 * and mark any locks found there dead, and notify any waiters.
 *
 * We silently return on any sign of list-walking problem.
 */
void compat_exit_robust_list(struct task_struct *curr)
{
	struct compat_robust_list_head __user *head = curr->compat_robust_list;
	struct robust_list __user *entry, *pending;
	compat_uptr_t uentry, upending;
	unsigned int limit = ROBUST_LIST_LIMIT;
	compat_long_t futex_offset;

	/*
	 * Fetch the list head (which was registered earlier, via
	 * sys_set_robust_list()):
	 */
	if (get_user(uentry, &head->list.next))
		return;
	entry = compat_ptr(uentry);
	/*
	 * Fetch the relative futex offset:
	 */
	if (get_user(futex_offset, &head->futex_offset))
		return;
	/*
	 * Fetch any possibly pending lock-add first, and handle it
	 * if it exists:
	 */
	if (get_user(upending, &head->list_op_pending))
		return;
	pending = compat_ptr(upending);
	if (upending)
		handle_futex_death((void *)pending + futex_offset, curr);

	while (compat_ptr(uentry) != &head->list) {
		/*
		 * A pending lock might already be on the list, so
		 * dont process it twice:
		 */
		if (entry != pending)
			if (handle_futex_death((void *)entry + futex_offset,
						curr))
				return;

		/*
		 * Fetch the next entry in the list:
		 */
		if (get_user(uentry, (compat_uptr_t *)&entry->next))
			return;
		entry = compat_ptr(uentry);
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
compat_sys_get_robust_list(int pid, compat_uptr_t *head_ptr,
			   compat_size_t __user *len_ptr)
{
	struct compat_robust_list_head *head;
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
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (get_compat_timespec(&t, utime))
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	if (op >= FUTEX_REQUEUE)
		val2 = (int) (unsigned long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2, val3);
}
