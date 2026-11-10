/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_IOMAP_H
#define _LINUX_NTFS_IOMAP_H

#include <linux/pagemap.h>
#include <linux/iomap.h>

#include "volume.h"
#include "inode.h"

DECLARE_IOMAP_ITER_NEXT(ntfs_write_iomap_next);
DECLARE_IOMAP_ITER_NEXT(ntfs_read_iomap_next);
DECLARE_IOMAP_ITER_NEXT(ntfs_seek_iomap_next);
DECLARE_IOMAP_ITER_NEXT(ntfs_page_mkwrite_iomap_next);
DECLARE_IOMAP_ITER_NEXT(ntfs_dio_iomap_next);
extern const struct iomap_writeback_ops ntfs_writeback_ops;
extern const struct iomap_write_ops ntfs_iomap_folio_ops;
extern int ntfs_dio_zero_range(struct inode *inode, loff_t offset, loff_t length);
#endif /* _LINUX_NTFS_IOMAP_H */
