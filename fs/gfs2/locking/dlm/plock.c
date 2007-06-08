/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/miscdevice.h>
#include <linux/lock_dlm_plock.h>
#include <linux/poll.h>

#include "lock_dlm.h"


static spinlock_t ops_lock;
static struct list_head send_list;
static struct list_head recv_list;
static wait_queue_head_t send_wq;
static wait_queue_head_t recv_wq;

struct plock_op {
	struct list_head list;
	int done;
	struct gdlm_plock_info info;
};

struct plock_xop {
	struct plock_op xop;
	void *callback;
	void *fl;
	void *file;
	struct file_lock flc;
};


static inline void set_version(struct gdlm_plock_info *info)
{
	info->version[0] = GDLM_PLOCK_VERSION_MAJOR;
	info->version[1] = GDLM_PLOCK_VERSION_MINOR;
	info->version[2] = GDLM_PLOCK_VERSION_PATCH;
}

static int check_version(struct gdlm_plock_info *info)
{
	if ((GDLM_PLOCK_VERSION_MAJOR != info->version[0]) ||
	    (GDLM_PLOCK_VERSION_MINOR < info->version[1])) {
		log_error("plock device version mismatch: "
			  "kernel (%u.%u.%u), user (%u.%u.%u)",
			  GDLM_PLOCK_VERSION_MAJOR,
			  GDLM_PLOCK_VERSION_MINOR,
			  GDLM_PLOCK_VERSION_PATCH,
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

int gdlm_plock(void *lockspace, struct lm_lockname *name,
	       struct file *file, int cmd, struct file_lock *fl)
{
	struct gdlm_ls *ls = lockspace;
	struct plock_op *op;
	struct plock_xop *xop;
	int rv;

	xop = kzalloc(sizeof(*xop), GFP_KERNEL);
	if (!xop)
		return -ENOMEM;

	op = &xop->xop;
	op->info.optype		= GDLM_PLOCK_OP_LOCK;
	op->info.pid		= fl->fl_pid;
	op->info.ex		= (fl->fl_type == F_WRLCK);
	op->info.wait		= IS_SETLKW(cmd);
	op->info.fsid		= ls->id;
	op->info.number		= name->ln_number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	op->info.owner		= (__u64)(long) fl->fl_owner;
	if (fl->fl_lmops && fl->fl_lmops->fl_grant) {
		xop->callback	= fl->fl_lmops->fl_grant;
		locks_init_lock(&xop->flc);
		locks_copy_lock(&xop->flc, fl);
		xop->fl		= fl;
		xop->file	= file;
	} else
		xop->callback	= NULL;

	send_op(op);

	if (xop->callback == NULL)
		wait_event(recv_wq, (op->done != 0));
	else
		return -EINPROGRESS;

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk(KERN_INFO "plock op on list\n");
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	rv = op->info.rv;

	if (!rv) {
		if (posix_lock_file_wait(file, fl) < 0)
			log_error("gdlm_plock: vfs lock error %x,%llx",
				  name->ln_type,
				  (unsigned long long)name->ln_number);
	}

	kfree(xop);
	return rv;
}

/* Returns failure iff a succesful lock operation should be canceled */
static int gdlm_plock_callback(struct plock_op *op)
{
	struct file *file;
	struct file_lock *fl;
	struct file_lock *flc;
	int (*notify)(void *, void *, int) = NULL;
	struct plock_xop *xop = (struct plock_xop *)op;
	int rv = 0;

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk(KERN_INFO "plock op on list\n");
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	/* check if the following 2 are still valid or make a copy */
	file = xop->file;
	flc = &xop->flc;
	fl = xop->fl;
	notify = xop->callback;

	if (op->info.rv) {
		notify(flc, NULL, op->info.rv);
		goto out;
	}

	/* got fs lock; bookkeep locally as well: */
	flc->fl_flags &= ~FL_SLEEP;
	if (posix_lock_file(file, flc, NULL)) {
		/*
		 * This can only happen in the case of kmalloc() failure.
		 * The filesystem's own lock is the authoritative lock,
		 * so a failure to get the lock locally is not a disaster.
		 * As long as GFS cannot reliably cancel locks (especially
		 * in a low-memory situation), we're better off ignoring
		 * this failure than trying to recover.
		 */
		log_error("gdlm_plock: vfs lock error file %p fl %p",
				file, fl);
	}

	rv = notify(flc, NULL, 0);
	if (rv) {
		/* XXX: We need to cancel the fs lock here: */
		printk("gfs2 lock granted after lock request failed;"
						" dangling lock!\n");
		goto out;
	}

out:
	kfree(xop);
	return rv;
}

int gdlm_punlock(void *lockspace, struct lm_lockname *name,
		 struct file *file, struct file_lock *fl)
{
	struct gdlm_ls *ls = lockspace;
	struct plock_op *op;
	int rv;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	if (posix_lock_file_wait(file, fl) < 0)
		log_error("gdlm_punlock: vfs unlock error %x,%llx",
			  name->ln_type, (unsigned long long)name->ln_number);

	op->info.optype		= GDLM_PLOCK_OP_UNLOCK;
	op->info.pid		= fl->fl_pid;
	op->info.fsid		= ls->id;
	op->info.number		= name->ln_number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	op->info.owner		= (__u64)(long) fl->fl_owner;

	send_op(op);
	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk(KERN_INFO "punlock op on list\n");
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	rv = op->info.rv;

	if (rv == -ENOENT)
		rv = 0;

	kfree(op);
	return rv;
}

int gdlm_plock_get(void *lockspace, struct lm_lockname *name,
		   struct file *file, struct file_lock *fl)
{
	struct gdlm_ls *ls = lockspace;
	struct plock_op *op;
	int rv;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	op->info.optype		= GDLM_PLOCK_OP_GET;
	op->info.pid		= fl->fl_pid;
	op->info.ex		= (fl->fl_type == F_WRLCK);
	op->info.fsid		= ls->id;
	op->info.number		= name->ln_number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;
	op->info.owner		= (__u64)(long) fl->fl_owner;

	send_op(op);
	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk(KERN_INFO "plock_get op on list\n");
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
		fl->fl_type = (op->info.ex) ? F_WRLCK : F_RDLCK;
		fl->fl_pid = op->info.pid;
		fl->fl_start = op->info.start;
		fl->fl_end = op->info.end;
		rv = 0;
	}

	kfree(op);
	return rv;
}

/* a read copies out one plock request from the send list */
static ssize_t dev_read(struct file *file, char __user *u, size_t count,
			loff_t *ppos)
{
	struct gdlm_plock_info info;
	struct plock_op *op = NULL;

	if (count < sizeof(info))
		return -EINVAL;

	spin_lock(&ops_lock);
	if (!list_empty(&send_list)) {
		op = list_entry(send_list.next, struct plock_op, list);
		list_move(&op->list, &recv_list);
		memcpy(&info, &op->info, sizeof(info));
	}
	spin_unlock(&ops_lock);

	if (!op)
		return -EAGAIN;

	if (copy_to_user(u, &info, sizeof(info)))
		return -EFAULT;
	return sizeof(info);
}

/* a write copies in one plock result that should match a plock_op
   on the recv list */
static ssize_t dev_write(struct file *file, const char __user *u, size_t count,
			 loff_t *ppos)
{
	struct gdlm_plock_info info;
	struct plock_op *op;
	int found = 0;

	if (count != sizeof(info))
		return -EINVAL;

	if (copy_from_user(&info, u, sizeof(info)))
		return -EFAULT;

	if (check_version(&info))
		return -EINVAL;

	spin_lock(&ops_lock);
	list_for_each_entry(op, &recv_list, list) {
		if (op->info.fsid == info.fsid && op->info.number == info.number &&
		    op->info.owner == info.owner) {
			list_del_init(&op->list);
			found = 1;
			op->done = 1;
			memcpy(&op->info, &info, sizeof(info));
			break;
		}
	}
	spin_unlock(&ops_lock);

	if (found) {
		struct plock_xop *xop;
		xop = (struct plock_xop *)op;
		if (xop->callback)
			count = gdlm_plock_callback(op);
		else
			wake_up(&recv_wq);
	} else
		printk(KERN_INFO "gdlm dev_write no op %x %llx\n", info.fsid,
			(unsigned long long)info.number);
	return count;
}

static unsigned int dev_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &send_wq, wait);

	spin_lock(&ops_lock);
	if (!list_empty(&send_list)) {
		spin_unlock(&ops_lock);
		return POLLIN | POLLRDNORM;
	}
	spin_unlock(&ops_lock);
	return 0;
}

static const struct file_operations dev_fops = {
	.read    = dev_read,
	.write   = dev_write,
	.poll    = dev_poll,
	.owner   = THIS_MODULE
};

static struct miscdevice plock_dev_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = GDLM_PLOCK_MISC_NAME,
	.fops = &dev_fops
};

int gdlm_plock_init(void)
{
	int rv;

	spin_lock_init(&ops_lock);
	INIT_LIST_HEAD(&send_list);
	INIT_LIST_HEAD(&recv_list);
	init_waitqueue_head(&send_wq);
	init_waitqueue_head(&recv_wq);

	rv = misc_register(&plock_dev_misc);
	if (rv)
		printk(KERN_INFO "gdlm_plock_init: misc_register failed %d",
		       rv);
	return rv;
}

void gdlm_plock_exit(void)
{
	if (misc_deregister(&plock_dev_misc) < 0)
		printk(KERN_INFO "gdlm_plock_exit: misc_deregister failed");
}

