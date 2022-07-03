// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"
#include "xfs_fsops.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_dir2.h"
#include "xfs_extfree_item.h"
#include "xfs_mru_cache.h"
#include "xfs_inode_item.h"
#include "xfs_icache.h"
#include "xfs_trace.h"
#include "xfs_icreate_item.h"
#include "xfs_filestream.h"
#include "xfs_quota.h"
#include "xfs_sysfs.h"
#include "xfs_ondisk.h"
#include "xfs_rmap_item.h"
#include "xfs_refcount_item.h"
#include "xfs_bmap_item.h"
#include "xfs_reflink.h"

#include <linux/magic.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

static const struct super_operations xfs_super_operations;

static struct kset *xfs_kset;		/* top-level xfs sysfs dir */
#ifdef DEBUG
static struct xfs_kobj xfs_dbg_kobj;	/* global debug sysfs attrs */
#endif

enum xfs_dax_mode {
	XFS_DAX_INODE = 0,
	XFS_DAX_ALWAYS = 1,
	XFS_DAX_NEVER = 2,
};

static void
xfs_mount_set_dax_mode(
	struct xfs_mount	*mp,
	enum xfs_dax_mode	mode)
{
	switch (mode) {
	case XFS_DAX_INODE:
		mp->m_flags &= ~(XFS_MOUNT_DAX_ALWAYS | XFS_MOUNT_DAX_NEVER);
		break;
	case XFS_DAX_ALWAYS:
		mp->m_flags |= XFS_MOUNT_DAX_ALWAYS;
		mp->m_flags &= ~XFS_MOUNT_DAX_NEVER;
		break;
	case XFS_DAX_NEVER:
		mp->m_flags |= XFS_MOUNT_DAX_NEVER;
		mp->m_flags &= ~XFS_MOUNT_DAX_ALWAYS;
		break;
	}
}

static const struct constant_table dax_param_enums[] = {
	{"inode",	XFS_DAX_INODE },
	{"always",	XFS_DAX_ALWAYS },
	{"never",	XFS_DAX_NEVER },
	{}
};

/*
 * Table driven mount option parser.
 */
enum {
	Opt_logbufs, Opt_logbsize, Opt_logdev, Opt_rtdev,
	Opt_wsync, Opt_noalign, Opt_swalloc, Opt_sunit, Opt_swidth, Opt_nouuid,
	Opt_grpid, Opt_nogrpid, Opt_bsdgroups, Opt_sysvgroups,
	Opt_allocsize, Opt_norecovery, Opt_inode64, Opt_inode32, Opt_ikeep,
	Opt_noikeep, Opt_largeio, Opt_nolargeio, Opt_attr2, Opt_noattr2,
	Opt_filestreams, Opt_quota, Opt_noquota, Opt_usrquota, Opt_grpquota,
	Opt_prjquota, Opt_uquota, Opt_gquota, Opt_pquota,
	Opt_uqnoenforce, Opt_gqnoenforce, Opt_pqnoenforce, Opt_qnoenforce,
	Opt_discard, Opt_nodiscard, Opt_dax, Opt_dax_enum,
};

static const struct fs_parameter_spec xfs_fs_parameters[] = {
	fsparam_u32("logbufs",		Opt_logbufs),
	fsparam_string("logbsize",	Opt_logbsize),
	fsparam_string("logdev",	Opt_logdev),
	fsparam_string("rtdev",		Opt_rtdev),
	fsparam_flag("wsync",		Opt_wsync),
	fsparam_flag("noalign",		Opt_noalign),
	fsparam_flag("swalloc",		Opt_swalloc),
	fsparam_u32("sunit",		Opt_sunit),
	fsparam_u32("swidth",		Opt_swidth),
	fsparam_flag("nouuid",		Opt_nouuid),
	fsparam_flag("grpid",		Opt_grpid),
	fsparam_flag("nogrpid",		Opt_nogrpid),
	fsparam_flag("bsdgroups",	Opt_bsdgroups),
	fsparam_flag("sysvgroups",	Opt_sysvgroups),
	fsparam_string("allocsize",	Opt_allocsize),
	fsparam_flag("norecovery",	Opt_norecovery),
	fsparam_flag("inode64",		Opt_inode64),
	fsparam_flag("inode32",		Opt_inode32),
	fsparam_flag("ikeep",		Opt_ikeep),
	fsparam_flag("noikeep",		Opt_noikeep),
	fsparam_flag("largeio",		Opt_largeio),
	fsparam_flag("nolargeio",	Opt_nolargeio),
	fsparam_flag("attr2",		Opt_attr2),
	fsparam_flag("noattr2",		Opt_noattr2),
	fsparam_flag("filestreams",	Opt_filestreams),
	fsparam_flag("quota",		Opt_quota),
	fsparam_flag("noquota",		Opt_noquota),
	fsparam_flag("usrquota",	Opt_usrquota),
	fsparam_flag("grpquota",	Opt_grpquota),
	fsparam_flag("prjquota",	Opt_prjquota),
	fsparam_flag("uquota",		Opt_uquota),
	fsparam_flag("gquota",		Opt_gquota),
	fsparam_flag("pquota",		Opt_pquota),
	fsparam_flag("uqnoenforce",	Opt_uqnoenforce),
	fsparam_flag("gqnoenforce",	Opt_gqnoenforce),
	fsparam_flag("pqnoenforce",	Opt_pqnoenforce),
	fsparam_flag("qnoenforce",	Opt_qnoenforce),
	fsparam_flag("discard",		Opt_discard),
	fsparam_flag("nodiscard",	Opt_nodiscard),
	fsparam_flag("dax",		Opt_dax),
	fsparam_enum("dax",		Opt_dax_enum, dax_param_enums),
	{}
};

struct proc_xfs_info {
	uint64_t	flag;
	char		*str;
};

static int
xfs_fs_show_options(
	struct seq_file		*m,
	struct dentry		*root)
{
	static struct proc_xfs_info xfs_info_set[] = {
		/* the few simple ones we can get from the mount struct */
		{ XFS_MOUNT_IKEEP,		",ikeep" },
		{ XFS_MOUNT_WSYNC,		",wsync" },
		{ XFS_MOUNT_NOALIGN,		",noalign" },
		{ XFS_MOUNT_SWALLOC,		",swalloc" },
		{ XFS_MOUNT_NOUUID,		",nouuid" },
		{ XFS_MOUNT_NORECOVERY,		",norecovery" },
		{ XFS_MOUNT_ATTR2,		",attr2" },
		{ XFS_MOUNT_FILESTREAMS,	",filestreams" },
		{ XFS_MOUNT_GRPID,		",grpid" },
		{ XFS_MOUNT_DISCARD,		",discard" },
		{ XFS_MOUNT_LARGEIO,		",largeio" },
		{ XFS_MOUNT_DAX_ALWAYS,		",dax=always" },
		{ XFS_MOUNT_DAX_NEVER,		",dax=never" },
		{ 0, NULL }
	};
	struct xfs_mount	*mp = XFS_M(root->d_sb);
	struct proc_xfs_info	*xfs_infop;

	for (xfs_infop = xfs_info_set; xfs_infop->flag; xfs_infop++) {
		if (mp->m_flags & xfs_infop->flag)
			seq_puts(m, xfs_infop->str);
	}

	seq_printf(m, ",inode%d",
		(mp->m_flags & XFS_MOUNT_SMALL_INUMS) ? 32 : 64);

	if (mp->m_flags & XFS_MOUNT_ALLOCSIZE)
		seq_printf(m, ",allocsize=%dk",
			   (1 << mp->m_allocsize_log) >> 10);

	if (mp->m_logbufs > 0)
		seq_printf(m, ",logbufs=%d", mp->m_logbufs);
	if (mp->m_logbsize > 0)
		seq_printf(m, ",logbsize=%dk", mp->m_logbsize >> 10);

	if (mp->m_logname)
		seq_show_option(m, "logdev", mp->m_logname);
	if (mp->m_rtname)
		seq_show_option(m, "rtdev", mp->m_rtname);

