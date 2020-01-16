/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * resize.h
 *
 * Function prototypes
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef OCFS2_RESIZE_H
#define OCFS2_RESIZE_H

int ocfs2_group_extend(struct iyesde * iyesde, int new_clusters);
int ocfs2_group_add(struct iyesde *iyesde, struct ocfs2_new_group_input *input);

#endif /* OCFS2_RESIZE_H */
