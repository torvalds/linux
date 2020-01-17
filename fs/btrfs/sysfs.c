// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/bug.h>
#include <crypto/hash.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "sysfs.h"
#include "volumes.h"
#include "space-info.h"
#include "block-group.h"

struct btrfs_feature_attr {
	struct kobj_attribute kobj_attr;
	enum btrfs_feature_set feature_set;
	u64 feature_bit;
};

/* For raid type sysfs entries */
struct raid_kobject {
	u64 flags;
	struct kobject kobj;
};

#define __INIT_KOBJ_ATTR(_name, _mode, _show, _store)			\
{									\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,						\
	.store	= _store,						\
}

#define BTRFS_ATTR_RW(_prefix, _name, _show, _store)			\
	static struct kobj_attribute btrfs_attr_##_prefix##_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0644, _show, _store)

#define BTRFS_ATTR(_prefix, _name, _show)				\
	static struct kobj_attribute btrfs_attr_##_prefix##_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0444, _show, NULL)

#define BTRFS_ATTR_PTR(_prefix, _name)					\
	(&btrfs_attr_##_prefix##_##_name.attr)

#define BTRFS_FEAT_ATTR(_name, _feature_set, _feature_prefix, _feature_bit)  \
static struct btrfs_feature_attr btrfs_attr_features_##_name = {	     \
	.kobj_attr = __INIT_KOBJ_ATTR(_name, S_IRUGO,			     \
				      btrfs_feature_attr_show,		     \
				      btrfs_feature_attr_store),	     \
	.feature_set	= _feature_set,					     \
	.feature_bit	= _feature_prefix ##_## _feature_bit,		     \
}
#define BTRFS_FEAT_ATTR_PTR(_name)					     \
	(&btrfs_attr_features_##_name.kobj_attr.attr)

#define BTRFS_FEAT_ATTR_COMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_COMPAT_RO(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT_RO, BTRFS_FEATURE_COMPAT_RO, feature)
#define BTRFS_FEAT_ATTR_INCOMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_INCOMPAT, BTRFS_FEATURE_INCOMPAT, feature)

static inline struct btrfs_fs_info *to_fs_info(struct kobject *kobj);
static inline struct btrfs_fs_devices *to_fs_devs(struct kobject *kobj);

static struct btrfs_feature_attr *to_btrfs_feature_attr(struct kobj_attribute *a)
{
	return container_of(a, struct btrfs_feature_attr, kobj_attr);
}

static struct kobj_attribute *attr_to_btrfs_attr(struct attribute *attr)
{
	return container_of(attr, struct kobj_attribute, attr);
}

static struct btrfs_feature_attr *attr_to_btrfs_feature_attr(
		struct attribute *attr)
{
	return to_btrfs_feature_attr(attr_to_btrfs_attr(attr));
}

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
BTRFS_FEAT_ATTR_INCOMPAT(metadata_uuid, METADATA_UUID);
BTRFS_FEAT_ATTR_COMPAT_RO(free_space_tree, FREE_SPACE_TREE);
BTRFS_FEAT_ATTR_INCOMPAT(raid1c34, RAID1C34);

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
	BTRFS_FEAT_ATTR_PTR(metadata_uuid),
	BTRFS_FEAT_ATTR_PTR(free_space_tree),
	BTRFS_FEAT_ATTR_PTR(raid1c34),
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

static ssize_t supported_checksums_show(struct kobject *kobj,
					struct kobj_attribute *a, char *buf)
{
	ssize_t ret = 0;
	int i;

	for (i = 0; i < btrfs_get_num_csums(); i++) {
		/*
		 * This "trick" only works as long as 'enum btrfs_csum_type' has
		 * no holes in it
		 */
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%s%s",
				(i == 0 ? "" : " "), btrfs_super_csum_name(i));

	}

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	return ret;
}
BTRFS_ATTR(static_feature, supported_checksums, supported_checksums_show);

