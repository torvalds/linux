/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dir.h - Defines for directory handling in NTFS Linux kernel driver. Part of
 *	   the Linux-NTFS project.
 *
 * Copyright (c) 2002-2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_DIR_H
#define _LINUX_NTFS_DIR_H

#include "layout.h"
#include "inode.h"
#include "types.h"

/*
 * ntfs_name is used to return the file name to the caller of
 * ntfs_lookup_inode_by_name() in order for the caller (namei.c::ntfs_lookup())
 * to be able to deal with dcache aliasing issues.
 */
typedef struct {
	MFT_REF mref;
	FILE_NAME_TYPE_FLAGS type;
	u8 len;
	ntfschar name[0];
} __attribute__ ((__packed__)) ntfs_name;

/* The little endian Unicode string $I30 as a global constant. */
extern ntfschar I30[5];

extern MFT_REF ntfs_lookup_inode_by_name(ntfs_inode *dir_ni,
		const ntfschar *uname, const int uname_len, ntfs_name **res);

#endif /* _LINUX_NTFS_FS_DIR_H */
