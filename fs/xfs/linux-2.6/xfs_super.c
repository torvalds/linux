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
#include "xfs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
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

STATIC struct quotactl_ops xfs_quotactl_operations;
STATIC struct super_operations xfs_super_operations;
STATIC kmem_zone_t *xfs_vnode_zone;
STATIC kmem_zone_t *xfs_ioend_zone;
mempool_t *xfs_ioend_pool;

STATIC struct xfs_mount_args *
xfs_args_allocate(
	struct super_block	*sb,
	int			silent)
{
	struct xfs_mount_args	*args;

	args = kmem_zalloc(sizeof(struct xfs_mount_args), KM_SLEEP);
	args->logbufs = args->logbufsize = -1;
	strncpy(args->fsname, sb->s_id, MAXNAMELEN);

	/* Copy the already-parsed mount(2) flags we're interested in */
	if (sb->s_flags & MS_DIRSYNC)
		args->flags |= XFSMNT_DIRSYNC;
	if (sb->s_flags & MS_SYNCHRONOUS)
		args->flags |= XFSMNT_WSYNC;
	if (silent)
		args->flags |= XFSMNT_QUIET;
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
		inode->i_op = &xfs_inode_operations;
		inode->i_fop = &xfs_file_operations;
		inode->i_mapping->a_ops = &xfs_address_space_operations;
		break;
	case S_IFDIR:
		inode->i_op = &xfs_dir_inode_operations;
		inode->i_fop = &xfs_dir_file_operations;
		break;
	case S_IFLNK:
		inode->i_op = &xfs_symlink_inode_operations;
		if (inode->i_blocks)
			inode->i_mapping->a_ops = &xfs_address_space_operations;
		break;
	default:
		inode->i_op = &xfs_inode_operations;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	}
}

STATIC __inline__ void
xfs_revalidate_inode(
	xfs_mount_t		*mp,
	bhv_vnode_t		*vp,
	xfs_inode_t		*ip)
{
	struct inode		*inode = vn_to_inode(vp);

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
	bhv_vnode_t		*vp,
	bhv_desc_t		*inode_bhv,
	int			unlock)
{
	xfs_inode_t		*ip = XFS_BHVTOI(inode_bhv);
	struct inode		*inode = vn_to_inode(vp);

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
		return;
	}

	if (mp->m_ddev_targp->bt_bdev->bd_disk->queue->ordered ==
					QUEUE_ORDERED_NONE) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, not supported by the underlying device");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}

	error = xfs_barrier_test(mp);
	if (error) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, trial barrier write failed");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}
}

void
xfs_blkdev_issue_flush(
	xfs_buftarg_t		*buftarg)
{
	blkdev_issue_flush(buftarg->bt_bdev, NULL);
}

STATIC struct inode *
xfs_fs_alloc_inode(
	struct super_block	*sb)
{
	bhv_vnode_t		*vp;

	vp = kmem_zone_alloc(xfs_vnode_zone, KM_SLEEP);
	if (unlikely(!vp))
		return NULL;
	return vn_to_inode(vp);
}

STATIC void
xfs_fs_destroy_inode(
	struct inode		*inode)
{
	kmem_zone_free(xfs_vnode_zone, vn_from_inode(inode));
}

STATIC void
xfs_fs_inode_init_once(
	void			*vnode,
	kmem_zone_t		*zonep,
	unsigned long		flags)
{
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
		      SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(vn_to_inode((bhv_vnode_t *)vnode));
}

