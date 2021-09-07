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
#include "xfs_pwork.h"
#include "xfs_ag.h"

#include <linux/magic.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

static const struct super_operations xfs_super_operations;

static struct kset *xfs_kset;		/* top-level xfs sysfs dir */
#ifdef DEBUG
static struct xfs_kobj xfs_dbg_kobj;	/* global debug sysfs attrs */
#endif

#ifdef CONFIG_HOTPLUG_CPU
static LIST_HEAD(xfs_mount_list);
static DEFINE_SPINLOCK(xfs_mount_list_lock);

static inline void xfs_mount_list_add(struct xfs_mount *mp)
{
	spin_lock(&xfs_mount_list_lock);
	list_add(&mp->m_mount_list, &xfs_mount_list);
	spin_unlock(&xfs_mount_list_lock);
}

static inline void xfs_mount_list_del(struct xfs_mount *mp)
{
	spin_lock(&xfs_mount_list_lock);
	list_del(&mp->m_mount_list);
	spin_unlock(&xfs_mount_list_lock);
}
#else /* !CONFIG_HOTPLUG_CPU */
static inline void xfs_mount_list_add(struct xfs_mount *mp) {}
static inline void xfs_mount_list_del(struct xfs_mount *mp) {}
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
		mp->m_features &= ~(XFS_FEAT_DAX_ALWAYS | XFS_FEAT_DAX_NEVER);
		break;
	case XFS_DAX_ALWAYS:
		mp->m_features |= XFS_FEAT_DAX_ALWAYS;
		mp->m_features &= ~XFS_FEAT_DAX_NEVER;
		break;
	case XFS_DAX_NEVER:
		mp->m_features |= XFS_FEAT_DAX_NEVER;
		mp->m_features &= ~XFS_FEAT_DAX_ALWAYS;
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
		{ XFS_FEAT_IKEEP,		",ikeep" },
		{ XFS_FEAT_WSYNC,		",wsync" },
		{ XFS_FEAT_NOALIGN,		",noalign" },
		{ XFS_FEAT_SWALLOC,		",swalloc" },
		{ XFS_FEAT_NOUUID,		",nouuid" },
		{ XFS_FEAT_NORECOVERY,		",norecovery" },
		{ XFS_FEAT_ATTR2,		",attr2" },
		{ XFS_FEAT_FILESTREAMS,		",filestreams" },
		{ XFS_FEAT_GRPID,		",grpid" },
		{ XFS_FEAT_DISCARD,		",discard" },
		{ XFS_FEAT_LARGE_IOSIZE,	",largeio" },
		{ XFS_FEAT_DAX_ALWAYS,		",dax=always" },
		{ XFS_FEAT_DAX_NEVER,		",dax=never" },
		{ 0, NULL }
	};
	struct xfs_mount	*mp = XFS_M(root->d_sb);
	struct proc_xfs_info	*xfs_infop;

	for (xfs_infop = xfs_info_set; xfs_infop->flag; xfs_infop++) {
		if (mp->m_features & xfs_infop->flag)
			seq_puts(m, xfs_infop->str);
	}

	seq_printf(m, ",inode%d", xfs_has_small_inums(mp) ? 32 : 64);

	if (xfs_has_allocsize(mp))
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

	if (mp->m_qflags & XFS_UQUOTA_ENFD)
		seq_puts(m, ",usrquota");
	else if (mp->m_qflags & XFS_UQUOTA_ACCT)
		seq_puts(m, ",uqnoenforce");

	if (mp->m_qflags & XFS_PQUOTA_ENFD)
		seq_puts(m, ",prjquota");
	else if (mp->m_qflags & XFS_PQUOTA_ACCT)
		seq_puts(m, ",pqnoenforce");

	if (mp->m_qflags & XFS_GQUOTA_ENFD)
		seq_puts(m, ",grpquota");
	else if (mp->m_qflags & XFS_GQUOTA_ACCT)
		seq_puts(m, ",gqnoenforce");

	if (!(mp->m_qflags & XFS_ALL_QUOTA_ACCT))
		seq_puts(m, ",noquota");

	return 0;
}

