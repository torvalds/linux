/*
 * seglist.h - expediential structure and routines to handle list of segments
 *             (would be removed in a future release)
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */
#ifndef _NILFS_SEGLIST_H
#define _NILFS_SEGLIST_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_fs.h>
#include "sufile.h"

struct nilfs_segment_entry {
	__u64			segnum;

#define NILFS_SLH_FREED		0x0001	/* The segment was freed provisonally.
					   It must be cancelled if
					   construction aborted */

	unsigned		flags;
	struct list_head	list;
	struct buffer_head     *bh_su;
	struct nilfs_segment_usage *raw_su;
};


void nilfs_dispose_segment_list(struct list_head *);

static inline struct nilfs_segment_entry *
nilfs_alloc_segment_entry(__u64 segnum)
{
	struct nilfs_segment_entry *ent = kmalloc(sizeof(*ent), GFP_NOFS);

	if (likely(ent)) {
		ent->segnum = segnum;
		ent->flags = 0;
		ent->bh_su = NULL;
		ent->raw_su = NULL;
		INIT_LIST_HEAD(&ent->list);
	}
	return ent;
}

static inline int nilfs_open_segment_entry(struct nilfs_segment_entry *ent,
					   struct inode *sufile)
{
	return nilfs_sufile_get_segment_usage(sufile, ent->segnum,
					      &ent->raw_su, &ent->bh_su);
}

static inline void nilfs_close_segment_entry(struct nilfs_segment_entry *ent,
					     struct inode *sufile)
{
	if (!ent->bh_su)
		return;
	nilfs_sufile_put_segment_usage(sufile, ent->segnum, ent->bh_su);
	ent->bh_su = NULL;
	ent->raw_su = NULL;
}

static inline void nilfs_free_segment_entry(struct nilfs_segment_entry *ent)
{
	kfree(ent);
}

#endif /* _NILFS_SEGLIST_H */
