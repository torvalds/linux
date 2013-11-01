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
#include <linux/genhd.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "sysfs.h"
#include "volumes.h"

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

static void set_features(struct btrfs_fs_info *fs_info,
			 enum btrfs_feature_set set, u64 features)
{
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	if (set == FEAT_COMPAT)
		btrfs_set_super_compat_flags(disk_super, features);
	else if (set == FEAT_COMPAT_RO)
		btrfs_set_super_compat_ro_flags(disk_super, features);
	else
		btrfs_set_super_incompat_flags(disk_super, features);
}

static int can_modify_feature(struct btrfs_feature_attr *fa)
{
	int val = 0;
	u64 set, clear;
	switch (fa->feature_set) {
	case FEAT_COMPAT:
		set = BTRFS_FEATURE_COMPAT_SAFE_SET;
		clear = BTRFS_FEATURE_COMPAT_SAFE_CLEAR;
		break;
	case FEAT_COMPAT_RO:
		set = BTRFS_FEATURE_COMPAT_RO_SAFE_SET;
		clear = BTRFS_FEATURE_COMPAT_RO_SAFE_CLEAR;
		break;
	case FEAT_INCOMPAT:
		set = BTRFS_FEATURE_INCOMPAT_SAFE_SET;
		clear = BTRFS_FEATURE_INCOMPAT_SAFE_CLEAR;
		break;
	default:
		BUG();
	}

	if (set & fa->feature_bit)
		val |= 1;
	if (clear & fa->feature_bit)
		val |= 2;

	return val;
}

static ssize_t btrfs_feature_attr_show(struct kobject *kobj,
				       struct kobj_attribute *a, char *buf)
{
	int val = 0;
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	struct btrfs_feature_attr *fa = to_btrfs_feature_attr(a);
	if (fs_info) {
		u64 features = get_features(fs_info, fa->feature_set);
		if (features & fa->feature_bit)
			val = 1;
	} else
		val = can_modify_feature(fa);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t btrfs_feature_attr_store(struct kobject *kobj,
					struct kobj_attribute *a,
					const char *buf, size_t count)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_feature_attr *fa = to_btrfs_feature_attr(a);
	struct btrfs_trans_handle *trans;
	u64 features, set, clear;
	unsigned long val;
	int ret;

	fs_info = to_fs_info(kobj);
	if (!fs_info)
		return -EPERM;

	ret = kstrtoul(skip_spaces(buf), 0, &val);
	if (ret)
		return ret;

	if (fa->feature_set == FEAT_COMPAT) {
		set = BTRFS_FEATURE_COMPAT_SAFE_SET;
		clear = BTRFS_FEATURE_COMPAT_SAFE_CLEAR;
	} else if (fa->feature_set == FEAT_COMPAT_RO) {
		set = BTRFS_FEATURE_COMPAT_RO_SAFE_SET;
		clear = BTRFS_FEATURE_COMPAT_RO_SAFE_CLEAR;
	} else {
		set = BTRFS_FEATURE_INCOMPAT_SAFE_SET;
		clear = BTRFS_FEATURE_INCOMPAT_SAFE_CLEAR;
	}

	features = get_features(fs_info, fa->feature_set);

	/* Nothing to do */
	if ((val && (features & fa->feature_bit)) ||
	    (!val && !(features & fa->feature_bit)))
		return count;

	if ((val && !(set & fa->feature_bit)) ||
	    (!val && !(clear & fa->feature_bit))) {
		btrfs_info(fs_info,
			"%sabling feature %s on mounted fs is not supported.",
			val ? "En" : "Dis", fa->kobj_attr.attr.name);
		return -EPERM;
	}

	btrfs_info(fs_info, "%s %s feature flag",
		   val ? "Setting" : "Clearing", fa->kobj_attr.attr.name);

	trans = btrfs_start_transaction(fs_info->fs_root, 1);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	spin_lock(&fs_info->super_lock);
	features = get_features(fs_info, fa->feature_set);
	if (val)
		features |= fa->feature_bit;
	else
		features &= ~fa->feature_bit;
	set_features(fs_info, fa->feature_set, features);
	spin_unlock(&fs_info->super_lock);

	ret = btrfs_commit_transaction(trans, fs_info->fs_root);
	if (ret)
		return ret;

	return count;
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

