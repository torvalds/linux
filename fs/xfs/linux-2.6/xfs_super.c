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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_version.h"

#include <linux/namei.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/mempool.h>
#include <linux/writeback.h>
#include <linux/kthread.h>

STATIC struct quotactl_ops linvfs_qops;
STATIC struct super_operations linvfs_sops;
STATIC kmem_zone_t *xfs_vnode_zone;
STATIC kmem_zone_t *xfs_ioend_zone;
mempool_t *xfs_ioend_pool;

STATIC struct xfs_mount_args *
xfs_args_allocate(
	struct super_block	*sb)
{
	struct xfs_mount_args	*args;

	args = kmem_zalloc(sizeof(struct xfs_mount_args), KM_SLEEP);
	args->logbufs = args->logbufsize = -1;
	strncpy(args->fsname, sb->s_id, MAXNAMELEN);

	/* Copy the already-parsed mount(2) flags we're interested in */
	if (sb->s_flags & MS_NOATIME)
		args->flags |= XFSMNT_NOATIME;
	if (sb->s_flags & MS_DIRSYNC)
		args->flags |= XFSMNT_DIRSYNC;
	if (sb->s_flags & MS_SYNCHRONOUS)
		args->flags |= XFSMNT_WSYNC;

	/* Default to 32 bit inodes on Linux all the time */
	args->flags |= XFSMNT_32BITINODES;

	return args;
}

__uint64_t
xfs_max_file_offset(
	unsigned int		blockshift)
{
	unsigned int		pagefactor = 1;
	unsigned int		bitshift = BITS_PER_LONG - 1;

	/* Figure out maximum filesize, on Linux this can depend on
	 * the filesystem blocksize (on 32 bit platforms).
	 * __block_prepare_write does this in an [unsigned] long...
	 *      page->index << (PAGE_CACHE_SHIFT - bbits)
	 * So, for page sized blocks (4K on 32 bit platforms),
	 * this wraps at around 8Tb (hence MAX_LFS_FILESIZE which is
	 *      (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG-1))-1)
	 * but for smaller blocksizes it is less (bbits = log2 bsize).
	 * Note1: get_block_t takes a long (implicit cast from above)
	 * Note2: The Large Block Device (LBD and HAVE_SECTOR_T) patch
	 * can optionally convert the [unsigned] long from above into
	 * an [unsigned] long long.
	 */

#if BITS_PER_LONG == 32
# if defined(CONFIG_LBD)
	ASSERT(sizeof(sector_t) == 8);
	pagefactor = PAGE_CACHE_SIZE;
	bitshift = BITS_PER_LONG;
# else
	pagefactor = PAGE_CACHE_SIZE >> (PAGE_CACHE_SHIFT - blockshift);
# endif
#endif

	return (((__uint64_t)pagefactor) << bitshift) - 1;
}

STATIC __inline__ void
xfs_set_inodeops(
	struct inode		*inode)
{
	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &linvfs_file_inode_operations;
		inode->i_fop = &linvfs_file_operations;
		inode->i_mapping->a_ops = &linvfs_aops;
		break;
	case S_IFDIR:
		inode->i_op = &linvfs_dir_inode_operations;
		inode->i_fop = &linvfs_dir_operations;
		break;
	case S_IFLNK:
		inode->i_op = &linvfs_symlink_inode_operations;
		if (inode->i_blocks)
			inode->i_mapping->a_ops = &linvfs_aops;
		break;
	default:
		inode->i_op = &linvfs_file_inode_operations;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	}
}

