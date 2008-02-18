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
 * Leveraged for setting/resetting capabilities
 */

const kernel_cap_t __cap_empty_set = CAP_EMPTY_SET;
const kernel_cap_t __cap_full_set = CAP_FULL_SET;
const kernel_cap_t __cap_init_eff_set = CAP_INIT_EFF_SET;

EXPORT_SYMBOL(__cap_empty_set);
EXPORT_SYMBOL(__cap_full_set);
EXPORT_SYMBOL(__cap_init_eff_set);

/*
 * More recent versions of libcap are available from:
 *
 *   http://www.kernel.org/pub/linux/libs/security/linux-privs/
 */

static void warn_legacy_capability_use(void)
{
	static int warned;
	if (!warned) {
		char name[sizeof(current->comm)];

		printk(KERN_INFO "warning: `%s' uses 32-bit capabilities"
		       " (legacy support in use)\n",
		       get_task_comm(name, current));
		warned = 1;
	}
}

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
	unsigned tocopy;
	kernel_cap_t pE, pI, pP;

	if (get_user(version, &header->version))
		return -EFAULT;

	switch (version) {
	case _LINUX_CAPABILITY_VERSION_1:
		warn_legacy_capability_use();
		tocopy = _LINUX_CAPABILITY_U32S_1;
		break;
	case _LINUX_CAPABILITY_VERSION_2:
		tocopy = _LINUX_CAPABILITY_U32S_2;
		break;
	default:
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
		target = find_task_by_vpid(pid);
		if (!target) {
			ret = -ESRCH;
			goto out;
		}
	} else
		target = current;

	ret = security_capget(target, &pE, &pI, &pP);

out:
	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	if (!ret) {
		struct __user_cap_data_struct kdata[_LINUX_CAPABILITY_U32S];
		unsigned i;

		for (i = 0; i < tocopy; i++) {
			kdata[i].effective = pE.cap[i];
			kdata[i].permitted = pP.cap[i];
			kdata[i].inheritable = pI.cap[i];
		}

		/*
		 * Note, in the case, tocopy < _LINUX_CAPABILITY_U32S,
		 * we silently drop the upper capabilities here. This
		 * has the effect of making older libcap
		 * implementations implicitly drop upper capability
		 * bits when they perform a: capget/modify/capset
		 * sequence.
		 *
		 * This behavior is considered fail-safe
		 * behavior. Upgrading the application to a newer
		 * version of libcap will enable access to the newer
		 * capabilities.
		 *
		 * An alternative would be to return an error here
		 * (-ERANGE), but that causes legacy applications to
		 * unexpectidly fail; the capget/modify/capset aborts
		 * before modification is attempted and the application
		 * fails.
		 */

		if (copy_to_user(dataptr, kdata, tocopy
				 * sizeof(struct __user_cap_data_struct))) {
			return -EFAULT;
		}
	}

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

	pgrp = find_vpid(pgrp_nr);
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
	struct __user_cap_data_struct kdata[_LINUX_CAPABILITY_U32S];
	unsigned i, tocopy;
	kernel_cap_t inheritable, permitted, effective;
	__u32 version;
	struct task_struct *target;
	int ret;
	pid_t pid;

	if (get_user(version, &header->version))
		return -EFAULT;

	switch (version) {
	case _LINUX_CAPABILITY_VERSION_1:
		warn_legacy_capability_use();
		tocopy = _LINUX_CAPABILITY_U32S_1;
		break;
	case _LINUX_CAPABILITY_VERSION_2:
		tocopy = _LINUX_CAPABILITY_U32S_2;
		break;
	default:
		if (put_user(_LINUX_CAPABILITY_VERSION, &header->version))
			return -EFAULT;
		return -EINVAL;
	}

	if (get_user(pid, &header->pid))
		return -EFAULT;

	if (pid && pid != task_pid_vnr(current) && !capable(CAP_SETPCAP))
		return -EPERM;

	if (copy_from_user(&kdata, data, tocopy
			   * sizeof(struct __user_cap_data_struct))) {
		return -EFAULT;
	}

	for (i = 0; i < tocopy; i++) {
		effective.cap[i] = kdata[i].effective;
		permitted.cap[i] = kdata[i].permitted;
		inheritable.cap[i] = kdata[i].inheritable;
	}
	while (i < _LINUX_CAPABILITY_U32S) {
		effective.cap[i] = 0;
		permitted.cap[i] = 0;
		inheritable.cap[i] = 0;
		i++;
	}

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	if (pid > 0 && pid != task_pid_vnr(current)) {
		target = find_task_by_vpid(pid);
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
