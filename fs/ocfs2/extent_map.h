/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * extent_map.h
 *
 * In-memory file extent mappings for OCFS2.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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

int ocfs2_overwrite_io(struct inode *inode, struct buffer_head *di_bh,
		       u64 map_start, u64 map_len);

int ocfs2_seek_data_hole_offset(struct file *file, loff_t *offset, int origin);

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
