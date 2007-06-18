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
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_quota.h"
#include "xfs_fsops.h"

STATIC void	xfs_mount_log_sbunit(xfs_mount_t *, __int64_t);
STATIC int	xfs_uuid_mount(xfs_mount_t *);
STATIC void	xfs_uuid_unmount(xfs_mount_t *mp);
STATIC void	xfs_unmountfs_wait(xfs_mount_t *);


#ifdef HAVE_PERCPU_SB
STATIC void	xfs_icsb_destroy_counters(xfs_mount_t *);
STATIC void	xfs_icsb_balance_counter(xfs_mount_t *, xfs_sb_field_t,
						int, int);
STATIC void	xfs_icsb_sync_counters(xfs_mount_t *);
STATIC int	xfs_icsb_modify_counters(xfs_mount_t *, xfs_sb_field_t,
						int64_t, int);
STATIC int	xfs_icsb_disable_counter(xfs_mount_t *, xfs_sb_field_t);

#else

#define xfs_icsb_destroy_counters(mp)			do { } while (0)
#define xfs_icsb_balance_counter(mp, a, b, c)		do { } while (0)
#define xfs_icsb_sync_counters(mp)			do { } while (0)
#define xfs_icsb_modify_counters(mp, a, b, c)		do { } while (0)

#endif

static const struct {
	short offset;
	short type;	/* 0 = integer
			 * 1 = binary / string (no translation)
			 */
} xfs_sb_info[] = {
    { offsetof(xfs_sb_t, sb_magicnum),   0 },
    { offsetof(xfs_sb_t, sb_blocksize),  0 },
    { offsetof(xfs_sb_t, sb_dblocks),    0 },
    { offsetof(xfs_sb_t, sb_rblocks),    0 },
    { offsetof(xfs_sb_t, sb_rextents),   0 },
    { offsetof(xfs_sb_t, sb_uuid),       1 },
    { offsetof(xfs_sb_t, sb_logstart),   0 },
    { offsetof(xfs_sb_t, sb_rootino),    0 },
    { offsetof(xfs_sb_t, sb_rbmino),     0 },
    { offsetof(xfs_sb_t, sb_rsumino),    0 },
    { offsetof(xfs_sb_t, sb_rextsize),   0 },
    { offsetof(xfs_sb_t, sb_agblocks),   0 },
    { offsetof(xfs_sb_t, sb_agcount),    0 },
    { offsetof(xfs_sb_t, sb_rbmblocks),  0 },
    { offsetof(xfs_sb_t, sb_logblocks),  0 },
    { offsetof(xfs_sb_t, sb_versionnum), 0 },
    { offsetof(xfs_sb_t, sb_sectsize),   0 },
    { offsetof(xfs_sb_t, sb_inodesize),  0 },
    { offsetof(xfs_sb_t, sb_inopblock),  0 },
    { offsetof(xfs_sb_t, sb_fname[0]),   1 },
    { offsetof(xfs_sb_t, sb_blocklog),   0 },
    { offsetof(xfs_sb_t, sb_sectlog),    0 },
    { offsetof(xfs_sb_t, sb_inodelog),   0 },
    { offsetof(xfs_sb_t, sb_inopblog),   0 },
    { offsetof(xfs_sb_t, sb_agblklog),   0 },
    { offsetof(xfs_sb_t, sb_rextslog),   0 },
    { offsetof(xfs_sb_t, sb_inprogress), 0 },
    { offsetof(xfs_sb_t, sb_imax_pct),   0 },
    { offsetof(xfs_sb_t, sb_icount),     0 },
    { offsetof(xfs_sb_t, sb_ifree),      0 },
    { offsetof(xfs_sb_t, sb_fdblocks),   0 },
    { offsetof(xfs_sb_t, sb_frextents),  0 },
    { offsetof(xfs_sb_t, sb_uquotino),   0 },
    { offsetof(xfs_sb_t, sb_gquotino),   0 },
    { offsetof(xfs_sb_t, sb_qflags),     0 },
    { offsetof(xfs_sb_t, sb_flags),      0 },
    { offsetof(xfs_sb_t, sb_shared_vn),  0 },
    { offsetof(xfs_sb_t, sb_inoalignmt), 0 },
    { offsetof(xfs_sb_t, sb_unit),	 0 },
    { offsetof(xfs_sb_t, sb_width),	 0 },
    { offsetof(xfs_sb_t, sb_dirblklog),	 0 },
    { offsetof(xfs_sb_t, sb_logsectlog), 0 },
    { offsetof(xfs_sb_t, sb_logsectsize),0 },
    { offsetof(xfs_sb_t, sb_logsunit),	 0 },
    { offsetof(xfs_sb_t, sb_features2),	 0 },
    { sizeof(xfs_sb_t),			 0 }
};

/*
 * Return a pointer to an initialized xfs_mount structure.
 */
xfs_mount_t *
xfs_mount_init(void)
{
	xfs_mount_t *mp;

	mp = kmem_zalloc(sizeof(xfs_mount_t), KM_SLEEP);

	if (xfs_icsb_init_counters(mp)) {
		mp->m_flags |= XFS_MOUNT_NO_PERCPU_SB;
	}

	AIL_LOCKINIT(&mp->m_ail_lock, "xfs_ail");
	spinlock_init(&mp->m_sb_lock, "xfs_sb");
	mutex_init(&mp->m_ilock);
	initnsema(&mp->m_growlock, 1, "xfs_grow");
	/*
	 * Initialize the AIL.
	 */
	xfs_trans_ail_init(mp);

	atomic_set(&mp->m_active_trans, 0);

	return mp;
}

/*
 * Free up the resources associated with a mount structure.  Assume that
 * the structure was initially zeroed, so we can tell which fields got
 * initialized.
 */
void
xfs_mount_free(
	xfs_mount_t	*mp,
	int		remove_bhv)
{
	if (mp->m_ihash)
		xfs_ihash_free(mp);
	if (mp->m_chash)
		xfs_chash_free(mp);

	if (mp->m_perag) {
		int	agno;

		for (agno = 0; agno < mp->m_maxagi; agno++)
			if (mp->m_perag[agno].pagb_list)
				kmem_free(mp->m_perag[agno].pagb_list,
						sizeof(xfs_perag_busy_t) *
							XFS_PAGB_NUM_SLOTS);
		kmem_free(mp->m_perag,
			  sizeof(xfs_perag_t) * mp->m_sb.sb_agcount);
	}

	AIL_LOCK_DESTROY(&mp->m_ail_lock);
	spinlock_destroy(&mp->m_sb_lock);
	mutex_destroy(&mp->m_ilock);
	freesema(&mp->m_growlock);
	if (mp->m_quotainfo)
		XFS_QM_DONE(mp);

	if (mp->m_fsname != NULL)
		kmem_free(mp->m_fsname, mp->m_fsname_len);
	if (mp->m_rtname != NULL)
		kmem_free(mp->m_rtname, strlen(mp->m_rtname) + 1);
	if (mp->m_logname != NULL)
		kmem_free(mp->m_logname, strlen(mp->m_logname) + 1);

	if (remove_bhv) {
		struct bhv_vfs	*vfsp = XFS_MTOVFS(mp);

		bhv_remove_all_vfsops(vfsp, 0);
		VFS_REMOVEBHV(vfsp, &mp->m_bhv);
	}

	xfs_icsb_destroy_counters(mp);
	kmem_free(mp, sizeof(xfs_mount_t));
}

/*
 * Check size of device based on the (data/realtime) block count.
 * Note: this check is used by the growfs code as well as mount.
 */
int
xfs_sb_validate_fsb_count(
	xfs_sb_t	*sbp,
	__uint64_t	nblocks)
{
	ASSERT(PAGE_SHIFT >= sbp->sb_blocklog);
	ASSERT(sbp->sb_blocklog >= BBSHIFT);

#if XFS_BIG_BLKNOS     /* Limited by ULONG_MAX of page cache index */
	if (nblocks >> (PAGE_CACHE_SHIFT - sbp->sb_blocklog) > ULONG_MAX)
		return E2BIG;
#else                  /* Limited by UINT_MAX of sectors */
	if (nblocks << (sbp->sb_blocklog - BBSHIFT) > UINT_MAX)
		return E2BIG;
#endif
	return 0;
}

/*
 * Check the validity of the SB found.
 */
