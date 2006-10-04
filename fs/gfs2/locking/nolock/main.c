/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/lm_interface.h>

struct nolock_lockspace {
	unsigned int nl_lvb_size;
};

static const struct lm_lockops nolock_ops;

static int nolock_mount(char *table_name, char *host_data,
			lm_callback_t cb, void *cb_data,
			unsigned int min_lvb_size, int flags,
			struct lm_lockstruct *lockstruct,
			struct kobject *fskobj)
{
	char *c;
	unsigned int jid;
	struct nolock_lockspace *nl;

	c = strstr(host_data, "jid=");
	if (!c)
		jid = 0;
	else {
		c += 4;
		sscanf(c, "%u", &jid);
	}

	nl = kzalloc(sizeof(struct nolock_lockspace), GFP_KERNEL);
	if (!nl)
		return -ENOMEM;

	nl->nl_lvb_size = min_lvb_size;

	lockstruct->ls_jid = jid;
	lockstruct->ls_first = 1;
	lockstruct->ls_lvb_size = min_lvb_size;
	lockstruct->ls_lockspace = nl;
	lockstruct->ls_ops = &nolock_ops;
	lockstruct->ls_flags = LM_LSFLAG_LOCAL;

	return 0;
}

static void nolock_others_may_mount(void *lockspace)
{
}

static void nolock_unmount(void *lockspace)
{
	struct nolock_lockspace *nl = lockspace;
	kfree(nl);
}

static void nolock_withdraw(void *lockspace)
{
}

/**
 * nolock_get_lock - get a lm_lock_t given a descripton of the lock
 * @lockspace: the lockspace the lock lives in
 * @name: the name of the lock
 * @lockp: return the lm_lock_t here
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int nolock_get_lock(void *lockspace, struct lm_lockname *name,
			   void **lockp)
{
	*lockp = lockspace;
	return 0;
}

/**
 * nolock_put_lock - get rid of a lock structure
 * @lock: the lock to throw away
 *
 */

static void nolock_put_lock(void *lock)
{
}

/**
 * nolock_lock - acquire a lock
 * @lock: the lock to manipulate
 * @cur_state: the current state
 * @req_state: the requested state
 * @flags: modifier flags
 *
 * Returns: A bitmap of LM_OUT_*
 */

static unsigned int nolock_lock(void *lock, unsigned int cur_state,
				unsigned int req_state, unsigned int flags)
{
	return req_state | LM_OUT_CACHEABLE;
}

/**
 * nolock_unlock - unlock a lock
 * @lock: the lock to manipulate
 * @cur_state: the current state
 *
 * Returns: 0
 */

static unsigned int nolock_unlock(void *lock, unsigned int cur_state)
{
	return 0;
}

static void nolock_cancel(void *lock)
{
}

/**
 * nolock_hold_lvb - hold on to a lock value block
 * @lock: the lock the LVB is associated with
 * @lvbp: return the lm_lvb_t here
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int nolock_hold_lvb(void *lock, char **lvbp)
{
	struct nolock_lockspace *nl = lock;
	int error = 0;

	*lvbp = kzalloc(nl->nl_lvb_size, GFP_KERNEL);
	if (!*lvbp)
		error = -ENOMEM;

	return error;
}

/**
 * nolock_unhold_lvb - release a LVB
 * @lock: the lock the LVB is associated with
 * @lvb: the lock value block
 *
 */

static void nolock_unhold_lvb(void *lock, char *lvb)
{
	kfree(lvb);
}

static int nolock_plock_get(void *lockspace, struct lm_lockname *name,
			    struct file *file, struct file_lock *fl)
{
	struct file_lock tmp;
	int ret;

	ret = posix_test_lock(file, fl, &tmp);
	fl->fl_type = F_UNLCK;
	if (ret)
		memcpy(fl, &tmp, sizeof(struct file_lock));

	return 0;
}

static int nolock_plock(void *lockspace, struct lm_lockname *name,
			struct file *file, int cmd, struct file_lock *fl)
{
	int error;
	error = posix_lock_file_wait(file, fl);
	return error;
}

static int nolock_punlock(void *lockspace, struct lm_lockname *name,
			  struct file *file, struct file_lock *fl)
{
	int error;
	error = posix_lock_file_wait(file, fl);
	return error;
}

static void nolock_recovery_done(void *lockspace, unsigned int jid,
				 unsigned int message)
{
}

static const struct lm_lockops nolock_ops = {
	.lm_proto_name = "lock_nolock",
	.lm_mount = nolock_mount,
	.lm_others_may_mount = nolock_others_may_mount,
	.lm_unmount = nolock_unmount,
	.lm_withdraw = nolock_withdraw,
	.lm_get_lock = nolock_get_lock,
	.lm_put_lock = nolock_put_lock,
	.lm_lock = nolock_lock,
	.lm_unlock = nolock_unlock,
	.lm_cancel = nolock_cancel,
	.lm_hold_lvb = nolock_hold_lvb,
	.lm_unhold_lvb = nolock_unhold_lvb,
	.lm_plock_get = nolock_plock_get,
	.lm_plock = nolock_plock,
	.lm_punlock = nolock_punlock,
	.lm_recovery_done = nolock_recovery_done,
	.lm_owner = THIS_MODULE,
};

static int __init init_nolock(void)
{
	int error;

	error = gfs2_register_lockproto(&nolock_ops);
	if (error) {
		printk(KERN_WARNING
		       "lock_nolock: can't register protocol: %d\n", error);
		return error;
	}

	printk(KERN_INFO
	       "Lock_Nolock (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

static void __exit exit_nolock(void)
{
	gfs2_unregister_lockproto(&nolock_ops);
}

module_init(init_nolock);
module_exit(exit_nolock);

MODULE_DESCRIPTION("GFS Nolock Locking Module");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

