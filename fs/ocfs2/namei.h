/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * namei.h
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

#ifndef OCFS2_NAMEI_H
#define OCFS2_NAMEI_H

extern const struct inode_operations ocfs2_dir_iops;

struct dentry *ocfs2_get_parent(struct dentry *child);

int ocfs2_check_dir_entry (struct inode *dir,
			   struct ocfs2_dir_entry *de,
			   struct buffer_head *bh,
			   unsigned long offset);
struct buffer_head *ocfs2_find_entry(const char *name,
				     int namelen,
				     struct inode *dir,
				     struct ocfs2_dir_entry **res_dir);
int ocfs2_orphan_del(struct ocfs2_super *osb,
		     handle_t *handle,
		     struct inode *orphan_dir_inode,
		     struct inode *inode,
		     struct buffer_head *orphan_dir_bh);

static inline int ocfs2_match(int len,
			      const char * const name,
			      struct ocfs2_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

#endif /* OCFS2_NAMEI_H */
