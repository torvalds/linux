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
#include "messages.h"
#include "ctree.h"
#include "discard.h"
#include "disk-io.h"
#include "send.h"
#include "transaction.h"
#include "sysfs.h"
#include "volumes.h"
#include "space-info.h"
#include "block-group.h"
#include "qgroup.h"
#include "misc.h"
#include "fs.h"
#include "accessors.h"

/*
 * Structure name                       Path
 * --------------------------------------------------------------------------
 * btrfs_supported_static_feature_attrs /sys/fs/btrfs/features
 * btrfs_supported_feature_attrs	/sys/fs/btrfs/features and
 *					/sys/fs/btrfs/<uuid>/features
 * btrfs_attrs				/sys/fs/btrfs/<uuid>
 * devid_attrs				/sys/fs/btrfs/<uuid>/devinfo/<devid>
 * allocation_attrs			/sys/fs/btrfs/<uuid>/allocation
 * qgroup_attrs				/sys/fs/btrfs/<uuid>/qgroups/<level>_<qgroupid>
 * space_info_attrs			/sys/fs/btrfs/<uuid>/allocation/<bg-type>
 * raid_attrs				/sys/fs/btrfs/<uuid>/allocation/<bg-type>/<bg-profile>
 * discard_attrs			/sys/fs/btrfs/<uuid>/discard
 *
 * When built with BTRFS_CONFIG_DEBUG:
 *
 * btrfs_debug_feature_attrs		/sys/fs/btrfs/debug
 * btrfs_debug_mount_attrs		/sys/fs/btrfs/<uuid>/debug
 */

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

#define BTRFS_ATTR_W(_prefix, _name, _store)			        \
	static struct kobj_attribute btrfs_attr_##_prefix##_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0200, NULL, _store)

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
static struct kobject *get_btrfs_kobj(struct kobject *kobj);

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

	return sysfs_emit(buf, "%d\n", val);
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
	set_bit(BTRFS_FS_NEED_TRANS_COMMIT, &fs_info->flags);
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

BTRFS_FEAT_ATTR_INCOMPAT(default_subvol, DEFAULT_SUBVOL);
BTRFS_FEAT_ATTR_INCOMPAT(mixed_groups, MIXED_GROUPS);
BTRFS_FEAT_ATTR_INCOMPAT(compress_lzo, COMPRESS_LZO);
BTRFS_FEAT_ATTR_INCOMPAT(compress_zstd, COMPRESS_ZSTD);
BTRFS_FEAT_ATTR_INCOMPAT(extended_iref, EXTENDED_IREF);
BTRFS_FEAT_ATTR_INCOMPAT(raid56, RAID56);
BTRFS_FEAT_ATTR_INCOMPAT(skinny_metadata, SKINNY_METADATA);
BTRFS_FEAT_ATTR_INCOMPAT(no_holes, NO_HOLES);
BTRFS_FEAT_ATTR_INCOMPAT(metadata_uuid, METADATA_UUID);
BTRFS_FEAT_ATTR_COMPAT_RO(free_space_tree, FREE_SPACE_TREE);
BTRFS_FEAT_ATTR_COMPAT_RO(block_group_tree, BLOCK_GROUP_TREE);
BTRFS_FEAT_ATTR_INCOMPAT(raid1c34, RAID1C34);
#ifdef CONFIG_BLK_DEV_ZONED
BTRFS_FEAT_ATTR_INCOMPAT(zoned, ZONED);
#endif
#ifdef CONFIG_BTRFS_DEBUG
/* Remove once support for extent tree v2 is feature complete */
BTRFS_FEAT_ATTR_INCOMPAT(extent_tree_v2, EXTENT_TREE_V2);
#endif
#ifdef CONFIG_FS_VERITY
BTRFS_FEAT_ATTR_COMPAT_RO(verity, VERITY);
#endif

/*
 * Features which depend on feature bits and may differ between each fs.
 *
 * /sys/fs/btrfs/features      - all available features implemented by this version
 * /sys/fs/btrfs/UUID/features - features of the fs which are enabled or
 *                               can be changed on a mounted filesystem.
 */
static struct attribute *btrfs_supported_feature_attrs[] = {
	BTRFS_FEAT_ATTR_PTR(default_subvol),
	BTRFS_FEAT_ATTR_PTR(mixed_groups),
	BTRFS_FEAT_ATTR_PTR(compress_lzo),
	BTRFS_FEAT_ATTR_PTR(compress_zstd),
	BTRFS_FEAT_ATTR_PTR(extended_iref),
	BTRFS_FEAT_ATTR_PTR(raid56),
	BTRFS_FEAT_ATTR_PTR(skinny_metadata),
	BTRFS_FEAT_ATTR_PTR(no_holes),
	BTRFS_FEAT_ATTR_PTR(metadata_uuid),
	BTRFS_FEAT_ATTR_PTR(free_space_tree),
	BTRFS_FEAT_ATTR_PTR(raid1c34),
	BTRFS_FEAT_ATTR_PTR(block_group_tree),
#ifdef CONFIG_BLK_DEV_ZONED
	BTRFS_FEAT_ATTR_PTR(zoned),
#endif
#ifdef CONFIG_BTRFS_DEBUG
	BTRFS_FEAT_ATTR_PTR(extent_tree_v2),
#endif
#ifdef CONFIG_FS_VERITY
	BTRFS_FEAT_ATTR_PTR(verity),
#endif
	NULL
};

static const struct attribute_group btrfs_feature_attr_group = {
	.name = "features",
	.is_visible = btrfs_feature_visible,
	.attrs = btrfs_supported_feature_attrs,
};

static ssize_t rmdir_subvol_show(struct kobject *kobj,
				 struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "0\n");
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
		ret += sysfs_emit_at(buf, ret, "%s%s", (i == 0 ? "" : " "),
				     btrfs_super_csum_name(i));

	}

	ret += sysfs_emit_at(buf, ret, "\n");
	return ret;
}
BTRFS_ATTR(static_feature, supported_checksums, supported_checksums_show);

static ssize_t send_stream_version_show(struct kobject *kobj,
					struct kobj_attribute *ka, char *buf)
{
	return sysfs_emit(buf, "%d\n", BTRFS_SEND_STREAM_VERSION);
}
BTRFS_ATTR(static_feature, send_stream_version, send_stream_version_show);

static const char *rescue_opts[] = {
	"usebackuproot",
	"nologreplay",
	"ignorebadroots",
	"ignoredatacsums",
	"all",
};

static ssize_t supported_rescue_options_show(struct kobject *kobj,
					     struct kobj_attribute *a,
					     char *buf)
{
	ssize_t ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(rescue_opts); i++)
		ret += sysfs_emit_at(buf, ret, "%s%s", (i ? " " : ""), rescue_opts[i]);
	ret += sysfs_emit_at(buf, ret, "\n");
	return ret;
}
BTRFS_ATTR(static_feature, supported_rescue_options,
	   supported_rescue_options_show);

