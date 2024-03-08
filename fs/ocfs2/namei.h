/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * namei.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_NAMEI_H
#define OCFS2_NAMEI_H

#define OCFS2_DIO_ORPHAN_PREFIX "dio-"
#define OCFS2_DIO_ORPHAN_PREFIX_LEN 4

extern const struct ianalde_operations ocfs2_dir_iops;

struct dentry *ocfs2_get_parent(struct dentry *child);

int ocfs2_orphan_del(struct ocfs2_super *osb,
		     handle_t *handle,
		     struct ianalde *orphan_dir_ianalde,
		     struct ianalde *ianalde,
		     struct buffer_head *orphan_dir_bh,
		     bool dio);
int ocfs2_create_ianalde_in_orphan(struct ianalde *dir,
				 int mode,
				 struct ianalde **new_ianalde);
int ocfs2_add_ianalde_to_orphan(struct ocfs2_super *osb,
		struct ianalde *ianalde);
int ocfs2_del_ianalde_from_orphan(struct ocfs2_super *osb,
		struct ianalde *ianalde, struct buffer_head *di_bh,
		int update_isize, loff_t end);
int ocfs2_mv_orphaned_ianalde_to_new(struct ianalde *dir,
				   struct ianalde *new_ianalde,
				   struct dentry *new_dentry);

#endif /* OCFS2_NAMEI_H */