/*
 * Set parameters for inode allocation heuristics, taking into account
 * filesystem size and inode32/inode64 mount options; i.e. specifically
 * whether or not XFS_FEAT_SMALL_INUMS is set.
 *
 * Inode allocation patterns are altered only if inode32 is requested
 * (XFS_FEAT_SMALL_INUMS), and the filesystem is sufficiently large.
 * If altered, XFS_OPSTATE_INODE32 is set as well.
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
	 * sufficiently large, set XFS_OPSTATE_INODE32 if we must alter
	 * the allocator to accommodate the request.
	 */
	if (xfs_has_small_inums(mp) && ino > XFS_MAXINUMBER_32)
		set_bit(XFS_OPSTATE_INODE32, &mp->m_opstate);
	else
		clear_bit(XFS_OPSTATE_INODE32, &mp->m_opstate);

	for (index = 0; index < agcount; index++) {
		struct xfs_perag	*pag;

		ino = XFS_AGINO_TO_INO(mp, index, agino);

		pag = xfs_perag_get(mp, index);

		if (xfs_is_inode32(mp)) {
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

	return xfs_is_inode32(mp) ? maxagi : agcount;
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

		if (xfs_has_sector(mp))
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
			XFS_WQFLAGS(WQ_FREEZABLE | WQ_MEM_RECLAIM),
			1, mp->m_super->s_id);
	if (!mp->m_buf_workqueue)
		goto out;

	mp->m_unwritten_workqueue = alloc_workqueue("xfs-conv/%s",
			XFS_WQFLAGS(WQ_FREEZABLE | WQ_MEM_RECLAIM),
			0, mp->m_super->s_id);
	if (!mp->m_unwritten_workqueue)
		goto out_destroy_buf;

	mp->m_reclaim_workqueue = alloc_workqueue("xfs-reclaim/%s",
			XFS_WQFLAGS(WQ_FREEZABLE | WQ_MEM_RECLAIM),
			0, mp->m_super->s_id);
	if (!mp->m_reclaim_workqueue)
		goto out_destroy_unwritten;

	mp->m_blockgc_wq = alloc_workqueue("xfs-blockgc/%s",
			XFS_WQFLAGS(WQ_UNBOUND | WQ_FREEZABLE | WQ_MEM_RECLAIM),
			0, mp->m_super->s_id);
	if (!mp->m_blockgc_wq)
		goto out_destroy_reclaim;

	mp->m_inodegc_wq = alloc_workqueue("xfs-inodegc/%s",
			XFS_WQFLAGS(WQ_FREEZABLE | WQ_MEM_RECLAIM),
			1, mp->m_super->s_id);
	if (!mp->m_inodegc_wq)
		goto out_destroy_blockgc;

	mp->m_sync_workqueue = alloc_workqueue("xfs-sync/%s",
			XFS_WQFLAGS(WQ_FREEZABLE), 0, mp->m_super->s_id);
	if (!mp->m_sync_workqueue)
		goto out_destroy_inodegc;

	return 0;

out_destroy_inodegc:
	destroy_workqueue(mp->m_inodegc_wq);
out_destroy_blockgc:
	destroy_workqueue(mp->m_blockgc_wq);
out_destroy_reclaim:
	destroy_workqueue(mp->m_reclaim_workqueue);
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
	destroy_workqueue(mp->m_blockgc_wq);
	destroy_workqueue(mp->m_inodegc_wq);
	destroy_workqueue(mp->m_reclaim_workqueue);
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
	xfs_inode_mark_reclaimable(ip);
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
		ASSERT(xlog_recovery_needed(ip->i_mount->m_log));
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

	trace_xfs_fs_sync_fs(mp, __return_address);

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

	/*
	 * If we are called with page faults frozen out, it means we are about
	 * to freeze the transaction subsystem. Take the opportunity to shut
	 * down inodegc because once SB_FREEZE_FS is set it's too late to
	 * prevent inactivation races with freeze. The fs doesn't get called
	 * again by the freezing process until after SB_FREEZE_FS has been set,
	 * so it's now or never.  Same logic applies to speculative allocation
	 * garbage collection.
	 *
	 * We don't care if this is a normal syncfs call that does this or
	 * freeze that does this - we can run this multiple times without issue
	 * and we won't race with a restart because a restart can only occur
	 * when the state is either SB_FREEZE_FS or SB_FREEZE_COMPLETE.
	 */
	if (sb->s_writers.frozen == SB_FREEZE_PAGEFAULT) {
		xfs_inodegc_stop(mp);
		xfs_blockgc_stop(mp);
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

	/* Wait for whatever inactivations are in progress. */
	xfs_inodegc_flush(mp);

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


	if ((ip->i_diflags & XFS_DIFLAG_PROJINHERIT) &&
	    ((mp->m_qflags & (XFS_PQUOTA_ACCT|XFS_PQUOTA_ENFD))) ==
			      (XFS_PQUOTA_ACCT|XFS_PQUOTA_ENFD))
		xfs_qm_statvfs(ip, statp);

	if (XFS_IS_REALTIME_MOUNT(mp) &&
	    (ip->i_diflags & (XFS_DIFLAG_RTINHERIT | XFS_DIFLAG_REALTIME))) {
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
	xfs_save_resvblks(mp);
	ret = xfs_log_quiesce(mp);
	memalloc_nofs_restore(flags);

	/*
	 * For read-write filesystems, we need to restart the inodegc on error
	 * because we stopped it at SB_FREEZE_PAGEFAULT level and a thaw is not
	 * going to be run to restart it now.  We are at SB_FREEZE_FS level
	 * here, so we can restart safely without racing with a stop in
	 * xfs_fs_sync_fs().
	 */
	if (ret && !xfs_is_readonly(mp)) {
		xfs_blockgc_start(mp);
		xfs_inodegc_start(mp);
	}

	return ret;
}

STATIC int
xfs_fs_unfreeze(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_restore_resvblks(mp);
	xfs_log_work_queue(mp);

	/*
	 * Don't reactivate the inodegc worker on a readonly filesystem because
	 * inodes are sent directly to reclaim.  Don't reactivate the blockgc
	 * worker because there are no speculative preallocations on a readonly
	 * filesystem.
	 */
	if (!xfs_is_readonly(mp)) {
		xfs_blockgc_start(mp);
		xfs_inodegc_start(mp);
	}

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
	/* Fail a mount where the logbuf is smaller than the log stripe */
	if (xfs_has_logv2(mp)) {
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
	if (xfs_has_crc(mp) && xfs_has_noattr2(mp)) {
		xfs_warn(mp, "Cannot mount a V5 filesystem as noattr2. "
			     "attr2 is always enabled for V5 filesystems.");
		return -EINVAL;
	}

	/*
	 * prohibit r/w mounts of read-only filesystems
	 */
	if ((mp->m_sb.sb_flags & XFS_SBF_READONLY) && !xfs_is_readonly(mp)) {
		xfs_warn(mp,
			"cannot mount a read-only filesystem as read-write");
		return -EROFS;
	}

	if ((mp->m_qflags & XFS_GQUOTA_ACCT) &&
	    (mp->m_qflags & XFS_PQUOTA_ACCT) &&
	    !xfs_has_pquotino(mp)) {
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
	ASSERT(xfs_is_shutdown(mp) ||
	       percpu_counter_sum(&mp->m_delalloc_blks) == 0);
	percpu_counter_destroy(&mp->m_delalloc_blks);
}

static int
xfs_inodegc_init_percpu(
	struct xfs_mount	*mp)
{
	struct xfs_inodegc	*gc;
	int			cpu;

	mp->m_inodegc = alloc_percpu(struct xfs_inodegc);
	if (!mp->m_inodegc)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		gc = per_cpu_ptr(mp->m_inodegc, cpu);
		init_llist_head(&gc->list);
		gc->items = 0;
		INIT_WORK(&gc->work, xfs_inodegc_worker);
	}
	return 0;
}

static void
xfs_inodegc_free_percpu(
	struct xfs_mount	*mp)
{
	if (!mp->m_inodegc)
		return;
	free_percpu(mp->m_inodegc);
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
	xfs_mount_list_del(mp);
	xfs_inodegc_free_percpu(mp);
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
            !!(XFS_M(fc->root->d_sb)->m_features & flag) == value)
		return;
	xfs_warn(fc->s_fs_info, "%s mount option is deprecated.", param->key);
}

/*
 * Set mount state from a mount option.
 *
 * NOTE: mp->m_super is NULL here!
 */
static int
xfs_fs_parse_param(
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
		parsing_mp->m_features |= XFS_FEAT_ALLOCSIZE;
		return 0;
	case Opt_grpid:
	case Opt_bsdgroups:
		parsing_mp->m_features |= XFS_FEAT_GRPID;
		return 0;
	case Opt_nogrpid:
	case Opt_sysvgroups:
		parsing_mp->m_features &= ~XFS_FEAT_GRPID;
		return 0;
	case Opt_wsync:
		parsing_mp->m_features |= XFS_FEAT_WSYNC;
		return 0;
	case Opt_norecovery:
		parsing_mp->m_features |= XFS_FEAT_NORECOVERY;
		return 0;
	case Opt_noalign:
		parsing_mp->m_features |= XFS_FEAT_NOALIGN;
		return 0;
	case Opt_swalloc:
		parsing_mp->m_features |= XFS_FEAT_SWALLOC;
		return 0;
	case Opt_sunit:
		parsing_mp->m_dalign = result.uint_32;
		return 0;
	case Opt_swidth:
		parsing_mp->m_swidth = result.uint_32;
		return 0;
	case Opt_inode32:
		parsing_mp->m_features |= XFS_FEAT_SMALL_INUMS;
		return 0;
	case Opt_inode64:
		parsing_mp->m_features &= ~XFS_FEAT_SMALL_INUMS;
		return 0;
	case Opt_nouuid:
		parsing_mp->m_features |= XFS_FEAT_NOUUID;
		return 0;
	case Opt_largeio:
		parsing_mp->m_features |= XFS_FEAT_LARGE_IOSIZE;
		return 0;
	case Opt_nolargeio:
		parsing_mp->m_features &= ~XFS_FEAT_LARGE_IOSIZE;
		return 0;
	case Opt_filestreams:
		parsing_mp->m_features |= XFS_FEAT_FILESTREAMS;
		return 0;
	case Opt_noquota:
		parsing_mp->m_qflags &= ~XFS_ALL_QUOTA_ACCT;
		parsing_mp->m_qflags &= ~XFS_ALL_QUOTA_ENFD;
		return 0;
	case Opt_quota:
	case Opt_uquota:
	case Opt_usrquota:
		parsing_mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ENFD);
		return 0;
	case Opt_qnoenforce:
	case Opt_uqnoenforce:
		parsing_mp->m_qflags |= XFS_UQUOTA_ACCT;
		parsing_mp->m_qflags &= ~XFS_UQUOTA_ENFD;
		return 0;
	case Opt_pquota:
	case Opt_prjquota:
		parsing_mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ENFD);
		return 0;
	case Opt_pqnoenforce:
		parsing_mp->m_qflags |= XFS_PQUOTA_ACCT;
		parsing_mp->m_qflags &= ~XFS_PQUOTA_ENFD;
		return 0;
	case Opt_gquota:
	case Opt_grpquota:
		parsing_mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ENFD);
		return 0;
	case Opt_gqnoenforce:
		parsing_mp->m_qflags |= XFS_GQUOTA_ACCT;
		parsing_mp->m_qflags &= ~XFS_GQUOTA_ENFD;
		return 0;
	case Opt_discard:
		parsing_mp->m_features |= XFS_FEAT_DISCARD;
		return 0;
	case Opt_nodiscard:
		parsing_mp->m_features &= ~XFS_FEAT_DISCARD;
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
		xfs_fs_warn_deprecated(fc, param, XFS_FEAT_IKEEP, true);
		parsing_mp->m_features |= XFS_FEAT_IKEEP;
		return 0;
	case Opt_noikeep:
		xfs_fs_warn_deprecated(fc, param, XFS_FEAT_IKEEP, false);
		parsing_mp->m_features &= ~XFS_FEAT_IKEEP;
		return 0;
	case Opt_attr2:
		xfs_fs_warn_deprecated(fc, param, XFS_FEAT_ATTR2, true);
		parsing_mp->m_features |= XFS_FEAT_ATTR2;
		return 0;
	case Opt_noattr2:
		xfs_fs_warn_deprecated(fc, param, XFS_FEAT_NOATTR2, true);
		parsing_mp->m_features |= XFS_FEAT_NOATTR2;
		return 0;
	default:
		xfs_warn(parsing_mp, "unknown mount option [%s].", param->key);
		return -EINVAL;
	}

	return 0;
}

