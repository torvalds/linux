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
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_fsops.h"
#include "xfs_utils.h"
#include "xfs_trace.h"
#include "xfs_icache.h"


#ifdef HAVE_PERCPU_SB
STATIC void	xfs_icsb_balance_counter(xfs_mount_t *, xfs_sb_field_t,
						int);
STATIC void	xfs_icsb_balance_counter_locked(xfs_mount_t *, xfs_sb_field_t,
						int);
STATIC void	xfs_icsb_disable_counter(xfs_mount_t *, xfs_sb_field_t);
#else

#define xfs_icsb_balance_counter(mp, a, b)		do { } while (0)
#define xfs_icsb_balance_counter_locked(mp, a, b)	do { } while (0)
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
    { offsetof(xfs_sb_t, sb_bad_features2), 0 },
    { sizeof(xfs_sb_t),			 0 }
};

static DEFINE_MUTEX(xfs_uuid_table_mutex);
static int xfs_uuid_table_size;
static uuid_t *xfs_uuid_table;

/*
 * See if the UUID is unique among mounted XFS filesystems.
 * Mount fails if UUID is nil or a FS with the same UUID is already mounted.
 */
STATIC int
xfs_uuid_mount(
	struct xfs_mount	*mp)
{
	uuid_t			*uuid = &mp->m_sb.sb_uuid;
	int			hole, i;

	if (mp->m_flags & XFS_MOUNT_NOUUID)
		return 0;

	if (uuid_is_nil(uuid)) {
		xfs_warn(mp, "Filesystem has nil UUID - can't mount");
		return XFS_ERROR(EINVAL);
	}

	mutex_lock(&xfs_uuid_table_mutex);
	for (i = 0, hole = -1; i < xfs_uuid_table_size; i++) {
		if (uuid_is_nil(&xfs_uuid_table[i])) {
			hole = i;
			continue;
		}
		if (uuid_equal(uuid, &xfs_uuid_table[i]))
			goto out_duplicate;
	}

	if (hole < 0) {
		xfs_uuid_table = kmem_realloc(xfs_uuid_table,
			(xfs_uuid_table_size + 1) * sizeof(*xfs_uuid_table),
			xfs_uuid_table_size  * sizeof(*xfs_uuid_table),
			KM_SLEEP);
		hole = xfs_uuid_table_size++;
	}
	xfs_uuid_table[hole] = *uuid;
	mutex_unlock(&xfs_uuid_table_mutex);

	return 0;

 out_duplicate:
	mutex_unlock(&xfs_uuid_table_mutex);
	xfs_warn(mp, "Filesystem has duplicate UUID %pU - can't mount", uuid);
	return XFS_ERROR(EINVAL);
}

STATIC void
xfs_uuid_unmount(
	struct xfs_mount	*mp)
{
	uuid_t			*uuid = &mp->m_sb.sb_uuid;
	int			i;

	if (mp->m_flags & XFS_MOUNT_NOUUID)
		return;

	mutex_lock(&xfs_uuid_table_mutex);
	for (i = 0; i < xfs_uuid_table_size; i++) {
		if (uuid_is_nil(&xfs_uuid_table[i]))
			continue;
		if (!uuid_equal(uuid, &xfs_uuid_table[i]))
			continue;
		memset(&xfs_uuid_table[i], 0, sizeof(uuid_t));
		break;
	}
	ASSERT(i < xfs_uuid_table_size);
	mutex_unlock(&xfs_uuid_table_mutex);
}


/*
 * Reference counting access wrappers to the perag structures.
 * Because we never free per-ag structures, the only thing we
 * have to protect against changes is the tree structure itself.
 */
struct xfs_perag *
xfs_perag_get(struct xfs_mount *mp, xfs_agnumber_t agno)
{
	struct xfs_perag	*pag;
	int			ref = 0;

	rcu_read_lock();
	pag = radix_tree_lookup(&mp->m_perag_tree, agno);
	if (pag) {
		ASSERT(atomic_read(&pag->pag_ref) >= 0);
		ref = atomic_inc_return(&pag->pag_ref);
	}
	rcu_read_unlock();
	trace_xfs_perag_get(mp, agno, ref, _RET_IP_);
	return pag;
}

/*
 * search from @first to find the next perag with the given tag set.
 */
struct xfs_perag *
xfs_perag_get_tag(
	struct xfs_mount	*mp,
	xfs_agnumber_t		first,
	int			tag)
{
	struct xfs_perag	*pag;
	int			found;
	int			ref;

	rcu_read_lock();
	found = radix_tree_gang_lookup_tag(&mp->m_perag_tree,
					(void **)&pag, first, 1, tag);
	if (found <= 0) {
		rcu_read_unlock();
		return NULL;
	}
	ref = atomic_inc_return(&pag->pag_ref);
	rcu_read_unlock();
	trace_xfs_perag_get_tag(mp, pag->pag_agno, ref, _RET_IP_);
	return pag;
}

void
xfs_perag_put(struct xfs_perag *pag)
{
	int	ref;

	ASSERT(atomic_read(&pag->pag_ref) > 0);
	ref = atomic_dec_return(&pag->pag_ref);
	trace_xfs_perag_put(pag->pag_mount, pag->pag_agno, ref, _RET_IP_);
}

STATIC void
__xfs_free_perag(
	struct rcu_head	*head)
{
	struct xfs_perag *pag = container_of(head, struct xfs_perag, rcu_head);

	ASSERT(atomic_read(&pag->pag_ref) == 0);
	kmem_free(pag);
}

/*
 * Free up the per-ag resources associated with the mount structure.
 */
STATIC void
xfs_free_perag(
	xfs_mount_t	*mp)
{
	xfs_agnumber_t	agno;
	struct xfs_perag *pag;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		spin_lock(&mp->m_perag_lock);
		pag = radix_tree_delete(&mp->m_perag_tree, agno);
		spin_unlock(&mp->m_perag_lock);
		ASSERT(pag);
		ASSERT(atomic_read(&pag->pag_ref) == 0);
		call_rcu(&pag->rcu_head, __xfs_free_perag);
	}
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
		return EFBIG;
