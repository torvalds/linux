/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_imap.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_quota.h"

int
vfs_mount(
	struct bhv_desc		*bdp,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_mount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_mount)(next, args, cr));
}

int
vfs_parseargs(
	struct bhv_desc		*bdp,
	char			*s,
	struct xfs_mount_args	*args,
	int			f)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_parseargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_parseargs)(next, s, args, f));
}

int
vfs_showargs(
	struct bhv_desc		*bdp,
	struct seq_file		*m)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_showargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_showargs)(next, m));
}

int
vfs_unmount(
	struct bhv_desc		*bdp,
	int			fl,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_unmount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_unmount)(next, fl, cr));
}

int
vfs_mntupdate(
	struct bhv_desc		*bdp,
	int			*fl,
	struct xfs_mount_args	*args)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_mntupdate)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_mntupdate)(next, fl, args));
}

int
vfs_root(
	struct bhv_desc		*bdp,
	bhv_vnode_t		**vpp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_root)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_root)(next, vpp));
}

int
vfs_statvfs(
	struct bhv_desc		*bdp,
	bhv_statvfs_t		*statp,
	bhv_vnode_t		*vp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_statvfs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_statvfs)(next, statp, vp));
}

int
vfs_sync(
	struct bhv_desc		*bdp,
	int			fl,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_sync)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_sync)(next, fl, cr));
}

int
vfs_vget(
	struct bhv_desc		*bdp,
	bhv_vnode_t		**vpp,
	struct fid		*fidp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_vget)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_vget)(next, vpp, fidp));
}

void
vfs_init_vnode(
	struct bhv_desc		*bdp,
	bhv_vnode_t		*vp,
	struct xfs_inode	*ip,
	int			unlock)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_init_vnode)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_init_vnode)(next, vp, ip, unlock));
}

void
vfs_force_shutdown(
	struct bhv_desc		*bdp,
	int			fl,
	char			*file,
	int			line)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_force_shutdown)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_force_shutdown)(next, fl, file, line));
}

void
vfs_freeze(
	struct bhv_desc		*bdp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_freeze)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_freeze)(next));
}

bhv_vfs_t *
vfs_allocate(
	struct super_block	*sb)
{
	struct bhv_vfs		*vfsp;

	vfsp = kmem_zalloc(sizeof(bhv_vfs_t), KM_SLEEP);
	INIT_LIST_HEAD(&vfsp->vfs_sync_list);
	spin_lock_init(&vfsp->vfs_sync_lock);
	init_waitqueue_head(&vfsp->vfs_wait_single_sync_task);

	vfsp->vfs_super = sb;
	sb->s_fs_info = vfsp;

	return vfsp;
}

bhv_vfs_t *
vfs_from_sb(
	struct super_block	*sb)
{
	return (bhv_vfs_t *)sb->s_fs_info;
}

void
vfs_deallocate(
	struct bhv_vfs		*vfsp)
{
	kmem_free(vfsp, sizeof(bhv_vfs_t));
}