	if (mp->m_dalign > 0)
		seq_printf(m, ",sunit=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_dalign));
	if (mp->m_swidth > 0)
		seq_printf(m, ",swidth=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_swidth));

	if (mp->m_qflags & XFS_UQUOTA_ACCT) {
		if (mp->m_qflags & XFS_UQUOTA_ENFD)
			seq_puts(m, ",usrquota");
		else
			seq_puts(m, ",uqnoenforce");
	}

	if (mp->m_qflags & XFS_PQUOTA_ACCT) {
		if (mp->m_qflags & XFS_PQUOTA_ENFD)
			seq_puts(m, ",prjquota");
		else
			seq_puts(m, ",pqnoenforce");
	}
	if (mp->m_qflags & XFS_GQUOTA_ACCT) {
		if (mp->m_qflags & XFS_GQUOTA_ENFD)
			seq_puts(m, ",grpquota");
		else
			seq_puts(m, ",gqnoenforce");
	}

	if (!(mp->m_qflags & XFS_ALL_QUOTA_ACCT))
		seq_puts(m, ",noquota");

	return 0;
}

/*
 * Set parameters for inode allocation heuristics, taking into account
 * filesystem size and inode32/inode64 mount options; i.e. specifically
 * whether or not XFS_MOUNT_SMALL_INUMS is set.
 *
 * Inode allocation patterns are altered only if inode32 is requested
 * (XFS_MOUNT_SMALL_INUMS), and the filesystem is sufficiently large.
 * If altered, XFS_MOUNT_32BITINODES is set as well.
 *
 * An agcount independent of that in the mount structure is provided
 * because in the growfs case, mp->m_sb.sb_agcount is not yet updated
 * to the potentially higher ag count.
 *
 * Returns the maximum AG index which may contain inodes.
 */
xfs_agnumber_t
xfs_set_inode_alloc(
	struct xfs_mount *mp,
	xfs_agnumber_t	agcount)
{
	xfs_agnumber_t	index;
	xfs_agnumber_t	maxagi = 0;
	xfs_sb_t	*sbp = &mp->m_sb;
	xfs_agnumber_t	max_metadata;
	xfs_agino_t	agino;
	xfs_ino_t	ino;

	/*
	 * Calculate how much should be reserved for inodes to meet
	 * the max inode percentage.  Used only for inode32.
	 */
	if (M_IGEO(mp)->maxicount) {
		uint64_t	icount;

		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		icount += sbp->sb_agblocks - 1;
		do_div(icount, sbp->sb_agblocks);
		max_metadata = icount;
	} else {
		max_metadata = agcount;
	}

	/* Get the last possible inode in the filesystem */
	agino =	XFS_AGB_TO_AGINO(mp, sbp->sb_agblocks - 1);
	ino = XFS_AGINO_TO_INO(mp, agcount - 1, agino);

	/*
	 * If user asked for no more than 32-bit inodes, and the fs is
	 * sufficiently large, set XFS_MOUNT_32BITINODES if we must alter
	 * the allocator to accommodate the request.
	 */
	if ((mp->m_flags & XFS_MOUNT_SMALL_INUMS) && ino > XFS_MAXINUMBER_32)
		mp->m_flags |= XFS_MOUNT_32BITINODES;
	else
		mp->m_flags &= ~XFS_MOUNT_32BITINODES;

	for (index = 0; index < agcount; index++) {
		struct xfs_perag	*pag;

		ino = XFS_AGINO_TO_INO(mp, index, agino);

		pag = xfs_perag_get(mp, index);

		if (mp->m_flags & XFS_MOUNT_32BITINODES) {
			if (ino > XFS_MAXINUMBER_32) {
				pag->pagi_inodeok = 0;
				pag->pagf_metadata = 0;
			} else {
				pag->pagi_inodeok = 1;
				maxagi++;
				if (index < max_metadata)
					pag->pagf_metadata = 1;
				else
					pag->pagf_metadata = 0;
			}
		} else {
			pag->pagi_inodeok = 1;
			pag->pagf_metadata = 0;
		}

		xfs_perag_put(pag);
	}

	return (mp->m_flags & XFS_MOUNT_32BITINODES) ? maxagi : agcount;
}

STATIC int
xfs_blkdev_get(
	xfs_mount_t		*mp,
	const char		*name,
	struct block_device	**bdevp)
{
	int			error = 0;

	*bdevp = blkdev_get_by_path(name, FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				    mp);
	if (IS_ERR(*bdevp)) {
		error = PTR_ERR(*bdevp);
		xfs_warn(mp, "Invalid device [%s], error=%d", name, error);
	}

	return error;
}

STATIC void
xfs_blkdev_put(
	struct block_device	*bdev)
{
	if (bdev)
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

void
xfs_blkdev_issue_flush(
	xfs_buftarg_t		*buftarg)
{
	blkdev_issue_flush(buftarg->bt_bdev, GFP_NOFS);
}

STATIC void
xfs_close_devices(
	struct xfs_mount	*mp)
{
	struct dax_device *dax_ddev = mp->m_ddev_targp->bt_daxdev;

	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp) {
		struct block_device *logdev = mp->m_logdev_targp->bt_bdev;
		struct dax_device *dax_logdev = mp->m_logdev_targp->bt_daxdev;

		xfs_free_buftarg(mp->m_logdev_targp);
		xfs_blkdev_put(logdev);
		fs_put_dax(dax_logdev);
	}
	if (mp->m_rtdev_targp) {
		struct block_device *rtdev = mp->m_rtdev_targp->bt_bdev;
		struct dax_device *dax_rtdev = mp->m_rtdev_targp->bt_daxdev;

		xfs_free_buftarg(mp->m_rtdev_targp);
		xfs_blkdev_put(rtdev);
		fs_put_dax(dax_rtdev);
	}
	xfs_free_buftarg(mp->m_ddev_targp);
	fs_put_dax(dax_ddev);
}

/*
 * The file system configurations are:
 *	(1) device (partition) with data and internal log
 *	(2) logical volume with data and log subvolumes.
 *	(3) logical volume with data, log, and realtime subvolumes.
 *
 * We only have to handle opening the log and realtime volumes here if
 * they are present.  The data subvolume has already been opened by
 * get_sb_bdev() and is stored in sb->s_bdev.
 */
STATIC int
xfs_open_devices(
	struct xfs_mount	*mp)
{
	struct block_device	*ddev = mp->m_super->s_bdev;
	struct dax_device	*dax_ddev = fs_dax_get_by_bdev(ddev);
	struct dax_device	*dax_logdev = NULL, *dax_rtdev = NULL;
	struct block_device	*logdev = NULL, *rtdev = NULL;
	int			error;

	/*
	 * Open real time and log devices - order is important.
	 */
	if (mp->m_logname) {
		error = xfs_blkdev_get(mp, mp->m_logname, &logdev);
		if (error)
			goto out;
		dax_logdev = fs_dax_get_by_bdev(logdev);
	}

	if (mp->m_rtname) {
		error = xfs_blkdev_get(mp, mp->m_rtname, &rtdev);
		if (error)
			goto out_close_logdev;

		if (rtdev == ddev || rtdev == logdev) {
			xfs_warn(mp,
	"Cannot mount filesystem with identical rtdev and ddev/logdev.");
			error = -EINVAL;
			goto out_close_rtdev;
		}
		dax_rtdev = fs_dax_get_by_bdev(rtdev);
	}

	/*
	 * Setup xfs_mount buffer target pointers
	 */
	error = -ENOMEM;
	mp->m_ddev_targp = xfs_alloc_buftarg(mp, ddev, dax_ddev);
	if (!mp->m_ddev_targp)
		goto out_close_rtdev;

	if (rtdev) {
		mp->m_rtdev_targp = xfs_alloc_buftarg(mp, rtdev, dax_rtdev);
		if (!mp->m_rtdev_targp)
			goto out_free_ddev_targ;
	}

	if (logdev && logdev != ddev) {
		mp->m_logdev_targp = xfs_alloc_buftarg(mp, logdev, dax_logdev);
		if (!mp->m_logdev_targp)
			goto out_free_rtdev_targ;
	} else {
		mp->m_logdev_targp = mp->m_ddev_targp;
	}

	return 0;

 out_free_rtdev_targ:
	if (mp->m_rtdev_targp)
		xfs_free_buftarg(mp->m_rtdev_targp);
 out_free_ddev_targ:
	xfs_free_buftarg(mp->m_ddev_targp);
 out_close_rtdev:
	xfs_blkdev_put(rtdev);
	fs_put_dax(dax_rtdev);
 out_close_logdev:
	if (logdev && logdev != ddev) {
		xfs_blkdev_put(logdev);
		fs_put_dax(dax_logdev);
	}
 out:
	fs_put_dax(dax_ddev);
	return error;
}

/*
 * Setup xfs_mount buffer target pointers based on superblock
 */
STATIC int
xfs_setup_devices(
	struct xfs_mount	*mp)
{
	int			error;

	error = xfs_setsize_buftarg(mp->m_ddev_targp, mp->m_sb.sb_sectsize);
	if (error)
		return error;

	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp) {
		unsigned int	log_sector_size = BBSIZE;

		if (xfs_sb_version_hassector(&mp->m_sb))
			log_sector_size = mp->m_sb.sb_logsectsize;
		error = xfs_setsize_buftarg(mp->m_logdev_targp,
					    log_sector_size);
		if (error)
			return error;
	}
	if (mp->m_rtdev_targp) {
		error = xfs_setsize_buftarg(mp->m_rtdev_targp,
					    mp->m_sb.sb_sectsize);
		if (error)
			return error;
	}

	return 0;
}

STATIC int
xfs_init_mount_workqueues(
	struct xfs_mount	*mp)
{
	mp->m_buf_workqueue = alloc_workqueue("xfs-buf/%s",
			WQ_MEM_RECLAIM|WQ_FREEZABLE, 1, mp->m_super->s_id);
	if (!mp->m_buf_workqueue)
		goto out;

	mp->m_unwritten_workqueue = alloc_workqueue("xfs-conv/%s",
			WQ_MEM_RECLAIM|WQ_FREEZABLE, 0, mp->m_super->s_id);
	if (!mp->m_unwritten_workqueue)
		goto out_destroy_buf;

	mp->m_cil_workqueue = alloc_workqueue("xfs-cil/%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE | WQ_UNBOUND,
			0, mp->m_super->s_id);
	if (!mp->m_cil_workqueue)
		goto out_destroy_unwritten;

	mp->m_reclaim_workqueue = alloc_workqueue("xfs-reclaim/%s",
			WQ_MEM_RECLAIM|WQ_FREEZABLE, 0, mp->m_super->s_id);
	if (!mp->m_reclaim_workqueue)
		goto out_destroy_cil;

	mp->m_eofblocks_workqueue = alloc_workqueue("xfs-eofblocks/%s",
			WQ_MEM_RECLAIM|WQ_FREEZABLE, 0, mp->m_super->s_id);
	if (!mp->m_eofblocks_workqueue)
		goto out_destroy_reclaim;

	mp->m_sync_workqueue = alloc_workqueue("xfs-sync/%s", WQ_FREEZABLE, 0,
					       mp->m_super->s_id);
	if (!mp->m_sync_workqueue)
		goto out_destroy_eofb;

