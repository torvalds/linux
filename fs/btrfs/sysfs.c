// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/sched/mm.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "sysfs.h"
#include "volumes.h"

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj);
static inline struct btrfs_fs_devices *to_fs_devs(struct kobject *kobj);

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
		pr_warn("btrfs: sysfs: unknown feature set %d\n",
				fa->feature_set);
		return 0;
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
	u64 features, set, clear;
	unsigned long val;
	int ret;

	fs_info = to_fs_info(kobj);
	if (!fs_info)
		return -EPERM;

	if (sb_rdonly(fs_info->sb))
		return -EROFS;

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

	spin_lock(&fs_info->super_lock);
	features = get_features(fs_info, fa->feature_set);
	if (val)
		features |= fa->feature_bit;
	else
		features &= ~fa->feature_bit;
	set_features(fs_info, fa->feature_set, features);
	spin_unlock(&fs_info->super_lock);

	/*
	 * We don't want to do full transaction commit from inside sysfs
	 */
	btrfs_set_pending(fs_info, COMMIT);
	wake_up_process(fs_info->transaction_kthread);

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
BTRFS_FEAT_ATTR_INCOMPAT(compress_zstd, COMPRESS_ZSTD);
BTRFS_FEAT_ATTR_INCOMPAT(big_metadata, BIG_METADATA);
BTRFS_FEAT_ATTR_INCOMPAT(extended_iref, EXTENDED_IREF);
BTRFS_FEAT_ATTR_INCOMPAT(raid56, RAID56);
BTRFS_FEAT_ATTR_INCOMPAT(skinny_metadata, SKINNY_METADATA);
BTRFS_FEAT_ATTR_INCOMPAT(no_holes, NO_HOLES);
BTRFS_FEAT_ATTR_COMPAT_RO(free_space_tree, FREE_SPACE_TREE);

static struct attribute *btrfs_supported_feature_attrs[] = {
	BTRFS_FEAT_ATTR_PTR(mixed_backref),
	BTRFS_FEAT_ATTR_PTR(default_subvol),
	BTRFS_FEAT_ATTR_PTR(mixed_groups),
	BTRFS_FEAT_ATTR_PTR(compress_lzo),
	BTRFS_FEAT_ATTR_PTR(compress_zstd),
	BTRFS_FEAT_ATTR_PTR(big_metadata),
	BTRFS_FEAT_ATTR_PTR(extended_iref),
	BTRFS_FEAT_ATTR_PTR(raid56),
	BTRFS_FEAT_ATTR_PTR(skinny_metadata),
	BTRFS_FEAT_ATTR_PTR(no_holes),
	BTRFS_FEAT_ATTR_PTR(free_space_tree),
	NULL
};

/*
 * Features which depend on feature bits and may differ between each fs.
 *
 * /sys/fs/btrfs/features lists all available features of this kernel while
 * /sys/fs/btrfs/UUID/features shows features of the fs which are enabled or
 * can be changed online.
 */
static const struct attribute_group btrfs_feature_attr_group = {
	.name = "features",
	.is_visible = btrfs_feature_visible,
	.attrs = btrfs_supported_feature_attrs,
};

static ssize_t rmdir_subvol_show(struct kobject *kobj,
				 struct kobj_attribute *ka, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0\n");
}
BTRFS_ATTR(static_feature, rmdir_subvol, rmdir_subvol_show);

static struct attribute *btrfs_supported_static_feature_attrs[] = {
	BTRFS_ATTR_PTR(static_feature, rmdir_subvol),
	NULL
};

/*
 * Features which only depend on kernel version.
 *
 * These are listed in /sys/fs/btrfs/features along with
 * btrfs_feature_attr_group
 */
static const struct attribute_group btrfs_static_feature_attr_group = {
	.name = "features",
	.attrs = btrfs_supported_static_feature_attrs,
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
BTRFS_ATTR(allocation, global_rsv_size, global_rsv_size_show);

static ssize_t global_rsv_reserved_show(struct kobject *kobj,
					struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj->parent);
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	return btrfs_show_u64(&block_rsv->reserved, &block_rsv->lock, buf);
}
BTRFS_ATTR(allocation, global_rsv_reserved, global_rsv_reserved_show);

#define to_space_info(_kobj) container_of(_kobj, struct btrfs_space_info, kobj)
#define to_raid_kobj(_kobj) container_of(_kobj, struct raid_kobject, kobj)