STATIC int
xfs_mount_validate_sb(
	xfs_mount_t	*mp,
	xfs_sb_t	*sbp,
	int		flags)
{
	/*
	 * If the log device and data device have the
	 * same device number, the log is internal.
	 * Consequently, the sb_logstart should be non-zero.  If
	 * we have a zero sb_logstart in this case, we may be trying to mount
	 * a volume filesystem in a non-volume manner.
	 */
	if (sbp->sb_magicnum != XFS_SB_MAGIC) {
		xfs_fs_mount_cmn_err(flags, "bad magic number");
		return XFS_ERROR(EWRONGFS);
	}

	if (!XFS_SB_GOOD_VERSION(sbp)) {
		xfs_fs_mount_cmn_err(flags, "bad version");
		return XFS_ERROR(EWRONGFS);
	}

	if (unlikely(
	    sbp->sb_logstart == 0 && mp->m_logdev_targp == mp->m_ddev_targp)) {
		xfs_fs_mount_cmn_err(flags,
			"filesystem is marked as having an external log; "
			"specify logdev on the\nmount command line.");
		return XFS_ERROR(EINVAL);
	}

	if (unlikely(
	    sbp->sb_logstart != 0 && mp->m_logdev_targp != mp->m_ddev_targp)) {
		xfs_fs_mount_cmn_err(flags,
			"filesystem is marked as having an internal log; "
			"do not specify logdev on\nthe mount command line.");
		return XFS_ERROR(EINVAL);
	}

	/*
	 * More sanity checking. These were stolen directly from
	 * xfs_repair.
	 */
	if (unlikely(
	    sbp->sb_agcount <= 0					||
	    sbp->sb_sectsize < XFS_MIN_SECTORSIZE			||
	    sbp->sb_sectsize > XFS_MAX_SECTORSIZE			||
	    sbp->sb_sectlog < XFS_MIN_SECTORSIZE_LOG			||
	    sbp->sb_sectlog > XFS_MAX_SECTORSIZE_LOG			||
	    sbp->sb_blocksize < XFS_MIN_BLOCKSIZE			||
	    sbp->sb_blocksize > XFS_MAX_BLOCKSIZE			||
	    sbp->sb_blocklog < XFS_MIN_BLOCKSIZE_LOG			||
	    sbp->sb_blocklog > XFS_MAX_BLOCKSIZE_LOG			||
	    sbp->sb_inodesize < XFS_DINODE_MIN_SIZE			||
	    sbp->sb_inodesize > XFS_DINODE_MAX_SIZE			||
	    sbp->sb_inodelog < XFS_DINODE_MIN_LOG			||
	    sbp->sb_inodelog > XFS_DINODE_MAX_LOG			||
	    (sbp->sb_blocklog - sbp->sb_inodelog != sbp->sb_inopblog)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize > XFS_MAX_RTEXTSIZE)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize < XFS_MIN_RTEXTSIZE)	||
	    (sbp->sb_imax_pct > 100 /* zero sb_imax_pct is valid */))) {
		xfs_fs_mount_cmn_err(flags, "SB sanity check 1 failed");
		return XFS_ERROR(EFSCORRUPTED);
	}

	/*
	 * Sanity check AG count, size fields against data size field
	 */
	if (unlikely(
	    sbp->sb_dblocks == 0 ||
	    sbp->sb_dblocks >
	     (xfs_drfsbno_t)sbp->sb_agcount * sbp->sb_agblocks ||
	    sbp->sb_dblocks < (xfs_drfsbno_t)(sbp->sb_agcount - 1) *
			      sbp->sb_agblocks + XFS_MIN_AG_BLOCKS)) {
		xfs_fs_mount_cmn_err(flags, "SB sanity check 2 failed");
		return XFS_ERROR(EFSCORRUPTED);
	}

	if (xfs_sb_validate_fsb_count(sbp, sbp->sb_dblocks) ||
	    xfs_sb_validate_fsb_count(sbp, sbp->sb_rblocks)) {
		xfs_fs_mount_cmn_err(flags,
			"file system too large to be mounted on this system.");
		return XFS_ERROR(E2BIG);
	}

	if (unlikely(sbp->sb_inprogress)) {
		xfs_fs_mount_cmn_err(flags, "file system busy");
		return XFS_ERROR(EFSCORRUPTED);
	}

	/*
	 * Version 1 directory format has never worked on Linux.
	 */
	if (unlikely(!XFS_SB_VERSION_HASDIRV2(sbp))) {
		xfs_fs_mount_cmn_err(flags,
			"file system using version 1 directory format");
		return XFS_ERROR(ENOSYS);
	}

	/*
	 * Until this is fixed only page-sized or smaller data blocks work.
	 */
	if (unlikely(sbp->sb_blocksize > PAGE_SIZE)) {
		xfs_fs_mount_cmn_err(flags,
			"file system with blocksize %d bytes",
			sbp->sb_blocksize);
		xfs_fs_mount_cmn_err(flags,
			"only pagesize (%ld) or less will currently work.",
			PAGE_SIZE);
		return XFS_ERROR(ENOSYS);
	}

	return 0;
}

xfs_agnumber_t
xfs_initialize_perag(
	bhv_vfs_t	*vfs,
	xfs_mount_t	*mp,
	xfs_agnumber_t	agcount)
{
	xfs_agnumber_t	index, max_metadata;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;
	xfs_ino_t	ino;
	xfs_sb_t	*sbp = &mp->m_sb;
	xfs_ino_t	max_inum = XFS_MAXINUMBER_32;

	/* Check to see if the filesystem can overflow 32 bit inodes */
	agino = XFS_OFFBNO_TO_AGINO(mp, sbp->sb_agblocks - 1, 0);
	ino = XFS_AGINO_TO_INO(mp, agcount - 1, agino);

	/* Clear the mount flag if no inode can overflow 32 bits
	 * on this filesystem, or if specifically requested..
	 */
	if ((vfs->vfs_flag & VFS_32BITINODES) && ino > max_inum) {
		mp->m_flags |= XFS_MOUNT_32BITINODES;
	} else {
		mp->m_flags &= ~XFS_MOUNT_32BITINODES;
	}

	/* If we can overflow then setup the ag headers accordingly */
	if (mp->m_flags & XFS_MOUNT_32BITINODES) {
		/* Calculate how much should be reserved for inodes to
		 * meet the max inode percentage.
		 */
		if (mp->m_maxicount) {
			__uint64_t	icount;

			icount = sbp->sb_dblocks * sbp->sb_imax_pct;
			do_div(icount, 100);
			icount += sbp->sb_agblocks - 1;
			do_div(icount, sbp->sb_agblocks);
			max_metadata = icount;
		} else {
			max_metadata = agcount;
		}
		for (index = 0; index < agcount; index++) {
			ino = XFS_AGINO_TO_INO(mp, index, agino);
			if (ino > max_inum) {
				index++;
				break;
			}

			/* This ag is preferred for inodes */
			pag = &mp->m_perag[index];
			pag->pagi_inodeok = 1;
			if (index < max_metadata)
				pag->pagf_metadata = 1;
		}
	} else {
		/* Setup default behavior for smaller filesystems */
		for (index = 0; index < agcount; index++) {
			pag = &mp->m_perag[index];
			pag->pagi_inodeok = 1;
		}
	}
	return index;
}

/*
 * xfs_xlatesb
 *
 *     data       - on disk version of sb
 *     sb         - a superblock
 *     dir        - conversion direction: <0 - convert sb to buf
 *                                        >0 - convert buf to sb
 *     fields     - which fields to copy (bitmask)
 */
void
xfs_xlatesb(
	void		*data,
	xfs_sb_t	*sb,
	int		dir,
	__int64_t	fields)
{
	xfs_caddr_t	buf_ptr;
	xfs_caddr_t	mem_ptr;
	xfs_sb_field_t	f;
	int		first;
	int		size;

	ASSERT(dir);
	ASSERT(fields);

	if (!fields)
		return;

	buf_ptr = (xfs_caddr_t)data;
	mem_ptr = (xfs_caddr_t)sb;

	while (fields) {
		f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
		first = xfs_sb_info[f].offset;
		size = xfs_sb_info[f + 1].offset - first;

		ASSERT(xfs_sb_info[f].type == 0 || xfs_sb_info[f].type == 1);

		if (size == 1 || xfs_sb_info[f].type == 1) {
			if (dir > 0) {
				memcpy(mem_ptr + first, buf_ptr + first, size);
			} else {
				memcpy(buf_ptr + first, mem_ptr + first, size);
			}
		} else {
			switch (size) {
			case 2:
				INT_XLATE(*(__uint16_t*)(buf_ptr+first),
					  *(__uint16_t*)(mem_ptr+first),
					  dir, ARCH_CONVERT);
				break;
			case 4:
				INT_XLATE(*(__uint32_t*)(buf_ptr+first),
					  *(__uint32_t*)(mem_ptr+first),
					  dir, ARCH_CONVERT);
				break;
			case 8:
				INT_XLATE(*(__uint64_t*)(buf_ptr+first),
					  *(__uint64_t*)(mem_ptr+first), dir, ARCH_CONVERT);
				break;
			default:
				ASSERT(0);
			}
		}

		fields &= ~(1LL << f);
	}
}

/*
 * xfs_readsb
 *
 * Does the initial read of the superblock.
 */
int
xfs_readsb(xfs_mount_t *mp, int flags)
{
	unsigned int	sector_size;
	unsigned int	extra_flags;
	xfs_buf_t	*bp;
	xfs_sb_t	*sbp;
	int		error;

	ASSERT(mp->m_sb_bp == NULL);
	ASSERT(mp->m_ddev_targp != NULL);

	/*
	 * Allocate a (locked) buffer to hold the superblock.
	 * This will be kept around at all times to optimize
	 * access to the superblock.
	 */
	sector_size = xfs_getsize_buftarg(mp->m_ddev_targp);
	extra_flags = XFS_BUF_LOCK | XFS_BUF_MANAGE | XFS_BUF_MAPPED;

	bp = xfs_buf_read_flags(mp->m_ddev_targp, XFS_SB_DADDR,
				BTOBB(sector_size), extra_flags);
	if (!bp || XFS_BUF_ISERROR(bp)) {
		xfs_fs_mount_cmn_err(flags, "SB read failed");
		error = bp ? XFS_BUF_GETERROR(bp) : ENOMEM;
		goto fail;
	}
	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);

	/*
	 * Initialize the mount structure from the superblock.
	 * But first do some basic consistency checking.
	 */
	sbp = XFS_BUF_TO_SBP(bp);
	xfs_xlatesb(XFS_BUF_PTR(bp), &(mp->m_sb), 1, XFS_SB_ALL_BITS);

	error = xfs_mount_validate_sb(mp, &(mp->m_sb), flags);
	if (error) {
		xfs_fs_mount_cmn_err(flags, "SB validate failed");
		goto fail;
	}

	/*
	 * We must be able to do sector-sized and sector-aligned IO.
	 */
	if (sector_size > mp->m_sb.sb_sectsize) {
		xfs_fs_mount_cmn_err(flags,
			"device supports only %u byte sectors (not %u)",
			sector_size, mp->m_sb.sb_sectsize);
		error = ENOSYS;
		goto fail;
	}

	/*
	 * If device sector size is smaller than the superblock size,
	 * re-read the superblock so the buffer is correctly sized.
	 */
	if (sector_size < mp->m_sb.sb_sectsize) {
		XFS_BUF_UNMANAGE(bp);
		xfs_buf_relse(bp);
		sector_size = mp->m_sb.sb_sectsize;
		bp = xfs_buf_read_flags(mp->m_ddev_targp, XFS_SB_DADDR,
					BTOBB(sector_size), extra_flags);
		if (!bp || XFS_BUF_ISERROR(bp)) {
			xfs_fs_mount_cmn_err(flags, "SB re-read failed");
			error = bp ? XFS_BUF_GETERROR(bp) : ENOMEM;
			goto fail;
		}
		ASSERT(XFS_BUF_ISBUSY(bp));
		ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);
	}

	/* Initialize per-cpu counters */
	xfs_icsb_reinit_counters(mp);

	mp->m_sb_bp = bp;
	xfs_buf_relse(bp);
	ASSERT(XFS_BUF_VALUSEMA(bp) > 0);
	return 0;

 fail:
	if (bp) {
		XFS_BUF_UNMANAGE(bp);
		xfs_buf_relse(bp);
	}
	return error;
}


