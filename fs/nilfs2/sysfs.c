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
 *                        NILFS device attrs                            *
 ************************************************************************/

static
ssize_t nilfs_dev_revision_show(struct nilfs_dev_attr *attr,
				struct the_nilfs *nilfs,
				char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	u32 major = le32_to_cpu(sbp[0]->s_rev_level);
	u16 minor = le16_to_cpu(sbp[0]->s_minor_rev_level);

	return snprintf(buf, PAGE_SIZE, "%d.%d\n", major, minor);
}

static
ssize_t nilfs_dev_blocksize_show(struct nilfs_dev_attr *attr,
				 struct the_nilfs *nilfs,
				 char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", nilfs->ns_blocksize);
}

static
ssize_t nilfs_dev_device_size_show(struct nilfs_dev_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	u64 dev_size = le64_to_cpu(sbp[0]->s_dev_size);

	return snprintf(buf, PAGE_SIZE, "%llu\n", dev_size);
}

static
ssize_t nilfs_dev_free_blocks_show(struct nilfs_dev_attr *attr,
				   struct the_nilfs *nilfs,
				   char *buf)
{
	sector_t free_blocks = 0;

	nilfs_count_free_blocks(nilfs, &free_blocks);
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)free_blocks);
}

static
ssize_t nilfs_dev_uuid_show(struct nilfs_dev_attr *attr,
			    struct the_nilfs *nilfs,
			    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	return snprintf(buf, PAGE_SIZE, "%pUb\n", sbp[0]->s_uuid);
}

static
ssize_t nilfs_dev_volume_name_show(struct nilfs_dev_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	return scnprintf(buf, sizeof(sbp[0]->s_volume_name), "%s\n",
			 sbp[0]->s_volume_name);
}

static const char dev_readme_str[] =
	"The <device> group contains attributes that describe file system\n"
	"partition's details.\n\n"
	"(1) revision\n\tshow NILFS file system revision.\n\n"
	"(2) blocksize\n\tshow volume block size in bytes.\n\n"
	"(3) device_size\n\tshow volume size in bytes.\n\n"
	"(4) free_blocks\n\tshow count of free blocks on volume.\n\n"
	"(5) uuid\n\tshow volume's UUID.\n\n"
	"(6) volume_name\n\tshow volume's name.\n\n";

static ssize_t nilfs_dev_README_show(struct nilfs_dev_attr *attr,
				     struct the_nilfs *nilfs,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, dev_readme_str);
}

NILFS_DEV_RO_ATTR(revision);
NILFS_DEV_RO_ATTR(blocksize);
NILFS_DEV_RO_ATTR(device_size);
NILFS_DEV_RO_ATTR(free_blocks);
NILFS_DEV_RO_ATTR(uuid);
NILFS_DEV_RO_ATTR(volume_name);
NILFS_DEV_RO_ATTR(README);

static struct attribute *nilfs_dev_attrs[] = {
	NILFS_DEV_ATTR_LIST(revision),
	NILFS_DEV_ATTR_LIST(blocksize),
	NILFS_DEV_ATTR_LIST(device_size),
	NILFS_DEV_ATTR_LIST(free_blocks),
	NILFS_DEV_ATTR_LIST(uuid),
	NILFS_DEV_ATTR_LIST(volume_name),
	NILFS_DEV_ATTR_LIST(README),
	NULL,
};

static ssize_t nilfs_dev_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	struct nilfs_dev_attr *a = container_of(attr, struct nilfs_dev_attr,
						attr);

	return a->show ? a->show(a, nilfs, buf) : 0;
}

static ssize_t nilfs_dev_attr_store(struct kobject *kobj,
				    struct attribute *attr,
				    const char *buf, size_t len)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	struct nilfs_dev_attr *a = container_of(attr, struct nilfs_dev_attr,
						attr);

	return a->store ? a->store(a, nilfs, buf, len) : 0;
}

static void nilfs_dev_attr_release(struct kobject *kobj)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	complete(&nilfs->ns_dev_kobj_unregister);
}

static const struct sysfs_ops nilfs_dev_attr_ops = {
	.show	= nilfs_dev_attr_show,
	.store	= nilfs_dev_attr_store,
};

static struct kobj_type nilfs_dev_ktype = {
	.default_attrs	= nilfs_dev_attrs,
	.sysfs_ops	= &nilfs_dev_attr_ops,
	.release	= nilfs_dev_attr_release,
};

int nilfs_sysfs_create_device_group(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	int err;

	nilfs->ns_dev_kobj.kset = nilfs_kset;
	init_completion(&nilfs->ns_dev_kobj_unregister);
	err = kobject_init_and_add(&nilfs->ns_dev_kobj, &nilfs_dev_ktype, NULL,
				    "%s", sb->s_id);
	if (err)
		goto failed_create_device_group;

	return 0;

failed_create_device_group:
	return err;
}

void nilfs_sysfs_delete_device_group(struct the_nilfs *nilfs)
{
	kobject_del(&nilfs->ns_dev_kobj);
}

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
