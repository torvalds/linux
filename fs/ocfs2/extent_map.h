/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_map.h
 *
 * In-memory file extent mappings for OCFS2.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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

#ifndef _EXTENT_MAP_H
#define _EXTENT_MAP_H

int init_ocfs2_extent_maps(void);
void exit_ocfs2_extent_maps(void);

/*
 * EVERY CALL here except _init, _trunc, and _drop expects alloc_sem
 * to be held.  The allocation cannot change at all while the map is
 * in the process of being updated.
 */
int ocfs2_extent_map_init(struct inode *inode);
int ocfs2_extent_map_append(struct inode *inode,
			    struct ocfs2_extent_rec *rec,
			    u32 new_clusters);
int ocfs2_extent_map_get_blocks(struct inode *inode,
				u64 v_blkno, int count,
				u64 *p_blkno, int *ret_count);
int ocfs2_extent_map_drop(struct inode *inode, u32 new_clusters);
int ocfs2_extent_map_trunc(struct inode *inode, u32 new_clusters);

#endif  /* _EXTENT_MAP_H */
