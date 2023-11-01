// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/syscalls.h>
#include <linux/time_namespace.h>

#include "futex.h"

/*
 * Support for robust futexes: the kernel cleans up held futexes at
 * thread exit time.
 *
 * Implementation: user-space maintains a per-thread list of locks it
 * is holding. Upon do_exit(), the kernel carefully walks this list,
 * and marks all locks that are owned by this thread with the
 * FUTEX_OWNER_DIED bit, and wakes up a waiter (if any). The list is
 * always manipulated with the lock held, so the list is private and
 * per-thread. Userspace also maintains a per-thread 'list_op_pending'
 * field, to allow the kernel to clean up if the thread dies after
 * acquiring the lock, but just before it could have added itself to
 * the list. There can only be one such pending lock.
 */

/**
 * sys_set_robust_list() - Set the robust-futex list head of a task
 * @head:	pointer to the list-head
 * @len:	length of the list-head, as userspace expects
 */
SYSCALL_DEFINE2(set_robust_list, struct robust_list_head __user *, head,
		size_t, len)
{
	/*
	 * The kernel knows only one size for now:
	 */
	if (unlikely(len != sizeof(*head)))
		return -EINVAL;

	current->robust_list = head;

	return 0;
}

/**
 * sys_get_robust_list() - Get the robust-futex list head of a task
 * @pid:	pid of the process [zero for current task]
 * @head_ptr:	pointer to a list-head pointer, the kernel fills it in
 * @len_ptr:	pointer to a length field, the kernel fills in the header size
 */
SYSCALL_DEFINE3(get_robust_list, int, pid,
		struct robust_list_head __user * __user *, head_ptr,
		size_t __user *, len_ptr)
{
	struct robust_list_head __user *head;
	unsigned long ret;
	struct task_struct *p;

	rcu_read_lock();

	ret = -ESRCH;
	if (!pid)
		p = current;
	else {
		p = find_task_by_vpid(pid);
		if (!p)
			goto err_unlock;
	}

	ret = -EPERM;
	if (!ptrace_may_access(p, PTRACE_MODE_READ_REALCREDS))
		goto err_unlock;

	head = p->robust_list;
	rcu_read_unlock();

	if (put_user(sizeof(*head), len_ptr))
		return -EFAULT;
	return put_user(head, head_ptr);

err_unlock:
	rcu_read_unlock();

	return ret;
}

long do_futex(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
		u32 __user *uaddr2, u32 val2, u32 val3)
{
	unsigned int flags = futex_to_flags(op);
	int cmd = op & FUTEX_CMD_MASK;

	if (flags & FLAGS_CLOCKRT) {
		if (cmd != FUTEX_WAIT_BITSET &&
		    cmd != FUTEX_WAIT_REQUEUE_PI &&
		    cmd != FUTEX_LOCK_PI2)
			return -ENOSYS;
	}

	switch (cmd) {
	case FUTEX_WAIT:
		val3 = FUTEX_BITSET_MATCH_ANY;
		fallthrough;
	case FUTEX_WAIT_BITSET:
		return futex_wait(uaddr, flags, val, timeout, val3);
	case FUTEX_WAKE:
		val3 = FUTEX_BITSET_MATCH_ANY;
		fallthrough;
	case FUTEX_WAKE_BITSET:
		return futex_wake(uaddr, flags, val, val3);
	case FUTEX_REQUEUE:
		return futex_requeue(uaddr, flags, uaddr2, flags, val, val2, NULL, 0);
	case FUTEX_CMP_REQUEUE:
		return futex_requeue(uaddr, flags, uaddr2, flags, val, val2, &val3, 0);
	case FUTEX_WAKE_OP:
		return futex_wake_op(uaddr, flags, uaddr2, val, val2, val3);
	case FUTEX_LOCK_PI:
		flags |= FLAGS_CLOCKRT;
		fallthrough;
	case FUTEX_LOCK_PI2:
		return futex_lock_pi(uaddr, flags, timeout, 0);
	case FUTEX_UNLOCK_PI:
		return futex_unlock_pi(uaddr, flags);
	case FUTEX_TRYLOCK_PI:
		return futex_lock_pi(uaddr, flags, NULL, 1);
	case FUTEX_WAIT_REQUEUE_PI:
		val3 = FUTEX_BITSET_MATCH_ANY;
		return futex_wait_requeue_pi(uaddr, flags, val, timeout, val3,
					     uaddr2);
	case FUTEX_CMP_REQUEUE_PI:
		return futex_requeue(uaddr, flags, uaddr2, flags, val, val2, &val3, 1);
	}
	return -ENOSYS;
}

