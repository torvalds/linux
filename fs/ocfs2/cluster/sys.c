// SPDX-License-Identifier: GPL-2.0-only
/*
 * sys.c
 *
 * OCFS2 cluster sysfs interface
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>

#include "ocfs2_nodemanager.h"
#include "masklog.h"
#include "sys.h"


static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", O2NM_API_VERSION);
}
static struct kobj_attribute attr_version =
	__ATTR(interface_revision, S_IRUGO, version_show, NULL);

static struct attribute *o2cb_attrs[] = {
	&attr_version.attr,
	NULL,
};

static struct attribute_group o2cb_attr_group = {
	.attrs = o2cb_attrs,
};

static struct kset *o2cb_kset;

void o2cb_sys_shutdown(void)
{
	mlog_sys_shutdown();
	kset_unregister(o2cb_kset);
}

int o2cb_sys_init(void)
{
	int ret;

	o2cb_kset = kset_create_and_add("o2cb", NULL, fs_kobj);
	if (!o2cb_kset)
		return -ENOMEM;

	ret = sysfs_create_group(&o2cb_kset->kobj, &o2cb_attr_group);
	if (ret)
		goto error;

	ret = mlog_sys_init(o2cb_kset);
	if (ret)
		goto error;
	return 0;
error:
	kset_unregister(o2cb_kset);
	return ret;
}
