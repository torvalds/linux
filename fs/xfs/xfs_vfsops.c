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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_refcache.h"
#include "xfs_buf_item.h"
#include "xfs_log_priv.h"
#include "xfs_dir2_trace.h"
#include "xfs_extfree_item.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_clnt.h"

STATIC int xfs_sync(bhv_desc_t *, int, cred_t *);

int
xfs_init(void)
{
	extern kmem_zone_t	*xfs_bmap_free_item_zone;
	extern kmem_zone_t	*xfs_btree_cur_zone;
	extern kmem_zone_t	*xfs_trans_zone;
	extern kmem_zone_t	*xfs_buf_item_zone;
	extern kmem_zone_t	*xfs_dabuf_zone;
#ifdef XFS_DABUF_DEBUG
	extern lock_t	        xfs_dabuf_global_lock;
	spinlock_init(&xfs_dabuf_global_lock, "xfsda");
#endif

	/*
	 * Initialize all of the zone allocators we use.
	 */
	xfs_bmap_free_item_zone = kmem_zone_init(sizeof(xfs_bmap_free_item_t),
						 "xfs_bmap_free_item");
	xfs_btree_cur_zone = kmem_zone_init(sizeof(xfs_btree_cur_t),
					    "xfs_btree_cur");
	xfs_inode_zone = kmem_zone_init(sizeof(xfs_inode_t), "xfs_inode");
	xfs_trans_zone = kmem_zone_init(sizeof(xfs_trans_t), "xfs_trans");
	xfs_da_state_zone =
		kmem_zone_init(sizeof(xfs_da_state_t), "xfs_da_state");
	xfs_dabuf_zone = kmem_zone_init(sizeof(xfs_dabuf_t), "xfs_dabuf");

	/*
	 * The size of the zone allocated buf log item is the maximum
	 * size possible under XFS.  This wastes a little bit of memory,
	 * but it is much faster.
	 */
	xfs_buf_item_zone =
		kmem_zone_init((sizeof(xfs_buf_log_item_t) +
				(((XFS_MAX_BLOCKSIZE / XFS_BLI_CHUNK) /
				  NBWORD) * sizeof(int))),
			       "xfs_buf_item");
	xfs_efd_zone = kmem_zone_init((sizeof(xfs_efd_log_item_t) +
				       ((XFS_EFD_MAX_FAST_EXTENTS - 1) * sizeof(xfs_extent_t))),
				      "xfs_efd_item");
	xfs_efi_zone = kmem_zone_init((sizeof(xfs_efi_log_item_t) +
				       ((XFS_EFI_MAX_FAST_EXTENTS - 1) * sizeof(xfs_extent_t))),
				      "xfs_efi_item");
	xfs_ifork_zone = kmem_zone_init(sizeof(xfs_ifork_t), "xfs_ifork");
	xfs_ili_zone = kmem_zone_init(sizeof(xfs_inode_log_item_t), "xfs_ili");
	xfs_chashlist_zone = kmem_zone_init(sizeof(xfs_chashlist_t),
					    "xfs_chashlist");
	xfs_acl_zone_init(xfs_acl_zone, "xfs_acl");

	/*
	 * Allocate global trace buffers.
	 */
#ifdef XFS_ALLOC_TRACE
	xfs_alloc_trace_buf = ktrace_alloc(XFS_ALLOC_TRACE_SIZE, KM_SLEEP);
#endif
#ifdef XFS_BMAP_TRACE
	xfs_bmap_trace_buf = ktrace_alloc(XFS_BMAP_TRACE_SIZE, KM_SLEEP);
#endif
#ifdef XFS_BMBT_TRACE
	xfs_bmbt_trace_buf = ktrace_alloc(XFS_BMBT_TRACE_SIZE, KM_SLEEP);
#endif
#ifdef XFS_DIR_TRACE
	xfs_dir_trace_buf = ktrace_alloc(XFS_DIR_TRACE_SIZE, KM_SLEEP);
#endif
#ifdef XFS_ATTR_TRACE
	xfs_attr_trace_buf = ktrace_alloc(XFS_ATTR_TRACE_SIZE, KM_SLEEP);
#endif
#ifdef XFS_DIR2_TRACE
	xfs_dir2_trace_buf = ktrace_alloc(XFS_DIR2_GTRACE_SIZE, KM_SLEEP);
#endif

	xfs_dir_startup();

#if (defined(DEBUG) || defined(INDUCE_IO_ERROR))
	xfs_error_test_init();
#endif /* DEBUG || INDUCE_IO_ERROR */

	xfs_init_procfs();
	xfs_sysctl_register();
	return 0;
}

void
xfs_cleanup(void)
{
	extern kmem_zone_t	*xfs_bmap_free_item_zone;
	extern kmem_zone_t	*xfs_btree_cur_zone;
	extern kmem_zone_t	*xfs_inode_zone;
	extern kmem_zone_t	*xfs_trans_zone;
	extern kmem_zone_t	*xfs_da_state_zone;
	extern kmem_zone_t	*xfs_dabuf_zone;
	extern kmem_zone_t	*xfs_efd_zone;
	extern kmem_zone_t	*xfs_efi_zone;
	extern kmem_zone_t	*xfs_buf_item_zone;
	extern kmem_zone_t	*xfs_chashlist_zone;

	xfs_cleanup_procfs();
	xfs_sysctl_unregister();
	xfs_refcache_destroy();
	xfs_acl_zone_destroy(xfs_acl_zone);

#ifdef XFS_DIR2_TRACE
	ktrace_free(xfs_dir2_trace_buf);
#endif
#ifdef XFS_ATTR_TRACE
	ktrace_free(xfs_attr_trace_buf);
#endif
#ifdef XFS_DIR_TRACE
	ktrace_free(xfs_dir_trace_buf);
#endif
#ifdef XFS_BMBT_TRACE
	ktrace_free(xfs_bmbt_trace_buf);
#endif
#ifdef XFS_BMAP_TRACE
	ktrace_free(xfs_bmap_trace_buf);
#endif
#ifdef XFS_ALLOC_TRACE
	ktrace_free(xfs_alloc_trace_buf);
#endif

	kmem_cache_destroy(xfs_bmap_free_item_zone);
	kmem_cache_destroy(xfs_btree_cur_zone);
	kmem_cache_destroy(xfs_inode_zone);
	kmem_cache_destroy(xfs_trans_zone);
	kmem_cache_destroy(xfs_da_state_zone);
	kmem_cache_destroy(xfs_dabuf_zone);
	kmem_cache_destroy(xfs_buf_item_zone);
	kmem_cache_destroy(xfs_efd_zone);
	kmem_cache_destroy(xfs_efi_zone);
	kmem_cache_destroy(xfs_ifork_zone);
	kmem_cache_destroy(xfs_ili_zone);
	kmem_cache_destroy(xfs_chashlist_zone);
}

/*
 * xfs_start_flags
 *
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock has _not_ yet been read in.
 */
