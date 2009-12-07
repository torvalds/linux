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

struct ocfs2_extent_map_item {
	unsigned int			ei_cpos;
	unsigned int			ei_phys;
	unsigned int			ei_clusters;
	unsigned int			ei_flags;

	struct list_head		ei_list;
};

#define OCFS2_MAX_EXTENT_MAP_ITEMS			3
struct ocfs2_extent_map {
	unsigned int			em_num_items;
	struct list_head		em_list;
};

void ocfs2_extent_map_init(struct inode *inode);
void ocfs2_extent_map_trunc(struct inode *inode, unsigned int cluster);
void ocfs2_extent_map_insert_rec(struct inode *inode,
				 struct ocfs2_extent_rec *rec);

int ocfs2_get_clusters(struct inode *inode, u32 v_cluster, u32 *p_cluster,
		       u32 *num_clusters, unsigned int *extent_flags);
int ocfs2_extent_map_get_blocks(struct inode *inode, u64 v_blkno, u64 *p_blkno,
				u64 *ret_count, unsigned int *extent_flags);

int ocfs2_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 map_start, u64 map_len);

int ocfs2_xattr_get_clusters(struct inode *inode, u32 v_cluster,
			     u32 *p_cluster, u32 *num_clusters,
			     struct ocfs2_extent_list *el,
			     unsigned int *extent_flags);

int ocfs2_read_virt_blocks(struct inode *inode, u64 v_block, int nr,
			   struct buffer_head *bhs[], int flags,
			   int (*validate)(struct super_block *sb,
					   struct buffer_head *bh));
int ocfs2_figure_hole_clusters(struct ocfs2_caching_info *ci,
			       struct ocfs2_extent_list *el,
			       struct buffer_head *eb_bh,
			       u32 v_cluster,
			       u32 *num_clusters);
static inline int ocfs2_read_virt_block(struct inode *inode, u64 v_block,
					struct buffer_head **bh,
					int (*validate)(struct super_block *sb,
							struct buffer_head *bh))
{
	int status = 0;

	if (bh == NULL) {
		printk("ocfs2: bh == NULL\n");
		status = -EINVAL;
		goto bail;
	}

	status = ocfs2_read_virt_blocks(inode, v_block, 1, bh, 0, validate);

bail:
	return status;
}


#endif  /* _EXTENT_MAP_H */
