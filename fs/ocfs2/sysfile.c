/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * sysfile.c
 *
 * Initialize, read, write, etc. system files.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#include "ocfs2.h"

#define MLOG_MASK_PREFIX ML_INODE
#include <cluster/masklog.h>

#include "alloc.h"
#include "dir.h"
#include "inode.h"
#include "journal.h"
#include "sysfile.h"

#include "buffer_head_io.h"

static struct inode * _ocfs2_get_system_file_inode(struct ocfs2_super *osb,
						   int type,
						   u32 slot);

static inline int is_global_system_inode(int type);
static inline int is_in_system_inode_array(struct ocfs2_super *osb,
					   int type,
					   u32 slot);

static inline int is_global_system_inode(int type)
{
	return type >= OCFS2_FIRST_ONLINE_SYSTEM_INODE &&
		type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE;
}

static inline int is_in_system_inode_array(struct ocfs2_super *osb,
					   int type,
					   u32 slot)
{
	return slot == osb->slot_num || is_global_system_inode(type);
}

struct inode *ocfs2_get_system_file_inode(struct ocfs2_super *osb,
					  int type,
					  u32 slot)
{
	struct inode *inode = NULL;
	struct inode **arr = NULL;

	/* avoid the lookup if cached in local system file array */
	if (is_in_system_inode_array(osb, type, slot))
		arr = &(osb->system_inodes[type]);

	if (arr && ((inode = *arr) != NULL)) {
		/* get a ref in addition to the array ref */
		inode = igrab(inode);
		if (!inode)
			BUG();

		return inode;
	}

	/* this gets one ref thru iget */
	inode = _ocfs2_get_system_file_inode(osb, type, slot);

	/* add one more if putting into array for first time */
	if (arr && inode) {
		*arr = igrab(inode);
		if (!*arr)
			BUG();
	}
	return inode;
}

static struct inode * _ocfs2_get_system_file_inode(struct ocfs2_super *osb,
						   int type,
						   u32 slot)
{
	char namebuf[40];
	struct inode *inode = NULL;
	u64 blkno;
	struct buffer_head *dirent_bh = NULL;
	struct ocfs2_dir_entry *de = NULL;
	int status = 0;

	ocfs2_sprintf_system_inode_name(namebuf,
					sizeof(namebuf),
					type, slot);

	status = ocfs2_find_files_on_disk(namebuf, strlen(namebuf),
					  &blkno, osb->sys_root_inode,
					  &dirent_bh, &de);
	if (status < 0) {
		goto bail;
	}

	inode = ocfs2_iget(osb, blkno);
	if (IS_ERR(inode)) {
		mlog_errno(PTR_ERR(inode));
		inode = NULL;
		goto bail;
	}
bail:
	if (dirent_bh)
		brelse(dirent_bh);
	return inode;
}

