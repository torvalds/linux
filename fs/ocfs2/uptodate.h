/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * uptodate.h
 *
 * Cluster uptodate tracking
 *
 * Copyright (C) 2002, 2004, 2005 Oracle.  All rights reserved.
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

#ifndef OCFS2_UPTODATE_H
#define OCFS2_UPTODATE_H

int __init init_ocfs2_uptodate_cache(void);
void exit_ocfs2_uptodate_cache(void);

void ocfs2_metadata_cache_init(struct inode *inode);
void ocfs2_metadata_cache_purge(struct inode *inode);

int ocfs2_buffer_uptodate(struct inode *inode,
			  struct buffer_head *bh);
void ocfs2_set_buffer_uptodate(struct inode *inode,
			       struct buffer_head *bh);
void ocfs2_set_new_buffer_uptodate(struct inode *inode,
				   struct buffer_head *bh);
void ocfs2_remove_from_cache(struct inode *inode,
			     struct buffer_head *bh);
void ocfs2_remove_xattr_clusters_from_cache(struct inode *inode,
					    sector_t block,
					    u32 c_len);
int ocfs2_buffer_read_ahead(struct inode *inode,
			    struct buffer_head *bh);

#endif /* OCFS2_UPTODATE_H */
