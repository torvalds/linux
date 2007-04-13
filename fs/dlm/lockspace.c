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

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "ast.h"
#include "dir.h"
#include "lowcomms.h"
#include "config.h"
#include "memory.h"
#include "lock.h"
#include "recover.h"
#include "requestqueue.h"

#ifdef CONFIG_DLM_DEBUG
int dlm_create_debug_file(struct dlm_ls *ls);
void dlm_delete_debug_file(struct dlm_ls *ls);
#else
static inline int dlm_create_debug_file(struct dlm_ls *ls) { return 0; }
static inline void dlm_delete_debug_file(struct dlm_ls *ls) { }
#endif

static int			ls_count;
static struct mutex		ls_lock;
static struct list_head		lslist;
static spinlock_t		lslist_lock;
static struct task_struct *	scand_task;


static ssize_t dlm_control_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int n = simple_strtol(buf, NULL, 0);

	ls = dlm_find_lockspace_local(ls->ls_local_handle);
	if (!ls)
		return -EINVAL;

	switch (n) {
	case 0:
		dlm_ls_stop(ls);
		break;
	case 1:
		dlm_ls_start(ls);
		break;
	default:
		ret = -EINVAL;
	}
	dlm_put_lockspace(ls);
	return ret;
}

static ssize_t dlm_event_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ls->ls_uevent_result = simple_strtol(buf, NULL, 0);
	set_bit(LSFL_UEVENT_WAIT, &ls->ls_flags);
	wake_up(&ls->ls_uevent_wait);
	return len;
}

static ssize_t dlm_id_show(struct dlm_ls *ls, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", ls->ls_global_id);
}

static ssize_t dlm_id_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ls->ls_global_id = simple_strtoul(buf, NULL, 0);
	return len;
}

static ssize_t dlm_recover_status_show(struct dlm_ls *ls, char *buf)
{
	uint32_t status = dlm_recover_status(ls);
	return snprintf(buf, PAGE_SIZE, "%x\n", status);
}

static ssize_t dlm_recover_nodeid_show(struct dlm_ls *ls, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ls->ls_recover_nodeid);
}

struct dlm_attr {
	struct attribute attr;
	ssize_t (*show)(struct dlm_ls *, char *);
	ssize_t (*store)(struct dlm_ls *, const char *, size_t);
};

static struct dlm_attr dlm_attr_control = {
	.attr  = {.name = "control", .mode = S_IWUSR},
	.store = dlm_control_store
};

static struct dlm_attr dlm_attr_event = {
	.attr  = {.name = "event_done", .mode = S_IWUSR},
	.store = dlm_event_store
};

static struct dlm_attr dlm_attr_id = {
	.attr  = {.name = "id", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_id_show,
	.store = dlm_id_store
};

static struct dlm_attr dlm_attr_recover_status = {
	.attr  = {.name = "recover_status", .mode = S_IRUGO},
	.show  = dlm_recover_status_show
};

static struct dlm_attr dlm_attr_recover_nodeid = {
	.attr  = {.name = "recover_nodeid", .mode = S_IRUGO},
	.show  = dlm_recover_nodeid_show
};

static struct attribute *dlm_attrs[] = {
	&dlm_attr_control.attr,
	&dlm_attr_event.attr,
	&dlm_attr_id.attr,
	&dlm_attr_recover_status.attr,
	&dlm_attr_recover_nodeid.attr,
	NULL,
};

static ssize_t dlm_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct dlm_ls *ls  = container_of(kobj, struct dlm_ls, ls_kobj);
	struct dlm_attr *a = container_of(attr, struct dlm_attr, attr);
	return a->show ? a->show(ls, buf) : 0;
}

static ssize_t dlm_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t len)
{
	struct dlm_ls *ls  = container_of(kobj, struct dlm_ls, ls_kobj);
	struct dlm_attr *a = container_of(attr, struct dlm_attr, attr);
	return a->store ? a->store(ls, buf, len) : len;
}