STATIC __inline__ void
xfs_revalidate_inode(
	xfs_mount_t		*mp,
	vnode_t			*vp,
	xfs_inode_t		*ip)
{
	struct inode		*inode = LINVFS_GET_IP(vp);

	inode->i_mode	= ip->i_d.di_mode;
	inode->i_nlink	= ip->i_d.di_nlink;
	inode->i_uid	= ip->i_d.di_uid;
	inode->i_gid	= ip->i_d.di_gid;

	switch (inode->i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		inode->i_rdev =
			MKDEV(sysv_major(ip->i_df.if_u2.if_rdev) & 0x1ff,
			      sysv_minor(ip->i_df.if_u2.if_rdev));
		break;
	default:
		inode->i_rdev = 0;
		break;
	}

	inode->i_blksize = xfs_preferred_iosize(mp);
	inode->i_generation = ip->i_d.di_gen;
	i_size_write(inode, ip->i_d.di_size);
	inode->i_blocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);
	inode->i_atime.tv_sec	= ip->i_d.di_atime.t_sec;
	inode->i_atime.tv_nsec	= ip->i_d.di_atime.t_nsec;
	inode->i_mtime.tv_sec	= ip->i_d.di_mtime.t_sec;
	inode->i_mtime.tv_nsec	= ip->i_d.di_mtime.t_nsec;
	inode->i_ctime.tv_sec	= ip->i_d.di_ctime.t_sec;
	inode->i_ctime.tv_nsec	= ip->i_d.di_ctime.t_nsec;
	if (ip->i_d.di_flags & XFS_DIFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (ip->i_d.di_flags & XFS_DIFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (ip->i_d.di_flags & XFS_DIFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (ip->i_d.di_flags & XFS_DIFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
	vp->v_flag &= ~VMODIFIED;
}

void
xfs_initialize_vnode(
	bhv_desc_t		*bdp,
	vnode_t			*vp,
	bhv_desc_t		*inode_bhv,
	int			unlock)
{
	xfs_inode_t		*ip = XFS_BHVTOI(inode_bhv);
	struct inode		*inode = LINVFS_GET_IP(vp);

	if (!inode_bhv->bd_vobj) {
		vp->v_vfsp = bhvtovfs(bdp);
		bhv_desc_init(inode_bhv, ip, vp, &xfs_vnodeops);
		bhv_insert(VN_BHV_HEAD(vp), inode_bhv);
	}

	/*
	 * We need to set the ops vectors, and unlock the inode, but if
	 * we have been called during the new inode create process, it is
	 * too early to fill in the Linux inode.  We will get called a
	 * second time once the inode is properly set up, and then we can
	 * finish our work.
	 */
	if (ip->i_d.di_mode != 0 && unlock && (inode->i_state & I_NEW)) {
		xfs_revalidate_inode(XFS_BHVTOM(bdp), vp, ip);
		xfs_set_inodeops(inode);
	
		ip->i_flags &= ~XFS_INEW;
		barrier();

		unlock_new_inode(inode);
	}
}

int
xfs_blkdev_get(
	xfs_mount_t		*mp,
	const char		*name,
	struct block_device	**bdevp)
{
	int			error = 0;

	*bdevp = open_bdev_excl(name, 0, mp);
	if (IS_ERR(*bdevp)) {
		error = PTR_ERR(*bdevp);
		printk("XFS: Invalid device [%s], error=%d\n", name, error);
	}

	return -error;
}

void
xfs_blkdev_put(
	struct block_device	*bdev)
{
	if (bdev)
		close_bdev_excl(bdev);
}

/*
 * Try to write out the superblock using barriers.
 */
STATIC int
xfs_barrier_test(
	xfs_mount_t	*mp)
{
	xfs_buf_t	*sbp = xfs_getsb(mp, 0);
	int		error;

	XFS_BUF_UNDONE(sbp);
	XFS_BUF_UNREAD(sbp);
	XFS_BUF_UNDELAYWRITE(sbp);
	XFS_BUF_WRITE(sbp);
	XFS_BUF_UNASYNC(sbp);
	XFS_BUF_ORDERED(sbp);

	xfsbdstrat(mp, sbp);
	error = xfs_iowait(sbp);

	/*
	 * Clear all the flags we set and possible error state in the
	 * buffer.  We only did the write to try out whether barriers
	 * worked and shouldn't leave any traces in the superblock
	 * buffer.
	 */
	XFS_BUF_DONE(sbp);
	XFS_BUF_ERROR(sbp, 0);
	XFS_BUF_UNORDERED(sbp);

	xfs_buf_relse(sbp);
	return error;
}

void
xfs_mountfs_check_barriers(xfs_mount_t *mp)
{
	int error;

	if (mp->m_logdev_targp != mp->m_ddev_targp) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, not supported with external log device");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
	}

	if (mp->m_ddev_targp->pbr_bdev->bd_disk->queue->ordered ==
					QUEUE_ORDERED_NONE) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, not supported by the underlying device");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
	}

	error = xfs_barrier_test(mp);
	if (error) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, trial barrier write failed");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
	}
}