static ssize_t raid_bytes_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf);
BTRFS_ATTR(raid, total_bytes, raid_bytes_show);
BTRFS_ATTR(raid, used_bytes, raid_bytes_show);

static ssize_t raid_bytes_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)

{
	struct btrfs_space_info *sinfo = to_space_info(kobj->parent);
	struct btrfs_block_group_cache *block_group;
	int index = btrfs_bg_flags_to_raid_index(to_raid_kobj(kobj)->flags);
	u64 val = 0;

	down_read(&sinfo->groups_sem);
	list_for_each_entry(block_group, &sinfo->block_groups[index], list) {
		if (&attr->attr == BTRFS_ATTR_PTR(raid, total_bytes))
			val += block_group->key.offset;
		else
			val += btrfs_block_group_used(&block_group->item);
	}
	up_read(&sinfo->groups_sem);
	return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static struct attribute *raid_attributes[] = {
	BTRFS_ATTR_PTR(raid, total_bytes),
	BTRFS_ATTR_PTR(raid, used_bytes),
	NULL
};

static void release_raid_kobj(struct kobject *kobj)
{
	kfree(to_raid_kobj(kobj));
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
BTRFS_ATTR(space_info, field, btrfs_space_info_show_##field)

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
SPACE_INFO_ATTR(bytes_readonly);
SPACE_INFO_ATTR(disk_used);
SPACE_INFO_ATTR(disk_total);
BTRFS_ATTR(space_info, total_bytes_pinned,
	   btrfs_space_info_show_total_bytes_pinned);

static struct attribute *space_info_attrs[] = {
	BTRFS_ATTR_PTR(space_info, flags),
	BTRFS_ATTR_PTR(space_info, total_bytes),
	BTRFS_ATTR_PTR(space_info, bytes_used),
	BTRFS_ATTR_PTR(space_info, bytes_pinned),
	BTRFS_ATTR_PTR(space_info, bytes_reserved),
	BTRFS_ATTR_PTR(space_info, bytes_may_use),
	BTRFS_ATTR_PTR(space_info, bytes_readonly),
	BTRFS_ATTR_PTR(space_info, disk_used),
	BTRFS_ATTR_PTR(space_info, disk_total),
	BTRFS_ATTR_PTR(space_info, total_bytes_pinned),
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
	BTRFS_ATTR_PTR(allocation, global_rsv_reserved),
	BTRFS_ATTR_PTR(allocation, global_rsv_size),
	NULL,
};

static ssize_t btrfs_label_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	char *label = fs_info->super_copy->label;
	ssize_t ret;

	spin_lock(&fs_info->super_lock);
	ret = snprintf(buf, PAGE_SIZE, label[0] ? "%s\n" : "%s", label);
	spin_unlock(&fs_info->super_lock);

	return ret;
}

static ssize_t btrfs_label_store(struct kobject *kobj,
				 struct kobj_attribute *a,
				 const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	size_t p_len;

	if (!fs_info)
		return -EPERM;

	if (sb_rdonly(fs_info->sb))
		return -EROFS;

	/*
	 * p_len is the len until the first occurrence of either
	 * '\n' or '\0'
	 */
	p_len = strcspn(buf, "\n");

	if (p_len >= BTRFS_LABEL_SIZE)
		return -EINVAL;

	spin_lock(&fs_info->super_lock);
	memset(fs_info->super_copy->label, 0, BTRFS_LABEL_SIZE);
	memcpy(fs_info->super_copy->label, buf, p_len);
	spin_unlock(&fs_info->super_lock);

	/*
	 * We don't want to do full transaction commit from inside sysfs
	 */
	btrfs_set_pending(fs_info, COMMIT);
	wake_up_process(fs_info->transaction_kthread);

	return len;
}
BTRFS_ATTR_RW(, label, btrfs_label_show, btrfs_label_store);

static ssize_t btrfs_nodesize_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return snprintf(buf, PAGE_SIZE, "%u\n", fs_info->super_copy->nodesize);
}

BTRFS_ATTR(, nodesize, btrfs_nodesize_show);

static ssize_t btrfs_sectorsize_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			fs_info->super_copy->sectorsize);
}

BTRFS_ATTR(, sectorsize, btrfs_sectorsize_show);

static ssize_t btrfs_clone_alignment_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			fs_info->super_copy->sectorsize);
}