static ssize_t supported_sectorsizes_show(struct kobject *kobj,
					  struct kobj_attribute *a,
					  char *buf)
{
	ssize_t ret = 0;

	/* An artificial limit to only support 4K and PAGE_SIZE */
	if (PAGE_SIZE > SZ_4K)
		ret += sysfs_emit_at(buf, ret, "%u ", SZ_4K);
	ret += sysfs_emit_at(buf, ret, "%lu\n", PAGE_SIZE);

	return ret;
}
BTRFS_ATTR(static_feature, supported_sectorsizes,
	   supported_sectorsizes_show);

/*
 * Features which only depend on kernel version.
 *
 * These are listed in /sys/fs/btrfs/features along with
 * btrfs_supported_feature_attrs.
 */
static struct attribute *btrfs_supported_static_feature_attrs[] = {
	BTRFS_ATTR_PTR(static_feature, rmdir_subvol),
	BTRFS_ATTR_PTR(static_feature, supported_checksums),
	BTRFS_ATTR_PTR(static_feature, send_stream_version),
	BTRFS_ATTR_PTR(static_feature, supported_rescue_options),
	BTRFS_ATTR_PTR(static_feature, supported_sectorsizes),
	NULL
};

static const struct attribute_group btrfs_static_feature_attr_group = {
	.name = "features",
	.attrs = btrfs_supported_static_feature_attrs,
};

/*
 * Discard statistics and tunables
 */
#define discard_to_fs_info(_kobj)	to_fs_info(get_btrfs_kobj(_kobj))

static ssize_t btrfs_discardable_bytes_show(struct kobject *kobj,
					    struct kobj_attribute *a,
					    char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%lld\n",
			atomic64_read(&fs_info->discard_ctl.discardable_bytes));
}
BTRFS_ATTR(discard, discardable_bytes, btrfs_discardable_bytes_show);

static ssize_t btrfs_discardable_extents_show(struct kobject *kobj,
					      struct kobj_attribute *a,
					      char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%d\n",
			atomic_read(&fs_info->discard_ctl.discardable_extents));
}
BTRFS_ATTR(discard, discardable_extents, btrfs_discardable_extents_show);

static ssize_t btrfs_discard_bitmap_bytes_show(struct kobject *kobj,
					       struct kobj_attribute *a,
					       char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%llu\n",
			  fs_info->discard_ctl.discard_bitmap_bytes);
}
BTRFS_ATTR(discard, discard_bitmap_bytes, btrfs_discard_bitmap_bytes_show);

static ssize_t btrfs_discard_bytes_saved_show(struct kobject *kobj,
					      struct kobj_attribute *a,
					      char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%lld\n",
		atomic64_read(&fs_info->discard_ctl.discard_bytes_saved));
}
BTRFS_ATTR(discard, discard_bytes_saved, btrfs_discard_bytes_saved_show);

static ssize_t btrfs_discard_extent_bytes_show(struct kobject *kobj,
					       struct kobj_attribute *a,
					       char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%llu\n",
			  fs_info->discard_ctl.discard_extent_bytes);
}
BTRFS_ATTR(discard, discard_extent_bytes, btrfs_discard_extent_bytes_show);

static ssize_t btrfs_discard_iops_limit_show(struct kobject *kobj,
					     struct kobj_attribute *a,
					     char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%u\n",
			  READ_ONCE(fs_info->discard_ctl.iops_limit));
}

static ssize_t btrfs_discard_iops_limit_store(struct kobject *kobj,
					      struct kobj_attribute *a,
					      const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);
	struct btrfs_discard_ctl *discard_ctl = &fs_info->discard_ctl;
	u32 iops_limit;
	int ret;

	ret = kstrtou32(buf, 10, &iops_limit);
	if (ret)
		return -EINVAL;

	WRITE_ONCE(discard_ctl->iops_limit, iops_limit);
	btrfs_discard_calc_delay(discard_ctl);
	btrfs_discard_schedule_work(discard_ctl, true);
	return len;
}
BTRFS_ATTR_RW(discard, iops_limit, btrfs_discard_iops_limit_show,
	      btrfs_discard_iops_limit_store);

static ssize_t btrfs_discard_kbps_limit_show(struct kobject *kobj,
					     struct kobj_attribute *a,
					     char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%u\n",
			  READ_ONCE(fs_info->discard_ctl.kbps_limit));
}

static ssize_t btrfs_discard_kbps_limit_store(struct kobject *kobj,
					      struct kobj_attribute *a,
					      const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);
	struct btrfs_discard_ctl *discard_ctl = &fs_info->discard_ctl;
	u32 kbps_limit;
	int ret;

	ret = kstrtou32(buf, 10, &kbps_limit);
	if (ret)
		return -EINVAL;

	WRITE_ONCE(discard_ctl->kbps_limit, kbps_limit);
	btrfs_discard_schedule_work(discard_ctl, true);
	return len;
}
BTRFS_ATTR_RW(discard, kbps_limit, btrfs_discard_kbps_limit_show,
	      btrfs_discard_kbps_limit_store);

static ssize_t btrfs_discard_max_discard_size_show(struct kobject *kobj,
						   struct kobj_attribute *a,
						   char *buf)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);

	return sysfs_emit(buf, "%llu\n",
			  READ_ONCE(fs_info->discard_ctl.max_discard_size));
}

static ssize_t btrfs_discard_max_discard_size_store(struct kobject *kobj,
						    struct kobj_attribute *a,
						    const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = discard_to_fs_info(kobj);
	struct btrfs_discard_ctl *discard_ctl = &fs_info->discard_ctl;
	u64 max_discard_size;
	int ret;

	ret = kstrtou64(buf, 10, &max_discard_size);
	if (ret)
		return -EINVAL;

	WRITE_ONCE(discard_ctl->max_discard_size, max_discard_size);

	return len;
}
BTRFS_ATTR_RW(discard, max_discard_size, btrfs_discard_max_discard_size_show,
	      btrfs_discard_max_discard_size_store);

/*
 * Per-filesystem stats for discard (when mounted with discard=async).
 *
 * Path: /sys/fs/btrfs/<uuid>/discard/
 */
static const struct attribute *discard_attrs[] = {
	BTRFS_ATTR_PTR(discard, discardable_bytes),
	BTRFS_ATTR_PTR(discard, discardable_extents),
	BTRFS_ATTR_PTR(discard, discard_bitmap_bytes),
	BTRFS_ATTR_PTR(discard, discard_bytes_saved),
	BTRFS_ATTR_PTR(discard, discard_extent_bytes),
	BTRFS_ATTR_PTR(discard, iops_limit),
	BTRFS_ATTR_PTR(discard, kbps_limit),
	BTRFS_ATTR_PTR(discard, max_discard_size),
	NULL,
};