static struct attribute *btrfs_supported_static_feature_attrs[] = {
	BTRFS_ATTR_PTR(static_feature, rmdir_subvol),
	BTRFS_ATTR_PTR(static_feature, supported_checksums),
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

#ifdef CONFIG_BTRFS_DEBUG

/*
 * Runtime debugging exported via sysfs
 *
 * /sys/fs/btrfs/debug - applies to module or all filesystems
 * /sys/fs/btrfs/UUID  - applies only to the given filesystem
 */
static struct attribute *btrfs_debug_feature_attrs[] = {
	NULL
};

static const struct attribute_group btrfs_debug_feature_attr_group = {
	.name = "debug",
	.attrs = btrfs_debug_feature_attrs,
};

#endif

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
	struct btrfs_block_group *block_group;
	int index = btrfs_bg_flags_to_raid_index(to_raid_kobj(kobj)->flags);
	u64 val = 0;

	down_read(&sinfo->groups_sem);
	list_for_each_entry(block_group, &sinfo->block_groups[index], list) {
		if (&attr->attr == BTRFS_ATTR_PTR(raid, total_bytes))
			val += block_group->length;
		else
			val += block_group->used;
	}
	up_read(&sinfo->groups_sem);
	return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static struct attribute *raid_attrs[] = {
	BTRFS_ATTR_PTR(raid, total_bytes),
	BTRFS_ATTR_PTR(raid, used_bytes),
	NULL
};
ATTRIBUTE_GROUPS(raid);

static void release_raid_kobj(struct kobject *kobj)
{
	kfree(to_raid_kobj(kobj));
}

static struct kobj_type btrfs_raid_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = release_raid_kobj,
	.default_groups = raid_groups,
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
ATTRIBUTE_GROUPS(space_info);

static void space_info_release(struct kobject *kobj)
{
	struct btrfs_space_info *sinfo = to_space_info(kobj);
	percpu_counter_destroy(&sinfo->total_bytes_pinned);
	kfree(sinfo);
}

static struct kobj_type space_info_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = space_info_release,
	.default_groups = space_info_groups,
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

static ssize_t btrfs_metadata_uuid_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return snprintf(buf, PAGE_SIZE, "%pU\n",
			fs_info->fs_devices->metadata_uuid);
}

BTRFS_ATTR(, metadata_uuid, btrfs_metadata_uuid_show);

static ssize_t btrfs_checksum_show(struct kobject *kobj,
				   struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	u16 csum_type = btrfs_super_csum_type(fs_info->super_copy);

	return snprintf(buf, PAGE_SIZE, "%s (%s)\n",
			btrfs_super_csum_name(csum_type),
			crypto_shash_driver_name(fs_info->csum_shash));
}

BTRFS_ATTR(, checksum, btrfs_checksum_show);

static const struct attribute *btrfs_attrs[] = {
	BTRFS_ATTR_PTR(, label),
	BTRFS_ATTR_PTR(, nodesize),
	BTRFS_ATTR_PTR(, sectorsize),
	BTRFS_ATTR_PTR(, clone_alignment),
	BTRFS_ATTR_PTR(, quota_override),
	BTRFS_ATTR_PTR(, metadata_uuid),
	BTRFS_ATTR_PTR(, checksum),
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

static const char * const btrfs_feature_set_names[FEAT_MAX] = {
	[FEAT_COMPAT]	 = "compat",
	[FEAT_COMPAT_RO] = "compat_ro",
	[FEAT_INCOMPAT]	 = "incompat",
};

const char * const btrfs_feature_set_name(enum btrfs_feature_set set)
{
	return btrfs_feature_set_names[set];
}

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

/*
 * Create a sysfs entry for a given block group type at path
 * /sys/fs/btrfs/UUID/allocation/data/TYPE
 */
void btrfs_sysfs_add_block_group_type(struct btrfs_block_group *cache)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_space_info *space_info = cache->space_info;
	struct raid_kobject *rkobj;
	const int index = btrfs_bg_flags_to_raid_index(cache->flags);
	unsigned int nofs_flag;
	int ret;

	/*
	 * Setup a NOFS context because kobject_add(), deep in its call chain,
	 * does GFP_KERNEL allocations, and we are often called in a context
	 * where if reclaim is triggered we can deadlock (we are either holding
	 * a transaction handle or some lock required for a transaction
	 * commit).
	 */
	nofs_flag = memalloc_nofs_save();

	rkobj = kzalloc(sizeof(*rkobj), GFP_NOFS);
	if (!rkobj) {
		memalloc_nofs_restore(nofs_flag);
		btrfs_warn(cache->fs_info,
				"couldn't alloc memory for raid level kobject");
		return;
	}

	rkobj->flags = cache->flags;
	kobject_init(&rkobj->kobj, &btrfs_raid_ktype);
	ret = kobject_add(&rkobj->kobj, &space_info->kobj, "%s",
			  btrfs_bg_type_to_raid_name(rkobj->flags));
	memalloc_nofs_restore(nofs_flag);
	if (ret) {
		kobject_put(&rkobj->kobj);
		btrfs_warn(fs_info,
			"failed to add kobject for block cache, ignoring");
		return;
	}

	space_info->block_group_kobjs[index] = &rkobj->kobj;
}

