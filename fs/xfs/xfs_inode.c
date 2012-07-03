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
#include <linux/log2.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_utils.h"
#include "xfs_quota.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"

kmem_zone_t *xfs_ifork_zone;
kmem_zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate_extents().  This is the maximum number of extents
 * freed from a file in a single transaction.
 */
#define	XFS_ITRUNC_MAX_EXTENTS	2

STATIC int xfs_iflush_int(xfs_inode_t *, xfs_buf_t *);
STATIC int xfs_iformat_local(xfs_inode_t *, xfs_dinode_t *, int, int);
STATIC int xfs_iformat_extents(xfs_inode_t *, xfs_dinode_t *, int);
STATIC int xfs_iformat_btree(xfs_inode_t *, xfs_dinode_t *, int);

/*
 * helper function to extract extent size hint from inode
 */
xfs_extlen_t
xfs_get_extsz_hint(
	struct xfs_inode	*ip)
{
	if ((ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE) && ip->i_d.di_extsize)
		return ip->i_d.di_extsize;
	if (XFS_IS_REALTIME_INODE(ip))
		return ip->i_mount->m_sb.sb_rextsize;
	return 0;
}

#ifdef DEBUG
/*
 * Make sure that the extents in the given memory buffer
 * are valid.
 */
STATIC void
xfs_validate_extents(
	xfs_ifork_t		*ifp,
	int			nrecs,
	xfs_exntfmt_t		fmt)
{
	xfs_bmbt_irec_t		irec;
	xfs_bmbt_rec_host_t	rec;
	int			i;

	for (i = 0; i < nrecs; i++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
		rec.l0 = get_unaligned(&ep->l0);
		rec.l1 = get_unaligned(&ep->l1);
		xfs_bmbt_get_all(&rec, &irec);
		if (fmt == XFS_EXTFMT_NOSTATE)
			ASSERT(irec.br_state == XFS_EXT_NORM);
	}
}
#else /* DEBUG */
#define xfs_validate_extents(ifp, nrecs, fmt)
#endif /* DEBUG */

/*
 * Check that none of the inode's in the buffer have a next
 * unlinked field of 0.
 */
#if defined(DEBUG)
void
xfs_inobp_check(
	xfs_mount_t	*mp,
	xfs_buf_t	*bp)
{
	int		i;
	int		j;
	xfs_dinode_t	*dip;

	j = mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog;

	for (i = 0; i < j; i++) {
		dip = (xfs_dinode_t *)xfs_buf_offset(bp,
					i * mp->m_sb.sb_inodesize);
		if (!dip->di_next_unlinked)  {
			xfs_alert(mp,
	"Detected bogus zero next_unlinked field in incore inode buffer 0x%p.",
				bp);
			ASSERT(dip->di_next_unlinked);
		}
	}
}
#endif

/*
 * This routine is called to map an inode to the buffer containing the on-disk
 * version of the inode.  It returns a pointer to the buffer containing the
 * on-disk inode in the bpp parameter, and in the dipp parameter it returns a
 * pointer to the on-disk inode within that buffer.
 *
 * If a non-zero error is returned, then the contents of bpp and dipp are
 * undefined.
 */
int
xfs_imap_to_bp(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_imap		*imap,
	struct xfs_dinode	**dipp,
	struct xfs_buf		**bpp,
	uint			buf_flags,
	uint			iget_flags)
{
	struct xfs_buf		*bp;
	int			error;
	int			i;
	int			ni;

	buf_flags |= XBF_UNMAPPED;
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, imap->im_blkno,
				   (int)imap->im_len, buf_flags, &bp);
	if (error) {
		if (error != EAGAIN) {
			xfs_warn(mp,
				"%s: xfs_trans_read_buf() returned error %d.",
				__func__, error);
		} else {
			ASSERT(buf_flags & XBF_TRYLOCK);
		}
		return error;
	}

	/*
	 * Validate the magic number and version of every inode in the buffer
	 * (if DEBUG kernel) or the first inode in the buffer, otherwise.
	 */
#ifdef DEBUG
	ni = BBTOB(imap->im_len) >> mp->m_sb.sb_inodelog;
#else	/* usual case */
	ni = 1;
#endif

	for (i = 0; i < ni; i++) {
		int		di_ok;
		xfs_dinode_t	*dip;

		dip = (xfs_dinode_t *)xfs_buf_offset(bp,
					(i << mp->m_sb.sb_inodelog));
		di_ok = dip->di_magic == cpu_to_be16(XFS_DINODE_MAGIC) &&
			    XFS_DINODE_GOOD_VERSION(dip->di_version);
		if (unlikely(XFS_TEST_ERROR(!di_ok, mp,
						XFS_ERRTAG_ITOBP_INOTOBP,
						XFS_RANDOM_ITOBP_INOTOBP))) {
			if (iget_flags & XFS_IGET_UNTRUSTED) {
				xfs_trans_brelse(tp, bp);
				return XFS_ERROR(EINVAL);
			}
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_HIGH,
					     mp, dip);
#ifdef DEBUG
			xfs_emerg(mp,
				"bad inode magic/vsn daddr %lld #%d (magic=%x)",
				(unsigned long long)imap->im_blkno, i,
				be16_to_cpu(dip->di_magic));
			ASSERT(0);
#endif
			xfs_trans_brelse(tp, bp);
			return XFS_ERROR(EFSCORRUPTED);
		}
	}

	xfs_inobp_check(mp, bp);

	*bpp = bp;
	*dipp = (struct xfs_dinode *)xfs_buf_offset(bp, imap->im_boffset);
	return 0;
}

/*
 * Move inode type and inode format specific information from the
 * on-disk inode to the in-core inode.  For fifos, devs, and sockets
 * this means set if_rdev to the proper value.  For files, directories,
 * and symlinks this means to bring in the in-line data or extent
 * pointers.  For a file in B-tree format, only the root is immediately
 * brought in-core.  The rest will be in-lined in if_extents when it
 * is first referenced (see xfs_iread_extents()).
 */
STATIC int
xfs_iformat(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip)
{
	xfs_attr_shortform_t	*atp;
	int			size;
	int			error = 0;
	xfs_fsize_t             di_size;

	if (unlikely(be32_to_cpu(dip->di_nextents) +
		     be16_to_cpu(dip->di_anextents) >
		     be64_to_cpu(dip->di_nblocks))) {
		xfs_warn(ip->i_mount,
			"corrupt dinode %Lu, extent total = %d, nblocks = %Lu.",
			(unsigned long long)ip->i_ino,
			(int)(be32_to_cpu(dip->di_nextents) +
			      be16_to_cpu(dip->di_anextents)),
			(unsigned long long)
				be64_to_cpu(dip->di_nblocks));
		XFS_CORRUPTION_ERROR("xfs_iformat(1)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	if (unlikely(dip->di_forkoff > ip->i_mount->m_sb.sb_inodesize)) {
		xfs_warn(ip->i_mount, "corrupt dinode %Lu, forkoff = 0x%x.",
			(unsigned long long)ip->i_ino,
			dip->di_forkoff);
		XFS_CORRUPTION_ERROR("xfs_iformat(2)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	if (unlikely((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) &&
		     !ip->i_mount->m_rtdev_targp)) {
		xfs_warn(ip->i_mount,
			"corrupt dinode %Lu, has realtime flag set.",
			ip->i_ino);
		XFS_CORRUPTION_ERROR("xfs_iformat(realtime)",
				     XFS_ERRLEVEL_LOW, ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		if (unlikely(dip->di_format != XFS_DINODE_FMT_DEV)) {
			XFS_CORRUPTION_ERROR("xfs_iformat(3)", XFS_ERRLEVEL_LOW,
					      ip->i_mount, dip);
			return XFS_ERROR(EFSCORRUPTED);
		}
		ip->i_d.di_size = 0;
		ip->i_df.if_u2.if_rdev = xfs_dinode_get_rdev(dip);
		break;

	case S_IFREG:
	case S_IFLNK:
	case S_IFDIR:
		switch (dip->di_format) {
		case XFS_DINODE_FMT_LOCAL:
			/*
			 * no local regular files yet
			 */
			if (unlikely(S_ISREG(be16_to_cpu(dip->di_mode)))) {
				xfs_warn(ip->i_mount,
			"corrupt inode %Lu (local format for regular file).",
					(unsigned long long) ip->i_ino);
				XFS_CORRUPTION_ERROR("xfs_iformat(4)",
						     XFS_ERRLEVEL_LOW,
						     ip->i_mount, dip);
				return XFS_ERROR(EFSCORRUPTED);
			}

			di_size = be64_to_cpu(dip->di_size);
			if (unlikely(di_size > XFS_DFORK_DSIZE(dip, ip->i_mount))) {
				xfs_warn(ip->i_mount,
			"corrupt inode %Lu (bad size %Ld for local inode).",
					(unsigned long long) ip->i_ino,
					(long long) di_size);
				XFS_CORRUPTION_ERROR("xfs_iformat(5)",
						     XFS_ERRLEVEL_LOW,
						     ip->i_mount, dip);
				return XFS_ERROR(EFSCORRUPTED);
			}

			size = (int)di_size;
			error = xfs_iformat_local(ip, dip, XFS_DATA_FORK, size);
			break;
		case XFS_DINODE_FMT_EXTENTS:
			error = xfs_iformat_extents(ip, dip, XFS_DATA_FORK);
			break;
		case XFS_DINODE_FMT_BTREE:
			error = xfs_iformat_btree(ip, dip, XFS_DATA_FORK);
			break;
		default:
			XFS_ERROR_REPORT("xfs_iformat(6)", XFS_ERRLEVEL_LOW,
					 ip->i_mount);
			return XFS_ERROR(EFSCORRUPTED);
		}
		break;

	default:
		XFS_ERROR_REPORT("xfs_iformat(7)", XFS_ERRLEVEL_LOW, ip->i_mount);
		return XFS_ERROR(EFSCORRUPTED);
	}
	if (error) {
		return error;
	}
	if (!XFS_DFORK_Q(dip))
		return 0;

	ASSERT(ip->i_afp == NULL);
	ip->i_afp = kmem_zone_zalloc(xfs_ifork_zone, KM_SLEEP | KM_NOFS);

	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_LOCAL:
		atp = (xfs_attr_shortform_t *)XFS_DFORK_APTR(dip);
		size = be16_to_cpu(atp->hdr.totsize);

		if (unlikely(size < sizeof(struct xfs_attr_sf_hdr))) {
			xfs_warn(ip->i_mount,
				"corrupt inode %Lu (bad attr fork size %Ld).",
				(unsigned long long) ip->i_ino,
				(long long) size);
			XFS_CORRUPTION_ERROR("xfs_iformat(8)",
					     XFS_ERRLEVEL_LOW,
					     ip->i_mount, dip);
			return XFS_ERROR(EFSCORRUPTED);
		}

		error = xfs_iformat_local(ip, dip, XFS_ATTR_FORK, size);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		error = xfs_iformat_extents(ip, dip, XFS_ATTR_FORK);
		break;
	case XFS_DINODE_FMT_BTREE:
		error = xfs_iformat_btree(ip, dip, XFS_ATTR_FORK);
		break;
	default:
		error = XFS_ERROR(EFSCORRUPTED);
		break;
	}
	if (error) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
	}
	return error;
}

/*
 * The file is in-lined in the on-disk inode.
 * If it fits into if_inline_data, then copy
 * it there, otherwise allocate a buffer for it
 * and copy the data there.  Either way, set
 * if_data to point at the data.
 * If we allocate a buffer for the data, make
 * sure that its size is a multiple of 4 and
 * record the real size in i_real_bytes.
 */