static int
xfs_fs_validate_params(
	struct xfs_mount	*mp)
{
	/* No recovery flag requires a read-only mount */
	if (xfs_has_norecovery(mp) && !xfs_is_readonly(mp)) {
		xfs_warn(mp, "no-recovery mounts must be read-only.");
		return -EINVAL;
	}

	/*
	 * We have not read the superblock at this point, so only the attr2
	 * mount option can set the attr2 feature by this stage.
	 */
	if (xfs_has_attr2(mp) && xfs_has_noattr2(mp)) {
		xfs_warn(mp, "attr2 and noattr2 cannot both be specified.");
		return -EINVAL;
	}


	if (xfs_has_noalign(mp) && (mp->m_dalign || mp->m_swidth)) {
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

	if (xfs_has_allocsize(mp) &&
	    (mp->m_allocsize_log > XFS_MAX_IO_LOG ||
	     mp->m_allocsize_log < XFS_MIN_IO_LOG)) {
		xfs_warn(mp, "invalid log iosize: %d [not %d-%d]",
			mp->m_allocsize_log, XFS_MIN_IO_LOG, XFS_MAX_IO_LOG);
		return -EINVAL;
	}

	return 0;
}

static int
xfs_fs_fill_super(
	struct super_block	*sb,
	struct fs_context	*fc)
{
	struct xfs_mount	*mp = sb->s_fs_info;
	struct inode		*root;
	int			flags = 0, error;