STATIC int
xfs_start_flags(
	struct vfs		*vfs,
	struct xfs_mount_args	*ap,
	struct xfs_mount	*mp)
{
	/* Values are in BBs */
	if ((ap->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		/*
		 * At this point the superblock has not been read
		 * in, therefore we do not know the block size.
		 * Before the mount call ends we will convert
		 * these to FSBs.
		 */
		mp->m_dalign = ap->sunit;
		mp->m_swidth = ap->swidth;
	}

	if (ap->logbufs != -1 &&
	    ap->logbufs != 0 &&
	    (ap->logbufs < XLOG_MIN_ICLOGS ||
	     ap->logbufs > XLOG_MAX_ICLOGS)) {
		cmn_err(CE_WARN,
			"XFS: invalid logbufs value: %d [not %d-%d]",
			ap->logbufs, XLOG_MIN_ICLOGS, XLOG_MAX_ICLOGS);
		return XFS_ERROR(EINVAL);
	}
	mp->m_logbufs = ap->logbufs;
	if (ap->logbufsize != -1 &&
	    ap->logbufsize !=  0 &&
	    ap->logbufsize != 16 * 1024 &&
	    ap->logbufsize != 32 * 1024 &&
	    ap->logbufsize != 64 * 1024 &&
	    ap->logbufsize != 128 * 1024 &&
	    ap->logbufsize != 256 * 1024) {
		cmn_err(CE_WARN,
	"XFS: invalid logbufsize: %d [not 16k,32k,64k,128k or 256k]",
			ap->logbufsize);
		return XFS_ERROR(EINVAL);
	}
	mp->m_ihsize = ap->ihashsize;
	mp->m_logbsize = ap->logbufsize;
	mp->m_fsname_len = strlen(ap->fsname) + 1;
	mp->m_fsname = kmem_alloc(mp->m_fsname_len, KM_SLEEP);
	strcpy(mp->m_fsname, ap->fsname);
	if (ap->rtname[0]) {
		mp->m_rtname = kmem_alloc(strlen(ap->rtname) + 1, KM_SLEEP);
		strcpy(mp->m_rtname, ap->rtname);
	}
	if (ap->logname[0]) {
		mp->m_logname = kmem_alloc(strlen(ap->logname) + 1, KM_SLEEP);
		strcpy(mp->m_logname, ap->logname);
	}

	if (ap->flags & XFSMNT_WSYNC)
		mp->m_flags |= XFS_MOUNT_WSYNC;
#if XFS_BIG_INUMS
	if (ap->flags & XFSMNT_INO64) {
		mp->m_flags |= XFS_MOUNT_INO64;
		mp->m_inoadd = XFS_INO64_OFFSET;
	}
#endif
	if (ap->flags & XFSMNT_NOATIME)
		mp->m_flags |= XFS_MOUNT_NOATIME;
	if (ap->flags & XFSMNT_RETERR)
		mp->m_flags |= XFS_MOUNT_RETERR;
	if (ap->flags & XFSMNT_NOALIGN)
		mp->m_flags |= XFS_MOUNT_NOALIGN;
	if (ap->flags & XFSMNT_SWALLOC)
		mp->m_flags |= XFS_MOUNT_SWALLOC;
	if (ap->flags & XFSMNT_OSYNCISOSYNC)
		mp->m_flags |= XFS_MOUNT_OSYNCISOSYNC;
	if (ap->flags & XFSMNT_32BITINODES)
		mp->m_flags |= XFS_MOUNT_32BITINODES;

	if (ap->flags & XFSMNT_IOSIZE) {
		if (ap->iosizelog > XFS_MAX_IO_LOG ||
		    ap->iosizelog < XFS_MIN_IO_LOG) {
			cmn_err(CE_WARN,
		"XFS: invalid log iosize: %d [not %d-%d]",
				ap->iosizelog, XFS_MIN_IO_LOG,
				XFS_MAX_IO_LOG);
			return XFS_ERROR(EINVAL);
		}

		mp->m_flags |= XFS_MOUNT_DFLT_IOSIZE;
		mp->m_readio_log = mp->m_writeio_log = ap->iosizelog;
	}

	if (ap->flags & XFSMNT_IHASHSIZE)
		mp->m_flags |= XFS_MOUNT_IHASHSIZE;
	if (ap->flags & XFSMNT_IDELETE)
		mp->m_flags |= XFS_MOUNT_IDELETE;
	if (ap->flags & XFSMNT_DIRSYNC)
		mp->m_flags |= XFS_MOUNT_DIRSYNC;
	if (ap->flags & XFSMNT_COMPAT_ATTR)
		mp->m_flags |= XFS_MOUNT_COMPAT_ATTR;

	if (ap->flags2 & XFSMNT2_COMPAT_IOSIZE)
		mp->m_flags |= XFS_MOUNT_COMPAT_IOSIZE;

	/*
	 * no recovery flag requires a read-only mount
	 */
	if (ap->flags & XFSMNT_NORECOVERY) {
		if (!(vfs->vfs_flag & VFS_RDONLY)) {
			cmn_err(CE_WARN,
	"XFS: tried to mount a FS read-write without recovery!");
			return XFS_ERROR(EINVAL);
		}
		mp->m_flags |= XFS_MOUNT_NORECOVERY;
	}

	if (ap->flags & XFSMNT_NOUUID)
		mp->m_flags |= XFS_MOUNT_NOUUID;
	if (ap->flags & XFSMNT_BARRIER)
		mp->m_flags |= XFS_MOUNT_BARRIER;

	return 0;
}

/*
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock _has_ now been read in.
 */
STATIC int
xfs_finish_flags(
	struct vfs		*vfs,
	struct xfs_mount_args	*ap,
	struct xfs_mount	*mp)
{
	int			ronly = (vfs->vfs_flag & VFS_RDONLY);

	/* Fail a mount where the logbuf is smaller then the log stripe */
	if (XFS_SB_VERSION_HASLOGV2(&mp->m_sb)) {
		if ((ap->logbufsize == -1) &&
		    (mp->m_sb.sb_logsunit > XLOG_BIG_RECORD_BSIZE)) {
			mp->m_logbsize = mp->m_sb.sb_logsunit;
		} else if (ap->logbufsize < mp->m_sb.sb_logsunit) {
			cmn_err(CE_WARN,
	"XFS: logbuf size must be greater than or equal to log stripe size");
			return XFS_ERROR(EINVAL);
		}
	} else {
		/* Fail a mount if the logbuf is larger than 32K */
		if (ap->logbufsize > XLOG_BIG_RECORD_BSIZE) {
			cmn_err(CE_WARN,
	"XFS: logbuf size for version 1 logs must be 16K or 32K");
			return XFS_ERROR(EINVAL);
		}
	}

	/*
	 * prohibit r/w mounts of read-only filesystems
	 */
	if ((mp->m_sb.sb_flags & XFS_SBF_READONLY) && !ronly) {
		cmn_err(CE_WARN,
	"XFS: cannot mount a read-only filesystem as read-write");
		return XFS_ERROR(EROFS);
	}

	/*
	 * check for shared mount.
	 */
	if (ap->flags & XFSMNT_SHARED) {
		if (!XFS_SB_VERSION_HASSHARED(&mp->m_sb))
			return XFS_ERROR(EINVAL);

		/*
		 * For IRIX 6.5, shared mounts must have the shared
		 * version bit set, have the persistent readonly
		 * field set, must be version 0 and can only be mounted
		 * read-only.
		 */
		if (!ronly || !(mp->m_sb.sb_flags & XFS_SBF_READONLY) ||
		     (mp->m_sb.sb_shared_vn != 0))
			return XFS_ERROR(EINVAL);

		mp->m_flags |= XFS_MOUNT_SHARED;

		/*
		 * Shared XFS V0 can't deal with DMI.  Return EINVAL.
		 */
		if (mp->m_sb.sb_shared_vn == 0 && (ap->flags & XFSMNT_DMAPI))
			return XFS_ERROR(EINVAL);
	}

	if (XFS_SB_VERSION_HASATTR2(&mp->m_sb)) {
		mp->m_flags &= ~XFS_MOUNT_COMPAT_ATTR;
	}

