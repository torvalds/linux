/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * sys.c
 *
 * OCFS2 cluster sysfs interface
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation,
 * version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
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
	__ATTR(interface_revision, S_IFREG | S_IRUGO, version_show, NULL);

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

	o2cb_kset = kset_create_and_add("o2cb", NULL, NULL);
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
