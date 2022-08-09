// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2021 Cisco Systems
 *
 * Author: Stefan Schaeckeler
 */


#include <linux/fs.h>
#include "ubifs.h"

enum attr_id_t {
	attr_errors_magic,
	attr_errors_node,
	attr_errors_crc,
};

struct ubifs_attr {
	struct attribute attr;
	enum attr_id_t attr_id;
};

#define UBIFS_ATTR(_name, _mode, _id)					\
static struct ubifs_attr ubifs_attr_##_name = {				\
	.attr = {.name = __stringify(_name), .mode = _mode },		\
	.attr_id = attr_##_id,						\
}

#define UBIFS_ATTR_FUNC(_name, _mode) UBIFS_ATTR(_name, _mode, _name)

UBIFS_ATTR_FUNC(errors_magic, 0444);
UBIFS_ATTR_FUNC(errors_crc, 0444);
UBIFS_ATTR_FUNC(errors_node, 0444);

#define ATTR_LIST(name) (&ubifs_attr_##name.attr)

static struct attribute *ubifs_attrs[] = {
	ATTR_LIST(errors_magic),
	ATTR_LIST(errors_node),
	ATTR_LIST(errors_crc),
	NULL,
};

static ssize_t ubifs_attr_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct ubifs_info *sbi = container_of(kobj, struct ubifs_info,
					      kobj);

	struct ubifs_attr *a = container_of(attr, struct ubifs_attr, attr);

	switch (a->attr_id) {
	case attr_errors_magic:
		return sysfs_emit(buf, "%u\n", sbi->stats->magic_errors);
	case attr_errors_node:
		return sysfs_emit(buf, "%u\n", sbi->stats->node_errors);
	case attr_errors_crc:
		return sysfs_emit(buf, "%u\n", sbi->stats->crc_errors);
	}
	return 0;
};

static void ubifs_sb_release(struct kobject *kobj)
{
	struct ubifs_info *c = container_of(kobj, struct ubifs_info, kobj);

	complete(&c->kobj_unregister);
}

static const struct sysfs_ops ubifs_attr_ops = {
	.show	= ubifs_attr_show,
};

static struct kobj_type ubifs_sb_ktype = {
	.default_attrs	= ubifs_attrs,
	.sysfs_ops	= &ubifs_attr_ops,
	.release	= ubifs_sb_release,
};

static struct kobj_type ubifs_ktype = {
	.sysfs_ops	= &ubifs_attr_ops,
};

static struct kset ubifs_kset = {
	.kobj	= {.ktype = &ubifs_ktype},
};

int ubifs_sysfs_register(struct ubifs_info *c)
{
	int ret, n;
	char dfs_dir_name[UBIFS_DFS_DIR_LEN+1];

	c->stats = kzalloc(sizeof(struct ubifs_stats_info), GFP_KERNEL);
	if (!c->stats) {
		ret = -ENOMEM;
		goto out_last;
	}
	n = snprintf(dfs_dir_name, UBIFS_DFS_DIR_LEN + 1, UBIFS_DFS_DIR_NAME,
		     c->vi.ubi_num, c->vi.vol_id);

	if (n > UBIFS_DFS_DIR_LEN) {
		/* The array size is too small */
		ret = -EINVAL;
		goto out_free;
	}

	c->kobj.kset = &ubifs_kset;
	init_completion(&c->kobj_unregister);

	ret = kobject_init_and_add(&c->kobj, &ubifs_sb_ktype, NULL,
				   "%s", dfs_dir_name);
	if (ret)
		goto out_put;

	return 0;

out_put:
	kobject_put(&c->kobj);
	wait_for_completion(&c->kobj_unregister);
out_free:
	kfree(c->stats);
out_last:
	ubifs_err(c, "cannot create sysfs entry for ubifs%d_%d, error %d\n",
		  c->vi.ubi_num, c->vi.vol_id, ret);
	return ret;
}

void ubifs_sysfs_unregister(struct ubifs_info *c)
{
	kobject_del(&c->kobj);
	kobject_put(&c->kobj);
	wait_for_completion(&c->kobj_unregister);

	kfree(c->stats);
}

int __init ubifs_sysfs_init(void)
{
	int ret;

	kobject_set_name(&ubifs_kset.kobj, "ubifs");
	ubifs_kset.kobj.parent = fs_kobj;
	ret = kset_register(&ubifs_kset);

	return ret;
}

void ubifs_sysfs_exit(void)
{
	kset_unregister(&ubifs_kset);
}