#else                  /* Limited by UINT_MAX of sectors */
	if (nblocks << (sbp->sb_blocklog - BBSHIFT) > UINT_MAX)
		return EFBIG;
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
	bool		check_inprogress)
{

	/*
	 * If the log device and data device have the
	 * same device number, the log is internal.
	 * Consequently, the sb_logstart should be non-zero.  If
	 * we have a zero sb_logstart in this case, we may be trying to mount
	 * a volume filesystem in a non-volume manner.
	 */
	if (sbp->sb_magicnum != XFS_SB_MAGIC) {
		xfs_warn(mp, "bad magic number");
		return XFS_ERROR(EWRONGFS);
	}

	if (!xfs_sb_good_version(sbp)) {
		xfs_warn(mp, "bad version");
		return XFS_ERROR(EWRONGFS);
	}

	if (unlikely(
	    sbp->sb_logstart == 0 && mp->m_logdev_targp == mp->m_ddev_targp)) {
		xfs_warn(mp,
		"filesystem is marked as having an external log; "
		"specify logdev on the mount command line.");
		return XFS_ERROR(EINVAL);
	}

	if (unlikely(
	    sbp->sb_logstart != 0 && mp->m_logdev_targp != mp->m_ddev_targp)) {
		xfs_warn(mp,
		"filesystem is marked as having an internal log; "
		"do not specify logdev on the mount command line.");
		return XFS_ERROR(EINVAL);
	}

	/*
	 * More sanity checking.  Most of these were stolen directly from
	 * xfs_repair.
	 */
	if (unlikely(
	    sbp->sb_agcount <= 0					||
	    sbp->sb_sectsize < XFS_MIN_SECTORSIZE			||
	    sbp->sb_sectsize > XFS_MAX_SECTORSIZE			||
	    sbp->sb_sectlog < XFS_MIN_SECTORSIZE_LOG			||
	    sbp->sb_sectlog > XFS_MAX_SECTORSIZE_LOG			||
	    sbp->sb_sectsize != (1 << sbp->sb_sectlog)			||
	    sbp->sb_blocksize < XFS_MIN_BLOCKSIZE			||
	    sbp->sb_blocksize > XFS_MAX_BLOCKSIZE			||
	    sbp->sb_blocklog < XFS_MIN_BLOCKSIZE_LOG			||
	    sbp->sb_blocklog > XFS_MAX_BLOCKSIZE_LOG			||
	    sbp->sb_blocksize != (1 << sbp->sb_blocklog)		||
	    sbp->sb_inodesize < XFS_DINODE_MIN_SIZE			||
	    sbp->sb_inodesize > XFS_DINODE_MAX_SIZE			||
	    sbp->sb_inodelog < XFS_DINODE_MIN_LOG			||
	    sbp->sb_inodelog > XFS_DINODE_MAX_LOG			||
	    sbp->sb_inodesize != (1 << sbp->sb_inodelog)		||
	    (sbp->sb_blocklog - sbp->sb_inodelog != sbp->sb_inopblog)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize > XFS_MAX_RTEXTSIZE)	||
	    (sbp->sb_rextsize * sbp->sb_blocksize < XFS_MIN_RTEXTSIZE)	||
	    (sbp->sb_imax_pct > 100 /* zero sb_imax_pct is valid */)	||
	    sbp->sb_dblocks == 0					||
	    sbp->sb_dblocks > XFS_MAX_DBLOCKS(sbp)			||
	    sbp->sb_dblocks < XFS_MIN_DBLOCKS(sbp))) {
		XFS_CORRUPTION_ERROR("SB sanity check failed",
				XFS_ERRLEVEL_LOW, mp, sbp);
		return XFS_ERROR(EFSCORRUPTED);
	}

	/*
	 * Until this is fixed only page-sized or smaller data blocks work.
	 */
	if (unlikely(sbp->sb_blocksize > PAGE_SIZE)) {
		xfs_warn(mp,
		"File system with blocksize %d bytes. "
		"Only pagesize (%ld) or less will currently work.",
				sbp->sb_blocksize, PAGE_SIZE);
		return XFS_ERROR(ENOSYS);
	}

	/*
	 * Currently only very few inode sizes are supported.
	 */
	switch (sbp->sb_inodesize) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		break;
	default:
		xfs_warn(mp, "inode size of %d bytes not supported",
				sbp->sb_inodesize);
		return XFS_ERROR(ENOSYS);
	}

	if (xfs_sb_validate_fsb_count(sbp, sbp->sb_dblocks) ||
	    xfs_sb_validate_fsb_count(sbp, sbp->sb_rblocks)) {
		xfs_warn(mp,
		"file system too large to be mounted on this system.");
		return XFS_ERROR(EFBIG);
	}

	if (check_inprogress && sbp->sb_inprogress) {
		xfs_warn(mp, "Offline file system operation in progress!");
		return XFS_ERROR(EFSCORRUPTED);
	}

	/*
	 * Version 1 directory format has never worked on Linux.
	 */
	if (unlikely(!xfs_sb_version_hasdirv2(sbp))) {
		xfs_warn(mp, "file system using version 1 directory format");
		return XFS_ERROR(ENOSYS);
	}

	return 0;
}

int
xfs_initialize_perag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agcount,
	xfs_agnumber_t	*maxagi)
{
	xfs_agnumber_t	index;
	xfs_agnumber_t	first_initialised = 0;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;
	xfs_ino_t	ino;
	xfs_sb_t	*sbp = &mp->m_sb;
	int		error = -ENOMEM;

	/*
	 * Walk the current per-ag tree so we don't try to initialise AGs
	 * that already exist (growfs case). Allocate and insert all the
	 * AGs we don't find ready for initialisation.
	 */
	for (index = 0; index < agcount; index++) {
		pag = xfs_perag_get(mp, index);
		if (pag) {
			xfs_perag_put(pag);
			continue;
		}
		if (!first_initialised)
			first_initialised = index;

		pag = kmem_zalloc(sizeof(*pag), KM_MAYFAIL);
		if (!pag)
			goto out_unwind;
		pag->pag_agno = index;
		pag->pag_mount = mp;
		spin_lock_init(&pag->pag_ici_lock);
		mutex_init(&pag->pag_ici_reclaim_lock);
		INIT_RADIX_TREE(&pag->pag_ici_root, GFP_ATOMIC);
		spin_lock_init(&pag->pag_buf_lock);
		pag->pag_buf_tree = RB_ROOT;

		if (radix_tree_preload(GFP_NOFS))
			goto out_unwind;

		spin_lock(&mp->m_perag_lock);
		if (radix_tree_insert(&mp->m_perag_tree, index, pag)) {
			BUG();
			spin_unlock(&mp->m_perag_lock);
			radix_tree_preload_end();
			error = -EEXIST;
			goto out_unwind;
		}
		spin_unlock(&mp->m_perag_lock);
		radix_tree_preload_end();
	}

	/*
	 * If we mount with the inode64 option, or no inode overflows
	 * the legacy 32-bit address space clear the inode32 option.
	 */
	agino = XFS_OFFBNO_TO_AGINO(mp, sbp->sb_agblocks - 1, 0);
	ino = XFS_AGINO_TO_INO(mp, agcount - 1, agino);

	if ((mp->m_flags & XFS_MOUNT_SMALL_INUMS) && ino > XFS_MAXINUMBER_32)
		mp->m_flags |= XFS_MOUNT_32BITINODES;
	else
		mp->m_flags &= ~XFS_MOUNT_32BITINODES;

	if (mp->m_flags & XFS_MOUNT_32BITINODES)
		index = xfs_set_inode32(mp);
	else
		index = xfs_set_inode64(mp);

	if (maxagi)
		*maxagi = index;
	return 0;

out_unwind:
	kmem_free(pag);
	for (; index > first_initialised; index--) {
		pag = radix_tree_delete(&mp->m_perag_tree, index);
		kmem_free(pag);
	}
	return error;
}