STATIC int
xfs_iformat_local(
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip,
	int		whichfork,
	int		size)
{
	xfs_ifork_t	*ifp;
	int		real_size;

	/*
	 * If the size is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_warn(ip->i_mount,
	"corrupt inode %Lu (bad size %d for local fork, size = %d).",
			(unsigned long long) ip->i_ino, size,
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork));
		XFS_CORRUPTION_ERROR("xfs_iformat_local", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}
	ifp = XFS_IFORK_PTR(ip, whichfork);
	real_size = 0;
	if (size == 0)
		ifp->if_u1.if_data = NULL;
	else if (size <= sizeof(ifp->if_u2.if_inline_data))
		ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
	else {
		real_size = roundup(size, 4);
		ifp->if_u1.if_data = kmem_alloc(real_size, KM_SLEEP | KM_NOFS);
	}
	ifp->if_bytes = size;
	ifp->if_real_bytes = real_size;
	if (size)
		memcpy(ifp->if_u1.if_data, XFS_DFORK_PTR(dip, whichfork), size);
	ifp->if_flags &= ~XFS_IFEXTENTS;
	ifp->if_flags |= XFS_IFINLINE;
	return 0;
}

/*
 * The file consists of a set of extents all
 * of which fit into the on-disk inode.
 * If there are few enough extents to fit into
 * the if_inline_ext, then copy them there.
 * Otherwise allocate a buffer for them and copy
 * them into it.  Either way, set if_extents
 * to point at the extents.
 */
STATIC int
xfs_iformat_extents(
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip,
	int		whichfork)
{
	xfs_bmbt_rec_t	*dp;
	xfs_ifork_t	*ifp;
	int		nex;
	int		size;
	int		i;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	nex = XFS_DFORK_NEXTENTS(dip, whichfork);
	size = nex * (uint)sizeof(xfs_bmbt_rec_t);

	/*
	 * If the number of extents is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size < 0 || size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_warn(ip->i_mount, "corrupt inode %Lu ((a)extents = %d).",
			(unsigned long long) ip->i_ino, nex);
		XFS_CORRUPTION_ERROR("xfs_iformat_extents(1)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	ifp->if_real_bytes = 0;
	if (nex == 0)
		ifp->if_u1.if_extents = NULL;
	else if (nex <= XFS_INLINE_EXTS)
		ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
	else
		xfs_iext_add(ifp, 0, nex);

	ifp->if_bytes = size;
	if (size) {
		dp = (xfs_bmbt_rec_t *) XFS_DFORK_PTR(dip, whichfork);
		xfs_validate_extents(ifp, nex, XFS_EXTFMT_INODE(ip));
		for (i = 0; i < nex; i++, dp++) {
			xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
			ep->l0 = get_unaligned_be64(&dp->l0);
			ep->l1 = get_unaligned_be64(&dp->l1);
		}
		XFS_BMAP_TRACE_EXLIST(ip, nex, whichfork);
		if (whichfork != XFS_DATA_FORK ||
			XFS_EXTFMT_INODE(ip) == XFS_EXTFMT_NOSTATE)
				if (unlikely(xfs_check_nostate_extents(
				    ifp, 0, nex))) {
					XFS_ERROR_REPORT("xfs_iformat_extents(2)",
							 XFS_ERRLEVEL_LOW,
							 ip->i_mount);
					return XFS_ERROR(EFSCORRUPTED);
				}
	}
	ifp->if_flags |= XFS_IFEXTENTS;
	return 0;
}

/*
 * The file has too many extents to fit into
 * the inode, so they are in B-tree format.
 * Allocate a buffer for the root of the B-tree
 * and copy the root into it.  The i_extents
 * field will remain NULL until all of the
 * extents are read in (when they are needed).
 */
STATIC int
xfs_iformat_btree(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
	int			whichfork)
{
	xfs_bmdr_block_t	*dfp;
	xfs_ifork_t		*ifp;
	/* REFERENCED */
	int			nrecs;
	int			size;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	dfp = (xfs_bmdr_block_t *)XFS_DFORK_PTR(dip, whichfork);
	size = XFS_BMAP_BROOT_SPACE(dfp);
	nrecs = be16_to_cpu(dfp->bb_numrecs);

	/*
	 * blow out if -- fork has less extents than can fit in
	 * fork (fork shouldn't be a btree format), root btree
	 * block has more records than can fit into the fork,
	 * or the number of extents is greater than the number of
	 * blocks.
	 */
	if (unlikely(XFS_IFORK_NEXTENTS(ip, whichfork) <=
			XFS_IFORK_MAXEXT(ip, whichfork) ||
		     XFS_BMDR_SPACE_CALC(nrecs) >
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork) ||
		     XFS_IFORK_NEXTENTS(ip, whichfork) > ip->i_d.di_nblocks)) {
		xfs_warn(ip->i_mount, "corrupt inode %Lu (btree).",
			(unsigned long long) ip->i_ino);
		XFS_CORRUPTION_ERROR("xfs_iformat_btree", XFS_ERRLEVEL_LOW,
				 ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	ifp->if_broot_bytes = size;
	ifp->if_broot = kmem_alloc(size, KM_SLEEP | KM_NOFS);
	ASSERT(ifp->if_broot != NULL);
	/*
	 * Copy and convert from the on-disk structure
	 * to the in-memory structure.
	 */
	xfs_bmdr_to_bmbt(ip->i_mount, dfp,
			 XFS_DFORK_SIZE(dip, ip->i_mount, whichfork),
			 ifp->if_broot, size);
	ifp->if_flags &= ~XFS_IFEXTENTS;
	ifp->if_flags |= XFS_IFBROOT;

	return 0;
}

STATIC void
xfs_dinode_from_disk(
	xfs_icdinode_t		*to,
	xfs_dinode_t		*from)
{
	to->di_magic = be16_to_cpu(from->di_magic);
	to->di_mode = be16_to_cpu(from->di_mode);
	to->di_version = from ->di_version;
	to->di_format = from->di_format;
	to->di_onlink = be16_to_cpu(from->di_onlink);
	to->di_uid = be32_to_cpu(from->di_uid);
	to->di_gid = be32_to_cpu(from->di_gid);
	to->di_nlink = be32_to_cpu(from->di_nlink);
	to->di_projid_lo = be16_to_cpu(from->di_projid_lo);
	to->di_projid_hi = be16_to_cpu(from->di_projid_hi);
	memcpy(to->di_pad, from->di_pad, sizeof(to->di_pad));
	to->di_flushiter = be16_to_cpu(from->di_flushiter);
	to->di_atime.t_sec = be32_to_cpu(from->di_atime.t_sec);
	to->di_atime.t_nsec = be32_to_cpu(from->di_atime.t_nsec);
	to->di_mtime.t_sec = be32_to_cpu(from->di_mtime.t_sec);
	to->di_mtime.t_nsec = be32_to_cpu(from->di_mtime.t_nsec);
	to->di_ctime.t_sec = be32_to_cpu(from->di_ctime.t_sec);
	to->di_ctime.t_nsec = be32_to_cpu(from->di_ctime.t_nsec);
	to->di_size = be64_to_cpu(from->di_size);
	to->di_nblocks = be64_to_cpu(from->di_nblocks);
	to->di_extsize = be32_to_cpu(from->di_extsize);
	to->di_nextents = be32_to_cpu(from->di_nextents);
	to->di_anextents = be16_to_cpu(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat	= from->di_aformat;
	to->di_dmevmask	= be32_to_cpu(from->di_dmevmask);
	to->di_dmstate	= be16_to_cpu(from->di_dmstate);
	to->di_flags	= be16_to_cpu(from->di_flags);
	to->di_gen	= be32_to_cpu(from->di_gen);
}

void
xfs_dinode_to_disk(
	xfs_dinode_t		*to,
	xfs_icdinode_t		*from)
{
	to->di_magic = cpu_to_be16(from->di_magic);
	to->di_mode = cpu_to_be16(from->di_mode);
	to->di_version = from ->di_version;
	to->di_format = from->di_format;
	to->di_onlink = cpu_to_be16(from->di_onlink);
	to->di_uid = cpu_to_be32(from->di_uid);
	to->di_gid = cpu_to_be32(from->di_gid);
	to->di_nlink = cpu_to_be32(from->di_nlink);
	to->di_projid_lo = cpu_to_be16(from->di_projid_lo);
	to->di_projid_hi = cpu_to_be16(from->di_projid_hi);
	memcpy(to->di_pad, from->di_pad, sizeof(to->di_pad));
	to->di_flushiter = cpu_to_be16(from->di_flushiter);
	to->di_atime.t_sec = cpu_to_be32(from->di_atime.t_sec);
	to->di_atime.t_nsec = cpu_to_be32(from->di_atime.t_nsec);
	to->di_mtime.t_sec = cpu_to_be32(from->di_mtime.t_sec);
	to->di_mtime.t_nsec = cpu_to_be32(from->di_mtime.t_nsec);
	to->di_ctime.t_sec = cpu_to_be32(from->di_ctime.t_sec);
	to->di_ctime.t_nsec = cpu_to_be32(from->di_ctime.t_nsec);
	to->di_size = cpu_to_be64(from->di_size);
	to->di_nblocks = cpu_to_be64(from->di_nblocks);
	to->di_extsize = cpu_to_be32(from->di_extsize);
	to->di_nextents = cpu_to_be32(from->di_nextents);
	to->di_anextents = cpu_to_be16(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = cpu_to_be32(from->di_dmevmask);
	to->di_dmstate = cpu_to_be16(from->di_dmstate);
	to->di_flags = cpu_to_be16(from->di_flags);
	to->di_gen = cpu_to_be32(from->di_gen);
}

STATIC uint
_xfs_dic2xflags(
	__uint16_t		di_flags)
{
	uint			flags = 0;

	if (di_flags & XFS_DIFLAG_ANY) {
		if (di_flags & XFS_DIFLAG_REALTIME)
			flags |= XFS_XFLAG_REALTIME;
		if (di_flags & XFS_DIFLAG_PREALLOC)
			flags |= XFS_XFLAG_PREALLOC;
		if (di_flags & XFS_DIFLAG_IMMUTABLE)
			flags |= XFS_XFLAG_IMMUTABLE;
		if (di_flags & XFS_DIFLAG_APPEND)
			flags |= XFS_XFLAG_APPEND;
		if (di_flags & XFS_DIFLAG_SYNC)
			flags |= XFS_XFLAG_SYNC;
		if (di_flags & XFS_DIFLAG_NOATIME)
			flags |= XFS_XFLAG_NOATIME;
		if (di_flags & XFS_DIFLAG_NODUMP)
			flags |= XFS_XFLAG_NODUMP;
		if (di_flags & XFS_DIFLAG_RTINHERIT)
			flags |= XFS_XFLAG_RTINHERIT;
		if (di_flags & XFS_DIFLAG_PROJINHERIT)
			flags |= XFS_XFLAG_PROJINHERIT;
		if (di_flags & XFS_DIFLAG_NOSYMLINKS)
			flags |= XFS_XFLAG_NOSYMLINKS;
		if (di_flags & XFS_DIFLAG_EXTSIZE)
			flags |= XFS_XFLAG_EXTSIZE;
		if (di_flags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= XFS_XFLAG_EXTSZINHERIT;
		if (di_flags & XFS_DIFLAG_NODEFRAG)
			flags |= XFS_XFLAG_NODEFRAG;
		if (di_flags & XFS_DIFLAG_FILESTREAM)
			flags |= XFS_XFLAG_FILESTREAM;
	}

	return flags;
}

uint
xfs_ip2xflags(
	xfs_inode_t		*ip)
{
	xfs_icdinode_t		*dic = &ip->i_d;

	return _xfs_dic2xflags(dic->di_flags) |
				(XFS_IFORK_Q(ip) ? XFS_XFLAG_HASATTR : 0);
}

uint
xfs_dic2xflags(
	xfs_dinode_t		*dip)
{
	return _xfs_dic2xflags(be16_to_cpu(dip->di_flags)) |
				(XFS_DFORK_Q(dip) ? XFS_XFLAG_HASATTR : 0);
}

/*
 * Read the disk inode attributes into the in-core inode structure.
 */
int
xfs_iread(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		iget_flags)
{
	xfs_buf_t	*bp;
	xfs_dinode_t	*dip;
	int		error;

	/*
	 * Fill in the location information in the in-core inode.
	 */
	error = xfs_imap(mp, tp, ip->i_ino, &ip->i_imap, iget_flags);
	if (error)
		return error;

	/*
	 * Get pointers to the on-disk inode and the buffer containing it.
	 */
	error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &bp, 0, iget_flags);
	if (error)
		return error;

	/*
	 * If we got something that isn't an inode it means someone
	 * (nfs or dmi) has a stale handle.
	 */
	if (dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC)) {
#ifdef DEBUG
		xfs_alert(mp,
			"%s: dip->di_magic (0x%x) != XFS_DINODE_MAGIC (0x%x)",
			__func__, be16_to_cpu(dip->di_magic), XFS_DINODE_MAGIC);
#endif /* DEBUG */
		error = XFS_ERROR(EINVAL);
		goto out_brelse;
	}

	/*
	 * If the on-disk inode is already linked to a directory
	 * entry, copy all of the inode into the in-core inode.
	 * xfs_iformat() handles copying in the inode format
	 * specific information.
	 * Otherwise, just get the truly permanent information.
	 */
	if (dip->di_mode) {
		xfs_dinode_from_disk(&ip->i_d, dip);
		error = xfs_iformat(ip, dip);
		if (error)  {
#ifdef DEBUG
			xfs_alert(mp, "%s: xfs_iformat() returned error %d",
				__func__, error);
#endif /* DEBUG */
			goto out_brelse;
		}
	} else {
		ip->i_d.di_magic = be16_to_cpu(dip->di_magic);
		ip->i_d.di_version = dip->di_version;
		ip->i_d.di_gen = be32_to_cpu(dip->di_gen);
		ip->i_d.di_flushiter = be16_to_cpu(dip->di_flushiter);
		/*
		 * Make sure to pull in the mode here as well in
		 * case the inode is released without being used.
		 * This ensures that xfs_inactive() will see that
		 * the inode is already free and not try to mess
		 * with the uninitialized part of it.
		 */
		ip->i_d.di_mode = 0;
	}

	/*
	 * The inode format changed when we moved the link count and
	 * made it 32 bits long.  If this is an old format inode,
	 * convert it in memory to look like a new one.  If it gets
	 * flushed to disk we will convert back before flushing or
	 * logging it.  We zero out the new projid field and the old link
	 * count field.  We'll handle clearing the pad field (the remains
	 * of the old uuid field) when we actually convert the inode to
	 * the new format. We don't change the version number so that we
	 * can distinguish this from a real new format inode.
	 */
	if (ip->i_d.di_version == 1) {
		ip->i_d.di_nlink = ip->i_d.di_onlink;
		ip->i_d.di_onlink = 0;
		xfs_set_projid(ip, 0);
	}

	ip->i_delayed_blks = 0;

	/*
	 * Mark the buffer containing the inode as something to keep
	 * around for a while.  This helps to keep recently accessed
	 * meta-data in-core longer.
	 */
	xfs_buf_set_ref(bp, XFS_INO_REF);

	/*
	 * Use xfs_trans_brelse() to release the buffer containing the
	 * on-disk inode, because it was acquired with xfs_trans_read_buf()
	 * in xfs_imap_to_bp() above.  If tp is NULL, this is just a normal
	 * brelse().  If we're within a transaction, then xfs_trans_brelse()
	 * will only release the buffer if it is not dirty within the
	 * transaction.  It will be OK to release the buffer in this case,
	 * because inodes on disk are never destroyed and we will be
	 * locking the new in-core inode before putting it in the hash
	 * table where other processes can find it.  Thus we don't have
	 * to worry about the inode being changed just because we released
	 * the buffer.
	 */
 out_brelse:
	xfs_trans_brelse(tp, bp);
	return error;
}

/*
 * Read in extents from a btree-format inode.
 * Allocate and fill in if_extents.  Real work is done in xfs_bmap.c.
 */
int
xfs_iread_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	int		whichfork)
{
	int		error;
	xfs_ifork_t	*ifp;
	xfs_extnum_t	nextents;

	if (unlikely(XFS_IFORK_FORMAT(ip, whichfork) != XFS_DINODE_FMT_BTREE)) {
		XFS_ERROR_REPORT("xfs_iread_extents", XFS_ERRLEVEL_LOW,
				 ip->i_mount);
		return XFS_ERROR(EFSCORRUPTED);
	}
	nextents = XFS_IFORK_NEXTENTS(ip, whichfork);
	ifp = XFS_IFORK_PTR(ip, whichfork);

	/*
	 * We know that the size is valid (it's checked in iformat_btree)
	 */
	ifp->if_bytes = ifp->if_real_bytes = 0;
	ifp->if_flags |= XFS_IFEXTENTS;
	xfs_iext_add(ifp, 0, nextents);
	error = xfs_bmap_read_extents(tp, ip, whichfork);
	if (error) {
		xfs_iext_destroy(ifp);
		ifp->if_flags &= ~XFS_IFEXTENTS;
		return error;
	}
	xfs_validate_extents(ifp, nextents, XFS_EXTFMT_INODE(ip));
	return 0;
}

/*
 * Allocate an inode on disk and return a copy of its in-core version.
 * The in-core inode is locked exclusively.  Set mode, nlink, and rdev
 * appropriately within the inode.  The uid and gid for the inode are
 * set according to the contents of the given cred structure.
 *
 * Use xfs_dialloc() to allocate the on-disk inode. If xfs_dialloc()
 * has a free inode available, call xfs_iget()
 * to obtain the in-core version of the allocated inode.  Finally,
 * fill in the inode and log its initial contents.  In this case,
 * ialloc_context would be set to NULL and call_again set to false.
 *
 * If xfs_dialloc() does not have an available inode,
 * it will replenish its supply by doing an allocation. Since we can
 * only do one allocation within a transaction without deadlocks, we
 * must commit the current transaction before returning the inode itself.
 * In this case, therefore, we will set call_again to true and return.
 * The caller should then commit the current transaction, start a new
 * transaction, and call xfs_ialloc() again to actually get the inode.
 *
 * To ensure that some other process does not grab the inode that
 * was allocated during the first call to xfs_ialloc(), this routine
 * also returns the [locked] bp pointing to the head of the freelist
 * as ialloc_context.  The caller should hold this buffer across
 * the commit and pass it back into this routine on the second call.
 *
 * If we are allocating quota inodes, we do not have a parent inode
 * to attach to or associate with (i.e. pip == NULL) because they
 * are not linked into the directory structure - they are attached
 * directly to the superblock - and so have no parent.
 */
int
xfs_ialloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*pip,
	umode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	prid_t		prid,
	int		okalloc,
	xfs_buf_t	**ialloc_context,
	boolean_t	*call_again,
	xfs_inode_t	**ipp)
{
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	uint		flags;
	int		error;
	timespec_t	tv;
	int		filestreams = 0;

	/*
	 * Call the space management code to pick
	 * the on-disk inode to be allocated.
	 */
	error = xfs_dialloc(tp, pip ? pip->i_ino : 0, mode, okalloc,
			    ialloc_context, call_again, &ino);
	if (error)
		return error;
	if (*call_again || ino == NULLFSINO) {
		*ipp = NULL;
		return 0;
	}
	ASSERT(*ialloc_context == NULL);

	/*
	 * Get the in-core inode with the lock held exclusively.
	 * This is because we're setting fields here we need
	 * to prevent others from looking at until we're done.
	 */
	error = xfs_iget(tp->t_mountp, tp, ino, XFS_IGET_CREATE,
			 XFS_ILOCK_EXCL, &ip);
	if (error)
		return error;
	ASSERT(ip != NULL);

	ip->i_d.di_mode = mode;
	ip->i_d.di_onlink = 0;
	ip->i_d.di_nlink = nlink;
	ASSERT(ip->i_d.di_nlink == nlink);
	ip->i_d.di_uid = current_fsuid();
	ip->i_d.di_gid = current_fsgid();
	xfs_set_projid(ip, prid);
	memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));

	/*
	 * If the superblock version is up to where we support new format
	 * inodes and this is currently an old format inode, then change
	 * the inode version number now.  This way we only do the conversion
	 * here rather than here and in the flush/logging code.
	 */
	if (xfs_sb_version_hasnlink(&tp->t_mountp->m_sb) &&
	    ip->i_d.di_version == 1) {
		ip->i_d.di_version = 2;
		/*
		 * We've already zeroed the old link count, the projid field,
		 * and the pad field.
		 */
	}

	/*
	 * Project ids won't be stored on disk if we are using a version 1 inode.
	 */
	if ((prid != 0) && (ip->i_d.di_version == 1))
		xfs_bump_ino_vers2(tp, ip);

	if (pip && XFS_INHERIT_GID(pip)) {
		ip->i_d.di_gid = pip->i_d.di_gid;
		if ((pip->i_d.di_mode & S_ISGID) && S_ISDIR(mode)) {
			ip->i_d.di_mode |= S_ISGID;
		}
	}

	/*
	 * If the group ID of the new file does not match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if ((irix_sgid_inherit) &&
	    (ip->i_d.di_mode & S_ISGID) &&
	    (!in_group_p((gid_t)ip->i_d.di_gid))) {
		ip->i_d.di_mode &= ~S_ISGID;
	}

	ip->i_d.di_size = 0;
	ip->i_d.di_nextents = 0;
	ASSERT(ip->i_d.di_nblocks == 0);

	nanotime(&tv);
	ip->i_d.di_mtime.t_sec = (__int32_t)tv.tv_sec;
	ip->i_d.di_mtime.t_nsec = (__int32_t)tv.tv_nsec;
	ip->i_d.di_atime = ip->i_d.di_mtime;
	ip->i_d.di_ctime = ip->i_d.di_mtime;

	/*
	 * di_gen will have been taken care of in xfs_iread.
	 */
	ip->i_d.di_extsize = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_dmstate = 0;
	ip->i_d.di_flags = 0;
	flags = XFS_ILOG_CORE;
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_df.if_u2.if_rdev = rdev;
		ip->i_df.if_flags = 0;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
		/*
		 * we can't set up filestreams until after the VFS inode
		 * is set up properly.
		 */
		if (pip && xfs_inode_is_filestream(pip))
			filestreams = 1;
		/* fall through */
	case S_IFDIR:
		if (pip && (pip->i_d.di_flags & XFS_DIFLAG_ANY)) {
			uint	di_flags = 0;

			if (S_ISDIR(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_RTINHERIT;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSZINHERIT;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			} else if (S_ISREG(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_REALTIME;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSIZE;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			}
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOATIME) &&
			    xfs_inherit_noatime)
				di_flags |= XFS_DIFLAG_NOATIME;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODUMP) &&
			    xfs_inherit_nodump)
				di_flags |= XFS_DIFLAG_NODUMP;
			if ((pip->i_d.di_flags & XFS_DIFLAG_SYNC) &&
			    xfs_inherit_sync)
				di_flags |= XFS_DIFLAG_SYNC;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOSYMLINKS) &&
			    xfs_inherit_nosymlinks)
				di_flags |= XFS_DIFLAG_NOSYMLINKS;
			if (pip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
				di_flags |= XFS_DIFLAG_PROJINHERIT;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODEFRAG) &&
			    xfs_inherit_nodefrag)
				di_flags |= XFS_DIFLAG_NODEFRAG;
			if (pip->i_d.di_flags & XFS_DIFLAG_FILESTREAM)
				di_flags |= XFS_DIFLAG_FILESTREAM;
			ip->i_d.di_flags |= di_flags;
		}
		/* FALLTHROUGH */
	case S_IFLNK:
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_df.if_flags = XFS_IFEXTENTS;
		ip->i_df.if_bytes = ip->i_df.if_real_bytes = 0;
		ip->i_df.if_u1.if_extents = NULL;
		break;
	default:
		ASSERT(0);
	}
	/*
	 * Attribute fork settings for new inode.
	 */
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_anextents = 0;

	/*
	 * Log the new values stuffed into the inode.
	 */
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, flags);

	/* now that we have an i_mode we can setup inode ops and unlock */
	xfs_setup_inode(ip);

	/* now we have set up the vfs inode we can associate the filestream */
	if (filestreams) {
		error = xfs_filestream_associate(pip, ip);
		if (error < 0)
			return -error;
		if (!error)
			xfs_iflags_set(ip, XFS_IFILESTREAM);
	}

	*ipp = ip;
	return 0;
}