#ifdef CONFIG_BTRFS_DEBUG

/*
 * Per-filesystem runtime debugging exported via sysfs.
 *
 * Path: /sys/fs/btrfs/UUID/debug/
 */
static const struct attribute *btrfs_debug_mount_attrs[] = {
	NULL,
};

/*
 * Runtime debugging exported via sysfs, applies to all mounted filesystems.
 *
 * Path: /sys/fs/btrfs/debug
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
	return sysfs_emit(buf, "%llu\n", val);
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
	return sysfs_emit(buf, "%llu\n", val);
}

/*
 * Allocation information about block group profiles.
 *
 * Path: /sys/fs/btrfs/<uuid>/allocation/<bg-type>/<bg-profile>/
 */
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

static ssize_t btrfs_chunk_size_show(struct kobject *kobj,
				     struct kobj_attribute *a, char *buf)
{
	struct btrfs_space_info *sinfo = to_space_info(kobj);

	return sysfs_emit(buf, "%llu\n", READ_ONCE(sinfo->chunk_size));
}

/*
 * Store new chunk size in space info. Can be called on a read-only filesystem.
 *
 * If the new chunk size value is larger than 10% of free space it is reduced
 * to match that limit. Alignment must be to 256M and the system chunk size
 * cannot be set.
 */
static ssize_t btrfs_chunk_size_store(struct kobject *kobj,
				      struct kobj_attribute *a,
				      const char *buf, size_t len)
{
	struct btrfs_space_info *space_info = to_space_info(kobj);
	struct btrfs_fs_info *fs_info = to_fs_info(get_btrfs_kobj(kobj));
	char *retptr;
	u64 val;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!fs_info->fs_devices)
		return -EINVAL;

	if (btrfs_is_zoned(fs_info))
		return -EINVAL;

	/* System block type must not be changed. */
	if (space_info->flags & BTRFS_BLOCK_GROUP_SYSTEM)
		return -EPERM;

	val = memparse(buf, &retptr);
	/* There could be trailing '\n', also catch any typos after the value */
	retptr = skip_spaces(retptr);
	if (*retptr != 0 || val == 0)
		return -EINVAL;

	val = min(val, BTRFS_MAX_DATA_CHUNK_SIZE);

	/* Limit stripe size to 10% of available space. */
	val = min(mult_perc(fs_info->fs_devices->total_rw_bytes, 10), val);

	/* Must be multiple of 256M. */
	val &= ~((u64)SZ_256M - 1);

	/* Must be at least 256M. */
	if (val < SZ_256M)
		return -EINVAL;

	btrfs_update_space_info_chunk_size(space_info, val);

	return len;
}

#ifdef CONFIG_BTRFS_DEBUG
/*
 * Request chunk allocation with current chunk size.
 */
static ssize_t btrfs_force_chunk_alloc_store(struct kobject *kobj,
					     struct kobj_attribute *a,
					     const char *buf, size_t len)
{
	struct btrfs_space_info *space_info = to_space_info(kobj);
	struct btrfs_fs_info *fs_info = to_fs_info(get_btrfs_kobj(kobj));
	struct btrfs_trans_handle *trans;
	bool val;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (sb_rdonly(fs_info->sb))
		return -EROFS;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	if (!val)
		return -EINVAL;

	/*
	 * This is unsafe to be called from sysfs context and may cause
	 * unexpected problems.
	 */
	trans = btrfs_start_transaction(fs_info->tree_root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	ret = btrfs_force_chunk_alloc(trans, space_info->flags);
	btrfs_end_transaction(trans);

	if (ret == 1)
		return len;

	return -ENOSPC;
}
BTRFS_ATTR_W(space_info, force_chunk_alloc, btrfs_force_chunk_alloc_store);

#endif

SPACE_INFO_ATTR(flags);
SPACE_INFO_ATTR(total_bytes);
SPACE_INFO_ATTR(bytes_used);
SPACE_INFO_ATTR(bytes_pinned);
SPACE_INFO_ATTR(bytes_reserved);
SPACE_INFO_ATTR(bytes_may_use);
SPACE_INFO_ATTR(bytes_readonly);
SPACE_INFO_ATTR(bytes_zone_unusable);
SPACE_INFO_ATTR(disk_used);
SPACE_INFO_ATTR(disk_total);
BTRFS_ATTR_RW(space_info, chunk_size, btrfs_chunk_size_show, btrfs_chunk_size_store);

static ssize_t btrfs_sinfo_bg_reclaim_threshold_show(struct kobject *kobj,
						     struct kobj_attribute *a,
						     char *buf)
{
	struct btrfs_space_info *space_info = to_space_info(kobj);

	return sysfs_emit(buf, "%d\n", READ_ONCE(space_info->bg_reclaim_threshold));
}

static ssize_t btrfs_sinfo_bg_reclaim_threshold_store(struct kobject *kobj,
						      struct kobj_attribute *a,
						      const char *buf, size_t len)
{
	struct btrfs_space_info *space_info = to_space_info(kobj);
	int thresh;
	int ret;

	ret = kstrtoint(buf, 10, &thresh);
	if (ret)
		return ret;

	if (thresh < 0 || thresh > 100)
		return -EINVAL;

	WRITE_ONCE(space_info->bg_reclaim_threshold, thresh);

	return len;
}

BTRFS_ATTR_RW(space_info, bg_reclaim_threshold,
	      btrfs_sinfo_bg_reclaim_threshold_show,
	      btrfs_sinfo_bg_reclaim_threshold_store);

/*
 * Allocation information about block group types.
 *
 * Path: /sys/fs/btrfs/<uuid>/allocation/<bg-type>/
 */
static struct attribute *space_info_attrs[] = {
	BTRFS_ATTR_PTR(space_info, flags),
	BTRFS_ATTR_PTR(space_info, total_bytes),
	BTRFS_ATTR_PTR(space_info, bytes_used),
	BTRFS_ATTR_PTR(space_info, bytes_pinned),
	BTRFS_ATTR_PTR(space_info, bytes_reserved),
	BTRFS_ATTR_PTR(space_info, bytes_may_use),
	BTRFS_ATTR_PTR(space_info, bytes_readonly),
	BTRFS_ATTR_PTR(space_info, bytes_zone_unusable),
	BTRFS_ATTR_PTR(space_info, disk_used),
	BTRFS_ATTR_PTR(space_info, disk_total),
	BTRFS_ATTR_PTR(space_info, bg_reclaim_threshold),
	BTRFS_ATTR_PTR(space_info, chunk_size),
#ifdef CONFIG_BTRFS_DEBUG
	BTRFS_ATTR_PTR(space_info, force_chunk_alloc),
#endif
	NULL,
};
ATTRIBUTE_GROUPS(space_info);

static void space_info_release(struct kobject *kobj)
{
	struct btrfs_space_info *sinfo = to_space_info(kobj);
	kfree(sinfo);
}

static struct kobj_type space_info_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = space_info_release,
	.default_groups = space_info_groups,
};