/*
 * xfs_mount_common
 *
 * Mount initialization code establishing various mount
 * fields from the superblock associated with the given
 * mount structure
 */
STATIC void
xfs_mount_common(xfs_mount_t *mp, xfs_sb_t *sbp)
{
	int	i;

	mp->m_agfrotor = mp->m_agirotor = 0;
	spinlock_init(&mp->m_agirotor_lock, "m_agirotor_lock");
	mp->m_maxagi = mp->m_sb.sb_agcount;
	mp->m_blkbit_log = sbp->sb_blocklog + XFS_NBBYLOG;
	mp->m_blkbb_log = sbp->sb_blocklog - BBSHIFT;
	mp->m_sectbb_log = sbp->sb_sectlog - BBSHIFT;
	mp->m_agno_log = xfs_highbit32(sbp->sb_agcount - 1) + 1;
	mp->m_agino_log = sbp->sb_inopblog + sbp->sb_agblklog;
	mp->m_litino = sbp->sb_inodesize -
		((uint)sizeof(xfs_dinode_core_t) + (uint)sizeof(xfs_agino_t));
	mp->m_blockmask = sbp->sb_blocksize - 1;
	mp->m_blockwsize = sbp->sb_blocksize >> XFS_WORDLOG;
	mp->m_blockwmask = mp->m_blockwsize - 1;
	INIT_LIST_HEAD(&mp->m_del_inodes);

	/*
	 * Setup for attributes, in case they get created.
	 * This value is for inodes getting attributes for the first time,
	 * the per-inode value is for old attribute values.
	 */
	ASSERT(sbp->sb_inodesize >= 256 && sbp->sb_inodesize <= 2048);
	switch (sbp->sb_inodesize) {
	case 256:
		mp->m_attroffset = XFS_LITINO(mp) -
				   XFS_BMDR_SPACE_CALC(MINABTPTRS);
		break;
	case 512:
	case 1024:
	case 2048:
		mp->m_attroffset = XFS_BMDR_SPACE_CALC(6 * MINABTPTRS);
		break;
	default:
		ASSERT(0);
	}
	ASSERT(mp->m_attroffset < XFS_LITINO(mp));

	for (i = 0; i < 2; i++) {
		mp->m_alloc_mxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_alloc, i == 0);
		mp->m_alloc_mnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_alloc, i == 0);
	}
	for (i = 0; i < 2; i++) {
		mp->m_bmap_dmxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_bmbt, i == 0);
		mp->m_bmap_dmnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_bmbt, i == 0);
	}
	for (i = 0; i < 2; i++) {
		mp->m_inobt_mxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_inobt, i == 0);
		mp->m_inobt_mnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_inobt, i == 0);
	}

	mp->m_bsize = XFS_FSB_TO_BB(mp, 1);
	mp->m_ialloc_inos = (int)MAX((__uint16_t)XFS_INODES_PER_CHUNK,
					sbp->sb_inopblock);
	mp->m_ialloc_blks = mp->m_ialloc_inos >> sbp->sb_inopblog;
}

/*
 * xfs_initialize_perag_data
 *
 * Read in each per-ag structure so we can count up the number of
 * allocated inodes, free inodes and used filesystem blocks as this
 * information is no longer persistent in the superblock. Once we have
 * this information, write it into the in-core superblock structure.
 */
STATIC int
xfs_initialize_perag_data(xfs_mount_t *mp, xfs_agnumber_t agcount)
{
	xfs_agnumber_t	index;
	xfs_perag_t	*pag;
	xfs_sb_t	*sbp = &mp->m_sb;
	uint64_t	ifree = 0;
	uint64_t	ialloc = 0;
	uint64_t	bfree = 0;
	uint64_t	bfreelst = 0;
	uint64_t	btree = 0;
	int		error;
	int		s;

	for (index = 0; index < agcount; index++) {
		/*
		 * read the agf, then the agi. This gets us
		 * all the inforamtion we need and populates the
		 * per-ag structures for us.
		 */
		error = xfs_alloc_pagf_init(mp, NULL, index, 0);
		if (error)
			return error;

		error = xfs_ialloc_pagi_init(mp, NULL, index);
		if (error)
			return error;
		pag = &mp->m_perag[index];
		ifree += pag->pagi_freecount;
		ialloc += pag->pagi_count;
		bfree += pag->pagf_freeblks;
		bfreelst += pag->pagf_flcount;
		btree += pag->pagf_btreeblks;
	}
	/*
	 * Overwrite incore superblock counters with just-read data
	 */
	s = XFS_SB_LOCK(mp);
	sbp->sb_ifree = ifree;
	sbp->sb_icount = ialloc;
	sbp->sb_fdblocks = bfree + bfreelst + btree;
	XFS_SB_UNLOCK(mp, s);

	/* Fixup the per-cpu counters as well. */
	xfs_icsb_reinit_counters(mp);

	return 0;
}

/*
 * xfs_mountfs
 *
 * This function does the following on an initial mount of a file system:
 *	- reads the superblock from disk and init the mount struct
 *	- if we're a 32-bit kernel, do a size check on the superblock
 *		so we don't mount terabyte filesystems
 *	- init mount struct realtime fields
 *	- allocate inode hash table for fs
 *	- init directory manager
 *	- perform recovery and init the log manager
 */