/*
 * Free up the underlying blocks past new_size.  The new size must be smaller
 * than the current size.  This routine can be used both for the attribute and
 * data fork, and does not modify the inode size, which is left to the caller.
 *
 * The transaction passed to this routine must have made a permanent log
 * reservation of at least XFS_ITRUNCATE_LOG_RES.  This routine may commit the
 * given transaction and start new ones, so make sure everything involved in
 * the transaction is tidy before calling here.  Some transaction will be
 * returned to the caller to be committed.  The incoming transaction must
 * already include the inode, and both inode locks must be held exclusively.
 * The inode must also be "held" within the transaction.  On return the inode
 * will be "held" within the returned transaction.  This routine does NOT
 * require any disk space to be reserved for it within the transaction.
 *
 * If we get an error, we must return with the inode locked and linked into the
 * current transaction. This keeps things simple for the higher level code,
 * because it always knows that the inode is locked and held in the transaction
 * that returns to it whether errors occur or not.  We don't mark the inode
 * dirty on error so that transactions can be easily aborted if possible.
 */
int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	struct xfs_trans	*ntp;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	xfs_fileoff_t		first_unmap_block;
	xfs_fileoff_t		last_block;
	xfs_filblks_t		unmap_len;
	int			committed;
	int			error = 0;
	int			done = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_IOLOCK_EXCL));
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_NOT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	/*
	 * Since it is possible for space to become allocated beyond
	 * the end of the file (in a crash where the space is allocated
	 * but the inode size is not yet updated), simply remove any
	 * blocks which show up between the new EOF and the maximum
	 * possible file size.  If the first block to be removed is
	 * beyond the maximum file size (ie it is the same as last_block),
	 * then there is nothing to do.
	 */
	first_unmap_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	last_block = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	if (first_unmap_block == last_block)
		return 0;

	ASSERT(first_unmap_block < last_block);
	unmap_len = last_block - first_unmap_block + 1;
	while (!done) {
		xfs_bmap_init(&free_list, &first_block);
		error = xfs_bunmapi(tp, ip,
				    first_unmap_block, unmap_len,
				    xfs_bmapi_aflag(whichfork),
				    XFS_ITRUNC_MAX_EXTENTS,
				    &first_block, &free_list,
				    &done);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Duplicate the transaction that has the permanent
		 * reservation and commit the old transaction.
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (committed)
			xfs_trans_ijoin(tp, ip, 0);
		if (error)
			goto out_bmap_cancel;

		if (committed) {
			/*
			 * Mark the inode dirty so it will be logged and
			 * moved forward in the log as part of every commit.
			 */
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

		ntp = xfs_trans_dup(tp);
		error = xfs_trans_commit(tp, 0);
		tp = ntp;

		xfs_trans_ijoin(tp, ip, 0);

		if (error)
			goto out;

		/*
		 * Transaction commit worked ok so we can drop the extra ticket
		 * reference that we gained in xfs_trans_dup()
		 */
		xfs_log_ticket_put(tp->t_ticket);
		error = xfs_trans_reserve(tp, 0,
					XFS_ITRUNCATE_LOG_RES(mp), 0,
					XFS_TRANS_PERM_LOG_RES,
					XFS_ITRUNCATE_LOG_COUNT);
		if (error)
			goto out;
	}

	/*
	 * Always re-log the inode so that our permanent transaction can keep
	 * on rolling it forward in the log.
	 */
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	trace_xfs_itruncate_extents_end(ip, new_size);

out:
	*tpp = tp;
	return error;
out_bmap_cancel:
	/*
	 * If the bunmapi call encounters an error, return to the caller where
	 * the transaction can be properly aborted.  We just need to make sure
	 * we're not holding any resources that we were not when we came in.
	 */
	xfs_bmap_cancel(&free_list);
	goto out;
}

/*
 * This is called when the inode's link count goes to 0.
 * We place the on-disk inode on a list in the AGI.  It
 * will be pulled from this list when the inode is freed.
 */
int
xfs_iunlink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agino_t	agino;
	short		bucket_index;
	int		offset;
	int		error;

	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_mode != 0);

	mp = tp->t_mountp;

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_read_agi(mp, tp, XFS_INO_TO_AGNO(mp, ip->i_ino), &agibp);
	if (error)
		return error;
	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index]);
	ASSERT(be32_to_cpu(agi->agi_unlinked[bucket_index]) != agino);

	if (agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO)) {
		/*
		 * There is already another inode in the bucket we need
		 * to add ourselves to.  Add us at the front of the list.
		 * Here we put the head pointer into our next pointer,
		 * and then we fall through to point the head at us.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error)
			return error;

		ASSERT(dip->di_next_unlinked == cpu_to_be32(NULLAGINO));
		dip->di_next_unlinked = agi->agi_unlinked[bucket_index];
		offset = ip->i_imap.im_boffset +
			offsetof(xfs_dinode_t, di_next_unlinked);
		xfs_trans_inode_buf(tp, ibp);
		xfs_trans_log_buf(tp, ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, ibp);
	}

	/*
	 * Point the bucket head pointer at the inode being inserted.
	 */
	ASSERT(agino != 0);
	agi->agi_unlinked[bucket_index] = cpu_to_be32(agino);
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		(sizeof(xfs_agino_t) * bucket_index);
	xfs_trans_log_buf(tp, agibp, offset,
			  (offset + sizeof(xfs_agino_t) - 1));
	return 0;
}

/*
 * Pull the on-disk inode from the AGI unlinked list.
 */