void
xfs_sb_from_disk(
	struct xfs_sb	*to,
	xfs_dsb_t	*from)
{
	to->sb_magicnum = be32_to_cpu(from->sb_magicnum);
	to->sb_blocksize = be32_to_cpu(from->sb_blocksize);
	to->sb_dblocks = be64_to_cpu(from->sb_dblocks);
	to->sb_rblocks = be64_to_cpu(from->sb_rblocks);
	to->sb_rextents = be64_to_cpu(from->sb_rextents);
	memcpy(&to->sb_uuid, &from->sb_uuid, sizeof(to->sb_uuid));
	to->sb_logstart = be64_to_cpu(from->sb_logstart);
	to->sb_rootino = be64_to_cpu(from->sb_rootino);
	to->sb_rbmino = be64_to_cpu(from->sb_rbmino);
	to->sb_rsumino = be64_to_cpu(from->sb_rsumino);
	to->sb_rextsize = be32_to_cpu(from->sb_rextsize);
	to->sb_agblocks = be32_to_cpu(from->sb_agblocks);
	to->sb_agcount = be32_to_cpu(from->sb_agcount);
	to->sb_rbmblocks = be32_to_cpu(from->sb_rbmblocks);
	to->sb_logblocks = be32_to_cpu(from->sb_logblocks);
	to->sb_versionnum = be16_to_cpu(from->sb_versionnum);
	to->sb_sectsize = be16_to_cpu(from->sb_sectsize);
	to->sb_inodesize = be16_to_cpu(from->sb_inodesize);
	to->sb_inopblock = be16_to_cpu(from->sb_inopblock);
	memcpy(&to->sb_fname, &from->sb_fname, sizeof(to->sb_fname));
	to->sb_blocklog = from->sb_blocklog;
	to->sb_sectlog = from->sb_sectlog;
	to->sb_inodelog = from->sb_inodelog;
	to->sb_inopblog = from->sb_inopblog;
	to->sb_agblklog = from->sb_agblklog;
	to->sb_rextslog = from->sb_rextslog;
	to->sb_inprogress = from->sb_inprogress;
	to->sb_imax_pct = from->sb_imax_pct;
	to->sb_icount = be64_to_cpu(from->sb_icount);
	to->sb_ifree = be64_to_cpu(from->sb_ifree);
	to->sb_fdblocks = be64_to_cpu(from->sb_fdblocks);
	to->sb_frextents = be64_to_cpu(from->sb_frextents);
	to->sb_uquotino = be64_to_cpu(from->sb_uquotino);
	to->sb_gquotino = be64_to_cpu(from->sb_gquotino);
	to->sb_qflags = be16_to_cpu(from->sb_qflags);
	to->sb_flags = from->sb_flags;
	to->sb_shared_vn = from->sb_shared_vn;
	to->sb_inoalignmt = be32_to_cpu(from->sb_inoalignmt);
	to->sb_unit = be32_to_cpu(from->sb_unit);
	to->sb_width = be32_to_cpu(from->sb_width);
	to->sb_dirblklog = from->sb_dirblklog;
	to->sb_logsectlog = from->sb_logsectlog;
	to->sb_logsectsize = be16_to_cpu(from->sb_logsectsize);
	to->sb_logsunit = be32_to_cpu(from->sb_logsunit);
	to->sb_features2 = be32_to_cpu(from->sb_features2);
	to->sb_bad_features2 = be32_to_cpu(from->sb_bad_features2);
}

/*
 * Copy in core superblock to ondisk one.
 *
 * The fields argument is mask of superblock fields to copy.
 */
void
xfs_sb_to_disk(
	xfs_dsb_t	*to,
	xfs_sb_t	*from,
	__int64_t	fields)
{
	xfs_caddr_t	to_ptr = (xfs_caddr_t)to;
	xfs_caddr_t	from_ptr = (xfs_caddr_t)from;
	xfs_sb_field_t	f;
	int		first;
	int		size;

	ASSERT(fields);
	if (!fields)
		return;

	while (fields) {
		f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
		first = xfs_sb_info[f].offset;
		size = xfs_sb_info[f + 1].offset - first;

		ASSERT(xfs_sb_info[f].type == 0 || xfs_sb_info[f].type == 1);

		if (size == 1 || xfs_sb_info[f].type == 1) {
			memcpy(to_ptr + first, from_ptr + first, size);
		} else {
			switch (size) {
			case 2:
				*(__be16 *)(to_ptr + first) =
					cpu_to_be16(*(__u16 *)(from_ptr + first));
				break;
			case 4:
				*(__be32 *)(to_ptr + first) =
					cpu_to_be32(*(__u32 *)(from_ptr + first));
				break;
			case 8:
				*(__be64 *)(to_ptr + first) =
					cpu_to_be64(*(__u64 *)(from_ptr + first));
				break;
			default:
				ASSERT(0);
			}
		}

		fields &= ~(1LL << f);
	}
}

static void
xfs_sb_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_sb	sb;
	int		error;

	xfs_sb_from_disk(&sb, XFS_BUF_TO_SBP(bp));

	/*
	 * Only check the in progress field for the primary superblock as
	 * mkfs.xfs doesn't clear it from secondary superblocks.
	 */
	error = xfs_mount_validate_sb(mp, &sb, bp->b_bn == XFS_SB_DADDR);
	if (error)
		xfs_buf_ioerror(bp, error);
}

void
xfs_sb_write_verify(
	struct xfs_buf	*bp)
{
	xfs_sb_verify(bp);
}

void
xfs_sb_read_verify(
	struct xfs_buf	*bp)
{
	xfs_sb_verify(bp);
	bp->b_pre_io = xfs_sb_write_verify;
	bp->b_iodone = NULL;
	xfs_buf_ioend(bp, 0);
}

/*
 * We may be probed for a filesystem match, so we may not want to emit
 * messages when the superblock buffer is not actually an XFS superblock.
 * If we find an XFS superblock, the run a normal, noisy mount because we are
 * really going to mount it and want to know about errors.
 */
void
xfs_sb_quiet_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_sb	sb;

	xfs_sb_from_disk(&sb, XFS_BUF_TO_SBP(bp));

	if (sb.sb_magicnum == XFS_SB_MAGIC) {
		/* XFS filesystem, verify noisily! */
		xfs_sb_read_verify(bp);
		return;
	}
	/* quietly fail */
	xfs_buf_ioerror(bp, EFSCORRUPTED);
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
	xfs_buf_t	*bp;
	int		error;
	int		loud = !(flags & XFS_MFSI_QUIET);

	ASSERT(mp->m_sb_bp == NULL);
	ASSERT(mp->m_ddev_targp != NULL);

	/*
	 * Allocate a (locked) buffer to hold the superblock.
	 * This will be kept around at all times to optimize
	 * access to the superblock.
	 */
	sector_size = xfs_getsize_buftarg(mp->m_ddev_targp);

reread:
	bp = xfs_buf_read_uncached(mp->m_ddev_targp, XFS_SB_DADDR,
				   BTOBB(sector_size), 0,
				   loud ? xfs_sb_read_verify
				        : xfs_sb_quiet_read_verify);
	if (!bp) {
		if (loud)
			xfs_warn(mp, "SB buffer read failed");
		return EIO;
	}
	if (bp->b_error) {
		error = bp->b_error;
		if (loud)
			xfs_warn(mp, "SB validate failed");
		goto release_buf;
	}

	/*
	 * Initialize the mount structure from the superblock.
	 */
	xfs_sb_from_disk(&mp->m_sb, XFS_BUF_TO_SBP(bp));

	/*
	 * We must be able to do sector-sized and sector-aligned IO.
	 */
	if (sector_size > mp->m_sb.sb_sectsize) {
		if (loud)
			xfs_warn(mp, "device supports %u byte sectors (not %u)",
				sector_size, mp->m_sb.sb_sectsize);
		error = ENOSYS;
		goto release_buf;
	}

	/*
	 * If device sector size is smaller than the superblock size,
	 * re-read the superblock so the buffer is correctly sized.
	 */
	if (sector_size < mp->m_sb.sb_sectsize) {
		xfs_buf_relse(bp);
		sector_size = mp->m_sb.sb_sectsize;
		goto reread;
	}

	/* Initialize per-cpu counters */
	xfs_icsb_reinit_counters(mp);

	mp->m_sb_bp = bp;
	xfs_buf_unlock(bp);
	return 0;

