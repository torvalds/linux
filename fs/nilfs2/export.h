/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NILFS_EXPORT_H
#define NILFS_EXPORT_H

#include <linux/exportfs.h>

extern const struct export_operations nilfs_export_ops;

/**
 * struct nilfs_fid - NILFS file id type
 * @cyes: checkpoint number
 * @iyes: iyesde number
 * @gen: file generation (version) for NFS
 * @parent_gen: parent generation (version) for NFS
 * @parent_iyes: parent iyesde number
 */
struct nilfs_fid {
	u64 cyes;
	u64 iyes;
	u32 gen;

	u32 parent_gen;
	u64 parent_iyes;
} __packed;

#endif
