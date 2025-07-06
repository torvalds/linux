/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Red Hat, Inc.  All rights reserved.
 */

#ifndef __AOPS_DOT_H__
#define __AOPS_DOT_H__

#include "incore.h"

void adjust_fs_space(struct inode *inode);
int gfs2_jdata_writeback(struct address_space *mapping, struct writeback_control *wbc);

#endif /* __AOPS_DOT_H__ */