release_buf:
	xfs_buf_relse(bp);
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
	mp->m_agfrotor = mp->m_agirotor = 0;
	spin_lock_init(&mp->m_agirotor_lock);
	mp->m_maxagi = mp->m_sb.sb_agcount;
	mp->m_blkbit_log = sbp->sb_blocklog + XFS_NBBYLOG;
	mp->m_blkbb_log = sbp->sb_blocklog - BBSHIFT;
	mp->m_sectbb_log = sbp->sb_sectlog - BBSHIFT;
	mp->m_agno_log = xfs_highbit32(sbp->sb_agcount - 1) + 1;
	mp->m_agino_log = sbp->sb_inopblog + sbp->sb_agblklog;
	mp->m_blockmask = sbp->sb_blocksize - 1;
	mp->m_blockwsize = sbp->sb_blocksize >> XFS_WORDLOG;
	mp->m_blockwmask = mp->m_blockwsize - 1;

	mp->m_alloc_mxr[0] = xfs_allocbt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_alloc_mxr[1] = xfs_allocbt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_alloc_mnr[0] = mp->m_alloc_mxr[0] / 2;
	mp->m_alloc_mnr[1] = mp->m_alloc_mxr[1] / 2;

	mp->m_inobt_mxr[0] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_inobt_mxr[1] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_inobt_mnr[0] = mp->m_inobt_mxr[0] / 2;
	mp->m_inobt_mnr[1] = mp->m_inobt_mxr[1] / 2;

	mp->m_bmap_dmxr[0] = xfs_bmbt_maxrecs(mp, sbp->sb_blocksize, 1);
	mp->m_bmap_dmxr[1] = xfs_bmbt_maxrecs(mp, sbp->sb_blocksize, 0);
	mp->m_bmap_dmnr[0] = mp->m_bmap_dmxr[0] / 2;
	mp->m_bmap_dmnr[1] = mp->m_bmap_dmxr[1] / 2;

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

	for (index = 0; index < agcount; index++) {
		/*
		 * read the agf, then the agi. This gets us
		 * all the information we need and populates the
		 * per-ag structures for us.
		 */
		error = xfs_alloc_pagf_init(mp, NULL, index, 0);
		if (error)
			return error;

		error = xfs_ialloc_pagi_init(mp, NULL, index);
		if (error)
			return error;
		pag = xfs_perag_get(mp, index);
		ifree += pag->pagi_freecount;
		ialloc += pag->pagi_count;
		bfree += pag->pagf_freeblks;
		bfreelst += pag->pagf_flcount;
		btree += pag->pagf_btreeblks;
		xfs_perag_put(pag);
	}
	/*
	 * Overwrite incore superblock counters with just-read data
	 */
	spin_lock(&mp->m_sb_lock);
	sbp->sb_ifree = ifree;
	sbp->sb_icount = ialloc;
	sbp->sb_fdblocks = bfree + bfreelst + btree;
	spin_unlock(&mp->m_sb_lock);

	/* Fixup the per-cpu counters as well. */
	xfs_icsb_reinit_counters(mp);

	return 0;
}

/*
 * Update alignment values based on mount options and sb values
 */
STATIC int
xfs_update_alignment(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);

	if (mp->m_dalign) {
		/*
		 * If stripe unit and stripe width are not multiples
		 * of the fs blocksize turn off alignment.
		 */
		if ((BBTOB(mp->m_dalign) & mp->m_blockmask) ||
		    (BBTOB(mp->m_swidth) & mp->m_blockmask)) {
			if (mp->m_flags & XFS_MOUNT_RETERR) {
				xfs_warn(mp, "alignment check failed: "
					 "(sunit/swidth vs. blocksize)");
				return XFS_ERROR(EINVAL);
			}
			mp->m_dalign = mp->m_swidth = 0;
		} else {
			/*
			 * Convert the stripe unit and width to FSBs.
			 */
			mp->m_dalign = XFS_BB_TO_FSBT(mp, mp->m_dalign);
			if (mp->m_dalign && (sbp->sb_agblocks % mp->m_dalign)) {
				if (mp->m_flags & XFS_MOUNT_RETERR) {
					xfs_warn(mp, "alignment check failed: "
						 "(sunit/swidth vs. ag size)");
					return XFS_ERROR(EINVAL);
				}
				xfs_warn(mp,
		"stripe alignment turned off: sunit(%d)/swidth(%d) "
		"incompatible with agsize(%d)",
					mp->m_dalign, mp->m_swidth,
					sbp->sb_agblocks);

				mp->m_dalign = 0;
				mp->m_swidth = 0;
			} else if (mp->m_dalign) {
				mp->m_swidth = XFS_BB_TO_FSBT(mp, mp->m_swidth);
			} else {
				if (mp->m_flags & XFS_MOUNT_RETERR) {
					xfs_warn(mp, "alignment check failed: "
						"sunit(%d) less than bsize(%d)",
						mp->m_dalign,
						mp->m_blockmask +1);
					return XFS_ERROR(EINVAL);
				}
				mp->m_swidth = 0;
			}
		}

		/*
		 * Update superblock with new values
		 * and log changes
		 */
		if (xfs_sb_version_hasdalign(sbp)) {
			if (sbp->sb_unit != mp->m_dalign) {
				sbp->sb_unit = mp->m_dalign;
				mp->m_update_flags |= XFS_SB_UNIT;
			}
			if (sbp->sb_width != mp->m_swidth) {
				sbp->sb_width = mp->m_swidth;
				mp->m_update_flags |= XFS_SB_WIDTH;
			}
		}
	} else if ((mp->m_flags & XFS_MOUNT_NOALIGN) != XFS_MOUNT_NOALIGN &&
		    xfs_sb_version_hasdalign(&mp->m_sb)) {
			mp->m_dalign = sbp->sb_unit;
			mp->m_swidth = sbp->sb_width;
	}

	return 0;
}

/*
 * Set the maximum inode count for this filesystem
 */
STATIC void
xfs_set_maxicount(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);
	__uint64_t	icount;

	if (sbp->sb_imax_pct) {
		/*
		 * Make sure the maximum inode count is a multiple
		 * of the units we allocate inodes in.
		 */
		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		do_div(icount, mp->m_ialloc_blks);
		mp->m_maxicount = (icount * mp->m_ialloc_blks)  <<
				   sbp->sb_inopblog;
	} else {
		mp->m_maxicount = 0;
	}
}

/*
 * Set the default minimum read and write sizes unless
 * already specified in a mount option.
 * We use smaller I/O sizes when the file system
 * is being used for NFS service (wsync mount option).
 */
STATIC void
xfs_set_rw_sizes(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);
	int		readio_log, writeio_log;

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
}

/*
 * precalculate the low space thresholds for dynamic speculative preallocation.
 */
void
xfs_set_low_space_thresholds(
	struct xfs_mount	*mp)
{
	int i;

	for (i = 0; i < XFS_LOWSP_MAX; i++) {
		__uint64_t space = mp->m_sb.sb_dblocks;

		do_div(space, 100);
		mp->m_low_space[i] = space * (i + 1);
	}
}


