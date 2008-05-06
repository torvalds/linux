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
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc_btree.h"
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
#include "xfs_buf_item.h"
#include "xfs_log_priv.h"
#include "xfs_dir2_trace.h"
#include "xfs_extfree_item.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_clnt.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"
#include "xfs_fsops.h"
#include "xfs_vnodeops.h"
#include "xfs_vfsops.h"
#include "xfs_utils.h"


int __init
xfs_init(void)
{
#ifdef XFS_DABUF_DEBUG
	extern spinlock_t        xfs_dabuf_global_lock;
	spin_lock_init(&xfs_dabuf_global_lock);
#endif

	/*
	 * Initialize all of the zone allocators we use.
	 */
	xfs_log_ticket_zone = kmem_zone_init(sizeof(xlog_ticket_t),
						"xfs_log_ticket");
	xfs_bmap_free_item_zone = kmem_zone_init(sizeof(xfs_bmap_free_item_t),
						"xfs_bmap_free_item");
	xfs_btree_cur_zone = kmem_zone_init(sizeof(xfs_btree_cur_t),
						"xfs_btree_cur");
	xfs_da_state_zone = kmem_zone_init(sizeof(xfs_da_state_t),
						"xfs_da_state");
	xfs_dabuf_zone = kmem_zone_init(sizeof(xfs_dabuf_t), "xfs_dabuf");
	xfs_ifork_zone = kmem_zone_init(sizeof(xfs_ifork_t), "xfs_ifork");
	xfs_trans_zone = kmem_zone_init(sizeof(xfs_trans_t), "xfs_trans");
	xfs_acl_zone_init(xfs_acl_zone, "xfs_acl");
	xfs_mru_cache_init();
	xfs_filestream_init();

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
	xfs_efd_zone =
		kmem_zone_init((sizeof(xfs_efd_log_item_t) +
			       ((XFS_EFD_MAX_FAST_EXTENTS - 1) *
				 sizeof(xfs_extent_t))),
				      "xfs_efd_item");
	xfs_efi_zone =
		kmem_zone_init((sizeof(xfs_efi_log_item_t) +
			       ((XFS_EFI_MAX_FAST_EXTENTS - 1) *
				 sizeof(xfs_extent_t))),
				      "xfs_efi_item");

	/*
	 * These zones warrant special memory allocator hints
	 */
	xfs_inode_zone =
		kmem_zone_init_flags(sizeof(xfs_inode_t), "xfs_inode",
					KM_ZONE_HWALIGN | KM_ZONE_RECLAIM |
					KM_ZONE_SPREAD, NULL);
	xfs_ili_zone =
		kmem_zone_init_flags(sizeof(xfs_inode_log_item_t), "xfs_ili",
					KM_ZONE_SPREAD, NULL);

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

void __exit
xfs_cleanup(void)
{
	extern kmem_zone_t	*xfs_inode_zone;
	extern kmem_zone_t	*xfs_efd_zone;
	extern kmem_zone_t	*xfs_efi_zone;

	xfs_cleanup_procfs();
	xfs_sysctl_unregister();
	xfs_filestream_uninit();
	xfs_mru_cache_uninit();
	xfs_acl_zone_destroy(xfs_acl_zone);

#ifdef XFS_DIR2_TRACE
	ktrace_free(xfs_dir2_trace_buf);
#endif
#ifdef XFS_ATTR_TRACE
	ktrace_free(xfs_attr_trace_buf);
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

	kmem_zone_destroy(xfs_bmap_free_item_zone);
	kmem_zone_destroy(xfs_btree_cur_zone);
	kmem_zone_destroy(xfs_inode_zone);
	kmem_zone_destroy(xfs_trans_zone);
	kmem_zone_destroy(xfs_da_state_zone);
	kmem_zone_destroy(xfs_dabuf_zone);
	kmem_zone_destroy(xfs_buf_item_zone);
	kmem_zone_destroy(xfs_efd_zone);
	kmem_zone_destroy(xfs_efi_zone);
	kmem_zone_destroy(xfs_ifork_zone);
	kmem_zone_destroy(xfs_ili_zone);
	kmem_zone_destroy(xfs_log_ticket_zone);
}

/*
 * xfs_start_flags
 *
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock has _not_ yet been read in.
 */
STATIC int
xfs_start_flags(
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
	    (ap->logbufsize < XLOG_MIN_RECORD_BSIZE ||
	     ap->logbufsize > XLOG_MAX_RECORD_BSIZE ||
	     !is_power_of_2(ap->logbufsize))) {
		cmn_err(CE_WARN,
	"XFS: invalid logbufsize: %d [not 16k,32k,64k,128k or 256k]",
			ap->logbufsize);
		return XFS_ERROR(EINVAL);
	}
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

	if (ap->flags & XFSMNT_IKEEP)
		mp->m_flags |= XFS_MOUNT_IKEEP;
	if (ap->flags & XFSMNT_DIRSYNC)
		mp->m_flags |= XFS_MOUNT_DIRSYNC;
	if (ap->flags & XFSMNT_ATTR2)
		mp->m_flags |= XFS_MOUNT_ATTR2;

	if (ap->flags2 & XFSMNT2_COMPAT_IOSIZE)
		mp->m_flags |= XFS_MOUNT_COMPAT_IOSIZE;

	/*
	 * no recovery flag requires a read-only mount
	 */
	if (ap->flags & XFSMNT_NORECOVERY) {
		if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
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
	else
		mp->m_flags &= ~XFS_MOUNT_BARRIER;

	if (ap->flags2 & XFSMNT2_FILESTREAMS)
		mp->m_flags |= XFS_MOUNT_FILESTREAMS;

	if (ap->flags & XFSMNT_DMAPI)
		mp->m_flags |= XFS_MOUNT_DMAPI;
	return 0;
}

/*
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock _has_ now been read in.
 */
STATIC int
xfs_finish_flags(
	struct xfs_mount_args	*ap,
	struct xfs_mount	*mp)
{
	int			ronly = (mp->m_flags & XFS_MOUNT_RDONLY);

	/* Fail a mount where the logbuf is smaller then the log stripe */
	if (xfs_sb_version_haslogv2(&mp->m_sb)) {
		if ((ap->logbufsize <= 0) &&
		    (mp->m_sb.sb_logsunit > XLOG_BIG_RECORD_BSIZE)) {
			mp->m_logbsize = mp->m_sb.sb_logsunit;
		} else if (ap->logbufsize > 0 &&
			   ap->logbufsize < mp->m_sb.sb_logsunit) {
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

	if (xfs_sb_version_hasattr2(&mp->m_sb))
		mp->m_flags |= XFS_MOUNT_ATTR2;

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
		if (!xfs_sb_version_hasshared(&mp->m_sb))
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

	if (ap->flags & XFSMNT_UQUOTA) {
		mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE);
		if (ap->flags & XFSMNT_UQUOTAENF)
			mp->m_qflags |= XFS_UQUOTA_ENFD;
	}

	if (ap->flags & XFSMNT_GQUOTA) {
		mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE);
		if (ap->flags & XFSMNT_GQUOTAENF)
			mp->m_qflags |= XFS_OQUOTA_ENFD;
	} else if (ap->flags & XFSMNT_PQUOTA) {
		mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE);
		if (ap->flags & XFSMNT_PQUOTAENF)
			mp->m_qflags |= XFS_OQUOTA_ENFD;
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
int
xfs_mount(
	struct xfs_mount	*mp,
	struct xfs_mount_args	*args,
	cred_t			*credp)
{
	struct block_device	*ddev, *logdev, *rtdev;
	int			flags = 0, error;

	ddev = mp->m_super->s_bdev;
	logdev = rtdev = NULL;

	error = xfs_dmops_get(mp, args);
	if (error)
		return error;
	error = xfs_qmops_get(mp, args);
	if (error)
		return error;

	if (args->flags & XFSMNT_QUIET)
		flags |= XFS_MFSI_QUIET;

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
		if (!mp->m_rtdev_targp) {
			xfs_blkdev_put(logdev);
			xfs_blkdev_put(rtdev);
			goto error0;
		}
	}
	mp->m_logdev_targp = (logdev && logdev != ddev) ?
				xfs_alloc_buftarg(logdev, 1) : mp->m_ddev_targp;
	if (!mp->m_logdev_targp) {
		xfs_blkdev_put(logdev);
		xfs_blkdev_put(rtdev);
		goto error0;
	}

	/*
	 * Setup flags based on mount(2) options and then the superblock
	 */
	error = xfs_start_flags(args, mp);
	if (error)
		goto error1;
	error = xfs_readsb(mp, flags);
	if (error)
		goto error1;
	error = xfs_finish_flags(args, mp);
	if (error)
		goto error2;

	/*
	 * Setup xfs_mount buffer target pointers based on superblock
	 */
	error = xfs_setsize_buftarg(mp->m_ddev_targp, mp->m_sb.sb_blocksize,
				    mp->m_sb.sb_sectsize);
	if (!error && logdev && logdev != ddev) {
		unsigned int	log_sector_size = BBSIZE;

		if (xfs_sb_version_hassector(&mp->m_sb))
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

	if (mp->m_flags & XFS_MOUNT_BARRIER)
		xfs_mountfs_check_barriers(mp);

	if ((error = xfs_filestream_mount(mp)))
		goto error2;

	error = xfs_mountfs(mp, flags);
	if (error)
		goto error2;

	XFS_SEND_MOUNT(mp, DM_RIGHT_NULL, args->mtpt, args->fsname);

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
	xfs_qmops_put(mp);
	xfs_dmops_put(mp);
	return error;
}

int
xfs_unmount(
	xfs_mount_t	*mp,
	int		flags,
	cred_t		*credp)
{
	xfs_inode_t	*rip;
	bhv_vnode_t	*rvp;
	int		unmount_event_wanted = 0;
	int		unmount_event_flags = 0;
	int		xfs_unmountfs_needed = 0;
	int		error;

	rip = mp->m_rootip;
	rvp = XFS_ITOV(rip);

#ifdef HAVE_DMAPI
	if (mp->m_flags & XFS_MOUNT_DMAPI) {
		error = XFS_SEND_PREUNMOUNT(mp,
				rip, DM_RIGHT_NULL, rip, DM_RIGHT_NULL,
				NULL, NULL, 0, 0,
				(mp->m_dmevmask & (1<<DM_EVENT_PREUNMOUNT))?
					0:DM_FLAGS_UNWANTED);
			if (error)
				return XFS_ERROR(error);
		unmount_event_wanted = 1;
		unmount_event_flags = (mp->m_dmevmask & (1<<DM_EVENT_UNMOUNT))?
					0 : DM_FLAGS_UNWANTED;
	}
#endif

	/*
	 * Blow away any referenced inode in the filestreams cache.
	 * This can and will cause log traffic as inodes go inactive
	 * here.
	 */
	xfs_filestream_unmount(mp);

	XFS_bflush(mp->m_ddev_targp);
	error = xfs_unmount_flush(mp, 0);
	if (error)
		goto out;

	ASSERT(vn_count(rvp) == 1);

	/*
	 * Drop the reference count
	 */
	IRELE(rip);

	/*
	 * If we're forcing a shutdown, typically because of a media error,
	 * we want to make sure we invalidate dirty pages that belong to
	 * referenced vnodes as well.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = xfs_sync(mp, SYNC_WAIT | SYNC_CLOSE);
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
		XFS_SEND_UNMOUNT(mp, error == 0 ? rip : NULL,
			DM_RIGHT_NULL, 0, error, unmount_event_flags);
	}
	if (xfs_unmountfs_needed) {
		/*
		 * Call common unmount function to flush to disk
		 * and free the super block buffer & mount structures.
		 */
		xfs_unmountfs(mp, credp);
		xfs_qmops_put(mp);
		xfs_dmops_put(mp);
		kmem_free(mp, sizeof(xfs_mount_t));
	}

	return XFS_ERROR(error);
}

STATIC void
xfs_quiesce_fs(
	xfs_mount_t		*mp)
{
	int			count = 0, pincount;

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
		xfs_syncsub(mp, SYNC_INODE_QUIESCE, NULL);
		pincount = xfs_flush_buftarg(mp->m_ddev_targp, 1);
		if (!pincount) {
			delay(50);
			count++;
		}
	} while (count < 2);
}