STATIC int
xfs_iunlink_remove(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_ino_t	next_ino;
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agnumber_t	agno;
	xfs_agino_t	agino;
	xfs_agino_t	next_agino;
	xfs_buf_t	*last_ibp;
	xfs_dinode_t	*last_dip = NULL;
	short		bucket_index;
	int		offset, last_offset = 0;
	int		error;

	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_read_agi(mp, tp, agno, &agibp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO));
	ASSERT(agi->agi_unlinked[bucket_index]);

	if (be32_to_cpu(agi->agi_unlinked[bucket_index]) == agino) {
		/*
		 * We're at the head of the list.  Get the inode's on-disk
		 * buffer to see if there is anyone after us on the list.
		 * Only modify our next pointer if it is not already NULLAGINO.
		 * This saves us the overhead of dealing with the buffer when
		 * there is no need to change it.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);
			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the bucket head pointer at the next inode.
		 */
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		agi->agi_unlinked[bucket_index] = cpu_to_be32(next_agino);
		offset = offsetof(xfs_agi_t, agi_unlinked) +
			(sizeof(xfs_agino_t) * bucket_index);
		xfs_trans_log_buf(tp, agibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
	} else {
		/*
		 * We need to search the list for the inode being freed.
		 */
		next_agino = be32_to_cpu(agi->agi_unlinked[bucket_index]);
		last_ibp = NULL;
		while (next_agino != agino) {
			struct xfs_imap	imap;

			if (last_ibp)
				xfs_trans_brelse(tp, last_ibp);

			imap.im_blkno = 0;
			next_ino = XFS_AGINO_TO_INO(mp, agno, next_agino);

			error = xfs_imap(mp, tp, next_ino, &imap, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap returned error %d.",
					 __func__, error);
				return error;
			}

			error = xfs_imap_to_bp(mp, tp, &imap, &last_dip,
					       &last_ibp, 0, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap_to_bp returned error %d.",
					__func__, error);
				return error;
			}

			last_offset = imap.im_boffset;
			next_agino = be32_to_cpu(last_dip->di_next_unlinked);
			ASSERT(next_agino != NULLAGINO);
			ASSERT(next_agino != 0);
		}

		/*
		 * Now last_ibp points to the buffer previous to us on the
		 * unlinked list.  Pull us from the list.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp(2) returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);
			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the previous inode on the list to the next inode.
		 */
		last_dip->di_next_unlinked = cpu_to_be32(next_agino);
		ASSERT(next_agino != 0);
		offset = last_offset + offsetof(xfs_dinode_t, di_next_unlinked);
		xfs_trans_inode_buf(tp, last_ibp);
		xfs_trans_log_buf(tp, last_ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, last_ibp);
	}
	return 0;
}

/*
 * A big issue when freeing the inode cluster is is that we _cannot_ skip any
 * inodes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
STATIC int
xfs_ifree_cluster(
	xfs_inode_t	*free_ip,
	xfs_trans_t	*tp,
	xfs_ino_t	inum)
{
	xfs_mount_t		*mp = free_ip->i_mount;
	int			blks_per_cluster;
	int			nbufs;
	int			ninodes;
	int			i, j;
	xfs_daddr_t		blkno;
	xfs_buf_t		*bp;
	xfs_inode_t		*ip;
	xfs_inode_log_item_t	*iip;
	xfs_log_item_t		*lip;
	struct xfs_perag	*pag;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, inum));
	if (mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(mp)) {
		blks_per_cluster = 1;
		ninodes = mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp);
	} else {
		blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) /
					mp->m_sb.sb_blocksize;
		ninodes = blks_per_cluster * mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp) / blks_per_cluster;
	}

	for (j = 0; j < nbufs; j++, inum += ninodes) {
		blkno = XFS_AGB_TO_DADDR(mp, XFS_INO_TO_AGNO(mp, inum),
					 XFS_INO_TO_AGBNO(mp, inum));

		/*
		 * We obtain and lock the backing buffer first in the process
		 * here, as we have to ensure that any dirty inode that we
		 * can't get the flush lock on is attached to the buffer.
		 * If we scan the in-memory inodes first, then buffer IO can
		 * complete before we get a lock on it, and hence we may fail
		 * to mark all the active inodes on the buffer stale.
		 */
		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkno,
					mp->m_bsize * blks_per_cluster, 0);

		if (!bp)
			return ENOMEM;
		/*
		 * Walk the inodes already attached to the buffer and mark them
		 * stale. These will all have the flush locks held, so an
		 * in-memory inode walk can't lock them. By marking them all
		 * stale first, we will not attempt to lock them in the loop
		 * below as the XFS_ISTALE flag will be set.
		 */
		lip = bp->b_fspriv;
		while (lip) {
			if (lip->li_type == XFS_LI_INODE) {
				iip = (xfs_inode_log_item_t *)lip;
				ASSERT(iip->ili_logged == 1);
				lip->li_cb = xfs_istale_done;
				xfs_trans_ail_copy_lsn(mp->m_ail,
							&iip->ili_flush_lsn,
							&iip->ili_item.li_lsn);
				xfs_iflags_set(iip->ili_inode, XFS_ISTALE);
			}
			lip = lip->li_bio_list;
		}


		/*
		 * For each inode in memory attempt to add it to the inode
		 * buffer and set it up for being staled on buffer IO
		 * completion.  This is safe as we've locked out tail pushing
		 * and flushing by locking the buffer.
		 *
		 * We have already marked every inode that was part of a
		 * transaction stale above, which means there is no point in
		 * even trying to lock them.
		 */
		for (i = 0; i < ninodes; i++) {
retry:
			rcu_read_lock();
			ip = radix_tree_lookup(&pag->pag_ici_root,
					XFS_INO_TO_AGINO(mp, (inum + i)));

			/* Inode not in memory, nothing to do */
			if (!ip) {
				rcu_read_unlock();
				continue;
			}

			/*
			 * because this is an RCU protected lookup, we could
			 * find a recently freed or even reallocated inode
			 * during the lookup. We need to check under the
			 * i_flags_lock for a valid inode here. Skip it if it
			 * is not valid, the wrong inode or stale.
			 */
			spin_lock(&ip->i_flags_lock);
			if (ip->i_ino != inum + i ||
			    __xfs_iflags_test(ip, XFS_ISTALE)) {
				spin_unlock(&ip->i_flags_lock);
				rcu_read_unlock();
				continue;
			}
			spin_unlock(&ip->i_flags_lock);

			/*
			 * Don't try to lock/unlock the current inode, but we
			 * _cannot_ skip the other inodes that we did not find
			 * in the list attached to the buffer and are not
			 * already marked stale. If we can't lock it, back off
			 * and retry.
			 */
			if (ip != free_ip &&
			    !xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				rcu_read_unlock();
				delay(1);
				goto retry;
			}
			rcu_read_unlock();

			xfs_iflock(ip);
			xfs_iflags_set(ip, XFS_ISTALE);

			/*
			 * we don't need to attach clean inodes or those only
			 * with unlogged changes (which we throw away, anyway).
			 */
			iip = ip->i_itemp;
			if (!iip || xfs_inode_clean(ip)) {
				ASSERT(ip != free_ip);
				xfs_ifunlock(ip);
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				continue;
			}

			iip->ili_last_fields = iip->ili_fields;
			iip->ili_fields = 0;
			iip->ili_logged = 1;
			xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
						&iip->ili_item.li_lsn);

			xfs_buf_attach_iodone(bp, xfs_istale_done,
						  &iip->ili_item);

			if (ip != free_ip)
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		xfs_trans_stale_inode_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}

	xfs_perag_put(pag);
	return 0;
}

/*
 * This is called to return an inode to the inode free list.
 * The inode should already be truncated to 0 length and have
 * no pages associated with it.  This routine also assumes that
 * the inode is already a part of the transaction.
 *
 * The on-disk copy of the inode will have been added to the list
 * of unlinked inodes in the AGI. We need to remove the inode from
 * that list atomically with respect to freeing it here.
 */
int
xfs_ifree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_bmap_free_t	*flist)
{
	int			error;
	int			delete;
	xfs_ino_t		first_ino;
	xfs_dinode_t    	*dip;
	xfs_buf_t       	*ibp;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_size == 0 || !S_ISREG(ip->i_d.di_mode));
	ASSERT(ip->i_d.di_nblocks == 0);

	/*
	 * Pull the on-disk inode from the AGI unlinked list.
	 */
	error = xfs_iunlink_remove(tp, ip);
	if (error != 0) {
		return error;
	}

	error = xfs_difree(tp, ip->i_ino, flist, &delete, &first_ino);
	if (error != 0) {
		return error;
	}
	ip->i_d.di_mode = 0;		/* mark incore inode as free */
	ip->i_d.di_flags = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_forkoff = 0;		/* mark the attr fork not in use */
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	ip->i_d.di_gen++;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_imap_to_bp(ip->i_mount, tp, &ip->i_imap, &dip, &ibp,
			       0, 0);
	if (error)
		return error;

        /*
	* Clear the on-disk di_mode. This is to prevent xfs_bulkstat
	* from picking up this inode when it is reclaimed (its incore state
	* initialzed but not flushed to disk yet). The in-core di_mode is
	* already cleared  and a corresponding transaction logged.
	* The hack here just synchronizes the in-core to on-disk
	* di_mode value in advance before the actual inode sync to disk.
	* This is OK because the inode is already unlinked and would never
	* change its di_mode again for this inode generation.
	* This is a temporary hack that would require a proper fix
	* in the future.
	*/
	dip->di_mode = 0;

	if (delete) {
		error = xfs_ifree_cluster(ip, tp, first_ino);
	}

	return error;
}

/*
 * Reallocate the space for if_broot based on the number of records
 * being added or deleted as indicated in rec_diff.  Move the records
 * and pointers in if_broot to fit the new size.  When shrinking this
 * will eliminate holes between the records and pointers created by
 * the caller.  When growing this will create holes to be filled in
 * by the caller.
 *
 * The caller must not request to add more records than would fit in
 * the on-disk inode root.  If the if_broot is currently NULL, then
 * if we adding records one will be allocated.  The caller must also
 * not request that the number of records go below zero, although
 * it can go to zero.
 *
 * ip -- the inode whose if_broot area is changing
 * ext_diff -- the change in the number of records, positive or negative,
 *	 requested for the if_broot array.
 */
void
xfs_iroot_realloc(
	xfs_inode_t		*ip,
	int			rec_diff,
	int			whichfork)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			cur_max;
	xfs_ifork_t		*ifp;
	struct xfs_btree_block	*new_broot;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;

	/*
	 * Handle the degenerate case quietly.
	 */
	if (rec_diff == 0) {
		return;
	}

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (rec_diff > 0) {
		/*
		 * If there wasn't any memory allocated before, just
		 * allocate it now and get out.
		 */
		if (ifp->if_broot_bytes == 0) {
			new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(rec_diff);
			ifp->if_broot = kmem_alloc(new_size, KM_SLEEP | KM_NOFS);
			ifp->if_broot_bytes = (int)new_size;
			return;
		}

		/*
		 * If there is already an existing if_broot, then we need
		 * to realloc() it and shift the pointers to their new
		 * location.  The records don't change location because
		 * they are kept butted up against the btree block header.
		 */
		cur_max = xfs_bmbt_maxrecs(mp, ifp->if_broot_bytes, 0);
		new_max = cur_max + rec_diff;
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
		ifp->if_broot = kmem_realloc(ifp->if_broot, new_size,
				(size_t)XFS_BMAP_BROOT_SPACE_CALC(cur_max), /* old size */
				KM_SLEEP | KM_NOFS);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     (int)new_size);
		ifp->if_broot_bytes = (int)new_size;
		ASSERT(ifp->if_broot_bytes <=
			XFS_IFORK_SIZE(ip, whichfork) + XFS_BROOT_SIZE_ADJ);
		memmove(np, op, cur_max * (uint)sizeof(xfs_dfsbno_t));
		return;
	}

	/*
	 * rec_diff is less than 0.  In this case, we are shrinking the
	 * if_broot buffer.  It must already exist.  If we go to zero
	 * records, just get rid of the root and clear the status bit.
	 */
	ASSERT((ifp->if_broot != NULL) && (ifp->if_broot_bytes > 0));
	cur_max = xfs_bmbt_maxrecs(mp, ifp->if_broot_bytes, 0);
	new_max = cur_max + rec_diff;
	ASSERT(new_max >= 0);
	if (new_max > 0)
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
	else
		new_size = 0;
	if (new_size > 0) {
		new_broot = kmem_alloc(new_size, KM_SLEEP | KM_NOFS);
		/*
		 * First copy over the btree block header.
		 */
		memcpy(new_broot, ifp->if_broot, XFS_BTREE_LBLOCK_LEN);
	} else {
		new_broot = NULL;
		ifp->if_flags &= ~XFS_IFBROOT;
	}

	/*
	 * Only copy the records and pointers if there are any.
	 */
	if (new_max > 0) {
		/*
		 * First copy the records.
		 */
		op = (char *)XFS_BMBT_REC_ADDR(mp, ifp->if_broot, 1);
		np = (char *)XFS_BMBT_REC_ADDR(mp, new_broot, 1);
		memcpy(np, op, new_max * (uint)sizeof(xfs_bmbt_rec_t));

		/*
		 * Then copy the pointers.
		 */
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, new_broot, 1,
						     (int)new_size);
		memcpy(np, op, new_max * (uint)sizeof(xfs_dfsbno_t));
	}
	kmem_free(ifp->if_broot);
	ifp->if_broot = new_broot;
	ifp->if_broot_bytes = (int)new_size;
	ASSERT(ifp->if_broot_bytes <=
		XFS_IFORK_SIZE(ip, whichfork) + XFS_BROOT_SIZE_ADJ);
	return;
}


/*
 * This is called when the amount of space needed for if_data
 * is increased or decreased.  The change in size is indicated by
 * the number of bytes that need to be added or deleted in the
 * byte_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Otherwise,
 * use kmem_realloc() or kmem_alloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose if_data area is changing
 * byte_diff -- the change in the number of bytes, positive or negative,
 *	 requested for the if_data array.
 */