int
xfs_mountfs(
	bhv_vfs_t	*vfsp,
	xfs_mount_t	*mp,
	int		mfsi_flags)
{
	xfs_buf_t	*bp;
	xfs_sb_t	*sbp = &(mp->m_sb);
	xfs_inode_t	*rip;
	bhv_vnode_t	*rvp = NULL;
	int		readio_log, writeio_log;
	xfs_daddr_t	d;
	__uint64_t	resblks;
	__int64_t	update_flags;
	uint		quotamount, quotaflags;
	int		agno;
	int		uuid_mounted = 0;
	int		error = 0;

	if (mp->m_sb_bp == NULL) {
		if ((error = xfs_readsb(mp, mfsi_flags))) {
			return error;
		}
	}
	xfs_mount_common(mp, sbp);

	/*
	 * Check if sb_agblocks is aligned at stripe boundary
	 * If sb_agblocks is NOT aligned turn off m_dalign since
	 * allocator alignment is within an ag, therefore ag has
	 * to be aligned at stripe boundary.
	 */
	update_flags = 0LL;
	if (mp->m_dalign && !(mfsi_flags & XFS_MFSI_SECOND)) {
		/*
		 * If stripe unit and stripe width are not multiples
		 * of the fs blocksize turn off alignment.
		 */
		if ((BBTOB(mp->m_dalign) & mp->m_blockmask) ||
		    (BBTOB(mp->m_swidth) & mp->m_blockmask)) {
			if (mp->m_flags & XFS_MOUNT_RETERR) {
				cmn_err(CE_WARN,
					"XFS: alignment check 1 failed");
				error = XFS_ERROR(EINVAL);
				goto error1;
			}
			mp->m_dalign = mp->m_swidth = 0;
		} else {
			/*
			 * Convert the stripe unit and width to FSBs.
			 */
			mp->m_dalign = XFS_BB_TO_FSBT(mp, mp->m_dalign);
			if (mp->m_dalign && (sbp->sb_agblocks % mp->m_dalign)) {
				if (mp->m_flags & XFS_MOUNT_RETERR) {
					error = XFS_ERROR(EINVAL);
					goto error1;
				}
				xfs_fs_cmn_err(CE_WARN, mp,
"stripe alignment turned off: sunit(%d)/swidth(%d) incompatible with agsize(%d)",
					mp->m_dalign, mp->m_swidth,
					sbp->sb_agblocks);

				mp->m_dalign = 0;
				mp->m_swidth = 0;
			} else if (mp->m_dalign) {
				mp->m_swidth = XFS_BB_TO_FSBT(mp, mp->m_swidth);
			} else {
				if (mp->m_flags & XFS_MOUNT_RETERR) {
					xfs_fs_cmn_err(CE_WARN, mp,
"stripe alignment turned off: sunit(%d) less than bsize(%d)",
                                        	mp->m_dalign,
						mp->m_blockmask +1);
					error = XFS_ERROR(EINVAL);
					goto error1;
				}
				mp->m_swidth = 0;
			}
		}

		/*
		 * Update superblock with new values
		 * and log changes
		 */
		if (XFS_SB_VERSION_HASDALIGN(sbp)) {
			if (sbp->sb_unit != mp->m_dalign) {
				sbp->sb_unit = mp->m_dalign;
				update_flags |= XFS_SB_UNIT;
			}
			if (sbp->sb_width != mp->m_swidth) {
				sbp->sb_width = mp->m_swidth;
				update_flags |= XFS_SB_WIDTH;
			}
		}
	} else if ((mp->m_flags & XFS_MOUNT_NOALIGN) != XFS_MOUNT_NOALIGN &&
		    XFS_SB_VERSION_HASDALIGN(&mp->m_sb)) {
			mp->m_dalign = sbp->sb_unit;
			mp->m_swidth = sbp->sb_width;
	}

	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp, XFS_DATA_FORK);
	xfs_bmap_compute_maxlevels(mp, XFS_ATTR_FORK);
	xfs_ialloc_compute_maxlevels(mp);

	if (sbp->sb_imax_pct) {
		__uint64_t	icount;

		/* Make sure the maximum inode count is a multiple of the
		 * units we allocate inodes in.
		 */

		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		do_div(icount, mp->m_ialloc_blks);
		mp->m_maxicount = (icount * mp->m_ialloc_blks)  <<
				   sbp->sb_inopblog;
	} else
		mp->m_maxicount = 0;

	mp->m_maxioffset = xfs_max_file_offset(sbp->sb_blocklog);

	/*
	 * XFS uses the uuid from the superblock as the unique
	 * identifier for fsid.  We can not use the uuid from the volume
	 * since a single partition filesystem is identical to a single
	 * partition volume/filesystem.
	 */
	if ((mfsi_flags & XFS_MFSI_SECOND) == 0 &&
	    (mp->m_flags & XFS_MOUNT_NOUUID) == 0) {
		__uint64_t	ret64;
		if (xfs_uuid_mount(mp)) {
			error = XFS_ERROR(EINVAL);
			goto error1;
		}
		uuid_mounted=1;
		ret64 = uuid_hash64(&sbp->sb_uuid);
		memcpy(&vfsp->vfs_fsid, &ret64, sizeof(ret64));
	}

	/*
	 * Set the default minimum read and write sizes unless
	 * already specified in a mount option.
	 * We use smaller I/O sizes when the file system
	 * is being used for NFS service (wsync mount option).
	 */
	if (!(mp->m_flags & XFS_MOUNT_DFLT_IOSIZE)) {
		if (mp->m_flags & XFS_MOUNT_WSYNC) {
			readio_log = XFS_WSYNC_READIO_LOG;
			writeio_log = XFS_WSYNC_WRITEIO_LOG;
		} else {
			readio_log = XFS_READIO_LOG_LARGE;
			writeio_log = XFS_WRITEIO_LOG_LARGE;
		}
	} else {
		readio_log = mp->m_readio_log;
		writeio_log = mp->m_writeio_log;
	}

	/*
	 * Set the number of readahead buffers to use based on
	 * physical memory size.
	 */
	if (xfs_physmem <= 4096)		/* <= 16MB */
		mp->m_nreadaheads = XFS_RW_NREADAHEAD_16MB;
	else if (xfs_physmem <= 8192)	/* <= 32MB */
		mp->m_nreadaheads = XFS_RW_NREADAHEAD_32MB;
	else
		mp->m_nreadaheads = XFS_RW_NREADAHEAD_K32;
	if (sbp->sb_blocklog > readio_log) {
		mp->m_readio_log = sbp->sb_blocklog;
	} else {
		mp->m_readio_log = readio_log;
	}
	mp->m_readio_blocks = 1 << (mp->m_readio_log - sbp->sb_blocklog);
	if (sbp->sb_blocklog > writeio_log) {
		mp->m_writeio_log = sbp->sb_blocklog;
	} else {
		mp->m_writeio_log = writeio_log;
	}
	mp->m_writeio_blocks = 1 << (mp->m_writeio_log - sbp->sb_blocklog);

	/*
	 * Set the inode cluster size based on the physical memory
	 * size.  This may still be overridden by the file system
	 * block size if it is larger than the chosen cluster size.
	 */
	if (xfs_physmem <= btoc(32 * 1024 * 1024)) { /* <= 32 MB */
		mp->m_inode_cluster_size = XFS_INODE_SMALL_CLUSTER_SIZE;
	} else {
		mp->m_inode_cluster_size = XFS_INODE_BIG_CLUSTER_SIZE;
	}
	/*
	 * Set whether we're using inode alignment.
	 */
	if (XFS_SB_VERSION_HASALIGN(&mp->m_sb) &&
	    mp->m_sb.sb_inoalignmt >=
	    XFS_B_TO_FSBT(mp, mp->m_inode_cluster_size))
		mp->m_inoalign_mask = mp->m_sb.sb_inoalignmt - 1;
	else
		mp->m_inoalign_mask = 0;
	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the inode alignment
	 */
	if (mp->m_dalign && mp->m_inoalign_mask &&
	    !(mp->m_dalign & mp->m_inoalign_mask))
		mp->m_sinoalign = mp->m_dalign;
	else
		mp->m_sinoalign = 0;
	/*
	 * Check that the data (and log if separate) are an ok size.
	 */
	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_dblocks) {
		cmn_err(CE_WARN, "XFS: size check 1 failed");
		error = XFS_ERROR(E2BIG);
		goto error1;
	}
	error = xfs_read_buf(mp, mp->m_ddev_targp,
			     d - XFS_FSS_TO_BB(mp, 1),
			     XFS_FSS_TO_BB(mp, 1), 0, &bp);
	if (!error) {
		xfs_buf_relse(bp);
	} else {
		cmn_err(CE_WARN, "XFS: size check 2 failed");
		if (error == ENOSPC) {
			error = XFS_ERROR(E2BIG);
		}
		goto error1;
	}

	if (((mfsi_flags & XFS_MFSI_CLIENT) == 0) &&
	    mp->m_logdev_targp != mp->m_ddev_targp) {
		d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_logblocks);
		if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_logblocks) {
			cmn_err(CE_WARN, "XFS: size check 3 failed");
			error = XFS_ERROR(E2BIG);
			goto error1;
		}
		error = xfs_read_buf(mp, mp->m_logdev_targp,
				     d - XFS_FSB_TO_BB(mp, 1),
				     XFS_FSB_TO_BB(mp, 1), 0, &bp);
		if (!error) {
			xfs_buf_relse(bp);
		} else {
			cmn_err(CE_WARN, "XFS: size check 3 failed");
			if (error == ENOSPC) {
				error = XFS_ERROR(E2BIG);
			}
			goto error1;
		}
	}

	/*
	 * Initialize realtime fields in the mount structure
	 */
	if ((error = xfs_rtmount_init(mp))) {
		cmn_err(CE_WARN, "XFS: RT mount failed");
		goto error1;
	}

	/*
	 * For client case we are done now
	 */
	if (mfsi_flags & XFS_MFSI_CLIENT) {
		return 0;
	}

	/*
	 *  Copies the low order bits of the timestamp and the randomly
	 *  set "sequence" number out of a UUID.
	 */
	uuid_getnodeuniq(&sbp->sb_uuid, mp->m_fixedfsid);

	/*
	 *  The vfs structure needs to have a file system independent
	 *  way of checking for the invariant file system ID.  Since it
	 *  can't look at mount structures it has a pointer to the data
	 *  in the mount structure.
	 *
	 *  File systems that don't support user level file handles (i.e.
	 *  all of them except for XFS) will leave vfs_altfsid as NULL.
	 */
	vfsp->vfs_altfsid = (xfs_fsid_t *)mp->m_fixedfsid;
	mp->m_dmevmask = 0;	/* not persistent; set after each mount */

	xfs_dir_mount(mp);

	/*
	 * Initialize the attribute manager's entries.
	 */
	mp->m_attr_magicpct = (mp->m_sb.sb_blocksize * 37) / 100;

	/*
	 * Initialize the precomputed transaction reservations values.
	 */
	xfs_trans_init(mp);

	/*
	 * Allocate and initialize the inode hash table for this
	 * file system.
	 */
	xfs_ihash_init(mp);
	xfs_chash_init(mp);

	/*
	 * Allocate and initialize the per-ag data.
	 */
	init_rwsem(&mp->m_peraglock);
	mp->m_perag =
		kmem_zalloc(sbp->sb_agcount * sizeof(xfs_perag_t), KM_SLEEP);

	mp->m_maxagi = xfs_initialize_perag(vfsp, mp, sbp->sb_agcount);

	/*
	 * log's mount-time initialization. Perform 1st part recovery if needed
	 */
	if (likely(sbp->sb_logblocks > 0)) {	/* check for volume case */
		error = xfs_log_mount(mp, mp->m_logdev_targp,
				      XFS_FSB_TO_DADDR(mp, sbp->sb_logstart),
				      XFS_FSB_TO_BB(mp, sbp->sb_logblocks));
		if (error) {
			cmn_err(CE_WARN, "XFS: log mount failed");
			goto error2;
		}
	} else {	/* No log has been defined */
		cmn_err(CE_WARN, "XFS: no log defined");
		XFS_ERROR_REPORT("xfs_mountfs_int(1)", XFS_ERRLEVEL_LOW, mp);
		error = XFS_ERROR(EFSCORRUPTED);
		goto error2;
	}

	/*
	 * Now the log is mounted, we know if it was an unclean shutdown or
	 * not. If it was, with the first phase of recovery has completed, we
	 * have consistent AG blocks on disk. We have not recovered EFIs yet,
	 * but they are recovered transactionally in the second recovery phase
	 * later.
	 *
	 * Hence we can safely re-initialise incore superblock counters from
	 * the per-ag data. These may not be correct if the filesystem was not
	 * cleanly unmounted, so we need to wait for recovery to finish before
	 * doing this.
	 *
	 * If the filesystem was cleanly unmounted, then we can trust the
	 * values in the superblock to be correct and we don't need to do
	 * anything here.
	 *
	 * If we are currently making the filesystem, the initialisation will
	 * fail as the perag data is in an undefined state.
	 */

	if (xfs_sb_version_haslazysbcount(&mp->m_sb) &&
	    !XFS_LAST_UNMOUNT_WAS_CLEAN(mp) &&
	     !mp->m_sb.sb_inprogress) {
		error = xfs_initialize_perag_data(mp, sbp->sb_agcount);
		if (error) {
			goto error2;
		}
	}
	/*
	 * Get and sanity-check the root inode.
	 * Save the pointer to it in the mount structure.
	 */
	error = xfs_iget(mp, NULL, sbp->sb_rootino, 0, XFS_ILOCK_EXCL, &rip, 0);
	if (error) {
		cmn_err(CE_WARN, "XFS: failed to read root inode");
		goto error3;
	}

	ASSERT(rip != NULL);
	rvp = XFS_ITOV(rip);

	if (unlikely((rip->i_d.di_mode & S_IFMT) != S_IFDIR)) {
		cmn_err(CE_WARN, "XFS: corrupted root inode");
		cmn_err(CE_WARN, "Device %s - root %llu is not a directory",
			XFS_BUFTARG_NAME(mp->m_ddev_targp),
			(unsigned long long)rip->i_ino);
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		XFS_ERROR_REPORT("xfs_mountfs_int(2)", XFS_ERRLEVEL_LOW,
				 mp);
		error = XFS_ERROR(EFSCORRUPTED);
		goto error4;
	}
	mp->m_rootip = rip;	/* save it */

	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	/*
	 * Initialize realtime inode pointers in the mount structure
	 */
	if ((error = xfs_rtmount_inodes(mp))) {
		/*
		 * Free up the root inode.
		 */
		cmn_err(CE_WARN, "XFS: failed to read RT inodes");
		goto error4;
	}

	/*
	 * If fs is not mounted readonly, then update the superblock
	 * unit and width changes.
	 */
	if (update_flags && !(vfsp->vfs_flag & VFS_RDONLY))
		xfs_mount_log_sbunit(mp, update_flags);

	/*
	 * Initialise the XFS quota management subsystem for this mount
	 */
	if ((error = XFS_QM_INIT(mp, &quotamount, &quotaflags)))
		goto error4;

	/*
	 * Finish recovering the file system.  This part needed to be
	 * delayed until after the root and real-time bitmap inodes
	 * were consistently read in.
	 */
	error = xfs_log_mount_finish(mp, mfsi_flags);
	if (error) {
		cmn_err(CE_WARN, "XFS: log mount finish failed");
		goto error4;
	}

	/*
	 * Complete the quota initialisation, post-log-replay component.
	 */
	if ((error = XFS_QM_MOUNT(mp, quotamount, quotaflags, mfsi_flags)))
		goto error4;

	/*
	 * Now we are mounted, reserve a small amount of unused space for
	 * privileged transactions. This is needed so that transaction
	 * space required for critical operations can dip into this pool
	 * when at ENOSPC. This is needed for operations like create with
	 * attr, unwritten extent conversion at ENOSPC, etc. Data allocations
	 * are not allowed to use this reserved space.
	 *
	 * We default to 5% or 1024 fsbs of space reserved, whichever is smaller.
	 * This may drive us straight to ENOSPC on mount, but that implies
	 * we were already there on the last unmount.
	 */
	resblks = mp->m_sb.sb_dblocks;
	do_div(resblks, 20);
	resblks = min_t(__uint64_t, resblks, 1024);
	xfs_reserve_blocks(mp, &resblks, NULL);

	return 0;

 error4:
	/*
	 * Free up the root inode.
	 */
	VN_RELE(rvp);
 error3:
	xfs_log_unmount_dealloc(mp);
 error2:
	xfs_ihash_free(mp);
	xfs_chash_free(mp);
	for (agno = 0; agno < sbp->sb_agcount; agno++)
		if (mp->m_perag[agno].pagb_list)
			kmem_free(mp->m_perag[agno].pagb_list,
			  sizeof(xfs_perag_busy_t) * XFS_PAGB_NUM_SLOTS);
	kmem_free(mp->m_perag, sbp->sb_agcount * sizeof(xfs_perag_t));
	mp->m_perag = NULL;
	/* FALLTHROUGH */
 error1:
	if (uuid_mounted)
		xfs_uuid_unmount(mp);
	xfs_freesb(mp);
	return error;
}