/*
 * Remove sysfs directories for all block group types of a given space info and
 * the space info as well
 */
void btrfs_sysfs_remove_space_info(struct btrfs_space_info *space_info)
{
	int i;

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++) {
		struct kobject *kobj;

		kobj = space_info->block_group_kobjs[i];
		space_info->block_group_kobjs[i] = NULL;
		if (kobj) {
			kobject_del(kobj);
			kobject_put(kobj);
		}
	}
	kobject_del(&space_info->kobj);
	kobject_put(&space_info->kobj);
}

static const char *alloc_name(u64 flags)
{
	switch (flags) {
	case BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA:
		return "mixed";
	case BTRFS_BLOCK_GROUP_METADATA:
		return "metadata";
	case BTRFS_BLOCK_GROUP_DATA:
		return "data";
	case BTRFS_BLOCK_GROUP_SYSTEM:
		return "system";
	default:
		WARN_ON(1);
		return "invalid-combination";
	};
}

/*
 * Create a sysfs entry for a space info type at path
 * /sys/fs/btrfs/UUID/allocation/TYPE
 */
int btrfs_sysfs_add_space_info_type(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info)
{
	int ret;

	ret = kobject_init_and_add(&space_info->kobj, &space_info_ktype,
				   fs_info->space_info_kobj, "%s",
				   alloc_name(space_info->flags));
	if (ret) {
		kobject_put(&space_info->kobj);
		return ret;
	}

	return 0;
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

	return error;
}

void btrfs_kobject_uevent(struct block_device *bdev, enum kobject_action action)
{
	int ret;

	ret = kobject_uevent(&disk_to_dev(bdev->bd_disk)->kobj, action);
	if (ret)
		pr_warn("BTRFS: Sending event '%d' to kobject: '%s' (%p): failed\n",
			action, kobject_name(&disk_to_dev(bdev->bd_disk)->kobj),
			&disk_to_dev(bdev->bd_disk)->kobj);
}

void btrfs_sysfs_update_sprout_fsid(struct btrfs_fs_devices *fs_devices,
				    const u8 *fsid)
{
	char fsid_buf[BTRFS_UUID_UNPARSED_SIZE];

	/*
	 * Sprouting changes fsid of the mounted filesystem, rename the fsid
	 * directory
	 */
	snprintf(fsid_buf, BTRFS_UUID_UNPARSED_SIZE, "%pU", fsid);
	if (kobject_rename(&fs_devices->fsid_kobj, fsid_buf))
		btrfs_warn(fs_devices->fs_info,
				"sysfs: failed to create fsid for sprout");
}

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

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

#ifdef CONFIG_BTRFS_DEBUG
	error = sysfs_create_group(fsid_kobj,
				   &btrfs_debug_feature_attr_group);
	if (error)
		goto failure;
#endif

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

int __init btrfs_init_sysfs(void)
{
	int ret;

	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	init_feature_attrs();
	ret = sysfs_create_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	if (ret)
		goto out2;
	ret = sysfs_merge_group(&btrfs_kset->kobj,
				&btrfs_static_feature_attr_group);
	if (ret)
		goto out_remove_group;

#ifdef CONFIG_BTRFS_DEBUG
	ret = sysfs_create_group(&btrfs_kset->kobj, &btrfs_debug_feature_attr_group);
	if (ret)
		goto out2;
#endif

	return 0;

out_remove_group:
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
out2:
	kset_unregister(btrfs_kset);

	return ret;
}

void __cold btrfs_exit_sysfs(void)
{
	sysfs_unmerge_group(&btrfs_kset->kobj,
			    &btrfs_static_feature_attr_group);
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_feature_attr_group);
	kset_unregister(btrfs_kset);
}