void
xfs_idata_realloc(
	xfs_inode_t	*ip,
	int		byte_diff,
	int		whichfork)
{
	xfs_ifork_t	*ifp;
	int		new_size;
	int		real_size;

	if (byte_diff == 0) {
		return;
	}

	ifp = XFS_IFORK_PTR(ip, whichfork);
	new_size = (int)ifp->if_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			kmem_free(ifp->if_u1.if_data);
		}
		ifp->if_u1.if_data = NULL;
		real_size = 0;
	} else if (new_size <= sizeof(ifp->if_u2.if_inline_data)) {
		/*
		 * If the valid extents/data can fit in if_inline_ext/data,
		 * copy them from the malloc'd vector and free it.
		 */
		if (ifp->if_u1.if_data == NULL) {
			ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
		} else if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			ASSERT(ifp->if_real_bytes != 0);
			memcpy(ifp->if_u2.if_inline_data, ifp->if_u1.if_data,
			      new_size);
			kmem_free(ifp->if_u1.if_data);
			ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
		}
		real_size = 0;
	} else {
		/*
		 * Stuck with malloc/realloc.
		 * For inline data, the underlying buffer must be
		 * a multiple of 4 bytes in size so that it can be
		 * logged and stay on word boundaries.  We enforce
		 * that here.
		 */
		real_size = roundup(new_size, 4);
		if (ifp->if_u1.if_data == NULL) {
			ASSERT(ifp->if_real_bytes == 0);
			ifp->if_u1.if_data = kmem_alloc(real_size,
							KM_SLEEP | KM_NOFS);
		} else if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			/*
			 * Only do the realloc if the underlying size
			 * is really changing.
			 */
			if (ifp->if_real_bytes != real_size) {
				ifp->if_u1.if_data =
					kmem_realloc(ifp->if_u1.if_data,
							real_size,
							ifp->if_real_bytes,
							KM_SLEEP | KM_NOFS);
			}
		} else {
			ASSERT(ifp->if_real_bytes == 0);
			ifp->if_u1.if_data = kmem_alloc(real_size,
							KM_SLEEP | KM_NOFS);
			memcpy(ifp->if_u1.if_data, ifp->if_u2.if_inline_data,
				ifp->if_bytes);
		}
	}
	ifp->if_real_bytes = real_size;
	ifp->if_bytes = new_size;
	ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
}

void
xfs_idestroy_fork(
	xfs_inode_t	*ip,
	int		whichfork)
{
	xfs_ifork_t	*ifp;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_broot != NULL) {
		kmem_free(ifp->if_broot);
		ifp->if_broot = NULL;
	}

	/*
	 * If the format is local, then we can't have an extents
	 * array so just look for an inline data array.  If we're
	 * not local then we may or may not have an extents list,
	 * so check and free it up if we do.
	 */
	if (XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_LOCAL) {
		if ((ifp->if_u1.if_data != ifp->if_u2.if_inline_data) &&
		    (ifp->if_u1.if_data != NULL)) {
			ASSERT(ifp->if_real_bytes != 0);
			kmem_free(ifp->if_u1.if_data);
			ifp->if_u1.if_data = NULL;
			ifp->if_real_bytes = 0;
		}
	} else if ((ifp->if_flags & XFS_IFEXTENTS) &&
		   ((ifp->if_flags & XFS_IFEXTIREC) ||
		    ((ifp->if_u1.if_extents != NULL) &&
		     (ifp->if_u1.if_extents != ifp->if_u2.if_inline_ext)))) {
		ASSERT(ifp->if_real_bytes != 0);
		xfs_iext_destroy(ifp);
	}
	ASSERT(ifp->if_u1.if_extents == NULL ||
	       ifp->if_u1.if_extents == ifp->if_u2.if_inline_ext);
	ASSERT(ifp->if_real_bytes == 0);
	if (whichfork == XFS_ATTR_FORK) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
	}
}

/*
 * This is called to unpin an inode.  The caller must have the inode locked
 * in at least shared mode so that the buffer cannot be subsequently pinned
 * once someone is waiting for it to be unpinned.
 */
static void
xfs_iunpin(
	struct xfs_inode	*ip)
{
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));

	trace_xfs_inode_unpin_nowait(ip, _RET_IP_);

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force_lsn(ip->i_mount, ip->i_itemp->ili_last_lsn, 0);

}

static void
__xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IPINNED_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IPINNED_BIT);

	xfs_iunpin(ip);

	do {
		prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		if (xfs_ipincount(ip))
			io_schedule();
	} while (xfs_ipincount(ip));
	finish_wait(wq, &wait.wait);
}

void
xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	if (xfs_ipincount(ip))
		__xfs_iunpin_wait(ip);
}

/*
 * xfs_iextents_copy()
 *
 * This is called to copy the REAL extents (as opposed to the delayed
 * allocation extents) from the inode into the given buffer.  It
 * returns the number of bytes copied into the buffer.
 *
 * If there are no delayed allocation extents, then we can just
 * memcpy() the extents into the buffer.  Otherwise, we need to
 * examine each extent in turn and skip those which are delayed.
 */
int
xfs_iextents_copy(
	xfs_inode_t		*ip,
	xfs_bmbt_rec_t		*dp,
	int			whichfork)
{
	int			copied;
	int			i;
	xfs_ifork_t		*ifp;
	int			nrecs;
	xfs_fsblock_t		start_block;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(ifp->if_bytes > 0);

	nrecs = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	XFS_BMAP_TRACE_EXLIST(ip, nrecs, whichfork);
	ASSERT(nrecs > 0);

	/*
	 * There are some delayed allocation extents in the
	 * inode, so copy the extents one at a time and skip
	 * the delayed ones.  There must be at least one
	 * non-delayed extent.
	 */
	copied = 0;
	for (i = 0; i < nrecs; i++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
		start_block = xfs_bmbt_get_startblock(ep);
		if (isnullstartblock(start_block)) {
			/*
			 * It's a delayed allocation extent, so skip it.
			 */
			continue;
		}

		/* Translate to on disk format */
		put_unaligned(cpu_to_be64(ep->l0), &dp->l0);
		put_unaligned(cpu_to_be64(ep->l1), &dp->l1);
		dp++;
		copied++;
	}
	ASSERT(copied != 0);
	xfs_validate_extents(ifp, copied, XFS_EXTFMT_INODE(ip));

	return (copied * (uint)sizeof(xfs_bmbt_rec_t));
}

/*
 * Each of the following cases stores data into the same region
 * of the on-disk inode, so only one of them can be valid at
 * any given time. While it is possible to have conflicting formats
 * and log flags, e.g. having XFS_ILOG_?DATA set when the fork is
 * in EXTENTS format, this can only happen when the fork has
 * changed formats after being modified but before being flushed.
 * In these cases, the format always takes precedence, because the
 * format indicates the current state of the fork.
 */
/*ARGSUSED*/
STATIC void
xfs_iflush_fork(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
	xfs_inode_log_item_t	*iip,
	int			whichfork,
	xfs_buf_t		*bp)
{
	char			*cp;
	xfs_ifork_t		*ifp;
	xfs_mount_t		*mp;
#ifdef XFS_TRANS_DEBUG
	int			first;
#endif
	static const short	brootflag[2] =
		{ XFS_ILOG_DBROOT, XFS_ILOG_ABROOT };
	static const short	dataflag[2] =
		{ XFS_ILOG_DDATA, XFS_ILOG_ADATA };
	static const short	extflag[2] =
		{ XFS_ILOG_DEXT, XFS_ILOG_AEXT };

	if (!iip)
		return;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	/*
	 * This can happen if we gave up in iformat in an error path,
	 * for the attribute fork.
	 */
	if (!ifp) {
		ASSERT(whichfork == XFS_ATTR_FORK);
		return;
	}
	cp = XFS_DFORK_PTR(dip, whichfork);
	mp = ip->i_mount;
	switch (XFS_IFORK_FORMAT(ip, whichfork)) {
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & dataflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(ifp->if_u1.if_data != NULL);
			ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
			memcpy(cp, ifp->if_u1.if_data, ifp->if_bytes);
		}
		break;

	case XFS_DINODE_FMT_EXTENTS:
		ASSERT((ifp->if_flags & XFS_IFEXTENTS) ||
		       !(iip->ili_fields & extflag[whichfork]));
		if ((iip->ili_fields & extflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(xfs_iext_get_ext(ifp, 0));
			ASSERT(XFS_IFORK_NEXTENTS(ip, whichfork) > 0);
			(void)xfs_iextents_copy(ip, (xfs_bmbt_rec_t *)cp,
				whichfork);
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & brootflag[whichfork]) &&
		    (ifp->if_broot_bytes > 0)) {
			ASSERT(ifp->if_broot != NULL);
			ASSERT(ifp->if_broot_bytes <=
			       (XFS_IFORK_SIZE(ip, whichfork) +
				XFS_BROOT_SIZE_ADJ));
			xfs_bmbt_to_bmdr(mp, ifp->if_broot, ifp->if_broot_bytes,
				(xfs_bmdr_block_t *)cp,
				XFS_DFORK_SIZE(dip, mp, whichfork));
		}
		break;

	case XFS_DINODE_FMT_DEV:
		if (iip->ili_fields & XFS_ILOG_DEV) {
			ASSERT(whichfork == XFS_DATA_FORK);
			xfs_dinode_put_rdev(dip, ip->i_df.if_u2.if_rdev);
		}
		break;

	case XFS_DINODE_FMT_UUID:
		if (iip->ili_fields & XFS_ILOG_UUID) {
			ASSERT(whichfork == XFS_DATA_FORK);
			memcpy(XFS_DFORK_DPTR(dip),
			       &ip->i_df.if_u2.if_uuid,
			       sizeof(uuid_t));
		}
		break;

	default:
		ASSERT(0);
		break;
	}
}

STATIC int
xfs_iflush_cluster(
	xfs_inode_t	*ip,
	xfs_buf_t	*bp)
{
	xfs_mount_t		*mp = ip->i_mount;
	struct xfs_perag	*pag;
	unsigned long		first_index, mask;
	unsigned long		inodes_per_cluster;
	int			ilist_size;
	xfs_inode_t		**ilist;
	xfs_inode_t		*iq;
	int			nr_found;
	int			clcount = 0;
	int			bufwasdelwri;
	int			i;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	inodes_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog;
	ilist_size = inodes_per_cluster * sizeof(xfs_inode_t *);
	ilist = kmem_alloc(ilist_size, KM_MAYFAIL|KM_NOFS);
	if (!ilist)
		goto out_put;

	mask = ~(((XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog)) - 1);
	first_index = XFS_INO_TO_AGINO(mp, ip->i_ino) & mask;
	rcu_read_lock();
	/* really need a gang lookup range call here */
	nr_found = radix_tree_gang_lookup(&pag->pag_ici_root, (void**)ilist,
					first_index, inodes_per_cluster);
	if (nr_found == 0)
		goto out_free;

	for (i = 0; i < nr_found; i++) {
		iq = ilist[i];
		if (iq == ip)
			continue;

		/*
		 * because this is an RCU protected lookup, we could find a
		 * recently freed or even reallocated inode during the lookup.
		 * We need to check under the i_flags_lock for a valid inode
		 * here. Skip it if it is not valid or the wrong inode.
		 */
		spin_lock(&ip->i_flags_lock);
		if (!ip->i_ino ||
		    (XFS_INO_TO_AGINO(mp, iq->i_ino) & mask) != first_index) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}
		spin_unlock(&ip->i_flags_lock);

		/*
		 * Do an un-protected check to see if the inode is dirty and
		 * is a candidate for flushing.  These checks will be repeated
		 * later after the appropriate locks are acquired.
		 */
		if (xfs_inode_clean(iq) && xfs_ipincount(iq) == 0)
			continue;

		/*
		 * Try to get locks.  If any are unavailable or it is pinned,
		 * then this inode cannot be flushed and is skipped.
		 */

		if (!xfs_ilock_nowait(iq, XFS_ILOCK_SHARED))
			continue;
		if (!xfs_iflock_nowait(iq)) {
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}
		if (xfs_ipincount(iq)) {
			xfs_ifunlock(iq);
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}

		/*
		 * arriving here means that this inode can be flushed.  First
		 * re-check that it's dirty before flushing.
		 */
		if (!xfs_inode_clean(iq)) {
			int	error;
			error = xfs_iflush_int(iq, bp);
			if (error) {
				xfs_iunlock(iq, XFS_ILOCK_SHARED);
				goto cluster_corrupt_out;
			}
			clcount++;
		} else {
			xfs_ifunlock(iq);
		}
		xfs_iunlock(iq, XFS_ILOCK_SHARED);
	}

	if (clcount) {
		XFS_STATS_INC(xs_icluster_flushcnt);
		XFS_STATS_ADD(xs_icluster_flushinode, clcount);
	}

out_free:
	rcu_read_unlock();
	kmem_free(ilist);
out_put:
	xfs_perag_put(pag);
	return 0;


cluster_corrupt_out:
	/*
	 * Corruption detected in the clustering loop.  Invalidate the
	 * inode buffer and shut down the filesystem.
	 */
	rcu_read_unlock();
	/*
	 * Clean up the buffer.  If it was delwri, just release it --
	 * brelse can handle it with no problems.  If not, shut down the
	 * filesystem before releasing the buffer.
	 */
	bufwasdelwri = (bp->b_flags & _XBF_DELWRI_Q);
	if (bufwasdelwri)
		xfs_buf_relse(bp);

	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);

	if (!bufwasdelwri) {
		/*
		 * Just like incore_relse: if we have b_iodone functions,
		 * mark the buffer as an error and call them.  Otherwise
		 * mark it as stale and brelse.
		 */
		if (bp->b_iodone) {
			XFS_BUF_UNDONE(bp);
			xfs_buf_stale(bp);
			xfs_buf_ioerror(bp, EIO);
			xfs_buf_ioend(bp, 0);
		} else {
			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
		}
	}

	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(iq, false);
	kmem_free(ilist);
	xfs_perag_put(pag);
	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * Flush dirty inode metadata into the backing buffer.
 *
 * The caller must have the inode lock and the inode flush lock held.  The
 * inode lock will still be held upon return to the caller, and the inode
 * flush lock will be released after the inode has reached the disk.
 *
 * The caller must write out the buffer returned in *bpp and release it.
 */