/*
 * Allocation information about block groups.
 *
 * Path: /sys/fs/btrfs/<uuid>/allocation/
 */
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
	ret = sysfs_emit(buf, label[0] ? "%s\n" : "%s", label);
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
	set_bit(BTRFS_FS_NEED_TRANS_COMMIT, &fs_info->flags);
	wake_up_process(fs_info->transaction_kthread);

	return len;
}
BTRFS_ATTR_RW(, label, btrfs_label_show, btrfs_label_store);

static ssize_t btrfs_nodesize_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf, "%u\n", fs_info->super_copy->nodesize);
}

BTRFS_ATTR(, nodesize, btrfs_nodesize_show);

static ssize_t btrfs_sectorsize_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf, "%u\n", fs_info->super_copy->sectorsize);
}

BTRFS_ATTR(, sectorsize, btrfs_sectorsize_show);

static ssize_t btrfs_commit_stats_show(struct kobject *kobj,
				       struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf,
		"commits %llu\n"
		"last_commit_ms %llu\n"
		"max_commit_ms %llu\n"
		"total_commit_ms %llu\n",
		fs_info->commit_stats.commit_count,
		div_u64(fs_info->commit_stats.last_commit_dur, NSEC_PER_MSEC),
		div_u64(fs_info->commit_stats.max_commit_dur, NSEC_PER_MSEC),
		div_u64(fs_info->commit_stats.total_commit_dur, NSEC_PER_MSEC));
}

static ssize_t btrfs_commit_stats_store(struct kobject *kobj,
					struct kobj_attribute *a,
					const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	unsigned long val;
	int ret;

	if (!fs_info)
		return -EPERM;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val)
		return -EINVAL;

	WRITE_ONCE(fs_info->commit_stats.max_commit_dur, 0);

	return len;
}
BTRFS_ATTR_RW(, commit_stats, btrfs_commit_stats_show, btrfs_commit_stats_store);

static ssize_t btrfs_clone_alignment_show(struct kobject *kobj,
				struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf, "%u\n", fs_info->super_copy->sectorsize);
}

BTRFS_ATTR(, clone_alignment, btrfs_clone_alignment_show);

static ssize_t quota_override_show(struct kobject *kobj,
				   struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	int quota_override;

	quota_override = test_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags);
	return sysfs_emit(buf, "%d\n", quota_override);
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

	return sysfs_emit(buf, "%pU\n", fs_info->fs_devices->metadata_uuid);
}

BTRFS_ATTR(, metadata_uuid, btrfs_metadata_uuid_show);

static ssize_t btrfs_checksum_show(struct kobject *kobj,
				   struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	u16 csum_type = btrfs_super_csum_type(fs_info->super_copy);

	return sysfs_emit(buf, "%s (%s)\n",
			  btrfs_super_csum_name(csum_type),
			  crypto_shash_driver_name(fs_info->csum_shash));
}

BTRFS_ATTR(, checksum, btrfs_checksum_show);

static ssize_t btrfs_exclusive_operation_show(struct kobject *kobj,
		struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	const char *str;

	switch (READ_ONCE(fs_info->exclusive_operation)) {
		case  BTRFS_EXCLOP_NONE:
			str = "none\n";
			break;
		case BTRFS_EXCLOP_BALANCE:
			str = "balance\n";
			break;
		case BTRFS_EXCLOP_BALANCE_PAUSED:
			str = "balance paused\n";
			break;
		case BTRFS_EXCLOP_DEV_ADD:
			str = "device add\n";
			break;
		case BTRFS_EXCLOP_DEV_REMOVE:
			str = "device remove\n";
			break;
		case BTRFS_EXCLOP_DEV_REPLACE:
			str = "device replace\n";
			break;
		case BTRFS_EXCLOP_RESIZE:
			str = "resize\n";
			break;
		case BTRFS_EXCLOP_SWAP_ACTIVATE:
			str = "swap activate\n";
			break;
		default:
			str = "UNKNOWN\n";
			break;
	}
	return sysfs_emit(buf, "%s", str);
}
BTRFS_ATTR(, exclusive_operation, btrfs_exclusive_operation_show);

static ssize_t btrfs_generation_show(struct kobject *kobj,
				     struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf, "%llu\n", fs_info->generation);
}
BTRFS_ATTR(, generation, btrfs_generation_show);

static const char * const btrfs_read_policy_name[] = { "pid" };

static ssize_t btrfs_read_policy_show(struct kobject *kobj,
				      struct kobj_attribute *a, char *buf)
{
	struct btrfs_fs_devices *fs_devices = to_fs_devs(kobj);
	ssize_t ret = 0;
	int i;

	for (i = 0; i < BTRFS_NR_READ_POLICY; i++) {
		if (fs_devices->read_policy == i)
			ret += sysfs_emit_at(buf, ret, "%s[%s]",
					 (ret == 0 ? "" : " "),
					 btrfs_read_policy_name[i]);
		else
			ret += sysfs_emit_at(buf, ret, "%s%s",
					 (ret == 0 ? "" : " "),
					 btrfs_read_policy_name[i]);
	}

	ret += sysfs_emit_at(buf, ret, "\n");

	return ret;
}

static ssize_t btrfs_read_policy_store(struct kobject *kobj,
				       struct kobj_attribute *a,
				       const char *buf, size_t len)
{
	struct btrfs_fs_devices *fs_devices = to_fs_devs(kobj);
	int i;

	for (i = 0; i < BTRFS_NR_READ_POLICY; i++) {
		if (sysfs_streq(buf, btrfs_read_policy_name[i])) {
			if (i != fs_devices->read_policy) {
				fs_devices->read_policy = i;
				btrfs_info(fs_devices->fs_info,
					   "read policy set to '%s'",
					   btrfs_read_policy_name[i]);
			}
			return len;
		}
	}

	return -EINVAL;
}
BTRFS_ATTR_RW(, read_policy, btrfs_read_policy_show, btrfs_read_policy_store);

static ssize_t btrfs_bg_reclaim_threshold_show(struct kobject *kobj,
					       struct kobj_attribute *a,
					       char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);

	return sysfs_emit(buf, "%d\n", READ_ONCE(fs_info->bg_reclaim_threshold));
}

static ssize_t btrfs_bg_reclaim_threshold_store(struct kobject *kobj,
						struct kobj_attribute *a,
						const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(kobj);
	int thresh;
	int ret;

	ret = kstrtoint(buf, 10, &thresh);
	if (ret)
		return ret;

	if (thresh != 0 && (thresh <= 50 || thresh > 100))
		return -EINVAL;

	WRITE_ONCE(fs_info->bg_reclaim_threshold, thresh);

	return len;
}
BTRFS_ATTR_RW(, bg_reclaim_threshold, btrfs_bg_reclaim_threshold_show,
	      btrfs_bg_reclaim_threshold_store);