	return 0;

out_destroy_eofb:
	destroy_workqueue(mp->m_eofblocks_workqueue);
out_destroy_reclaim:
	destroy_workqueue(mp->m_reclaim_workqueue);
out_destroy_cil:
	destroy_workqueue(mp->m_cil_workqueue);
out_destroy_unwritten:
	destroy_workqueue(mp->m_unwritten_workqueue);
out_destroy_buf:
	destroy_workqueue(mp->m_buf_workqueue);
out:
	return -ENOMEM;
}

STATIC void
xfs_destroy_mount_workqueues(
	struct xfs_mount	*mp)
{
	destroy_workqueue(mp->m_sync_workqueue);
	destroy_workqueue(mp->m_eofblocks_workqueue);
	destroy_workqueue(mp->m_reclaim_workqueue);
	destroy_workqueue(mp->m_cil_workqueue);
	destroy_workqueue(mp->m_unwritten_workqueue);
	destroy_workqueue(mp->m_buf_workqueue);
}

static void
xfs_flush_inodes_worker(
	struct work_struct	*work)
{
	struct xfs_mount	*mp = container_of(work, struct xfs_mount,
						   m_flush_inodes_work);
	struct super_block	*sb = mp->m_super;

	if (down_read_trylock(&sb->s_umount)) {
		sync_inodes_sb(sb);
		up_read(&sb->s_umount);
	}
}

/*
 * Flush all dirty data to disk. Must not be called while holding an XFS_ILOCK
 * or a page lock. We use sync_inodes_sb() here to ensure we block while waiting
 * for IO to complete so that we effectively throttle multiple callers to the
 * rate at which IO is completing.
 */
void
xfs_flush_inodes(
	struct xfs_mount	*mp)
{
	/*
	 * If flush_work() returns true then that means we waited for a flush
	 * which was already in progress.  Don't bother running another scan.
	 */
	if (flush_work(&mp->m_flush_inodes_work))
		return;

	queue_work(mp->m_sync_workqueue, &mp->m_flush_inodes_work);
	flush_work(&mp->m_flush_inodes_work);
}

/* Catch misguided souls that try to use this interface on XFS */
STATIC struct inode *
xfs_fs_alloc_inode(
	struct super_block	*sb)
{
	BUG();
	return NULL;
}

#ifdef DEBUG
static void
xfs_check_delalloc(
	struct xfs_inode	*ip,
	int			whichfork)
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	struct xfs_bmbt_irec	got;
	struct xfs_iext_cursor	icur;

	if (!ifp || !xfs_iext_lookup_extent(ip, ifp, 0, &icur, &got))
		return;
	do {
		if (isnullstartblock(got.br_startblock)) {
			xfs_warn(ip->i_mount,
	"ino %llx %s fork has delalloc extent at [0x%llx:0x%llx]",
				ip->i_ino,
				whichfork == XFS_DATA_FORK ? "data" : "cow",
				got.br_startoff, got.br_blockcount);
		}
	} while (xfs_iext_next_extent(ifp, &icur, &got));
}
#else
#define xfs_check_delalloc(ip, whichfork)	do { } while (0)
#endif

/*
 * Now that the generic code is guaranteed not to be accessing
 * the linux inode, we can inactivate and reclaim the inode.
 */
STATIC void
xfs_fs_destroy_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);

	trace_xfs_destroy_inode(ip);

	ASSERT(!rwsem_is_locked(&inode->i_rwsem));
	XFS_STATS_INC(ip->i_mount, vn_rele);
	XFS_STATS_INC(ip->i_mount, vn_remove);

	xfs_inactive(ip);

	if (!XFS_FORCED_SHUTDOWN(ip->i_mount) && ip->i_delayed_blks) {
		xfs_check_delalloc(ip, XFS_DATA_FORK);
		xfs_check_delalloc(ip, XFS_COW_FORK);
		ASSERT(0);
	}

	XFS_STATS_INC(ip->i_mount, vn_reclaim);

	/*
	 * We should never get here with one of the reclaim flags already set.
	 */
	ASSERT_ALWAYS(!xfs_iflags_test(ip, XFS_IRECLAIMABLE));
	ASSERT_ALWAYS(!xfs_iflags_test(ip, XFS_IRECLAIM));

	/*
	 * We always use background reclaim here because even if the inode is
	 * clean, it still may be under IO and hence we have wait for IO
	 * completion to occur before we can reclaim the inode. The background
	 * reclaim path handles this more efficiently than we can here, so
	 * simply let background reclaim tear down all inodes.
	 */
	xfs_inode_set_reclaim_tag(ip);
}

static void
xfs_fs_dirty_inode(
	struct inode			*inode,
	int				flag)
{
	struct xfs_inode		*ip = XFS_I(inode);
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_trans		*tp;

	if (!(inode->i_sb->s_flags & SB_LAZYTIME))
		return;
	if (flag != I_DIRTY_SYNC || !(inode->i_state & I_DIRTY_TIME))
		return;

	if (xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0, 0, &tp))
		return;
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_TIMESTAMP);
	xfs_trans_commit(tp);
}

/*
 * Slab object creation initialisation for the XFS inode.
 * This covers only the idempotent fields in the XFS inode;
 * all other fields need to be initialised on allocation
 * from the slab. This avoids the need to repeatedly initialise
 * fields in the xfs inode that left in the initialise state
 * when freeing the inode.
 */
STATIC void
xfs_fs_inode_init_once(
	void			*inode)
{
	struct xfs_inode	*ip = inode;

	memset(ip, 0, sizeof(struct xfs_inode));

	/* vfs inode */
	inode_init_once(VFS_I(ip));

	/* xfs inode */
	atomic_set(&ip->i_pincount, 0);
	spin_lock_init(&ip->i_flags_lock);

	mrlock_init(&ip->i_mmaplock, MRLOCK_ALLOW_EQUAL_PRI|MRLOCK_BARRIER,
		     "xfsino", ip->i_ino);
	mrlock_init(&ip->i_lock, MRLOCK_ALLOW_EQUAL_PRI|MRLOCK_BARRIER,
		     "xfsino", ip->i_ino);
}

/*
 * We do an unlocked check for XFS_IDONTCACHE here because we are already
 * serialised against cache hits here via the inode->i_lock and igrab() in
 * xfs_iget_cache_hit(). Hence a lookup that might clear this flag will not be
 * racing with us, and it avoids needing to grab a spinlock here for every inode
 * we drop the final reference on.
 */
STATIC int
xfs_fs_drop_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);

	/*
	 * If this unlinked inode is in the middle of recovery, don't
	 * drop the inode just yet; log recovery will take care of
	 * that.  See the comment for this inode flag.
	 */
	if (ip->i_flags & XFS_IRECOVERY) {
		ASSERT(ip->i_mount->m_log->l_flags & XLOG_RECOVERY_NEEDED);
		return 0;
	}

	return generic_drop_inode(inode);
}

static void
xfs_mount_free(
	struct xfs_mount	*mp)
{
	kfree(mp->m_rtname);
	kfree(mp->m_logname);
	kmem_free(mp);
}

STATIC int
xfs_fs_sync_fs(
	struct super_block	*sb,
	int			wait)
{
	struct xfs_mount	*mp = XFS_M(sb);

	/*
	 * Doing anything during the async pass would be counterproductive.
	 */
	if (!wait)
		return 0;

	xfs_log_force(mp, XFS_LOG_SYNC);
	if (laptop_mode) {
		/*
		 * The disk must be active because we're syncing.
		 * We schedule log work now (now that the disk is
		 * active) instead of later (when it might not be).
		 */
		flush_delayed_work(&mp->m_log->l_work);
	}

	return 0;
}

STATIC int
xfs_fs_statfs(
	struct dentry		*dentry,
	struct kstatfs		*statp)
{
	struct xfs_mount	*mp = XFS_M(dentry->d_sb);
	xfs_sb_t		*sbp = &mp->m_sb;
	struct xfs_inode	*ip = XFS_I(d_inode(dentry));
	uint64_t		fakeinos, id;
	uint64_t		icount;
	uint64_t		ifree;
	uint64_t		fdblocks;
	xfs_extlen_t		lsize;
	int64_t			ffree;

	statp->f_type = XFS_SUPER_MAGIC;
	statp->f_namelen = MAXNAMELEN - 1;

	id = huge_encode_dev(mp->m_ddev_targp->bt_dev);
	statp->f_fsid = u64_to_fsid(id);

	icount = percpu_counter_sum(&mp->m_icount);
	ifree = percpu_counter_sum(&mp->m_ifree);
	fdblocks = percpu_counter_sum(&mp->m_fdblocks);

	spin_lock(&mp->m_sb_lock);
	statp->f_bsize = sbp->sb_blocksize;
	lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
	statp->f_blocks = sbp->sb_dblocks - lsize;
	spin_unlock(&mp->m_sb_lock);

	/* make sure statp->f_bfree does not underflow */
	statp->f_bfree = max_t(int64_t, fdblocks - mp->m_alloc_set_aside, 0);
	statp->f_bavail = statp->f_bfree;

	fakeinos = XFS_FSB_TO_INO(mp, statp->f_bfree);
	statp->f_files = min(icount + fakeinos, (uint64_t)XFS_MAXINUMBER);
	if (M_IGEO(mp)->maxicount)
		statp->f_files = min_t(typeof(statp->f_files),
					statp->f_files,
					M_IGEO(mp)->maxicount);

	/* If sb_icount overshot maxicount, report actual allocation */
	statp->f_files = max_t(typeof(statp->f_files),
					statp->f_files,
					sbp->sb_icount);

	/* make sure statp->f_ffree does not underflow */
	ffree = statp->f_files - (icount - ifree);
	statp->f_ffree = max_t(int64_t, ffree, 0);


