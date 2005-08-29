/*
 * fs/ioprio.c
 *
 * Copyright (C) 2004 Jens Axboe <axboe@suse.de>
 *
 * Helper functions for setting/querying io priorities of processes. The
 * system calls closely mimmick getpriority/setpriority, see the man page for
 * those. The prio argument is a composite of prio class and prio data, where
 * the data argument has meaning within that class. The standard scheduling
 * classes have 8 distinct prio levels, with 0 being the highest prio and 7
 * being the lowest.
 *
 * IOW, setting BE scheduling class with prio 2 is done ala:
 *
 * unsigned int prio = (IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT) | 2;
 *
 * ioprio_set(PRIO_PROCESS, pid, prio);
 *
 * See also Documentation/block/ioprio.txt
 *
 */
#include <linux/kernel.h>
#include <linux/ioprio.h>
#include <linux/blkdev.h>

static int set_task_ioprio(struct task_struct *task, int ioprio)
{
	struct io_context *ioc;

	if (task->uid != current->euid &&
	    task->uid != current->uid && !capable(CAP_SYS_NICE))
		return -EPERM;

	task_lock(task);

	task->ioprio = ioprio;

	ioc = task->io_context;
	if (ioc && ioc->set_ioprio)
		ioc->set_ioprio(ioc, ioprio);

	task_unlock(task);
	return 0;
}

asmlinkage long sys_ioprio_set(int which, int who, int ioprio)
{
	int class = IOPRIO_PRIO_CLASS(ioprio);
	int data = IOPRIO_PRIO_DATA(ioprio);
	struct task_struct *p, *g;
	struct user_struct *user;
	int ret;

	switch (class) {
		case IOPRIO_CLASS_RT:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			/* fall through, rt has prio field too */
		case IOPRIO_CLASS_BE:
			if (data >= IOPRIO_BE_NR || data < 0)
				return -EINVAL;

			break;
		case IOPRIO_CLASS_IDLE:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			break;
		default:
			return -EINVAL;
	}

	ret = -ESRCH;
	read_lock_irq(&tasklist_lock);
	switch (which) {
		case IOPRIO_WHO_PROCESS:
			if (!who)
				p = current;
			else
				p = find_task_by_pid(who);
			if (p)
				ret = set_task_ioprio(p, ioprio);
			break;
		case IOPRIO_WHO_PGRP:
			if (!who)
				who = process_group(current);
			do_each_task_pid(who, PIDTYPE_PGID, p) {
				ret = set_task_ioprio(p, ioprio);
				if (ret)
					break;
			} while_each_task_pid(who, PIDTYPE_PGID, p);
			break;
		case IOPRIO_WHO_USER:
			if (!who)
				user = current->user;
			else
				user = find_user(who);

			if (!user)
				break;

			do_each_thread(g, p) {
				if (p->uid != who)
					continue;
				ret = set_task_ioprio(p, ioprio);
				if (ret)
					break;
			} while_each_thread(g, p);

			if (who)
				free_uid(user);
			break;
		default:
			ret = -EINVAL;
	}

	read_unlock_irq(&tasklist_lock);
	return ret;
}

asmlinkage long sys_ioprio_get(int which, int who)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	int ret = -ESRCH;

	read_lock_irq(&tasklist_lock);
	switch (which) {
		case IOPRIO_WHO_PROCESS:
			if (!who)
				p = current;
			else
				p = find_task_by_pid(who);
			if (p)
				ret = p->ioprio;
			break;
		case IOPRIO_WHO_PGRP:
			if (!who)
				who = process_group(current);
			do_each_task_pid(who, PIDTYPE_PGID, p) {
				if (ret == -ESRCH)
					ret = p->ioprio;
				else
					ret = ioprio_best(ret, p->ioprio);
			} while_each_task_pid(who, PIDTYPE_PGID, p);
			break;
		case IOPRIO_WHO_USER:
			if (!who)
				user = current->user;
			else
				user = find_user(who);

			if (!user)
				break;

			do_each_thread(g, p) {
				if (p->uid != user->uid)
					continue;
				if (ret == -ESRCH)
					ret = p->ioprio;
				else
					ret = ioprio_best(ret, p->ioprio);
			} while_each_thread(g, p);

			if (who)
				free_uid(user);
			break;
		default:
			ret = -EINVAL;
	}

	read_unlock_irq(&tasklist_lock);
	return ret;
}

