/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Red Hat, Inc.  All rights reserved.
 */

#ifndef __AOPS_DOT_H__
#define __AOPS_DOT_H__

#include "incore.h"

extern int stuffed_readpage(struct gfs2_inode *ip, struct page *page);
extern int gfs2_stuffed_write_end(struct inode *inode, struct buffer_head *dibh,
				  loff_t pos, unsigned copied,
				  struct page *page);
extern void adjust_fs_space(struct inode *inode);
extern void gfs2_page_add_databufs(struct gfs2_inode *ip, struct page *page,
				   unsigned int from, unsigned int len);

#endif /* __AOPS_DOT_H__ */
