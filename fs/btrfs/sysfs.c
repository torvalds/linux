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
#include <linux/bug.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "sysfs.h"

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj);

static u64 get_features(struct btrfs_fs_info *fs_info,
			enum btrfs_feature_set set)
{
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	if (set == FEAT_COMPAT)
		return btrfs_super_compat_flags(disk_super);
	else if (set == FEAT_COMPAT_RO)
		return btrfs_super_compat_ro_flags(disk_super);
	else
		return btrfs_super_incompat_flags(disk_super);
}

static ssize_t btrfs_feature_attr_show(struct kobject *kobj,
				       struct kobj_attribute *a, char *buf)
{
	int val = 0;
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	if (fs_info) {
		struct btrfs_feature_attr *fa = to_btrfs_feature_attr(a);
		u64 features = get_features(fs_info, fa->feature_set);
		if (features & fa->feature_bit)
			val = 1;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static umode_t btrfs_feature_visible(struct kobject *kobj,
				     struct attribute *attr, int unused)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	umode_t mode = attr->mode;

	if (fs_info) {
		struct btrfs_feature_attr *fa;
		u64 features;

		fa = attr_to_btrfs_feature_attr(attr);
		features = get_features(fs_info, fa->feature_set);

		if (!(features & fa->feature_bit))
			mode = 0;
	}

	return mode;
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
	.is_visible = btrfs_feature_visible,
	.attrs = btrfs_supported_feature_attrs,
};

static void btrfs_release_super_kobj(struct kobject *kobj)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	complete(&fs_info->kobj_unregister);
}

static struct kobj_type btrfs_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= btrfs_release_super_kobj,
};

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj)
{
	if (kobj->ktype != &btrfs_ktype)
		return NULL;
	return container_of(kobj, struct btrfs_fs_info, super_kobj);
}

void btrfs_sysfs_remove_one(struct btrfs_fs_info *fs_info)
{
	kobject_del(&fs_info->super_kobj);
	kobject_put(&fs_info->super_kobj);
	wait_for_completion(&fs_info->kobj_unregister);
}

const char * const btrfs_feature_set_names[3] = {
	[FEAT_COMPAT]	 = "compat",
	[FEAT_COMPAT_RO] = "compat_ro",
	[FEAT_INCOMPAT]	 = "incompat",
};

#define NUM_FEATURE_BITS 64
static char btrfs_unknown_feature_names[3][NUM_FEATURE_BITS][13];
static struct btrfs_feature_attr btrfs_feature_attrs[3][NUM_FEATURE_BITS];

static void init_feature_attrs(void)
{
	struct btrfs_feature_attr *fa;
	int set, i;

	BUILD_BUG_ON(ARRAY_SIZE(btrfs_unknown_feature_names) !=
		     ARRAY_SIZE(btrfs_feature_attrs));
	BUILD_BUG_ON(ARRAY_SIZE(btrfs_unknown_feature_names[0]) !=
		     ARRAY_SIZE(btrfs_feature_attrs[0]));

	for (i = 0; btrfs_supported_feature_attrs[i]; i++) {
		struct btrfs_feature_attr *sfa;
		struct attribute *a = btrfs_supported_feature_attrs[i];
		sfa = attr_to_btrfs_feature_attr(a);
		fa = &btrfs_feature_attrs[sfa->feature_set][sfa->feature_bit];

		fa->kobj_attr.attr.name = sfa->kobj_attr.attr.name;
	}

	for (set = 0; set < FEAT_MAX; set++) {
		for (i = 0; i < ARRAY_SIZE(btrfs_feature_attrs[set]); i++) {
			char *name = btrfs_unknown_feature_names[set][i];
			fa = &btrfs_feature_attrs[set][i];

			if (fa->kobj_attr.attr.name)
				continue;

			snprintf(name, 13, "%s:%u",
				 btrfs_feature_set_names[set], i);

			fa->kobj_attr.attr.name = name;
			fa->kobj_attr.attr.mode = S_IRUGO;
			fa->feature_set = set;
			fa->feature_bit = 1ULL << i;
		}
	}
}

static u64 supported_feature_masks[3] = {
	[FEAT_COMPAT]    = BTRFS_FEATURE_COMPAT_SUPP,
	[FEAT_COMPAT_RO] = BTRFS_FEATURE_COMPAT_RO_SUPP,
	[FEAT_INCOMPAT]  = BTRFS_FEATURE_INCOMPAT_SUPP,
};

static int add_unknown_feature_attrs(struct btrfs_fs_info *fs_info)
{
	int set;

	for (set = 0; set < FEAT_MAX; set++) {
		int i, count, ret, index = 0;
		struct attribute **attrs;
		struct attribute_group agroup = {
			.name = "features",
		};
		u64 features = get_features(fs_info, set);
		features &= ~supported_feature_masks[set];

		count = hweight64(features);

		if (!count)
			continue;

		attrs = kcalloc(count + 1, sizeof(void *), GFP_KERNEL);

		for (i = 0; i < NUM_FEATURE_BITS; i++) {
			struct btrfs_feature_attr *fa;

			if (!(features & (1ULL << i)))
				continue;

			fa = &btrfs_feature_attrs[set][i];
			attrs[index++] = &fa->kobj_attr.attr;
		}

		attrs[index] = NULL;
		agroup.attrs = attrs;

		ret = sysfs_merge_group(&fs_info->super_kobj, &agroup);
		kfree(attrs);
		if (ret)
			return ret;
	}
	return 0;
}

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

int btrfs_sysfs_add_one(struct btrfs_fs_info *fs_info)
{
	int error;

	init_completion(&fs_info->kobj_unregister);
	fs_info->super_kobj.kset = btrfs_kset;
	error = kobject_init_and_add(&fs_info->super_kobj, &btrfs_ktype, NULL,
				     "%pU", fs_info->fsid);

	error = sysfs_create_group(&fs_info->super_kobj,
				   &btrfs_feature_attr_group);
	if (error)
		goto failure;

	error = add_unknown_feature_attrs(fs_info);
	if (error)
		goto failure;

	return 0;
failure:
	btrfs_sysfs_remove_one(fs_info);
	return error;
}

int btrfs_init_sysfs(void)
{
	int ret;
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	init_feature_attrs();

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