	return 0;
}

/*
 * xfs_mount
 *
 * The file system configurations are:
 *	(1) device (partition) with data and internal log
 *	(2) logical volume with data and log subvolumes.
 *	(3) logical volume with data, log, and realtime subvolumes.
 *
 * We only have to handle opening the log and realtime volumes here if
 * they are present.  The data subvolume has already been opened by
 * get_sb_bdev() and is stored in vfsp->vfs_super->s_bdev.
 */
STATIC int
xfs_mount(
	struct bhv_desc		*bhvp,
	struct xfs_mount_args	*args,
	cred_t			*credp)
{
	struct vfs		*vfsp = bhvtovfs(bhvp);
	struct bhv_desc		*p;
	struct xfs_mount	*mp = XFS_BHVTOM(bhvp);
	struct block_device	*ddev, *logdev, *rtdev;
	int			flags = 0, error;

	ddev = vfsp->vfs_super->s_bdev;
	logdev = rtdev = NULL;

	/*
	 * Setup xfs_mount function vectors from available behaviors
	 */
	p = vfs_bhv_lookup(vfsp, VFS_POSITION_DM);
	mp->m_dm_ops = p ? *(xfs_dmops_t *) vfs_bhv_custom(p) : xfs_dmcore_stub;
	p = vfs_bhv_lookup(vfsp, VFS_POSITION_QM);
	mp->m_qm_ops = p ? *(xfs_qmops_t *) vfs_bhv_custom(p) : xfs_qmcore_stub;
	p = vfs_bhv_lookup(vfsp, VFS_POSITION_IO);
	mp->m_io_ops = p ? *(xfs_ioops_t *) vfs_bhv_custom(p) : xfs_iocore_xfs;

	/*
	 * Open real time and log devices - order is important.
	 */
	if (args->logname[0]) {
		error = xfs_blkdev_get(mp, args->logname, &logdev);
		if (error)
			return error;
	}
	if (args->rtname[0]) {
		error = xfs_blkdev_get(mp, args->rtname, &rtdev);
		if (error) {
			xfs_blkdev_put(logdev);
			return error;
		}

		if (rtdev == ddev || rtdev == logdev) {
			cmn_err(CE_WARN,
	"XFS: Cannot mount filesystem with identical rtdev and ddev/logdev.");
			xfs_blkdev_put(logdev);
			xfs_blkdev_put(rtdev);
			return EINVAL;
		}
	}

	/*
	 * Setup xfs_mount buffer target pointers
	 */
	error = ENOMEM;
	mp->m_ddev_targp = xfs_alloc_buftarg(ddev, 0);
	if (!mp->m_ddev_targp) {
		xfs_blkdev_put(logdev);
		xfs_blkdev_put(rtdev);
		return error;
	}
	if (rtdev) {
		mp->m_rtdev_targp = xfs_alloc_buftarg(rtdev, 1);
		if (!mp->m_rtdev_targp)
			goto error0;
	}
	mp->m_logdev_targp = (logdev && logdev != ddev) ?
				xfs_alloc_buftarg(logdev, 1) : mp->m_ddev_targp;
	if (!mp->m_logdev_targp)
		goto error0;

	/*
	 * Setup flags based on mount(2) options and then the superblock
	 */
	error = xfs_start_flags(vfsp, args, mp);
	if (error)
		goto error1;
	error = xfs_readsb(mp);
	if (error)
		goto error1;
	error = xfs_finish_flags(vfsp, args, mp);
	if (error)
		goto error2;

	/*
	 * Setup xfs_mount buffer target pointers based on superblock
	 */
	error = xfs_setsize_buftarg(mp->m_ddev_targp, mp->m_sb.sb_blocksize,
				    mp->m_sb.sb_sectsize);
	if (!error && logdev && logdev != ddev) {
		unsigned int	log_sector_size = BBSIZE;

		if (XFS_SB_VERSION_HASSECTOR(&mp->m_sb))
			log_sector_size = mp->m_sb.sb_logsectsize;
		error = xfs_setsize_buftarg(mp->m_logdev_targp,
					    mp->m_sb.sb_blocksize,
					    log_sector_size);
	}
	if (!error && rtdev)
		error = xfs_setsize_buftarg(mp->m_rtdev_targp,
					    mp->m_sb.sb_blocksize,
					    mp->m_sb.sb_sectsize);
	if (error)
		goto error2;

	error = XFS_IOINIT(vfsp, args, flags);
	if (error)
		goto error2;

	if ((args->flags & XFSMNT_BARRIER) &&
	    !(XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY))
		xfs_mountfs_check_barriers(mp);
	return 0;

error2:
	if (mp->m_sb_bp)
		xfs_freesb(mp);
error1:
	xfs_binval(mp->m_ddev_targp);
	if (logdev && logdev != ddev)
		xfs_binval(mp->m_logdev_targp);
	if (rtdev)
		xfs_binval(mp->m_rtdev_targp);
error0:
	xfs_unmountfs_close(mp, credp);
	return error;
}

STATIC int
xfs_unmount(
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp)
{
	struct vfs	*vfsp = bhvtovfs(bdp);
	xfs_mount_t	*mp = XFS_BHVTOM(bdp);
	xfs_inode_t	*rip;
	vnode_t		*rvp;
	int		unmount_event_wanted = 0;
	int		unmount_event_flags = 0;
	int		xfs_unmountfs_needed = 0;
	int		error;

	rip = mp->m_rootip;
	rvp = XFS_ITOV(rip);

	if (vfsp->vfs_flag & VFS_DMI) {
		error = XFS_SEND_PREUNMOUNT(mp, vfsp,
				rvp, DM_RIGHT_NULL, rvp, DM_RIGHT_NULL,
				NULL, NULL, 0, 0,
				(mp->m_dmevmask & (1<<DM_EVENT_PREUNMOUNT))?
					0:DM_FLAGS_UNWANTED);
			if (error)
				return XFS_ERROR(error);
		unmount_event_wanted = 1;
		unmount_event_flags = (mp->m_dmevmask & (1<<DM_EVENT_UNMOUNT))?
					0 : DM_FLAGS_UNWANTED;
	}

	/*
	 * First blow any referenced inode from this file system
	 * out of the reference cache, and delete the timer.
	 */
	xfs_refcache_purge_mp(mp);

	XFS_bflush(mp->m_ddev_targp);
	error = xfs_unmount_flush(mp, 0);
	if (error)
		goto out;

	ASSERT(vn_count(rvp) == 1);

	/*
	 * Drop the reference count
	 */
	VN_RELE(rvp);

	/*
	 * If we're forcing a shutdown, typically because of a media error,
	 * we want to make sure we invalidate dirty pages that belong to
	 * referenced vnodes as well.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = xfs_sync(&mp->m_bhv,
			 (SYNC_WAIT | SYNC_CLOSE), credp);
		ASSERT(error != EFSCORRUPTED);
	}
	xfs_unmountfs_needed = 1;

out:
	/*	Send DMAPI event, if required.
	 *	Then do xfs_unmountfs() if needed.
	 *	Then return error (or zero).
	 */
	if (unmount_event_wanted) {
		/* Note: mp structure must still exist for
		 * XFS_SEND_UNMOUNT() call.
		 */
		XFS_SEND_UNMOUNT(mp, vfsp, error == 0 ? rvp : NULL,
			DM_RIGHT_NULL, 0, error, unmount_event_flags);
	}
	if (xfs_unmountfs_needed) {
		/*
		 * Call common unmount function to flush to disk
		 * and free the super block buffer & mount structures.
		 */
		xfs_unmountfs(mp, credp);
	}

	return XFS_ERROR(error);
}

STATIC int
xfs_quiesce_fs(
	xfs_mount_t		*mp)
{
	int			count = 0, pincount;
		
	xfs_refcache_purge_mp(mp);
	xfs_flush_buftarg(mp->m_ddev_targp, 0);
	xfs_finish_reclaim_all(mp, 0);

	/* This loop must run at least twice.
	 * The first instance of the loop will flush
	 * most meta data but that will generate more
	 * meta data (typically directory updates).
	 * Which then must be flushed and logged before
	 * we can write the unmount record.
	 */ 
	do {
		xfs_syncsub(mp, SYNC_REMOUNT|SYNC_ATTR|SYNC_WAIT, 0, NULL);
		pincount = xfs_flush_buftarg(mp->m_ddev_targp, 1);
		if (!pincount) {
			delay(50);
			count++;
		}
	} while (count < 2);

	return 0;
}

STATIC int
xfs_mntupdate(
	bhv_desc_t			*bdp,
	int				*flags,
	struct xfs_mount_args		*args)
{
	struct vfs	*vfsp = bhvtovfs(bdp);
	xfs_mount_t	*mp = XFS_BHVTOM(bdp);
	int		error;

	if (args->flags & XFSMNT_NOATIME)
		mp->m_flags |= XFS_MOUNT_NOATIME;
	else
		mp->m_flags &= ~XFS_MOUNT_NOATIME;

	if ((vfsp->vfs_flag & VFS_RDONLY) &&
	    !(*flags & MS_RDONLY)) {
		vfsp->vfs_flag &= ~VFS_RDONLY;

		if (args->flags & XFSMNT_BARRIER)
			xfs_mountfs_check_barriers(mp);
	}

	if (!(vfsp->vfs_flag & VFS_RDONLY) &&
	    (*flags & MS_RDONLY)) {
		VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_BDFLUSH|SYNC_ATTR, NULL, error);

		xfs_quiesce_fs(mp);

		/* Ok now write out an unmount record */
		xfs_log_unmount_write(mp);
		xfs_unmountfs_writesb(mp);
		vfsp->vfs_flag |= VFS_RDONLY;
	}

	return 0;
}

/*
 * xfs_unmount_flush implements a set of flush operation on special
 * inodes, which are needed as a separate set of operations so that
 * they can be called as part of relocation process.
 */
