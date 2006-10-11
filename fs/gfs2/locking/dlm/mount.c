/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include "lock_dlm.h"

int gdlm_drop_count;
int gdlm_drop_period;
const struct lm_lockops gdlm_ops;


static struct gdlm_ls *init_gdlm(lm_callback_t cb, struct gfs2_sbd *sdp,
				 int flags, char *table_name)
{
	struct gdlm_ls *ls;
	char buf[256], *p;

	ls = kzalloc(sizeof(struct gdlm_ls), GFP_KERNEL);
	if (!ls)
		return NULL;

	ls->drop_locks_count = gdlm_drop_count;
	ls->drop_locks_period = gdlm_drop_period;
	ls->fscb = cb;
	ls->sdp = sdp;
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

	p = strchr(buf, ':');
	if (!p) {
		log_info("invalid table_name \"%s\"", table_name);
		kfree(ls);
		return NULL;
	}
	*p = '\0';
	p++;

	strncpy(ls->clustername, buf, GDLM_NAME_LEN);
	strncpy(ls->fsname, p, GDLM_NAME_LEN);

	return ls;
}

static int make_args(struct gdlm_ls *ls, char *data_arg, int *nodir)
{
	char data[256];
	char *options, *x, *y;
	int error = 0;

	memset(data, 0, 256);
	strncpy(data, data_arg, 255);

	for (options = data; (x = strsep(&options, ":")); ) {
		if (!*x)
			continue;

		y = strchr(x, '=');
		if (y)
			*y++ = 0;

		if (!strcmp(x, "jid")) {
			if (!y) {
				log_error("need argument to jid");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &ls->jid);

		} else if (!strcmp(x, "first")) {
			if (!y) {
				log_error("need argument to first");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &ls->first);

		} else if (!strcmp(x, "id")) {
			if (!y) {
				log_error("need argument to id");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &ls->id);

		} else if (!strcmp(x, "nodir")) {
			if (!y) {
				log_error("need argument to nodir");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", nodir);

		} else {
			log_error("unkonwn option: %s", x);
			error = -EINVAL;
			break;
		}
	}

	return error;
}

static int gdlm_mount(char *table_name, char *host_data,
			lm_callback_t cb, void *cb_data,
			unsigned int min_lvb_size, int flags,
			struct lm_lockstruct *lockstruct,
			struct kobject *fskobj)
{
	struct gdlm_ls *ls;
	int error = -ENOMEM, nodir = 0;

	if (min_lvb_size > GDLM_LVB_SIZE)
		goto out;

	ls = init_gdlm(cb, cb_data, flags, table_name);
	if (!ls)
		goto out;

	error = make_args(ls, host_data, &nodir);
	if (error)
		goto out;

	error = gdlm_init_threads(ls);
	if (error)
		goto out_free;

	error = gdlm_kobject_setup(ls, fskobj);
	if (error)
		goto out_thread;

	error = dlm_new_lockspace(ls->fsname, strlen(ls->fsname),
				  &ls->dlm_lockspace,
				  nodir ? DLM_LSFL_NODIR : 0,
				  GDLM_LVB_SIZE);
	if (error) {
		log_error("dlm_new_lockspace error %d", error);
		goto out_kobj;
	}

	lockstruct->ls_jid = ls->jid;
	lockstruct->ls_first = ls->first;
	lockstruct->ls_lockspace = ls;
	lockstruct->ls_ops = &gdlm_ops;
	lockstruct->ls_flags = 0;
	lockstruct->ls_lvb_size = GDLM_LVB_SIZE;
	return 0;

out_kobj:
	gdlm_kobject_release(ls);
out_thread:
	gdlm_release_threads(ls);
out_free:
	kfree(ls);
out:
	return error;
}

static void gdlm_unmount(void *lockspace)
{
	struct gdlm_ls *ls = lockspace;
	int rv;

	log_debug("unmount flags %lx", ls->flags);

	/* FIXME: serialize unmount and withdraw in case they
	   happen at once.  Also, if unmount follows withdraw,
	   wait for withdraw to finish. */

	if (test_bit(DFL_WITHDRAW, &ls->flags))
		goto out;

	gdlm_kobject_release(ls);
	dlm_release_lockspace(ls->dlm_lockspace, 2);
	gdlm_release_threads(ls);
	rv = gdlm_release_all_locks(ls);
	if (rv)
		log_info("gdlm_unmount: %d stray locks freed", rv);
out:
	kfree(ls);
}

static void gdlm_recovery_done(void *lockspace, unsigned int jid,
                               unsigned int message)
{
	struct gdlm_ls *ls = lockspace;
	ls->recover_jid_done = jid;
	ls->recover_jid_status = message;
	kobject_uevent(&ls->kobj, KOBJ_CHANGE);
}

static void gdlm_others_may_mount(void *lockspace)
{
	struct gdlm_ls *ls = lockspace;
	ls->first_done = 1;
	kobject_uevent(&ls->kobj, KOBJ_CHANGE);
}

/* Userspace gets the offline uevent, blocks new gfs locks on
   other mounters, and lets us know (sets WITHDRAW flag).  Then,
   userspace leaves the mount group while we leave the lockspace. */

static void gdlm_withdraw(void *lockspace)
{
	struct gdlm_ls *ls = lockspace;

	kobject_uevent(&ls->kobj, KOBJ_OFFLINE);

	wait_event_interruptible(ls->wait_control,
				 test_bit(DFL_WITHDRAW, &ls->flags));

	dlm_release_lockspace(ls->dlm_lockspace, 2);
	gdlm_release_threads(ls);
	gdlm_release_all_locks(ls);
	gdlm_kobject_release(ls);
}

const struct lm_lockops gdlm_ops = {
	.lm_proto_name = "lock_dlm",
	.lm_mount = gdlm_mount,
	.lm_others_may_mount = gdlm_others_may_mount,
	.lm_unmount = gdlm_unmount,
	.lm_withdraw = gdlm_withdraw,
	.lm_get_lock = gdlm_get_lock,
	.lm_put_lock = gdlm_put_lock,
	.lm_lock = gdlm_lock,
	.lm_unlock = gdlm_unlock,
	.lm_plock = gdlm_plock,
	.lm_punlock = gdlm_punlock,
	.lm_plock_get = gdlm_plock_get,
	.lm_cancel = gdlm_cancel,
	.lm_hold_lvb = gdlm_hold_lvb,
	.lm_unhold_lvb = gdlm_unhold_lvb,
	.lm_recovery_done = gdlm_recovery_done,
	.lm_owner = THIS_MODULE,
};

