/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.h
 *
 * Function prototypes
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

#ifndef OCFS2_DIR_H
#define OCFS2_DIR_H

struct ocfs2_dx_hinfo {
	u32	major_hash;
	u32	minor_hash;
};

struct ocfs2_dir_lookup_result {
	struct buffer_head		*dl_leaf_bh;	/* Unindexed leaf
							 * block */
	struct ocfs2_dir_entry		*dl_entry;	/* Target dirent in
							 * unindexed leaf */

	struct buffer_head		*dl_dx_root_bh;	/* Root of indexed
							 * tree */

	struct buffer_head		*dl_dx_leaf_bh;	/* Indexed leaf block */
	struct ocfs2_dx_entry		*dl_dx_entry;	/* Target dx_entry in
							 * indexed leaf */
	struct ocfs2_dx_hinfo		dl_hinfo;	/* Name hash results */

	struct buffer_head		*dl_prev_leaf_bh;/* Previous entry in
							  * dir free space
							  * list. NULL if
							  * previous entry is
							  * dx root block. */
};

void ocfs2_free_dir_lookup_result(struct ocfs2_dir_lookup_result *res);

int ocfs2_find_entry(const char *name, int namelen,
		     struct inode *dir,
		     struct ocfs2_dir_lookup_result *lookup);
int ocfs2_delete_entry(handle_t *handle,
		       struct inode *dir,
		       struct ocfs2_dir_lookup_result *res);
int __ocfs2_add_entry(handle_t *handle,
		      struct inode *dir,
		      const char *name, int namelen,
		      struct inode *inode, u64 blkno,
		      struct buffer_head *parent_fe_bh,
		      struct ocfs2_dir_lookup_result *lookup);
static inline int ocfs2_add_entry(handle_t *handle,
				  struct dentry *dentry,
				  struct inode *inode, u64 blkno,
				  struct buffer_head *parent_fe_bh,
				  struct ocfs2_dir_lookup_result *lookup)
{
	return __ocfs2_add_entry(handle, dentry->d_parent->d_inode,
				 dentry->d_name.name, dentry->d_name.len,
				 inode, blkno, parent_fe_bh, lookup);
}
int ocfs2_update_entry(struct inode *dir, handle_t *handle,
		       struct ocfs2_dir_lookup_result *res,
		       struct inode *new_entry_inode);

int ocfs2_check_dir_for_entry(struct inode *dir,
			      const char *name,
			      int namelen);
int ocfs2_empty_dir(struct inode *inode);

int ocfs2_find_files_on_disk(const char *name,
			     int namelen,
			     u64 *blkno,
			     struct inode *inode,
			     struct ocfs2_dir_lookup_result *res);
int ocfs2_lookup_ino_from_name(struct inode *dir, const char *name,
			       int namelen, u64 *blkno);
int ocfs2_readdir(struct file *filp, void *dirent, filldir_t filldir);
int ocfs2_dir_foreach(struct inode *inode, loff_t *f_pos, void *priv,
		      filldir_t filldir);
int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct inode *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct ocfs2_dir_lookup_result *lookup);
struct ocfs2_alloc_context;
int ocfs2_fill_new_dir(struct ocfs2_super *osb,
		       handle_t *handle,
		       struct inode *parent,
		       struct inode *inode,
		       struct buffer_head *fe_bh,
		       struct ocfs2_alloc_context *data_ac,
		       struct ocfs2_alloc_context *meta_ac);

int ocfs2_dx_dir_truncate(struct inode *dir, struct buffer_head *di_bh);

struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_size(int blocksize,
							    void *data);
#endif /* OCFS2_DIR_H */