int
xfs_unmount_flush(
	xfs_mount_t	*mp,		/* Mount structure we are getting
					   rid of. */
	int             relocation)	/* Called from vfs relocation. */
{
	xfs_inode_t	*rip = mp->m_rootip;
	xfs_inode_t	*rbmip;
	xfs_inode_t	*rsumip = NULL;
	vnode_t		*rvp = XFS_ITOV(rip);
	int		error;

	xfs_ilock(rip, XFS_ILOCK_EXCL);
	xfs_iflock(rip);

	/*
	 * Flush out the real time inodes.
	 */
	if ((rbmip = mp->m_rbmip) != NULL) {
		xfs_ilock(rbmip, XFS_ILOCK_EXCL);
		xfs_iflock(rbmip);
		error = xfs_iflush(rbmip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rbmip, XFS_ILOCK_EXCL);

		if (error == EFSCORRUPTED)
			goto fscorrupt_out;

		ASSERT(vn_count(XFS_ITOV(rbmip)) == 1);

		rsumip = mp->m_rsumip;
		xfs_ilock(rsumip, XFS_ILOCK_EXCL);
		xfs_iflock(rsumip);
		error = xfs_iflush(rsumip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rsumip, XFS_ILOCK_EXCL);

		if (error == EFSCORRUPTED)
			goto fscorrupt_out;

		ASSERT(vn_count(XFS_ITOV(rsumip)) == 1);
	}

	/*
	 * Synchronously flush root inode to disk
	 */
	error = xfs_iflush(rip, XFS_IFLUSH_SYNC);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;

	if (vn_count(rvp) != 1 && !relocation) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		return XFS_ERROR(EBUSY);
	}

	/*
	 * Release dquot that rootinode, rbmino and rsumino might be holding,
	 * flush and purge the quota inodes.
	 */
	error = XFS_QM_UNMOUNT(mp);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;

	if (rbmip) {
		VN_RELE(XFS_ITOV(rbmip));
		VN_RELE(XFS_ITOV(rsumip));
	}

	xfs_iunlock(rip, XFS_ILOCK_EXCL);
	return 0;

fscorrupt_out:
	xfs_ifunlock(rip);

fscorrupt_out2:
	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * xfs_root extracts the root vnode from a vfs.
 *
 * vfsp -- the vfs struct for the desired file system
 * vpp  -- address of the caller's vnode pointer which should be
 *         set to the desired fs root vnode
 */
STATIC int
xfs_root(
	bhv_desc_t	*bdp,
	vnode_t		**vpp)
{
	vnode_t		*vp;

	vp = XFS_ITOV((XFS_BHVTOM(bdp))->m_rootip);
	VN_HOLD(vp);
	*vpp = vp;
	return 0;
}

/*
 * xfs_statvfs
 *
 * Fill in the statvfs structure for the given file system.  We use
 * the superblock lock in the mount structure to ensure a consistent
 * snapshot of the counters returned.
 */
STATIC int
xfs_statvfs(
	bhv_desc_t	*bdp,
	xfs_statfs_t	*statp,
	vnode_t		*vp)
{
	__uint64_t	fakeinos;
	xfs_extlen_t	lsize;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	unsigned long	s;

	mp = XFS_BHVTOM(bdp);
	sbp = &(mp->m_sb);

	statp->f_type = XFS_SB_MAGIC;

	s = XFS_SB_LOCK(mp);
	statp->f_bsize = sbp->sb_blocksize;
	lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
	statp->f_blocks = sbp->sb_dblocks - lsize;
	statp->f_bfree = statp->f_bavail = sbp->sb_fdblocks;
	fakeinos = statp->f_bfree << sbp->sb_inopblog;
#if XFS_BIG_INUMS
	fakeinos += mp->m_inoadd;
#endif
	statp->f_files =
	    MIN(sbp->sb_icount + fakeinos, (__uint64_t)XFS_MAXINUMBER);
	if (mp->m_maxicount)
#if XFS_BIG_INUMS
		if (!mp->m_inoadd)
#endif
			statp->f_files = min_t(typeof(statp->f_files),
						statp->f_files,
						mp->m_maxicount);
	statp->f_ffree = statp->f_files - (sbp->sb_icount - sbp->sb_ifree);
	XFS_SB_UNLOCK(mp, s);

	xfs_statvfs_fsid(statp, mp);
	statp->f_namelen = MAXNAMELEN - 1;

	return 0;
}


/*
 * xfs_sync flushes any pending I/O to file system vfsp.
 *
 * This routine is called by vfs_sync() to make sure that things make it
 * out to disk eventually, on sync() system calls to flush out everything,
 * and when the file system is unmounted.  For the vfs_sync() case, all
 * we really need to do is sync out the log to make all of our meta-data
 * updates permanent (except for timestamps).  For calls from pflushd(),
 * dirty pages are kept moving by calling pdflush() on the inodes
 * containing them.  We also flush the inodes that we can lock without
 * sleeping and the superblock if we can lock it without sleeping from
 * vfs_sync() so that items at the tail of the log are always moving out.
 *
 * Flags:
 *      SYNC_BDFLUSH - We're being called from vfs_sync() so we don't want
 *		       to sleep if we can help it.  All we really need
 *		       to do is ensure that the log is synced at least
 *		       periodically.  We also push the inodes and
 *		       superblock if we can lock them without sleeping
 *			and they are not pinned.
 *      SYNC_ATTR    - We need to flush the inodes.  If SYNC_BDFLUSH is not
 *		       set, then we really want to lock each inode and flush
 *		       it.
 *      SYNC_WAIT    - All the flushes that take place in this call should
 *		       be synchronous.
 *      SYNC_DELWRI  - This tells us to push dirty pages associated with
 *		       inodes.  SYNC_WAIT and SYNC_BDFLUSH are used to
 *		       determine if they should be flushed sync, async, or
 *		       delwri.
 *      SYNC_CLOSE   - This flag is passed when the system is being
 *		       unmounted.  We should sync and invalidate everthing.
 *      SYNC_FSDATA  - This indicates that the caller would like to make
 *		       sure the superblock is safe on disk.  We can ensure
 *		       this by simply makeing sure the log gets flushed
 *		       if SYNC_BDFLUSH is set, and by actually writing it
 *		       out otherwise.
 *
 */
/*ARGSUSED*/
STATIC int
xfs_sync(
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp)
{
	xfs_mount_t	*mp = XFS_BHVTOM(bdp);

	if (unlikely(flags == SYNC_QUIESCE))
		return xfs_quiesce_fs(mp);
	else
		return xfs_syncsub(mp, flags, 0, NULL);
}

/*
 * xfs sync routine for internal use
 *
 * This routine supports all of the flags defined for the generic VFS_SYNC
 * interface as explained above under xfs_sync.  In the interests of not
 * changing interfaces within the 6.5 family, additional internallly-
 * required functions are specified within a separate xflags parameter,
 * only available by calling this routine.
 *
 */
int
xfs_sync_inodes(
	xfs_mount_t	*mp,
	int		flags,
	int             xflags,
	int             *bypassed)
{
	xfs_inode_t	*ip = NULL;
	xfs_inode_t	*ip_next;
	xfs_buf_t	*bp;
	vnode_t		*vp = NULL;
	int		error;
	int		last_error;
	uint64_t	fflag;
	uint		lock_flags;
	uint		base_lock_flags;
	boolean_t	mount_locked;
	boolean_t	vnode_refed;
	int		preempt;
	xfs_dinode_t	*dip;
	xfs_iptr_t	*ipointer;
#ifdef DEBUG
	boolean_t	ipointer_in = B_FALSE;

#define IPOINTER_SET	ipointer_in = B_TRUE
#define IPOINTER_CLR	ipointer_in = B_FALSE
#else
#define IPOINTER_SET
#define IPOINTER_CLR
#endif


/* Insert a marker record into the inode list after inode ip. The list
 * must be locked when this is called. After the call the list will no
 * longer be locked.
 */
#define IPOINTER_INSERT(ip, mp)	{ \
		ASSERT(ipointer_in == B_FALSE); \
		ipointer->ip_mnext = ip->i_mnext; \
		ipointer->ip_mprev = ip; \
		ip->i_mnext = (xfs_inode_t *)ipointer; \
		ipointer->ip_mnext->i_mprev = (xfs_inode_t *)ipointer; \
		preempt = 0; \
		XFS_MOUNT_IUNLOCK(mp); \
		mount_locked = B_FALSE; \
		IPOINTER_SET; \
	}

/* Remove the marker from the inode list. If the marker was the only item
 * in the list then there are no remaining inodes and we should zero out
 * the whole list. If we are the current head of the list then move the head
 * past us.
 */
