// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file system for zoned block devices exposing zones as files.
 *
 * Copyright (C) 2022 Western Digital Corporation or its affiliates.
 */
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>

#include "zonefs.h"

struct zonefs_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct zonefs_sb_info *sbi, char *buf);
};

#define ZONEFS_SYSFS_ATTR_RO(name) \
static struct zonefs_sysfs_attr zonefs_sysfs_attr_##name = __ATTR_RO(name)

#define ATTR_LIST(name) &zonefs_sysfs_attr_##name.attr

static ssize_t zonefs_sysfs_attr_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct zonefs_sb_info *sbi =
		container_of(kobj, struct zonefs_sb_info, s_kobj);
	struct zonefs_sysfs_attr *zonefs_attr =
		container_of(attr, struct zonefs_sysfs_attr, attr);

	if (!zonefs_attr->show)
		return 0;

	return zonefs_attr->show(sbi, buf);
}

static ssize_t max_wro_seq_files_show(struct zonefs_sb_info *sbi, char *buf)
{
	return sysfs_emit(buf, "%u\n", sbi->s_max_wro_seq_files);
}
ZONEFS_SYSFS_ATTR_RO(max_wro_seq_files);

static ssize_t nr_wro_seq_files_show(struct zonefs_sb_info *sbi, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&sbi->s_wro_seq_files));
}
ZONEFS_SYSFS_ATTR_RO(nr_wro_seq_files);

static ssize_t max_active_seq_files_show(struct zonefs_sb_info *sbi, char *buf)
{
	return sysfs_emit(buf, "%u\n", sbi->s_max_active_seq_files);
}
ZONEFS_SYSFS_ATTR_RO(max_active_seq_files);

static ssize_t nr_active_seq_files_show(struct zonefs_sb_info *sbi, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&sbi->s_active_seq_files));
}
ZONEFS_SYSFS_ATTR_RO(nr_active_seq_files);

static struct attribute *zonefs_sysfs_attrs[] = {
	ATTR_LIST(max_wro_seq_files),
	ATTR_LIST(nr_wro_seq_files),
	ATTR_LIST(max_active_seq_files),
	ATTR_LIST(nr_active_seq_files),
	NULL,
};
ATTRIBUTE_GROUPS(zonefs_sysfs);

static void zonefs_sysfs_sb_release(struct kobject *kobj)
{
	struct zonefs_sb_info *sbi =
		container_of(kobj, struct zonefs_sb_info, s_kobj);

	complete(&sbi->s_kobj_unregister);
}

static const struct sysfs_ops zonefs_sysfs_attr_ops = {
	.show	= zonefs_sysfs_attr_show,
};

static struct kobj_type zonefs_sb_ktype = {
	.default_groups = zonefs_sysfs_groups,
	.sysfs_ops	= &zonefs_sysfs_attr_ops,
	.release	= zonefs_sysfs_sb_release,
};

static struct kobject *zonefs_sysfs_root;

int zonefs_sysfs_register(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	int ret;

	init_completion(&sbi->s_kobj_unregister);
	ret = kobject_init_and_add(&sbi->s_kobj, &zonefs_sb_ktype,
				   zonefs_sysfs_root, "%s", sb->s_id);
	if (ret) {
		kobject_put(&sbi->s_kobj);
		wait_for_completion(&sbi->s_kobj_unregister);
		return ret;
	}

	sbi->s_sysfs_registered = true;

	return 0;
}

void zonefs_sysfs_unregister(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);

	if (!sbi || !sbi->s_sysfs_registered)
		return;

	kobject_del(&sbi->s_kobj);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
}

int __init zonefs_sysfs_init(void)
{
	zonefs_sysfs_root = kobject_create_and_add("zonefs", fs_kobj);
	if (!zonefs_sysfs_root)
		return -ENOMEM;

	return 0;
}

void zonefs_sysfs_exit(void)
{
	kobject_put(zonefs_sysfs_root);
	zonefs_sysfs_root = NULL;
}
