/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "lock_dlm.h"

int gdlm_drop_count;
int gdlm_drop_period;
struct lm_lockops gdlm_ops;


static struct gdlm_ls *init_gdlm(lm_callback_t cb, lm_fsdata_t *fsdata,
				 int flags, char *table_name)
{
	struct gdlm_ls *ls;
	char buf[256], *p;

	ls = kmalloc(sizeof(struct gdlm_ls), GFP_KERNEL);
	if (!ls)
		return NULL;

	memset(ls, 0, sizeof(struct gdlm_ls));

	ls->drop_locks_count = gdlm_drop_count;
	ls->drop_locks_period = gdlm_drop_period;

	ls->fscb = cb;
	ls->fsdata = fsdata;
	ls->fsflags = flags;

	spin_lock_init(&ls->async_lock);

	INIT_LIST_HEAD(&ls->complete);
	INIT_LIST_HEAD(&ls->blocking);
	INIT_LIST_HEAD(&ls->delayed);
	INIT_LIST_HEAD(&ls->submit);
	INIT_LIST_HEAD(&ls->all_locks);

	init_waitqueue_head(&ls->thread_wait);
	init_waitqueue_head(&ls->wait_control);
	ls->thread1 = NULL;
	ls->thread2 = NULL;
	ls->drop_time = jiffies;
	ls->jid = -1;

	strncpy(buf, table_name, 256);
	buf[255] = '\0';

	p = strstr(buf, ":");
	if (!p) {
		printk("lock_dlm: invalid table_name \"%s\"\n", table_name);
		kfree(ls);
		return NULL;
	}
	*p = '\0';
	p++;

	strncpy(ls->clustername, buf, 128);
	strncpy(ls->fsname, p, 128);

	return ls;
}

static int gdlm_mount(char *table_name, char *host_data,
			lm_callback_t cb, lm_fsdata_t *fsdata,
			unsigned int min_lvb_size, int flags,
			struct lm_lockstruct *lockstruct)
{
	struct gdlm_ls *ls;
	int error = -ENOMEM;

	if (min_lvb_size > GDLM_LVB_SIZE)
		goto out;

	ls = init_gdlm(cb, fsdata, flags, table_name);
	if (!ls)
		goto out;

	error = gdlm_init_threads(ls);
	if (error)
		goto out_free;

	error = dlm_new_lockspace(ls->fsname, strlen(ls->fsname),
				  &ls->dlm_lockspace, 0, GDLM_LVB_SIZE);
	if (error) {
		printk("lock_dlm: dlm_new_lockspace error %d\n", error);
		goto out_thread;
	}

	error = gdlm_kobject_setup(ls);
	if (error)
		goto out_dlm;
	kobject_uevent(&ls->kobj, KOBJ_MOUNT, NULL);

	/* Now we depend on userspace to notice the new mount,
	   join the appropriate group, and do a write to our sysfs
	   "mounted" or "terminate" file.  Before the start, userspace
	   must set "jid" and "first". */

	error = wait_event_interruptible(ls->wait_control,
			test_bit(DFL_JOIN_DONE, &ls->flags));
	if (error)
		goto out_sysfs;

	if (test_bit(DFL_TERMINATE, &ls->flags)) {
		error = -ERESTARTSYS;
		goto out_sysfs;
	}

	lockstruct->ls_jid = ls->jid;
	lockstruct->ls_first = ls->first;
	lockstruct->ls_lockspace = ls;
	lockstruct->ls_ops = &gdlm_ops;
	lockstruct->ls_flags = 0;
	lockstruct->ls_lvb_size = GDLM_LVB_SIZE;
	return 0;

 out_sysfs:
	gdlm_kobject_release(ls);
 out_dlm:
	dlm_release_lockspace(ls->dlm_lockspace, 2);
 out_thread:
	gdlm_release_threads(ls);
 out_free:
	kfree(ls);
 out:
	return error;
}

static void gdlm_unmount(lm_lockspace_t *lockspace)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;
	int rv;

	log_debug("unmount flags %lx", ls->flags);

	if (test_bit(DFL_WITHDRAW, &ls->flags)) {
		gdlm_kobject_release(ls);
		goto out;
	}

	kobject_uevent(&ls->kobj, KOBJ_UMOUNT, NULL);

	wait_event_interruptible(ls->wait_control,
				 test_bit(DFL_LEAVE_DONE, &ls->flags));

	gdlm_kobject_release(ls);
	dlm_release_lockspace(ls->dlm_lockspace, 2);
	gdlm_release_threads(ls);
	rv = gdlm_release_all_locks(ls);
	if (rv)
		log_all("lm_dlm_unmount: %d stray locks freed", rv);
 out:
	kfree(ls);
}

static void gdlm_recovery_done(lm_lockspace_t *lockspace, unsigned int jid,
                               unsigned int message)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;
	ls->recover_done = jid;
	kobject_uevent(&ls->kobj, KOBJ_CHANGE, NULL);
}

static void gdlm_others_may_mount(lm_lockspace_t *lockspace)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;
	ls->first_done = 1;
	kobject_uevent(&ls->kobj, KOBJ_CHANGE, NULL);
}

static void gdlm_withdraw(lm_lockspace_t *lockspace)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;

	/* userspace suspends locking on all other members */

	kobject_uevent(&ls->kobj, KOBJ_OFFLINE, NULL);

	wait_event_interruptible(ls->wait_control,
				 test_bit(DFL_WITHDRAW, &ls->flags));

	dlm_release_lockspace(ls->dlm_lockspace, 2);
	gdlm_release_threads(ls);
	gdlm_release_all_locks(ls);

	kobject_uevent(&ls->kobj, KOBJ_UMOUNT, NULL);

	/* userspace leaves the mount group, we don't need to wait for
	   that to complete */
}

int gdlm_plock_get(lm_lockspace_t *lockspace, struct lm_lockname *name,
		   struct file *file, struct file_lock *fl)
{
	return -ENOSYS;
}

int gdlm_punlock(lm_lockspace_t *lockspace, struct lm_lockname *name,
		   struct file *file, struct file_lock *fl)
{
	return -ENOSYS;
}

int gdlm_plock(lm_lockspace_t *lockspace, struct lm_lockname *name,
	       struct file *file, int cmd, struct file_lock *fl)
{
	return -ENOSYS;
}

struct lm_lockops gdlm_ops = {
	lm_proto_name:"lock_dlm",
	lm_mount:gdlm_mount,
	lm_others_may_mount:gdlm_others_may_mount,
	lm_unmount:gdlm_unmount,
	lm_withdraw:gdlm_withdraw,
	lm_get_lock:gdlm_get_lock,
	lm_put_lock:gdlm_put_lock,
	lm_lock:gdlm_lock,
	lm_unlock:gdlm_unlock,
	lm_plock:gdlm_plock,
	lm_punlock:gdlm_punlock,
	lm_plock_get:gdlm_plock_get,
	lm_cancel:gdlm_cancel,
	lm_hold_lvb:gdlm_hold_lvb,
	lm_unhold_lvb:gdlm_unhold_lvb,
	lm_sync_lvb:gdlm_sync_lvb,
	lm_recovery_done:gdlm_recovery_done,
	lm_owner:THIS_MODULE,
};

