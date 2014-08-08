/*
 * sysfs.c - sysfs support implementation.
 *
 * Copyright (C) 2005-2014 Nippon Telegraph and Telephone Corporation.
 * Copyright (C) 2014 HGST, Inc., a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Vyacheslav Dubeyko <Vyacheslav.Dubeyko@hgst.com>
 */

#include <linux/kobject.h>

#include "nilfs.h"
#include "mdt.h"
#include "sufile.h"
#include "cpfile.h"
#include "sysfs.h"

/* /sys/fs/<nilfs>/ */
static struct kset *nilfs_kset;

#define NILFS_SHOW_TIME(time_t_val, buf) ({ \
		struct tm res; \
		int count = 0; \
		time_to_tm(time_t_val, 0, &res); \
		res.tm_year += 1900; \
		res.tm_mon += 1; \
		count = scnprintf(buf, PAGE_SIZE, \
				    "%ld-%.2d-%.2d %.2d:%.2d:%.2d\n", \
				    res.tm_year, res.tm_mon, res.tm_mday, \
				    res.tm_hour, res.tm_min, res.tm_sec);\
		count; \
})

/************************************************************************
 *                        NILFS feature attrs                           *
 ************************************************************************/

static ssize_t nilfs_feature_revision_show(struct kobject *kobj,
					    struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d.%d\n",
			NILFS_CURRENT_REV, NILFS_MINOR_REV);
}

static const char features_readme_str[] =
	"The features group contains attributes that describe NILFS file\n"
	"system driver features.\n\n"
	"(1) revision\n\tshow current revision of NILFS file system driver.\n";

static ssize_t nilfs_feature_README_show(struct kobject *kobj,
					 struct attribute *attr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE, features_readme_str);
}

NILFS_FEATURE_RO_ATTR(revision);
NILFS_FEATURE_RO_ATTR(README);

static struct attribute *nilfs_feature_attrs[] = {
	NILFS_FEATURE_ATTR_LIST(revision),
	NILFS_FEATURE_ATTR_LIST(README),
	NULL,
};

static const struct attribute_group nilfs_feature_attr_group = {
	.name = "features",
	.attrs = nilfs_feature_attrs,
};

int __init nilfs_sysfs_init(void)
{
	int err;

	nilfs_kset = kset_create_and_add(NILFS_ROOT_GROUP_NAME, NULL, fs_kobj);
	if (!nilfs_kset) {
		err = -ENOMEM;
		printk(KERN_ERR "NILFS: unable to create sysfs entry: err %d\n",
			err);
		goto failed_sysfs_init;
	}

	err = sysfs_create_group(&nilfs_kset->kobj, &nilfs_feature_attr_group);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: unable to create feature group: err %d\n",
			err);
		goto cleanup_sysfs_init;
	}

	return 0;

cleanup_sysfs_init:
	kset_unregister(nilfs_kset);

failed_sysfs_init:
	return err;
}

void nilfs_sysfs_exit(void)
{
	sysfs_remove_group(&nilfs_kset->kobj, &nilfs_feature_attr_group);
	kset_unregister(nilfs_kset);
}