STATIC int
xfs_init_zones(void)
{
	xfs_vnode_zone = kmem_zone_init_flags(sizeof(bhv_vnode_t), "xfs_vnode",
					KM_ZONE_HWALIGN | KM_ZONE_RECLAIM |
					KM_ZONE_SPREAD,
					xfs_fs_inode_init_once);
	if (!xfs_vnode_zone)
		goto out;

	xfs_ioend_zone = kmem_zone_init(sizeof(xfs_ioend_t), "xfs_ioend");
	if (!xfs_ioend_zone)
		goto out_destroy_vnode_zone;

	xfs_ioend_pool = mempool_create_slab_pool(4 * MAX_BUF_PER_PAGE,
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
xfs_destroy_zones(void)
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
xfs_fs_write_inode(
	struct inode		*inode,
	int			sync)
{
	bhv_vnode_t		*vp = vn_from_inode(inode);
	int			error = 0, flags = FLUSH_INODE;

	if (vp) {
		vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);
		if (sync)
			flags |= FLUSH_SYNC;
		error = bhv_vop_iflush(vp, flags);
		if (error == EAGAIN)
			error = sync? bhv_vop_iflush(vp, flags | FLUSH_LOG) : 0;
	}
	return -error;
}

STATIC void
xfs_fs_clear_inode(
	struct inode		*inode)
{
	bhv_vnode_t		*vp = vn_from_inode(inode);

	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	XFS_STATS_INC(vn_rele);
	XFS_STATS_INC(vn_remove);
	XFS_STATS_INC(vn_reclaim);
	XFS_STATS_DEC(vn_active);

	/*
	 * This can happen because xfs_iget_core calls xfs_idestroy if we
	 * find an inode with di_mode == 0 but without IGET_CREATE set.
	 */
	if (VNHEAD(vp))
		bhv_vop_inactive(vp, NULL);

	VN_LOCK(vp);
	vp->v_flag &= ~VMODIFIED;
	VN_UNLOCK(vp, 0);

	if (VNHEAD(vp))
		if (bhv_vop_reclaim(vp))
			panic("%s: cannot reclaim 0x%p\n", __FUNCTION__, vp);

	ASSERT(VNHEAD(vp) == NULL);

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
	struct bhv_vfs	*vfs,
	void		*data,
	void		(*syncer)(bhv_vfs_t *, void *))
{
	struct bhv_vfs_sync_work *work;

	work = kmem_alloc(sizeof(struct bhv_vfs_sync_work), KM_SLEEP);
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
	bhv_vfs_t	*vfs,
	void		*inode)
{
	filemap_flush(((struct inode *)inode)->i_mapping);
	iput((struct inode *)inode);
}

void
xfs_flush_inode(
	xfs_inode_t	*ip)
{
	struct inode	*inode = vn_to_inode(XFS_ITOV(ip));
	struct bhv_vfs	*vfs = XFS_MTOVFS(ip->i_mount);

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
	bhv_vfs_t	*vfs,
	void		*inode)
{
	sync_blockdev(vfs->vfs_super->s_bdev);
	iput((struct inode *)inode);
}

void
xfs_flush_device(
	xfs_inode_t	*ip)
{
	struct inode	*inode = vn_to_inode(XFS_ITOV(ip));
	struct bhv_vfs	*vfs = XFS_MTOVFS(ip->i_mount);

	igrab(inode);
	xfs_syncd_queue_work(vfs, inode, xfs_flush_device_work);
	delay(msecs_to_jiffies(500));
	xfs_log_force(ip->i_mount, (xfs_lsn_t)0, XFS_LOG_FORCE|XFS_LOG_SYNC);
}

STATIC void
vfs_sync_worker(
	bhv_vfs_t	*vfsp,
	void		*unused)
{
	int		error;

	if (!(vfsp->vfs_flag & VFS_RDONLY))
		error = bhv_vfs_sync(vfsp, SYNC_FSDATA | SYNC_BDFLUSH | \
					SYNC_ATTR | SYNC_REFCACHE, NULL);
	vfsp->vfs_sync_seq++;
	wmb();
	wake_up(&vfsp->vfs_wait_single_sync_task);
}

STATIC int
xfssyncd(
	void			*arg)
{
	long			timeleft;
	bhv_vfs_t		*vfsp = (bhv_vfs_t *) arg;
	bhv_vfs_sync_work_t	*work, *n;
	LIST_HEAD		(tmp);

	timeleft = xfs_syncd_centisecs * msecs_to_jiffies(10);
	for (;;) {
		timeleft = schedule_timeout_interruptible(timeleft);
		/* swsusp */
		try_to_freeze();
		if (kthread_should_stop() && list_empty(&vfsp->vfs_sync_list))
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
			kmem_free(work, sizeof(struct bhv_vfs_sync_work));
		}
	}

	return 0;
}