#define IPOINTER_REMOVE(ip, mp)	{ \
		ASSERT(ipointer_in == B_TRUE); \
		if (ipointer->ip_mnext != (xfs_inode_t *)ipointer) { \
			ip = ipointer->ip_mnext; \
			ip->i_mprev = ipointer->ip_mprev; \
			ipointer->ip_mprev->i_mnext = ip; \
			if (mp->m_inodes == (xfs_inode_t *)ipointer) { \
				mp->m_inodes = ip; \
			} \
		} else { \
			ASSERT(mp->m_inodes == (xfs_inode_t *)ipointer); \
			mp->m_inodes = NULL; \
			ip = NULL; \
		} \
		IPOINTER_CLR; \
	}

#define XFS_PREEMPT_MASK	0x7f

	if (bypassed)
		*bypassed = 0;
	if (XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY)
		return 0;
	error = 0;
	last_error = 0;
	preempt = 0;

	/* Allocate a reference marker */
	ipointer = (xfs_iptr_t *)kmem_zalloc(sizeof(xfs_iptr_t), KM_SLEEP);

	fflag = XFS_B_ASYNC;		/* default is don't wait */
	if (flags & (SYNC_BDFLUSH | SYNC_DELWRI))
		fflag = XFS_B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */

	base_lock_flags = XFS_ILOCK_SHARED;
	if (flags & (SYNC_DELWRI | SYNC_CLOSE)) {
		/*
		 * We need the I/O lock if we're going to call any of
		 * the flush/inval routines.
		 */
		base_lock_flags |= XFS_IOLOCK_SHARED;
	}

	XFS_MOUNT_ILOCK(mp);

	ip = mp->m_inodes;

	mount_locked = B_TRUE;
	vnode_refed  = B_FALSE;

	IPOINTER_CLR;

	do {
		ASSERT(ipointer_in == B_FALSE);
		ASSERT(vnode_refed == B_FALSE);

		lock_flags = base_lock_flags;

		/*
		 * There were no inodes in the list, just break out
		 * of the loop.
		 */
		if (ip == NULL) {
			break;
		}

		/*
		 * We found another sync thread marker - skip it
		 */
		if (ip->i_mount == NULL) {
			ip = ip->i_mnext;
			continue;
		}

		vp = XFS_ITOV_NULL(ip);

		/*
		 * If the vnode is gone then this is being torn down,
		 * call reclaim if it is flushed, else let regular flush
		 * code deal with it later in the loop.
		 */

		if (vp == NULL) {
			/* Skip ones already in reclaim */
			if (ip->i_flags & XFS_IRECLAIM) {
				ip = ip->i_mnext;
				continue;
			}
			if (xfs_ilock_nowait(ip, XFS_ILOCK_EXCL) == 0) {
				ip = ip->i_mnext;
			} else if ((xfs_ipincount(ip) == 0) &&
				    xfs_iflock_nowait(ip)) {
				IPOINTER_INSERT(ip, mp);

				xfs_finish_reclaim(ip, 1,
						XFS_IFLUSH_DELWRI_ELSE_ASYNC);

				XFS_MOUNT_ILOCK(mp);
				mount_locked = B_TRUE;
				IPOINTER_REMOVE(ip, mp);
			} else {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				ip = ip->i_mnext;
			}
			continue;
		}

		if (VN_BAD(vp)) {
			ip = ip->i_mnext;
			continue;
		}

		if (XFS_FORCED_SHUTDOWN(mp) && !(flags & SYNC_CLOSE)) {
			XFS_MOUNT_IUNLOCK(mp);
			kmem_free(ipointer, sizeof(xfs_iptr_t));
			return 0;
		}

		/*
		 * If this is just vfs_sync() or pflushd() calling
		 * then we can skip inodes for which it looks like
		 * there is nothing to do.  Since we don't have the
		 * inode locked this is racey, but these are periodic
		 * calls so it doesn't matter.  For the others we want
		 * to know for sure, so we at least try to lock them.
		 */
		if (flags & SYNC_BDFLUSH) {
			if (((ip->i_itemp == NULL) ||
			     !(ip->i_itemp->ili_format.ilf_fields &
			       XFS_ILOG_ALL)) &&
			    (ip->i_update_core == 0)) {
				ip = ip->i_mnext;
				continue;
			}
		}

		/*
		 * Try to lock without sleeping.  We're out of order with
		 * the inode list lock here, so if we fail we need to drop
		 * the mount lock and try again.  If we're called from
		 * bdflush() here, then don't bother.
		 *
		 * The inode lock here actually coordinates with the
		 * almost spurious inode lock in xfs_ireclaim() to prevent
		 * the vnode we handle here without a reference from
		 * being freed while we reference it.  If we lock the inode
		 * while it's on the mount list here, then the spurious inode
		 * lock in xfs_ireclaim() after the inode is pulled from
		 * the mount list will sleep until we release it here.
		 * This keeps the vnode from being freed while we reference
		 * it.
		 */
		if (xfs_ilock_nowait(ip, lock_flags) == 0) {
			if ((flags & SYNC_BDFLUSH) || (vp == NULL)) {
				ip = ip->i_mnext;
				continue;
			}

			vp = vn_grab(vp);
			if (vp == NULL) {
				ip = ip->i_mnext;
				continue;
			}

			IPOINTER_INSERT(ip, mp);
			xfs_ilock(ip, lock_flags);

			ASSERT(vp == XFS_ITOV(ip));
			ASSERT(ip->i_mount == mp);

			vnode_refed = B_TRUE;
		}

		/* From here on in the loop we may have a marker record
		 * in the inode list.
		 */

		if ((flags & SYNC_CLOSE)  && (vp != NULL)) {
			/*
			 * This is the shutdown case.  We just need to
			 * flush and invalidate all the pages associated
			 * with the inode.  Drop the inode lock since
			 * we can't hold it across calls to the buffer
			 * cache.
			 *
			 * We don't set the VREMAPPING bit in the vnode
			 * here, because we don't hold the vnode lock
			 * exclusively.  It doesn't really matter, though,
			 * because we only come here when we're shutting
			 * down anyway.
			 */
			xfs_iunlock(ip, XFS_ILOCK_SHARED);

			if (XFS_FORCED_SHUTDOWN(mp)) {
				VOP_TOSS_PAGES(vp, 0, -1, FI_REMAPF);
			} else {
				VOP_FLUSHINVAL_PAGES(vp, 0, -1, FI_REMAPF);
			}

			xfs_ilock(ip, XFS_ILOCK_SHARED);

		} else if ((flags & SYNC_DELWRI) && (vp != NULL)) {
			if (VN_DIRTY(vp)) {
				/* We need to have dropped the lock here,
				 * so insert a marker if we have not already
				 * done so.
				 */
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
				}

				/*
				 * Drop the inode lock since we can't hold it
				 * across calls to the buffer cache.
				 */
				xfs_iunlock(ip, XFS_ILOCK_SHARED);
				VOP_FLUSH_PAGES(vp, (xfs_off_t)0, -1,
							fflag, FI_NONE, error);
				xfs_ilock(ip, XFS_ILOCK_SHARED);
			}

		}

		if (flags & SYNC_BDFLUSH) {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     ((ip->i_itemp != NULL) &&
			      (ip->i_itemp->ili_format.ilf_fields != 0)))) {

				/* Insert marker and drop lock if not already
				 * done.
				 */
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
				}

				/*
				 * We don't want the periodic flushing of the
				 * inodes by vfs_sync() to interfere with
				 * I/O to the file, especially read I/O
				 * where it is only the access time stamp
				 * that is being flushed out.  To prevent
				 * long periods where we have both inode
				 * locks held shared here while reading the
				 * inode's buffer in from disk, we drop the
				 * inode lock while reading in the inode
				 * buffer.  We have to release the buffer
				 * and reacquire the inode lock so that they
				 * are acquired in the proper order (inode
				 * locks first).  The buffer will go at the
				 * end of the lru chain, though, so we can
				 * expect it to still be there when we go
				 * for it again in xfs_iflush().
				 */
				if ((xfs_ipincount(ip) == 0) &&
				    xfs_iflock_nowait(ip)) {

					xfs_ifunlock(ip);
					xfs_iunlock(ip, XFS_ILOCK_SHARED);

					error = xfs_itobp(mp, NULL, ip,
							  &dip, &bp, 0);
					if (!error) {
						xfs_buf_relse(bp);
					} else {
						/* Bailing out, remove the
						 * marker and free it.
						 */
						XFS_MOUNT_ILOCK(mp);

						IPOINTER_REMOVE(ip, mp);

						XFS_MOUNT_IUNLOCK(mp);

						ASSERT(!(lock_flags &
							XFS_IOLOCK_SHARED));

						kmem_free(ipointer,
							sizeof(xfs_iptr_t));
						return (0);
					}

					/*
					 * Since we dropped the inode lock,
					 * the inode may have been reclaimed.
					 * Therefore, we reacquire the mount
					 * lock and check to see if we were the
					 * inode reclaimed. If this happened
					 * then the ipointer marker will no
					 * longer point back at us. In this
					 * case, move ip along to the inode
					 * after the marker, remove the marker
					 * and continue.
					 */
					XFS_MOUNT_ILOCK(mp);
					mount_locked = B_TRUE;

					if (ip != ipointer->ip_mprev) {
						IPOINTER_REMOVE(ip, mp);

						ASSERT(!vnode_refed);
						ASSERT(!(lock_flags &
							XFS_IOLOCK_SHARED));
						continue;
					}

					ASSERT(ip->i_mount == mp);

					if (xfs_ilock_nowait(ip,
						    XFS_ILOCK_SHARED) == 0) {
						ASSERT(ip->i_mount == mp);
						/*
						 * We failed to reacquire
						 * the inode lock without
						 * sleeping, so just skip
						 * the inode for now.  We
						 * clear the ILOCK bit from
						 * the lock_flags so that we
						 * won't try to drop a lock
						 * we don't hold below.
						 */
						lock_flags &= ~XFS_ILOCK_SHARED;
						IPOINTER_REMOVE(ip_next, mp);
					} else if ((xfs_ipincount(ip) == 0) &&
						   xfs_iflock_nowait(ip)) {
						ASSERT(ip->i_mount == mp);
						/*
						 * Since this is vfs_sync()
						 * calling we only flush the
						 * inode out if we can lock
						 * it without sleeping and
						 * it is not pinned.  Drop
						 * the mount lock here so
						 * that we don't hold it for
						 * too long. We already have
						 * a marker in the list here.
						 */
						XFS_MOUNT_IUNLOCK(mp);
						mount_locked = B_FALSE;
						error = xfs_iflush(ip,
							   XFS_IFLUSH_DELWRI);
					} else {
						ASSERT(ip->i_mount == mp);
						IPOINTER_REMOVE(ip_next, mp);
					}
				}

			}

		} else {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     ((ip->i_itemp != NULL) &&
			      (ip->i_itemp->ili_format.ilf_fields != 0)))) {
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
				}

				if (flags & SYNC_WAIT) {
					xfs_iflock(ip);
					error = xfs_iflush(ip,
							   XFS_IFLUSH_SYNC);
				} else {
					/*
					 * If we can't acquire the flush
					 * lock, then the inode is already
					 * being flushed so don't bother
					 * waiting.  If we can lock it then
					 * do a delwri flush so we can
					 * combine multiple inode flushes
					 * in each disk write.
					 */
					if (xfs_iflock_nowait(ip)) {
						error = xfs_iflush(ip,
							   XFS_IFLUSH_DELWRI);
					}
					else if (bypassed)
						(*bypassed)++;
				}
			}
		}

		if (lock_flags != 0) {
			xfs_iunlock(ip, lock_flags);
		}

		if (vnode_refed) {
			/*
			 * If we had to take a reference on the vnode
			 * above, then wait until after we've unlocked
			 * the inode to release the reference.  This is
			 * because we can be already holding the inode
			 * lock when VN_RELE() calls xfs_inactive().
			 *
			 * Make sure to drop the mount lock before calling
			 * VN_RELE() so that we don't trip over ourselves if
			 * we have to go for the mount lock again in the
			 * inactive code.
			 */
			if (mount_locked) {
				IPOINTER_INSERT(ip, mp);
			}

			VN_RELE(vp);

			vnode_refed = B_FALSE;
		}

		if (error) {
			last_error = error;
		}

		/*
		 * bail out if the filesystem is corrupted.
		 */
		if (error == EFSCORRUPTED)  {
			if (!mount_locked) {
				XFS_MOUNT_ILOCK(mp);
				IPOINTER_REMOVE(ip, mp);
			}
			XFS_MOUNT_IUNLOCK(mp);
			ASSERT(ipointer_in == B_FALSE);
			kmem_free(ipointer, sizeof(xfs_iptr_t));
			return XFS_ERROR(error);
		}

		/* Let other threads have a chance at the mount lock
		 * if we have looped many times without dropping the
		 * lock.
		 */
		if ((++preempt & XFS_PREEMPT_MASK) == 0) {
			if (mount_locked) {
				IPOINTER_INSERT(ip, mp);
			}
		}

		if (mount_locked == B_FALSE) {
			XFS_MOUNT_ILOCK(mp);
			mount_locked = B_TRUE;
			IPOINTER_REMOVE(ip, mp);
			continue;
		}

		ASSERT(ipointer_in == B_FALSE);
		ip = ip->i_mnext;

	} while (ip != mp->m_inodes);

	XFS_MOUNT_IUNLOCK(mp);

	ASSERT(ipointer_in == B_FALSE);

	kmem_free(ipointer, sizeof(xfs_iptr_t));
	return XFS_ERROR(last_error);
}

