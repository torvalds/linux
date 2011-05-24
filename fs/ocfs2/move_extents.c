/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * move_extents.c
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/swap.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "ocfs2_ioctl.h"

#include "alloc.h"
#include "aops.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "inode.h"
#include "journal.h"
#include "suballoc.h"
#include "uptodate.h"
#include "super.h"
#include "dir.h"
#include "buffer_head_io.h"
#include "sysfile.h"
#include "suballoc.h"
#include "refcounttree.h"
#include "move_extents.h"

struct ocfs2_move_extents_context {
	struct inode *inode;
	struct file *file;
	int auto_defrag;
	int credits;
	u32 new_phys_cpos;
	u32 clusters_moved;
	u64 refcount_loc;
	struct ocfs2_move_extents *range;
	struct ocfs2_extent_tree et;
	struct ocfs2_alloc_context *meta_ac;
	struct ocfs2_alloc_context *data_ac;
	struct ocfs2_cached_dealloc_ctxt dealloc;
};
