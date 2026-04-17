/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for directory handling in NTFS Linux kernel driver.
 *
 * Copyright (c) 2002-2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_DIR_H
#define _LINUX_NTFS_DIR_H

#include "inode.h"

/*
 * ntfs_name is used to return the file name to the caller of
 * ntfs_lookup_inode_by_name() in order for the caller (namei.c::ntfs_lookup())
 * to be able to deal with dcache aliasing issues.
 */
struct ntfs_name {
	u64 mref;
	u8 type;
	u8 len;
	__le16 name[];
} __packed;

/* The little endian Unicode string $I30 as a global constant. */
extern __le16 I30[5];

u64 ntfs_lookup_inode_by_name(struct ntfs_inode *dir_ni,
		const __le16 *uname, const int uname_len, struct ntfs_name **res);
int ntfs_check_empty_dir(struct ntfs_inode *ni, struct mft_record *ni_mrec);

#endif /* _LINUX_NTFS_FS_DIR_H */
