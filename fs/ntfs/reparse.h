/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2008-2021 Jean-Pierre Andre
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_REPARSE_H
#define _LINUX_NTFS_REPARSE_H

extern __le16 reparse_index_name[];

unsigned int ntfs_make_symlink(struct ntfs_inode *ni);
unsigned int ntfs_reparse_tag_dt_types(struct ntfs_volume *vol, unsigned long mref);
int ntfs_reparse_set_wsl_symlink(struct ntfs_inode *ni,
			const __le16 *target, int target_len);
int ntfs_reparse_set_wsl_not_symlink(struct ntfs_inode *ni, mode_t mode);
int ntfs_delete_reparse_index(struct ntfs_inode *ni);
int ntfs_remove_ntfs_reparse_data(struct ntfs_inode *ni);

#endif /* _LINUX_NTFS_REPARSE_H */