STATIC int
xfs_fs_start_syncd(
	bhv_vfs_t		*vfsp)
{
	vfsp->vfs_sync_work.w_syncer = vfs_sync_worker;
	vfsp->vfs_sync_work.w_vfs = vfsp;
	vfsp->vfs_sync_task = kthread_run(xfssyncd, vfsp, "xfssyncd");
	if (IS_ERR(vfsp->vfs_sync_task))
		return -PTR_ERR(vfsp->vfs_sync_task);
	return 0;
}

STATIC void
xfs_fs_stop_syncd(
	bhv_vfs_t		*vfsp)
{
	kthread_stop(vfsp->vfs_sync_task);
}

STATIC void
xfs_fs_put_super(
	struct super_block	*sb)
{
	bhv_vfs_t		*vfsp = vfs_from_sb(sb);
	int			error;

	xfs_fs_stop_syncd(vfsp);
	bhv_vfs_sync(vfsp, SYNC_ATTR | SYNC_DELWRI, NULL);
	error = bhv_vfs_unmount(vfsp, 0, NULL);
	if (error) {
		printk("XFS: unmount got error=%d\n", error);
		printk("%s: vfs=0x%p left dangling!\n", __FUNCTION__, vfsp);
	} else {
		vfs_deallocate(vfsp);
	}
}

STATIC void
xfs_fs_write_super(
	struct super_block	*sb)
{
	if (!(sb->s_flags & MS_RDONLY))
		bhv_vfs_sync(vfs_from_sb(sb), SYNC_FSDATA, NULL);
	sb->s_dirt = 0;
}

STATIC int
xfs_fs_sync_super(
	struct super_block	*sb,
	int			wait)
{
	bhv_vfs_t		*vfsp = vfs_from_sb(sb);
	int			error;
	int			flags;

	if (unlikely(sb->s_frozen == SB_FREEZE_WRITE))
		flags = SYNC_QUIESCE;
	else
		flags = SYNC_FSDATA | (wait ? SYNC_WAIT : 0);

	error = bhv_vfs_sync(vfsp, flags, NULL);
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
xfs_fs_statfs(
	struct dentry		*dentry,
	struct kstatfs		*statp)
{
	return -bhv_vfs_statvfs(vfs_from_sb(dentry->d_sb), statp, NULL);
}

STATIC int
xfs_fs_remount(
	struct super_block	*sb,
	int			*flags,
	char			*options)
{
	bhv_vfs_t		*vfsp = vfs_from_sb(sb);
	struct xfs_mount_args	*args = xfs_args_allocate(sb, 0);
	int			error;

	error = bhv_vfs_parseargs(vfsp, options, args, 1);
	if (!error)
		error = bhv_vfs_mntupdate(vfsp, flags, args);
	kmem_free(args, sizeof(*args));
	return -error;
}

STATIC void
xfs_fs_lockfs(
	struct super_block	*sb)
{
	bhv_vfs_freeze(vfs_from_sb(sb));
}

STATIC int
xfs_fs_show_options(
	struct seq_file		*m,
	struct vfsmount		*mnt)
{
	return -bhv_vfs_showargs(vfs_from_sb(mnt->mnt_sb), m);
}

STATIC int
xfs_fs_quotasync(
	struct super_block	*sb,
	int			type)
{
	return -bhv_vfs_quotactl(vfs_from_sb(sb), Q_XQUOTASYNC, 0, NULL);
}

STATIC int
xfs_fs_getxstate(
	struct super_block	*sb,
	struct fs_quota_stat	*fqs)
{
	return -bhv_vfs_quotactl(vfs_from_sb(sb), Q_XGETQSTAT, 0, (caddr_t)fqs);
}

STATIC int
xfs_fs_setxstate(
	struct super_block	*sb,
	unsigned int		flags,
	int			op)
{
	return -bhv_vfs_quotactl(vfs_from_sb(sb), op, 0, (caddr_t)&flags);
}

STATIC int
xfs_fs_getxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	return -bhv_vfs_quotactl(vfs_from_sb(sb),
				 (type == USRQUOTA) ? Q_XGETQUOTA :
				  ((type == GRPQUOTA) ? Q_XGETGQUOTA :
				   Q_XGETPQUOTA), id, (caddr_t)fdq);
}

STATIC int
xfs_fs_setxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	return -bhv_vfs_quotactl(vfs_from_sb(sb),
				 (type == USRQUOTA) ? Q_XSETQLIM :
				  ((type == GRPQUOTA) ? Q_XSETGQLIM :
				   Q_XSETPQLIM), id, (caddr_t)fdq);
}

