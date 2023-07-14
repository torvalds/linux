/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SYSFS_H
#define BTRFS_SYSFS_H

#include <linux/kobject.h>

enum btrfs_feature_set {
	FEAT_COMPAT,
	FEAT_COMPAT_RO,
	FEAT_INCOMPAT,
	FEAT_MAX
};

char *btrfs_printable_features(enum btrfs_feature_set set, u64 flags);
const char *btrfs_feature_set_name(enum btrfs_feature_set set);
int btrfs_sysfs_add_device(struct btrfs_device *device);
void btrfs_sysfs_remove_device(struct btrfs_device *device);
int btrfs_sysfs_add_fsid(struct btrfs_fs_devices *fs_devs);
void btrfs_sysfs_remove_fsid(struct btrfs_fs_devices *fs_devs);
void btrfs_sysfs_update_sprout_fsid(struct btrfs_fs_devices *fs_devices);
void btrfs_sysfs_feature_update(struct btrfs_fs_info *fs_info);
void btrfs_kobject_uevent(struct block_device *bdev, enum kobject_action action);

int __init btrfs_init_sysfs(void);
void __cold btrfs_exit_sysfs(void);
int btrfs_sysfs_add_mounted(struct btrfs_fs_info *fs_info);
void btrfs_sysfs_remove_mounted(struct btrfs_fs_info *fs_info);
void btrfs_sysfs_add_block_group_type(struct btrfs_block_group *cache);
int btrfs_sysfs_add_space_info_type(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info);
void btrfs_sysfs_remove_space_info(struct btrfs_space_info *space_info);
void btrfs_sysfs_update_devid(struct btrfs_device *device);

int btrfs_sysfs_add_one_qgroup(struct btrfs_fs_info *fs_info,
				struct btrfs_qgroup *qgroup);
void btrfs_sysfs_del_qgroups(struct btrfs_fs_info *fs_info);
int btrfs_sysfs_add_qgroups(struct btrfs_fs_info *fs_info);
void btrfs_sysfs_del_one_qgroup(struct btrfs_fs_info *fs_info,
				struct btrfs_qgroup *qgroup);

#endif
