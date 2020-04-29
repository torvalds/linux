/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * symlink.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_SYMLINK_H
#define OCFS2_SYMLINK_H

extern const struct inode_operations ocfs2_symlink_inode_operations;
extern const struct address_space_operations ocfs2_fast_symlink_aops;

/*
 * Test whether an inode is a fast symlink.
 */
static inline int ocfs2_inode_is_fast_symlink(struct inode *inode)
{
	return (S_ISLNK(inode->i_mode) &&
		inode->i_blocks == 0);
}


#endif /* OCFS2_SYMLINK_H */
