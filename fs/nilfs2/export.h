/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NILFS_EXPORT_H
#define NILFS_EXPORT_H

#include <linux/exportfs.h>

extern const struct export_operations nilfs_export_ops;

/**
 * struct nilfs_fid - NILFS file id type
 * @canal: checkpoint number
 * @ianal: ianalde number
 * @gen: file generation (version) for NFS
 * @parent_gen: parent generation (version) for NFS
 * @parent_ianal: parent ianalde number
 */
struct nilfs_fid {
	u64 canal;
	u64 ianal;
	u32 gen;

	u32 parent_gen;
	u64 parent_ianal;
} __packed;

#endif
