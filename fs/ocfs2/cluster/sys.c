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

#include "ocfs2_nodemanager.h"
#include "masklog.h"
#include "sys.h"

struct o2cb_attribute {
	struct attribute	attr;
	ssize_t (*show)(char *buf);
	ssize_t (*store)(const char *buf, size_t count);
};

#define O2CB_ATTR(_name, _mode, _show, _store)	\
struct o2cb_attribute o2cb_attr_##_name = __ATTR(_name, _mode, _show, _store)

#define to_o2cb_subsys(k) container_of(to_kset(k), struct subsystem, kset)
#define to_o2cb_attr(_attr) container_of(_attr, struct o2cb_attribute, attr)

static ssize_t o2cb_interface_revision_show(char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", O2NM_API_VERSION);
}

static O2CB_ATTR(interface_revision, S_IFREG | S_IRUGO, o2cb_interface_revision_show, NULL);

static struct attribute *o2cb_attrs[] = {
	&o2cb_attr_interface_revision.attr,
	NULL,
};

static ssize_t
o2cb_show(struct kobject * kobj, struct attribute * attr, char * buffer);
static ssize_t
o2cb_store(struct kobject * kobj, struct attribute * attr,
	   const char * buffer, size_t count);
static struct sysfs_ops o2cb_sysfs_ops = {
	.show	= o2cb_show,
	.store	= o2cb_store,
};

static struct kobj_type o2cb_subsys_type = {
	.default_attrs	= o2cb_attrs,
	.sysfs_ops	= &o2cb_sysfs_ops,
};

/* gives us o2cb_subsys */
static decl_subsys(o2cb, NULL, NULL);

static ssize_t
o2cb_show(struct kobject * kobj, struct attribute * attr, char * buffer)
{
	struct o2cb_attribute *o2cb_attr = to_o2cb_attr(attr);
	struct subsystem *sbs = to_o2cb_subsys(kobj);

	BUG_ON(sbs != &o2cb_subsys);

	if (o2cb_attr->show)
		return o2cb_attr->show(buffer);
	return -EIO;
}

static ssize_t
o2cb_store(struct kobject * kobj, struct attribute * attr,
	     const char * buffer, size_t count)
{
	struct o2cb_attribute *o2cb_attr = to_o2cb_attr(attr);
	struct subsystem *sbs = to_o2cb_subsys(kobj);

	BUG_ON(sbs != &o2cb_subsys);

	if (o2cb_attr->store)
		return o2cb_attr->store(buffer, count);
	return -EIO;
}

void o2cb_sys_shutdown(void)
{
	mlog_sys_shutdown();
	subsystem_unregister(&o2cb_subsys);
}

int o2cb_sys_init(void)
{
	int ret;

	o2cb_subsys.kset.kobj.ktype = &o2cb_subsys_type;
	ret = subsystem_register(&o2cb_subsys);
	if (ret)
		return ret;

	ret = mlog_sys_init(&o2cb_subsys);
	if (ret)
		subsystem_unregister(&o2cb_subsys);
	return ret;
}
