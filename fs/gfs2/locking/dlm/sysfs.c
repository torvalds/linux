/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/ctype.h>
#include <linux/stat.h>

#include "lock_dlm.h"

extern struct lm_lockops gdlm_ops;

static ssize_t proto_name_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%s\n", gdlm_ops.lm_proto_name);
}

static ssize_t block_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = 0;

	if (test_bit(DFL_BLOCK_LOCKS, &ls->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t block_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_BLOCK_LOCKS, &ls->flags);
	else if (val == 0) {
		clear_bit(DFL_BLOCK_LOCKS, &ls->flags);
		gdlm_submit_delayed(ls);
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static ssize_t withdraw_show(struct gdlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = 0;

	if (test_bit(DFL_WITHDRAW, &ls->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t withdraw_store(struct gdlm_ls *ls, const char *buf, size_t len)
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

static ssize_t id_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%u\n", ls->id);
}

static ssize_t jid_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->jid);
}

static ssize_t first_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->first);
}

static ssize_t first_done_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->first_done);
}

static ssize_t recover_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->recover_jid);
}

static ssize_t recover_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ls->recover_jid = simple_strtol(buf, NULL, 0);
	ls->fscb(ls->sdp, LM_CB_NEED_RECOVERY, &ls->recover_jid);
	return len;
}

static ssize_t recover_done_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->recover_jid_done);
}

static ssize_t recover_status_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->recover_jid_status);
}

static ssize_t drop_count_show(struct gdlm_ls *ls, char *buf)
{
	return sprintf(buf, "%d\n", ls->drop_locks_count);
}

static ssize_t drop_count_store(struct gdlm_ls *ls, const char *buf, size_t len)
{
	ls->drop_locks_count = simple_strtol(buf, NULL, 0);
	return len;
}

struct gdlm_attr {
	struct attribute attr;
	ssize_t (*show)(struct gdlm_ls *, char *);
	ssize_t (*store)(struct gdlm_ls *, const char *, size_t);
};

#define GDLM_ATTR(_name,_mode,_show,_store) \
static struct gdlm_attr gdlm_attr_##_name = __ATTR(_name,_mode,_show,_store)

GDLM_ATTR(proto_name,     0444, proto_name_show,     NULL);
GDLM_ATTR(block,          0644, block_show,          block_store);
GDLM_ATTR(withdraw,       0644, withdraw_show,       withdraw_store);
GDLM_ATTR(id,             0444, id_show,             NULL);
GDLM_ATTR(jid,            0444, jid_show,            NULL);
GDLM_ATTR(first,          0444, first_show,          NULL);
GDLM_ATTR(first_done,     0444, first_done_show,     NULL);
GDLM_ATTR(recover,        0644, recover_show,        recover_store);
GDLM_ATTR(recover_done,   0444, recover_done_show,   NULL);
GDLM_ATTR(recover_status, 0444, recover_status_show, NULL);
GDLM_ATTR(drop_count,     0644, drop_count_show,     drop_count_store);

static struct attribute *gdlm_attrs[] = {
	&gdlm_attr_proto_name.attr,
	&gdlm_attr_block.attr,
	&gdlm_attr_withdraw.attr,
	&gdlm_attr_id.attr,
	&gdlm_attr_jid.attr,
	&gdlm_attr_first.attr,
	&gdlm_attr_first_done.attr,
	&gdlm_attr_recover.attr,
	&gdlm_attr_recover_done.attr,
	&gdlm_attr_recover_status.attr,
	&gdlm_attr_drop_count.attr,
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

int gdlm_kobject_setup(struct gdlm_ls *ls, struct kobject *fskobj)
{
	int error;

	error = kobject_set_name(&ls->kobj, "%s", "lock_module");
	if (error) {
		log_error("can't set kobj name %d", error);
		return error;
	}

	ls->kobj.kset = &gdlm_kset;
	ls->kobj.ktype = &gdlm_ktype;
	ls->kobj.parent = fskobj;

	error = kobject_register(&ls->kobj);
	if (error)
		log_error("can't register kobj %d", error);

	return error;
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