/*
 * Set whether we're using inode alignment.
 */
STATIC void
xfs_set_inoalignment(xfs_mount_t *mp)
{
	if (xfs_sb_version_hasalign(&mp->m_sb) &&
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
}

/*
 * Check that the data (and log if separate) are an ok size.
 */
STATIC int
xfs_check_sizes(xfs_mount_t *mp)
{
	xfs_buf_t	*bp;
	xfs_daddr_t	d;

	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_dblocks) {
		xfs_warn(mp, "filesystem size mismatch detected");
		return XFS_ERROR(EFBIG);
	}
	bp = xfs_buf_read_uncached(mp->m_ddev_targp,
					d - XFS_FSS_TO_BB(mp, 1),
					XFS_FSS_TO_BB(mp, 1), 0, NULL);
	if (!bp) {
		xfs_warn(mp, "last sector read failed");
		return EIO;
	}
	xfs_buf_relse(bp);

	if (mp->m_logdev_targp != mp->m_ddev_targp) {
		d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_logblocks);
		if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_logblocks) {
			xfs_warn(mp, "log size mismatch detected");
			return XFS_ERROR(EFBIG);
		}
		bp = xfs_buf_read_uncached(mp->m_logdev_targp,
					d - XFS_FSB_TO_BB(mp, 1),
					XFS_FSB_TO_BB(mp, 1), 0, NULL);
		if (!bp) {
			xfs_warn(mp, "log device read failed");
			return EIO;
		}
		xfs_buf_relse(bp);
	}
	return 0;
}

/*
 * Clear the quotaflags in memory and in the superblock.
 */
int
xfs_mount_reset_sbqflags(
	struct xfs_mount	*mp)
{
	int			error;
	struct xfs_trans	*tp;

	mp->m_qflags = 0;

	/*
	 * It is OK to look at sb_qflags here in mount path,
	 * without m_sb_lock.
	 */
	if (mp->m_sb.sb_qflags == 0)
		return 0;
	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_qflags = 0;
	spin_unlock(&mp->m_sb_lock);

	/*
	 * If the fs is readonly, let the incore superblock run
	 * with quotas off but don't flush the update out to disk
	 */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SBCHANGE);
	error = xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
				      XFS_DEFAULT_LOG_COUNT);
	if (error) {
		xfs_trans_cancel(tp, 0);
		xfs_alert(mp, "%s: Superblock update failed!", __func__);
		return error;
	}

	xfs_mod_sb(tp, XFS_SB_QFLAGS);
	return xfs_trans_commit(tp, 0);
}

__uint64_t
xfs_default_resblks(xfs_mount_t *mp)
{
	__uint64_t resblks;

	/*
	 * We default to 5% or 8192 fsbs of space reserved, whichever is
	 * smaller.  This is intended to cover concurrent allocation
	 * transactions when we initially hit enospc. These each require a 4
	 * block reservation. Hence by default we cover roughly 2000 concurrent
	 * allocation reservations.
	 */
	resblks = mp->m_sb.sb_dblocks;
	do_div(resblks, 20);
	resblks = min_t(__uint64_t, resblks, 8192);
	return resblks;
}