	if ((ip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
	    ((mp->m_qflags & (XFS_PQUOTA_ACCT|XFS_PQUOTA_ENFD))) ==
			      (XFS_PQUOTA_ACCT|XFS_PQUOTA_ENFD))
		xfs_qm_statvfs(ip, statp);

	if (XFS_IS_REALTIME_MOUNT(mp) &&
	    (ip->i_d.di_flags & (XFS_DIFLAG_RTINHERIT | XFS_DIFLAG_REALTIME))) {
		statp->f_blocks = sbp->sb_rblocks;
		statp->f_bavail = statp->f_bfree =
			sbp->sb_frextents * sbp->sb_rextsize;
	}

	return 0;
}

STATIC void
xfs_save_resvblks(struct xfs_mount *mp)
{
	uint64_t resblks = 0;

	mp->m_resblks_save = mp->m_resblks;
	xfs_reserve_blocks(mp, &resblks, NULL);
}

STATIC void
xfs_restore_resvblks(struct xfs_mount *mp)
{
	uint64_t resblks;

	if (mp->m_resblks_save) {
		resblks = mp->m_resblks_save;
		mp->m_resblks_save = 0;
	} else
		resblks = xfs_default_resblks(mp);

	xfs_reserve_blocks(mp, &resblks, NULL);
}

/*
 * Trigger writeback of all the dirty metadata in the file system.
 *
 * This ensures that the metadata is written to their location on disk rather
 * than just existing in transactions in the log. This means after a quiesce
 * there is no log replay required to write the inodes to disk - this is the
 * primary difference between a sync and a quiesce.
 *
 * We cancel log work early here to ensure all transactions the log worker may
 * run have finished before we clean up and log the superblock and write an
 * unmount record. The unfreeze process is responsible for restarting the log
 * worker correctly.
 */
void
xfs_quiesce_attr(
	struct xfs_mount	*mp)
{
	int	error = 0;

	cancel_delayed_work_sync(&mp->m_log->l_work);

	/* force the log to unpin objects from the now complete transactions */
	xfs_log_force(mp, XFS_LOG_SYNC);


	/* Push the superblock and write an unmount record */
	error = xfs_log_sbcount(mp);
	if (error)
		xfs_warn(mp, "xfs_attr_quiesce: failed to log sb changes. "
				"Frozen image may not be consistent.");
	xfs_log_quiesce(mp);
}

/*
 * Second stage of a freeze. The data is already frozen so we only
 * need to take care of the metadata. Once that's done sync the superblock
 * to the log to dirty it in case of a crash while frozen. This ensures that we
 * will recover the unlinked inode lists on the next mount.
 */
STATIC int
xfs_fs_freeze(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);
	unsigned int		flags;
	int			ret;

	/*
	 * The filesystem is now frozen far enough that memory reclaim
	 * cannot safely operate on the filesystem. Hence we need to
	 * set a GFP_NOFS context here to avoid recursion deadlocks.
	 */
	flags = memalloc_nofs_save();
	xfs_stop_block_reaping(mp);
	xfs_save_resvblks(mp);
	xfs_quiesce_attr(mp);
	ret = xfs_sync_sb(mp, true);
	memalloc_nofs_restore(flags);
	return ret;
}

STATIC int
xfs_fs_unfreeze(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_restore_resvblks(mp);
	xfs_log_work_queue(mp);
	xfs_start_block_reaping(mp);
	return 0;
}

/*
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock _has_ now been read in.
 */
STATIC int
xfs_finish_flags(
	struct xfs_mount	*mp)
{
	int			ronly = (mp->m_flags & XFS_MOUNT_RDONLY);

	/* Fail a mount where the logbuf is smaller than the log stripe */
	if (xfs_sb_version_haslogv2(&mp->m_sb)) {
		if (mp->m_logbsize <= 0 &&
		    mp->m_sb.sb_logsunit > XLOG_BIG_RECORD_BSIZE) {
			mp->m_logbsize = mp->m_sb.sb_logsunit;
		} else if (mp->m_logbsize > 0 &&
			   mp->m_logbsize < mp->m_sb.sb_logsunit) {
			xfs_warn(mp,
		"logbuf size must be greater than or equal to log stripe size");
			return -EINVAL;
		}
	} else {
		/* Fail a mount if the logbuf is larger than 32K */
		if (mp->m_logbsize > XLOG_BIG_RECORD_BSIZE) {
			xfs_warn(mp,
		"logbuf size for version 1 logs must be 16K or 32K");
			return -EINVAL;
		}
	}

	/*
	 * V5 filesystems always use attr2 format for attributes.
	 */
	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	    (mp->m_flags & XFS_MOUNT_NOATTR2)) {
		xfs_warn(mp, "Cannot mount a V5 filesystem as noattr2. "
			     "attr2 is always enabled for V5 filesystems.");
		return -EINVAL;
	}

	/*
	 * mkfs'ed attr2 will turn on attr2 mount unless explicitly
	 * told by noattr2 to turn it off
	 */
	if (xfs_sb_version_hasattr2(&mp->m_sb) &&
	    !(mp->m_flags & XFS_MOUNT_NOATTR2))
		mp->m_flags |= XFS_MOUNT_ATTR2;

	/*
	 * prohibit r/w mounts of read-only filesystems
	 */
	if ((mp->m_sb.sb_flags & XFS_SBF_READONLY) && !ronly) {
		xfs_warn(mp,
			"cannot mount a read-only filesystem as read-write");
		return -EROFS;
	}

	if ((mp->m_qflags & (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE)) &&
	    (mp->m_qflags & (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE)) &&
	    !xfs_sb_version_has_pquotino(&mp->m_sb)) {
		xfs_warn(mp,
		  "Super block does not support project and group quota together");
		return -EINVAL;
	}

	return 0;
}

static int
xfs_init_percpu_counters(
	struct xfs_mount	*mp)
{
	int		error;

	error = percpu_counter_init(&mp->m_icount, 0, GFP_KERNEL);
	if (error)
		return -ENOMEM;

	error = percpu_counter_init(&mp->m_ifree, 0, GFP_KERNEL);
	if (error)
		goto free_icount;

	error = percpu_counter_init(&mp->m_fdblocks, 0, GFP_KERNEL);
	if (error)
		goto free_ifree;

	error = percpu_counter_init(&mp->m_delalloc_blks, 0, GFP_KERNEL);
	if (error)
		goto free_fdblocks;

	return 0;

free_fdblocks:
	percpu_counter_destroy(&mp->m_fdblocks);
free_ifree:
	percpu_counter_destroy(&mp->m_ifree);
free_icount:
	percpu_counter_destroy(&mp->m_icount);
	return -ENOMEM;
}

void
xfs_reinit_percpu_counters(
	struct xfs_mount	*mp)
{
	percpu_counter_set(&mp->m_icount, mp->m_sb.sb_icount);
	percpu_counter_set(&mp->m_ifree, mp->m_sb.sb_ifree);
	percpu_counter_set(&mp->m_fdblocks, mp->m_sb.sb_fdblocks);
}

static void
xfs_destroy_percpu_counters(
	struct xfs_mount	*mp)
{
	percpu_counter_destroy(&mp->m_icount);
	percpu_counter_destroy(&mp->m_ifree);
	percpu_counter_destroy(&mp->m_fdblocks);
	ASSERT(XFS_FORCED_SHUTDOWN(mp) ||
	       percpu_counter_sum(&mp->m_delalloc_blks) == 0);
	percpu_counter_destroy(&mp->m_delalloc_blks);
}

static void
xfs_fs_put_super(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	/* if ->fill_super failed, we have no mount to tear down */
	if (!sb->s_fs_info)
		return;

	xfs_notice(mp, "Unmounting Filesystem");
	xfs_filestream_unmount(mp);
	xfs_unmountfs(mp);

	xfs_freesb(mp);
	free_percpu(mp->m_stats.xs_stats);
	xfs_destroy_percpu_counters(mp);
	xfs_destroy_mount_workqueues(mp);
	xfs_close_devices(mp);

	sb->s_fs_info = NULL;
	xfs_mount_free(mp);
}

static long
xfs_fs_nr_cached_objects(
	struct super_block	*sb,
	struct shrink_control	*sc)
{
	/* Paranoia: catch incorrect calls during mount setup or teardown */
	if (WARN_ON_ONCE(!sb->s_fs_info))
		return 0;
	return xfs_reclaim_inodes_count(XFS_M(sb));
}

static long
xfs_fs_free_cached_objects(
	struct super_block	*sb,
	struct shrink_control	*sc)
{
	return xfs_reclaim_inodes_nr(XFS_M(sb), sc->nr_to_scan);
}

static const struct super_operations xfs_super_operations = {
	.alloc_inode		= xfs_fs_alloc_inode,
	.destroy_inode		= xfs_fs_destroy_inode,
	.dirty_inode		= xfs_fs_dirty_inode,
	.drop_inode		= xfs_fs_drop_inode,
	.put_super		= xfs_fs_put_super,
	.sync_fs		= xfs_fs_sync_fs,
	.freeze_fs		= xfs_fs_freeze,
	.unfreeze_fs		= xfs_fs_unfreeze,
	.statfs			= xfs_fs_statfs,
	.show_options		= xfs_fs_show_options,
	.nr_cached_objects	= xfs_fs_nr_cached_objects,
	.free_cached_objects	= xfs_fs_free_cached_objects,
};