/*
 * xfs_unmountfs
 *
 * This flushes out the inodes,dquots and the superblock, unmounts the
 * log and makes sure that incore structures are freed.
 */
int
xfs_unmountfs(xfs_mount_t *mp, struct cred *cr)
{
	struct bhv_vfs	*vfsp = XFS_MTOVFS(mp);
#if defined(DEBUG) || defined(INDUCE_IO_ERROR)
	int64_t		fsid;
#endif
	__uint64_t	resblks;

	/*
	 * We can potentially deadlock here if we have an inode cluster
	 * that has been freed has it's buffer still pinned in memory because
	 * the transaction is still sitting in a iclog. The stale inodes
	 * on that buffer will have their flush locks held until the
	 * transaction hits the disk and the callbacks run. the inode
	 * flush takes the flush lock unconditionally and with nothing to
	 * push out the iclog we will never get that unlocked. hence we
	 * need to force the log first.
	 */
	xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE | XFS_LOG_SYNC);
	xfs_iflush_all(mp);

	XFS_QM_DQPURGEALL(mp, XFS_QMOPT_QUOTALL | XFS_QMOPT_UMOUNTING);

	/*
	 * Flush out the log synchronously so that we know for sure
	 * that nothing is pinned.  This is important because bflush()
	 * will skip pinned buffers.
	 */
	xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE | XFS_LOG_SYNC);

	xfs_binval(mp->m_ddev_targp);
	if (mp->m_rtdev_targp) {
		xfs_binval(mp->m_rtdev_targp);
	}

	/*
	 * Unreserve any blocks we have so that when we unmount we don't account
	 * the reserved free space as used. This is really only necessary for
	 * lazy superblock counting because it trusts the incore superblock
	 * counters to be aboslutely correct on clean unmount.
	 *
	 * We don't bother correcting this elsewhere for lazy superblock
	 * counting because on mount of an unclean filesystem we reconstruct the
	 * correct counter value and this is irrelevant.
	 *
	 * For non-lazy counter filesystems, this doesn't matter at all because
	 * we only every apply deltas to the superblock and hence the incore
	 * value does not matter....
	 */
	resblks = 0;
	xfs_reserve_blocks(mp, &resblks, NULL);

	xfs_log_sbcount(mp, 1);
	xfs_unmountfs_writesb(mp);
	xfs_unmountfs_wait(mp); 		/* wait for async bufs */
	xfs_log_unmount(mp);			/* Done! No more fs ops. */

	xfs_freesb(mp);

	/*
	 * All inodes from this mount point should be freed.
	 */
	ASSERT(mp->m_inodes == NULL);

	xfs_unmountfs_close(mp, cr);
	if ((mp->m_flags & XFS_MOUNT_NOUUID) == 0)
		xfs_uuid_unmount(mp);

#if defined(DEBUG) || defined(INDUCE_IO_ERROR)
	/*
	 * clear all error tags on this filesystem
	 */
	memcpy(&fsid, &vfsp->vfs_fsid, sizeof(int64_t));
	xfs_errortag_clearall_umount(fsid, mp->m_fsname, 0);
#endif
	XFS_IODONE(vfsp);
	xfs_mount_free(mp, 1);
	return 0;
}

void
xfs_unmountfs_close(xfs_mount_t *mp, struct cred *cr)
{
	if (mp->m_logdev_targp != mp->m_ddev_targp)
		xfs_free_buftarg(mp->m_logdev_targp, 1);
	if (mp->m_rtdev_targp)
		xfs_free_buftarg(mp->m_rtdev_targp, 1);
	xfs_free_buftarg(mp->m_ddev_targp, 0);
}

STATIC void
xfs_unmountfs_wait(xfs_mount_t *mp)
{
	if (mp->m_logdev_targp != mp->m_ddev_targp)
		xfs_wait_buftarg(mp->m_logdev_targp);
	if (mp->m_rtdev_targp)
		xfs_wait_buftarg(mp->m_rtdev_targp);
	xfs_wait_buftarg(mp->m_ddev_targp);
}

int
xfs_fs_writable(xfs_mount_t *mp)
{
	bhv_vfs_t	*vfsp = XFS_MTOVFS(mp);

	return !(vfs_test_for_freeze(vfsp) || XFS_FORCED_SHUTDOWN(mp) ||
		(vfsp->vfs_flag & VFS_RDONLY));
}

/*
 * xfs_log_sbcount
 *
 * Called either periodically to keep the on disk superblock values
 * roughly up to date or from unmount to make sure the values are
 * correct on a clean unmount.
 *
 * Note this code can be called during the process of freezing, so
 * we may need to use the transaction allocator which does not not
 * block when the transaction subsystem is in its frozen state.
 */
int
xfs_log_sbcount(
	xfs_mount_t	*mp,
	uint		sync)
{
	xfs_trans_t	*tp;
	int		error;

	if (!xfs_fs_writable(mp))
		return 0;

	xfs_icsb_sync_counters(mp);

	/*
	 * we don't need to do this if we are updating the superblock
	 * counters on every modification.
	 */
	if (!xfs_sb_version_haslazysbcount(&mp->m_sb))
		return 0;

	tp = _xfs_trans_alloc(mp, XFS_TRANS_SB_COUNT);
	error = xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
					XFS_DEFAULT_LOG_COUNT);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_mod_sb(tp, XFS_SB_IFREE | XFS_SB_ICOUNT | XFS_SB_FDBLOCKS);
	if (sync)
		xfs_trans_set_sync(tp);
	xfs_trans_commit(tp, 0);

	return 0;
}

int
xfs_unmountfs_writesb(xfs_mount_t *mp)
{
	xfs_buf_t	*sbp;
	xfs_sb_t	*sb;
	int		error = 0;

	/*
	 * skip superblock write if fs is read-only, or
	 * if we are doing a forced umount.
	 */
	if (!(XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY ||
		XFS_FORCED_SHUTDOWN(mp))) {

		sbp = xfs_getsb(mp, 0);
 		sb = XFS_BUF_TO_SBP(sbp);

		/*
		 * mark shared-readonly if desired
		 */
		if (mp->m_mk_sharedro) {
			if (!(sb->sb_flags & XFS_SBF_READONLY))
				sb->sb_flags |= XFS_SBF_READONLY;
			if (!XFS_SB_VERSION_HASSHARED(sb))
				XFS_SB_VERSION_ADDSHARED(sb);
			xfs_fs_cmn_err(CE_NOTE, mp,
				"Unmounting, marking shared read-only");
		}

		XFS_BUF_UNDONE(sbp);
		XFS_BUF_UNREAD(sbp);
		XFS_BUF_UNDELAYWRITE(sbp);
		XFS_BUF_WRITE(sbp);
		XFS_BUF_UNASYNC(sbp);
		ASSERT(XFS_BUF_TARGET(sbp) == mp->m_ddev_targp);
		xfsbdstrat(mp, sbp);
		/* Nevermind errors we might get here. */
		error = xfs_iowait(sbp);
		if (error)
			xfs_ioerror_alert("xfs_unmountfs_writesb",
					  mp, sbp, XFS_BUF_ADDR(sbp));
		if (error && mp->m_mk_sharedro)
			xfs_fs_cmn_err(CE_ALERT, mp, "Superblock write error detected while unmounting.  Filesystem may not be marked shared readonly");
		xfs_buf_relse(sbp);
	}
	return error;
}

/*
 * xfs_mod_sb() can be used to copy arbitrary changes to the
 * in-core superblock into the superblock buffer to be logged.
 * It does not provide the higher level of locking that is
 * needed to protect the in-core superblock from concurrent
 * access.
 */
void
xfs_mod_sb(xfs_trans_t *tp, __int64_t fields)
{
	xfs_buf_t	*bp;
	int		first;
	int		last;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	xfs_sb_field_t	f;

	ASSERT(fields);
	if (!fields)
		return;
	mp = tp->t_mountp;
	bp = xfs_trans_getsb(tp, mp, 0);
	sbp = XFS_BUF_TO_SBP(bp);
	first = sizeof(xfs_sb_t);
	last = 0;

	/* translate/copy */

	xfs_xlatesb(XFS_BUF_PTR(bp), &(mp->m_sb), -1, fields);

	/* find modified range */

	f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	first = xfs_sb_info[f].offset;

	f = (xfs_sb_field_t)xfs_highbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	last = xfs_sb_info[f + 1].offset - 1;

	xfs_trans_log_buf(tp, bp, first, last);
}