/*
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
	xfs_mount_t	*mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);
	xfs_inode_t	*rip;
	__uint64_t	resblks;
	uint		quotamount = 0;
	uint		quotaflags = 0;
	int		error = 0;

	xfs_mount_common(mp, sbp);

	/*
	 * Check for a mismatched features2 values.  Older kernels
	 * read & wrote into the wrong sb offset for sb_features2
	 * on some platforms due to xfs_sb_t not being 64bit size aligned
	 * when sb_features2 was added, which made older superblock
	 * reading/writing routines swap it as a 64-bit value.
	 *
	 * For backwards compatibility, we make both slots equal.
	 *
	 * If we detect a mismatched field, we OR the set bits into the
	 * existing features2 field in case it has already been modified; we
	 * don't want to lose any features.  We then update the bad location
	 * with the ORed value so that older kernels will see any features2
	 * flags, and mark the two fields as needing updates once the
	 * transaction subsystem is online.
	 */
	if (xfs_sb_has_mismatched_features2(sbp)) {
		xfs_warn(mp, "correcting sb_features alignment problem");
		sbp->sb_features2 |= sbp->sb_bad_features2;
		sbp->sb_bad_features2 = sbp->sb_features2;
		mp->m_update_flags |= XFS_SB_FEATURES2 | XFS_SB_BAD_FEATURES2;

		/*
		 * Re-check for ATTR2 in case it was found in bad_features2
		 * slot.
		 */
		if (xfs_sb_version_hasattr2(&mp->m_sb) &&
		   !(mp->m_flags & XFS_MOUNT_NOATTR2))
			mp->m_flags |= XFS_MOUNT_ATTR2;
	}

	if (xfs_sb_version_hasattr2(&mp->m_sb) &&
	   (mp->m_flags & XFS_MOUNT_NOATTR2)) {
		xfs_sb_version_removeattr2(&mp->m_sb);
		mp->m_update_flags |= XFS_SB_FEATURES2;

		/* update sb_versionnum for the clearing of the morebits */
		if (!sbp->sb_features2)
			mp->m_update_flags |= XFS_SB_VERSIONNUM;
	}

	/*
	 * Check if sb_agblocks is aligned at stripe boundary
	 * If sb_agblocks is NOT aligned turn off m_dalign since
	 * allocator alignment is within an ag, therefore ag has
	 * to be aligned at stripe boundary.
	 */
	error = xfs_update_alignment(mp);
	if (error)
		goto out;

	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp, XFS_DATA_FORK);
	xfs_bmap_compute_maxlevels(mp, XFS_ATTR_FORK);
	xfs_ialloc_compute_maxlevels(mp);

	xfs_set_maxicount(mp);

	error = xfs_uuid_mount(mp);
	if (error)
		goto out;

	/*
	 * Set the minimum read and write sizes
	 */
	xfs_set_rw_sizes(mp);

	/* set the low space thresholds for dynamic preallocation */
	xfs_set_low_space_thresholds(mp);

	/*
	 * Set the inode cluster size.
	 * This may still be overridden by the file system
	 * block size if it is larger than the chosen cluster size.
	 */
	mp->m_inode_cluster_size = XFS_INODE_BIG_CLUSTER_SIZE;

	/*
	 * Set inode alignment fields
	 */
	xfs_set_inoalignment(mp);

	/*
	 * Check that the data (and log if separate) are an ok size.
	 */
	error = xfs_check_sizes(mp);
	if (error)
		goto out_remove_uuid;

	/*
	 * Initialize realtime fields in the mount structure
	 */
	error = xfs_rtmount_init(mp);
	if (error) {
		xfs_warn(mp, "RT mount failed");
		goto out_remove_uuid;
	}

	/*
	 *  Copies the low order bits of the timestamp and the randomly
	 *  set "sequence" number out of a UUID.
	 */
	uuid_getnodeuniq(&sbp->sb_uuid, mp->m_fixedfsid);

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
	 * Allocate and initialize the per-ag data.
	 */
	spin_lock_init(&mp->m_perag_lock);
	INIT_RADIX_TREE(&mp->m_perag_tree, GFP_ATOMIC);
	error = xfs_initialize_perag(mp, sbp->sb_agcount, &mp->m_maxagi);
	if (error) {
		xfs_warn(mp, "Failed per-ag init: %d", error);
		goto out_remove_uuid;
	}

	if (!sbp->sb_logblocks) {
		xfs_warn(mp, "no log defined");
		XFS_ERROR_REPORT("xfs_mountfs", XFS_ERRLEVEL_LOW, mp);
		error = XFS_ERROR(EFSCORRUPTED);
		goto out_free_perag;
	}

	/*
	 * log's mount-time initialization. Perform 1st part recovery if needed
	 */
	error = xfs_log_mount(mp, mp->m_logdev_targp,
			      XFS_FSB_TO_DADDR(mp, sbp->sb_logstart),
			      XFS_FSB_TO_BB(mp, sbp->sb_logblocks));
	if (error) {
		xfs_warn(mp, "log mount failed");
		goto out_fail_wait;
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
		if (error)
			goto out_fail_wait;
	}

	/*
	 * Get and sanity-check the root inode.
	 * Save the pointer to it in the mount structure.
	 */
	error = xfs_iget(mp, NULL, sbp->sb_rootino, 0, XFS_ILOCK_EXCL, &rip);
	if (error) {
		xfs_warn(mp, "failed to read root inode");
		goto out_log_dealloc;
	}

	ASSERT(rip != NULL);

	if (unlikely(!S_ISDIR(rip->i_d.di_mode))) {
		xfs_warn(mp, "corrupted root inode %llu: not a directory",
			(unsigned long long)rip->i_ino);
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		XFS_ERROR_REPORT("xfs_mountfs_int(2)", XFS_ERRLEVEL_LOW,
				 mp);
		error = XFS_ERROR(EFSCORRUPTED);
		goto out_rele_rip;
	}
	mp->m_rootip = rip;	/* save it */

	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	/*
	 * Initialize realtime inode pointers in the mount structure
	 */
	error = xfs_rtmount_inodes(mp);
	if (error) {
		/*
		 * Free up the root inode.
		 */
		xfs_warn(mp, "failed to read RT inodes");
		goto out_rele_rip;
	}

	/*
	 * If this is a read-only mount defer the superblock updates until
	 * the next remount into writeable mode.  Otherwise we would never
	 * perform the update e.g. for the root filesystem.
	 */
	if (mp->m_update_flags && !(mp->m_flags & XFS_MOUNT_RDONLY)) {
		error = xfs_mount_log_sb(mp, mp->m_update_flags);
		if (error) {
			xfs_warn(mp, "failed to write sb changes");
			goto out_rtunmount;
		}
	}

	/*
	 * Initialise the XFS quota management subsystem for this mount
	 */
	if (XFS_IS_QUOTA_RUNNING(mp)) {
		error = xfs_qm_newmount(mp, &quotamount, &quotaflags);
		if (error)
			goto out_rtunmount;
	} else {
		ASSERT(!XFS_IS_QUOTA_ON(mp));

		/*
		 * If a file system had quotas running earlier, but decided to
		 * mount without -o uquota/pquota/gquota options, revoke the
		 * quotachecked license.
		 */
		if (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT) {
			xfs_notice(mp, "resetting quota flags");
			error = xfs_mount_reset_sbqflags(mp);
			if (error)
				return error;
		}
	}

	/*
	 * Finish recovering the file system.  This part needed to be
	 * delayed until after the root and real-time bitmap inodes
	 * were consistently read in.
	 */
	error = xfs_log_mount_finish(mp);
	if (error) {
		xfs_warn(mp, "log mount finish failed");
		goto out_rtunmount;
	}

	/*
	 * Complete the quota initialisation, post-log-replay component.
	 */
	if (quotamount) {
		ASSERT(mp->m_qflags == 0);
		mp->m_qflags = quotaflags;

		xfs_qm_mount_quotas(mp);
	}

	/*
	 * Now we are mounted, reserve a small amount of unused space for
	 * privileged transactions. This is needed so that transaction
	 * space required for critical operations can dip into this pool
	 * when at ENOSPC. This is needed for operations like create with
	 * attr, unwritten extent conversion at ENOSPC, etc. Data allocations
	 * are not allowed to use this reserved space.
	 *
	 * This may drive us straight to ENOSPC on mount, but that implies
	 * we were already there on the last unmount. Warn if this occurs.
	 */
	if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
		resblks = xfs_default_resblks(mp);
		error = xfs_reserve_blocks(mp, &resblks, NULL);
		if (error)
			xfs_warn(mp,
	"Unable to allocate reserve blocks. Continuing without reserve pool.");
	}

	return 0;

 out_rtunmount:
	xfs_rtunmount_inodes(mp);
 out_rele_rip:
	IRELE(rip);
 out_log_dealloc:
	xfs_log_unmount(mp);
 out_fail_wait:
	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp)
		xfs_wait_buftarg(mp->m_logdev_targp);
	xfs_wait_buftarg(mp->m_ddev_targp);
 out_free_perag:
	xfs_free_perag(mp);
 out_remove_uuid:
	xfs_uuid_unmount(mp);
 out:
	return error;
}

/*
 * This flushes out the inodes,dquots and the superblock, unmounts the
 * log and makes sure that incore structures are freed.
 */
void
xfs_unmountfs(
	struct xfs_mount	*mp)
{
	__uint64_t		resblks;
	int			error;

	cancel_delayed_work_sync(&mp->m_eofblocks_work);

	xfs_qm_unmount_quotas(mp);
	xfs_rtunmount_inodes(mp);
	IRELE(mp->m_rootip);

	/*
	 * We can potentially deadlock here if we have an inode cluster
	 * that has been freed has its buffer still pinned in memory because
	 * the transaction is still sitting in a iclog. The stale inodes
	 * on that buffer will have their flush locks held until the
	 * transaction hits the disk and the callbacks run. the inode
	 * flush takes the flush lock unconditionally and with nothing to
	 * push out the iclog we will never get that unlocked. hence we
	 * need to force the log first.
	 */
	xfs_log_force(mp, XFS_LOG_SYNC);

	/*
	 * Flush all pending changes from the AIL.
	 */
	xfs_ail_push_all_sync(mp->m_ail);

	/*
	 * And reclaim all inodes.  At this point there should be no dirty
	 * inodes and none should be pinned or locked, but use synchronous
	 * reclaim just to be sure. We can stop background inode reclaim
	 * here as well if it is still running.
	 */
	cancel_delayed_work_sync(&mp->m_reclaim_work);
	xfs_reclaim_inodes(mp, SYNC_WAIT);

	xfs_qm_unmount(mp);