	mp->m_super = sb;

	error = xfs_fs_validate_params(mp);
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

	error = xfs_inodegc_init_percpu(mp);
	if (error)
		goto out_destroy_counters;

	/*
	 * All percpu data structures requiring cleanup when a cpu goes offline
	 * must be allocated before adding this @mp to the cpu-dead handler's
	 * mount list.
	 */
	xfs_mount_list_add(mp);

	/* Allocate stats memory before we do operations that might use it */
	mp->m_stats.xs_stats = alloc_percpu(struct xfsstats);
	if (!mp->m_stats.xs_stats) {
		error = -ENOMEM;
		goto out_destroy_inodegc;
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
	if (!xfs_has_crc(mp)) {
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

	/* Filesystem claims it needs repair, so refuse the mount. */
	if (xfs_has_needsrepair(mp)) {
		xfs_warn(mp, "Filesystem needs repair.  Please run xfs_repair.");
		error = -EFSCORRUPTED;
		goto out_free_sb;
	}

	/*
	 * Don't touch the filesystem if a user tool thinks it owns the primary
	 * superblock.  mkfs doesn't clear the flag from secondary supers, so
	 * we don't check them at all.
	 */
	if (mp->m_sb.sb_inprogress) {
		xfs_warn(mp, "Offline file system operation in progress!");
		error = -EFSCORRUPTED;
		goto out_free_sb;
	}

	/*
	 * Until this is fixed only page-sized or smaller data blocks work.
	 */
	if (mp->m_sb.sb_blocksize > PAGE_SIZE) {
		xfs_warn(mp,
		"File system with blocksize %d bytes. "
		"Only pagesize (%ld) or less will currently work.",
				mp->m_sb.sb_blocksize, PAGE_SIZE);
		error = -ENOSYS;
		goto out_free_sb;
	}

	/* Ensure this filesystem fits in the page cache limits */
	if (xfs_sb_validate_fsb_count(&mp->m_sb, mp->m_sb.sb_dblocks) ||
	    xfs_sb_validate_fsb_count(&mp->m_sb, mp->m_sb.sb_rblocks)) {
		xfs_warn(mp,
		"file system too large to be mounted on this system.");
		error = -EFBIG;
		goto out_free_sb;
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
	if (!xfs_verify_fileoff(mp, XFS_B_TO_FSBT(mp, MAX_LFS_FILESIZE))) {
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
	if (xfs_has_bigtime(mp)) {
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
	if (xfs_has_crc(mp))
		sb->s_flags |= SB_I_VERSION;

	if (xfs_has_dax_always(mp)) {
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
		if (xfs_has_reflink(mp)) {
			xfs_alert(mp,
		"DAX and reflink cannot be used together!");
			error = -EINVAL;
			goto out_filestream_unmount;
		}
	}

	if (xfs_has_discard(mp)) {
		struct request_queue *q = bdev_get_queue(sb->s_bdev);

		if (!blk_queue_discard(q)) {
			xfs_warn(mp, "mounting with \"discard\" option, but "
					"the device does not support discard");
			mp->m_features &= ~XFS_FEAT_DISCARD;
		}
	}

	if (xfs_has_reflink(mp)) {
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

	if (xfs_has_rmapbt(mp) && mp->m_sb.sb_rblocks) {
		xfs_alert(mp,
	"reverse mapping btree not compatible with realtime device!");
		error = -EINVAL;
		goto out_filestream_unmount;
	}

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
 out_destroy_inodegc:
	xfs_mount_list_del(mp);
	xfs_inodegc_free_percpu(mp);
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
xfs_fs_get_tree(
	struct fs_context	*fc)
{
	return get_tree_bdev(fc, xfs_fs_fill_super);
}

static int
xfs_remount_rw(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	int error;

	if (xfs_has_norecovery(mp)) {
		xfs_warn(mp,
			"ro->rw transition prohibited on norecovery mount");
		return -EINVAL;
	}

	if (xfs_sb_is_v5(sbp) &&
	    xfs_sb_has_ro_compat_feature(sbp, XFS_SB_FEAT_RO_COMPAT_UNKNOWN)) {
		xfs_warn(mp,
	"ro->rw transition prohibited on unknown (0x%x) ro-compat filesystem",
			(sbp->sb_features_ro_compat &
				XFS_SB_FEAT_RO_COMPAT_UNKNOWN));
		return -EINVAL;
	}

	clear_bit(XFS_OPSTATE_READONLY, &mp->m_opstate);

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
	xfs_blockgc_start(mp);

	/* Create the per-AG metadata reservation pool .*/
	error = xfs_fs_reserve_ag_blocks(mp);
	if (error && error != -ENOSPC)
		return error;

	/* Re-enable the background inode inactivation worker. */
	xfs_inodegc_start(mp);

	return 0;
}

static int
xfs_remount_ro(
	struct xfs_mount	*mp)
{
	int error;

	/*
	 * Cancel background eofb scanning so it cannot race with the final
	 * log force+buftarg wait and deadlock the remount.
	 */
	xfs_blockgc_stop(mp);

	/* Get rid of any leftover CoW reservations... */
	error = xfs_blockgc_free_space(mp, NULL);
	if (error) {
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return error;
	}

	/*
	 * Stop the inodegc background worker.  xfs_fs_reconfigure already
	 * flushed all pending inodegc work when it sync'd the filesystem.
	 * The VFS holds s_umount, so we know that inodes cannot enter
	 * xfs_fs_destroy_inode during a remount operation.  In readonly mode
	 * we send inodes straight to reclaim, so no inodes will be queued.
	 */
	xfs_inodegc_stop(mp);

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

	xfs_log_clean(mp);
	set_bit(XFS_OPSTATE_READONLY, &mp->m_opstate);

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
xfs_fs_reconfigure(
	struct fs_context *fc)
{
	struct xfs_mount	*mp = XFS_M(fc->root->d_sb);
	struct xfs_mount        *new_mp = fc->s_fs_info;
	int			flags = fc->sb_flags;
	int			error;

	/* version 5 superblocks always support version counters. */
	if (xfs_has_crc(mp))
		fc->sb_flags |= SB_I_VERSION;

	error = xfs_fs_validate_params(new_mp);
	if (error)
		return error;

	sync_filesystem(mp->m_super);

	/* inode32 -> inode64 */
	if (xfs_has_small_inums(mp) && !xfs_has_small_inums(new_mp)) {
		mp->m_features &= ~XFS_FEAT_SMALL_INUMS;
		mp->m_maxagi = xfs_set_inode_alloc(mp, mp->m_sb.sb_agcount);
	}

	/* inode64 -> inode32 */
	if (!xfs_has_small_inums(mp) && xfs_has_small_inums(new_mp)) {
		mp->m_features |= XFS_FEAT_SMALL_INUMS;
		mp->m_maxagi = xfs_set_inode_alloc(mp, mp->m_sb.sb_agcount);
	}

	/* ro -> rw */
	if (xfs_is_readonly(mp) && !(flags & SB_RDONLY)) {
		error = xfs_remount_rw(mp);
		if (error)
			return error;
	}

	/* rw -> ro */
	if (!xfs_is_readonly(mp) && (flags & SB_RDONLY)) {
		error = xfs_remount_ro(mp);
		if (error)
			return error;
	}

	return 0;
}

static void xfs_fs_free(
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
	.parse_param = xfs_fs_parse_param,
	.get_tree    = xfs_fs_get_tree,
	.reconfigure = xfs_fs_reconfigure,
	.free        = xfs_fs_free,
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
		set_bit(XFS_OPSTATE_READONLY, &mp->m_opstate);
	if (fc->sb_flags & SB_DIRSYNC)
		mp->m_features |= XFS_FEAT_DIRSYNC;
	if (fc->sb_flags & SB_SYNCHRONOUS)
		mp->m_features |= XFS_FEAT_WSYNC;

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
	.fs_flags		= FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
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

	xfs_trans_zone = kmem_cache_create("xfs_trans",
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
			XFS_WQFLAGS(WQ_MEM_RECLAIM | WQ_FREEZABLE), 0);
	if (!xfs_alloc_wq)
		return -ENOMEM;

	xfs_discard_wq = alloc_workqueue("xfsdiscard", XFS_WQFLAGS(WQ_UNBOUND),
			0);
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

#ifdef CONFIG_HOTPLUG_CPU
static int
xfs_cpu_dead(
	unsigned int		cpu)
{
	struct xfs_mount	*mp, *n;

	spin_lock(&xfs_mount_list_lock);
	list_for_each_entry_safe(mp, n, &xfs_mount_list, m_mount_list) {
		spin_unlock(&xfs_mount_list_lock);
		xfs_inodegc_cpu_dead(mp, cpu);
		spin_lock(&xfs_mount_list_lock);
	}
	spin_unlock(&xfs_mount_list_lock);
	return 0;
}

static int __init
xfs_cpu_hotplug_init(void)
{
	int	error;

	error = cpuhp_setup_state_nocalls(CPUHP_XFS_DEAD, "xfs:dead", NULL,
			xfs_cpu_dead);
	if (error < 0)
		xfs_alert(NULL,
"Failed to initialise CPU hotplug, error %d. XFS is non-functional.",
			error);
	return error;
}

static void
xfs_cpu_hotplug_destroy(void)
{
	cpuhp_remove_state_nocalls(CPUHP_XFS_DEAD);
}

#else /* !CONFIG_HOTPLUG_CPU */
static inline int xfs_cpu_hotplug_init(void) { return 0; }
static inline void xfs_cpu_hotplug_destroy(void) {}
#endif

STATIC int __init
init_xfs_fs(void)
{
	int			error;

	xfs_check_ondisk_structs();

	printk(KERN_INFO XFS_VERSION_STRING " with "
			 XFS_BUILD_OPTIONS " enabled\n");

	xfs_dir_startup();

	error = xfs_cpu_hotplug_init();
	if (error)
		goto out;

	error = xfs_init_zones();
	if (error)
		goto out_destroy_hp;

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
 out_destroy_hp:
	xfs_cpu_hotplug_destroy();
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
	xfs_cpu_hotplug_destroy();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
