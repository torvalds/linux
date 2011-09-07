/*
 * Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/dlm.h>
#include <linux/dlm_plock.h>
#include <linux/slab.h>

#include "dlm_internal.h"
#include "lockspace.h"

static spinlock_t ops_lock;
static struct list_head send_list;
static struct list_head recv_list;
static wait_queue_head_t send_wq;
static wait_queue_head_t recv_wq;

struct plock_op {
	struct list_head list;
	int done;
	struct dlm_plock_info info;
};

struct plock_xop {
	struct plock_op xop;
	void *callback;
	void *fl;
	void *file;
	struct file_lock flc;
};


static inline void set_version(struct dlm_plock_info *info)
{
	info->version[0] = DLM_PLOCK_VERSION_MAJOR;
	info->version[1] = DLM_PLOCK_VERSION_MINOR;
	info->version[2] = DLM_PLOCK_VERSION_PATCH;
}

static int check_version(struct dlm_plock_info *info)
{
	if ((DLM_PLOCK_VERSION_MAJOR != info->version[0]) ||
	    (DLM_PLOCK_VERSION_MINOR < info->version[1])) {
		log_print("plock device version mismatch: "
			  "kernel (%u.%u.%u), user (%u.%u.%u)",
			  DLM_PLOCK_VERSION_MAJOR,
			  DLM_PLOCK_VERSION_MINOR,
			  DLM_PLOCK_VERSION_PATCH,
			  info->version[0],
			  info->version[1],
			  info->version[2]);
		return -EINVAL;
	}
	return 0;
}

static void send_op(struct plock_op *op)
{
	set_version(&op->info);
	INIT_LIST_HEAD(&op->list);
	spin_lock(&ops_lock);
	list_add_tail(&op->list, &send_list);
	spin_unlock(&ops_lock);
	wake_up(&send_wq);
}

/* If a process was killed while waiting for the only plock on a file,
   locks_remove_posix will not see any lock on the file so it won't
   send an unlock-close to us to pass on to userspace to clean up the
   abandoned waiter.  So, we have to insert the unlock-close when the
   lock call is interrupted. */

static void do_unlock_close(struct dlm_ls *ls, u64 number,
			    struct file *file, struct file_lock *fl)
{
	struct plock_op *op;

	op = kzalloc(sizeof(*op), GFP_NOFS);
	if (!op)
		return;

	op->info.optype		= DLM_PLOCK_OP_UNLOCK;
	op->info.pid		= fl->fl_pid;
	op->info.fsid		= ls->ls_global_id;
	op->info.number		= number;
	op->info.start		= 0;
	op->info.end		= OFFSET_MAX;
	if (fl->fl_lmops && fl->fl_lmops->lm_grant)
		op->info.owner	= (__u64) fl->fl_pid;
	else
		op->info.owner	= (__u64)(long) fl->fl_owner;

	op->info.flags |= DLM_PLOCK_FL_CLOSE;
	send_op(op);
}

int dlm_posix_lock(dlm_lockspace_t *lockspace, u64 number, struct file *file,
		   int cmd, struct file_lock *fl)
{
	struct dlm_ls *ls;
	struct plock_op *op;
	struct plock_xop *xop;
	int rv;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	xop = kzalloc(sizeof(*xop), GFP_NOFS);
	if (!xop) {
		rv = -ENOMEM;
		goto out;
	}

	op = &xop->xop;
	op->info.optype		= DLM_PLOCK_OP_LOCK;
	op->info.pid		= fl->fl_pid;
	op->info.ex		= (fl->fl_type == F_WRLCK);
	op->info.wait		= IS_SETLKW(cmd);
	op->info.fsid		= ls->ls_global_id;
	op->info.number		= number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	if (fl->fl_lmops && fl->fl_lmops->lm_grant) {
		/* fl_owner is lockd which doesn't distinguish
		   processes on the nfs client */
		op->info.owner	= (__u64) fl->fl_pid;
		xop->callback	= fl->fl_lmops->lm_grant;
		locks_init_lock(&xop->flc);
		locks_copy_lock(&xop->flc, fl);
		xop->fl		= fl;
		xop->file	= file;
	} else {
		op->info.owner	= (__u64)(long) fl->fl_owner;
		xop->callback	= NULL;
	}

	send_op(op);

	if (xop->callback == NULL) {
		rv = wait_event_killable(recv_wq, (op->done != 0));
		if (rv == -ERESTARTSYS) {
			log_debug(ls, "dlm_posix_lock: wait killed %llx",
				  (unsigned long long)number);
			spin_lock(&ops_lock);
			list_del(&op->list);
			spin_unlock(&ops_lock);
			kfree(xop);
			do_unlock_close(ls, number, file, fl);
			goto out;
		}
	} else {
		rv = FILE_LOCK_DEFERRED;
		goto out;
	}

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		log_error(ls, "dlm_posix_lock: op on list %llx",
			  (unsigned long long)number);
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	rv = op->info.rv;

	if (!rv) {
		if (posix_lock_file_wait(file, fl) < 0)
			log_error(ls, "dlm_posix_lock: vfs lock error %llx",
				  (unsigned long long)number);
	}

	kfree(xop);
out:
	dlm_put_lockspace(ls);
	return rv;
}
EXPORT_SYMBOL_GPL(dlm_posix_lock);

