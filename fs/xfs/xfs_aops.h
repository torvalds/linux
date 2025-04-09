// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2005-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_AOPS_H__
#define __XFS_AOPS_H__

extern const struct address_space_operations xfs_address_space_operations;
extern const struct address_space_operations xfs_dax_aops;

int xfs_setfilesize(struct xfs_inode *ip, xfs_off_t offset, size_t size);
void xfs_end_bio(struct bio *bio);

#endif /* __XFS_AOPS_H__ */