	/*
	 * Unreserve any blocks we have so that when we unmount we don't account
	 * the reserved free space as used. This is really only necessary for
	 * lazy superblock counting because it trusts the incore superblock
	 * counters to be absolutely correct on clean unmount.
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
	error = xfs_reserve_blocks(mp, &resblks, NULL);
	if (error)
		xfs_warn(mp, "Unable to free reserved block pool. "
				"Freespace may not be correct on next mount.");

	error = xfs_log_sbcount(mp);
	if (error)
		xfs_warn(mp, "Unable to update superblock counters. "
				"Freespace may not be correct on next mount.");

	xfs_log_unmount(mp);
	xfs_uuid_unmount(mp);

#if defined(DEBUG)
	xfs_errortag_clearall(mp, 0);
#endif
	xfs_free_perag(mp);
}

int
xfs_fs_writable(xfs_mount_t *mp)
{
	return !(mp->m_super->s_writers.frozen || XFS_FORCED_SHUTDOWN(mp) ||
		(mp->m_flags & XFS_MOUNT_RDONLY));
}

/*
 * xfs_log_sbcount
 *
 * Sync the superblock counters to disk.
 *
 * Note this code can be called during the process of freezing, so
 * we may need to use the transaction allocator which does not
 * block when the transaction subsystem is in its frozen state.
 */
int
xfs_log_sbcount(xfs_mount_t *mp)
{
	xfs_trans_t	*tp;
	int		error;

	if (!xfs_fs_writable(mp))
		return 0;

	xfs_icsb_sync_counters(mp, 0);

	/*
	 * we don't need to do this if we are updating the superblock
	 * counters on every modification.
	 */
	if (!xfs_sb_version_haslazysbcount(&mp->m_sb))
		return 0;

	tp = _xfs_trans_alloc(mp, XFS_TRANS_SB_COUNT, KM_SLEEP);
	error = xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
					XFS_DEFAULT_LOG_COUNT);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_mod_sb(tp, XFS_SB_IFREE | XFS_SB_ICOUNT | XFS_SB_FDBLOCKS);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
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
	xfs_sb_field_t	f;

	ASSERT(fields);
	if (!fields)
		return;
	mp = tp->t_mountp;
	bp = xfs_trans_getsb(tp, mp, 0);
	first = sizeof(xfs_sb_t);
	last = 0;

	/* translate/copy */

	xfs_sb_to_disk(XFS_BUF_TO_SBP(bp), &mp->m_sb, fields);

	/* find modified range */
	f = (xfs_sb_field_t)xfs_highbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	last = xfs_sb_info[f + 1].offset - 1;

	f = (xfs_sb_field_t)xfs_lowbit64((__uint64_t)fields);
	ASSERT((1LL << f) & XFS_SB_MOD_BITS);
	first = xfs_sb_info[f].offset;

	xfs_trans_log_buf(tp, bp, first, last);
}


/*
 * xfs_mod_incore_sb_unlocked() is a utility routine common used to apply
 * a delta to a specified field in the in-core superblock.  Simply
 * switch on the field indicated and apply the delta to that field.
 * Fields are not allowed to dip below zero, so if the delta would
 * do this do not apply it and return EINVAL.
 *
 * The m_sb_lock must be held when this routine is called.
 */
STATIC int
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
			if (lcounter >= 0) {
				mp->m_sb.sb_fdblocks = lcounter +
							XFS_ALLOC_SET_ASIDE(mp);
				return 0;
			}

			/*
			 * We are out of blocks, use any available reserved
			 * blocks if were allowed to.
			 */
			if (!rsvd)
				return XFS_ERROR(ENOSPC);

			lcounter = (long long)mp->m_resblks_avail + delta;
			if (lcounter >= 0) {
				mp->m_resblks_avail = lcounter;
				return 0;
			}
			printk_once(KERN_WARNING
				"Filesystem \"%s\": reserve blocks depleted! "
				"Consider increasing reserve pool size.",
				mp->m_fsname);
			return XFS_ERROR(ENOSPC);
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
 * is protected by the m_sb_lock.  Just use the xfs_mod_incore_sb_unlocked()
 * routine to do the work.
 */
int
xfs_mod_incore_sb(
	struct xfs_mount	*mp,
	xfs_sb_field_t		field,
	int64_t			delta,
	int			rsvd)
{
	int			status;

#ifdef HAVE_PERCPU_SB
	ASSERT(field < XFS_SBS_ICOUNT || field > XFS_SBS_FDBLOCKS);
#endif
	spin_lock(&mp->m_sb_lock);
	status = xfs_mod_incore_sb_unlocked(mp, field, delta, rsvd);
	spin_unlock(&mp->m_sb_lock);

	return status;
}

/*
 * Change more than one field in the in-core superblock structure at a time.
 *
 * The fields and changes to those fields are specified in the array of
 * xfs_mod_sb structures passed in.  Either all of the specified deltas
 * will be applied or none of them will.  If any modified field dips below 0,
 * then all modifications will be backed out and EINVAL will be returned.
 *
 * Note that this function may not be used for the superblock values that
 * are tracked with the in-memory per-cpu counters - a direct call to
 * xfs_icsb_modify_counters is required for these.
 */
int
xfs_mod_incore_sb_batch(
	struct xfs_mount	*mp,
	xfs_mod_sb_t		*msb,
	uint			nmsb,
	int			rsvd)
{
	xfs_mod_sb_t		*msbp;
	int			error = 0;

	/*
	 * Loop through the array of mod structures and apply each individually.
	 * If any fail, then back out all those which have already been applied.
	 * Do all of this within the scope of the m_sb_lock so that all of the
	 * changes will be atomic.
	 */
	spin_lock(&mp->m_sb_lock);
	for (msbp = msb; msbp < (msb + nmsb); msbp++) {
		ASSERT(msbp->msb_field < XFS_SBS_ICOUNT ||
		       msbp->msb_field > XFS_SBS_FDBLOCKS);

		error = xfs_mod_incore_sb_unlocked(mp, msbp->msb_field,
						   msbp->msb_delta, rsvd);
		if (error)
			goto unwind;
	}
	spin_unlock(&mp->m_sb_lock);
	return 0;

unwind:
	while (--msbp >= msb) {
		error = xfs_mod_incore_sb_unlocked(mp, msbp->msb_field,
						   -msbp->msb_delta, rsvd);
		ASSERT(error == 0);
	}
	spin_unlock(&mp->m_sb_lock);
	return error;
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
struct xfs_buf *
xfs_getsb(
	struct xfs_mount	*mp,
	int			flags)
{
	struct xfs_buf		*bp = mp->m_sb_bp;

	if (!xfs_buf_trylock(bp)) {
		if (flags & XBF_TRYLOCK)
			return NULL;
		xfs_buf_lock(bp);
	}

	xfs_buf_hold(bp);
	ASSERT(XFS_BUF_ISDONE(bp));
	return bp;
}

/*
 * Used to free the superblock along various error paths.
 */
void
xfs_freesb(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp = mp->m_sb_bp;

	xfs_buf_lock(bp);
	mp->m_sb_bp = NULL;
	xfs_buf_relse(bp);
}

/*
 * Used to log changes to the superblock unit and width fields which could
 * be altered by the mount options, as well as any potential sb_features2
 * fixup. Only the first superblock is updated.
 */
int
xfs_mount_log_sb(
	xfs_mount_t	*mp,
	__int64_t	fields)
{
	xfs_trans_t	*tp;
	int		error;

	ASSERT(fields & (XFS_SB_UNIT | XFS_SB_WIDTH | XFS_SB_UUID |
			 XFS_SB_FEATURES2 | XFS_SB_BAD_FEATURES2 |
			 XFS_SB_VERSIONNUM));

	tp = xfs_trans_alloc(mp, XFS_TRANS_SB_UNIT);
	error = xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
				XFS_DEFAULT_LOG_COUNT);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_mod_sb(tp, fields);
	error = xfs_trans_commit(tp, 0);
	return error;
}

/*
 * If the underlying (data/log/rt) device is readonly, there are some
 * operations that cannot proceed.
 */