/*
 * Second stage of a quiesce. The data is already synced, now we have to take
 * care of the metadata. New transactions are already blocked, so we need to
 * wait for any remaining transactions to drain out before proceding.
 */
void
xfs_attr_quiesce(
	xfs_mount_t	*mp)
{
	int	error = 0;

	/* wait for all modifications to complete */
	while (atomic_read(&mp->m_active_trans) > 0)
		delay(100);

	/* flush inodes and push all remaining buffers out to disk */
	xfs_quiesce_fs(mp);

	ASSERT_ALWAYS(atomic_read(&mp->m_active_trans) == 0);

	/* Push the superblock and write an unmount record */
	error = xfs_log_sbcount(mp, 1);
	if (error)
		xfs_fs_cmn_err(CE_WARN, mp,
				"xfs_attr_quiesce: failed to log sb changes. "
				"Frozen image may not be consistent.");
	xfs_log_unmount_write(mp);
	xfs_unmountfs_writesb(mp);
}

int
xfs_mntupdate(
	struct xfs_mount		*mp,
	int				*flags,
	struct xfs_mount_args		*args)
{
	if (!(*flags & MS_RDONLY)) {			/* rw/ro -> rw */
		if (mp->m_flags & XFS_MOUNT_RDONLY)
			mp->m_flags &= ~XFS_MOUNT_RDONLY;
		if (args->flags & XFSMNT_BARRIER) {
			mp->m_flags |= XFS_MOUNT_BARRIER;
			xfs_mountfs_check_barriers(mp);
		} else {
			mp->m_flags &= ~XFS_MOUNT_BARRIER;
		}
	} else if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {	/* rw -> ro */
		xfs_filestream_flush(mp);
		xfs_sync(mp, SYNC_DATA_QUIESCE);
		xfs_attr_quiesce(mp);
		mp->m_flags |= XFS_MOUNT_RDONLY;
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
	bhv_vnode_t	*rvp = XFS_ITOV(rip);
	int		error;

	xfs_ilock(rip, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
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
		IRELE(rbmip);
		IRELE(rsumip);
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
 *		       unmounted.  We should sync and invalidate everything.
 *      SYNC_FSDATA  - This indicates that the caller would like to make
 *		       sure the superblock is safe on disk.  We can ensure
 *		       this by simply making sure the log gets flushed
 *		       if SYNC_BDFLUSH is set, and by actually writing it
 *		       out otherwise.
 *	SYNC_IOWAIT  - The caller wants us to wait for all data I/O to complete
 *		       before we return (including direct I/O). Forms the drain
 *		       side of the write barrier needed to safely quiesce the
 *		       filesystem.
 *
 */
int
xfs_sync(
	xfs_mount_t	*mp,
	int		flags)
{
	int		error;

	/*
	 * Get the Quota Manager to flush the dquots.
	 *
	 * If XFS quota support is not enabled or this filesystem
	 * instance does not use quotas XFS_QM_DQSYNC will always
	 * return zero.
	 */
	error = XFS_QM_DQSYNC(mp, flags);
	if (error) {
		/*
		 * If we got an IO error, we will be shutting down.
		 * So, there's nothing more for us to do here.
		 */
		ASSERT(error != EIO || XFS_FORCED_SHUTDOWN(mp));
		if (XFS_FORCED_SHUTDOWN(mp))
			return XFS_ERROR(error);
	}

	if (flags & SYNC_IOWAIT)
		xfs_filestream_flush(mp);

	return xfs_syncsub(mp, flags, NULL);
}

/*
 * xfs sync routine for internal use
 *
 * This routine supports all of the flags defined for the generic vfs_sync
 * interface as explained above under xfs_sync.
 *
 */
int
xfs_sync_inodes(
	xfs_mount_t	*mp,
	int		flags,
	int             *bypassed)
{
	xfs_inode_t	*ip = NULL;
	bhv_vnode_t	*vp = NULL;
	int		error;
	int		last_error;
	uint64_t	fflag;
	uint		lock_flags;
	uint		base_lock_flags;
	boolean_t	mount_locked;
	boolean_t	vnode_refed;
	int		preempt;
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

	ASSERT(!(flags & SYNC_BDFLUSH));

	if (bypassed)
		*bypassed = 0;
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;
	error = 0;
	last_error = 0;
	preempt = 0;

	/* Allocate a reference marker */
	ipointer = (xfs_iptr_t *)kmem_zalloc(sizeof(xfs_iptr_t), KM_SLEEP);

	fflag = XFS_B_ASYNC;		/* default is don't wait */
	if (flags & SYNC_DELWRI)
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
			if (vp == NULL) {
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

		/*
		 * If we have to flush data or wait for I/O completion
		 * we need to drop the ilock that we currently hold.
		 * If we need to drop the lock, insert a marker if we
		 * have not already done so.
		 */
		if ((flags & (SYNC_CLOSE|SYNC_IOWAIT)) ||
		    ((flags & SYNC_DELWRI) && VN_DIRTY(vp))) {
			if (mount_locked) {
				IPOINTER_INSERT(ip, mp);
			}
			xfs_iunlock(ip, XFS_ILOCK_SHARED);

			if (flags & SYNC_CLOSE) {
				/* Shutdown case. Flush and invalidate. */
				if (XFS_FORCED_SHUTDOWN(mp))
					xfs_tosspages(ip, 0, -1,
							     FI_REMAPF);
				else
					error = xfs_flushinval_pages(ip,
							0, -1, FI_REMAPF);
			} else if ((flags & SYNC_DELWRI) && VN_DIRTY(vp)) {
				error = xfs_flush_pages(ip, 0,
							-1, fflag, FI_NONE);
			}

			/*
			 * When freezing, we need to wait ensure all I/O (including direct
			 * I/O) is complete to ensure no further data modification can take
			 * place after this point
			 */
			if (flags & SYNC_IOWAIT)
				vn_iowait(ip);

			xfs_ilock(ip, XFS_ILOCK_SHARED);
		}

		if ((flags & SYNC_ATTR) &&
		    (ip->i_update_core ||
		     (ip->i_itemp && ip->i_itemp->ili_format.ilf_fields))) {
			if (mount_locked)
				IPOINTER_INSERT(ip, mp);

			if (flags & SYNC_WAIT) {
				xfs_iflock(ip);
				error = xfs_iflush(ip, XFS_IFLUSH_SYNC);

			/*
			 * If we can't acquire the flush lock, then the inode
			 * is already being flushed so don't bother waiting.
			 *
			 * If we can lock it then do a delwri flush so we can
			 * combine multiple inode flushes in each disk write.
			 */
			} else if (xfs_iflock_nowait(ip)) {
				error = xfs_iflush(ip, XFS_IFLUSH_DELWRI);
			} else if (bypassed) {
				(*bypassed)++;
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
			 * lock when IRELE() calls xfs_inactive().
			 *
			 * Make sure to drop the mount lock before calling
			 * IRELE() so that we don't trip over ourselves if
			 * we have to go for the mount lock again in the
			 * inactive code.
			 */
			if (mount_locked) {
				IPOINTER_INSERT(ip, mp);
			}

			IRELE(ip);

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
 * This routine supports all of the flags defined for the generic vfs_sync
 * interface as explained above under xfs_sync.
 *
 */
int
xfs_syncsub(
	xfs_mount_t	*mp,
	int		flags,
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
			error = xfs_sync_inodes(mp, flags, bypassed);
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
		error = xfs_trans_commit(tp, 0);
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