void
xfs_blkdev_issue_flush(
	xfs_buftarg_t		*buftarg)
{
	blkdev_issue_flush(buftarg->pbr_bdev, NULL);
}

STATIC struct inode *
linvfs_alloc_inode(
	struct super_block	*sb)
{
	vnode_t			*vp;

	vp = kmem_cache_alloc(xfs_vnode_zone, kmem_flags_convert(KM_SLEEP));
	if (!vp)
		return NULL;
	return LINVFS_GET_IP(vp);
}

STATIC void
linvfs_destroy_inode(
	struct inode		*inode)
{
	kmem_zone_free(xfs_vnode_zone, LINVFS_GET_VP(inode));
}

STATIC void
linvfs_inode_init_once(
	void			*data,
	kmem_cache_t		*cachep,
	unsigned long		flags)
{
	vnode_t			*vp = (vnode_t *)data;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(LINVFS_GET_IP(vp));
}

STATIC int
linvfs_init_zones(void)
{
	xfs_vnode_zone = kmem_cache_create("xfs_vnode",
				sizeof(vnode_t), 0, SLAB_RECLAIM_ACCOUNT,
				linvfs_inode_init_once, NULL);
	if (!xfs_vnode_zone)
		goto out;

	xfs_ioend_zone = kmem_zone_init(sizeof(xfs_ioend_t), "xfs_ioend");
	if (!xfs_ioend_zone)
		goto out_destroy_vnode_zone;

	xfs_ioend_pool = mempool_create(4 * MAX_BUF_PER_PAGE,
			mempool_alloc_slab, mempool_free_slab,
			xfs_ioend_zone);
	if (!xfs_ioend_pool)
		goto out_free_ioend_zone;

	return 0;


 out_free_ioend_zone:
	kmem_zone_destroy(xfs_ioend_zone);
 out_destroy_vnode_zone:
	kmem_zone_destroy(xfs_vnode_zone);
 out:
	return -ENOMEM;
}

STATIC void
linvfs_destroy_zones(void)
{
	mempool_destroy(xfs_ioend_pool);
	kmem_zone_destroy(xfs_vnode_zone);
	kmem_zone_destroy(xfs_ioend_zone);
}

/*
 * Attempt to flush the inode, this will actually fail
 * if the inode is pinned, but we dirty the inode again
 * at the point when it is unpinned after a log write,
 * since this is when the inode itself becomes flushable. 
 */
STATIC int
linvfs_write_inode(
	struct inode		*inode,
	int			sync)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	int			error = 0, flags = FLUSH_INODE;

	if (vp) {
		vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);
		if (sync)
			flags |= FLUSH_SYNC;
		VOP_IFLUSH(vp, flags, error);
		if (error == EAGAIN) {
			if (sync)
				VOP_IFLUSH(vp, flags | FLUSH_LOG, error);
			else
				error = 0;
		}
	}

	return -error;
}