static __always_inline bool futex_cmd_has_timeout(u32 cmd)
{
	switch (cmd) {
	case FUTEX_WAIT:
	case FUTEX_LOCK_PI:
	case FUTEX_LOCK_PI2:
	case FUTEX_WAIT_BITSET:
	case FUTEX_WAIT_REQUEUE_PI:
		return true;
	}
	return false;
}

static __always_inline int
futex_init_timeout(u32 cmd, u32 op, struct timespec64 *ts, ktime_t *t)
{
	if (!timespec64_valid(ts))
		return -EINVAL;

	*t = timespec64_to_ktime(*ts);
	if (cmd == FUTEX_WAIT)
		*t = ktime_add_safe(ktime_get(), *t);
	else if (cmd != FUTEX_LOCK_PI && !(op & FUTEX_CLOCK_REALTIME))
		*t = timens_ktime_to_host(CLOCK_MONOTONIC, *t);
	return 0;
}

SYSCALL_DEFINE6(futex, u32 __user *, uaddr, int, op, u32, val,
		const struct __kernel_timespec __user *, utime,
		u32 __user *, uaddr2, u32, val3)
{
	int ret, cmd = op & FUTEX_CMD_MASK;
	ktime_t t, *tp = NULL;
	struct timespec64 ts;

	if (utime && futex_cmd_has_timeout(cmd)) {
		if (unlikely(should_fail_futex(!(op & FUTEX_PRIVATE_FLAG))))
			return -EFAULT;
		if (get_timespec64(&ts, utime))
			return -EFAULT;
		ret = futex_init_timeout(cmd, op, &ts, &t);
		if (ret)
			return ret;
		tp = &t;
	}

	return do_futex(uaddr, op, val, tp, uaddr2, (unsigned long)utime, val3);
}

/**
 * futex_parse_waitv - Parse a waitv array from userspace
 * @futexv:	Kernel side list of waiters to be filled
 * @uwaitv:     Userspace list to be parsed
 * @nr_futexes: Length of futexv
 * @wake:	Wake to call when futex is woken
 * @wake_data:	Data for the wake handler
 *
 * Return: Error code on failure, 0 on success
 */
int futex_parse_waitv(struct futex_vector *futexv,
		      struct futex_waitv __user *uwaitv,
		      unsigned int nr_futexes, futex_wake_fn *wake,
		      void *wake_data)
{
	struct futex_waitv aux;
	unsigned int i;

	for (i = 0; i < nr_futexes; i++) {
		unsigned int flags;

		if (copy_from_user(&aux, &uwaitv[i], sizeof(aux)))
			return -EFAULT;

		if ((aux.flags & ~FUTEX2_VALID_MASK) || aux.__reserved)
			return -EINVAL;

		flags = futex2_to_flags(aux.flags);
		if (!futex_flags_valid(flags))
			return -EINVAL;

		if (!futex_validate_input(flags, aux.val))
			return -EINVAL;

		futexv[i].w.flags = flags;
		futexv[i].w.val = aux.val;
		futexv[i].w.uaddr = aux.uaddr;
		futexv[i].q = futex_q_init;
		futexv[i].q.wake = wake;
		futexv[i].q.wake_data = wake_data;
	}

	return 0;
}

static int futex2_setup_timeout(struct __kernel_timespec __user *timeout,
				clockid_t clockid, struct hrtimer_sleeper *to)
{
	int flag_clkid = 0, flag_init = 0;
	struct timespec64 ts;
	ktime_t time;
	int ret;

	if (!timeout)
		return 0;

	if (clockid == CLOCK_REALTIME) {
		flag_clkid = FLAGS_CLOCKRT;
		flag_init = FUTEX_CLOCK_REALTIME;
	}

	if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC)
		return -EINVAL;

	if (get_timespec64(&ts, timeout))
		return -EFAULT;

	/*
	 * Since there's no opcode for futex_waitv, use
	 * FUTEX_WAIT_BITSET that uses absolute timeout as well
	 */
	ret = futex_init_timeout(FUTEX_WAIT_BITSET, flag_init, &ts, &time);
	if (ret)
		return ret;

	futex_setup_timer(&time, to, flag_clkid, 0);
	return 0;
}