/* Returns failure iff a successful lock operation should be canceled */
static int dlm_plock_callback(struct plock_op *op)
{
	struct file *file;
	struct file_lock *fl;
	struct file_lock *flc;
	int (*notify)(void *, void *, int) = NULL;
	struct plock_xop *xop = (struct plock_xop *)op;
	int rv = 0;

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		log_print("dlm_plock_callback: op on list %llx",
			  (unsigned long long)op->info.number);
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	/* check if the following 2 are still valid or make a copy */
	file = xop->file;
	flc = &xop->flc;
	fl = xop->fl;
	notify = xop->callback;

	if (op->info.rv) {
		notify(fl, NULL, op->info.rv);
		goto out;
	}

	/* got fs lock; bookkeep locally as well: */
	flc->fl_flags &= ~FL_SLEEP;
	if (posix_lock_file(file, flc, NULL)) {
		/*
		 * This can only happen in the case of kmalloc() failure.
		 * The filesystem's own lock is the authoritative lock,
		 * so a failure to get the lock locally is not a disaster.
		 * As long as the fs cannot reliably cancel locks (especially
		 * in a low-memory situation), we're better off ignoring
		 * this failure than trying to recover.
		 */
		log_print("dlm_plock_callback: vfs lock error %llx file %p fl %p",
			  (unsigned long long)op->info.number, file, fl);
	}

	rv = notify(fl, NULL, 0);
	if (rv) {
		/* XXX: We need to cancel the fs lock here: */
		log_print("dlm_plock_callback: lock granted after lock request "
			  "failed; dangling lock!\n");
		goto out;
	}

out:
	kfree(xop);
	return rv;
}

int dlm_posix_unlock(dlm_lockspace_t *lockspace, u64 number, struct file *file,
		     struct file_lock *fl)
{
	struct dlm_ls *ls;
	struct plock_op *op;
	int rv;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	op = kzalloc(sizeof(*op), GFP_NOFS);
	if (!op) {
		rv = -ENOMEM;
		goto out;
	}

	if (posix_lock_file_wait(file, fl) < 0)
		log_error(ls, "dlm_posix_unlock: vfs unlock error %llx",
			  (unsigned long long)number);

	op->info.optype		= DLM_PLOCK_OP_UNLOCK;
	op->info.pid		= fl->fl_pid;
	op->info.fsid		= ls->ls_global_id;
	op->info.number		= number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	if (fl->fl_lmops && fl->fl_lmops->lm_grant)
		op->info.owner	= (__u64) fl->fl_pid;
	else
		op->info.owner	= (__u64)(long) fl->fl_owner;

	if (fl->fl_flags & FL_CLOSE) {
		op->info.flags |= DLM_PLOCK_FL_CLOSE;
		send_op(op);
		rv = 0;
		goto out;
	}

	send_op(op);
	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		log_error(ls, "dlm_posix_unlock: op on list %llx",
			  (unsigned long long)number);
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	rv = op->info.rv;

	if (rv == -ENOENT)
		rv = 0;

	kfree(op);
out:
	dlm_put_lockspace(ls);
	return rv;
}
EXPORT_SYMBOL_GPL(dlm_posix_unlock);

int dlm_posix_get(dlm_lockspace_t *lockspace, u64 number, struct file *file,
		  struct file_lock *fl)
{
	struct dlm_ls *ls;
	struct plock_op *op;
	int rv;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	op = kzalloc(sizeof(*op), GFP_NOFS);
	if (!op) {
		rv = -ENOMEM;
		goto out;
	}

	op->info.optype		= DLM_PLOCK_OP_GET;
	op->info.pid		= fl->fl_pid;
	op->info.ex		= (fl->fl_type == F_WRLCK);
	op->info.fsid		= ls->ls_global_id;
	op->info.number		= number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	if (fl->fl_lmops && fl->fl_lmops->lm_grant)
		op->info.owner	= (__u64) fl->fl_pid;
	else
		op->info.owner	= (__u64)(long) fl->fl_owner;