STATIC void
linvfs_clear_inode(
	struct inode		*inode)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	int			error, cache;

	vn_trace_entry(vp, "clear_inode", (inst_t *)__return_address);

	XFS_STATS_INC(vn_rele);
	XFS_STATS_INC(vn_remove);
	XFS_STATS_INC(vn_reclaim);
	XFS_STATS_DEC(vn_active);

	/*
	 * This can happen because xfs_iget_core calls xfs_idestroy if we
	 * find an inode with di_mode == 0 but without IGET_CREATE set.
	 */
	if (vp->v_fbhv)
		VOP_INACTIVE(vp, NULL, cache);

	VN_LOCK(vp);
	vp->v_flag &= ~VMODIFIED;
	VN_UNLOCK(vp, 0);

	if (vp->v_fbhv) {
		VOP_RECLAIM(vp, error);
		if (error)
			panic("vn_purge: cannot reclaim");
	}

	ASSERT(vp->v_fbhv == NULL);

#ifdef XFS_VNODE_TRACE
	ktrace_free(vp->v_trace);
#endif
}

/*
 * Enqueue a work item to be picked up by the vfs xfssyncd thread.
 * Doing this has two advantages:
 * - It saves on stack space, which is tight in certain situations
 * - It can be used (with care) as a mechanism to avoid deadlocks.
 * Flushing while allocating in a full filesystem requires both.
 */
STATIC void
xfs_syncd_queue_work(
	struct vfs	*vfs,
	void		*data,
	void		(*syncer)(vfs_t *, void *))
{
	vfs_sync_work_t	*work;

	work = kmem_alloc(sizeof(struct vfs_sync_work), KM_SLEEP);
	INIT_LIST_HEAD(&work->w_list);
	work->w_syncer = syncer;
	work->w_data = data;
	work->w_vfs = vfs;
	spin_lock(&vfs->vfs_sync_lock);
	list_add_tail(&work->w_list, &vfs->vfs_sync_list);
	spin_unlock(&vfs->vfs_sync_lock);
	wake_up_process(vfs->vfs_sync_task);
}

/*
 * Flush delayed allocate data, attempting to free up reserved space
 * from existing allocations.  At this point a new allocation attempt
 * has failed with ENOSPC and we are in the process of scratching our
 * heads, looking about for more room...
 */
STATIC void
xfs_flush_inode_work(
	vfs_t		*vfs,
	void		*inode)
{
	filemap_flush(((struct inode *)inode)->i_mapping);
	iput((struct inode *)inode);
}

void
xfs_flush_inode(
	xfs_inode_t	*ip)
{
	struct inode	*inode = LINVFS_GET_IP(XFS_ITOV(ip));
	struct vfs	*vfs = XFS_MTOVFS(ip->i_mount);

	igrab(inode);
	xfs_syncd_queue_work(vfs, inode, xfs_flush_inode_work);
	delay(msecs_to_jiffies(500));
}

/*
 * This is the "bigger hammer" version of xfs_flush_inode_work...
 * (IOW, "If at first you don't succeed, use a Bigger Hammer").
 */
STATIC void
xfs_flush_device_work(
	vfs_t		*vfs,
	void		*inode)
{
	sync_blockdev(vfs->vfs_super->s_bdev);
	iput((struct inode *)inode);
}

void
xfs_flush_device(
	xfs_inode_t	*ip)
{
	struct inode	*inode = LINVFS_GET_IP(XFS_ITOV(ip));
	struct vfs	*vfs = XFS_MTOVFS(ip->i_mount);

	igrab(inode);
	xfs_syncd_queue_work(vfs, inode, xfs_flush_device_work);
	delay(msecs_to_jiffies(500));
	xfs_log_force(ip->i_mount, (xfs_lsn_t)0, XFS_LOG_FORCE|XFS_LOG_SYNC);
}

#define SYNCD_FLAGS	(SYNC_FSDATA|SYNC_BDFLUSH|SYNC_ATTR)
STATIC void
vfs_sync_worker(
	vfs_t		*vfsp,
	void		*unused)
{
	int		error;

	if (!(vfsp->vfs_flag & VFS_RDONLY))
		VFS_SYNC(vfsp, SYNCD_FLAGS, NULL, error);
	vfsp->vfs_sync_seq++;
	wmb();
	wake_up(&vfsp->vfs_wait_single_sync_task);
}