static inline void futex2_destroy_timeout(struct hrtimer_sleeper *to)
{
	hrtimer_cancel(&to->timer);
	destroy_hrtimer_on_stack(&to->timer);
}

/**
 * sys_futex_waitv - Wait on a list of futexes
 * @waiters:    List of futexes to wait on
 * @nr_futexes: Length of futexv
 * @flags:      Flag for timeout (monotonic/realtime)
 * @timeout:	Optional absolute timeout.
 * @clockid:	Clock to be used for the timeout, realtime or monotonic.
 *
 * Given an array of `struct futex_waitv`, wait on each uaddr. The thread wakes
 * if a futex_wake() is performed at any uaddr. The syscall returns immediately
 * if any waiter has *uaddr != val. *timeout is an optional timeout value for
 * the operation. Each waiter has individual flags. The `flags` argument for
 * the syscall should be used solely for specifying the timeout as realtime, if
 * needed. Flags for private futexes, sizes, etc. should be used on the
 * individual flags of each waiter.
 *
 * Returns the array index of one of the woken futexes. No further information
 * is provided: any number of other futexes may also have been woken by the
 * same event, and if more than one futex was woken, the retrned index may
 * refer to any one of them. (It is not necessaryily the futex with the
 * smallest index, nor the one most recently woken, nor...)
 */

SYSCALL_DEFINE5(futex_waitv, struct futex_waitv __user *, waiters,
		unsigned int, nr_futexes, unsigned int, flags,
		struct __kernel_timespec __user *, timeout, clockid_t, clockid)
{
	struct hrtimer_sleeper to;
	struct futex_vector *futexv;
	int ret;

	/* This syscall supports no flags for now */
	if (flags)
		return -EINVAL;

	if (!nr_futexes || nr_futexes > FUTEX_WAITV_MAX || !waiters)
		return -EINVAL;

	if (timeout && (ret = futex2_setup_timeout(timeout, clockid, &to)))
		return ret;

	futexv = kcalloc(nr_futexes, sizeof(*futexv), GFP_KERNEL);
	if (!futexv) {
		ret = -ENOMEM;
		goto destroy_timer;
	}

	ret = futex_parse_waitv(futexv, waiters, nr_futexes, futex_wake_mark,
				NULL);
	if (!ret)
		ret = futex_wait_multiple(futexv, nr_futexes, timeout ? &to : NULL);

	kfree(futexv);

destroy_timer:
	if (timeout)
		futex2_destroy_timeout(&to);
	return ret;
}

/*
 * sys_futex_wake - Wake a number of futexes
 * @uaddr:	Address of the futex(es) to wake
 * @mask:	bitmask
 * @nr:		Number of the futexes to wake
 * @flags:	FUTEX2 flags
 *
 * Identical to the traditional FUTEX_WAKE_BITSET op, except it is part of the
 * futex2 family of calls.
 */

SYSCALL_DEFINE4(futex_wake,
		void __user *, uaddr,
		unsigned long, mask,
		int, nr,
		unsigned int, flags)
{
	if (flags & ~FUTEX2_VALID_MASK)
		return -EINVAL;

	flags = futex2_to_flags(flags);
	if (!futex_flags_valid(flags))
		return -EINVAL;

	if (!futex_validate_input(flags, mask))
		return -EINVAL;

	return futex_wake(uaddr, FLAGS_STRICT | flags, nr, mask);
}

/*
 * sys_futex_wait - Wait on a futex
 * @uaddr:	Address of the futex to wait on
 * @val:	Value of @uaddr
 * @mask:	bitmask
 * @flags:	FUTEX2 flags
 * @timeout:	Optional absolute timeout
 * @clockid:	Clock to be used for the timeout, realtime or monotonic
 *
 * Identical to the traditional FUTEX_WAIT_BITSET op, except it is part of the
 * futex2 familiy of calls.
 */