static int
suffix_kstrtoint(
	const char	*s,
	unsigned int	base,
	int		*res)
{
	int		last, shift_left_factor = 0, _res;
	char		*value;
	int		ret = 0;

	value = kstrdup(s, GFP_KERNEL);
	if (!value)
		return -ENOMEM;

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

	if (kstrtoint(value, base, &_res))
		ret = -EINVAL;
	kfree(value);
	*res = _res << shift_left_factor;
	return ret;
}

static inline void
xfs_fs_warn_deprecated(
	struct fs_context	*fc,
	struct fs_parameter	*param,
	uint64_t		flag,
	bool			value)
{
	/* Don't print the warning if reconfiguring and current mount point
	 * already had the flag set
	 */
	if ((fc->purpose & FS_CONTEXT_FOR_RECONFIGURE) &&
			!!(XFS_M(fc->root->d_sb)->m_flags & flag) == value)
		return;
	xfs_warn(fc->s_fs_info, "%s mount option is deprecated.", param->key);
}

/*
 * Set mount state from a mount option.
 *
 * NOTE: mp->m_super is NULL here!
 */
static int
xfs_fc_parse_param(
	struct fs_context	*fc,
	struct fs_parameter	*param)
{
	struct xfs_mount	*parsing_mp = fc->s_fs_info;
	struct fs_parse_result	result;
	int			size = 0;
	int			opt;

	opt = fs_parse(fc, xfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_logbufs:
		parsing_mp->m_logbufs = result.uint_32;
		return 0;
	case Opt_logbsize:
		if (suffix_kstrtoint(param->string, 10, &parsing_mp->m_logbsize))
			return -EINVAL;
		return 0;
	case Opt_logdev:
		kfree(parsing_mp->m_logname);
		parsing_mp->m_logname = kstrdup(param->string, GFP_KERNEL);
		if (!parsing_mp->m_logname)
			return -ENOMEM;
		return 0;
	case Opt_rtdev:
		kfree(parsing_mp->m_rtname);
		parsing_mp->m_rtname = kstrdup(param->string, GFP_KERNEL);
		if (!parsing_mp->m_rtname)
			return -ENOMEM;
		return 0;
	case Opt_allocsize:
		if (suffix_kstrtoint(param->string, 10, &size))
			return -EINVAL;
		parsing_mp->m_allocsize_log = ffs(size) - 1;
		parsing_mp->m_flags |= XFS_MOUNT_ALLOCSIZE;
		return 0;
	case Opt_grpid:
	case Opt_bsdgroups:
		parsing_mp->m_flags |= XFS_MOUNT_GRPID;
		return 0;
	case Opt_nogrpid:
	case Opt_sysvgroups:
		parsing_mp->m_flags &= ~XFS_MOUNT_GRPID;
		return 0;
	case Opt_wsync:
		parsing_mp->m_flags |= XFS_MOUNT_WSYNC;
		return 0;
	case Opt_norecovery:
		parsing_mp->m_flags |= XFS_MOUNT_NORECOVERY;
		return 0;
	case Opt_noalign:
		parsing_mp->m_flags |= XFS_MOUNT_NOALIGN;
		return 0;
	case Opt_swalloc:
		parsing_mp->m_flags |= XFS_MOUNT_SWALLOC;
		return 0;
	case Opt_sunit:
		parsing_mp->m_dalign = result.uint_32;
		return 0;
	case Opt_swidth:
		parsing_mp->m_swidth = result.uint_32;
		return 0;
	case Opt_inode32:
		parsing_mp->m_flags |= XFS_MOUNT_SMALL_INUMS;
		return 0;
	case Opt_inode64:
		parsing_mp->m_flags &= ~XFS_MOUNT_SMALL_INUMS;
		return 0;
	case Opt_nouuid:
		parsing_mp->m_flags |= XFS_MOUNT_NOUUID;
		return 0;
	case Opt_largeio:
		parsing_mp->m_flags |= XFS_MOUNT_LARGEIO;
		return 0;
	case Opt_nolargeio:
		parsing_mp->m_flags &= ~XFS_MOUNT_LARGEIO;
		return 0;
	case Opt_filestreams:
		parsing_mp->m_flags |= XFS_MOUNT_FILESTREAMS;
		return 0;
	case Opt_noquota:
		parsing_mp->m_qflags &= ~XFS_ALL_QUOTA_ACCT;
		parsing_mp->m_qflags &= ~XFS_ALL_QUOTA_ENFD;
		parsing_mp->m_qflags &= ~XFS_ALL_QUOTA_ACTIVE;
		return 0;
	case Opt_quota:
	case Opt_uquota:
	case Opt_usrquota:
		parsing_mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE |
				 XFS_UQUOTA_ENFD);
		return 0;
	case Opt_qnoenforce:
	case Opt_uqnoenforce:
		parsing_mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE);
		parsing_mp->m_qflags &= ~XFS_UQUOTA_ENFD;
		return 0;
	case Opt_pquota:
	case Opt_prjquota:
		parsing_mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE |
				 XFS_PQUOTA_ENFD);
		return 0;
	case Opt_pqnoenforce:
		parsing_mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE);
		parsing_mp->m_qflags &= ~XFS_PQUOTA_ENFD;
		return 0;
	case Opt_gquota:
	case Opt_grpquota:
		parsing_mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE |
				 XFS_GQUOTA_ENFD);
		return 0;
	case Opt_gqnoenforce:
		parsing_mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE);
		parsing_mp->m_qflags &= ~XFS_GQUOTA_ENFD;
		return 0;
	case Opt_discard:
		parsing_mp->m_flags |= XFS_MOUNT_DISCARD;
		return 0;
	case Opt_nodiscard:
		parsing_mp->m_flags &= ~XFS_MOUNT_DISCARD;
		return 0;
#ifdef CONFIG_FS_DAX
	case Opt_dax:
		xfs_mount_set_dax_mode(parsing_mp, XFS_DAX_ALWAYS);
		return 0;
	case Opt_dax_enum:
		xfs_mount_set_dax_mode(parsing_mp, result.uint_32);
		return 0;
#endif
	/* Following mount options will be removed in September 2025 */
	case Opt_ikeep:
		xfs_fs_warn_deprecated(fc, param, XFS_MOUNT_IKEEP, true);
		parsing_mp->m_flags |= XFS_MOUNT_IKEEP;
		return 0;
	case Opt_noikeep:
		xfs_fs_warn_deprecated(fc, param, XFS_MOUNT_IKEEP, false);
		parsing_mp->m_flags &= ~XFS_MOUNT_IKEEP;
		return 0;
	case Opt_attr2:
		xfs_fs_warn_deprecated(fc, param, XFS_MOUNT_ATTR2, true);
		parsing_mp->m_flags |= XFS_MOUNT_ATTR2;
		return 0;
	case Opt_noattr2:
		xfs_fs_warn_deprecated(fc, param, XFS_MOUNT_NOATTR2, true);
		parsing_mp->m_flags &= ~XFS_MOUNT_ATTR2;
		parsing_mp->m_flags |= XFS_MOUNT_NOATTR2;
		return 0;
	default:
		xfs_warn(parsing_mp, "unknown mount option [%s].", param->key);
		return -EINVAL;
	}

	return 0;
}

static int
xfs_fc_validate_params(
	struct xfs_mount	*mp)
{
	/*
	 * no recovery flag requires a read-only mount
	 */
	if ((mp->m_flags & XFS_MOUNT_NORECOVERY) &&
	    !(mp->m_flags & XFS_MOUNT_RDONLY)) {
		xfs_warn(mp, "no-recovery mounts must be read-only.");
		return -EINVAL;
	}

	if ((mp->m_flags & XFS_MOUNT_NOALIGN) &&
	    (mp->m_dalign || mp->m_swidth)) {
		xfs_warn(mp,
	"sunit and swidth options incompatible with the noalign option");
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_XFS_QUOTA) && mp->m_qflags != 0) {
		xfs_warn(mp, "quota support not available in this kernel.");
		return -EINVAL;
	}

	if ((mp->m_dalign && !mp->m_swidth) ||
	    (!mp->m_dalign && mp->m_swidth)) {
		xfs_warn(mp, "sunit and swidth must be specified together");
		return -EINVAL;
	}

	if (mp->m_dalign && (mp->m_swidth % mp->m_dalign != 0)) {
		xfs_warn(mp,
	"stripe width (%d) must be a multiple of the stripe unit (%d)",
			mp->m_swidth, mp->m_dalign);
		return -EINVAL;
	}

	if (mp->m_logbufs != -1 &&
	    mp->m_logbufs != 0 &&
	    (mp->m_logbufs < XLOG_MIN_ICLOGS ||
	     mp->m_logbufs > XLOG_MAX_ICLOGS)) {
		xfs_warn(mp, "invalid logbufs value: %d [not %d-%d]",
			mp->m_logbufs, XLOG_MIN_ICLOGS, XLOG_MAX_ICLOGS);
		return -EINVAL;
	}

	if (mp->m_logbsize != -1 &&
	    mp->m_logbsize !=  0 &&
	    (mp->m_logbsize < XLOG_MIN_RECORD_BSIZE ||
	     mp->m_logbsize > XLOG_MAX_RECORD_BSIZE ||
	     !is_power_of_2(mp->m_logbsize))) {
		xfs_warn(mp,
			"invalid logbufsize: %d [not 16k,32k,64k,128k or 256k]",
			mp->m_logbsize);
		return -EINVAL;
	}

	if ((mp->m_flags & XFS_MOUNT_ALLOCSIZE) &&
	    (mp->m_allocsize_log > XFS_MAX_IO_LOG ||
	     mp->m_allocsize_log < XFS_MIN_IO_LOG)) {
		xfs_warn(mp, "invalid log iosize: %d [not %d-%d]",
			mp->m_allocsize_log, XFS_MIN_IO_LOG, XFS_MAX_IO_LOG);
		return -EINVAL;
	}

	return 0;
}

