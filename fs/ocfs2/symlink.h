/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * symlink.h
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

#ifndef OCFS2_SYMLINK_H
#define OCFS2_SYMLINK_H

extern struct inode_operations ocfs2_symlink_inode_operations;
extern struct inode_operations ocfs2_fast_symlink_inode_operations;

/*
 * Test whether an inode is a fast symlink.
 */
static inline int ocfs2_inode_is_fast_symlink(struct inode *inode)
{
	return (S_ISLNK(inode->i_mode) &&
		inode->i_blocks == 0);
}


#endif /* OCFS2_SYMLINK_H */