int
xfs_iflush(
	struct xfs_inode	*ip,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_buf		*bp;
	struct xfs_dinode	*dip;
	int			error;

	XFS_STATS_INC(xs_iflush_count);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));

	*bpp = NULL;

	xfs_iunpin_wait(ip);

	/*
	 * For stale inodes we cannot rely on the backing buffer remaining
	 * stale in cache for the remaining life of the stale inode and so
	 * xfs_imap_to_bp() below may give us a buffer that no longer contains
	 * inodes below. We have to check this after ensuring the inode is
	 * unpinned so that it is safe to reclaim the stale inode after the
	 * flush call.
	 */
	if (xfs_iflags_test(ip, XFS_ISTALE)) {
		xfs_ifunlock(ip);
		return 0;
	}

	/*
	 * This may have been unpinned because the filesystem is shutting
	 * down forcibly. If that's the case we must not write this inode
	 * to disk, because the log record didn't make it to disk.
	 *
	 * We also have to remove the log item from the AIL in this case,
	 * as we wait for an empty AIL as part of the unmount process.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = XFS_ERROR(EIO);
		goto abort_out;
	}

	/*
	 * Get the buffer containing the on-disk inode.
	 */
	error = xfs_imap_to_bp(mp, NULL, &ip->i_imap, &dip, &bp, XBF_TRYLOCK,
			       0);
	if (error || !bp) {
		xfs_ifunlock(ip);
		return error;
	}

	/*
	 * First flush out the inode that xfs_iflush was called with.
	 */
	error = xfs_iflush_int(ip, bp);
	if (error)
		goto corrupt_out;

	/*
	 * If the buffer is pinned then push on the log now so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (xfs_buf_ispinned(bp))
		xfs_log_force(mp, 0);

	/*
	 * inode clustering:
	 * see if other inodes can be gathered into this write
	 */
	error = xfs_iflush_cluster(ip, bp);
	if (error)
		goto cluster_corrupt_out;

	*bpp = bp;
	return 0;

corrupt_out:
	xfs_buf_relse(bp);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
cluster_corrupt_out:
	error = XFS_ERROR(EFSCORRUPTED);
abort_out:
	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(ip, false);
	return error;
}


STATIC int
xfs_iflush_int(
	xfs_inode_t		*ip,
	xfs_buf_t		*bp)
{
	xfs_inode_log_item_t	*iip;
	xfs_dinode_t		*dip;
	xfs_mount_t		*mp;
#ifdef XFS_TRANS_DEBUG
	int			first;
#endif

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));

	iip = ip->i_itemp;
	mp = ip->i_mount;

	/* set *dip = inode's place in the buffer */
	dip = (xfs_dinode_t *)xfs_buf_offset(bp, ip->i_imap.im_boffset);

	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1, XFS_RANDOM_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu magic number 0x%x, ptr 0x%p",
			__func__, ip->i_ino, be16_to_cpu(dip->di_magic), dip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_magic != XFS_DINODE_MAGIC,
				mp, XFS_ERRTAG_IFLUSH_2, XFS_RANDOM_IFLUSH_2)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu, ptr 0x%p, magic number 0x%x",
			__func__, ip->i_ino, ip, ip->i_d.di_magic);
		goto corrupt_out;
	}
	if (S_ISREG(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE),
		    mp, XFS_ERRTAG_IFLUSH_3, XFS_RANDOM_IFLUSH_3)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	} else if (S_ISDIR(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_LOCAL),
		    mp, XFS_ERRTAG_IFLUSH_4, XFS_RANDOM_IFLUSH_4)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_d.di_nextents + ip->i_d.di_anextents >
				ip->i_d.di_nblocks, mp, XFS_ERRTAG_IFLUSH_5,
				XFS_RANDOM_IFLUSH_5)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore inode %Lu, "
			"total extents = %d, nblocks = %Ld, ptr 0x%p",
			__func__, ip->i_ino,
			ip->i_d.di_nextents + ip->i_d.di_anextents,
			ip->i_d.di_nblocks, ip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_forkoff > mp->m_sb.sb_inodesize,
				mp, XFS_ERRTAG_IFLUSH_6, XFS_RANDOM_IFLUSH_6)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad inode %Lu, forkoff 0x%x, ptr 0x%p",
			__func__, ip->i_ino, ip->i_d.di_forkoff, ip);
		goto corrupt_out;
	}
	/*
	 * bump the flush iteration count, used to detect flushes which
	 * postdate a log record during recovery.
	 */

	ip->i_d.di_flushiter++;

	/*
	 * Copy the dirty parts of the inode into the on-disk
	 * inode.  We always copy out the core of the inode,
	 * because if the inode is dirty at all the core must
	 * be.
	 */
	xfs_dinode_to_disk(dip, &ip->i_d);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (ip->i_d.di_flushiter == DI_MAX_FLUSH)
		ip->i_d.di_flushiter = 0;

	/*
	 * If this is really an old format inode and the superblock version
	 * has not been updated to support only new format inodes, then
	 * convert back to the old inode format.  If the superblock version
	 * has been updated, then make the conversion permanent.
	 */
	ASSERT(ip->i_d.di_version == 1 || xfs_sb_version_hasnlink(&mp->m_sb));
	if (ip->i_d.di_version == 1) {
		if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
			/*
			 * Convert it back.
			 */
			ASSERT(ip->i_d.di_nlink <= XFS_MAXLINK_1);
			dip->di_onlink = cpu_to_be16(ip->i_d.di_nlink);
		} else {
			/*
			 * The superblock version has already been bumped,
			 * so just make the conversion to the new inode
			 * format permanent.
			 */
			ip->i_d.di_version = 2;
			dip->di_version = 2;
			ip->i_d.di_onlink = 0;
			dip->di_onlink = 0;
			memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
			memset(&(dip->di_pad[0]), 0,
			      sizeof(dip->di_pad));
			ASSERT(xfs_get_projid(ip) == 0);
		}
	}

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK, bp);
	if (XFS_IFORK_Q(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK, bp);
	xfs_inobp_check(mp, bp);

	/*
	 * We've recorded everything logged in the inode, so we'd like to clear
	 * the ili_fields bits so we don't log and flush things unnecessarily.
	 * However, we can't stop logging all this information until the data
	 * we've copied into the disk buffer is written to disk.  If we did we
	 * might overwrite the copy of the inode in the log with all the data
	 * after re-logging only part of it, and in the face of a crash we
	 * wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.  When
	 * logging the inode, these bits are moved back to the ili_fields field.
	 * In the xfs_iflush_done() routine we clear ili_last_fields, since we
	 * know that the information those bits represent is permanently on
	 * disk.  As long as the flush completes before the inode is logged
	 * again, then both ili_fields and ili_last_fields will be cleared.
	 *
	 * We can play with the ili_fields bits here, because the inode lock
	 * must be held exclusively in order to set bits there and the flush
	 * lock protects the ili_last_fields bits.  Set ili_logged so the flush
	 * done routine can tell whether or not to look in the AIL.  Also, store
	 * the current LSN of the inode so that we can tell whether the item has
	 * moved in the AIL from xfs_iflush_done().  In order to read the lsn we
	 * need the AIL lock, because it is a 64 bit value that cannot be read
	 * atomically.
	 */
	if (iip != NULL && iip->ili_fields != 0) {
		iip->ili_last_fields = iip->ili_fields;
		iip->ili_fields = 0;
		iip->ili_logged = 1;

		xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
					&iip->ili_item.li_lsn);

		/*
		 * Attach the function xfs_iflush_done to the inode's
		 * buffer.  This will remove the inode from the AIL
		 * and unlock the inode's flush lock when the inode is
		 * completely written to disk.
		 */
		xfs_buf_attach_iodone(bp, xfs_iflush_done, &iip->ili_item);

		ASSERT(bp->b_fspriv != NULL);
		ASSERT(bp->b_iodone != NULL);
	} else {
		/*
		 * We're flushing an inode which is not in the AIL and has
		 * not been logged.  For this case we can immediately drop
		 * the inode flush lock because we can avoid the whole
		 * AIL state thing.  It's OK to drop the flush lock now,
		 * because we've already locked the buffer and to do anything
		 * you really need both.
		 */
		if (iip != NULL) {
			ASSERT(iip->ili_logged == 0);
			ASSERT(iip->ili_last_fields == 0);
			ASSERT((iip->ili_item.li_flags & XFS_LI_IN_AIL) == 0);
		}
		xfs_ifunlock(ip);
	}

	return 0;

corrupt_out:
	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * Return a pointer to the extent record at file index idx.
 */
xfs_bmbt_rec_host_t *
xfs_iext_get_ext(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx)		/* index of target extent */
{
	ASSERT(idx >= 0);
	ASSERT(idx < ifp->if_bytes / sizeof(xfs_bmbt_rec_t));

	if ((ifp->if_flags & XFS_IFEXTIREC) && (idx == 0)) {
		return ifp->if_u1.if_ext_irec->er_extbuf;
	} else if (ifp->if_flags & XFS_IFEXTIREC) {
		xfs_ext_irec_t	*erp;		/* irec pointer */
		int		erp_idx = 0;	/* irec index */
		xfs_extnum_t	page_idx = idx;	/* ext index in target list */

		erp = xfs_iext_idx_to_irec(ifp, &page_idx, &erp_idx, 0);
		return &erp->er_extbuf[page_idx];
	} else if (ifp->if_bytes) {
		return &ifp->if_u1.if_extents[idx];
	} else {
		return NULL;
	}
}

/*
 * Insert new item(s) into the extent records for incore inode
 * fork 'ifp'.  'count' new items are inserted at index 'idx'.
 */
void
xfs_iext_insert(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* starting index of new items */
	xfs_extnum_t	count,		/* number of inserted items */
	xfs_bmbt_irec_t	*new,		/* items to insert */
	int		state)		/* type of extent conversion */
{
	xfs_ifork_t	*ifp = (state & BMAP_ATTRFORK) ? ip->i_afp : &ip->i_df;
	xfs_extnum_t	i;		/* extent record index */

	trace_xfs_iext_insert(ip, idx, new, state, _RET_IP_);

	ASSERT(ifp->if_flags & XFS_IFEXTENTS);
	xfs_iext_add(ifp, idx, count);
	for (i = idx; i < idx + count; i++, new++)
		xfs_bmbt_set_all(xfs_iext_get_ext(ifp, i), new);
}

/*
 * This is called when the amount of space required for incore file
 * extents needs to be increased. The ext_diff parameter stores the
 * number of new extents being added and the idx parameter contains
 * the extent index where the new extents will be added. If the new
 * extents are being appended, then we just need to (re)allocate and
 * initialize the space. Otherwise, if the new extents are being
 * inserted into the middle of the existing entries, a bit more work
 * is required to make room for the new extents to be inserted. The
 * caller is responsible for filling in the new extent entries upon
 * return.
 */
void
xfs_iext_add(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin adding exts */
	int		ext_diff)	/* number of extents to add */
{
	int		byte_diff;	/* new bytes being added */
	int		new_size;	/* size of extents after adding */
	xfs_extnum_t	nextents;	/* number of extents in file */

	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT((idx >= 0) && (idx <= nextents));
	byte_diff = ext_diff * sizeof(xfs_bmbt_rec_t);
	new_size = ifp->if_bytes + byte_diff;
	/*
	 * If the new number of extents (nextents + ext_diff)
	 * fits inside the inode, then continue to use the inline
	 * extent buffer.
	 */
	if (nextents + ext_diff <= XFS_INLINE_EXTS) {
		if (idx < nextents) {
			memmove(&ifp->if_u2.if_inline_ext[idx + ext_diff],
				&ifp->if_u2.if_inline_ext[idx],
				(nextents - idx) * sizeof(xfs_bmbt_rec_t));
			memset(&ifp->if_u2.if_inline_ext[idx], 0, byte_diff);
		}
		ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
		ifp->if_real_bytes = 0;
	}
	/*
	 * Otherwise use a linear (direct) extent list.
	 * If the extents are currently inside the inode,
	 * xfs_iext_realloc_direct will switch us from
	 * inline to direct extent allocation mode.
	 */
	else if (nextents + ext_diff <= XFS_LINEAR_EXTS) {
		xfs_iext_realloc_direct(ifp, new_size);
		if (idx < nextents) {
			memmove(&ifp->if_u1.if_extents[idx + ext_diff],
				&ifp->if_u1.if_extents[idx],
				(nextents - idx) * sizeof(xfs_bmbt_rec_t));
			memset(&ifp->if_u1.if_extents[idx], 0, byte_diff);
		}
	}
	/* Indirection array */
	else {
		xfs_ext_irec_t	*erp;
		int		erp_idx = 0;
		int		page_idx = idx;

		ASSERT(nextents + ext_diff > XFS_LINEAR_EXTS);
		if (ifp->if_flags & XFS_IFEXTIREC) {
			erp = xfs_iext_idx_to_irec(ifp, &page_idx, &erp_idx, 1);
		} else {
			xfs_iext_irec_init(ifp);
			ASSERT(ifp->if_flags & XFS_IFEXTIREC);
			erp = ifp->if_u1.if_ext_irec;
		}
		/* Extents fit in target extent page */
		if (erp && erp->er_extcount + ext_diff <= XFS_LINEAR_EXTS) {
			if (page_idx < erp->er_extcount) {
				memmove(&erp->er_extbuf[page_idx + ext_diff],
					&erp->er_extbuf[page_idx],
					(erp->er_extcount - page_idx) *
					sizeof(xfs_bmbt_rec_t));
				memset(&erp->er_extbuf[page_idx], 0, byte_diff);
			}
			erp->er_extcount += ext_diff;
			xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		}
		/* Insert a new extent page */
		else if (erp) {
			xfs_iext_add_indirect_multi(ifp,
				erp_idx, page_idx, ext_diff);
		}
		/*
		 * If extent(s) are being appended to the last page in
		 * the indirection array and the new extent(s) don't fit
		 * in the page, then erp is NULL and erp_idx is set to
		 * the next index needed in the indirection array.
		 */
		else {
			int	count = ext_diff;

			while (count) {
				erp = xfs_iext_irec_new(ifp, erp_idx);
				erp->er_extcount = count;
				count -= MIN(count, (int)XFS_LINEAR_EXTS);
				if (count) {
					erp_idx++;
				}
			}
		}
	}
	ifp->if_bytes = new_size;
}

/*
 * This is called when incore extents are being added to the indirection
 * array and the new extents do not fit in the target extent list. The
 * erp_idx parameter contains the irec index for the target extent list
 * in the indirection array, and the idx parameter contains the extent
 * index within the list. The number of extents being added is stored
 * in the count parameter.
 *
 *    |-------|   |-------|
 *    |       |   |       |    idx - number of extents before idx
 *    |  idx  |   | count |
 *    |       |   |       |    count - number of extents being inserted at idx
 *    |-------|   |-------|
 *    | count |   | nex2  |    nex2 - number of extents after idx + count
 *    |-------|   |-------|
 */