int
xfs_dev_is_read_only(
	struct xfs_mount	*mp,
	char			*message)
{
	if (xfs_readonly_buftarg(mp->m_ddev_targp) ||
	    xfs_readonly_buftarg(mp->m_logdev_targp) ||
	    (mp->m_rtdev_targp && xfs_readonly_buftarg(mp->m_rtdev_targp))) {
		xfs_notice(mp, "%s required on read-only device.", message);
		xfs_notice(mp, "write access unavailable, cannot proceed.");
		return EROFS;
	}
	return 0;
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
 * when we disable a per-cpu counter, we need to drain its resources back to
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
 * 	1. m_sb_lock before picking up per-cpu locks
 * 	2. per-cpu locks always picked up via for_each_online_cpu() order
 * 	3. accurate counter sync requires m_sb_lock + per cpu locks
 * 	4. modifying per-cpu counters requires holding per-cpu lock
 * 	5. modifying global counters requires holding m_sb_lock
 *	6. enabling or disabling a counter requires holding the m_sb_lock 
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
		xfs_icsb_balance_counter(mp, XFS_SBS_ICOUNT, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_IFREE, 0);
		xfs_icsb_balance_counter(mp, XFS_SBS_FDBLOCKS, 0);
		xfs_icsb_unlock(mp);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/* Disable all the counters, then fold the dead cpu's
		 * count into the total on the global superblock and
		 * re-enable the counters. */
		xfs_icsb_lock(mp);
		spin_lock(&mp->m_sb_lock);
		xfs_icsb_disable_counter(mp, XFS_SBS_ICOUNT);
		xfs_icsb_disable_counter(mp, XFS_SBS_IFREE);
		xfs_icsb_disable_counter(mp, XFS_SBS_FDBLOCKS);

		mp->m_sb.sb_icount += cntp->icsb_icount;
		mp->m_sb.sb_ifree += cntp->icsb_ifree;
		mp->m_sb.sb_fdblocks += cntp->icsb_fdblocks;

		memset(cntp, 0, sizeof(xfs_icsb_cnts_t));

		xfs_icsb_balance_counter_locked(mp, XFS_SBS_ICOUNT, 0);
		xfs_icsb_balance_counter_locked(mp, XFS_SBS_IFREE, 0);
		xfs_icsb_balance_counter_locked(mp, XFS_SBS_FDBLOCKS, 0);
		spin_unlock(&mp->m_sb_lock);
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
	xfs_icsb_balance_counter(mp, XFS_SBS_ICOUNT, 0);
	xfs_icsb_balance_counter(mp, XFS_SBS_IFREE, 0);
	xfs_icsb_balance_counter(mp, XFS_SBS_FDBLOCKS, 0);
	xfs_icsb_unlock(mp);
}

void
xfs_icsb_destroy_counters(
	xfs_mount_t	*mp)
{
	if (mp->m_sb_cnts) {
		unregister_hotcpu_notifier(&mp->m_icsb_notifier);
		free_percpu(mp->m_sb_cnts);
	}
	mutex_destroy(&mp->m_icsb_mutex);
}

STATIC void
xfs_icsb_lock_cntr(
	xfs_icsb_cnts_t	*icsbp)
{
	while (test_and_set_bit(XFS_ICSB_FLAG_LOCK, &icsbp->icsb_flags)) {
		ndelay(1000);
	}
}

STATIC void
xfs_icsb_unlock_cntr(
	xfs_icsb_cnts_t	*icsbp)
{
	clear_bit(XFS_ICSB_FLAG_LOCK, &icsbp->icsb_flags);
}


STATIC void
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

STATIC void
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

STATIC void
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
		return;

	xfs_icsb_lock_all_counters(mp);
	if (!test_and_set_bit(field, &mp->m_icsb_counters)) {
		/* drain back to superblock */

		xfs_icsb_count(mp, &cnt, XFS_ICSB_LAZY_COUNT);
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
xfs_icsb_sync_counters_locked(
	xfs_mount_t	*mp,
	int		flags)
{
	xfs_icsb_cnts_t	cnt;

	xfs_icsb_count(mp, &cnt, flags);

	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_ICOUNT))
		mp->m_sb.sb_icount = cnt.icsb_icount;
	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_IFREE))
		mp->m_sb.sb_ifree = cnt.icsb_ifree;
	if (!xfs_icsb_counter_disabled(mp, XFS_SBS_FDBLOCKS))
		mp->m_sb.sb_fdblocks = cnt.icsb_fdblocks;
}

/*
 * Accurate update of per-cpu counters to incore superblock
 */
void
xfs_icsb_sync_counters(
	xfs_mount_t	*mp,
	int		flags)
{
	spin_lock(&mp->m_sb_lock);
	xfs_icsb_sync_counters_locked(mp, flags);
	spin_unlock(&mp->m_sb_lock);
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
xfs_icsb_balance_counter_locked(
	xfs_mount_t	*mp,
	xfs_sb_field_t  field,
	int		min_per_cpu)
{
	uint64_t	count, resid;
	int		weight = num_online_cpus();
	uint64_t	min = (uint64_t)min_per_cpu;

	/* disable counter and sync counter */
	xfs_icsb_disable_counter(mp, field);

	/* update counters  - first CPU gets residual*/
	switch (field) {
	case XFS_SBS_ICOUNT:
		count = mp->m_sb.sb_icount;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_INO_CNTR_REENABLE))
			return;
		break;
	case XFS_SBS_IFREE:
		count = mp->m_sb.sb_ifree;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_INO_CNTR_REENABLE))
			return;
		break;
	case XFS_SBS_FDBLOCKS:
		count = mp->m_sb.sb_fdblocks;
		resid = do_div(count, weight);
		if (count < max(min, XFS_ICSB_FDBLK_CNTR_REENABLE(mp)))
			return;
		break;
	default:
		BUG();
		count = resid = 0;	/* quiet, gcc */
		break;
	}

	xfs_icsb_enable_counter(mp, field, count, resid);
}

STATIC void
xfs_icsb_balance_counter(
	xfs_mount_t	*mp,
	xfs_sb_field_t  fields,
	int		min_per_cpu)
{
	spin_lock(&mp->m_sb_lock);
	xfs_icsb_balance_counter_locked(mp, fields, min_per_cpu);
	spin_unlock(&mp->m_sb_lock);
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
	int		ret = 0;

	might_sleep();
again:
	preempt_disable();
	icsbp = this_cpu_ptr(mp->m_sb_cnts);

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
	preempt_enable();
	return 0;

slow_path:
	preempt_enable();

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
	 * here even though we need to get the m_sb_lock. Doing so
	 * will cause us to re-enter this function and deadlock.
	 * Hence we get the m_sb_lock ourselves and then call
	 * xfs_mod_incore_sb_unlocked() as the unlocked path operates
	 * directly on the global counters.
	 */
	spin_lock(&mp->m_sb_lock);
	ret = xfs_mod_incore_sb_unlocked(mp, field, delta, rsvd);
	spin_unlock(&mp->m_sb_lock);

	/*
	 * Now that we've modified the global superblock, we
	 * may be able to re-enable the distributed counters
	 * (e.g. lots of space just got freed). After that
	 * we are done.
	 */
	if (ret != ENOSPC)
		xfs_icsb_balance_counter(mp, field, 0);
	xfs_icsb_unlock(mp);
	return ret;

balance_counter:
	xfs_icsb_unlock_cntr(icsbp);
	preempt_enable();

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
	xfs_icsb_balance_counter(mp, field, delta);
	xfs_icsb_unlock(mp);
	goto again;
}

#endif