static void lockspace_kobj_release(struct kobject *k)
{
	struct dlm_ls *ls  = container_of(k, struct dlm_ls, ls_kobj);
	kfree(ls);
}

static struct sysfs_ops dlm_attr_ops = {
	.show  = dlm_attr_show,
	.store = dlm_attr_store,
};

static struct kobj_type dlm_ktype = {
	.default_attrs = dlm_attrs,
	.sysfs_ops     = &dlm_attr_ops,
	.release       = lockspace_kobj_release,
};

static struct kset dlm_kset = {
	.kobj   = {.name = "dlm",},
	.ktype  = &dlm_ktype,
};

static int kobject_setup(struct dlm_ls *ls)
{
	char lsname[DLM_LOCKSPACE_LEN];
	int error;

	memset(lsname, 0, DLM_LOCKSPACE_LEN);
	snprintf(lsname, DLM_LOCKSPACE_LEN, "%s", ls->ls_name);

	error = kobject_set_name(&ls->ls_kobj, "%s", lsname);
	if (error)
		return error;

	ls->ls_kobj.kset = &dlm_kset;
	ls->ls_kobj.ktype = &dlm_ktype;
	return 0;
}

static int do_uevent(struct dlm_ls *ls, int in)
{
	int error;

	if (in)
		kobject_uevent(&ls->ls_kobj, KOBJ_ONLINE);
	else
		kobject_uevent(&ls->ls_kobj, KOBJ_OFFLINE);

	error = wait_event_interruptible(ls->ls_uevent_wait,
			test_and_clear_bit(LSFL_UEVENT_WAIT, &ls->ls_flags));
	if (error)
		goto out;

	error = ls->ls_uevent_result;
 out:
	return error;
}


int dlm_lockspace_init(void)
{
	int error;

	ls_count = 0;
	mutex_init(&ls_lock);
	INIT_LIST_HEAD(&lslist);
	spin_lock_init(&lslist_lock);

	kobj_set_kset_s(&dlm_kset, kernel_subsys);
	error = kset_register(&dlm_kset);
	if (error)
		printk("dlm_lockspace_init: cannot register kset %d\n", error);
	return error;
}

void dlm_lockspace_exit(void)
{
	kset_unregister(&dlm_kset);
}

static int dlm_scand(void *data)
{
	struct dlm_ls *ls;

	while (!kthread_should_stop()) {
		list_for_each_entry(ls, &lslist, ls_list)
			dlm_scan_rsbs(ls);
		schedule_timeout_interruptible(dlm_config.ci_scan_secs * HZ);
	}
	return 0;
}

static int dlm_scand_start(void)
{
	struct task_struct *p;
	int error = 0;

	p = kthread_run(dlm_scand, NULL, "dlm_scand");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
		scand_task = p;
	return error;
}

static void dlm_scand_stop(void)
{
	kthread_stop(scand_task);
}

static struct dlm_ls *dlm_find_lockspace_name(char *name, int namelen)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_namelen == namelen &&
		    memcmp(ls->ls_name, name, namelen) == 0)
			goto out;
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