/*
 * xfs_mod_incore_sb_unlocked() is a utility routine common used to apply
 * a delta to a specified field in the in-core superblock.  Simply
 * switch on the field indicated and apply the delta to that field.
 * Fields are not allowed to dip below zero, so if the delta would
 * do this do not apply it and return EINVAL.
 *
 * The SB_LOCK must be held when this routine is called.
 */
int
xfs_mod_incore_sb_unlocked(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field,
	int64_t		delta,
	int		rsvd)
{
	int		scounter;	/* short counter for 32 bit fields */
	long long	lcounter;	/* long counter for 64 bit fields */
	long long	res_used, rem;

	/*
	 * With the in-core superblock spin lock held, switch
	 * on the indicated field.  Apply the delta to the
	 * proper field.  If the fields value would dip below
	 * 0, then do not apply the delta and return EINVAL.
	 */
	switch (field) {
	case XFS_SBS_ICOUNT:
		lcounter = (long long)mp->m_sb.sb_icount;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_icount = lcounter;
		return 0;
	case XFS_SBS_IFREE:
		lcounter = (long long)mp->m_sb.sb_ifree;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_ifree = lcounter;
		return 0;
	case XFS_SBS_FDBLOCKS:
		lcounter = (long long)
			mp->m_sb.sb_fdblocks - XFS_ALLOC_SET_ASIDE(mp);
		res_used = (long long)(mp->m_resblks - mp->m_resblks_avail);

		if (delta > 0) {		/* Putting blocks back */
			if (res_used > delta) {
				mp->m_resblks_avail += delta;
			} else {
				rem = delta - res_used;
				mp->m_resblks_avail = mp->m_resblks;
				lcounter += rem;
			}
		} else {				/* Taking blocks away */

			lcounter += delta;

		/*
		 * If were out of blocks, use any available reserved blocks if
		 * were allowed to.
		 */

			if (lcounter < 0) {
				if (rsvd) {
					lcounter = (long long)mp->m_resblks_avail + delta;
					if (lcounter < 0) {
						return XFS_ERROR(ENOSPC);
					}
					mp->m_resblks_avail = lcounter;
					return 0;
				} else {	/* not reserved */
					return XFS_ERROR(ENOSPC);
				}
			}
		}

		mp->m_sb.sb_fdblocks = lcounter + XFS_ALLOC_SET_ASIDE(mp);
		return 0;
	case XFS_SBS_FREXTENTS:
		lcounter = (long long)mp->m_sb.sb_frextents;
		lcounter += delta;
		if (lcounter < 0) {
			return XFS_ERROR(ENOSPC);
		}
		mp->m_sb.sb_frextents = lcounter;
		return 0;
	case XFS_SBS_DBLOCKS:
		lcounter = (long long)mp->m_sb.sb_dblocks;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_dblocks = lcounter;
		return 0;
	case XFS_SBS_AGCOUNT:
		scounter = mp->m_sb.sb_agcount;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_agcount = scounter;
		return 0;
	case XFS_SBS_IMAX_PCT:
		scounter = mp->m_sb.sb_imax_pct;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_imax_pct = scounter;
		return 0;
	case XFS_SBS_REXTSIZE:
		scounter = mp->m_sb.sb_rextsize;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_rextsize = scounter;
		return 0;
	case XFS_SBS_RBMBLOCKS:
		scounter = mp->m_sb.sb_rbmblocks;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_rbmblocks = scounter;
		return 0;
	case XFS_SBS_RBLOCKS:
		lcounter = (long long)mp->m_sb.sb_rblocks;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_rblocks = lcounter;
		return 0;
	case XFS_SBS_REXTENTS:
		lcounter = (long long)mp->m_sb.sb_rextents;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_rextents = lcounter;
		return 0;
	case XFS_SBS_REXTSLOG:
		scounter = mp->m_sb.sb_rextslog;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return XFS_ERROR(EINVAL);
		}
		mp->m_sb.sb_rextslog = scounter;
		return 0;
	default:
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
}

/*
 * xfs_mod_incore_sb() is used to change a field in the in-core
 * superblock structure by the specified delta.  This modification
 * is protected by the SB_LOCK.  Just use the xfs_mod_incore_sb_unlocked()
 * routine to do the work.
 */
int
xfs_mod_incore_sb(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field,
	int64_t		delta,
	int		rsvd)
{
	unsigned long	s;
	int	status;

	/* check for per-cpu counters */
	switch (field) {
#ifdef HAVE_PERCPU_SB
	case XFS_SBS_ICOUNT:
	case XFS_SBS_IFREE:
	case XFS_SBS_FDBLOCKS:
		if (!(mp->m_flags & XFS_MOUNT_NO_PERCPU_SB)) {
			status = xfs_icsb_modify_counters(mp, field,
							delta, rsvd);
			break;
		}
		/* FALLTHROUGH */
#endif
	default:
		s = XFS_SB_LOCK(mp);
		status = xfs_mod_incore_sb_unlocked(mp, field, delta, rsvd);
		XFS_SB_UNLOCK(mp, s);
		break;
	}

	return status;
}

/*
 * xfs_mod_incore_sb_batch() is used to change more than one field
 * in the in-core superblock structure at a time.  This modification
 * is protected by a lock internal to this module.  The fields and
 * changes to those fields are specified in the array of xfs_mod_sb
 * structures passed in.
 *
 * Either all of the specified deltas will be applied or none of
 * them will.  If any modified field dips below 0, then all modifications
 * will be backed out and EINVAL will be returned.
 */
int
xfs_mod_incore_sb_batch(xfs_mount_t *mp, xfs_mod_sb_t *msb, uint nmsb, int rsvd)
{
	unsigned long	s;
	int		status=0;
	xfs_mod_sb_t	*msbp;

	/*
	 * Loop through the array of mod structures and apply each
	 * individually.  If any fail, then back out all those
	 * which have already been applied.  Do all of this within
	 * the scope of the SB_LOCK so that all of the changes will
	 * be atomic.
	 */
	s = XFS_SB_LOCK(mp);
	msbp = &msb[0];
	for (msbp = &msbp[0]; msbp < (msb + nmsb); msbp++) {
		/*
		 * Apply the delta at index n.  If it fails, break
		 * from the loop so we'll fall into the undo loop
		 * below.
		 */
		switch (msbp->msb_field) {
#ifdef HAVE_PERCPU_SB
		case XFS_SBS_ICOUNT:
		case XFS_SBS_IFREE:
		case XFS_SBS_FDBLOCKS:
			if (!(mp->m_flags & XFS_MOUNT_NO_PERCPU_SB)) {
				XFS_SB_UNLOCK(mp, s);
				status = xfs_icsb_modify_counters(mp,
							msbp->msb_field,
							msbp->msb_delta, rsvd);
				s = XFS_SB_LOCK(mp);
				break;
			}
			/* FALLTHROUGH */
#endif
		default:
			status = xfs_mod_incore_sb_unlocked(mp,
						msbp->msb_field,
						msbp->msb_delta, rsvd);
			break;
		}

		if (status != 0) {
			break;
		}
	}

	/*
	 * If we didn't complete the loop above, then back out
	 * any changes made to the superblock.  If you add code
	 * between the loop above and here, make sure that you
	 * preserve the value of status. Loop back until
	 * we step below the beginning of the array.  Make sure
	 * we don't touch anything back there.
	 */
	if (status != 0) {
		msbp--;
		while (msbp >= msb) {
			switch (msbp->msb_field) {
#ifdef HAVE_PERCPU_SB
			case XFS_SBS_ICOUNT:
			case XFS_SBS_IFREE:
			case XFS_SBS_FDBLOCKS:
				if (!(mp->m_flags & XFS_MOUNT_NO_PERCPU_SB)) {
					XFS_SB_UNLOCK(mp, s);
					status = xfs_icsb_modify_counters(mp,
							msbp->msb_field,
							-(msbp->msb_delta),
							rsvd);
					s = XFS_SB_LOCK(mp);
					break;
				}
				/* FALLTHROUGH */
#endif
			default:
				status = xfs_mod_incore_sb_unlocked(mp,
							msbp->msb_field,
							-(msbp->msb_delta),
							rsvd);
				break;
			}
			ASSERT(status == 0);
			msbp--;
		}
	}
	XFS_SB_UNLOCK(mp, s);
	return status;
}

/*
 * xfs_getsb() is called to obtain the buffer for the superblock.
 * The buffer is returned locked and read in from disk.
 * The buffer should be released with a call to xfs_brelse().
 *
 * If the flags parameter is BUF_TRYLOCK, then we'll only return
 * the superblock buffer if it can be locked without sleeping.
 * If it can't then we'll return NULL.
 */
xfs_buf_t *
xfs_getsb(
	xfs_mount_t	*mp,
	int		flags)
{
	xfs_buf_t	*bp;

	ASSERT(mp->m_sb_bp != NULL);
	bp = mp->m_sb_bp;
	if (flags & XFS_BUF_TRYLOCK) {
		if (!XFS_BUF_CPSEMA(bp)) {
			return NULL;
		}
	} else {
		XFS_BUF_PSEMA(bp, PRIBIO);
	}
	XFS_BUF_HOLD(bp);
	ASSERT(XFS_BUF_ISDONE(bp));
	return bp;
}

/*
 * Used to free the superblock along various error paths.
 */
void
xfs_freesb(
	xfs_mount_t	*mp)
{
	xfs_buf_t	*bp;

	/*
	 * Use xfs_getsb() so that the buffer will be locked
	 * when we call xfs_buf_relse().
	 */
	bp = xfs_getsb(mp, 0);
	XFS_BUF_UNMANAGE(bp);
	xfs_buf_relse(bp);
	mp->m_sb_bp = NULL;
}

/*
 * See if the UUID is unique among mounted XFS filesystems.
 * Mount fails if UUID is nil or a FS with the same UUID is already mounted.
 */
