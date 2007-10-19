/*
 * linux/kernel/capability.c
 *
 * Copyright (C) 1997  Andrew Main <zefram@fysh.org>
 *
 * Integrated into 2.1.97+,  Andrew G. Morgan <morgan@kernel.org>
 * 30 May 2002:	Cleanup, Robert M. Love <rml@tech9.net>
 */

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/pid_namespace.h>
#include <asm/uaccess.h>

/*
 * This lock protects task->cap_* for all tasks including current.
 * Locking rule: acquire this prior to tasklist_lock.
 */
static DEFINE_SPINLOCK(task_capability_lock);

/*
 * For sys_getproccap() and sys_setproccap(), any of the three
 * capability set pointers may be NULL -- indicating that that set is
 * uninteresting and/or not to be changed.
 */

/**
 * sys_capget - get the capabilities of a given process.
 * @header: pointer to struct that contains capability version and
 *	target pid data
 * @dataptr: pointer to struct that contains the effective, permitted,
 *	and inheritable capabilities that are returned
 *
 * Returns 0 on success and < 0 on error.
 */
asmlinkage long sys_capget(cap_user_header_t header, cap_user_data_t dataptr)
{
	int ret = 0;
	pid_t pid;
	__u32 version;
	struct task_struct *target;
	struct __user_cap_data_struct data;

	if (get_user(version, &header->version))
		return -EFAULT;

	if (version != _LINUX_CAPABILITY_VERSION) {
		if (put_user(_LINUX_CAPABILITY_VERSION, &header->version))
			return -EFAULT;
		return -EINVAL;
	}

	if (get_user(pid, &header->pid))
		return -EFAULT;

	if (pid < 0)
		return -EINVAL;

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	if (pid && pid != task_pid_vnr(current)) {
		target = find_task_by_pid_ns(pid,
				current->nsproxy->pid_ns);
		if (!target) {
			ret = -ESRCH;
			goto out;
		}
	} else
		target = current;

	ret = security_capget(target, &data.effective, &data.inheritable, &data.permitted);

out:
	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	if (!ret && copy_to_user(dataptr, &data, sizeof data))
		return -EFAULT;

	return ret;
}

/*
 * cap_set_pg - set capabilities for all processes in a given process
 * group.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline int cap_set_pg(int pgrp_nr, kernel_cap_t *effective,
			      kernel_cap_t *inheritable,
			      kernel_cap_t *permitted)
{
	struct task_struct *g, *target;
	int ret = -EPERM;
	int found = 0;
	struct pid *pgrp;

	pgrp = find_pid_ns(pgrp_nr, current->nsproxy->pid_ns);
	do_each_pid_task(pgrp, PIDTYPE_PGID, g) {
		target = g;
		while_each_thread(g, target) {
			if (!security_capset_check(target, effective,
							inheritable,
							permitted)) {
				security_capset_set(target, effective,
							inheritable,
							permitted);
				ret = 0;
			}
			found = 1;
		}
	} while_each_pid_task(pgrp, PIDTYPE_PGID, g);

	if (!found)
		ret = 0;
	return ret;
}

/*
 * cap_set_all - set capabilities for all processes other than init
 * and self.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline int cap_set_all(kernel_cap_t *effective,
			       kernel_cap_t *inheritable,
			       kernel_cap_t *permitted)
{
     struct task_struct *g, *target;
     int ret = -EPERM;
     int found = 0;

     do_each_thread(g, target) {
             if (target == current || is_container_init(target->group_leader))
                     continue;
             found = 1;
	     if (security_capset_check(target, effective, inheritable,
						permitted))
		     continue;
	     ret = 0;
	     security_capset_set(target, effective, inheritable, permitted);
     } while_each_thread(g, target);

     if (!found)
	     ret = 0;
     return ret;
}

/**
 * sys_capset - set capabilities for a process or a group of processes
 * @header: pointer to struct that contains capability version and
 *	target pid data
 * @data: pointer to struct that contains the effective, permitted,
 *	and inheritable capabilities
 *
 * Set capabilities for a given process, all processes, or all
 * processes in a given process group.
 *
 * The restrictions on setting capabilities are specified as:
 *
 * [pid is for the 'target' task.  'current' is the calling task.]
 *
 * I: any raised capabilities must be a subset of the (old current) permitted
 * P: any raised capabilities must be a subset of the (old current) permitted
 * E: must be set to a subset of (new target) permitted
 *
 * Returns 0 on success and < 0 on error.
 */
asmlinkage long sys_capset(cap_user_header_t header, const cap_user_data_t data)
{
	kernel_cap_t inheritable, permitted, effective;
	__u32 version;
	struct task_struct *target;
	int ret;
	pid_t pid;

	if (get_user(version, &header->version))
		return -EFAULT;

	if (version != _LINUX_CAPABILITY_VERSION) {
		if (put_user(_LINUX_CAPABILITY_VERSION, &header->version))
			return -EFAULT;
		return -EINVAL;
	}

	if (get_user(pid, &header->pid))
		return -EFAULT;

	if (pid && pid != task_pid_vnr(current) && !capable(CAP_SETPCAP))
		return -EPERM;

	if (copy_from_user(&effective, &data->effective, sizeof(effective)) ||
	    copy_from_user(&inheritable, &data->inheritable, sizeof(inheritable)) ||
	    copy_from_user(&permitted, &data->permitted, sizeof(permitted)))
		return -EFAULT;

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	if (pid > 0 && pid != task_pid_vnr(current)) {
		target = find_task_by_pid_ns(pid,
				current->nsproxy->pid_ns);
		if (!target) {
			ret = -ESRCH;
			goto out;
		}
	} else
		target = current;

	ret = 0;

	/* having verified that the proposed changes are legal,
	   we now put them into effect. */
	if (pid < 0) {
		if (pid == -1)	/* all procs other than current and init */
			ret = cap_set_all(&effective, &inheritable, &permitted);

		else		/* all procs in process group */
			ret = cap_set_pg(-pid, &effective, &inheritable,
					 &permitted);
	} else {
		ret = security_capset_check(target, &effective, &inheritable,
					    &permitted);
		if (!ret)
			security_capset_set(target, &effective, &inheritable,
					    &permitted);
	}

out:
	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	return ret;
}

int __capable(struct task_struct *t, int cap)
{
	if (security_capable(t, cap) == 0) {
		t->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}

int capable(int cap)
{
	return __capable(current, cap);
}
EXPORT_SYMBOL(capable);
