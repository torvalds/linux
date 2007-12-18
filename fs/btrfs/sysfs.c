/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
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
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/kobject.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static ssize_t root_blocks_used_show(struct btrfs_root *root, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)btrfs_root_used(&root->root_item));
}

static ssize_t root_block_limit_show(struct btrfs_root *root, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)btrfs_root_limit(&root->root_item));
}

static ssize_t super_blocks_used_show(struct btrfs_fs_info *fs, char *buf)
{

	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)btrfs_super_bytes_used(&fs->super_copy));
}

static ssize_t super_total_blocks_show(struct btrfs_fs_info *fs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)btrfs_super_total_bytes(&fs->super_copy));
}

static ssize_t super_blocksize_show(struct btrfs_fs_info *fs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)btrfs_super_sectorsize(&fs->super_copy));
}

/* this is for root attrs (subvols/snapshots) */
struct btrfs_root_attr {
	struct attribute attr;
	ssize_t (*show)(struct btrfs_root *, char *);
	ssize_t (*store)(struct btrfs_root *, const char *, size_t);
};

#define ROOT_ATTR(name, mode, show, store) \
static struct btrfs_root_attr btrfs_root_attr_##name = __ATTR(name, mode, show, store)

ROOT_ATTR(blocks_used,	0444,	root_blocks_used_show,	NULL);
ROOT_ATTR(block_limit,	0644,	root_block_limit_show,	NULL);

static struct attribute *btrfs_root_attrs[] = {
	&btrfs_root_attr_blocks_used.attr,
	&btrfs_root_attr_block_limit.attr,
	NULL,
};

/* this is for super attrs (actual full fs) */
struct btrfs_super_attr {
	struct attribute attr;
	ssize_t (*show)(struct btrfs_fs_info *, char *);
	ssize_t (*store)(struct btrfs_fs_info *, const char *, size_t);
};

#define SUPER_ATTR(name, mode, show, store) \
static struct btrfs_super_attr btrfs_super_attr_##name = __ATTR(name, mode, show, store)

SUPER_ATTR(blocks_used,		0444,	super_blocks_used_show,		NULL);
SUPER_ATTR(total_blocks,	0444,	super_total_blocks_show,	NULL);
SUPER_ATTR(blocksize,		0444,	super_blocksize_show,		NULL);

static struct attribute *btrfs_super_attrs[] = {
	&btrfs_super_attr_blocks_used.attr,
	&btrfs_super_attr_total_blocks.attr,
	&btrfs_super_attr_blocksize.attr,
	NULL,
};

static ssize_t btrfs_super_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct btrfs_fs_info *fs = container_of(kobj, struct btrfs_fs_info,
						super_kobj);
	struct btrfs_super_attr *a = container_of(attr,
						  struct btrfs_super_attr,
						  attr);

	return a->show ? a->show(fs, buf) : 0;
}

static ssize_t btrfs_super_attr_store(struct kobject *kobj,
				     struct attribute *attr,
				     const char *buf, size_t len)
{
	struct btrfs_fs_info *fs = container_of(kobj, struct btrfs_fs_info,
						super_kobj);
	struct btrfs_super_attr *a = container_of(attr,
						  struct btrfs_super_attr,
						  attr);

	return a->store ? a->store(fs, buf, len) : 0;
}

static ssize_t btrfs_root_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct btrfs_root *root = container_of(kobj, struct btrfs_root,
						root_kobj);
	struct btrfs_root_attr *a = container_of(attr,
						 struct btrfs_root_attr,
						 attr);

	return a->show ? a->show(root, buf) : 0;
}

static ssize_t btrfs_root_attr_store(struct kobject *kobj,
				     struct attribute *attr,
				     const char *buf, size_t len)
{
	struct btrfs_root *root = container_of(kobj, struct btrfs_root,
						root_kobj);
	struct btrfs_root_attr *a = container_of(attr,
						 struct btrfs_root_attr,
						 attr);
	return a->store ? a->store(root, buf, len) : 0;
}

static void btrfs_super_release(struct kobject *kobj)
{
	struct btrfs_fs_info *fs = container_of(kobj, struct btrfs_fs_info,
						super_kobj);
	complete(&fs->kobj_unregister);
}

static void btrfs_root_release(struct kobject *kobj)
{
	struct btrfs_root *root = container_of(kobj, struct btrfs_root,
						root_kobj);
	complete(&root->kobj_unregister);
}

static struct sysfs_ops btrfs_super_attr_ops = {
	.show	= btrfs_super_attr_show,
	.store	= btrfs_super_attr_store,
};

static struct sysfs_ops btrfs_root_attr_ops = {
	.show	= btrfs_root_attr_show,
	.store	= btrfs_root_attr_store,
};

static struct kobj_type btrfs_root_ktype = {
	.default_attrs	= btrfs_root_attrs,
	.sysfs_ops	= &btrfs_root_attr_ops,
	.release	= btrfs_root_release,
};

static struct kobj_type btrfs_super_ktype = {
	.default_attrs	= btrfs_super_attrs,
	.sysfs_ops	= &btrfs_super_attr_ops,
	.release	= btrfs_super_release,
};

static struct kset btrfs_kset;

int btrfs_sysfs_add_super(struct btrfs_fs_info *fs)
{
	int error;
	char *name;
	char c;
	int len = strlen(fs->sb->s_id) + 1;
	int i;

	name = kmalloc(len, GFP_NOFS);
	if (!name) {
		error = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < len; i++) {
		c = fs->sb->s_id[i];
		if (c == '/' || c == '\\')
			c = '!';
		name[i] = c;
	}
	name[len] = '\0';

	fs->super_kobj.kset = &btrfs_kset;
	fs->super_kobj.ktype = &btrfs_super_ktype;

	error = kobject_set_name(&fs->super_kobj, "%s", name);
	if (error)
		goto fail;

	error = kobject_register(&fs->super_kobj);
	if (error)
		goto fail;

	kfree(name);
	return 0;

fail:
	kfree(name);
	printk(KERN_ERR "btrfs: sysfs creation for super failed\n");
	return error;
}

int btrfs_sysfs_add_root(struct btrfs_root *root)
{
	int error;

	root->root_kobj.ktype = &btrfs_root_ktype;
	root->root_kobj.parent = &root->fs_info->super_kobj;

	error = kobject_set_name(&root->root_kobj, "%s", root->name);
	if (error) {
		goto fail;
	}

	error = kobject_register(&root->root_kobj);
	if (error)
		goto fail;

	return 0;

fail:
	printk(KERN_ERR "btrfs: sysfs creation for root failed\n");
	return error;
}

void btrfs_sysfs_del_root(struct btrfs_root *root)
{
	kobject_unregister(&root->root_kobj);
	wait_for_completion(&root->kobj_unregister);
}

void btrfs_sysfs_del_super(struct btrfs_fs_info *fs)
{
	kobject_unregister(&fs->super_kobj);
	wait_for_completion(&fs->kobj_unregister);
}

int btrfs_init_sysfs()
{
	kobj_set_kset_s(&btrfs_kset, fs_subsys);
	kobject_set_name(&btrfs_kset.kobj, "btrfs");
	return kset_register(&btrfs_kset);
}

void btrfs_exit_sysfs()
{
	kset_unregister(&btrfs_kset);
}