/*
 * Per-filesystem information and stats.
 *
 * Path: /sys/fs/btrfs/<uuid>/
 */
static const struct attribute *btrfs_attrs[] = {
	BTRFS_ATTR_PTR(, label),
	BTRFS_ATTR_PTR(, nodesize),
	BTRFS_ATTR_PTR(, sectorsize),
	BTRFS_ATTR_PTR(, clone_alignment),
	BTRFS_ATTR_PTR(, quota_override),
	BTRFS_ATTR_PTR(, metadata_uuid),
	BTRFS_ATTR_PTR(, checksum),
	BTRFS_ATTR_PTR(, exclusive_operation),
	BTRFS_ATTR_PTR(, generation),
	BTRFS_ATTR_PTR(, read_policy),
	BTRFS_ATTR_PTR(, bg_reclaim_threshold),
	BTRFS_ATTR_PTR(, commit_stats),
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

static struct kobject *get_btrfs_kobj(struct kobject *kobj)
{
	while (kobj) {
		if (kobj->ktype == &btrfs_ktype)
			return kobj;
		kobj = kobj->parent;
	}
	return NULL;
}

#define NUM_FEATURE_BITS 64
#define BTRFS_FEATURE_NAME_MAX 13
static char btrfs_unknown_feature_names[FEAT_MAX][NUM_FEATURE_BITS][BTRFS_FEATURE_NAME_MAX];
static struct btrfs_feature_attr btrfs_feature_attrs[FEAT_MAX][NUM_FEATURE_BITS];

static_assert(ARRAY_SIZE(btrfs_unknown_feature_names) ==
	      ARRAY_SIZE(btrfs_feature_attrs));
static_assert(ARRAY_SIZE(btrfs_unknown_feature_names[0]) ==
	      ARRAY_SIZE(btrfs_feature_attrs[0]));

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
	if (fs_devs->devinfo_kobj) {
		kobject_del(fs_devs->devinfo_kobj);
		kobject_put(fs_devs->devinfo_kobj);
		fs_devs->devinfo_kobj = NULL;
	}

	if (fs_devs->devices_kobj) {
		kobject_del(fs_devs->devices_kobj);
		kobject_put(fs_devs->devices_kobj);
		fs_devs->devices_kobj = NULL;
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

static void btrfs_sysfs_remove_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *seed;

	list_for_each_entry(device, &fs_devices->devices, dev_list)
		btrfs_sysfs_remove_device(device);

	list_for_each_entry(seed, &fs_devices->seed_list, seed_list) {
		list_for_each_entry(device, &seed->devices, dev_list)
			btrfs_sysfs_remove_device(device);
	}
}

void btrfs_sysfs_remove_mounted(struct btrfs_fs_info *fs_info)
{
	struct kobject *fsid_kobj = &fs_info->fs_devices->fsid_kobj;

	sysfs_remove_link(fsid_kobj, "bdi");

	if (fs_info->space_info_kobj) {
		sysfs_remove_files(fs_info->space_info_kobj, allocation_attrs);
		kobject_del(fs_info->space_info_kobj);
		kobject_put(fs_info->space_info_kobj);
	}
	if (fs_info->discard_kobj) {
		sysfs_remove_files(fs_info->discard_kobj, discard_attrs);
		kobject_del(fs_info->discard_kobj);
		kobject_put(fs_info->discard_kobj);
	}
#ifdef CONFIG_BTRFS_DEBUG
	if (fs_info->debug_kobj) {
		sysfs_remove_files(fs_info->debug_kobj, btrfs_debug_mount_attrs);
		kobject_del(fs_info->debug_kobj);
		kobject_put(fs_info->debug_kobj);
	}
#endif
	addrm_unknown_feature_attrs(fs_info, false);
	sysfs_remove_group(fsid_kobj, &btrfs_feature_attr_group);
	sysfs_remove_files(fsid_kobj, btrfs_attrs);
	btrfs_sysfs_remove_fs_devices(fs_info->fs_devices);
}

static const char * const btrfs_feature_set_names[FEAT_MAX] = {
	[FEAT_COMPAT]	 = "compat",
	[FEAT_COMPAT_RO] = "compat_ro",
	[FEAT_INCOMPAT]	 = "incompat",
};

const char *btrfs_feature_set_name(enum btrfs_feature_set set)
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
		len += scnprintf(str + len, bufsize - len, "%s%s",
				len ? "," : "", name);
	}

	return str;
}

static void init_feature_attrs(void)
{
	struct btrfs_feature_attr *fa;
	int set, i;

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

	/*
	 * We call this either on mount, or if we've created a block group for a
	 * new index type while running (i.e. when restriping).  The running
	 * case is tricky because we could race with other threads, so we need
	 * to have this check to make sure we didn't already init the kobject.
	 *
	 * We don't have to protect on the free side because it only happens on
	 * unmount.
	 */
	spin_lock(&space_info->lock);
	if (space_info->block_group_kobjs[index]) {
		spin_unlock(&space_info->lock);
		kobject_put(&rkobj->kobj);
		return;
	} else {
		space_info->block_group_kobjs[index] = &rkobj->kobj;
	}
	spin_unlock(&space_info->lock);

	ret = kobject_add(&rkobj->kobj, &space_info->kobj, "%s",
			  btrfs_bg_type_to_raid_name(rkobj->flags));
	memalloc_nofs_restore(nofs_flag);
	if (ret) {
		spin_lock(&space_info->lock);
		space_info->block_group_kobjs[index] = NULL;
		spin_unlock(&space_info->lock);
		kobject_put(&rkobj->kobj);
		btrfs_warn(fs_info,
			"failed to add kobject for block cache, ignoring");
		return;
	}
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
	}
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

void btrfs_sysfs_remove_device(struct btrfs_device *device)
{
	struct kobject *devices_kobj;

	/*
	 * Seed fs_devices devices_kobj aren't used, fetch kobject from the
	 * fs_info::fs_devices.
	 */
	devices_kobj = device->fs_info->fs_devices->devices_kobj;
	ASSERT(devices_kobj);

	if (device->bdev)
		sysfs_remove_link(devices_kobj, bdev_kobj(device->bdev)->name);

	if (device->devid_kobj.state_initialized) {
		kobject_del(&device->devid_kobj);
		kobject_put(&device->devid_kobj);
		wait_for_completion(&device->kobj_unregister);
	}
}