static int
xfs_fc_fill_super(
	struct super_block	*sb,
	struct fs_context	*fc)
{
	struct xfs_mount	*mp = sb->s_fs_info;
	struct inode		*root;
	int			flags = 0, error;

	mp->m_super = sb;

	error = xfs_fc_validate_params(mp);
	if (error)
		goto out_free_names;

	sb_min_blocksize(sb, BBSIZE);
	sb->s_xattr = xfs_xattr_handlers;
	sb->s_export_op = &xfs_export_operations;
#ifdef CONFIG_XFS_QUOTA
	sb->s_qcop = &xfs_quotactl_operations;
	sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP | QTYPE_MASK_PRJ;
#endif
	sb->s_op = &xfs_super_operations;

	/*
	 * Delay mount work if the debug hook is set. This is debug
	 * instrumention to coordinate simulation of xfs mount failures with
	 * VFS superblock operations
	 */
	if (xfs_globals.mount_delay) {
		xfs_notice(mp, "Delaying mount for %d seconds.",
			xfs_globals.mount_delay);
		msleep(xfs_globals.mount_delay * 1000);
	}

	if (fc->sb_flags & SB_SILENT)
		flags |= XFS_MFSI_QUIET;

	error = xfs_open_devices(mp);
	if (error)
		goto out_free_names;

	error = xfs_init_mount_workqueues(mp);
	if (error)
		goto out_close_devices;

	error = xfs_init_percpu_counters(mp);
	if (error)
		goto out_destroy_workqueues;

	/* Allocate stats memory before we do operations that might use it */
	mp->m_stats.xs_stats = alloc_percpu(struct xfsstats);
	if (!mp->m_stats.xs_stats) {
		error = -ENOMEM;
		goto out_destroy_counters;
	}

	error = xfs_readsb(mp, flags);
	if (error)
		goto out_free_stats;

	error = xfs_finish_flags(mp);
	if (error)
		goto out_free_sb;

	error = xfs_setup_devices(mp);
	if (error)
		goto out_free_sb;

	/* V4 support is undergoing deprecation. */
	if (!xfs_sb_version_hascrc(&mp->m_sb)) {
#ifdef CONFIG_XFS_SUPPORT_V4
		xfs_warn_once(mp,
	"Deprecated V4 format (crc=0) will not be supported after September 2030.");
#else
		xfs_warn(mp,
	"Deprecated V4 format (crc=0) not supported by kernel.");
		error = -EINVAL;
		goto out_free_sb;
#endif
	}

	/*
	 * XFS block mappings use 54 bits to store the logical block offset.
	 * This should suffice to handle the maximum file size that the VFS
	 * supports (currently 2^63 bytes on 64-bit and ULONG_MAX << PAGE_SHIFT
	 * bytes on 32-bit), but as XFS and VFS have gotten the s_maxbytes
	 * calculation wrong on 32-bit kernels in the past, we'll add a WARN_ON
	 * to check this assertion.
	 *
	 * Avoid integer overflow by comparing the maximum bmbt offset to the
	 * maximum pagecache offset in units of fs blocks.
	 */
	if (XFS_B_TO_FSBT(mp, MAX_LFS_FILESIZE) > XFS_MAX_FILEOFF) {
		xfs_warn(mp,
"MAX_LFS_FILESIZE block offset (%llu) exceeds extent map maximum (%llu)!",
			 XFS_B_TO_FSBT(mp, MAX_LFS_FILESIZE),
			 XFS_MAX_FILEOFF);
		error = -EINVAL;
		goto out_free_sb;
	}

	error = xfs_filestream_mount(mp);
	if (error)
		goto out_free_sb;

	/*
	 * we must configure the block size in the superblock before we run the
	 * full mount process as the mount process can lookup and cache inodes.
	 */
	sb->s_magic = XFS_SUPER_MAGIC;
	sb->s_blocksize = mp->m_sb.sb_blocksize;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_max_links = XFS_MAXLINK;
	sb->s_time_gran = 1;
	if (xfs_sb_version_hasbigtime(&mp->m_sb)) {
		sb->s_time_min = xfs_bigtime_to_unix(XFS_BIGTIME_TIME_MIN);
		sb->s_time_max = xfs_bigtime_to_unix(XFS_BIGTIME_TIME_MAX);
	} else {
		sb->s_time_min = XFS_LEGACY_TIME_MIN;
		sb->s_time_max = XFS_LEGACY_TIME_MAX;
	}
	trace_xfs_inode_timestamp_range(mp, sb->s_time_min, sb->s_time_max);
	sb->s_iflags |= SB_I_CGROUPWB;

	set_posix_acl_flag(sb);

	/* version 5 superblocks support inode version counters. */
	if (XFS_SB_VERSION_NUM(&mp->m_sb) == XFS_SB_VERSION_5)
		sb->s_flags |= SB_I_VERSION;

	if (xfs_sb_version_hasbigtime(&mp->m_sb))
		xfs_warn(mp,
 "EXPERIMENTAL big timestamp feature in use. Use at your own risk!");

	if (mp->m_flags & XFS_MOUNT_DAX_ALWAYS) {
		bool rtdev_is_dax = false, datadev_is_dax;

		xfs_warn(mp,
		"DAX enabled. Warning: EXPERIMENTAL, use at your own risk");

		datadev_is_dax = bdev_dax_supported(mp->m_ddev_targp->bt_bdev,
			sb->s_blocksize);
		if (mp->m_rtdev_targp)
			rtdev_is_dax = bdev_dax_supported(
				mp->m_rtdev_targp->bt_bdev, sb->s_blocksize);
		if (!rtdev_is_dax && !datadev_is_dax) {
			xfs_alert(mp,
			"DAX unsupported by block device. Turning off DAX.");
			xfs_mount_set_dax_mode(mp, XFS_DAX_NEVER);
		}
		if (xfs_sb_version_hasreflink(&mp->m_sb)) {
			xfs_alert(mp,
		"DAX and reflink cannot be used together!");
			error = -EINVAL;
			goto out_filestream_unmount;
		}
	}

	if (mp->m_flags & XFS_MOUNT_DISCARD) {
		struct request_queue *q = bdev_get_queue(sb->s_bdev);

		if (!blk_queue_discard(q)) {
			xfs_warn(mp, "mounting with \"discard\" option, but "
					"the device does not support discard");
			mp->m_flags &= ~XFS_MOUNT_DISCARD;
		}
	}

	if (xfs_sb_version_hasreflink(&mp->m_sb)) {
		if (mp->m_sb.sb_rblocks) {
			xfs_alert(mp,
	"reflink not compatible with realtime device!");
			error = -EINVAL;
			goto out_filestream_unmount;
		}

		if (xfs_globals.always_cow) {
			xfs_info(mp, "using DEBUG-only always_cow mode.");
			mp->m_always_cow = true;
		}
	}

	if (xfs_sb_version_hasrmapbt(&mp->m_sb) && mp->m_sb.sb_rblocks) {
		xfs_alert(mp,
	"reverse mapping btree not compatible with realtime device!");
		error = -EINVAL;
		goto out_filestream_unmount;
	}

	if (xfs_sb_version_hasinobtcounts(&mp->m_sb))
		xfs_warn(mp,
 "EXPERIMENTAL inode btree counters feature in use. Use at your own risk!");

	error = xfs_mountfs(mp);
	if (error)
		goto out_filestream_unmount;

	root = igrab(VFS_I(mp->m_rootip));
	if (!root) {
		error = -ENOENT;
		goto out_unmount;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		error = -ENOMEM;
		goto out_unmount;
	}

	return 0;

 out_filestream_unmount:
	xfs_filestream_unmount(mp);
 out_free_sb:
	xfs_freesb(mp);
 out_free_stats:
	free_percpu(mp->m_stats.xs_stats);
 out_destroy_counters:
	xfs_destroy_percpu_counters(mp);
 out_destroy_workqueues:
	xfs_destroy_mount_workqueues(mp);
 out_close_devices:
	xfs_close_devices(mp);
 out_free_names:
	sb->s_fs_info = NULL;
	xfs_mount_free(mp);
	return error;

 out_unmount:
	xfs_filestream_unmount(mp);
	xfs_unmountfs(mp);
	goto out_free_sb;
}

static int
xfs_fc_get_tree(
	struct fs_context	*fc)
{
	return get_tree_bdev(fc, xfs_fc_fill_super);
}

static int
xfs_remount_rw(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	int error;

	if (mp->m_flags & XFS_MOUNT_NORECOVERY) {
		xfs_warn(mp,
			"ro->rw transition prohibited on norecovery mount");
		return -EINVAL;
	}

	if (XFS_SB_VERSION_NUM(sbp) == XFS_SB_VERSION_5 &&
	    xfs_sb_has_ro_compat_feature(sbp, XFS_SB_FEAT_RO_COMPAT_UNKNOWN)) {
		xfs_warn(mp,
	"ro->rw transition prohibited on unknown (0x%x) ro-compat filesystem",
			(sbp->sb_features_ro_compat &
				XFS_SB_FEAT_RO_COMPAT_UNKNOWN));
		return -EINVAL;
	}

	mp->m_flags &= ~XFS_MOUNT_RDONLY;

	/*
	 * If this is the first remount to writeable state we might have some
	 * superblock changes to update.
	 */
	if (mp->m_update_sb) {
		error = xfs_sync_sb(mp, false);
		if (error) {
			xfs_warn(mp, "failed to write sb changes");
			return error;
		}
		mp->m_update_sb = false;
	}

	/*
	 * Fill out the reserve pool if it is empty. Use the stashed value if
	 * it is non-zero, otherwise go with the default.
	 */
	xfs_restore_resvblks(mp);
	xfs_log_work_queue(mp);

	/* Recover any CoW blocks that never got remapped. */
	error = xfs_reflink_recover_cow(mp);
	if (error) {
		xfs_err(mp,
			"Error %d recovering leftover CoW allocations.", error);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return error;
	}
	xfs_start_block_reaping(mp);

	/* Create the per-AG metadata reservation pool .*/
	error = xfs_fs_reserve_ag_blocks(mp);
	if (error && error != -ENOSPC)
		return error;

	return 0;
}

