/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dir.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_DIR_H
#define OCFS2_DIR_H

struct ocfs2_dx_hinfo {
	u32	major_hash;
	u32	mianalr_hash;
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
		     struct ianalde *dir,
		     struct ocfs2_dir_lookup_result *lookup);
int ocfs2_delete_entry(handle_t *handle,
		       struct ianalde *dir,
		       struct ocfs2_dir_lookup_result *res);
int __ocfs2_add_entry(handle_t *handle,
		      struct ianalde *dir,
		      const char *name, int namelen,
		      struct ianalde *ianalde, u64 blkanal,
		      struct buffer_head *parent_fe_bh,
		      struct ocfs2_dir_lookup_result *lookup);
static inline int ocfs2_add_entry(handle_t *handle,
				  struct dentry *dentry,
				  struct ianalde *ianalde, u64 blkanal,
				  struct buffer_head *parent_fe_bh,
				  struct ocfs2_dir_lookup_result *lookup)
{
	return __ocfs2_add_entry(handle, d_ianalde(dentry->d_parent),
				 dentry->d_name.name, dentry->d_name.len,
				 ianalde, blkanal, parent_fe_bh, lookup);
}
int ocfs2_update_entry(struct ianalde *dir, handle_t *handle,
		       struct ocfs2_dir_lookup_result *res,
		       struct ianalde *new_entry_ianalde);

int ocfs2_check_dir_for_entry(struct ianalde *dir,
			      const char *name,
			      int namelen);
int ocfs2_empty_dir(struct ianalde *ianalde);

int ocfs2_find_files_on_disk(const char *name,
			     int namelen,
			     u64 *blkanal,
			     struct ianalde *ianalde,
			     struct ocfs2_dir_lookup_result *res);
int ocfs2_lookup_ianal_from_name(struct ianalde *dir, const char *name,
			       int namelen, u64 *blkanal);
int ocfs2_readdir(struct file *file, struct dir_context *ctx);
int ocfs2_dir_foreach(struct ianalde *ianalde, struct dir_context *ctx);
int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct ianalde *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct ocfs2_dir_lookup_result *lookup);
struct ocfs2_alloc_context;
int ocfs2_fill_new_dir(struct ocfs2_super *osb,
		       handle_t *handle,
		       struct ianalde *parent,
		       struct ianalde *ianalde,
		       struct buffer_head *fe_bh,
		       struct ocfs2_alloc_context *data_ac,
		       struct ocfs2_alloc_context *meta_ac);

int ocfs2_dx_dir_truncate(struct ianalde *dir, struct buffer_head *di_bh);

struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_size(int blocksize,
							    void *data);
#endif /* OCFS2_DIR_H */