struct dlm_ls *dlm_find_lockspace_global(uint32_t id)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_global_id == id) {
			ls->ls_count++;
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

struct dlm_ls *dlm_find_lockspace_local(dlm_lockspace_t *lockspace)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);
	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_local_handle == lockspace) {
			ls->ls_count++;
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

struct dlm_ls *dlm_find_lockspace_device(int minor)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);
	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_device.minor == minor) {
			ls->ls_count++;
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

void dlm_put_lockspace(struct dlm_ls *ls)
{
	spin_lock(&lslist_lock);
	ls->ls_count--;
	spin_unlock(&lslist_lock);
}

static void remove_lockspace(struct dlm_ls *ls)
{
	for (;;) {
		spin_lock(&lslist_lock);
		if (ls->ls_count == 0) {
			list_del(&ls->ls_list);
			spin_unlock(&lslist_lock);
			return;
		}
		spin_unlock(&lslist_lock);
		ssleep(1);
	}
}

static int threads_start(void)
{
	int error;

	/* Thread which process lock requests for all lockspace's */
	error = dlm_astd_start();
	if (error) {
		log_print("cannot start dlm_astd thread %d", error);
		goto fail;
	}

	error = dlm_scand_start();
	if (error) {
		log_print("cannot start dlm_scand thread %d", error);
		goto astd_fail;
	}

	/* Thread for sending/receiving messages for all lockspace's */
	error = dlm_lowcomms_start();
	if (error) {
		log_print("cannot start dlm lowcomms %d", error);
		goto scand_fail;
	}

	return 0;

 scand_fail:
	dlm_scand_stop();
 astd_fail:
	dlm_astd_stop();
 fail:
	return error;
}

static void threads_stop(void)
{
	dlm_scand_stop();
	dlm_lowcomms_stop();
	dlm_astd_stop();
}

static int new_lockspace(char *name, int namelen, void **lockspace,
			 uint32_t flags, int lvblen)
{
	struct dlm_ls *ls;
	int i, size, error = -ENOMEM;

	if (namelen > DLM_LOCKSPACE_LEN)
		return -EINVAL;

	if (!lvblen || (lvblen % 8))
		return -EINVAL;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	ls = dlm_find_lockspace_name(name, namelen);
	if (ls) {
		*lockspace = ls;
		module_put(THIS_MODULE);
		return -EEXIST;
	}

	ls = kzalloc(sizeof(struct dlm_ls) + namelen, GFP_KERNEL);
	if (!ls)
		goto out;
	memcpy(ls->ls_name, name, namelen);
	ls->ls_namelen = namelen;
	ls->ls_exflags = flags;
	ls->ls_lvblen = lvblen;
	ls->ls_count = 0;
	ls->ls_flags = 0;

	size = dlm_config.ci_rsbtbl_size;
	ls->ls_rsbtbl_size = size;

	ls->ls_rsbtbl = kmalloc(sizeof(struct dlm_rsbtable) * size, GFP_KERNEL);
	if (!ls->ls_rsbtbl)
		goto out_lsfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_rsbtbl[i].list);
		INIT_LIST_HEAD(&ls->ls_rsbtbl[i].toss);
		rwlock_init(&ls->ls_rsbtbl[i].lock);
	}

	size = dlm_config.ci_lkbtbl_size;
	ls->ls_lkbtbl_size = size;

	ls->ls_lkbtbl = kmalloc(sizeof(struct dlm_lkbtable) * size, GFP_KERNEL);
	if (!ls->ls_lkbtbl)
		goto out_rsbfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_lkbtbl[i].list);
		rwlock_init(&ls->ls_lkbtbl[i].lock);
		ls->ls_lkbtbl[i].counter = 1;
	}

	size = dlm_config.ci_dirtbl_size;
	ls->ls_dirtbl_size = size;

	ls->ls_dirtbl = kmalloc(sizeof(struct dlm_dirtable) * size, GFP_KERNEL);
	if (!ls->ls_dirtbl)
		goto out_lkbfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_dirtbl[i].list);
		rwlock_init(&ls->ls_dirtbl[i].lock);
	}

	INIT_LIST_HEAD(&ls->ls_waiters);
	mutex_init(&ls->ls_waiters_mutex);

	INIT_LIST_HEAD(&ls->ls_nodes);
	INIT_LIST_HEAD(&ls->ls_nodes_gone);
	ls->ls_num_nodes = 0;
	ls->ls_low_nodeid = 0;
	ls->ls_total_weight = 0;
	ls->ls_node_array = NULL;

	memset(&ls->ls_stub_rsb, 0, sizeof(struct dlm_rsb));
	ls->ls_stub_rsb.res_ls = ls;

	ls->ls_debug_rsb_dentry = NULL;
	ls->ls_debug_waiters_dentry = NULL;

	init_waitqueue_head(&ls->ls_uevent_wait);
	ls->ls_uevent_result = 0;

	ls->ls_recoverd_task = NULL;
	mutex_init(&ls->ls_recoverd_active);
	spin_lock_init(&ls->ls_recover_lock);
	spin_lock_init(&ls->ls_rcom_spin);
	get_random_bytes(&ls->ls_rcom_seq, sizeof(uint64_t));
	ls->ls_recover_status = 0;
	ls->ls_recover_seq = 0;
	ls->ls_recover_args = NULL;
	init_rwsem(&ls->ls_in_recovery);
	INIT_LIST_HEAD(&ls->ls_requestqueue);
	mutex_init(&ls->ls_requestqueue_mutex);
	mutex_init(&ls->ls_clear_proc_locks);

	ls->ls_recover_buf = kmalloc(dlm_config.ci_buffer_size, GFP_KERNEL);
	if (!ls->ls_recover_buf)
		goto out_dirfree;

	INIT_LIST_HEAD(&ls->ls_recover_list);
	spin_lock_init(&ls->ls_recover_list_lock);
	ls->ls_recover_list_count = 0;
	ls->ls_local_handle = ls;
	init_waitqueue_head(&ls->ls_wait_general);
	INIT_LIST_HEAD(&ls->ls_root_list);
	init_rwsem(&ls->ls_root_sem);

	down_write(&ls->ls_in_recovery);

	spin_lock(&lslist_lock);
	list_add(&ls->ls_list, &lslist);
	spin_unlock(&lslist_lock);

	/* needs to find ls in lslist */
	error = dlm_recoverd_start(ls);
	if (error) {
		log_error(ls, "can't start dlm_recoverd %d", error);
		goto out_rcomfree;
	}

	dlm_create_debug_file(ls);

	error = kobject_setup(ls);
	if (error)
		goto out_del;

	error = kobject_register(&ls->ls_kobj);
	if (error)
		goto out_del;

	error = do_uevent(ls, 1);
	if (error)
		goto out_unreg;

	*lockspace = ls;
	return 0;

 out_unreg:
	kobject_unregister(&ls->ls_kobj);
 out_del:
	dlm_delete_debug_file(ls);
	dlm_recoverd_stop(ls);
 out_rcomfree:
	spin_lock(&lslist_lock);
	list_del(&ls->ls_list);
	spin_unlock(&lslist_lock);
	kfree(ls->ls_recover_buf);
 out_dirfree:
	kfree(ls->ls_dirtbl);
 out_lkbfree:
	kfree(ls->ls_lkbtbl);
 out_rsbfree:
	kfree(ls->ls_rsbtbl);
 out_lsfree:
	kfree(ls);
 out:
	module_put(THIS_MODULE);
	return error;
}

