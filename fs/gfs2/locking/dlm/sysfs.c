/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/ctype.h>
#include <linux/stat.h>

#include "lock_dlm.h"

static ssize_t gdlm_block_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = 0;

	if (test_bit(DFL_BLOCK_LOCKS, &ls->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t gdlm_block_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_BLOCK_LOCKS, &ls->flags);
	else if (val == 0) {
		clear_bit(DFL_BLOCK_LOCKS, &ls->flags);
		gdlm_submit_delayed(ls);
	} else
		ret = -EINVAL;
	return ret;
}

static ssize_t gdlm_mounted_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = -2;

	if (test_bit(DFL_TERMINATE, &ls->flags))
		val = -1;
	else if (test_bit(DFL_LEAVE_DONE, &ls->flags))
		val = 0;
	else if (test_bit(DFL_JOIN_DONE, &ls->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t gdlm_mounted_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_JOIN_DONE, &ls->flags);
	else if (val == 0)
		set_bit(DFL_LEAVE_DONE, &ls->flags);
	else if (val == -1) {
		set_bit(DFL_TERMINATE, &ls->flags);
		set_bit(DFL_JOIN_DONE, &ls->flags);
		set_bit(DFL_LEAVE_DONE, &ls->flags);
	} else
		ret = -EINVAL;
	wake_up(&ls->wait_control);
	return ret;
}

static ssize_t gdlm_withdraw_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = 0;

	if (test_bit(DFL_WITHDRAW, &ls->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t gdlm_withdraw_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_WITHDRAW, &ls->flags);
	else
		ret = -EINVAL;
	wake_up(&ls->wait_control);
	return ret;
}

static ssize_t gdlm_jid_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%u\n", ls->jid);
}

static ssize_t gdlm_jid_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ls->jid = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t gdlm_first_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%u\n", ls->first);
}

static ssize_t gdlm_first_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ls->first = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t gdlm_first_done_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->first_done);
}

static ssize_t gdlm_recover_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%u\n", ls->recover_jid);
}

static ssize_t gdlm_recover_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ls->recover_jid = simple_strtol(buf, NULL, 0);
	ls->fscb(ls->fsdata, LM_CB_NEED_RECOVERY, &ls->recover_jid);
	return len;
}

static ssize_t gdlm_recover_done_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	ret = sprintf(buf, "%d\n", ls->recover_done);
	return ret;
}

static ssize_t gdlm_cluster_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	ret = sprintf(buf, "%s\n", ls->clustername);
	return ret;
}

static ssize_t gdlm_options_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret = 0;

	if (ls->fsflags & LM_MFLAG_SPECTATOR)
		ret += sprintf(buf, "spectator ");

	return ret;
}

struct gdlm_attr {
	struct attribute attr;
	ssize_t (*show)(struct gdlm_ls *, char *);
	ssize_t (*store)(struct gdlm_ls *, const char *, size_t);
};

static struct gdlm_attr gdlm_attr_block = {
	.attr  = {.name = "block", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_block_show,
	.store = gdlm_block_store
};

static struct gdlm_attr gdlm_attr_mounted = {
	.attr  = {.name = "mounted", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_mounted_show,
	.store = gdlm_mounted_store
};

static struct gdlm_attr gdlm_attr_withdraw = {
	.attr  = {.name = "withdraw", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_withdraw_show,
	.store = gdlm_withdraw_store
};

static struct gdlm_attr gdlm_attr_jid = {
	.attr  = {.name = "jid", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_jid_show,
	.store = gdlm_jid_store
};

static struct gdlm_attr gdlm_attr_first = {
	.attr  = {.name = "first", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_first_show,
	.store = gdlm_first_store
};

static struct gdlm_attr gdlm_attr_first_done = {
	.attr  = {.name = "first_done", .mode = S_IRUGO},
	.show  = gdlm_first_done_show,
};

static struct gdlm_attr gdlm_attr_recover = {
	.attr  = {.name = "recover", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_recover_show,
	.store = gdlm_recover_store
};

static struct gdlm_attr gdlm_attr_recover_done = {
	.attr  = {.name = "recover_done", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_recover_done_show,
};

static struct gdlm_attr gdlm_attr_cluster = {
	.attr  = {.name = "cluster", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_cluster_show,
};

static struct gdlm_attr gdlm_attr_options = {
	.attr  = {.name = "options", .mode = S_IRUGO | S_IWUSR},
	.show  = gdlm_options_show,
};

static struct attribute *gdlm_attrs[] = {
	&gdlm_attr_block.attr,
	&gdlm_attr_mounted.attr,
	&gdlm_attr_withdraw.attr,
	&gdlm_attr_jid.attr,
	&gdlm_attr_first.attr,
	&gdlm_attr_first_done.attr,
	&gdlm_attr_recover.attr,
	&gdlm_attr_recover_done.attr,
	&gdlm_attr_cluster.attr,
	&gdlm_attr_options.attr,
	NULL,
};

static ssize_t gdlm_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct gdlm_ls *ls = container_of(kobj, struct gdlm_ls, kobj);
	struct gdlm_attr *a = container_of(attr, struct gdlm_attr, attr);
	return a->show ? a->show(ls, buf) : 0;
}

static ssize_t gdlm_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	struct gdlm_ls *ls = container_of(kobj, struct gdlm_ls, kobj);
	struct gdlm_attr *a = container_of(attr, struct gdlm_attr, attr);
	return a->store ? a->store(ls, buf, len) : len;
}

static struct sysfs_ops gdlm_attr_ops = {
	.show  = gdlm_attr_show,
	.store = gdlm_attr_store,
};

static struct kobj_type gdlm_ktype = {
	.default_attrs = gdlm_attrs,
	.sysfs_ops     = &gdlm_attr_ops,
};

static struct kset gdlm_kset = {
	.subsys = &kernel_subsys,
	.kobj   = {.name = "lock_dlm",},
	.ktype  = &gdlm_ktype,
};

int gdlm_kobject_setup(struct gdlm_ls *ls)
{
	int error;

	error = kobject_set_name(&ls->kobj, "%s", ls->fsname);
	if (error)
		return error;

	ls->kobj.kset = &gdlm_kset;
	ls->kobj.ktype = &gdlm_ktype;

	error = kobject_register(&ls->kobj);

	return 0;
}

void gdlm_kobject_release(struct gdlm_ls *ls)
{
	kobject_unregister(&ls->kobj);
}

int gdlm_sysfs_init(void)
{
	int error;

	error = kset_register(&gdlm_kset);
	if (error)
		printk("lock_dlm: cannot register kset %d\n", error);

	return error;
}

void gdlm_sysfs_exit(void)
{
	kset_unregister(&gdlm_kset);
}

