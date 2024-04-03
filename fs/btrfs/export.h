/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXPORT_H
#define BTRFS_EXPORT_H

#include <linux/exportfs.h>
#include <linux/types.h>

struct dentry;
struct super_block;

extern const struct export_operations btrfs_export_ops;

struct btrfs_fid {
	u64 objectid;
	u64 root_objectid;
	u32 gen;

	u64 parent_objectid;
	u32 parent_gen;

	u64 parent_root_objectid;
} __attribute__ ((packed));

struct dentry *btrfs_get_dentry(struct super_block *sb, u64 objectid,
				u64 root_objectid, u64 generation);
struct dentry *btrfs_get_parent(struct dentry *child);

#endif