int dlm_new_lockspace(char *name, int namelen, void **lockspace,
		      uint32_t flags, int lvblen)
{
	int error = 0;

	mutex_lock(&ls_lock);
	if (!ls_count)
		error = threads_start();
	if (error)
		goto out;

	error = new_lockspace(name, namelen, lockspace, flags, lvblen);
	if (!error)
		ls_count++;
 out:
	mutex_unlock(&ls_lock);
	return error;
}

/* Return 1 if the lockspace still has active remote locks,
 *        2 if the lockspace still has active local locks.
 */
static int lockspace_busy(struct dlm_ls *ls)
{
	int i, lkb_found = 0;
	struct dlm_lkb *lkb;

	/* NOTE: We check the lockidtbl here rather than the resource table.
	   This is because there may be LKBs queued as ASTs that have been
	   unlinked from their RSBs and are pending deletion once the AST has
	   been delivered */

	for (i = 0; i < ls->ls_lkbtbl_size; i++) {
		read_lock(&ls->ls_lkbtbl[i].lock);
		if (!list_empty(&ls->ls_lkbtbl[i].list)) {
			lkb_found = 1;
			list_for_each_entry(lkb, &ls->ls_lkbtbl[i].list,
					    lkb_idtbl_list) {
				if (!lkb->lkb_nodeid) {
					read_unlock(&ls->ls_lkbtbl[i].lock);
					return 2;
				}
			}
		}
		read_unlock(&ls->ls_lkbtbl[i].lock);
	}
	return lkb_found;
}