STATIC int
xfs_fs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			silent)
{
	struct bhv_vnode	*rootvp;
	struct bhv_vfs		*vfsp = vfs_allocate(sb);
	struct xfs_mount_args	*args = xfs_args_allocate(sb, silent);
	struct kstatfs		statvfs;
	int			error;

	bhv_insert_all_vfsops(vfsp);

	error = bhv_vfs_parseargs(vfsp, (char *)data, args, 0);
	if (error) {
		bhv_remove_all_vfsops(vfsp, 1);
		goto fail_vfsop;
	}

	sb_min_blocksize(sb, BBSIZE);
	sb->s_export_op = &xfs_export_operations;
	sb->s_qcop = &xfs_quotactl_operations;
	sb->s_op = &xfs_super_operations;

	error = bhv_vfs_mount(vfsp, args, NULL);
	if (error) {
		bhv_remove_all_vfsops(vfsp, 1);
		goto fail_vfsop;
	}

	error = bhv_vfs_statvfs(vfsp, &statvfs, NULL);
	if (error)
		goto fail_unmount;

	sb->s_dirt = 1;
	sb->s_magic = statvfs.f_type;
	sb->s_blocksize = statvfs.f_bsize;
	sb->s_blocksize_bits = ffs(statvfs.f_bsize) - 1;
	sb->s_maxbytes = xfs_max_file_offset(sb->s_blocksize_bits);
	sb->s_time_gran = 1;
	set_posix_acl_flag(sb);

	error = bhv_vfs_root(vfsp, &rootvp);
	if (error)
		goto fail_unmount;

	sb->s_root = d_alloc_root(vn_to_inode(rootvp));
	if (!sb->s_root) {
		error = ENOMEM;
		goto fail_vnrele;
	}
	if (is_bad_inode(sb->s_root->d_inode)) {
		error = EINVAL;
		goto fail_vnrele;
	}
	if ((error = xfs_fs_start_syncd(vfsp)))
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
	bhv_vfs_unmount(vfsp, 0, NULL);

fail_vfsop:
	vfs_deallocate(vfsp);
	kmem_free(args, sizeof(*args));
	return -error;
}

STATIC int
xfs_fs_get_sb(
	struct file_system_type	*fs_type,
	int			flags,
	const char		*dev_name,
	void			*data,
	struct vfsmount		*mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, xfs_fs_fill_super,
			   mnt);
}

STATIC struct super_operations xfs_super_operations = {
	.alloc_inode		= xfs_fs_alloc_inode,
	.destroy_inode		= xfs_fs_destroy_inode,
	.write_inode		= xfs_fs_write_inode,
	.clear_inode		= xfs_fs_clear_inode,
	.put_super		= xfs_fs_put_super,
	.write_super		= xfs_fs_write_super,
	.sync_fs		= xfs_fs_sync_super,
	.write_super_lockfs	= xfs_fs_lockfs,
	.statfs			= xfs_fs_statfs,
	.remount_fs		= xfs_fs_remount,
	.show_options		= xfs_fs_show_options,
};

STATIC struct quotactl_ops xfs_quotactl_operations = {
	.quota_sync		= xfs_fs_quotasync,
	.get_xstate		= xfs_fs_getxstate,
	.set_xstate		= xfs_fs_setxstate,
	.get_xquota		= xfs_fs_getxquota,
	.set_xquota		= xfs_fs_setxquota,
};

STATIC struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.get_sb			= xfs_fs_get_sb,
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

	error = xfs_init_zones();
	if (error < 0)
		goto undo_zones;

	error = xfs_buf_init();
	if (error < 0)
		goto undo_buffers;

	vn_init();
	xfs_init();
	uuid_init();
	vfs_initquota();

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto undo_register;
	return 0;

undo_register:
	xfs_buf_terminate();

undo_buffers:
	xfs_destroy_zones();

undo_zones:
	return error;
}

STATIC void __exit
exit_xfs_fs( void )
{
	vfs_exitquota();
	unregister_filesystem(&xfs_fs_type);
	xfs_cleanup();
	xfs_buf_terminate();
	xfs_destroy_zones();
	ktrace_uninit();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