STATIC int
xfssyncd(
	void			*arg)
{
	long			timeleft;
	vfs_t			*vfsp = (vfs_t *) arg;
	struct vfs_sync_work	*work, *n;
	LIST_HEAD		(tmp);

	timeleft = xfs_syncd_centisecs * msecs_to_jiffies(10);
	for (;;) {
		timeleft = schedule_timeout_interruptible(timeleft);
		/* swsusp */
		try_to_freeze();
		if (kthread_should_stop())
			break;

		spin_lock(&vfsp->vfs_sync_lock);
		/*
		 * We can get woken by laptop mode, to do a sync -
		 * that's the (only!) case where the list would be
		 * empty with time remaining.
		 */
		if (!timeleft || list_empty(&vfsp->vfs_sync_list)) {
			if (!timeleft)
				timeleft = xfs_syncd_centisecs *
							msecs_to_jiffies(10);
			INIT_LIST_HEAD(&vfsp->vfs_sync_work.w_list);
			list_add_tail(&vfsp->vfs_sync_work.w_list,
					&vfsp->vfs_sync_list);
		}
		list_for_each_entry_safe(work, n, &vfsp->vfs_sync_list, w_list)
			list_move(&work->w_list, &tmp);
		spin_unlock(&vfsp->vfs_sync_lock);

		list_for_each_entry_safe(work, n, &tmp, w_list) {
			(*work->w_syncer)(vfsp, work->w_data);
			list_del(&work->w_list);
			if (work == &vfsp->vfs_sync_work)
				continue;
			kmem_free(work, sizeof(struct vfs_sync_work));
		}
	}

	return 0;
}

STATIC int
linvfs_start_syncd(
	vfs_t			*vfsp)
{
	vfsp->vfs_sync_work.w_syncer = vfs_sync_worker;
	vfsp->vfs_sync_work.w_vfs = vfsp;
	vfsp->vfs_sync_task = kthread_run(xfssyncd, vfsp, "xfssyncd");
	if (IS_ERR(vfsp->vfs_sync_task))
		return -PTR_ERR(vfsp->vfs_sync_task);
	return 0;
}

STATIC void
linvfs_stop_syncd(
	vfs_t			*vfsp)
{
	kthread_stop(vfsp->vfs_sync_task);
}

STATIC void
linvfs_put_super(
	struct super_block	*sb)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	linvfs_stop_syncd(vfsp);
	VFS_SYNC(vfsp, SYNC_ATTR|SYNC_DELWRI, NULL, error);
	if (!error)
		VFS_UNMOUNT(vfsp, 0, NULL, error);
	if (error) {
		printk("XFS unmount got error %d\n", error);
		printk("%s: vfsp/0x%p left dangling!\n", __FUNCTION__, vfsp);
		return;
	}

	vfs_deallocate(vfsp);
}

STATIC void
linvfs_write_super(
	struct super_block	*sb)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	if (sb->s_flags & MS_RDONLY) {
		sb->s_dirt = 0; /* paranoia */
		return;
	}
	/* Push the log and superblock a little */
	VFS_SYNC(vfsp, SYNC_FSDATA, NULL, error);
	sb->s_dirt = 0;
}

STATIC int
linvfs_sync_super(
	struct super_block	*sb,
	int			wait)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	int		error;
	int		flags = SYNC_FSDATA;

	if (unlikely(sb->s_frozen == SB_FREEZE_WRITE))
		flags = SYNC_QUIESCE;
	else
		flags = SYNC_FSDATA | (wait ? SYNC_WAIT : 0);

	VFS_SYNC(vfsp, flags, NULL, error);
	sb->s_dirt = 0;

	if (unlikely(laptop_mode)) {
		int	prev_sync_seq = vfsp->vfs_sync_seq;

		/*
		 * The disk must be active because we're syncing.
		 * We schedule xfssyncd now (now that the disk is
		 * active) instead of later (when it might not be).
		 */
		wake_up_process(vfsp->vfs_sync_task);
		/*
		 * We have to wait for the sync iteration to complete.
		 * If we don't, the disk activity caused by the sync
		 * will come after the sync is completed, and that
		 * triggers another sync from laptop mode.
		 */
		wait_event(vfsp->vfs_wait_single_sync_task,
				vfsp->vfs_sync_seq != prev_sync_seq);
	}

	return -error;
}