static ssize_t btrfs_devinfo_in_fs_metadata_show(struct kobject *kobj,
					         struct kobj_attribute *a,
					         char *buf)
{
	int val;
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	val = !!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);

	return sysfs_emit(buf, "%d\n", val);
}
BTRFS_ATTR(devid, in_fs_metadata, btrfs_devinfo_in_fs_metadata_show);

static ssize_t btrfs_devinfo_missing_show(struct kobject *kobj,
					struct kobj_attribute *a, char *buf)
{
	int val;
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	val = !!test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state);

	return sysfs_emit(buf, "%d\n", val);
}
BTRFS_ATTR(devid, missing, btrfs_devinfo_missing_show);

static ssize_t btrfs_devinfo_replace_target_show(struct kobject *kobj,
					         struct kobj_attribute *a,
					         char *buf)
{
	int val;
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	val = !!test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);

	return sysfs_emit(buf, "%d\n", val);
}
BTRFS_ATTR(devid, replace_target, btrfs_devinfo_replace_target_show);

static ssize_t btrfs_devinfo_scrub_speed_max_show(struct kobject *kobj,
					     struct kobj_attribute *a,
					     char *buf)
{
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	return sysfs_emit(buf, "%llu\n", READ_ONCE(device->scrub_speed_max));
}

static ssize_t btrfs_devinfo_scrub_speed_max_store(struct kobject *kobj,
					      struct kobj_attribute *a,
					      const char *buf, size_t len)
{
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);
	char *endptr;
	unsigned long long limit;

	limit = memparse(buf, &endptr);
	WRITE_ONCE(device->scrub_speed_max, limit);
	return len;
}
BTRFS_ATTR_RW(devid, scrub_speed_max, btrfs_devinfo_scrub_speed_max_show,
	      btrfs_devinfo_scrub_speed_max_store);

static ssize_t btrfs_devinfo_writeable_show(struct kobject *kobj,
					    struct kobj_attribute *a, char *buf)
{
	int val;
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	val = !!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);

	return sysfs_emit(buf, "%d\n", val);
}
BTRFS_ATTR(devid, writeable, btrfs_devinfo_writeable_show);

static ssize_t btrfs_devinfo_fsid_show(struct kobject *kobj,
				       struct kobj_attribute *a, char *buf)
{
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	return sysfs_emit(buf, "%pU\n", device->fs_devices->fsid);
}
BTRFS_ATTR(devid, fsid, btrfs_devinfo_fsid_show);

static ssize_t btrfs_devinfo_error_stats_show(struct kobject *kobj,
		struct kobj_attribute *a, char *buf)
{
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	if (!device->dev_stats_valid)
		return sysfs_emit(buf, "invalid\n");

	/*
	 * Print all at once so we get a snapshot of all values from the same
	 * time. Keep them in sync and in order of definition of
	 * btrfs_dev_stat_values.
	 */
	return sysfs_emit(buf,
		"write_errs %d\n"
		"read_errs %d\n"
		"flush_errs %d\n"
		"corruption_errs %d\n"
		"generation_errs %d\n",
		btrfs_dev_stat_read(device, BTRFS_DEV_STAT_WRITE_ERRS),
		btrfs_dev_stat_read(device, BTRFS_DEV_STAT_READ_ERRS),
		btrfs_dev_stat_read(device, BTRFS_DEV_STAT_FLUSH_ERRS),
		btrfs_dev_stat_read(device, BTRFS_DEV_STAT_CORRUPTION_ERRS),
		btrfs_dev_stat_read(device, BTRFS_DEV_STAT_GENERATION_ERRS));
}
BTRFS_ATTR(devid, error_stats, btrfs_devinfo_error_stats_show);

/*
 * Information about one device.
 *
 * Path: /sys/fs/btrfs/<uuid>/devinfo/<devid>/
 */
static struct attribute *devid_attrs[] = {
	BTRFS_ATTR_PTR(devid, error_stats),
	BTRFS_ATTR_PTR(devid, fsid),
	BTRFS_ATTR_PTR(devid, in_fs_metadata),
	BTRFS_ATTR_PTR(devid, missing),
	BTRFS_ATTR_PTR(devid, replace_target),
	BTRFS_ATTR_PTR(devid, scrub_speed_max),
	BTRFS_ATTR_PTR(devid, writeable),
	NULL
};
ATTRIBUTE_GROUPS(devid);

static void btrfs_release_devid_kobj(struct kobject *kobj)
{
	struct btrfs_device *device = container_of(kobj, struct btrfs_device,
						   devid_kobj);

	memset(&device->devid_kobj, 0, sizeof(struct kobject));
	complete(&device->kobj_unregister);
}

static struct kobj_type devid_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_groups = devid_groups,
	.release	= btrfs_release_devid_kobj,
};

int btrfs_sysfs_add_device(struct btrfs_device *device)
{
	int ret;
	unsigned int nofs_flag;
	struct kobject *devices_kobj;
	struct kobject *devinfo_kobj;

	/*
	 * Make sure we use the fs_info::fs_devices to fetch the kobjects even
	 * for the seed fs_devices
	 */
	devices_kobj = device->fs_info->fs_devices->devices_kobj;
	devinfo_kobj = device->fs_info->fs_devices->devinfo_kobj;
	ASSERT(devices_kobj);
	ASSERT(devinfo_kobj);

	nofs_flag = memalloc_nofs_save();

	if (device->bdev) {
		struct kobject *disk_kobj = bdev_kobj(device->bdev);

		ret = sysfs_create_link(devices_kobj, disk_kobj, disk_kobj->name);
		if (ret) {
			btrfs_warn(device->fs_info,
				"creating sysfs device link for devid %llu failed: %d",
				device->devid, ret);
			goto out;
		}
	}

	init_completion(&device->kobj_unregister);
	ret = kobject_init_and_add(&device->devid_kobj, &devid_ktype,
				   devinfo_kobj, "%llu", device->devid);
	if (ret) {
		kobject_put(&device->devid_kobj);
		btrfs_warn(device->fs_info,
			   "devinfo init for devid %llu failed: %d",
			   device->devid, ret);
	}

out:
	memalloc_nofs_restore(nofs_flag);
	return ret;
}

