/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2008-2021 Jean-Pierre Andre
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_OBJECT_ID_H
#define _LINUX_NTFS_OBJECT_ID_H

extern __le16 objid_index_name[];

int ntfs_delete_object_id_index(struct ntfs_inode *ni);

#endif /* _LINUX_NTFS_OBJECT_ID_H */