STATIC int
linvfs_statfs(
	struct super_block	*sb,
	struct kstatfs		*statp)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_STATVFS(vfsp, statp, NULL, error);
	return -error;
}

STATIC int
linvfs_remount(
	struct super_block	*sb,
	int			*flags,
	char			*options)
{
	vfs_t			*vfsp = LINVFS_GET_VFS(sb);
	struct xfs_mount_args	*args = xfs_args_allocate(sb);
	int			error;

	VFS_PARSEARGS(vfsp, options, args, 1, error);
	if (!error)
		VFS_MNTUPDATE(vfsp, flags, args, error);
	kmem_free(args, sizeof(*args));
	return -error;
}

STATIC void
linvfs_freeze_fs(
	struct super_block	*sb)
{
	VFS_FREEZE(LINVFS_GET_VFS(sb));
}

STATIC int
linvfs_show_options(
	struct seq_file		*m,
	struct vfsmount		*mnt)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(mnt->mnt_sb);
	int			error;

	VFS_SHOWARGS(vfsp, m, error);
	return error;
}

STATIC int
linvfs_quotasync(
	struct super_block	*sb,
	int			type)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_QUOTACTL(vfsp, Q_XQUOTASYNC, 0, (caddr_t)NULL, error);
	return -error;
}

STATIC int
linvfs_getxstate(
	struct super_block	*sb,
	struct fs_quota_stat	*fqs)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_QUOTACTL(vfsp, Q_XGETQSTAT, 0, (caddr_t)fqs, error);
	return -error;
}

STATIC int
linvfs_setxstate(
	struct super_block	*sb,
	unsigned int		flags,
	int			op)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(sb);
	int			error;

	VFS_QUOTACTL(vfsp, op, 0, (caddr_t)&flags, error);
	return -error;
}

STATIC int
linvfs_getxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(sb);
	int			error, getmode;

	getmode = (type == USRQUOTA) ? Q_XGETQUOTA :
		 ((type == GRPQUOTA) ? Q_XGETGQUOTA : Q_XGETPQUOTA);
	VFS_QUOTACTL(vfsp, getmode, id, (caddr_t)fdq, error);
	return -error;
}

STATIC int
linvfs_setxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	struct vfs		*vfsp = LINVFS_GET_VFS(sb);
	int			error, setmode;

	setmode = (type == USRQUOTA) ? Q_XSETQLIM :
		 ((type == GRPQUOTA) ? Q_XSETGQLIM : Q_XSETPQLIM);
	VFS_QUOTACTL(vfsp, setmode, id, (caddr_t)fdq, error);
	return -error;
}