void
xfs_iext_add_indirect_multi(
	xfs_ifork_t	*ifp,			/* inode fork pointer */
	int		erp_idx,		/* target extent irec index */
	xfs_extnum_t	idx,			/* index within target list */
	int		count)			/* new extents being added */
{
	int		byte_diff;		/* new bytes being added */
	xfs_ext_irec_t	*erp;			/* pointer to irec entry */
	xfs_extnum_t	ext_diff;		/* number of extents to add */
	xfs_extnum_t	ext_cnt;		/* new extents still needed */
	xfs_extnum_t	nex2;			/* extents after idx + count */
	xfs_bmbt_rec_t	*nex2_ep = NULL;	/* temp list for nex2 extents */
	int		nlists;			/* number of irec's (lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	erp = &ifp->if_u1.if_ext_irec[erp_idx];
	nex2 = erp->er_extcount - idx;
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;

	/*
	 * Save second part of target extent list
	 * (all extents past */
	if (nex2) {
		byte_diff = nex2 * sizeof(xfs_bmbt_rec_t);
		nex2_ep = (xfs_bmbt_rec_t *) kmem_alloc(byte_diff, KM_NOFS);
		memmove(nex2_ep, &erp->er_extbuf[idx], byte_diff);
		erp->er_extcount -= nex2;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, -nex2);
		memset(&erp->er_extbuf[idx], 0, byte_diff);
	}

	/*
	 * Add the new extents to the end of the target
	 * list, then allocate new irec record(s) and
	 * extent buffer(s) as needed to store the rest
	 * of the new extents.
	 */
	ext_cnt = count;
	ext_diff = MIN(ext_cnt, (int)XFS_LINEAR_EXTS - erp->er_extcount);
	if (ext_diff) {
		erp->er_extcount += ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		ext_cnt -= ext_diff;
	}
	while (ext_cnt) {
		erp_idx++;
		erp = xfs_iext_irec_new(ifp, erp_idx);
		ext_diff = MIN(ext_cnt, (int)XFS_LINEAR_EXTS);
		erp->er_extcount = ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		ext_cnt -= ext_diff;
	}

	/* Add nex2 extents back to indirection array */
	if (nex2) {
		xfs_extnum_t	ext_avail;
		int		i;

		byte_diff = nex2 * sizeof(xfs_bmbt_rec_t);
		ext_avail = XFS_LINEAR_EXTS - erp->er_extcount;
		i = 0;
		/*
		 * If nex2 extents fit in the current page, append
		 * nex2_ep after the new extents.
		 */
		if (nex2 <= ext_avail) {
			i = erp->er_extcount;
		}
		/*
		 * Otherwise, check if space is available in the
		 * next page.
		 */
		else if ((erp_idx < nlists - 1) &&
			 (nex2 <= (ext_avail = XFS_LINEAR_EXTS -
			  ifp->if_u1.if_ext_irec[erp_idx+1].er_extcount))) {
			erp_idx++;
			erp++;
			/* Create a hole for nex2 extents */
			memmove(&erp->er_extbuf[nex2], erp->er_extbuf,
				erp->er_extcount * sizeof(xfs_bmbt_rec_t));
		}
		/*
		 * Final choice, create a new extent page for
		 * nex2 extents.
		 */
		else {
			erp_idx++;
			erp = xfs_iext_irec_new(ifp, erp_idx);
		}
		memmove(&erp->er_extbuf[i], nex2_ep, byte_diff);
		kmem_free(nex2_ep);
		erp->er_extcount += nex2;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, nex2);
	}
}

/*
 * This is called when the amount of space required for incore file
 * extents needs to be decreased. The ext_diff parameter stores the
 * number of extents to be removed and the idx parameter contains
 * the extent index where the extents will be removed from.
 *
 * If the amount of space needed has decreased below the linear
 * limit, XFS_IEXT_BUFSZ, then switch to using the contiguous
 * extent array.  Otherwise, use kmem_realloc() to adjust the
 * size to what is needed.
 */
void
xfs_iext_remove(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff,	/* number of extents to remove */
	int		state)		/* type of extent conversion */
{
	xfs_ifork_t	*ifp = (state & BMAP_ATTRFORK) ? ip->i_afp : &ip->i_df;
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		new_size;	/* size of extents after removal */

	trace_xfs_iext_remove(ip, idx, state, _RET_IP_);

	ASSERT(ext_diff > 0);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	new_size = (nextents - ext_diff) * sizeof(xfs_bmbt_rec_t);

	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	} else if (ifp->if_flags & XFS_IFEXTIREC) {
		xfs_iext_remove_indirect(ifp, idx, ext_diff);
	} else if (ifp->if_real_bytes) {
		xfs_iext_remove_direct(ifp, idx, ext_diff);
	} else {
		xfs_iext_remove_inline(ifp, idx, ext_diff);
	}
	ifp->if_bytes = new_size;
}

/*
 * This removes ext_diff extents from the inline buffer, beginning
 * at extent index idx.
 */
void
xfs_iext_remove_inline(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff)	/* number of extents to remove */
{
	int		nextents;	/* number of extents in file */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	ASSERT(idx < XFS_INLINE_EXTS);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(((nextents - ext_diff) > 0) &&
		(nextents - ext_diff) < XFS_INLINE_EXTS);

	if (idx + ext_diff < nextents) {
		memmove(&ifp->if_u2.if_inline_ext[idx],
			&ifp->if_u2.if_inline_ext[idx + ext_diff],
			(nextents - (idx + ext_diff)) *
			 sizeof(xfs_bmbt_rec_t));
		memset(&ifp->if_u2.if_inline_ext[nextents - ext_diff],
			0, ext_diff * sizeof(xfs_bmbt_rec_t));
	} else {
		memset(&ifp->if_u2.if_inline_ext[idx], 0,
			ext_diff * sizeof(xfs_bmbt_rec_t));
	}
}

/*
 * This removes ext_diff extents from a linear (direct) extent list,
 * beginning at extent index idx. If the extents are being removed
 * from the end of the list (ie. truncate) then we just need to re-
 * allocate the list to remove the extra space. Otherwise, if the
 * extents are being removed from the middle of the existing extent
 * entries, then we first need to move the extent records beginning
 * at idx + ext_diff up in the list to overwrite the records being
 * removed, then remove the extra space via kmem_realloc.
 */
void
xfs_iext_remove_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff)	/* number of extents to remove */
{
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		new_size;	/* size of extents after removal */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	new_size = ifp->if_bytes -
		(ext_diff * sizeof(xfs_bmbt_rec_t));
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);

	if (new_size == 0) {
		xfs_iext_destroy(ifp);
		return;
	}
	/* Move extents up in the list (if needed) */
	if (idx + ext_diff < nextents) {
		memmove(&ifp->if_u1.if_extents[idx],
			&ifp->if_u1.if_extents[idx + ext_diff],
			(nextents - (idx + ext_diff)) *
			 sizeof(xfs_bmbt_rec_t));
	}
	memset(&ifp->if_u1.if_extents[nextents - ext_diff],
		0, ext_diff * sizeof(xfs_bmbt_rec_t));
	/*
	 * Reallocate the direct extent list. If the extents
	 * will fit inside the inode then xfs_iext_realloc_direct
	 * will switch from direct to inline extent allocation
	 * mode for us.
	 */
	xfs_iext_realloc_direct(ifp, new_size);
	ifp->if_bytes = new_size;
}

/*
 * This is called when incore extents are being removed from the
 * indirection array and the extents being removed span multiple extent
 * buffers. The idx parameter contains the file extent index where we
 * want to begin removing extents, and the count parameter contains
 * how many extents need to be removed.
 *
 *    |-------|   |-------|
 *    | nex1  |   |       |    nex1 - number of extents before idx
 *    |-------|   | count |
 *    |       |   |       |    count - number of extents being removed at idx
 *    | count |   |-------|
 *    |       |   | nex2  |    nex2 - number of extents after idx + count
 *    |-------|   |-------|
 */
void
xfs_iext_remove_indirect(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing extents */
	int		count)		/* number of extents to remove */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		erp_idx = 0;	/* indirection array index */
	xfs_extnum_t	ext_cnt;	/* extents left to remove */
	xfs_extnum_t	ext_diff;	/* extents to remove in current list */
	xfs_extnum_t	nex1;		/* number of extents before idx */
	xfs_extnum_t	nex2;		/* extents after idx + count */
	int		page_idx = idx;	/* index in target extent list */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	erp = xfs_iext_idx_to_irec(ifp,  &page_idx, &erp_idx, 0);
	ASSERT(erp != NULL);
	nex1 = page_idx;
	ext_cnt = count;
	while (ext_cnt) {
		nex2 = MAX((erp->er_extcount - (nex1 + ext_cnt)), 0);
		ext_diff = MIN(ext_cnt, (erp->er_extcount - nex1));
		/*
		 * Check for deletion of entire list;
		 * xfs_iext_irec_remove() updates extent offsets.
		 */
		if (ext_diff == erp->er_extcount) {
			xfs_iext_irec_remove(ifp, erp_idx);
			ext_cnt -= ext_diff;
			nex1 = 0;
			if (ext_cnt) {
				ASSERT(erp_idx < ifp->if_real_bytes /
					XFS_IEXT_BUFSZ);
				erp = &ifp->if_u1.if_ext_irec[erp_idx];
				nex1 = 0;
				continue;
			} else {
				break;
			}
		}
		/* Move extents up (if needed) */
		if (nex2) {
			memmove(&erp->er_extbuf[nex1],
				&erp->er_extbuf[nex1 + ext_diff],
				nex2 * sizeof(xfs_bmbt_rec_t));
		}
		/* Zero out rest of page */
		memset(&erp->er_extbuf[nex1 + nex2], 0, (XFS_IEXT_BUFSZ -
			((nex1 + nex2) * sizeof(xfs_bmbt_rec_t))));
		/* Update remaining counters */
		erp->er_extcount -= ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, -ext_diff);
		ext_cnt -= ext_diff;
		nex1 = 0;
		erp_idx++;
		erp++;
	}
	ifp->if_bytes -= count * sizeof(xfs_bmbt_rec_t);
	xfs_iext_irec_compact(ifp);
}

/*
 * Create, destroy, or resize a linear (direct) block of extents.
 */
void
xfs_iext_realloc_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* new size of extents */
{
	int		rnew_size;	/* real new size of extents */

	rnew_size = new_size;

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC) ||
		((new_size >= 0) && (new_size <= XFS_IEXT_BUFSZ) &&
		 (new_size != ifp->if_real_bytes)));

	/* Free extent records */
	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	}
	/* Resize direct extent list and zero any new bytes */
	else if (ifp->if_real_bytes) {
		/* Check if extents will fit inside the inode */
		if (new_size <= XFS_INLINE_EXTS * sizeof(xfs_bmbt_rec_t)) {
			xfs_iext_direct_to_inline(ifp, new_size /
				(uint)sizeof(xfs_bmbt_rec_t));
			ifp->if_bytes = new_size;
			return;
		}
		if (!is_power_of_2(new_size)){
			rnew_size = roundup_pow_of_two(new_size);
		}
		if (rnew_size != ifp->if_real_bytes) {
			ifp->if_u1.if_extents =
				kmem_realloc(ifp->if_u1.if_extents,
						rnew_size,
						ifp->if_real_bytes, KM_NOFS);
		}
		if (rnew_size > ifp->if_real_bytes) {
			memset(&ifp->if_u1.if_extents[ifp->if_bytes /
				(uint)sizeof(xfs_bmbt_rec_t)], 0,
				rnew_size - ifp->if_real_bytes);
		}
	}
	/*
	 * Switch from the inline extent buffer to a direct
	 * extent list. Be sure to include the inline extent
	 * bytes in new_size.
	 */
	else {
		new_size += ifp->if_bytes;
		if (!is_power_of_2(new_size)) {
			rnew_size = roundup_pow_of_two(new_size);
		}
		xfs_iext_inline_to_direct(ifp, rnew_size);
	}
	ifp->if_real_bytes = rnew_size;
	ifp->if_bytes = new_size;
}

/*
 * Switch from linear (direct) extent records to inline buffer.
 */
void
xfs_iext_direct_to_inline(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	nextents)	/* number of extents in file */
{
	ASSERT(ifp->if_flags & XFS_IFEXTENTS);
	ASSERT(nextents <= XFS_INLINE_EXTS);
	/*
	 * The inline buffer was zeroed when we switched
	 * from inline to direct extent allocation mode,
	 * so we don't need to clear it here.
	 */
	memcpy(ifp->if_u2.if_inline_ext, ifp->if_u1.if_extents,
		nextents * sizeof(xfs_bmbt_rec_t));
	kmem_free(ifp->if_u1.if_extents);
	ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
	ifp->if_real_bytes = 0;
}

/*
 * Switch from inline buffer to linear (direct) extent records.
 * new_size should already be rounded up to the next power of 2
 * by the caller (when appropriate), so use new_size as it is.
 * However, since new_size may be rounded up, we can't update
 * if_bytes here. It is the caller's responsibility to update
 * if_bytes upon return.
 */
void
xfs_iext_inline_to_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* number of extents in file */
{
	ifp->if_u1.if_extents = kmem_alloc(new_size, KM_NOFS);
	memset(ifp->if_u1.if_extents, 0, new_size);
	if (ifp->if_bytes) {
		memcpy(ifp->if_u1.if_extents, ifp->if_u2.if_inline_ext,
			ifp->if_bytes);
		memset(ifp->if_u2.if_inline_ext, 0, XFS_INLINE_EXTS *
			sizeof(xfs_bmbt_rec_t));
	}
	ifp->if_real_bytes = new_size;
}

/*
 * Resize an extent indirection array to new_size bytes.
 */
STATIC void
xfs_iext_realloc_indirect(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* new indirection array size */
{
	int		nlists;		/* number of irec's (ex lists) */
	int		size;		/* current indirection array size */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	size = nlists * sizeof(xfs_ext_irec_t);
	ASSERT(ifp->if_real_bytes);
	ASSERT((new_size >= 0) && (new_size != size));
	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	} else {
		ifp->if_u1.if_ext_irec = (xfs_ext_irec_t *)
			kmem_realloc(ifp->if_u1.if_ext_irec,
				new_size, size, KM_NOFS);
	}
}

