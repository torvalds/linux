// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include <linux/module.h>

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "dir.h"
#include "midcomms.h"
#include "config.h"
#include "memory.h"
#include "lock.h"
#include "recover.h"
#include "requestqueue.h"
#include "user.h"
#include "ast.h"

static int			ls_count;
static struct mutex		ls_lock;
static struct list_head		lslist;
static spinlock_t		lslist_lock;

static ssize_t dlm_control_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int n;
	int rc = kstrtoint(buf, 0, &n);

	if (rc)
		return rc;
	ls = dlm_find_lockspace_local(ls);
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
	int rc = kstrtoint(buf, 0, &ls->ls_uevent_result);

	if (rc)
		return rc;
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
	int rc = kstrtouint(buf, 0, &ls->ls_global_id);

	if (rc)
		return rc;
	return len;
}

static ssize_t dlm_nodir_show(struct dlm_ls *ls, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", dlm_no_directory(ls));
}

static ssize_t dlm_nodir_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	int val;
	int rc = kstrtoint(buf, 0, &val);

	if (rc)
		return rc;
	if (val == 1)
		set_bit(LSFL_NODIR, &ls->ls_flags);
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

static struct dlm_attr dlm_attr_nodir = {
	.attr  = {.name = "nodir", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_nodir_show,
	.store = dlm_nodir_store
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
	&dlm_attr_nodir.attr,
	&dlm_attr_recover_status.attr,
	&dlm_attr_recover_nodeid.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dlm);

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

static const struct sysfs_ops dlm_attr_ops = {
	.show  = dlm_attr_show,
	.store = dlm_attr_store,
};

static struct kobj_type dlm_ktype = {
	.default_groups = dlm_groups,
	.sysfs_ops     = &dlm_attr_ops,
};

static struct kset *dlm_kset;

static int do_uevent(struct dlm_ls *ls, int in)
{
	if (in)
		kobject_uevent(&ls->ls_kobj, KOBJ_ONLINE);
	else
		kobject_uevent(&ls->ls_kobj, KOBJ_OFFLINE);

	log_rinfo(ls, "%s the lockspace group...", in ? "joining" : "leaving");

	/* dlm_controld will see the uevent, do the necessary group management
	   and then write to sysfs to wake us */

	wait_event(ls->ls_uevent_wait,
		   test_and_clear_bit(LSFL_UEVENT_WAIT, &ls->ls_flags));

	log_rinfo(ls, "group event done %d", ls->ls_uevent_result);

	return ls->ls_uevent_result;
}

static int dlm_uevent(const struct kobject *kobj, struct kobj_uevent_env *env)
{
	const struct dlm_ls *ls = container_of(kobj, struct dlm_ls, ls_kobj);

	add_uevent_var(env, "LOCKSPACE=%s", ls->ls_name);
	return 0;
}

static const struct kset_uevent_ops dlm_uevent_ops = {
	.uevent = dlm_uevent,
};

int __init dlm_lockspace_init(void)
{
	ls_count = 0;
	mutex_init(&ls_lock);
	INIT_LIST_HEAD(&lslist);
	spin_lock_init(&lslist_lock);

	dlm_kset = kset_create_and_add("dlm", &dlm_uevent_ops, kernel_kobj);
	if (!dlm_kset) {
		printk(KERN_WARNING "%s: can not create kset\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

void dlm_lockspace_exit(void)
{
	kset_unregister(dlm_kset);
}

struct dlm_ls *dlm_find_lockspace_global(uint32_t id)
{
	struct dlm_ls *ls;

	spin_lock_bh(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_global_id == id) {
			atomic_inc(&ls->ls_count);
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock_bh(&lslist_lock);
	return ls;
}

struct dlm_ls *dlm_find_lockspace_local(dlm_lockspace_t *lockspace)
{
	struct dlm_ls *ls = lockspace;

	atomic_inc(&ls->ls_count);
	return ls;
}

struct dlm_ls *dlm_find_lockspace_device(int minor)
{
	struct dlm_ls *ls;

	spin_lock_bh(&lslist_lock);
	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_device.minor == minor) {
			atomic_inc(&ls->ls_count);
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock_bh(&lslist_lock);
	return ls;
}

void dlm_put_lockspace(struct dlm_ls *ls)
{
	if (atomic_dec_and_test(&ls->ls_count))
		wake_up(&ls->ls_count_wait);
}

static void remove_lockspace(struct dlm_ls *ls)
{
retry:
	wait_event(ls->ls_count_wait, atomic_read(&ls->ls_count) == 0);

	spin_lock_bh(&lslist_lock);
	if (atomic_read(&ls->ls_count) != 0) {
		spin_unlock_bh(&lslist_lock);
		goto retry;
	}

	WARN_ON(ls->ls_create_count != 0);
	list_del(&ls->ls_list);
	spin_unlock_bh(&lslist_lock);
}

static int threads_start(void)
{
	int error;

	/* Thread for sending/receiving messages for all lockspace's */
	error = dlm_midcomms_start();
	if (error)
		log_print("cannot start dlm midcomms %d", error);

	return error;
}

static int lkb_idr_free(struct dlm_lkb *lkb)
{
	if (lkb->lkb_lvbptr && test_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags))
		dlm_free_lvb(lkb->lkb_lvbptr);

	dlm_free_lkb(lkb);
	return 0;
}

static void rhash_free_rsb(void *ptr, void *arg)
{
	struct dlm_rsb *rsb = ptr;

	dlm_free_rsb(rsb);
}

static void free_lockspace(struct work_struct *work)
{
	struct dlm_ls *ls  = container_of(work, struct dlm_ls, ls_free_work);
	struct dlm_lkb *lkb;
	unsigned long id;

	/*
	 * Free all lkb's in xa
	 */
	xa_for_each(&ls->ls_lkbxa, id, lkb) {
		lkb_idr_free(lkb);
	}
	xa_destroy(&ls->ls_lkbxa);

	/*
	 * Free all rsb's on rsbtbl
	 */
	rhashtable_free_and_destroy(&ls->ls_rsbtbl, rhash_free_rsb, NULL);

	kfree(ls);
}

static int new_lockspace(const char *name, const char *cluster,
			 uint32_t flags, int lvblen,
			 const struct dlm_lockspace_ops *ops, void *ops_arg,
			 int *ops_result, dlm_lockspace_t **lockspace)
{
	struct dlm_ls *ls;
	int namelen = strlen(name);
	int error;

	if (namelen > DLM_LOCKSPACE_LEN || namelen == 0)
		return -EINVAL;

	if (lvblen % 8)
		return -EINVAL;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	if (!dlm_user_daemon_available()) {
		log_print("dlm user daemon not available");
		error = -EUNATCH;
		goto out;
	}

	if (ops && ops_result) {
	       	if (!dlm_config.ci_recover_callbacks)
			*ops_result = -EOPNOTSUPP;
		else
			*ops_result = 0;
	}

	if (!cluster)
		log_print("dlm cluster name '%s' is being used without an application provided cluster name",
			  dlm_config.ci_cluster_name);

	if (dlm_config.ci_recover_callbacks && cluster &&
	    strncmp(cluster, dlm_config.ci_cluster_name, DLM_LOCKSPACE_LEN)) {
		log_print("dlm cluster name '%s' does not match "
			  "the application cluster name '%s'",
			  dlm_config.ci_cluster_name, cluster);
		error = -EBADR;
		goto out;
	}

	error = 0;

	spin_lock_bh(&lslist_lock);
	list_for_each_entry(ls, &lslist, ls_list) {
		WARN_ON(ls->ls_create_count <= 0);
		if (ls->ls_namelen != namelen)
			continue;
		if (memcmp(ls->ls_name, name, namelen))
			continue;
		if (flags & DLM_LSFL_NEWEXCL) {
			error = -EEXIST;
			break;
		}
		ls->ls_create_count++;
		*lockspace = ls;
		error = 1;
		break;
	}
	spin_unlock_bh(&lslist_lock);

	if (error)
		goto out;

	error = -ENOMEM;

	ls = kzalloc(sizeof(*ls), GFP_NOFS);
	if (!ls)
		goto out;
	memcpy(ls->ls_name, name, namelen);
	ls->ls_namelen = namelen;
	ls->ls_lvblen = lvblen;
	atomic_set(&ls->ls_count, 0);
	init_waitqueue_head(&ls->ls_count_wait);
	ls->ls_flags = 0;

	if (ops && dlm_config.ci_recover_callbacks) {
		ls->ls_ops = ops;
		ls->ls_ops_arg = ops_arg;
	}

	if (flags & DLM_LSFL_SOFTIRQ)
		set_bit(LSFL_SOFTIRQ, &ls->ls_flags);

	/* ls_exflags are forced to match among nodes, and we don't
	 * need to require all nodes to have some flags set
	 */
	ls->ls_exflags = (flags & ~(DLM_LSFL_FS | DLM_LSFL_NEWEXCL |
				    DLM_LSFL_SOFTIRQ));

	INIT_LIST_HEAD(&ls->ls_slow_inactive);
	INIT_LIST_HEAD(&ls->ls_slow_active);
	rwlock_init(&ls->ls_rsbtbl_lock);

	error = rhashtable_init(&ls->ls_rsbtbl, &dlm_rhash_rsb_params);
	if (error)
		goto out_lsfree;

	xa_init_flags(&ls->ls_lkbxa, XA_FLAGS_ALLOC | XA_FLAGS_LOCK_BH);
	rwlock_init(&ls->ls_lkbxa_lock);

	INIT_LIST_HEAD(&ls->ls_waiters);
	spin_lock_init(&ls->ls_waiters_lock);
	INIT_LIST_HEAD(&ls->ls_orphans);
	spin_lock_init(&ls->ls_orphans_lock);

	INIT_LIST_HEAD(&ls->ls_nodes);
	INIT_LIST_HEAD(&ls->ls_nodes_gone);
	ls->ls_num_nodes = 0;
	ls->ls_low_nodeid = 0;
	ls->ls_total_weight = 0;
	ls->ls_node_array = NULL;

	memset(&ls->ls_local_rsb, 0, sizeof(struct dlm_rsb));
	ls->ls_local_rsb.res_ls = ls;

	ls->ls_debug_rsb_dentry = NULL;
	ls->ls_debug_waiters_dentry = NULL;

	init_waitqueue_head(&ls->ls_uevent_wait);
	ls->ls_uevent_result = 0;
	init_completion(&ls->ls_recovery_done);
	ls->ls_recovery_result = -1;

	spin_lock_init(&ls->ls_cb_lock);
	INIT_LIST_HEAD(&ls->ls_cb_delay);

	INIT_WORK(&ls->ls_free_work, free_lockspace);

	ls->ls_recoverd_task = NULL;
	mutex_init(&ls->ls_recoverd_active);
	spin_lock_init(&ls->ls_recover_lock);
	spin_lock_init(&ls->ls_rcom_spin);
	get_random_bytes(&ls->ls_rcom_seq, sizeof(uint64_t));
	ls->ls_recover_status = 0;
	ls->ls_recover_seq = get_random_u64();
	ls->ls_recover_args = NULL;
	init_rwsem(&ls->ls_in_recovery);
	rwlock_init(&ls->ls_recv_active);
	INIT_LIST_HEAD(&ls->ls_requestqueue);
	rwlock_init(&ls->ls_requestqueue_lock);
	spin_lock_init(&ls->ls_clear_proc_locks);

	/* Due backwards compatibility with 3.1 we need to use maximum
	 * possible dlm message size to be sure the message will fit and
	 * not having out of bounds issues. However on sending side 3.2
	 * might send less.
	 */
	ls->ls_recover_buf = kmalloc(DLM_MAX_SOCKET_BUFSIZE, GFP_NOFS);
	if (!ls->ls_recover_buf) {
		error = -ENOMEM;
		goto out_lkbxa;
	}

	ls->ls_slot = 0;
	ls->ls_num_slots = 0;
	ls->ls_slots_size = 0;
	ls->ls_slots = NULL;

	INIT_LIST_HEAD(&ls->ls_recover_list);
	spin_lock_init(&ls->ls_recover_list_lock);
	xa_init_flags(&ls->ls_recover_xa, XA_FLAGS_ALLOC | XA_FLAGS_LOCK_BH);
	spin_lock_init(&ls->ls_recover_xa_lock);
	ls->ls_recover_list_count = 0;
	init_waitqueue_head(&ls->ls_wait_general);
	INIT_LIST_HEAD(&ls->ls_masters_list);
	rwlock_init(&ls->ls_masters_lock);
	INIT_LIST_HEAD(&ls->ls_dir_dump_list);
	rwlock_init(&ls->ls_dir_dump_lock);

	INIT_LIST_HEAD(&ls->ls_scan_list);
	spin_lock_init(&ls->ls_scan_lock);
	timer_setup(&ls->ls_scan_timer, dlm_rsb_scan, TIMER_DEFERRABLE);

	spin_lock_bh(&lslist_lock);
	ls->ls_create_count = 1;
	list_add(&ls->ls_list, &lslist);
	spin_unlock_bh(&lslist_lock);

	if (flags & DLM_LSFL_FS)
		set_bit(LSFL_FS, &ls->ls_flags);

	error = dlm_callback_start(ls);
	if (error) {
		log_error(ls, "can't start dlm_callback %d", error);
		goto out_delist;
	}

	init_waitqueue_head(&ls->ls_recover_lock_wait);

	/*
	 * Once started, dlm_recoverd first looks for ls in lslist, then
	 * initializes ls_in_recovery as locked in "down" mode.  We need
	 * to wait for the wakeup from dlm_recoverd because in_recovery
	 * has to start out in down mode.
	 */

	error = dlm_recoverd_start(ls);
	if (error) {
		log_error(ls, "can't start dlm_recoverd %d", error);
		goto out_callback;
	}

	wait_event(ls->ls_recover_lock_wait,
		   test_bit(LSFL_RECOVER_LOCK, &ls->ls_flags));

	ls->ls_kobj.kset = dlm_kset;
	error = kobject_init_and_add(&ls->ls_kobj, &dlm_ktype, NULL,
				     "%s", ls->ls_name);
	if (error)
		goto out_recoverd;
	kobject_uevent(&ls->ls_kobj, KOBJ_ADD);

	/* This uevent triggers dlm_controld in userspace to add us to the
	   group of nodes that are members of this lockspace (managed by the
	   cluster infrastructure.)  Once it's done that, it tells us who the
	   current lockspace members are (via configfs) and then tells the
	   lockspace to start running (via sysfs) in dlm_ls_start(). */

	error = do_uevent(ls, 1);
	if (error)
		goto out_recoverd;

	/* wait until recovery is successful or failed */
	wait_for_completion(&ls->ls_recovery_done);
	error = ls->ls_recovery_result;
	if (error)
		goto out_members;

	dlm_create_debug_file(ls);

	log_rinfo(ls, "join complete");
	*lockspace = ls;
	return 0;

 out_members:
	do_uevent(ls, 0);
	dlm_clear_members(ls);
	kfree(ls->ls_node_array);
 out_recoverd:
	dlm_recoverd_stop(ls);
 out_callback:
	dlm_callback_stop(ls);
 out_delist:
	spin_lock_bh(&lslist_lock);
	list_del(&ls->ls_list);
	spin_unlock_bh(&lslist_lock);
	xa_destroy(&ls->ls_recover_xa);
	kfree(ls->ls_recover_buf);
 out_lkbxa:
	xa_destroy(&ls->ls_lkbxa);
	rhashtable_destroy(&ls->ls_rsbtbl);
 out_lsfree:
	kobject_put(&ls->ls_kobj);
	kfree(ls);
 out:
	module_put(THIS_MODULE);
	return error;
}

static int __dlm_new_lockspace(const char *name, const char *cluster,
			       uint32_t flags, int lvblen,
			       const struct dlm_lockspace_ops *ops,
			       void *ops_arg, int *ops_result,
			       dlm_lockspace_t **lockspace)
{
	int error = 0;

	mutex_lock(&ls_lock);
	if (!ls_count)
		error = threads_start();
	if (error)
		goto out;

	error = new_lockspace(name, cluster, flags, lvblen, ops, ops_arg,
			      ops_result, lockspace);
	if (!error)
		ls_count++;
	if (error > 0)
		error = 0;
	if (!ls_count) {
		dlm_midcomms_shutdown();
		dlm_midcomms_stop();
	}
 out:
	mutex_unlock(&ls_lock);
	return error;
}

int dlm_new_lockspace(const char *name, const char *cluster, uint32_t flags,
		      int lvblen, const struct dlm_lockspace_ops *ops,
		      void *ops_arg, int *ops_result,
		      dlm_lockspace_t **lockspace)
{
	return __dlm_new_lockspace(name, cluster, flags | DLM_LSFL_FS, lvblen,
				   ops, ops_arg, ops_result, lockspace);
}

int dlm_new_user_lockspace(const char *name, const char *cluster,
			   uint32_t flags, int lvblen,
			   const struct dlm_lockspace_ops *ops,
			   void *ops_arg, int *ops_result,
			   dlm_lockspace_t **lockspace)
{
	if (flags & DLM_LSFL_SOFTIRQ)
		return -EINVAL;

	return __dlm_new_lockspace(name, cluster, flags, lvblen, ops,
				   ops_arg, ops_result, lockspace);
}

/* NOTE: We check the lkbxa here rather than the resource table.
   This is because there may be LKBs queued as ASTs that have been unlinked
   from their RSBs and are pending deletion once the AST has been delivered */

static int lockspace_busy(struct dlm_ls *ls, int force)
{
	struct dlm_lkb *lkb;
	unsigned long id;
	int rv = 0;

	read_lock_bh(&ls->ls_lkbxa_lock);
	if (force == 0) {
		xa_for_each(&ls->ls_lkbxa, id, lkb) {
			rv = 1;
			break;
		}
	} else if (force == 1) {
		xa_for_each(&ls->ls_lkbxa, id, lkb) {
			if (lkb->lkb_nodeid == 0 &&
			    lkb->lkb_grmode != DLM_LOCK_IV) {
				rv = 1;
				break;
			}
		}
	} else {
		rv = 0;
	}
	read_unlock_bh(&ls->ls_lkbxa_lock);
	return rv;
}

static int release_lockspace(struct dlm_ls *ls, int force)
{
	int busy, rv;

	busy = lockspace_busy(ls, force);

	spin_lock_bh(&lslist_lock);
	if (ls->ls_create_count == 1) {
		if (busy) {
			rv = -EBUSY;
		} else {
			/* remove_lockspace takes ls off lslist */
			ls->ls_create_count = 0;
			rv = 0;
		}
	} else if (ls->ls_create_count > 1) {
		rv = --ls->ls_create_count;
	} else {
		rv = -EINVAL;
	}
	spin_unlock_bh(&lslist_lock);

	if (rv) {
		log_debug(ls, "release_lockspace no remove %d", rv);
		return rv;
	}

	if (ls_count == 1)
		dlm_midcomms_version_wait();

	dlm_device_deregister(ls);

	if (force < 3 && dlm_user_daemon_available())
		do_uevent(ls, 0);

	dlm_recoverd_stop(ls);

	/* clear the LSFL_RUNNING flag to fast up
	 * time_shutdown_sync(), we don't care anymore
	 */
	clear_bit(LSFL_RUNNING, &ls->ls_flags);
	timer_shutdown_sync(&ls->ls_scan_timer);

	if (ls_count == 1) {
		dlm_clear_members(ls);
		dlm_midcomms_shutdown();
	}

	dlm_callback_stop(ls);

	remove_lockspace(ls);

	dlm_delete_debug_file(ls);

	kobject_put(&ls->ls_kobj);

	xa_destroy(&ls->ls_recover_xa);
	kfree(ls->ls_recover_buf);

	/*
	 * Free structures on any other lists
	 */

	dlm_purge_requestqueue(ls);
	kfree(ls->ls_recover_args);
	dlm_clear_members(ls);
	dlm_clear_members_gone(ls);
	kfree(ls->ls_node_array);

	log_rinfo(ls, "%s final free", __func__);

	/* delayed free of data structures see free_lockspace() */
	queue_work(dlm_wq, &ls->ls_free_work);
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
 * 0 - don't destroy lockspace if it has any LKBs
 * 1 - destroy lockspace if it has remote LKBs but not if it has local LKBs
 * 2 - destroy lockspace regardless of LKBs
 * 3 - destroy lockspace as part of a forced shutdown
 */

int dlm_release_lockspace(void *lockspace, int force)
{
	struct dlm_ls *ls;
	int error;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;
	dlm_put_lockspace(ls);

	mutex_lock(&ls_lock);
	error = release_lockspace(ls, force);
	if (!error)
		ls_count--;
	if (!ls_count)
		dlm_midcomms_stop();
	mutex_unlock(&ls_lock);

	return error;
}

void dlm_stop_lockspaces(void)
{
	struct dlm_ls *ls;
	int count;

 restart:
	count = 0;
	spin_lock_bh(&lslist_lock);
	list_for_each_entry(ls, &lslist, ls_list) {
		if (!test_bit(LSFL_RUNNING, &ls->ls_flags)) {
			count++;
			continue;
		}
		spin_unlock_bh(&lslist_lock);
		log_error(ls, "no userland control daemon, stopping lockspace");
		dlm_ls_stop(ls);
		goto restart;
	}
	spin_unlock_bh(&lslist_lock);

	if (count)
		log_print("dlm user daemon left %d lockspaces", count);
}