STATIC int
linvfs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			silent)
{
	vnode_t			*rootvp;
	struct vfs		*vfsp = vfs_allocate();
	struct xfs_mount_args	*args = xfs_args_allocate(sb);
	struct kstatfs		statvfs;
	int			error, error2;

	vfsp->vfs_super = sb;
	LINVFS_SET_VFS(sb, vfsp);
	if (sb->s_flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;
	bhv_insert_all_vfsops(vfsp);

	VFS_PARSEARGS(vfsp, (char *)data, args, 0, error);
	if (error) {
		bhv_remove_all_vfsops(vfsp, 1);
		goto fail_vfsop;
	}

	sb_min_blocksize(sb, BBSIZE);
#ifdef CONFIG_XFS_EXPORT
	sb->s_export_op = &linvfs_export_ops;
#endif
	sb->s_qcop = &linvfs_qops;
	sb->s_op = &linvfs_sops;

	VFS_MOUNT(vfsp, args, NULL, error);
	if (error) {
		bhv_remove_all_vfsops(vfsp, 1);
		goto fail_vfsop;
	}

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_dirt = 1;
	sb->s_magic = statvfs.f_type;
	sb->s_blocksize = statvfs.f_bsize;
	sb->s_blocksize_bits = ffs(statvfs.f_bsize) - 1;
	sb->s_maxbytes = xfs_max_file_offset(sb->s_blocksize_bits);
	sb->s_time_gran = 1;
	set_posix_acl_flag(sb);

	VFS_ROOT(vfsp, &rootvp, error);
	if (error)
		goto fail_unmount;

	sb->s_root = d_alloc_root(LINVFS_GET_IP(rootvp));
	if (!sb->s_root) {
		error = ENOMEM;
		goto fail_vnrele;
	}
	if (is_bad_inode(sb->s_root->d_inode)) {
		error = EINVAL;
		goto fail_vnrele;
	}
	if ((error = linvfs_start_syncd(vfsp)))
		goto fail_vnrele;
	vn_trace_exit(rootvp, __FUNCTION__, (inst_t *)__return_address);

	kmem_free(args, sizeof(*args));
	return 0;

fail_vnrele:
	if (sb->s_root) {
		dput(sb->s_root);
		sb->s_root = NULL;
	} else {
		VN_RELE(rootvp);
	}

fail_unmount:
	VFS_UNMOUNT(vfsp, 0, NULL, error2);

fail_vfsop:
	vfs_deallocate(vfsp);
	kmem_free(args, sizeof(*args));
	return -error;
}

STATIC struct super_block *
linvfs_get_sb(
	struct file_system_type	*fs_type,
	int			flags,
	const char		*dev_name,
	void			*data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, linvfs_fill_super);
}

STATIC struct super_operations linvfs_sops = {
	.alloc_inode		= linvfs_alloc_inode,
	.destroy_inode		= linvfs_destroy_inode,
	.write_inode		= linvfs_write_inode,
	.clear_inode		= linvfs_clear_inode,
	.put_super		= linvfs_put_super,
	.write_super		= linvfs_write_super,
	.sync_fs		= linvfs_sync_super,
	.write_super_lockfs	= linvfs_freeze_fs,
	.statfs			= linvfs_statfs,
	.remount_fs		= linvfs_remount,
	.show_options		= linvfs_show_options,
};

STATIC struct quotactl_ops linvfs_qops = {
	.quota_sync		= linvfs_quotasync,
	.get_xstate		= linvfs_getxstate,
	.set_xstate		= linvfs_setxstate,
	.get_xquota		= linvfs_getxquota,
	.set_xquota		= linvfs_setxquota,
};

STATIC struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.get_sb			= linvfs_get_sb,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
};


STATIC int __init
init_xfs_fs( void )
{
	int			error;
	struct sysinfo		si;
	static char		message[] __initdata = KERN_INFO \
		XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled\n";

	printk(message);

	si_meminfo(&si);
	xfs_physmem = si.totalram;

	ktrace_init(64);

	error = linvfs_init_zones();
	if (error < 0)
		goto undo_zones;

	error = pagebuf_init();
	if (error < 0)
		goto undo_pagebuf;

	vn_init();
	xfs_init();
	uuid_init();
	vfs_initquota();

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto undo_register;
	XFS_DM_INIT(&xfs_fs_type);
	return 0;

undo_register:
	pagebuf_terminate();

undo_pagebuf:
	linvfs_destroy_zones();

undo_zones:
	return error;
}

STATIC void __exit
exit_xfs_fs( void )
{
	vfs_exitquota();
	XFS_DM_EXIT(&xfs_fs_type);
	unregister_filesystem(&xfs_fs_type);
	xfs_cleanup();
	pagebuf_terminate();
	linvfs_destroy_zones();
	ktrace_uninit();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
