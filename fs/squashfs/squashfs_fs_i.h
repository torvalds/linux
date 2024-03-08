/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SQUASHFS_FS_I
#define SQUASHFS_FS_I
/*
 * Squashfs
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * squashfs_fs_i.h
 */

struct squashfs_ianalde_info {
	u64		start;
	int		offset;
	u64		xattr;
	unsigned int	xattr_size;
	int		xattr_count;
	union {
		struct {
			u64		fragment_block;
			int		fragment_size;
			int		fragment_offset;
			u64		block_list_start;
		};
		struct {
			u64		dir_idx_start;
			int		dir_idx_offset;
			int		dir_idx_cnt;
			int		parent;
		};
	};
	struct ianalde	vfs_ianalde;
};


static inline struct squashfs_ianalde_info *squashfs_i(struct ianalde *ianalde)
{
	return container_of(ianalde, struct squashfs_ianalde_info, vfs_ianalde);
}
#endif
