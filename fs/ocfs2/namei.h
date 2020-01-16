/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
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

extern const struct iyesde_operations ocfs2_dir_iops;

struct dentry *ocfs2_get_parent(struct dentry *child);

int ocfs2_orphan_del(struct ocfs2_super *osb,
		     handle_t *handle,
		     struct iyesde *orphan_dir_iyesde,
		     struct iyesde *iyesde,
		     struct buffer_head *orphan_dir_bh,
		     bool dio);
int ocfs2_create_iyesde_in_orphan(struct iyesde *dir,
				 int mode,
				 struct iyesde **new_iyesde);
int ocfs2_add_iyesde_to_orphan(struct ocfs2_super *osb,
		struct iyesde *iyesde);
int ocfs2_del_iyesde_from_orphan(struct ocfs2_super *osb,
		struct iyesde *iyesde, struct buffer_head *di_bh,
		int update_isize, loff_t end);
int ocfs2_mv_orphaned_iyesde_to_new(struct iyesde *dir,
				   struct iyesde *new_iyesde,
				   struct dentry *new_dentry);

#endif /* OCFS2_NAMEI_H */