static int release_lockspace(struct dlm_ls *ls, int force)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *rsb;
	struct list_head *head;
	int i;
	int busy = lockspace_busy(ls);

	if (busy > force)
		return -EBUSY;

	if (force < 3)
		do_uevent(ls, 0);

	dlm_recoverd_stop(ls);

	remove_lockspace(ls);

	dlm_delete_debug_file(ls);

	dlm_astd_suspend();

	kfree(ls->ls_recover_buf);

	/*
	 * Free direntry structs.
	 */

	dlm_dir_clear(ls);
	kfree(ls->ls_dirtbl);

	/*
	 * Free all lkb's on lkbtbl[] lists.
	 */

	for (i = 0; i < ls->ls_lkbtbl_size; i++) {
		head = &ls->ls_lkbtbl[i].list;
		while (!list_empty(head)) {
			lkb = list_entry(head->next, struct dlm_lkb,
					 lkb_idtbl_list);

			list_del(&lkb->lkb_idtbl_list);

			dlm_del_ast(lkb);

			if (lkb->lkb_lvbptr && lkb->lkb_flags & DLM_IFL_MSTCPY)
				free_lvb(lkb->lkb_lvbptr);

			free_lkb(lkb);
		}
	}
	dlm_astd_resume();

	kfree(ls->ls_lkbtbl);

	/*
	 * Free all rsb's on rsbtbl[] lists
	 */

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		head = &ls->ls_rsbtbl[i].list;
		while (!list_empty(head)) {
			rsb = list_entry(head->next, struct dlm_rsb,
					 res_hashchain);

			list_del(&rsb->res_hashchain);
			free_rsb(rsb);
		}

		head = &ls->ls_rsbtbl[i].toss;
		while (!list_empty(head)) {
			rsb = list_entry(head->next, struct dlm_rsb,
					 res_hashchain);
			list_del(&rsb->res_hashchain);
			free_rsb(rsb);
		}
	}

	kfree(ls->ls_rsbtbl);

	/*
	 * Free structures on any other lists
	 */

	dlm_purge_requestqueue(ls);
	kfree(ls->ls_recover_args);
	dlm_clear_free_entries(ls);
	dlm_clear_members(ls);
	dlm_clear_members_gone(ls);
	kfree(ls->ls_node_array);
	kobject_unregister(&ls->ls_kobj);
        /* The ls structure will be freed when the kobject is done with */

	mutex_lock(&ls_lock);
	ls_count--;
	if (!ls_count)
		threads_stop();
	mutex_unlock(&ls_lock);

	module_put(THIS_MODULE);
	return 0;
}

/*
 * Called when a system has released all its locks and is not going to use the
 * lockspace any longer.  We free everything we're managing for this lockspace.
 * Remaining nodes will go through the recovery process as if we'd died.  The
 * lockspace must continue to function as usual, participating in recoveries,
 * until this returns.
 *
 * Force has 4 possible values:
 * 0 - don't destroy locksapce if it has any LKBs
 * 1 - destroy lockspace if it has remote LKBs but not if it has local LKBs
 * 2 - destroy lockspace regardless of LKBs
 * 3 - destroy lockspace as part of a forced shutdown
 */

int dlm_release_lockspace(void *lockspace, int force)
{
	struct dlm_ls *ls;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;
	dlm_put_lockspace(ls);
	return release_lockspace(ls, force);
}

