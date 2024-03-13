// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */
#include <linux/fs.h>
#include <linux/kobject.h>

#include <uapi/linux/incrementalfs.h>

#include "sysfs.h"
#include "data_mgmt.h"
#include "vfs.h"

/******************************************************************************
 * Define sys/fs/incrementalfs & sys/fs/incrementalfs/features
 *****************************************************************************/
#define INCFS_NODE_FEATURES "features"
#define INCFS_NODE_INSTANCES "instances"

static struct kobject *sysfs_root;
static struct kobject *features_node;
static struct kobject *instances_node;

#define DECLARE_FEATURE_FLAG(name)					\
	static ssize_t name##_show(struct kobject *kobj,		\
			 struct kobj_attribute *attr, char *buff)	\
{									\
	return sysfs_emit(buff, "supported\n");				\
}									\
									\
static struct kobj_attribute name##_attr = __ATTR_RO(name)

DECLARE_FEATURE_FLAG(corefs);
DECLARE_FEATURE_FLAG(zstd);
DECLARE_FEATURE_FLAG(v2);
DECLARE_FEATURE_FLAG(bugfix_throttling);
DECLARE_FEATURE_FLAG(bugfix_inode_eviction);

static struct attribute *attributes[] = {
	&corefs_attr.attr,
	&zstd_attr.attr,
	&v2_attr.attr,
	&bugfix_throttling_attr.attr,
	&bugfix_inode_eviction_attr.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attributes,
};

int __init incfs_init_sysfs(void)
{
	int res = -ENOMEM;

	sysfs_root = kobject_create_and_add(INCFS_NAME, fs_kobj);
	if (!sysfs_root)
		return -ENOMEM;

	instances_node = kobject_create_and_add(INCFS_NODE_INSTANCES,
						sysfs_root);
	if (!instances_node)
		goto err_put_root;

	features_node = kobject_create_and_add(INCFS_NODE_FEATURES,
						sysfs_root);
	if (!features_node)
		goto err_put_instances;

	res = sysfs_create_group(features_node, &attr_group);
	if (res)
		goto err_put_features;

	return 0;

err_put_features:
	kobject_put(features_node);
err_put_instances:
	kobject_put(instances_node);
err_put_root:
	kobject_put(sysfs_root);

	return res;
}

void incfs_cleanup_sysfs(void)
{
	if (features_node) {
		sysfs_remove_group(features_node, &attr_group);
		kobject_put(features_node);
	}

	kobject_put(instances_node);
	kobject_put(sysfs_root);
}

/******************************************************************************
 * Define sys/fs/incrementalfs/instances/<name>/
 *****************************************************************************/
#define __DECLARE_STATUS_FLAG(name)					\
static ssize_t name##_show(struct kobject *kobj,			\
			 struct kobj_attribute *attr, char *buff)	\
{									\
	struct incfs_sysfs_node *node = container_of(kobj,		\
			struct incfs_sysfs_node, isn_sysfs_node);	\
									\
	return sysfs_emit(buff, "%d\n", node->isn_mi->mi_##name);	\
}									\
									\
static struct kobj_attribute name##_attr = __ATTR_RO(name)

#define __DECLARE_STATUS_FLAG64(name)					\
static ssize_t name##_show(struct kobject *kobj,			\
			 struct kobj_attribute *attr, char *buff)	\
{									\
	struct incfs_sysfs_node *node = container_of(kobj,		\
			struct incfs_sysfs_node, isn_sysfs_node);	\
									\
	return sysfs_emit(buff, "%lld\n", node->isn_mi->mi_##name);	\
}									\
									\
static struct kobj_attribute name##_attr = __ATTR_RO(name)

__DECLARE_STATUS_FLAG(reads_failed_timed_out);
__DECLARE_STATUS_FLAG(reads_failed_hash_verification);
__DECLARE_STATUS_FLAG(reads_failed_other);
__DECLARE_STATUS_FLAG(reads_delayed_pending);
__DECLARE_STATUS_FLAG64(reads_delayed_pending_us);
__DECLARE_STATUS_FLAG(reads_delayed_min);
__DECLARE_STATUS_FLAG64(reads_delayed_min_us);

static struct attribute *mount_attributes[] = {
	&reads_failed_timed_out_attr.attr,
	&reads_failed_hash_verification_attr.attr,
	&reads_failed_other_attr.attr,
	&reads_delayed_pending_attr.attr,
	&reads_delayed_pending_us_attr.attr,
	&reads_delayed_min_attr.attr,
	&reads_delayed_min_us_attr.attr,
	NULL,
};

static void incfs_sysfs_release(struct kobject *kobj)
{
	struct incfs_sysfs_node *node = container_of(kobj,
				struct incfs_sysfs_node, isn_sysfs_node);

	complete(&node->isn_completion);
}

static const struct attribute_group mount_attr_group = {
	.attrs = mount_attributes,
};

static struct kobj_type incfs_kobj_node_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= &incfs_sysfs_release,
};

struct incfs_sysfs_node *incfs_add_sysfs_node(const char *name,
					      struct mount_info *mi)
{
	struct incfs_sysfs_node *node = NULL;
	int error;

	if (!name)
		return NULL;

	node = kzalloc(sizeof(*node), GFP_NOFS);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->isn_mi = mi;

	init_completion(&node->isn_completion);
	kobject_init(&node->isn_sysfs_node, &incfs_kobj_node_ktype);
	error = kobject_add(&node->isn_sysfs_node, instances_node, "%s", name);
	if (error)
		goto err;

	error = sysfs_create_group(&node->isn_sysfs_node, &mount_attr_group);
	if (error)
		goto err;

	return node;

err:
	/*
	 * Note kobject_put always calls release, so incfs_sysfs_release will
	 * free node
	 */
	kobject_put(&node->isn_sysfs_node);
	return ERR_PTR(error);
}

void incfs_free_sysfs_node(struct incfs_sysfs_node *node)
{
	if (!node)
		return;

	sysfs_remove_group(&node->isn_sysfs_node, &mount_attr_group);
	kobject_put(&node->isn_sysfs_node);
	wait_for_completion_interruptible(&node->isn_completion);
	kfree(node);
}
