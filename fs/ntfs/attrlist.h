/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Exports for attribute list attribute handling.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 * Copyright (c) 2004 Yura Pakhuchiy
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _NTFS_ATTRLIST_H
#define _NTFS_ATTRLIST_H

#include "attrib.h"

int ntfs_attrlist_need(struct ntfs_inode *ni);
int ntfs_attrlist_entry_add(struct ntfs_inode *ni, struct attr_record *attr);
int ntfs_attrlist_entry_rm(struct ntfs_attr_search_ctx *ctx);
int ntfs_attrlist_update(struct ntfs_inode *base_ni);

#endif /* defined _NTFS_ATTRLIST_H */
