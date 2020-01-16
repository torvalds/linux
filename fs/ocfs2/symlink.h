/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * symlink.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_SYMLINK_H
#define OCFS2_SYMLINK_H

extern const struct iyesde_operations ocfs2_symlink_iyesde_operations;
extern const struct address_space_operations ocfs2_fast_symlink_aops;

/*
 * Test whether an iyesde is a fast symlink.
 */
static inline int ocfs2_iyesde_is_fast_symlink(struct iyesde *iyesde)
{
	return (S_ISLNK(iyesde->i_mode) &&
		iyesde->i_blocks == 0);
}


#endif /* OCFS2_SYMLINK_H */