BTRFS_ATTR(, clone_alignment, btrfs_clone_alignment_show);

static ssize_t quota_override_show(struct kobject *kobj,
				   struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	int quota_override;

	quota_override = test_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags);
	return snprintf(buf, PAGE_SIZE, "%d\n", quota_override);
}

static ssize_t quota_override_store(struct kobject *kobj,
				    struct kobj_attribute *a,
				    const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	unsigned long knob;
	int err;

	if (!fs_info)
		return -EPERM;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	err = kstrtoul(buf, 10, &knob);
	if (err)
		return err;
	if (knob > 1)
		return -EINVAL;

	if (knob)
		set_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags);
	else
		clear_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags);

	return len;
}

BTRFS_ATTR_RW(, quota_override, quota_override_show, quota_override_store);

static const struct attribute *btrfs_attrs[] = {
	BTRFS_ATTR_PTR(, label),
	BTRFS_ATTR_PTR(, nodesize),
	BTRFS_ATTR_PTR(, sectorsize),
	BTRFS_ATTR_PTR(, clone_alignment),
	BTRFS_ATTR_PTR(, quota_override),
	NULL,
};

static void btrfs_release_fsid_kobj(struct kobject *kobj)
{
	struct btrfs_fs_devices *fs_devs = to_fs_devs(kobj);

	memset(&fs_devs->fsid_kobj, 0, sizeof(struct kobject));
	complete(&fs_devs->kobj_unregister);
}

static struct kobj_type btrfs_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= btrfs_release_fsid_kobj,
};

static inline struct btrfs_fs_devices *to_fs_devs(struct kobject *kobj)
{
	if (kobj->ktype != &btrfs_ktype)
		return NULL;
	return container_of(kobj, struct btrfs_fs_devices, fsid_kobj);
}

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj)
{
	if (kobj->ktype != &btrfs_ktype)
		return NULL;
	return to_fs_devs(kobj)->fs_info;
}

#define NUM_FEATURE_BITS 64
#define BTRFS_FEATURE_NAME_MAX 13
static char btrfs_unknown_feature_names[FEAT_MAX][NUM_FEATURE_BITS][BTRFS_FEATURE_NAME_MAX];
static struct btrfs_feature_attr btrfs_feature_attrs[FEAT_MAX][NUM_FEATURE_BITS];

static const u64 supported_feature_masks[FEAT_MAX] = {
	[FEAT_COMPAT]    = BTRFS_FEATURE_COMPAT_SUPP,
	[FEAT_COMPAT_RO] = BTRFS_FEATURE_COMPAT_RO_SUPP,
	[FEAT_INCOMPAT]  = BTRFS_FEATURE_INCOMPAT_SUPP,
};

static int addrm_unknown_feature_attrs(struct btrfs_fs_info *fs_info, bool add)
{
	int set;

	for (set = 0; set < FEAT_MAX; set++) {
		int i;
		struct attribute *attrs[2];
		struct attribute_group agroup = {
			.name = "features",
			.attrs = attrs,
		};
		u64 features = get_features(fs_info, set);
		features &= ~supported_feature_masks[set];

		if (!features)
			continue;

		attrs[1] = NULL;
		for (i = 0; i < NUM_FEATURE_BITS; i++) {
			struct btrfs_feature_attr *fa;

			if (!(features & (1ULL << i)))
				continue;

			fa = &btrfs_feature_attrs[set][i];
			attrs[0] = &fa->kobj_attr.attr;
			if (add) {
				int ret;
				ret = sysfs_merge_group(&fs_info->fs_devices->fsid_kobj,
							&agroup);
				if (ret)
					return ret;
			} else
				sysfs_unmerge_group(&fs_info->fs_devices->fsid_kobj,
						    &agroup);
		}

	}
	return 0;
}

static void __btrfs_sysfs_remove_fsid(struct btrfs_fs_devices *fs_devs)
{
	if (fs_devs->device_dir_kobj) {
		kobject_del(fs_devs->device_dir_kobj);
		kobject_put(fs_devs->device_dir_kobj);
		fs_devs->device_dir_kobj = NULL;
	}

	if (fs_devs->fsid_kobj.state_initialized) {
		kobject_del(&fs_devs->fsid_kobj);
		kobject_put(&fs_devs->fsid_kobj);
		wait_for_completion(&fs_devs->kobj_unregister);
	}
}

