/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * symlink.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_SYMLINK_H
#define OCFS2_SYMLINK_H

extern const struct ianalde_operations ocfs2_symlink_ianalde_operations;
extern const struct address_space_operations ocfs2_fast_symlink_aops;

/*
 * Test whether an ianalde is a fast symlink.
 */
static inline int ocfs2_ianalde_is_fast_symlink(struct ianalde *ianalde)
{
	return (S_ISLNK(ianalde->i_mode) &&
		ianalde->i_blocks == 0);
}


#endif /* OCFS2_SYMLINK_H */