/*
 * Switch from indirection array to linear (direct) extent allocations.
 */
STATIC void
xfs_iext_indirect_to_direct(
	 xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_bmbt_rec_host_t *ep;	/* extent record pointer */
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		size;		/* size of file extents */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(nextents <= XFS_LINEAR_EXTS);
	size = nextents * sizeof(xfs_bmbt_rec_t);

	xfs_iext_irec_compact_pages(ifp);
	ASSERT(ifp->if_real_bytes == XFS_IEXT_BUFSZ);

	ep = ifp->if_u1.if_ext_irec->er_extbuf;
	kmem_free(ifp->if_u1.if_ext_irec);
	ifp->if_flags &= ~XFS_IFEXTIREC;
	ifp->if_u1.if_extents = ep;
	ifp->if_bytes = size;
	if (nextents < XFS_LINEAR_EXTS) {
		xfs_iext_realloc_direct(ifp, size);
	}
}

/*
 * Free incore file extents.
 */
void
xfs_iext_destroy(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	if (ifp->if_flags & XFS_IFEXTIREC) {
		int	erp_idx;
		int	nlists;

		nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
		for (erp_idx = nlists - 1; erp_idx >= 0 ; erp_idx--) {
			xfs_iext_irec_remove(ifp, erp_idx);
		}
		ifp->if_flags &= ~XFS_IFEXTIREC;
	} else if (ifp->if_real_bytes) {
		kmem_free(ifp->if_u1.if_extents);
	} else if (ifp->if_bytes) {
		memset(ifp->if_u2.if_inline_ext, 0, XFS_INLINE_EXTS *
			sizeof(xfs_bmbt_rec_t));
	}
	ifp->if_u1.if_extents = NULL;
	ifp->if_real_bytes = 0;
	ifp->if_bytes = 0;
}

/*
 * Return a pointer to the extent record for file system block bno.
 */
xfs_bmbt_rec_host_t *			/* pointer to found extent record */
xfs_iext_bno_to_ext(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_fileoff_t	bno,		/* block number to search for */
	xfs_extnum_t	*idxp)		/* index of target extent */
{
	xfs_bmbt_rec_host_t *base;	/* pointer to first extent */
	xfs_filblks_t	blockcount = 0;	/* number of blocks in extent */
	xfs_bmbt_rec_host_t *ep = NULL;	/* pointer to target extent */
	xfs_ext_irec_t	*erp = NULL;	/* indirection array pointer */
	int		high;		/* upper boundary in search */
	xfs_extnum_t	idx = 0;	/* index of target extent */
	int		low;		/* lower boundary in search */
	xfs_extnum_t	nextents;	/* number of file extents */
	xfs_fileoff_t	startoff = 0;	/* start offset of extent */

	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	if (nextents == 0) {
		*idxp = 0;
		return NULL;
	}
	low = 0;
	if (ifp->if_flags & XFS_IFEXTIREC) {
		/* Find target extent list */
		int	erp_idx = 0;
		erp = xfs_iext_bno_to_irec(ifp, bno, &erp_idx);
		base = erp->er_extbuf;
		high = erp->er_extcount - 1;
	} else {
		base = ifp->if_u1.if_extents;
		high = nextents - 1;
	}
	/* Binary search extent records */
	while (low <= high) {
		idx = (low + high) >> 1;
		ep = base + idx;
		startoff = xfs_bmbt_get_startoff(ep);
		blockcount = xfs_bmbt_get_blockcount(ep);
		if (bno < startoff) {
			high = idx - 1;
		} else if (bno >= startoff + blockcount) {
			low = idx + 1;
		} else {
			/* Convert back to file-based extent index */
			if (ifp->if_flags & XFS_IFEXTIREC) {
				idx += erp->er_extoff;
			}
			*idxp = idx;
			return ep;
		}
	}
	/* Convert back to file-based extent index */
	if (ifp->if_flags & XFS_IFEXTIREC) {
		idx += erp->er_extoff;
	}
	if (bno >= startoff + blockcount) {
		if (++idx == nextents) {
			ep = NULL;
		} else {
			ep = xfs_iext_get_ext(ifp, idx);
		}
	}
	*idxp = idx;
	return ep;
}

/*
 * Return a pointer to the indirection array entry containing the
 * extent record for filesystem block bno. Store the index of the
 * target irec in *erp_idxp.
 */
xfs_ext_irec_t *			/* pointer to found extent record */
xfs_iext_bno_to_irec(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_fileoff_t	bno,		/* block number to search for */
	int		*erp_idxp)	/* irec index of target ext list */
{
	xfs_ext_irec_t	*erp = NULL;	/* indirection array pointer */
	xfs_ext_irec_t	*erp_next;	/* next indirection array entry */
	int		erp_idx;	/* indirection array index */
	int		nlists;		/* number of extent irec's (lists) */
	int		high;		/* binary search upper limit */
	int		low;		/* binary search lower limit */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp_idx = 0;
	low = 0;
	high = nlists - 1;
	while (low <= high) {
		erp_idx = (low + high) >> 1;
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		erp_next = erp_idx < nlists - 1 ? erp + 1 : NULL;
		if (bno < xfs_bmbt_get_startoff(erp->er_extbuf)) {
			high = erp_idx - 1;
		} else if (erp_next && bno >=
			   xfs_bmbt_get_startoff(erp_next->er_extbuf)) {
			low = erp_idx + 1;
		} else {
			break;
		}
	}
	*erp_idxp = erp_idx;
	return erp;
}

/*
 * Return a pointer to the indirection array entry containing the
 * extent record at file extent index *idxp. Store the index of the
 * target irec in *erp_idxp and store the page index of the target
 * extent record in *idxp.
 */
xfs_ext_irec_t *
xfs_iext_idx_to_irec(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	*idxp,		/* extent index (file -> page) */
	int		*erp_idxp,	/* pointer to target irec */
	int		realloc)	/* new bytes were just added */
{
	xfs_ext_irec_t	*prev;		/* pointer to previous irec */
	xfs_ext_irec_t	*erp = NULL;	/* pointer to current irec */
	int		erp_idx;	/* indirection array index */
	int		nlists;		/* number of irec's (ex lists) */
	int		high;		/* binary search upper limit */
	int		low;		/* binary search lower limit */
	xfs_extnum_t	page_idx = *idxp; /* extent index in target list */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	ASSERT(page_idx >= 0);
	ASSERT(page_idx <= ifp->if_bytes / sizeof(xfs_bmbt_rec_t));
	ASSERT(page_idx < ifp->if_bytes / sizeof(xfs_bmbt_rec_t) || realloc);

	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp_idx = 0;
	low = 0;
	high = nlists - 1;

	/* Binary search extent irec's */
	while (low <= high) {
		erp_idx = (low + high) >> 1;
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		prev = erp_idx > 0 ? erp - 1 : NULL;
		if (page_idx < erp->er_extoff || (page_idx == erp->er_extoff &&
		     realloc && prev && prev->er_extcount < XFS_LINEAR_EXTS)) {
			high = erp_idx - 1;
		} else if (page_idx > erp->er_extoff + erp->er_extcount ||
			   (page_idx == erp->er_extoff + erp->er_extcount &&
			    !realloc)) {
			low = erp_idx + 1;
		} else if (page_idx == erp->er_extoff + erp->er_extcount &&
			   erp->er_extcount == XFS_LINEAR_EXTS) {
			ASSERT(realloc);
			page_idx = 0;
			erp_idx++;
			erp = erp_idx < nlists ? erp + 1 : NULL;
			break;
		} else {
			page_idx -= erp->er_extoff;
			break;
		}
	}
	*idxp = page_idx;
	*erp_idxp = erp_idx;
	return(erp);
}

/*
 * Allocate and initialize an indirection array once the space needed
 * for incore extents increases above XFS_IEXT_BUFSZ.
 */
void
xfs_iext_irec_init(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	xfs_extnum_t	nextents;	/* number of extents in file */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(nextents <= XFS_LINEAR_EXTS);

	erp = kmem_alloc(sizeof(xfs_ext_irec_t), KM_NOFS);

	if (nextents == 0) {
		ifp->if_u1.if_extents = kmem_alloc(XFS_IEXT_BUFSZ, KM_NOFS);
	} else if (!ifp->if_real_bytes) {
		xfs_iext_inline_to_direct(ifp, XFS_IEXT_BUFSZ);
	} else if (ifp->if_real_bytes < XFS_IEXT_BUFSZ) {
		xfs_iext_realloc_direct(ifp, XFS_IEXT_BUFSZ);
	}
	erp->er_extbuf = ifp->if_u1.if_extents;
	erp->er_extcount = nextents;
	erp->er_extoff = 0;

	ifp->if_flags |= XFS_IFEXTIREC;
	ifp->if_real_bytes = XFS_IEXT_BUFSZ;
	ifp->if_bytes = nextents * sizeof(xfs_bmbt_rec_t);
	ifp->if_u1.if_ext_irec = erp;

	return;
}

/*
 * Allocate and initialize a new entry in the indirection array.
 */
xfs_ext_irec_t *
xfs_iext_irec_new(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx)	/* index for new irec */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;

	/* Resize indirection array */
	xfs_iext_realloc_indirect(ifp, ++nlists *
				  sizeof(xfs_ext_irec_t));
	/*
	 * Move records down in the array so the
	 * new page can use erp_idx.
	 */
	erp = ifp->if_u1.if_ext_irec;
	for (i = nlists - 1; i > erp_idx; i--) {
		memmove(&erp[i], &erp[i-1], sizeof(xfs_ext_irec_t));
	}
	ASSERT(i == erp_idx);

	/* Initialize new extent record */
	erp = ifp->if_u1.if_ext_irec;
	erp[erp_idx].er_extbuf = kmem_alloc(XFS_IEXT_BUFSZ, KM_NOFS);
	ifp->if_real_bytes = nlists * XFS_IEXT_BUFSZ;
	memset(erp[erp_idx].er_extbuf, 0, XFS_IEXT_BUFSZ);
	erp[erp_idx].er_extcount = 0;
	erp[erp_idx].er_extoff = erp_idx > 0 ?
		erp[erp_idx-1].er_extoff + erp[erp_idx-1].er_extcount : 0;
	return (&erp[erp_idx]);
}

/*
 * Remove a record from the indirection array.
 */
void
xfs_iext_irec_remove(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx)	/* irec index to remove */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp = &ifp->if_u1.if_ext_irec[erp_idx];
	if (erp->er_extbuf) {
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1,
			-erp->er_extcount);
		kmem_free(erp->er_extbuf);
	}
	/* Compact extent records */
	erp = ifp->if_u1.if_ext_irec;
	for (i = erp_idx; i < nlists - 1; i++) {
		memmove(&erp[i], &erp[i+1], sizeof(xfs_ext_irec_t));
	}
	/*
	 * Manually free the last extent record from the indirection
	 * array.  A call to xfs_iext_realloc_indirect() with a size
	 * of zero would result in a call to xfs_iext_destroy() which
	 * would in turn call this function again, creating a nasty
	 * infinite loop.
	 */
	if (--nlists) {
		xfs_iext_realloc_indirect(ifp,
			nlists * sizeof(xfs_ext_irec_t));
	} else {
		kmem_free(ifp->if_u1.if_ext_irec);
	}
	ifp->if_real_bytes = nlists * XFS_IEXT_BUFSZ;
}

/*
 * This is called to clean up large amounts of unused memory allocated
 * by the indirection array.  Before compacting anything though, verify
 * that the indirection array is still needed and switch back to the
 * linear extent list (or even the inline buffer) if possible.  The
 * compaction policy is as follows:
 *
 *    Full Compaction: Extents fit into a single page (or inline buffer)
 * Partial Compaction: Extents occupy less than 50% of allocated space
 *      No Compaction: Extents occupy at least 50% of allocated space
 */
void
xfs_iext_irec_compact(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);

	if (nextents == 0) {
		xfs_iext_destroy(ifp);
	} else if (nextents <= XFS_INLINE_EXTS) {
		xfs_iext_indirect_to_direct(ifp);
		xfs_iext_direct_to_inline(ifp, nextents);
	} else if (nextents <= XFS_LINEAR_EXTS) {
		xfs_iext_indirect_to_direct(ifp);
	} else if (nextents < (nlists * XFS_LINEAR_EXTS) >> 1) {
		xfs_iext_irec_compact_pages(ifp);
	}
}

/*
 * Combine extents from neighboring extent pages.
 */
void
xfs_iext_irec_compact_pages(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_ext_irec_t	*erp, *erp_next;/* pointers to irec entries */
	int		erp_idx = 0;	/* indirection array index */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	while (erp_idx < nlists - 1) {
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		erp_next = erp + 1;
		if (erp_next->er_extcount <=
		    (XFS_LINEAR_EXTS - erp->er_extcount)) {
			memcpy(&erp->er_extbuf[erp->er_extcount],
				erp_next->er_extbuf, erp_next->er_extcount *
				sizeof(xfs_bmbt_rec_t));
			erp->er_extcount += erp_next->er_extcount;
			/*
			 * Free page before removing extent record
			 * so er_extoffs don't get modified in
			 * xfs_iext_irec_remove.
			 */
			kmem_free(erp_next->er_extbuf);
			erp_next->er_extbuf = NULL;
			xfs_iext_irec_remove(ifp, erp_idx + 1);
			nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
		} else {
			erp_idx++;
		}
	}
}

/*
 * This is called to update the er_extoff field in the indirection
 * array when extents have been added or removed from one of the
 * extent lists. erp_idx contains the irec index to begin updating
 * at and ext_diff contains the number of extents that were added
 * or removed.
 */
void
xfs_iext_irec_update_extoffs(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx,	/* irec index to update */
	int		ext_diff)	/* number of new extents */
{
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	for (i = erp_idx; i < nlists; i++) {
		ifp->if_u1.if_ext_irec[i].er_extoff += ext_diff;
	}
}