/*
 * xfs sync routine for internal use
 *
 * This routine supports all of the flags defined for the generic VFS_SYNC
 * interface as explained above under xfs_sync.  In the interests of not
 * changing interfaces within the 6.5 family, additional internallly-
 * required functions are specified within a separate xflags parameter,
 * only available by calling this routine.
 *
 */
int
xfs_syncsub(
	xfs_mount_t	*mp,
	int		flags,
	int             xflags,
	int             *bypassed)
{
	int		error = 0;
	int		last_error = 0;
	uint		log_flags = XFS_LOG_FORCE;
	xfs_buf_t	*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Sync out the log.  This ensures that the log is periodically
	 * flushed even if there is not enough activity to fill it up.
	 */
	if (flags & SYNC_WAIT)
		log_flags |= XFS_LOG_SYNC;

	xfs_log_force(mp, (xfs_lsn_t)0, log_flags);

	if (flags & (SYNC_ATTR|SYNC_DELWRI)) {
		if (flags & SYNC_BDFLUSH)
			xfs_finish_reclaim_all(mp, 1);
		else
			error = xfs_sync_inodes(mp, flags, xflags, bypassed);
	}

	/*
	 * Flushing out dirty data above probably generated more
	 * log activity, so if this isn't vfs_sync() then flush
	 * the log again.
	 */
	if (flags & SYNC_DELWRI) {
		xfs_log_force(mp, (xfs_lsn_t)0, log_flags);
	}

	if (flags & SYNC_FSDATA) {
		/*
		 * If this is vfs_sync() then only sync the superblock
		 * if we can lock it without sleeping and it is not pinned.
		 */
		if (flags & SYNC_BDFLUSH) {
			bp = xfs_getsb(mp, XFS_BUF_TRYLOCK);
			if (bp != NULL) {
				bip = XFS_BUF_FSPRIVATE(bp,xfs_buf_log_item_t*);
				if ((bip != NULL) &&
				    xfs_buf_item_dirty(bip)) {
					if (!(XFS_BUF_ISPINNED(bp))) {
						XFS_BUF_ASYNC(bp);
						error = xfs_bwrite(mp, bp);
					} else {
						xfs_buf_relse(bp);
					}
				} else {
					xfs_buf_relse(bp);
				}
			}
		} else {
			bp = xfs_getsb(mp, 0);
			/*
			 * If the buffer is pinned then push on the log so
			 * we won't get stuck waiting in the write for
			 * someone, maybe ourselves, to flush the log.
			 * Even though we just pushed the log above, we
			 * did not have the superblock buffer locked at
			 * that point so it can become pinned in between
			 * there and here.
			 */
			if (XFS_BUF_ISPINNED(bp))
				xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
			if (flags & SYNC_WAIT)
				XFS_BUF_UNASYNC(bp);
			else
				XFS_BUF_ASYNC(bp);
			error = xfs_bwrite(mp, bp);
		}
		if (error) {
			last_error = error;
		}
	}

	/*
	 * If this is the periodic sync, then kick some entries out of
	 * the reference cache.  This ensures that idle entries are
	 * eventually kicked out of the cache.
	 */
	if (flags & SYNC_REFCACHE) {
		if (flags & SYNC_WAIT)
			xfs_refcache_purge_mp(mp);
		else
			xfs_refcache_purge_some(mp);
	}

	/*
	 * Now check to see if the log needs a "dummy" transaction.
	 */

	if (!(flags & SYNC_REMOUNT) && xfs_log_need_covered(mp)) {
		xfs_trans_t *tp;
		xfs_inode_t *ip;

		/*
		 * Put a dummy transaction in the log to tell
		 * recovery that all others are OK.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DUMMY1);
		if ((error = xfs_trans_reserve(tp, 0,
				XFS_ICHANGE_LOG_RES(mp),
				0, 0, 0)))  {
			xfs_trans_cancel(tp, 0);
			return error;
		}

		ip = mp->m_rootip;
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		error = xfs_trans_commit(tp, 0, NULL);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_log_force(mp, (xfs_lsn_t)0, log_flags);
	}

	/*
	 * When shutting down, we need to insure that the AIL is pushed
	 * to disk or the filesystem can appear corrupt from the PROM.
	 */
	if ((flags & (SYNC_CLOSE|SYNC_WAIT)) == (SYNC_CLOSE|SYNC_WAIT)) {
		XFS_bflush(mp->m_ddev_targp);
		if (mp->m_rtdev_targp) {
			XFS_bflush(mp->m_rtdev_targp);
		}
	}

	return XFS_ERROR(last_error);
}

/*
 * xfs_vget - called by DMAPI and NFSD to get vnode from file handle
 */
STATIC int
xfs_vget(
	bhv_desc_t	*bdp,
	vnode_t		**vpp,
	fid_t		*fidp)
{
	xfs_mount_t	*mp = XFS_BHVTOM(bdp);
	xfs_fid_t	*xfid = (struct xfs_fid *)fidp;
	xfs_inode_t	*ip;
	int		error;
	xfs_ino_t	ino;
	unsigned int	igen;

	/*
	 * Invalid.  Since handles can be created in user space and passed in
	 * via gethandle(), this is not cause for a panic.
	 */
	if (xfid->xfs_fid_len != sizeof(*xfid) - sizeof(xfid->xfs_fid_len))
		return XFS_ERROR(EINVAL);

	ino  = xfid->xfs_fid_ino;
	igen = xfid->xfs_fid_gen;

	/*
	 * NFS can sometimes send requests for ino 0.  Fail them gracefully.
	 */
	if (ino == 0)
		return XFS_ERROR(ESTALE);

	error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_SHARED, &ip, 0);
	if (error) {
		*vpp = NULL;
		return error;
	}

	if (ip == NULL) {
		*vpp = NULL;
		return XFS_ERROR(EIO);
	}

	if (ip->i_d.di_mode == 0 || ip->i_d.di_gen != igen) {
		xfs_iput_new(ip, XFS_ILOCK_SHARED);
		*vpp = NULL;
		return XFS_ERROR(ENOENT);
	}

	*vpp = XFS_ITOV(ip);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return 0;
}


