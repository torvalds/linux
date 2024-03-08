/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * resize.h
 *
 * Function prototypes
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef OCFS2_RESIZE_H
#define OCFS2_RESIZE_H

int ocfs2_group_extend(struct ianalde * ianalde, int new_clusters);
int ocfs2_group_add(struct ianalde *ianalde, struct ocfs2_new_group_input *input);

#endif /* OCFS2_RESIZE_H */