STATIC int
xfs_uuid_mount(
	xfs_mount_t	*mp)
{
	if (uuid_is_nil(&mp->m_sb.sb_uuid)) {
		cmn_err(CE_WARN,
			"XFS: Filesystem %s has nil UUID - can't mount",
			mp->m_fsname);
		return -1;
	}
	if (!uuid_table_insert(&mp->m_sb.sb_uuid)) {
		cmn_err(CE_WARN,
			"XFS: Filesystem %s has duplicate UUID - can't mount",
			mp->m_fsname);
		return -1;
	}
	return 0;
}

/*
 * Remove filesystem from the UUID table.
 */
STATIC void
xfs_uuid_unmount(
	xfs_mount_t	*mp)
{
	uuid_table_remove(&mp->m_sb.sb_uuid);
}

/*
 * Used to log changes to the superblock unit and width fields which could
 * be altered by the mount options. Only the first superblock is updated.
 */
STATIC void
xfs_mount_log_sbunit(
	xfs_mount_t	*mp,
	__int64_t	fields)
{
	xfs_trans_t	*tp;

	ASSERT(fields & (XFS_SB_UNIT|XFS_SB_WIDTH|XFS_SB_UUID));

	tp = xfs_trans_alloc(mp, XFS_TRANS_SB_UNIT);
	if (xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
				XFS_DEFAULT_LOG_COUNT)) {
		xfs_trans_cancel(tp, 0);
		return;
	}
	xfs_mod_sb(tp, fields);
	xfs_trans_commit(tp, 0);
}


#ifdef HAVE_PERCPU_SB
/*
 * Per-cpu incore superblock counters
 *
 * Simple concept, difficult implementation
 *
 * Basically, replace the incore superblock counters with a distributed per cpu
 * counter for contended fields (e.g.  free block count).
 *
 * Difficulties arise in that the incore sb is used for ENOSPC checking, and
 * hence needs to be accurately read when we are running low on space. Hence
 * there is a method to enable and disable the per-cpu counters based on how
 * much "stuff" is available in them.
 *
 * Basically, a counter is enabled if there is enough free resource to justify
 * running a per-cpu fast-path. If the per-cpu counter runs out (i.e. a local
 * ENOSPC), then we disable the counters to synchronise all callers and
 * re-distribute the available resources.
 *
 * If, once we redistributed the available resources, we still get a failure,
 * we disable the per-cpu counter and go through the slow path.
 *
 * The slow path is the current xfs_mod_incore_sb() function.  This means that
 * when we disable a per-cpu counter, we need to drain it's resources back to
 * the global superblock. We do this after disabling the counter to prevent
 * more threads from queueing up on the counter.
 *
 * Essentially, this means that we still need a lock in the fast path to enable
 * synchronisation between the global counters and the per-cpu counters. This
 * is not a problem because the lock will be local to a CPU almost all the time
 * and have little contention except when we get to ENOSPC conditions.
 *
 * Basically, this lock becomes a barrier that enables us to lock out the fast
 * path while we do things like enabling and disabling counters and
 * synchronising the counters.
 *
 * Locking rules:
 *
 * 	1. XFS_SB_LOCK() before picking up per-cpu locks
 * 	2. per-cpu locks always picked up via for_each_online_cpu() order
 * 	3. accurate counter sync requires XFS_SB_LOCK + per cpu locks
 * 	4. modifying per-cpu counters requires holding per-cpu lock
 * 	5. modifying global counters requires holding XFS_SB_LOCK
 *	6. enabling or disabling a counter requires holding the XFS_SB_LOCK
 *	   and _none_ of the per-cpu locks.
 *
 * Disabled counters are only ever re-enabled by a balance operation
 * that results in more free resources per CPU than a given threshold.
 * To ensure counters don't remain disabled, they are rebalanced when
 * the global resource goes above a higher threshold (i.e. some hysteresis
 * is present to prevent thrashing).
 */

#ifdef CONFIG_HOTPLUG_CPU
/*
 * hot-plug CPU notifier support.
 *
 * We need a notifier per filesystem as we need to be able to identify
 * the filesystem to balance the counters out. This is achieved by
 * having a notifier block embedded in the xfs_mount_t and doing pointer
 * magic to get the mount pointer from the notifier block address.
 */
STATIC int
xfs_icsb_cpu_notify(
	struct notifier_block *nfb,
	unsigned long action,
	void *hcpu)
{
	xfs_icsb_cnts_t *cntp;
	xfs_mount_t	*mp;
	int		s;

	mp = (xfs_mount_t *)container_of(nfb, xfs_mount_t, m_icsb_notifier);
	cntp = (xfs_icsb_cnts_t *)
			per_cpu_ptr(mp->m_sb_cnts, (unsigned long)hcpu);
	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		/* Easy Case - initialize the area and locks, and
		 * then rebalance when online does everything else for us. */
		memset(cntp, 0, sizeof(xfs_icsb_cnts_t));
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		xfs_icsb_lock(mp);
		xfs_icsb_balance_counter(mp, XFS_SBS_ICOUNT, 0, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_IFREE, 0, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_FDBLOCKS, 0, 0);
		xfs_icsb_unlock(mp);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/* Disable all the counters, then fold the dead cpu's
		 * count into the total on the global superblock and
		 * re-enable the counters. */
		xfs_icsb_lock(mp);
		s = XFS_SB_LOCK(mp);
		xfs_icsb_disable_counter(mp, XFS_SBS_ICOUNT);
		xfs_icsb_disable_counter(mp, XFS_SBS_IFREE);
		xfs_icsb_disable_counter(mp, XFS_SBS_FDBLOCKS);

		mp->m_sb.sb_icount += cntp->icsb_icount;
		mp->m_sb.sb_ifree += cntp->icsb_ifree;
		mp->m_sb.sb_fdblocks += cntp->icsb_fdblocks;

		memset(cntp, 0, sizeof(xfs_icsb_cnts_t));

		xfs_icsb_balance_counter(mp, XFS_SBS_ICOUNT,
					 XFS_ICSB_SB_LOCKED, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_IFREE,
					 XFS_ICSB_SB_LOCKED, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_FDBLOCKS,
					 XFS_ICSB_SB_LOCKED, 0);
		XFS_SB_UNLOCK(mp, s);
		xfs_icsb_unlock(mp);
		break;
	}

	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */

int
xfs_icsb_init_counters(
	xfs_mount_t	*mp)
{
	xfs_icsb_cnts_t *cntp;
	int		i;

	mp->m_sb_cnts = alloc_percpu(xfs_icsb_cnts_t);
	if (mp->m_sb_cnts == NULL)
		return -ENOMEM;

#ifdef CONFIG_HOTPLUG_CPU
	mp->m_icsb_notifier.notifier_call = xfs_icsb_cpu_notify;
	mp->m_icsb_notifier.priority = 0;
	register_hotcpu_notifier(&mp->m_icsb_notifier);
#endif /* CONFIG_HOTPLUG_CPU */

	for_each_online_cpu(i) {
		cntp = (xfs_icsb_cnts_t *)per_cpu_ptr(mp->m_sb_cnts, i);
		memset(cntp, 0, sizeof(xfs_icsb_cnts_t));
	}

	mutex_init(&mp->m_icsb_mutex);

	/*
	 * start with all counters disabled so that the
	 * initial balance kicks us off correctly
	 */
	mp->m_icsb_counters = -1;
	return 0;
}

void
xfs_icsb_reinit_counters(
	xfs_mount_t	*mp)
{
	xfs_icsb_lock(mp);
	/*
	 * start with all counters disabled so that the
	 * initial balance kicks us off correctly
	 */
	mp->m_icsb_counters = -1;
	xfs_icsb_balance_counter(mp, XFS_SBS_ICOUNT, 0, 0);
	xfs_icsb_balance_counter(mp, XFS_SBS_IFREE, 0, 0);
	xfs_icsb_balance_counter(mp, XFS_SBS_FDBLOCKS, 0, 0);
	xfs_icsb_unlock(mp);
}

STATIC void
xfs_icsb_destroy_counters(
	xfs_mount_t	*mp)
{
	if (mp->m_sb_cnts) {
		unregister_hotcpu_notifier(&mp->m_icsb_notifier);
		free_percpu(mp->m_sb_cnts);
	}
	mutex_destroy(&mp->m_icsb_mutex);
}

STATIC_INLINE void
xfs_icsb_lock_cntr(
	xfs_icsb_cnts_t	*icsbp)
{
	while (test_and_set_bit(XFS_ICSB_FLAG_LOCK, &icsbp->icsb_flags)) {
		ndelay(1000);
	}
}

STATIC_INLINE void
xfs_icsb_unlock_cntr(
	xfs_icsb_cnts_t	*icsbp)
{
	clear_bit(XFS_ICSB_FLAG_LOCK, &icsbp->icsb_flags);
}


STATIC_INLINE void
xfs_icsb_lock_all_counters(
	xfs_mount_t	*mp)
{
	xfs_icsb_cnts_t *cntp;
	int		i;

	for_each_online_cpu(i) {
		cntp = (xfs_icsb_cnts_t *)per_cpu_ptr(mp->m_sb_cnts, i);
		xfs_icsb_lock_cntr(cntp);
	}
}

STATIC_INLINE void
xfs_icsb_unlock_all_counters(
	xfs_mount_t	*mp)
{
	xfs_icsb_cnts_t *cntp;
	int		i;

	for_each_online_cpu(i) {
		cntp = (xfs_icsb_cnts_t *)per_cpu_ptr(mp->m_sb_cnts, i);
		xfs_icsb_unlock_cntr(cntp);
	}
}

STATIC void
xfs_icsb_count(
	xfs_mount_t	*mp,
	xfs_icsb_cnts_t	*cnt,
	int		flags)
{
	xfs_icsb_cnts_t *cntp;
	int		i;

	memset(cnt, 0, sizeof(xfs_icsb_cnts_t));

	if (!(flags & XFS_ICSB_LAZY_COUNT))
		xfs_icsb_lock_all_counters(mp);

	for_each_online_cpu(i) {
		cntp = (xfs_icsb_cnts_t *)per_cpu_ptr(mp->m_sb_cnts, i);
		cnt->icsb_icount += cntp->icsb_icount;
		cnt->icsb_ifree += cntp->icsb_ifree;
		cnt->icsb_fdblocks += cntp->icsb_fdblocks;
	}

	if (!(flags & XFS_ICSB_LAZY_COUNT))
		xfs_icsb_unlock_all_counters(mp);
}