/* when fs_devs is NULL it will remove all fsid kobject */
void btrfs_sysfs_remove_fsid(struct btrfs_fs_devices *fs_devs)
{
	struct list_head *fs_uuids = btrfs_get_fs_uuids();

	if (fs_devs) {
		__btrfs_sysfs_remove_fsid(fs_devs);
		return;
	}

	list_for_each_entry(fs_devs, fs_uuids, fs_list) {
		__btrfs_sysfs_remove_fsid(fs_devs);
	}
}

void btrfs_sysfs_remove_mounted(struct btrfs_fs_info *fs_info)
{
	btrfs_reset_fs_info_ptr(fs_info);

	if (fs_info->space_info_kobj) {
		sysfs_remove_files(fs_info->space_info_kobj, allocation_attrs);
		kobject_del(fs_info->space_info_kobj);
		kobject_put(fs_info->space_info_kobj);
	}
	addrm_unknown_feature_attrs(fs_info, false);
	sysfs_remove_group(&fs_info->fs_devices->fsid_kobj, &btrfs_feature_attr_group);
	sysfs_remove_files(&fs_info->fs_devices->fsid_kobj, btrfs_attrs);
	btrfs_sysfs_rm_device_link(fs_info->fs_devices, NULL);
}

const char * const btrfs_feature_set_names[FEAT_MAX] = {
	[FEAT_COMPAT]	 = "compat",
	[FEAT_COMPAT_RO] = "compat_ro",
	[FEAT_INCOMPAT]	 = "incompat",
};

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

			snprintf(name, BTRFS_FEATURE_NAME_MAX, "%s:%u",
				 btrfs_feature_set_names[set], i);

			fa->kobj_attr.attr.name = name;
			fa->kobj_attr.attr.mode = S_IRUGO;
			fa->feature_set = set;
			fa->feature_bit = 1ULL << i;
		}
	}
}

/* when one_device is NULL, it removes all device links */

int btrfs_sysfs_rm_device_link(struct btrfs_fs_devices *fs_devices,
		struct btrfs_device *one_device)
{
	struct hd_struct *disk;
	struct kobject *disk_kobj;

	if (!fs_devices->device_dir_kobj)
		return -EINVAL;

	if (one_device && one_device->bdev) {
		disk = one_device->bdev->bd_part;
		disk_kobj = &part_to_dev(disk)->kobj;

		sysfs_remove_link(fs_devices->device_dir_kobj,
						disk_kobj->name);
	}

	if (one_device)
		return 0;

	list_for_each_entry(one_device,
			&fs_devices->devices, dev_list) {
		if (!one_device->bdev)
			continue;
		disk = one_device->bdev->bd_part;
		disk_kobj = &part_to_dev(disk)->kobj;

		sysfs_remove_link(fs_devices->device_dir_kobj,
						disk_kobj->name);
	}

	return 0;
}

int btrfs_sysfs_add_device(struct btrfs_fs_devices *fs_devs)
{
	if (!fs_devs->device_dir_kobj)
		fs_devs->device_dir_kobj = kobject_create_and_add("devices",
						&fs_devs->fsid_kobj);

	if (!fs_devs->device_dir_kobj)
		return -ENOMEM;

	return 0;
}

int btrfs_sysfs_add_device_link(struct btrfs_fs_devices *fs_devices,
				struct btrfs_device *one_device)
{
	int error = 0;
	struct btrfs_device *dev;
	unsigned int nofs_flag;

	nofs_flag = memalloc_nofs_save();
	list_for_each_entry(dev, &fs_devices->devices, dev_list) {
		struct hd_struct *disk;
		struct kobject *disk_kobj;

		if (!dev->bdev)
			continue;

		if (one_device && one_device != dev)
			continue;

		disk = dev->bdev->bd_part;
		disk_kobj = &part_to_dev(disk)->kobj;

		error = sysfs_create_link(fs_devices->device_dir_kobj,
					  disk_kobj, disk_kobj->name);
		if (error)
			break;
	}
	memalloc_nofs_restore(nofs_flag);

	return error;
}

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

/* /sys/kernel/debug/btrfs */
static struct dentry *btrfs_debugfs_root_dentry;

/* Debugging tunables and exported data */
u64 btrfs_debugfs_test;

/*
 * Can be called by the device discovery thread.
 * And parent can be specified for seed device
 */
int btrfs_sysfs_add_fsid(struct btrfs_fs_devices *fs_devs,
				struct kobject *parent)
{
	int error;