#define MNTOPT_LOGBUFS	"logbufs"	/* number of XFS log buffers */
#define MNTOPT_LOGBSIZE	"logbsize"	/* size of XFS log buffers */
#define MNTOPT_LOGDEV	"logdev"	/* log device */
#define MNTOPT_RTDEV	"rtdev"		/* realtime I/O device */
#define MNTOPT_BIOSIZE	"biosize"	/* log2 of preferred buffered io size */
#define MNTOPT_WSYNC	"wsync"		/* safe-mode nfs compatible mount */
#define MNTOPT_INO64	"ino64"		/* force inodes into 64-bit range */
#define MNTOPT_NOALIGN	"noalign"	/* turn off stripe alignment */
#define MNTOPT_SWALLOC	"swalloc"	/* turn on stripe width allocation */
#define MNTOPT_SUNIT	"sunit"		/* data volume stripe unit */
#define MNTOPT_SWIDTH	"swidth"	/* data volume stripe width */
#define MNTOPT_NOUUID	"nouuid"	/* ignore filesystem UUID */
#define MNTOPT_MTPT	"mtpt"		/* filesystem mount point */
#define MNTOPT_GRPID	"grpid"		/* group-ID from parent directory */
#define MNTOPT_NOGRPID	"nogrpid"	/* group-ID from current process */
#define MNTOPT_BSDGROUPS    "bsdgroups"    /* group-ID from parent directory */
#define MNTOPT_SYSVGROUPS   "sysvgroups"   /* group-ID from current process */
#define MNTOPT_ALLOCSIZE    "allocsize"    /* preferred allocation size */
#define MNTOPT_IHASHSIZE    "ihashsize"    /* size of inode hash table */
#define MNTOPT_NORECOVERY   "norecovery"   /* don't run XFS recovery */
#define MNTOPT_BARRIER	"barrier"	/* use writer barriers for log write and
					 * unwritten extent conversion */
#define MNTOPT_OSYNCISOSYNC "osyncisosync" /* o_sync is REALLY o_sync */
#define MNTOPT_64BITINODE   "inode64"	/* inodes can be allocated anywhere */
#define MNTOPT_IKEEP	"ikeep"		/* do not free empty inode clusters */
#define MNTOPT_NOIKEEP	"noikeep"	/* free empty inode clusters */
#define MNTOPT_LARGEIO	   "largeio"	/* report large I/O sizes in stat() */
#define MNTOPT_NOLARGEIO   "nolargeio"	/* do not report large I/O sizes
					 * in stat(). */
#define MNTOPT_ATTR2	"attr2"		/* do use attr2 attribute format */
#define MNTOPT_NOATTR2	"noattr2"	/* do not use attr2 attribute format */

STATIC unsigned long
suffix_strtoul(const char *cp, char **endp, unsigned int base)
{
	int	last, shift_left_factor = 0;
	char	*value = (char *)cp;

	last = strlen(value) - 1;
	if (value[last] == 'K' || value[last] == 'k') {
		shift_left_factor = 10;
		value[last] = '\0';
	}
	if (value[last] == 'M' || value[last] == 'm') {
		shift_left_factor = 20;
		value[last] = '\0';
	}
	if (value[last] == 'G' || value[last] == 'g') {
		shift_left_factor = 30;
		value[last] = '\0';
	}

	return simple_strtoul(cp, endp, base) << shift_left_factor;
}

STATIC int
xfs_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	char			*this_char, *value, *eov;
	int			dsunit, dswidth, vol_dsunit, vol_dswidth;
	int			iosize;

	args->flags2 |= XFSMNT2_COMPAT_IOSIZE;
	args->flags |= XFSMNT_COMPAT_ATTR;

#if 0	/* XXX: off by default, until some remaining issues ironed out */
	args->flags |= XFSMNT_IDELETE; /* default to on */
