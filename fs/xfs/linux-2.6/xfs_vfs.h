/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_VFS_H__
#define __XFS_VFS_H__

#include <linux/vfs.h>
#include "xfs_fs.h"

struct inode;

struct fid;
struct cred;
struct seq_file;
struct super_block;
struct xfs_inode;
struct xfs_mount;
struct xfs_mount_args;

typedef struct kstatfs	bhv_statvfs_t;

#define SHUTDOWN_META_IO_ERROR	0x0001	/* write attempt to metadata failed */
#define SHUTDOWN_LOG_IO_ERROR	0x0002	/* write attempt to the log failed */
#define SHUTDOWN_FORCE_UMOUNT	0x0004	/* shutdown from a forced unmount */
#define SHUTDOWN_CORRUPT_INCORE	0x0008	/* corrupt in-memory data structures */
#define SHUTDOWN_REMOTE_REQ	0x0010	/* shutdown came from remote cell */
#define SHUTDOWN_DEVICE_REQ	0x0020	/* failed all paths to the device */

#define xfs_test_for_freeze(mp)		((mp)->m_super->s_frozen)
#define xfs_wait_for_freeze(mp,l)	vfs_check_frozen((mp)->m_super, (l))

#endif	/* __XFS_VFS_H__ */
