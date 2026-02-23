/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_VERIFY_MEDIA_H__
#define __XFS_VERIFY_MEDIA_H__

struct xfs_verify_media;
int xfs_ioc_verify_media(struct file *file,
		struct xfs_verify_media __user *arg);

#endif /* __XFS_VERIFY_MEDIA_H__ */