		if (can_modify_feature(fa))
			mode |= S_IWUSR;
		else if (!(features & fa->feature_bit))
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

static ssize_t btrfs_show_u64(u64 *value_ptr, spinlock_t *lock, char *buf)
{
	u64 val;
	if (lock)
		spin_lock(lock);
	val = *value_ptr;
	if (lock)
		spin_unlock(lock);
	return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t global_rsv_size_show(struct kobject *kobj,
				    struct kobj_attribute *ka, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj->parent);
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	return btrfs_show_u64(&block_rsv->size, &block_rsv->lock, buf);
}
BTRFS_ATTR(global_rsv_size, 0444, global_rsv_size_show);

static ssize_t global_rsv_reserved_show(struct kobject *kobj,
					struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj->parent);
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	return btrfs_show_u64(&block_rsv->reserved, &block_rsv->lock, buf);
}
BTRFS_ATTR(global_rsv_reserved, 0444, global_rsv_reserved_show);

#define to_space_info(_kobj) container_of(_kobj, struct btrfs_space_info, kobj)

static ssize_t raid_bytes_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf);
BTRFS_RAID_ATTR(total_bytes, raid_bytes_show);
BTRFS_RAID_ATTR(used_bytes, raid_bytes_show);

static ssize_t raid_bytes_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)

{
	struct btrfs_space_info *sinfo = to_space_info(kobj->parent);
	struct btrfs_block_group_cache *block_group;
	int index = kobj - sinfo->block_group_kobjs;
	u64 val = 0;

	down_read(&sinfo->groups_sem);
	list_for_each_entry(block_group, &sinfo->block_groups[index], list) {
		if (&attr->attr == BTRFS_RAID_ATTR_PTR(total_bytes))
			val += block_group->key.offset;
		else
			val += btrfs_block_group_used(&block_group->item);
	}
	up_read(&sinfo->groups_sem);
	return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static struct attribute *raid_attributes[] = {
	BTRFS_RAID_ATTR_PTR(total_bytes),
	BTRFS_RAID_ATTR_PTR(used_bytes),
	NULL
};

static void release_raid_kobj(struct kobject *kobj)
{
	kobject_put(kobj->parent);
}

struct kobj_type btrfs_raid_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = release_raid_kobj,
	.default_attrs = raid_attributes,
};

#define SPACE_INFO_ATTR(field)						\
static ssize_t btrfs_space_info_show_##field(struct kobject *kobj,	\
					     struct kobj_attribute *a,	\
					     char *buf)			\
{									\
	struct btrfs_space_info *sinfo = to_space_info(kobj);		\
	return btrfs_show_u64(&sinfo->field, &sinfo->lock, buf);	\
}									\
BTRFS_ATTR(field, 0444, btrfs_space_info_show_##field)

static ssize_t btrfs_space_info_show_total_bytes_pinned(struct kobject *kobj,
						       struct kobj_attribute *a,
						       char *buf)
{
	struct btrfs_space_info *sinfo = to_space_info(kobj);
	s64 val = percpu_counter_sum(&sinfo->total_bytes_pinned);
	return snprintf(buf, PAGE_SIZE, "%lld\n", val);
}

SPACE_INFO_ATTR(flags);
SPACE_INFO_ATTR(total_bytes);
SPACE_INFO_ATTR(bytes_used);
SPACE_INFO_ATTR(bytes_pinned);
SPACE_INFO_ATTR(bytes_reserved);
SPACE_INFO_ATTR(bytes_may_use);
SPACE_INFO_ATTR(disk_used);
SPACE_INFO_ATTR(disk_total);
BTRFS_ATTR(total_bytes_pinned, 0444, btrfs_space_info_show_total_bytes_pinned);

static struct attribute *space_info_attrs[] = {
	BTRFS_ATTR_PTR(flags),
	BTRFS_ATTR_PTR(total_bytes),
	BTRFS_ATTR_PTR(bytes_used),
	BTRFS_ATTR_PTR(bytes_pinned),
	BTRFS_ATTR_PTR(bytes_reserved),
	BTRFS_ATTR_PTR(bytes_may_use),
	BTRFS_ATTR_PTR(disk_used),
	BTRFS_ATTR_PTR(disk_total),
	BTRFS_ATTR_PTR(total_bytes_pinned),
	NULL,
};

static void space_info_release(struct kobject *kobj)
{
	struct btrfs_space_info *sinfo = to_space_info(kobj);
	percpu_counter_destroy(&sinfo->total_bytes_pinned);
	kfree(sinfo);
}

struct kobj_type space_info_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = space_info_release,
	.default_attrs = space_info_attrs,
};

static const struct attribute *allocation_attrs[] = {
	BTRFS_ATTR_PTR(global_rsv_reserved),
	BTRFS_ATTR_PTR(global_rsv_size),
	NULL,
};