STATIC int
xfs_icsb_counter_disabled(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field)
{
	ASSERT((field >= XFS_SBS_ICOUNT) && (field <= XFS_SBS_FDBLOCKS));
	return test_bit(field, &mp->m_icsb_counters);
}

STATIC int
xfs_icsb_disable_counter(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field)
{
	xfs_icsb_cnts_t	cnt;

	ASSERT((field >= XFS_SBS_ICOUNT) && (field <= XFS_SBS_FDBLOCKS));

	/*
	 * If we are already disabled, then there is nothing to do
	 * here. We check before locking all the counters to avoid
	 * the expensive lock operation when being called in the
	 * slow path and the counter is already disabled. This is
	 * safe because the only time we set or clear this state is under
	 * the m_icsb_mutex.
	 */
	if (xfs_icsb_counter_disabled(mp, field))
		return 0;

	xfs_icsb_lock_all_counters(mp);
	if (!test_and_set_bit(field, &mp->m_icsb_counters)) {
		/* drain back to superblock */

		xfs_icsb_count(mp, &cnt, XFS_ICSB_SB_LOCKED|XFS_ICSB_LAZY_COUNT);
		switch(field) {
		case XFS_SBS_ICOUNT:
			mp->m_sb.sb_icount = cnt.icsb_icount;
			break;
		case XFS_SBS_IFREE:
			mp->m_sb.sb_ifree = cnt.icsb_ifree;
			break;
		case XFS_SBS_FDBLOCKS:
			mp->m_sb.sb_fdblocks = cnt.icsb_fdblocks;
			break;
		default:
			BUG();
		}
	}

	xfs_icsb_unlock_all_counters(mp);

	return 0;
}

STATIC void
xfs_icsb_enable_counter(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field,
	uint64_t	count,
	uint64_t	resid)
{
	xfs_icsb_cnts_t	*cntp;
	int		i;

	ASSERT((field >= XFS_SBS_ICOUNT) && (field <= XFS_SBS_FDBLOCKS));

	xfs_icsb_lock_all_counters(mp);
	for_each_online_cpu(i) {
		cntp = per_cpu_ptr(mp->m_sb_cnts, i);
		switch (field) {
		case XFS_SBS_ICOUNT:
			cntp->icsb_icount = count + resid;
			break;
		case XFS_SBS_IFREE:
			cntp->icsb_ifree = count + resid;
			break;
		case XFS_SBS_FDBLOCKS:
			cntp->icsb_fdblocks = count + resid;
			break;
		default:
			BUG();
			break;
		}
		resid = 0;
	}
	clear_bit(field, &mp->m_icsb_counters);
	xfs_icsb_unlock_all_counters(mp);
}

void
xfs_icsb_sync_counters_flags(
	xfs_mount_t	*mp,
	int		flags)
{
	xfs_icsb_cnts_t	cnt;
	int		s;

	/* Pass 1: lock all counters */
	if ((flags & XFS_ICSB_SB_LOCKED) == 0)
		s = XFS_SB_LOCK(mp);

	xfs_icsb_count(mp, &cnt, flags);

	/* Step 3: update mp->m_sb fields */
	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_ICOUNT))
		mp->m_sb.sb_icount = cnt.icsb_icount;
	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_IFREE))
		mp->m_sb.sb_ifree = cnt.icsb_ifree;
	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_FDBLOCKS))
		mp->m_sb.sb_fdblocks = cnt.icsb_fdblocks;

	if ((flags & XFS_ICSB_SB_LOCKED) == 0)
		XFS_SB_UNLOCK(mp, s);
}

/*
 * Accurate update of per-cpu counters to incore superblock
 */
STATIC void
xfs_icsb_sync_counters(
	xfs_mount_t	*mp)
{
	xfs_icsb_sync_counters_flags(mp, 0);
}

/*
 * Balance and enable/disable counters as necessary.
 *
 * Thresholds for re-enabling counters are somewhat magic.  inode counts are
 * chosen to be the same number as single on disk allocation chunk per CPU, and
 * free blocks is something far enough zero that we aren't going thrash when we
 * get near ENOSPC. We also need to supply a minimum we require per cpu to
 * prevent looping endlessly when xfs_alloc_space asks for more than will
 * be distributed to a single CPU but each CPU has enough blocks to be
 * reenabled.
 *
 * Note that we can be called when counters are already disabled.
 * xfs_icsb_disable_counter() optimises the counter locking in this case to
 * prevent locking every per-cpu counter needlessly.
 */

#define XFS_ICSB_INO_CNTR_REENABLE	(uint64_t)64
#define XFS_ICSB_FDBLK_CNTR_REENABLE(mp) \
		(uint64_t)(512 + XFS_ALLOC_SET_ASIDE(mp))
STATIC void
xfs_icsb_balance_counter(
	xfs_mount_t	*mp,
	xfs_sb_field_t  field,
	int		flags,
	int		min_per_cpu)
{
	uint64_t	count, resid;
	int		weight = num_online_cpus();
	int		s;
	uint64_t	min = (uint64_t)min_per_cpu;

	if (!(flags & XFS_ICSB_SB_LOCKED))
		s = XFS_SB_LOCK(mp);

	/* disable counter and sync counter */
	xfs_icsb_disable_counter(mp, field);

	/* update counters  - first CPU gets residual*/
	switch (field) {
	case XFS_SBS_ICOUNT:
		count = mp->m_sb.sb_icount;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_INO_CNTR_REENABLE))
			goto out;
		break;
	case XFS_SBS_IFREE:
		count = mp->m_sb.sb_ifree;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_INO_CNTR_REENABLE))
			goto out;
		break;
	case XFS_SBS_FDBLOCKS:
		count = mp->m_sb.sb_fdblocks;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_FDBLK_CNTR_REENABLE(mp)))
			goto out;
		break;
	default:
		BUG();
		count = resid = 0;	/* quiet, gcc */
		break;
	}

	xfs_icsb_enable_counter(mp, field, count, resid);
out:
	if (!(flags & XFS_ICSB_SB_LOCKED))
		XFS_SB_UNLOCK(mp, s);
}

int
xfs_icsb_modify_counters(
	xfs_mount_t	*mp,
	xfs_sb_field_t	field,
	int64_t		delta,
	int		rsvd)
{
	xfs_icsb_cnts_t	*icsbp;
	long long	lcounter;	/* long counter for 64 bit fields */
	int		cpu, ret = 0, s;

	might_sleep();
again:
	cpu = get_cpu();
	icsbp = (xfs_icsb_cnts_t *)per_cpu_ptr(mp->m_sb_cnts, cpu);

	/*
	 * if the counter is disabled, go to slow path
	 */
	if (unlikely(xfs_icsb_counter_disabled(mp, field)))
		goto slow_path;
	xfs_icsb_lock_cntr(icsbp);
	if (unlikely(xfs_icsb_counter_disabled(mp, field))) {
		xfs_icsb_unlock_cntr(icsbp);
		goto slow_path;
	}

	switch (field) {
	case XFS_SBS_ICOUNT:
		lcounter = icsbp->icsb_icount;
		lcounter += delta;
		if (unlikely(lcounter < 0))
			goto balance_counter;
		icsbp->icsb_icount = lcounter;
		break;

	case XFS_SBS_IFREE:
		lcounter = icsbp->icsb_ifree;
		lcounter += delta;
		if (unlikely(lcounter < 0))
			goto balance_counter;
		icsbp->icsb_ifree = lcounter;
		break;

	case XFS_SBS_FDBLOCKS:
		BUG_ON((mp->m_resblks - mp->m_resblks_avail) != 0);

		lcounter = icsbp->icsb_fdblocks - XFS_ALLOC_SET_ASIDE(mp);
		lcounter += delta;
		if (unlikely(lcounter < 0))
			goto balance_counter;
		icsbp->icsb_fdblocks = lcounter + XFS_ALLOC_SET_ASIDE(mp);
		break;
	default:
		BUG();
		break;
	}
	xfs_icsb_unlock_cntr(icsbp);
	put_cpu();
	return 0;

slow_path:
	put_cpu();

	/*
	 * serialise with a mutex so we don't burn lots of cpu on
	 * the superblock lock. We still need to hold the superblock
	 * lock, however, when we modify the global structures.
	 */
	xfs_icsb_lock(mp);

	/*
	 * Now running atomically.
	 *
	 * If the counter is enabled, someone has beaten us to rebalancing.
	 * Drop the lock and try again in the fast path....
	 */
	if (!(xfs_icsb_counter_disabled(mp, field))) {
		xfs_icsb_unlock(mp);
		goto again;
	}

	/*
	 * The counter is currently disabled. Because we are
	 * running atomically here, we know a rebalance cannot
	 * be in progress. Hence we can go straight to operating
	 * on the global superblock. We do not call xfs_mod_incore_sb()
	 * here even though we need to get the SB_LOCK. Doing so
	 * will cause us to re-enter this function and deadlock.
	 * Hence we get the SB_LOCK ourselves and then call
	 * xfs_mod_incore_sb_unlocked() as the unlocked path operates
	 * directly on the global counters.
	 */
	s = XFS_SB_LOCK(mp);
	ret = xfs_mod_incore_sb_unlocked(mp, field, delta, rsvd);
	XFS_SB_UNLOCK(mp, s);

	/*
	 * Now that we've modified the global superblock, we
	 * may be able to re-enable the distributed counters
	 * (e.g. lots of space just got freed). After that
	 * we are done.
	 */
	if (ret != ENOSPC)
		xfs_icsb_balance_counter(mp, field, 0, 0);
	xfs_icsb_unlock(mp);
	return ret;

balance_counter:
	xfs_icsb_unlock_cntr(icsbp);
	put_cpu();

	/*
	 * We may have multiple threads here if multiple per-cpu
	 * counters run dry at the same time. This will mean we can
	 * do more balances than strictly necessary but it is not
	 * the common slowpath case.
	 */
	xfs_icsb_lock(mp);

	/*
	 * running atomically.
	 *
	 * This will leave the counter in the correct state for future
	 * accesses. After the rebalance, we simply try again and our retry
	 * will either succeed through the fast path or slow path without
	 * another balance operation being required.
	 */
	xfs_icsb_balance_counter(mp, field, 0, delta);
	xfs_icsb_unlock(mp);
	goto again;
}

#endif