SYSCALL_DEFINE6(futex_wait,
		void __user *, uaddr,
		unsigned long, val,
		unsigned long, mask,
		unsigned int, flags,
		struct __kernel_timespec __user *, timeout,
		clockid_t, clockid)
{
	struct hrtimer_sleeper to;
	int ret;

	if (flags & ~FUTEX2_VALID_MASK)
		return -EINVAL;

	flags = futex2_to_flags(flags);
	if (!futex_flags_valid(flags))
		return -EINVAL;

	if (!futex_validate_input(flags, val) ||
	    !futex_validate_input(flags, mask))
		return -EINVAL;

	if (timeout && (ret = futex2_setup_timeout(timeout, clockid, &to)))
		return ret;

	ret = __futex_wait(uaddr, flags, val, timeout ? &to : NULL, mask);

	if (timeout)
		futex2_destroy_timeout(&to);

	return ret;
}

/*
 * sys_futex_requeue - Requeue a waiter from one futex to another
 * @waiters:	array describing the source and destination futex
 * @flags:	unused
 * @nr_wake:	number of futexes to wake
 * @nr_requeue:	number of futexes to requeue
 *
 * Identical to the traditional FUTEX_CMP_REQUEUE op, except it is part of the
 * futex2 family of calls.
 */

SYSCALL_DEFINE4(futex_requeue,
		struct futex_waitv __user *, waiters,
		unsigned int, flags,
		int, nr_wake,
		int, nr_requeue)
{
	struct futex_vector futexes[2];
	u32 cmpval;
	int ret;

	if (flags)
		return -EINVAL;

	if (!waiters)
		return -EINVAL;

	ret = futex_parse_waitv(futexes, waiters, 2, futex_wake_mark, NULL);
	if (ret)
		return ret;

	cmpval = futexes[0].w.val;

	return futex_requeue(u64_to_user_ptr(futexes[0].w.uaddr), futexes[0].w.flags,
			     u64_to_user_ptr(futexes[1].w.uaddr), futexes[1].w.flags,
			     nr_wake, nr_requeue, &cmpval, 0);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(set_robust_list,
		struct compat_robust_list_head __user *, head,
		compat_size_t, len)
{
	if (unlikely(len != sizeof(*head)))
		return -EINVAL;

	current->compat_robust_list = head;

	return 0;
}

COMPAT_SYSCALL_DEFINE3(get_robust_list, int, pid,
			compat_uptr_t __user *, head_ptr,
			compat_size_t __user *, len_ptr)
{
	struct compat_robust_list_head __user *head;
	unsigned long ret;
	struct task_struct *p;

	rcu_read_lock();

	ret = -ESRCH;
	if (!pid)
		p = current;
	else {
		p = find_task_by_vpid(pid);
		if (!p)
			goto err_unlock;
	}

	ret = -EPERM;
	if (!ptrace_may_access(p, PTRACE_MODE_READ_REALCREDS))
		goto err_unlock;

	head = p->compat_robust_list;
	rcu_read_unlock();

	if (put_user(sizeof(*head), len_ptr))
		return -EFAULT;
	return put_user(ptr_to_compat(head), head_ptr);

err_unlock:
	rcu_read_unlock();

	return ret;
}
#endif /* CONFIG_COMPAT */

#ifdef CONFIG_COMPAT_32BIT_TIME
SYSCALL_DEFINE6(futex_time32, u32 __user *, uaddr, int, op, u32, val,
		const struct old_timespec32 __user *, utime, u32 __user *, uaddr2,
		u32, val3)
{
	int ret, cmd = op & FUTEX_CMD_MASK;
	ktime_t t, *tp = NULL;
	struct timespec64 ts;

	if (utime && futex_cmd_has_timeout(cmd)) {
		if (get_old_timespec32(&ts, utime))
			return -EFAULT;
		ret = futex_init_timeout(cmd, op, &ts, &t);
		if (ret)
			return ret;
		tp = &t;
	}

	return do_futex(uaddr, op, val, tp, uaddr2, (unsigned long)utime, val3);
}
#endif /* CONFIG_COMPAT_32BIT_TIME */