static int btrfs_sysfs_add_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	int ret;
	struct btrfs_device *device;
	struct btrfs_fs_devices *seed;

	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		ret = btrfs_sysfs_add_device(device);
		if (ret)
			goto fail;
	}

	list_for_each_entry(seed, &fs_devices->seed_list, seed_list) {
		list_for_each_entry(device, &seed->devices, dev_list) {
			ret = btrfs_sysfs_add_device(device);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	btrfs_sysfs_remove_fs_devices(fs_devices);
	return ret;
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

void btrfs_sysfs_update_sprout_fsid(struct btrfs_fs_devices *fs_devices)

{
	char fsid_buf[BTRFS_UUID_UNPARSED_SIZE];

	/*
	 * Sprouting changes fsid of the mounted filesystem, rename the fsid
	 * directory
	 */
	snprintf(fsid_buf, BTRFS_UUID_UNPARSED_SIZE, "%pU", fs_devices->fsid);
	if (kobject_rename(&fs_devices->fsid_kobj, fsid_buf))
		btrfs_warn(fs_devices->fs_info,
				"sysfs: failed to create fsid for sprout");
}

void btrfs_sysfs_update_devid(struct btrfs_device *device)
{
	char tmp[24];

	snprintf(tmp, sizeof(tmp), "%llu", device->devid);

	if (kobject_rename(&device->devid_kobj, tmp))
		btrfs_warn(device->fs_devices->fs_info,
			   "sysfs: failed to update devid for %llu",
			   device->devid);
}

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

/*
 * Creates:
 *		/sys/fs/btrfs/UUID
 *
 * Can be called by the device discovery thread.
 */
int btrfs_sysfs_add_fsid(struct btrfs_fs_devices *fs_devs)
{
	int error;

	init_completion(&fs_devs->kobj_unregister);
	fs_devs->fsid_kobj.kset = btrfs_kset;
	error = kobject_init_and_add(&fs_devs->fsid_kobj, &btrfs_ktype, NULL,
				     "%pU", fs_devs->fsid);
	if (error) {
		kobject_put(&fs_devs->fsid_kobj);
		return error;
	}

	fs_devs->devices_kobj = kobject_create_and_add("devices",
						       &fs_devs->fsid_kobj);
	if (!fs_devs->devices_kobj) {
		btrfs_err(fs_devs->fs_info,
			  "failed to init sysfs device interface");
		btrfs_sysfs_remove_fsid(fs_devs);
		return -ENOMEM;
	}

	fs_devs->devinfo_kobj = kobject_create_and_add("devinfo",
						       &fs_devs->fsid_kobj);
	if (!fs_devs->devinfo_kobj) {
		btrfs_err(fs_devs->fs_info,
			  "failed to init sysfs devinfo kobject");
		btrfs_sysfs_remove_fsid(fs_devs);
		return -ENOMEM;
	}

	return 0;
}

int btrfs_sysfs_add_mounted(struct btrfs_fs_info *fs_info)
{
	int error;
	struct btrfs_fs_devices *fs_devs = fs_info->fs_devices;
	struct kobject *fsid_kobj = &fs_devs->fsid_kobj;

	error = btrfs_sysfs_add_fs_devices(fs_devs);
	if (error)
		return error;

	error = sysfs_create_files(fsid_kobj, btrfs_attrs);
	if (error) {
		btrfs_sysfs_remove_fs_devices(fs_devs);
		return error;
	}

	error = sysfs_create_group(fsid_kobj,
				   &btrfs_feature_attr_group);
	if (error)
		goto failure;

#ifdef CONFIG_BTRFS_DEBUG
	fs_info->debug_kobj = kobject_create_and_add("debug", fsid_kobj);
	if (!fs_info->debug_kobj) {
		error = -ENOMEM;
		goto failure;
	}

	error = sysfs_create_files(fs_info->debug_kobj, btrfs_debug_mount_attrs);
	if (error)
		goto failure;
#endif

	/* Discard directory */
	fs_info->discard_kobj = kobject_create_and_add("discard", fsid_kobj);
	if (!fs_info->discard_kobj) {
		error = -ENOMEM;
		goto failure;
	}

	error = sysfs_create_files(fs_info->discard_kobj, discard_attrs);
	if (error)
		goto failure;

	error = addrm_unknown_feature_attrs(fs_info, true);
	if (error)
		goto failure;

	error = sysfs_create_link(fsid_kobj, &fs_info->sb->s_bdi->dev->kobj, "bdi");
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

static ssize_t qgroup_enabled_show(struct kobject *qgroups_kobj,
				   struct kobj_attribute *a,
				   char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(qgroups_kobj->parent);
	bool enabled;

	spin_lock(&fs_info->qgroup_lock);
	enabled = fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	return sysfs_emit(buf, "%d\n", enabled);
}
BTRFS_ATTR(qgroups, enabled, qgroup_enabled_show);

static ssize_t qgroup_inconsistent_show(struct kobject *qgroups_kobj,
					struct kobj_attribute *a,
					char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(qgroups_kobj->parent);
	bool inconsistent;

	spin_lock(&fs_info->qgroup_lock);
	inconsistent = (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT);
	spin_unlock(&fs_info->qgroup_lock);

	return sysfs_emit(buf, "%d\n", inconsistent);
}
BTRFS_ATTR(qgroups, inconsistent, qgroup_inconsistent_show);

static ssize_t qgroup_drop_subtree_thres_show(struct kobject *qgroups_kobj,
					      struct kobj_attribute *a,
					      char *buf)
{
	struct btrfs_fs_info *fs_info = to_fs_info(qgroups_kobj->parent);
	u8 result;

	spin_lock(&fs_info->qgroup_lock);
	result = fs_info->qgroup_drop_subtree_thres;
	spin_unlock(&fs_info->qgroup_lock);

	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t qgroup_drop_subtree_thres_store(struct kobject *qgroups_kobj,
					       struct kobj_attribute *a,
					       const char *buf, size_t len)
{
	struct btrfs_fs_info *fs_info = to_fs_info(qgroups_kobj->parent);
	u8 new_thres;
	int ret;

	ret = kstrtou8(buf, 10, &new_thres);
	if (ret)
		return -EINVAL;

	if (new_thres > BTRFS_MAX_LEVEL)
		return -EINVAL;

	spin_lock(&fs_info->qgroup_lock);
	fs_info->qgroup_drop_subtree_thres = new_thres;
	spin_unlock(&fs_info->qgroup_lock);

	return len;
}
BTRFS_ATTR_RW(qgroups, drop_subtree_threshold, qgroup_drop_subtree_thres_show,
	      qgroup_drop_subtree_thres_store);

/*
 * Qgroups global info
 *
 * Path: /sys/fs/btrfs/<uuid>/qgroups/
 */
static struct attribute *qgroups_attrs[] = {
	BTRFS_ATTR_PTR(qgroups, enabled),
	BTRFS_ATTR_PTR(qgroups, inconsistent),
	BTRFS_ATTR_PTR(qgroups, drop_subtree_threshold),
	NULL
};
ATTRIBUTE_GROUPS(qgroups);

static void qgroups_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type qgroups_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = qgroups_groups,
	.release = qgroups_release,
};

static inline struct btrfs_fs_info *qgroup_kobj_to_fs_info(struct kobject *kobj)
{
	return to_fs_info(kobj->parent->parent);
}

#define QGROUP_ATTR(_member, _show_name)					\
static ssize_t btrfs_qgroup_show_##_member(struct kobject *qgroup_kobj,		\
					   struct kobj_attribute *a,		\
					   char *buf)				\
{										\
	struct btrfs_fs_info *fs_info = qgroup_kobj_to_fs_info(qgroup_kobj);	\
	struct btrfs_qgroup *qgroup = container_of(qgroup_kobj,			\
			struct btrfs_qgroup, kobj);				\
	return btrfs_show_u64(&qgroup->_member, &fs_info->qgroup_lock, buf);	\
}										\
BTRFS_ATTR(qgroup, _show_name, btrfs_qgroup_show_##_member)

#define QGROUP_RSV_ATTR(_name, _type)						\
static ssize_t btrfs_qgroup_rsv_show_##_name(struct kobject *qgroup_kobj,	\
					     struct kobj_attribute *a,		\
					     char *buf)				\
{										\
	struct btrfs_fs_info *fs_info = qgroup_kobj_to_fs_info(qgroup_kobj);	\
	struct btrfs_qgroup *qgroup = container_of(qgroup_kobj,			\
			struct btrfs_qgroup, kobj);				\
	return btrfs_show_u64(&qgroup->rsv.values[_type],			\
			&fs_info->qgroup_lock, buf);				\
}										\
BTRFS_ATTR(qgroup, rsv_##_name, btrfs_qgroup_rsv_show_##_name)

QGROUP_ATTR(rfer, referenced);
QGROUP_ATTR(excl, exclusive);
QGROUP_ATTR(max_rfer, max_referenced);
QGROUP_ATTR(max_excl, max_exclusive);
QGROUP_ATTR(lim_flags, limit_flags);
QGROUP_RSV_ATTR(data, BTRFS_QGROUP_RSV_DATA);
QGROUP_RSV_ATTR(meta_pertrans, BTRFS_QGROUP_RSV_META_PERTRANS);
QGROUP_RSV_ATTR(meta_prealloc, BTRFS_QGROUP_RSV_META_PREALLOC);

/*
 * Qgroup information.
 *
 * Path: /sys/fs/btrfs/<uuid>/qgroups/<level>_<qgroupid>/
 */
static struct attribute *qgroup_attrs[] = {
	BTRFS_ATTR_PTR(qgroup, referenced),
	BTRFS_ATTR_PTR(qgroup, exclusive),
	BTRFS_ATTR_PTR(qgroup, max_referenced),
	BTRFS_ATTR_PTR(qgroup, max_exclusive),
	BTRFS_ATTR_PTR(qgroup, limit_flags),
	BTRFS_ATTR_PTR(qgroup, rsv_data),
	BTRFS_ATTR_PTR(qgroup, rsv_meta_pertrans),
	BTRFS_ATTR_PTR(qgroup, rsv_meta_prealloc),
	NULL
};
ATTRIBUTE_GROUPS(qgroup);

static void qgroup_release(struct kobject *kobj)
{
	struct btrfs_qgroup *qgroup = container_of(kobj, struct btrfs_qgroup, kobj);

	memset(&qgroup->kobj, 0, sizeof(*kobj));
}

static struct kobj_type qgroup_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = qgroup_release,
	.default_groups = qgroup_groups,
};

int btrfs_sysfs_add_one_qgroup(struct btrfs_fs_info *fs_info,
				struct btrfs_qgroup *qgroup)
{
	struct kobject *qgroups_kobj = fs_info->qgroups_kobj;
	int ret;

	if (test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state))
		return 0;
	if (qgroup->kobj.state_initialized)
		return 0;
	if (!qgroups_kobj)
		return -EINVAL;

	ret = kobject_init_and_add(&qgroup->kobj, &qgroup_ktype, qgroups_kobj,
			"%hu_%llu", btrfs_qgroup_level(qgroup->qgroupid),
			btrfs_qgroup_subvolid(qgroup->qgroupid));
	if (ret < 0)
		kobject_put(&qgroup->kobj);

	return ret;
}

void btrfs_sysfs_del_qgroups(struct btrfs_fs_info *fs_info)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup *next;

	if (test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state))
		return;

	rbtree_postorder_for_each_entry_safe(qgroup, next,
					     &fs_info->qgroup_tree, node)
		btrfs_sysfs_del_one_qgroup(fs_info, qgroup);
	if (fs_info->qgroups_kobj) {
		kobject_del(fs_info->qgroups_kobj);
		kobject_put(fs_info->qgroups_kobj);
		fs_info->qgroups_kobj = NULL;
	}
}