static int
xfs_remount_ro(
	struct xfs_mount	*mp)
{
	struct xfs_eofblocks	eofb = {
		.eof_flags	= XFS_EOF_FLAGS_SYNC,
	};
	int			error;

	/*
	 * Cancel background eofb scanning so it cannot race with the final
	 * log force+buftarg wait and deadlock the remount.
	 */
	xfs_stop_block_reaping(mp);

	/*
	 * Clear out all remaining COW staging extents and speculative post-EOF
	 * preallocations so that we don't leave inodes requiring inactivation
	 * cleanups during reclaim on a read-only mount.  We must process every
	 * cached inode, so this requires a synchronous cache scan.
	 */
	error = xfs_icache_free_cowblocks(mp, &eofb);
	if (error) {
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return error;
	}

	/* Free the per-AG metadata reservation pool. */
	error = xfs_fs_unreserve_ag_blocks(mp);
	if (error) {
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return error;
	}

	/*
	 * Before we sync the metadata, we need to free up the reserve block
	 * pool so that the used block count in the superblock on disk is
	 * correct at the end of the remount. Stash the current* reserve pool
	 * size so that if we get remounted rw, we can return it to the same
	 * size.
	 */
	xfs_save_resvblks(mp);

	xfs_quiesce_attr(mp);
	mp->m_flags |= XFS_MOUNT_RDONLY;

	return 0;
}

/*
 * Logically we would return an error here to prevent users from believing
 * they might have changed mount options using remount which can't be changed.
 *
 * But unfortunately mount(8) adds all options from mtab and fstab to the mount
 * arguments in some cases so we can't blindly reject options, but have to
 * check for each specified option if it actually differs from the currently
 * set option and only reject it if that's the case.
 *
 * Until that is implemented we return success for every remount request, and
 * silently ignore all options that we can't actually change.
 */
static int
xfs_fc_reconfigure(
	struct fs_context *fc)
{
	struct xfs_mount	*mp = XFS_M(fc->root->d_sb);
	struct xfs_mount        *new_mp = fc->s_fs_info;
	xfs_sb_t		*sbp = &mp->m_sb;
	int			flags = fc->sb_flags;
	int			error;

	/* version 5 superblocks always support version counters. */
	if (XFS_SB_VERSION_NUM(&mp->m_sb) == XFS_SB_VERSION_5)
		fc->sb_flags |= SB_I_VERSION;

	error = xfs_fc_validate_params(new_mp);
	if (error)
		return error;

	sync_filesystem(mp->m_super);

	/* inode32 -> inode64 */
	if ((mp->m_flags & XFS_MOUNT_SMALL_INUMS) &&
	    !(new_mp->m_flags & XFS_MOUNT_SMALL_INUMS)) {
		mp->m_flags &= ~XFS_MOUNT_SMALL_INUMS;
		mp->m_maxagi = xfs_set_inode_alloc(mp, sbp->sb_agcount);
	}

	/* inode64 -> inode32 */
	if (!(mp->m_flags & XFS_MOUNT_SMALL_INUMS) &&
	    (new_mp->m_flags & XFS_MOUNT_SMALL_INUMS)) {
		mp->m_flags |= XFS_MOUNT_SMALL_INUMS;
		mp->m_maxagi = xfs_set_inode_alloc(mp, sbp->sb_agcount);
	}

	/* ro -> rw */
	if ((mp->m_flags & XFS_MOUNT_RDONLY) && !(flags & SB_RDONLY)) {
		error = xfs_remount_rw(mp);
		if (error)
			return error;
	}

	/* rw -> ro */
	if (!(mp->m_flags & XFS_MOUNT_RDONLY) && (flags & SB_RDONLY)) {
		error = xfs_remount_ro(mp);
		if (error)
			return error;
	}

	return 0;
}

static void xfs_fc_free(
	struct fs_context	*fc)
{
	struct xfs_mount	*mp = fc->s_fs_info;

	/*
	 * mp is stored in the fs_context when it is initialized.
	 * mp is transferred to the superblock on a successful mount,
	 * but if an error occurs before the transfer we have to free
	 * it here.
	 */
	if (mp)
		xfs_mount_free(mp);
}

static const struct fs_context_operations xfs_context_ops = {
	.parse_param = xfs_fc_parse_param,
	.get_tree    = xfs_fc_get_tree,
	.reconfigure = xfs_fc_reconfigure,
	.free        = xfs_fc_free,
};

static int xfs_init_fs_context(
	struct fs_context	*fc)
{
	struct xfs_mount	*mp;

	mp = kmem_alloc(sizeof(struct xfs_mount), KM_ZERO);
	if (!mp)
		return -ENOMEM;

	spin_lock_init(&mp->m_sb_lock);
	spin_lock_init(&mp->m_agirotor_lock);
	INIT_RADIX_TREE(&mp->m_perag_tree, GFP_ATOMIC);
	spin_lock_init(&mp->m_perag_lock);
	mutex_init(&mp->m_growlock);
	INIT_WORK(&mp->m_flush_inodes_work, xfs_flush_inodes_worker);
	INIT_DELAYED_WORK(&mp->m_reclaim_work, xfs_reclaim_worker);
	INIT_DELAYED_WORK(&mp->m_eofblocks_work, xfs_eofblocks_worker);
	INIT_DELAYED_WORK(&mp->m_cowblocks_work, xfs_cowblocks_worker);
	mp->m_kobj.kobject.kset = xfs_kset;
	/*
	 * We don't create the finobt per-ag space reservation until after log
	 * recovery, so we must set this to true so that an ifree transaction
	 * started during log recovery will not depend on space reservations
	 * for finobt expansion.
	 */
	mp->m_finobt_nores = true;

	/*
	 * These can be overridden by the mount option parsing.
	 */
	mp->m_logbufs = -1;
	mp->m_logbsize = -1;
	mp->m_allocsize_log = 16; /* 64k */

	/*
	 * Copy binary VFS mount flags we are interested in.
	 */
	if (fc->sb_flags & SB_RDONLY)
		mp->m_flags |= XFS_MOUNT_RDONLY;
	if (fc->sb_flags & SB_DIRSYNC)
		mp->m_flags |= XFS_MOUNT_DIRSYNC;
	if (fc->sb_flags & SB_SYNCHRONOUS)
		mp->m_flags |= XFS_MOUNT_WSYNC;

	fc->s_fs_info = mp;
	fc->ops = &xfs_context_ops;

	return 0;
}

static struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.init_fs_context	= xfs_init_fs_context,
	.parameters		= xfs_fs_parameters,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("xfs");

