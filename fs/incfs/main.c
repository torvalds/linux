// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>

#include <uapi/linux/incrementalfs.h>

#include "vfs.h"

#define INCFS_NODE_FEATURES "features"

static struct file_system_type incfs_fs_type = {
	.owner = THIS_MODULE,
	.name = INCFS_NAME,
	.mount = incfs_mount_fs,
	.kill_sb = incfs_kill_sb,
	.fs_flags = 0
};

static struct kobject *sysfs_root, *featurefs_root;

static ssize_t corefs_show(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buff)
{
	return snprintf(buff, PAGE_SIZE, "supported\n");
}

static struct kobj_attribute corefs_attr = __ATTR_RO(corefs);

static ssize_t report_uid_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buff)
{
	return snprintf(buff, PAGE_SIZE, "supported\n");
}

static struct kobj_attribute report_uid_attr = __ATTR_RO(report_uid);

static struct attribute *attributes[] = {
	&corefs_attr.attr,
	&report_uid_attr.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attributes,
};

static int __init init_sysfs(void)
{
	int res = 0;

	sysfs_root = kobject_create_and_add(INCFS_NAME, fs_kobj);
	if (!sysfs_root)
		return -ENOMEM;

	featurefs_root = kobject_create_and_add(INCFS_NODE_FEATURES,
						sysfs_root);
	if (!featurefs_root)
		return -ENOMEM;

	res = sysfs_create_group(featurefs_root, &attr_group);
	if (res) {
		kobject_put(sysfs_root);
		sysfs_root = NULL;
	}
	return res;
}

static void cleanup_sysfs(void)
{
	if (featurefs_root) {
		sysfs_remove_group(featurefs_root, &attr_group);
		kobject_put(featurefs_root);
		featurefs_root = NULL;
	}

	if (sysfs_root) {
		kobject_put(sysfs_root);
		sysfs_root = NULL;
	}
}

static int __init init_incfs_module(void)
{
	int err = 0;

	err = init_sysfs();
	if (err)
		return err;

	err = register_filesystem(&incfs_fs_type);
	if (err)
		cleanup_sysfs();

	return err;
}

static void __exit cleanup_incfs_module(void)
{
	cleanup_sysfs();
	unregister_filesystem(&incfs_fs_type);
}

module_init(init_incfs_module);
module_exit(cleanup_incfs_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Eugene Zemtsov <ezemtsov@google.com>");
MODULE_DESCRIPTION("Incremental File System");