/* Called when qgroups get initialized, thus there is no need for locking */
int btrfs_sysfs_add_qgroups(struct btrfs_fs_info *fs_info)
{
	struct kobject *fsid_kobj = &fs_info->fs_devices->fsid_kobj;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup *next;
	int ret = 0;

	if (test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state))
		return 0;

	ASSERT(fsid_kobj);
	if (fs_info->qgroups_kobj)
		return 0;

	fs_info->qgroups_kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!fs_info->qgroups_kobj)
		return -ENOMEM;

	ret = kobject_init_and_add(fs_info->qgroups_kobj, &qgroups_ktype,
				   fsid_kobj, "qgroups");
	if (ret < 0)
		goto out;

	rbtree_postorder_for_each_entry_safe(qgroup, next,
					     &fs_info->qgroup_tree, node) {
		ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
		if (ret < 0)
			goto out;
	}

out:
	if (ret < 0)
		btrfs_sysfs_del_qgroups(fs_info);
	return ret;
}

void btrfs_sysfs_del_one_qgroup(struct btrfs_fs_info *fs_info,
				struct btrfs_qgroup *qgroup)
{
	if (test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state))
		return;

	if (qgroup->kobj.state_initialized) {
		kobject_del(&qgroup->kobj);
		kobject_put(&qgroup->kobj);
	}
}

/*
 * Change per-fs features in /sys/fs/btrfs/UUID/features to match current
 * values in superblock. Call after any changes to incompat/compat_ro flags
 */
void btrfs_sysfs_feature_update(struct btrfs_fs_info *fs_info)
{
	struct kobject *fsid_kobj;
	int ret;

	if (!fs_info)
		return;

	fsid_kobj = &fs_info->fs_devices->fsid_kobj;
	if (!fsid_kobj->state_initialized)
		return;

	ret = sysfs_update_group(fsid_kobj, &btrfs_feature_attr_group);
	if (ret < 0)
		btrfs_warn(fs_info,
			   "failed to update /sys/fs/btrfs/%pU/features: %d",
			   fs_info->fs_devices->fsid, ret);
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
	if (ret) {
		sysfs_unmerge_group(&btrfs_kset->kobj,
				    &btrfs_static_feature_attr_group);
		goto out_remove_group;
	}
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
#ifdef CONFIG_BTRFS_DEBUG
	sysfs_remove_group(&btrfs_kset->kobj, &btrfs_debug_feature_attr_group);
#endif
	kset_unregister(btrfs_kset);
}
