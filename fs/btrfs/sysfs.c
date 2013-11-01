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
#include <linux/kobject.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "sysfs.h"

static ssize_t btrfs_feature_attr_show(struct kobject *kobj,
				       struct kobj_attribute *a, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0\n");
}

BTRFS_FEAT_ATTR_INCOMPAT(mixed_backref, MIXED_BACKREF);
BTRFS_FEAT_ATTR_INCOMPAT(default_subvol, DEFAULT_SUBVOL);
BTRFS_FEAT_ATTR_INCOMPAT(mixed_groups, MIXED_GROUPS);
BTRFS_FEAT_ATTR_INCOMPAT(compress_lzo, COMPRESS_LZO);
BTRFS_FEAT_ATTR_INCOMPAT(compress_lzov2, COMPRESS_LZOv2);
BTRFS_FEAT_ATTR_INCOMPAT(big_metadata, BIG_METADATA);
BTRFS_FEAT_ATTR_INCOMPAT(extended_iref, EXTENDED_IREF);
BTRFS_FEAT_ATTR_INCOMPAT(raid56, RAID56);
BTRFS_FEAT_ATTR_INCOMPAT(skinny_metadata, SKINNY_METADATA);

static struct attribute *btrfs_supported_feature_attrs[] = {
	BTRFS_FEAT_ATTR_PTR(mixed_backref),
	BTRFS_FEAT_ATTR_PTR(default_subvol),
	BTRFS_FEAT_ATTR_PTR(mixed_groups),
	BTRFS_FEAT_ATTR_PTR(compress_lzo),
	BTRFS_FEAT_ATTR_PTR(compress_lzov2),
	BTRFS_FEAT_ATTR_PTR(big_metadata),
	BTRFS_FEAT_ATTR_PTR(extended_iref),
	BTRFS_FEAT_ATTR_PTR(raid56),
	BTRFS_FEAT_ATTR_PTR(skinny_metadata),
	NULL
};

static const struct attribute_group btrfs_feature_attr_group = {
	.name = "features",
	.attrs = btrfs_supported_feature_attrs,
};

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

int btrfs_init_sysfs(void)
{
	int ret;
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	ret = sysfs_create_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	if (ret) {
		kset_unregister(btrfs_kset);
		return ret;
	}

	return 0;
}

void btrfs_exit_sysfs(void)
{
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	kset_unregister(btrfs_kset);
}