#endif

	if (!options)
		goto done;

	iosize = dsunit = dswidth = vol_dsunit = vol_dswidth = 0;

	while ((this_char = strsep(&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			args->logbufs = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGBSIZE)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			args->logbufsize = suffix_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGDEV)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			strncpy(args->logname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_MTPT)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			strncpy(args->mtpt, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_RTDEV)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			strncpy(args->rtname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_BIOSIZE)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			iosize = simple_strtoul(value, &eov, 10);
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = (uint8_t) iosize;
		} else if (!strcmp(this_char, MNTOPT_ALLOCSIZE)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			iosize = suffix_strtoul(value, &eov, 10);
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = ffs(iosize) - 1;
		} else if (!strcmp(this_char, MNTOPT_IHASHSIZE)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			args->flags |= XFSMNT_IHASHSIZE;
			args->ihashsize = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_GRPID) ||
			   !strcmp(this_char, MNTOPT_BSDGROUPS)) {
			vfsp->vfs_flag |= VFS_GRPID;
		} else if (!strcmp(this_char, MNTOPT_NOGRPID) ||
			   !strcmp(this_char, MNTOPT_SYSVGROUPS)) {
			vfsp->vfs_flag &= ~VFS_GRPID;
		} else if (!strcmp(this_char, MNTOPT_WSYNC)) {
			args->flags |= XFSMNT_WSYNC;
		} else if (!strcmp(this_char, MNTOPT_OSYNCISOSYNC)) {
			args->flags |= XFSMNT_OSYNCISOSYNC;
		} else if (!strcmp(this_char, MNTOPT_NORECOVERY)) {
			args->flags |= XFSMNT_NORECOVERY;
		} else if (!strcmp(this_char, MNTOPT_INO64)) {
			args->flags |= XFSMNT_INO64;
#if !XFS_BIG_INUMS
			printk("XFS: %s option not allowed on this system\n",
				this_char);
			return EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_NOALIGN)) {
			args->flags |= XFSMNT_NOALIGN;
		} else if (!strcmp(this_char, MNTOPT_SWALLOC)) {
			args->flags |= XFSMNT_SWALLOC;
		} else if (!strcmp(this_char, MNTOPT_SUNIT)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			dsunit = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_SWIDTH)) {
			if (!value || !*value) {
				printk("XFS: %s option requires an argument\n",
					this_char);
				return EINVAL;
			}
			dswidth = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_64BITINODE)) {
			args->flags &= ~XFSMNT_32BITINODES;
#if !XFS_BIG_INUMS
			printk("XFS: %s option not allowed on this system\n",
				this_char);
			return EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_NOUUID)) {
			args->flags |= XFSMNT_NOUUID;
		} else if (!strcmp(this_char, MNTOPT_BARRIER)) {
			args->flags |= XFSMNT_BARRIER;
		} else if (!strcmp(this_char, MNTOPT_IKEEP)) {
			args->flags &= ~XFSMNT_IDELETE;
		} else if (!strcmp(this_char, MNTOPT_NOIKEEP)) {
			args->flags |= XFSMNT_IDELETE;
		} else if (!strcmp(this_char, MNTOPT_LARGEIO)) {
			args->flags2 &= ~XFSMNT2_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_NOLARGEIO)) {
			args->flags2 |= XFSMNT2_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_ATTR2)) {
			args->flags &= ~XFSMNT_COMPAT_ATTR;
		} else if (!strcmp(this_char, MNTOPT_NOATTR2)) {
			args->flags |= XFSMNT_COMPAT_ATTR;
		} else if (!strcmp(this_char, "osyncisdsync")) {
			/* no-op, this is now the default */
printk("XFS: osyncisdsync is now the default, option is deprecated.\n");
		} else if (!strcmp(this_char, "irixsgid")) {
printk("XFS: irixsgid is now a sysctl(2) variable, option is deprecated.\n");
		} else {
			printk("XFS: unknown mount option [%s].\n", this_char);
			return EINVAL;
		}
	}

	if (args->flags & XFSMNT_NORECOVERY) {
		if ((vfsp->vfs_flag & VFS_RDONLY) == 0) {
			printk("XFS: no-recovery mounts must be read-only.\n");
			return EINVAL;
		}
	}

	if ((args->flags & XFSMNT_NOALIGN) && (dsunit || dswidth)) {
		printk(
	"XFS: sunit and swidth options incompatible with the noalign option\n");
		return EINVAL;
	}

	if ((dsunit && !dswidth) || (!dsunit && dswidth)) {
		printk("XFS: sunit and swidth must be specified together\n");
		return EINVAL;
	}

	if (dsunit && (dswidth % dsunit != 0)) {
		printk(
	"XFS: stripe width (%d) must be a multiple of the stripe unit (%d)\n",
			dswidth, dsunit);
		return EINVAL;
	}

	if ((args->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		if (dsunit) {
			args->sunit = dsunit;
			args->flags |= XFSMNT_RETERR;
		} else {
			args->sunit = vol_dsunit;
		}
		dswidth ? (args->swidth = dswidth) :
			  (args->swidth = vol_dswidth);
	} else {
		args->sunit = args->swidth = 0;
	}

done:
	if (args->flags & XFSMNT_32BITINODES)
		vfsp->vfs_flag |= VFS_32BITINODES;
	if (args->flags2)
		args->flags |= XFSMNT_FLAGS2;
	return 0;
}

STATIC int
xfs_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	static struct proc_xfs_info {
		int	flag;
		char	*str;
	} xfs_info[] = {
		/* the few simple ones we can get from the mount struct */
		{ XFS_MOUNT_WSYNC,		"," MNTOPT_WSYNC },
		{ XFS_MOUNT_INO64,		"," MNTOPT_INO64 },
		{ XFS_MOUNT_NOALIGN,		"," MNTOPT_NOALIGN },
		{ XFS_MOUNT_SWALLOC,		"," MNTOPT_SWALLOC },
		{ XFS_MOUNT_NOUUID,		"," MNTOPT_NOUUID },
		{ XFS_MOUNT_NORECOVERY,		"," MNTOPT_NORECOVERY },
		{ XFS_MOUNT_OSYNCISOSYNC,	"," MNTOPT_OSYNCISOSYNC },
		{ XFS_MOUNT_BARRIER,		"," MNTOPT_BARRIER },
		{ XFS_MOUNT_IDELETE,		"," MNTOPT_NOIKEEP },
		{ 0, NULL }
	};
	struct proc_xfs_info	*xfs_infop;
	struct xfs_mount	*mp = XFS_BHVTOM(bhv);
	struct vfs		*vfsp = XFS_MTOVFS(mp);

	for (xfs_infop = xfs_info; xfs_infop->flag; xfs_infop++) {
		if (mp->m_flags & xfs_infop->flag)
			seq_puts(m, xfs_infop->str);
	}

	if (mp->m_flags & XFS_MOUNT_IHASHSIZE)
		seq_printf(m, "," MNTOPT_IHASHSIZE "=%d", mp->m_ihsize);

	if (mp->m_flags & XFS_MOUNT_DFLT_IOSIZE)
		seq_printf(m, "," MNTOPT_ALLOCSIZE "=%dk",
				(int)(1 << mp->m_writeio_log) >> 10);

	if (mp->m_logbufs > 0)
		seq_printf(m, "," MNTOPT_LOGBUFS "=%d", mp->m_logbufs);

	if (mp->m_logbsize > 0)
		seq_printf(m, "," MNTOPT_LOGBSIZE "=%dk", mp->m_logbsize >> 10);

	if (mp->m_logname)
		seq_printf(m, "," MNTOPT_LOGDEV "=%s", mp->m_logname);

	if (mp->m_rtname)
		seq_printf(m, "," MNTOPT_RTDEV "=%s", mp->m_rtname);

	if (mp->m_dalign > 0)
		seq_printf(m, "," MNTOPT_SUNIT "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_dalign));

	if (mp->m_swidth > 0)
		seq_printf(m, "," MNTOPT_SWIDTH "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_swidth));

	if (!(mp->m_flags & XFS_MOUNT_COMPAT_ATTR))
		seq_printf(m, "," MNTOPT_ATTR2);

	if (!(mp->m_flags & XFS_MOUNT_COMPAT_IOSIZE))
		seq_printf(m, "," MNTOPT_LARGEIO);

	if (!(vfsp->vfs_flag & VFS_32BITINODES))
		seq_printf(m, "," MNTOPT_64BITINODE);

	if (vfsp->vfs_flag & VFS_GRPID)
		seq_printf(m, "," MNTOPT_GRPID);

	return 0;
}

STATIC void
xfs_freeze(
	bhv_desc_t	*bdp)
{
	xfs_mount_t	*mp = XFS_BHVTOM(bdp);

	while (atomic_read(&mp->m_active_trans) > 0)
		delay(100);

	/* Push the superblock and write an unmount record */
	xfs_log_unmount_write(mp);
	xfs_unmountfs_writesb(mp);
}


vfsops_t xfs_vfsops = {
	BHV_IDENTITY_INIT(VFS_BHV_XFS,VFS_POSITION_XFS),
	.vfs_parseargs		= xfs_parseargs,
	.vfs_showargs		= xfs_showargs,
	.vfs_mount		= xfs_mount,
	.vfs_unmount		= xfs_unmount,
	.vfs_mntupdate		= xfs_mntupdate,
	.vfs_root		= xfs_root,
	.vfs_statvfs		= xfs_statvfs,
	.vfs_sync		= xfs_sync,
	.vfs_vget		= xfs_vget,
	.vfs_dmapiops		= (vfs_dmapiops_t)fs_nosys,
	.vfs_quotactl		= (vfs_quotactl_t)fs_nosys,
	.vfs_init_vnode		= xfs_initialize_vnode,
	.vfs_force_shutdown	= xfs_do_force_shutdown,
	.vfs_freeze		= xfs_freeze,
};