	send_op(op);
	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		log_error(ls, "dlm_posix_get: op on list %llx",
			  (unsigned long long)number);
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	/* info.rv from userspace is 1 for conflict, 0 for no-conflict,
	   -ENOENT if there are no locks on the file */

	rv = op->info.rv;

	fl->fl_type = F_UNLCK;
	if (rv == -ENOENT)
		rv = 0;
	else if (rv > 0) {
		locks_init_lock(fl);
		fl->fl_type = (op->info.ex) ? F_WRLCK : F_RDLCK;
		fl->fl_flags = FL_POSIX;
		fl->fl_pid = op->info.pid;
		fl->fl_start = op->info.start;
		fl->fl_end = op->info.end;
		rv = 0;
	}

	kfree(op);
out:
	dlm_put_lockspace(ls);
	return rv;
}
EXPORT_SYMBOL_GPL(dlm_posix_get);

/* a read copies out one plock request from the send list */
static ssize_t dev_read(struct file *file, char __user *u, size_t count,
			loff_t *ppos)
{
	struct dlm_plock_info info;
	struct plock_op *op = NULL;

	if (count < sizeof(info))
		return -EINVAL;

	spin_lock(&ops_lock);
	if (!list_empty(&send_list)) {
		op = list_entry(send_list.next, struct plock_op, list);
		if (op->info.flags & DLM_PLOCK_FL_CLOSE)
			list_del(&op->list);
		else
			list_move(&op->list, &recv_list);
		memcpy(&info, &op->info, sizeof(info));
	}
	spin_unlock(&ops_lock);

	if (!op)
		return -EAGAIN;

	/* there is no need to get a reply from userspace for unlocks
	   that were generated by the vfs cleaning up for a close
	   (the process did not make an unlock call). */

	if (op->info.flags & DLM_PLOCK_FL_CLOSE)
		kfree(op);

	if (copy_to_user(u, &info, sizeof(info)))
		return -EFAULT;
	return sizeof(info);
}

/* a write copies in one plock result that should match a plock_op
   on the recv list */
static ssize_t dev_write(struct file *file, const char __user *u, size_t count,
			 loff_t *ppos)
{
	struct dlm_plock_info info;
	struct plock_op *op;
	int found = 0, do_callback = 0;

	if (count != sizeof(info))
		return -EINVAL;

	if (copy_from_user(&info, u, sizeof(info)))
		return -EFAULT;

	if (check_version(&info))
		return -EINVAL;

	spin_lock(&ops_lock);
	list_for_each_entry(op, &recv_list, list) {
		if (op->info.fsid == info.fsid &&
		    op->info.number == info.number &&
		    op->info.owner == info.owner) {
			struct plock_xop *xop = (struct plock_xop *)op;
			list_del_init(&op->list);
			memcpy(&op->info, &info, sizeof(info));
			if (xop->callback)
				do_callback = 1;
			else
				op->done = 1;
			found = 1;
			break;
		}
	}
	spin_unlock(&ops_lock);

	if (found) {
		if (do_callback)
			dlm_plock_callback(op);
		else
			wake_up(&recv_wq);
	} else
		log_print("dev_write no op %x %llx", info.fsid,
			  (unsigned long long)info.number);
	return count;
}

static unsigned int dev_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &send_wq, wait);

	spin_lock(&ops_lock);
	if (!list_empty(&send_list))
		mask = POLLIN | POLLRDNORM;
	spin_unlock(&ops_lock);

	return mask;
}

static const struct file_operations dev_fops = {
	.read    = dev_read,
	.write   = dev_write,
	.poll    = dev_poll,
	.owner   = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice plock_dev_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DLM_PLOCK_MISC_NAME,
	.fops = &dev_fops
};

int dlm_plock_init(void)
{
	int rv;

	spin_lock_init(&ops_lock);
	INIT_LIST_HEAD(&send_list);
	INIT_LIST_HEAD(&recv_list);
	init_waitqueue_head(&send_wq);
	init_waitqueue_head(&recv_wq);

	rv = misc_register(&plock_dev_misc);
	if (rv)
		log_print("dlm_plock_init: misc_register failed %d", rv);
	return rv;
}

void dlm_plock_exit(void)
{
	if (misc_deregister(&plock_dev_misc) < 0)
		log_print("dlm_plock_exit: misc_deregister failed");
}