	init_completion(&fs_devs->kobj_unregister);
	fs_devs->fsid_kobj.kset = btrfs_kset;
	error = kobject_init_and_add(&fs_devs->fsid_kobj,
				&btrfs_ktype, parent, "%pU", fs_devs->fsid);
	if (error) {
		kobject_put(&fs_devs->fsid_kobj);
		return error;
	}

	return 0;
}

int btrfs_sysfs_add_mounted(struct btrfs_fs_info *fs_info)
{
	int error;
	struct btrfs_fs_devices *fs_devs = fs_info->fs_devices;
	struct kobject *fsid_kobj = &fs_devs->fsid_kobj;

	btrfs_set_fs_info_ptr(fs_info);

	error = btrfs_sysfs_add_device_link(fs_devs, NULL);
	if (error)
		return error;

	error = sysfs_create_files(fsid_kobj, btrfs_attrs);
	if (error) {
		btrfs_sysfs_rm_device_link(fs_devs, NULL);
		return error;
	}

	error = sysfs_create_group(fsid_kobj,
				   &btrfs_feature_attr_group);
	if (error)
		goto failure;

	error = addrm_unknown_feature_attrs(fs_info, true);
	if (error)
		goto failure;

	fs_info->space_info_kobj = kobject_create_and_add("allocation",
						  fsid_kobj);
	if (!fs_info->space_info_kobj) {
		error = -ENOMEM;
		goto failure;
	}

	error = sysfs_create_files(fs_info->space_info_kobj, allocation_attrs);
	if (error)
		goto failure;

	return 0;
failure:
	btrfs_sysfs_remove_mounted(fs_info);
	return error;
}


/*
 * Change per-fs features in /sys/fs/btrfs/UUID/features to match current
 * values in superblock. Call after any changes to incompat/compat_ro flags
 */
void btrfs_sysfs_feature_update(struct btrfs_fs_info *fs_info,
		u64 bit, enum btrfs_feature_set set)
{
	struct btrfs_fs_devices *fs_devs;
	struct kobject *fsid_kobj;
	u64 features;
	int ret;

	if (!fs_info)
		return;

	features = get_features(fs_info, set);
	ASSERT(bit & supported_feature_masks[set]);

	fs_devs = fs_info->fs_devices;
	fsid_kobj = &fs_devs->fsid_kobj;

	if (!fsid_kobj->state_initialized)
		return;

	/*
	 * FIXME: this is too heavy to update just one value, ideally we'd like
	 * to use sysfs_update_group but some refactoring is needed first.
	 */
	sysfs_remove_group(fsid_kobj, &btrfs_feature_attr_group);
	ret = sysfs_create_group(fsid_kobj, &btrfs_feature_attr_group);
}

static int btrfs_init_debugfs(void)
{
#ifdef CONFIG_DEBUG_FS
	btrfs_debugfs_root_dentry = debugfs_create_dir("btrfs", NULL);
	if (!btrfs_debugfs_root_dentry)
		return -ENOMEM;

	/*
	 * Example code, how to export data through debugfs.
	 *
	 * file:        /sys/kernel/debug/btrfs/test
	 * contents of: btrfs_debugfs_test
	 */
#ifdef CONFIG_BTRFS_DEBUG
	debugfs_create_u64("test", S_IRUGO | S_IWUSR, btrfs_debugfs_root_dentry,
			&btrfs_debugfs_test);
#endif

#endif
	return 0;
}

int __init btrfs_init_sysfs(void)
{
	int ret;

	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	ret = btrfs_init_debugfs();
	if (ret)
		goto out1;

	init_feature_attrs();
	ret = sysfs_create_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	if (ret)
		goto out2;
	ret = sysfs_merge_group(&btrfs_kset->kobj,
				&btrfs_static_feature_attr_group);
	if (ret)
		goto out_remove_group;

	return 0;

out_remove_group:
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
out2:
	debugfs_remove_recursive(btrfs_debugfs_root_dentry);
out1:
	kset_unregister(btrfs_kset);

	return ret;
}

void __cold btrfs_exit_sysfs(void)
{
	sysfs_unmerge_group(&btrfs_kset->kobj,
			    &btrfs_static_feature_attr_group);
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	kset_unregister(btrfs_kset);
	debugfs_remove_recursive(btrfs_debugfs_root_dentry);
}

