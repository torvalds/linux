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

static const struct {
    short offset;
    short type;     /* 0 = integer
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

	mp = kmem_zalloc(sizeof(*mp), KM_SLEEP);

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
	xfs_mount_t *mp,
	int	    remove_bhv)
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
		struct vfs	*vfsp = XFS_MTOVFS(mp);

		bhv_remove_all_vfsops(vfsp, 0);
		VFS_REMOVEBHV(vfsp, &mp->m_bhv);
	}

	kmem_free(mp, sizeof(xfs_mount_t));
}


/*
 * Check the validity of the SB found.
 */
STATIC int
xfs_mount_validate_sb(
	xfs_mount_t	*mp,
	xfs_sb_t	*sbp)
{
	/*
	 * If the log device and data device have the
	 * same device number, the log is internal.
	 * Consequently, the sb_logstart should be non-zero.  If
	 * we have a zero sb_logstart in this case, we may be trying to mount
	 * a volume filesystem in a non-volume manner.
	 */
	if (sbp->sb_magicnum != XFS_SB_MAGIC) {
		cmn_err(CE_WARN, "XFS: bad magic number");
		return XFS_ERROR(EWRONGFS);
	}

	if (!XFS_SB_GOOD_VERSION(sbp)) {
		cmn_err(CE_WARN, "XFS: bad version");
		return XFS_ERROR(EWRONGFS);
	}

	if (unlikely(
	    sbp->sb_logstart == 0 && mp->m_logdev_targp == mp->m_ddev_targp)) {
		cmn_err(CE_WARN,
	"XFS: filesystem is marked as having an external log; "
	"specify logdev on the\nmount command line.");
		XFS_CORRUPTION_ERROR("xfs_mount_validate_sb(1)",
				     XFS_ERRLEVEL_HIGH, mp, sbp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	if (unlikely(
	    sbp->sb_logstart != 0 && mp->m_logdev_targp != mp->m_ddev_targp)) {
		cmn_err(CE_WARN,
	"XFS: filesystem is marked as having an internal log; "
	"don't specify logdev on\nthe mount command line.");
		XFS_CORRUPTION_ERROR("xfs_mount_validate_sb(2)",
				     XFS_ERRLEVEL_HIGH, mp, sbp);
		return XFS_ERROR(EFSCORRUPTED);
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
	    (sbp->sb_rextsize * sbp->sb_blocksize > XFS_MAX_RTEXTSIZE)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize < XFS_MIN_RTEXTSIZE)	||
	    sbp->sb_imax_pct > 100)) {
		cmn_err(CE_WARN, "XFS: SB sanity check 1 failed");
		XFS_CORRUPTION_ERROR("xfs_mount_validate_sb(3)",
				     XFS_ERRLEVEL_LOW, mp, sbp);
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
		cmn_err(CE_WARN, "XFS: SB sanity check 2 failed");
		XFS_ERROR_REPORT("xfs_mount_validate_sb(4)",
				 XFS_ERRLEVEL_LOW, mp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	ASSERT(PAGE_SHIFT >= sbp->sb_blocklog);
	ASSERT(sbp->sb_blocklog >= BBSHIFT);

#if XFS_BIG_BLKNOS     /* Limited by ULONG_MAX of page cache index */
	if (unlikely(
	    (sbp->sb_dblocks >> (PAGE_SHIFT - sbp->sb_blocklog)) > ULONG_MAX ||
	    (sbp->sb_rblocks >> (PAGE_SHIFT - sbp->sb_blocklog)) > ULONG_MAX)) {
#else                  /* Limited by UINT_MAX of sectors */
	if (unlikely(
	    (sbp->sb_dblocks << (sbp->sb_blocklog - BBSHIFT)) > UINT_MAX ||
	    (sbp->sb_rblocks << (sbp->sb_blocklog - BBSHIFT)) > UINT_MAX)) {
#endif
		cmn_err(CE_WARN,
	"XFS: File system is too large to be mounted on this system.");
		return XFS_ERROR(E2BIG);
	}

	if (unlikely(sbp->sb_inprogress)) {
		cmn_err(CE_WARN, "XFS: file system busy");
		XFS_ERROR_REPORT("xfs_mount_validate_sb(5)",
				 XFS_ERRLEVEL_LOW, mp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	/*
	 * Version 1 directory format has never worked on Linux.
	 */
	if (unlikely(!XFS_SB_VERSION_HASDIRV2(sbp))) {
		cmn_err(CE_WARN,
	"XFS: Attempted to mount file system using version 1 directory format");
		return XFS_ERROR(ENOSYS);
	}

	/*
	 * Until this is fixed only page-sized or smaller data blocks work.
	 */
	if (unlikely(sbp->sb_blocksize > PAGE_SIZE)) {
		cmn_err(CE_WARN,
		"XFS: Attempted to mount file system with blocksize %d bytes",
			sbp->sb_blocksize);
		cmn_err(CE_WARN,
		"XFS: Only page-sized (%ld) or less blocksizes currently work.",
			PAGE_SIZE);
		return XFS_ERROR(ENOSYS);
	}

	return 0;
}

xfs_agnumber_t
xfs_initialize_perag(
	struct vfs	*vfs,
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

			/* This ag is prefered for inodes */
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
xfs_readsb(xfs_mount_t *mp)
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
		cmn_err(CE_WARN, "XFS: SB read failed");
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

	error = xfs_mount_validate_sb(mp, &(mp->m_sb));
	if (error) {
		cmn_err(CE_WARN, "XFS: SB validate failed");
		goto fail;
	}

	/*
	 * We must be able to do sector-sized and sector-aligned IO.
	 */
	if (sector_size > mp->m_sb.sb_sectsize) {
		cmn_err(CE_WARN,
			"XFS: device supports only %u byte sectors (not %u)",
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
			cmn_err(CE_WARN, "XFS: SB re-read failed");
			error = bp ? XFS_BUF_GETERROR(bp) : ENOMEM;
			goto fail;
		}
		ASSERT(XFS_BUF_ISBUSY(bp));
		ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);
	}

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
	vfs_t		*vfsp,
	xfs_mount_t	*mp,
	int		mfsi_flags)
{
	xfs_buf_t	*bp;
	xfs_sb_t	*sbp = &(mp->m_sb);
	xfs_inode_t	*rip;
	vnode_t		*rvp = NULL;
	int		readio_log, writeio_log;
	xfs_daddr_t	d;
	__uint64_t	ret64;
	__int64_t	update_flags;
	uint		quotamount, quotaflags;
	int		agno;
	int		uuid_mounted = 0;
	int		error = 0;

	if (mp->m_sb_bp == NULL) {
		if ((error = xfs_readsb(mp))) {
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

	/*
	 * Select the right directory manager.
	 */
	mp->m_dirops =
		XFS_SB_VERSION_HASDIRV2(&mp->m_sb) ?
			xfsv2_dirops :
			xfsv1_dirops;

	/*
	 * Initialize directory manager's entries.
	 */
	XFS_DIR_MOUNT(mp);

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
		prdev("Root inode %llu is not a directory",
		      mp->m_ddev_targp, (unsigned long long)rip->i_ino);
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
	struct vfs	*vfsp = XFS_MTOVFS(mp);
#if defined(DEBUG) || defined(INDUCE_IO_ERROR)
	int64_t		fsid;
#endif

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
xfs_unmountfs_writesb(xfs_mount_t *mp)
{
	xfs_buf_t	*sbp;
	xfs_sb_t	*sb;
	int		error = 0;

	/*
	 * skip superblock write if fs is read-only, or
	 * if we are doing a forced umount.
	 */
	sbp = xfs_getsb(mp, 0);
	if (!(XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY ||
		XFS_FORCED_SHUTDOWN(mp))) {
		/*
		 * mark shared-readonly if desired
		 */
		sb = XFS_BUF_TO_SBP(sbp);
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
	}
	xfs_buf_relse(sbp);
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
STATIC int
xfs_mod_incore_sb_unlocked(xfs_mount_t *mp, xfs_sb_field_t field,
			int delta, int rsvd)
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

		lcounter = (long long)mp->m_sb.sb_fdblocks;
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

		mp->m_sb.sb_fdblocks = lcounter;
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
xfs_mod_incore_sb(xfs_mount_t *mp, xfs_sb_field_t field, int delta, int rsvd)
{
	unsigned long	s;
	int	status;

	s = XFS_SB_LOCK(mp);
	status = xfs_mod_incore_sb_unlocked(mp, field, delta, rsvd);
	XFS_SB_UNLOCK(mp, s);
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
		status = xfs_mod_incore_sb_unlocked(mp, msbp->msb_field,
						    msbp->msb_delta, rsvd);
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
			status = xfs_mod_incore_sb_unlocked(mp,
				    msbp->msb_field, -(msbp->msb_delta), rsvd);
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
	xfs_trans_commit(tp, 0, NULL);
}
