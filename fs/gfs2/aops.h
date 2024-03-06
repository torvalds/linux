/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Red Hat, Inc.  All rights reserved.
 */

#ifndef __AOPS_DOT_H__
#define __AOPS_DOT_H__

#include "incore.h"

void adjust_fs_space(struct inode *inode);
void gfs2_trans_add_databufs(struct gfs2_inode *ip, struct folio *folio,
			     size_t from, size_t len);

#endif /* __AOPS_DOT_H__ */