static ssize_t btrfs_label_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	return snprintf(buf, PAGE_SIZE, "%s\n", fs_info->super_copy->label);
}

static ssize_t btrfs_label_store(struct kobject *kobj,
				 struct kobj_attribute *a,
				 const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = fs_info->fs_root;
	int ret;

	if (len >= BTRFS_LABEL_SIZE) {
		pr_err("btrfs: unable to set label with more than %d bytes\n",
		       BTRFS_LABEL_SIZE - 1);
		return -EINVAL;
	}

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	spin_lock(&root->fs_info->super_lock);
	strcpy(fs_info->super_copy->label, buf);
	spin_unlock(&root->fs_info->super_lock);
	ret = btrfs_commit_transaction(trans, root);

	if (!ret)
		return len;

	return ret;
}
BTRFS_ATTR_RW(label, 0644, btrfs_label_show, btrfs_label_store);

static struct attribute *btrfs_attrs[] = {
	BTRFS_ATTR_PTR(label),
	NULL,
};

static void btrfs_release_super_kobj(struct kobject *kobj)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	complete(&fs_info->kobj_unregister);
}

static struct kobj_type btrfs_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= btrfs_release_super_kobj,
	.default_attrs	= btrfs_attrs,
};

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj)
{
	if (kobj->ktype != &btrfs_ktype)
		return NULL;
	return container_of(kobj, struct btrfs_fs_info, super_kobj);
}

void btrfs_sysfs_remove_one(struct btrfs_fs_info *fs_info)
{
	sysfs_remove_files(fs_info->space_info_kobj, allocation_attrs);
	kobject_del(fs_info->device_dir_kobj);
	kobject_put(fs_info->device_dir_kobj);
	kobject_del(fs_info->space_info_kobj);
	kobject_put(fs_info->space_info_kobj);
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

char *btrfs_printable_features(enum btrfs_feature_set set, u64 flags)
{
	size_t bufsize = 4096; /* safe max, 64 names * 64 bytes */
	int len = 0;
	int i;
	char *str;

	str = kmalloc(bufsize, GFP_KERNEL);
	if (!str)
		return str;

	for (i = 0; i < ARRAY_SIZE(btrfs_feature_attrs[set]); i++) {
		const char *name;

		if (!(flags & (1ULL << i)))
			continue;

		name = btrfs_feature_attrs[set][i].kobj_attr.attr.name;
		len += snprintf(str + len, bufsize - len, "%s%s",
				len ? "," : "", name);
	}

	return str;
}

static void init_feature_attrs(void)
{
	struct btrfs_feature_attr *fa;
	int set, i;

	BUILD_BUG_ON(ARRAY_SIZE(btrfs_unknown_feature_names) !=
		     ARRAY_SIZE(btrfs_feature_attrs));
	BUILD_BUG_ON(ARRAY_SIZE(btrfs_unknown_feature_names[0]) !=
		     ARRAY_SIZE(btrfs_feature_attrs[0]));

	memset(btrfs_feature_attrs, 0, sizeof(btrfs_feature_attrs));
	memset(btrfs_unknown_feature_names, 0,
	       sizeof(btrfs_unknown_feature_names));

	for (i = 0; btrfs_supported_feature_attrs[i]; i++) {
		struct btrfs_feature_attr *sfa;
		struct attribute *a = btrfs_supported_feature_attrs[i];
		int bit;
		sfa = attr_to_btrfs_feature_attr(a);
		bit = ilog2(sfa->feature_bit);
		fa = &btrfs_feature_attrs[sfa->feature_set][bit];

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

static int add_device_membership(struct btrfs_fs_info *fs_info)
{
	int error = 0;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *dev;

	fs_info->device_dir_kobj = kobject_create_and_add("devices",
						&fs_info->super_kobj);
	if (!fs_info->device_dir_kobj)
		return -ENOMEM;

	list_for_each_entry(dev, &fs_devices->devices, dev_list) {
		struct hd_struct *disk = dev->bdev->bd_part;
		struct kobject *disk_kobj = &part_to_dev(disk)->kobj;

		error = sysfs_create_link(fs_info->device_dir_kobj,
					  disk_kobj, disk_kobj->name);
		if (error)
			break;
	}

	return error;
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

	error = add_device_membership(fs_info);
	if (error)
		goto failure;

	fs_info->space_info_kobj = kobject_create_and_add("allocation",
						  &fs_info->super_kobj);
	if (!fs_info->space_info_kobj) {
		error = -ENOMEM;
		goto failure;
	}

	error = sysfs_create_files(fs_info->space_info_kobj, allocation_attrs);
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