STATIC int __init
xfs_init_zones(void)
{
	xfs_log_ticket_zone = kmem_cache_create("xfs_log_ticket",
						sizeof(struct xlog_ticket),
						0, 0, NULL);
	if (!xfs_log_ticket_zone)
		goto out;

	xfs_bmap_free_item_zone = kmem_cache_create("xfs_bmap_free_item",
					sizeof(struct xfs_extent_free_item),
					0, 0, NULL);
	if (!xfs_bmap_free_item_zone)
		goto out_destroy_log_ticket_zone;

	xfs_btree_cur_zone = kmem_cache_create("xfs_btree_cur",
					       sizeof(struct xfs_btree_cur),
					       0, 0, NULL);
	if (!xfs_btree_cur_zone)
		goto out_destroy_bmap_free_item_zone;

	xfs_da_state_zone = kmem_cache_create("xfs_da_state",
					      sizeof(struct xfs_da_state),
					      0, 0, NULL);
	if (!xfs_da_state_zone)
		goto out_destroy_btree_cur_zone;

	xfs_ifork_zone = kmem_cache_create("xfs_ifork",
					   sizeof(struct xfs_ifork),
					   0, 0, NULL);
	if (!xfs_ifork_zone)
		goto out_destroy_da_state_zone;

	xfs_trans_zone = kmem_cache_create("xf_trans",
					   sizeof(struct xfs_trans),
					   0, 0, NULL);
	if (!xfs_trans_zone)
		goto out_destroy_ifork_zone;


	/*
	 * The size of the zone allocated buf log item is the maximum
	 * size possible under XFS.  This wastes a little bit of memory,
	 * but it is much faster.
	 */
	xfs_buf_item_zone = kmem_cache_create("xfs_buf_item",
					      sizeof(struct xfs_buf_log_item),
					      0, 0, NULL);
	if (!xfs_buf_item_zone)
		goto out_destroy_trans_zone;

	xfs_efd_zone = kmem_cache_create("xfs_efd_item",
					(sizeof(struct xfs_efd_log_item) +
					(XFS_EFD_MAX_FAST_EXTENTS - 1) *
					sizeof(struct xfs_extent)),
					0, 0, NULL);
	if (!xfs_efd_zone)
		goto out_destroy_buf_item_zone;

	xfs_efi_zone = kmem_cache_create("xfs_efi_item",
					 (sizeof(struct xfs_efi_log_item) +
					 (XFS_EFI_MAX_FAST_EXTENTS - 1) *
					 sizeof(struct xfs_extent)),
					 0, 0, NULL);
	if (!xfs_efi_zone)
		goto out_destroy_efd_zone;

	xfs_inode_zone = kmem_cache_create("xfs_inode",
					   sizeof(struct xfs_inode), 0,
					   (SLAB_HWCACHE_ALIGN |
					    SLAB_RECLAIM_ACCOUNT |
					    SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					   xfs_fs_inode_init_once);
	if (!xfs_inode_zone)
		goto out_destroy_efi_zone;

	xfs_ili_zone = kmem_cache_create("xfs_ili",
					 sizeof(struct xfs_inode_log_item), 0,
					 SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
					 NULL);
	if (!xfs_ili_zone)
		goto out_destroy_inode_zone;

	xfs_icreate_zone = kmem_cache_create("xfs_icr",
					     sizeof(struct xfs_icreate_item),
					     0, 0, NULL);
	if (!xfs_icreate_zone)
		goto out_destroy_ili_zone;

	xfs_rud_zone = kmem_cache_create("xfs_rud_item",
					 sizeof(struct xfs_rud_log_item),
					 0, 0, NULL);
	if (!xfs_rud_zone)
		goto out_destroy_icreate_zone;

	xfs_rui_zone = kmem_cache_create("xfs_rui_item",
			xfs_rui_log_item_sizeof(XFS_RUI_MAX_FAST_EXTENTS),
			0, 0, NULL);
	if (!xfs_rui_zone)
		goto out_destroy_rud_zone;

	xfs_cud_zone = kmem_cache_create("xfs_cud_item",
					 sizeof(struct xfs_cud_log_item),
					 0, 0, NULL);
	if (!xfs_cud_zone)
		goto out_destroy_rui_zone;

	xfs_cui_zone = kmem_cache_create("xfs_cui_item",
			xfs_cui_log_item_sizeof(XFS_CUI_MAX_FAST_EXTENTS),
			0, 0, NULL);
	if (!xfs_cui_zone)
		goto out_destroy_cud_zone;

	xfs_bud_zone = kmem_cache_create("xfs_bud_item",
					 sizeof(struct xfs_bud_log_item),
					 0, 0, NULL);
	if (!xfs_bud_zone)
		goto out_destroy_cui_zone;

	xfs_bui_zone = kmem_cache_create("xfs_bui_item",
			xfs_bui_log_item_sizeof(XFS_BUI_MAX_FAST_EXTENTS),
			0, 0, NULL);
	if (!xfs_bui_zone)
		goto out_destroy_bud_zone;

	return 0;

 out_destroy_bud_zone:
	kmem_cache_destroy(xfs_bud_zone);
 out_destroy_cui_zone:
	kmem_cache_destroy(xfs_cui_zone);
 out_destroy_cud_zone:
	kmem_cache_destroy(xfs_cud_zone);
 out_destroy_rui_zone:
	kmem_cache_destroy(xfs_rui_zone);
 out_destroy_rud_zone:
	kmem_cache_destroy(xfs_rud_zone);
 out_destroy_icreate_zone:
	kmem_cache_destroy(xfs_icreate_zone);
 out_destroy_ili_zone:
	kmem_cache_destroy(xfs_ili_zone);
 out_destroy_inode_zone:
	kmem_cache_destroy(xfs_inode_zone);
 out_destroy_efi_zone:
	kmem_cache_destroy(xfs_efi_zone);
 out_destroy_efd_zone:
	kmem_cache_destroy(xfs_efd_zone);
 out_destroy_buf_item_zone:
	kmem_cache_destroy(xfs_buf_item_zone);
 out_destroy_trans_zone:
	kmem_cache_destroy(xfs_trans_zone);
 out_destroy_ifork_zone:
	kmem_cache_destroy(xfs_ifork_zone);
 out_destroy_da_state_zone:
	kmem_cache_destroy(xfs_da_state_zone);
 out_destroy_btree_cur_zone:
	kmem_cache_destroy(xfs_btree_cur_zone);
 out_destroy_bmap_free_item_zone:
	kmem_cache_destroy(xfs_bmap_free_item_zone);
 out_destroy_log_ticket_zone:
	kmem_cache_destroy(xfs_log_ticket_zone);
 out:
	return -ENOMEM;
}

STATIC void
xfs_destroy_zones(void)
{
	/*
	 * Make sure all delayed rcu free are flushed before we
	 * destroy caches.
	 */
	rcu_barrier();
	kmem_cache_destroy(xfs_bui_zone);
	kmem_cache_destroy(xfs_bud_zone);
	kmem_cache_destroy(xfs_cui_zone);
	kmem_cache_destroy(xfs_cud_zone);
	kmem_cache_destroy(xfs_rui_zone);
	kmem_cache_destroy(xfs_rud_zone);
	kmem_cache_destroy(xfs_icreate_zone);
	kmem_cache_destroy(xfs_ili_zone);
	kmem_cache_destroy(xfs_inode_zone);
	kmem_cache_destroy(xfs_efi_zone);
	kmem_cache_destroy(xfs_efd_zone);
	kmem_cache_destroy(xfs_buf_item_zone);
	kmem_cache_destroy(xfs_trans_zone);
	kmem_cache_destroy(xfs_ifork_zone);
	kmem_cache_destroy(xfs_da_state_zone);
	kmem_cache_destroy(xfs_btree_cur_zone);
	kmem_cache_destroy(xfs_bmap_free_item_zone);
	kmem_cache_destroy(xfs_log_ticket_zone);
}

STATIC int __init
xfs_init_workqueues(void)
{
	/*
	 * The allocation workqueue can be used in memory reclaim situations
	 * (writepage path), and parallelism is only limited by the number of
	 * AGs in all the filesystems mounted. Hence use the default large
	 * max_active value for this workqueue.
	 */
	xfs_alloc_wq = alloc_workqueue("xfsalloc",
			WQ_MEM_RECLAIM|WQ_FREEZABLE, 0);
	if (!xfs_alloc_wq)
		return -ENOMEM;

	xfs_discard_wq = alloc_workqueue("xfsdiscard", WQ_UNBOUND, 0);
	if (!xfs_discard_wq)
		goto out_free_alloc_wq;

	return 0;
out_free_alloc_wq:
	destroy_workqueue(xfs_alloc_wq);
	return -ENOMEM;
}

STATIC void
xfs_destroy_workqueues(void)
{
	destroy_workqueue(xfs_discard_wq);
	destroy_workqueue(xfs_alloc_wq);
}

STATIC int __init
init_xfs_fs(void)
{
	int			error;

	xfs_check_ondisk_structs();

	printk(KERN_INFO XFS_VERSION_STRING " with "
			 XFS_BUILD_OPTIONS " enabled\n");

	xfs_dir_startup();

	error = xfs_init_zones();
	if (error)
		goto out;

	error = xfs_init_workqueues();
	if (error)
		goto out_destroy_zones;

	error = xfs_mru_cache_init();
	if (error)
		goto out_destroy_wq;

	error = xfs_buf_init();
	if (error)
		goto out_mru_cache_uninit;

	error = xfs_init_procfs();
	if (error)
		goto out_buf_terminate;

	error = xfs_sysctl_register();
	if (error)
		goto out_cleanup_procfs;

	xfs_kset = kset_create_and_add("xfs", NULL, fs_kobj);
	if (!xfs_kset) {
		error = -ENOMEM;
		goto out_sysctl_unregister;
	}

	xfsstats.xs_kobj.kobject.kset = xfs_kset;

	xfsstats.xs_stats = alloc_percpu(struct xfsstats);
	if (!xfsstats.xs_stats) {
		error = -ENOMEM;
		goto out_kset_unregister;
	}

	error = xfs_sysfs_init(&xfsstats.xs_kobj, &xfs_stats_ktype, NULL,
			       "stats");
	if (error)
		goto out_free_stats;

#ifdef DEBUG
	xfs_dbg_kobj.kobject.kset = xfs_kset;
	error = xfs_sysfs_init(&xfs_dbg_kobj, &xfs_dbg_ktype, NULL, "debug");
	if (error)
		goto out_remove_stats_kobj;
#endif

	error = xfs_qm_init();
	if (error)
		goto out_remove_dbg_kobj;

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto out_qm_exit;
	return 0;

 out_qm_exit:
	xfs_qm_exit();
 out_remove_dbg_kobj:
#ifdef DEBUG
	xfs_sysfs_del(&xfs_dbg_kobj);
 out_remove_stats_kobj:
#endif
	xfs_sysfs_del(&xfsstats.xs_kobj);
 out_free_stats:
	free_percpu(xfsstats.xs_stats);
 out_kset_unregister:
	kset_unregister(xfs_kset);
 out_sysctl_unregister:
	xfs_sysctl_unregister();
 out_cleanup_procfs:
	xfs_cleanup_procfs();
 out_buf_terminate:
	xfs_buf_terminate();
 out_mru_cache_uninit:
	xfs_mru_cache_uninit();
 out_destroy_wq:
	xfs_destroy_workqueues();
 out_destroy_zones:
	xfs_destroy_zones();
 out:
	return error;
}

STATIC void __exit
exit_xfs_fs(void)
{
	xfs_qm_exit();
	unregister_filesystem(&xfs_fs_type);
#ifdef DEBUG
	xfs_sysfs_del(&xfs_dbg_kobj);
#endif
	xfs_sysfs_del(&xfsstats.xs_kobj);
	free_percpu(xfsstats.xs_stats);
	kset_unregister(xfs_kset);
	xfs_sysctl_unregister();
	xfs_cleanup_procfs();
	xfs_buf_terminate();
	xfs_mru_cache_uninit();
	xfs_destroy_workqueues();
	xfs_destroy_zones();
	xfs_uuid_table_free();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
