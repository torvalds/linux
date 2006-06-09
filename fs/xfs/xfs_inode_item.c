/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_buf_item.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_rw.h"


kmem_zone_t	*xfs_ili_zone;		/* inode log item zone */

/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We need one iovec for the inode log format structure, one for the
 * inode core, and possibly one for the inode data/extents/b-tree root
 * and one for the inode attribute data/extents/b-tree root.
 */
STATIC uint
xfs_inode_item_size(
	xfs_inode_log_item_t	*iip)
{
	uint		nvecs;
	xfs_inode_t	*ip;

	ip = iip->ili_inode;
	nvecs = 2;

	/*
	 * Only log the data/extents/b-tree root if there is something
	 * left to log.
	 */
	iip->ili_format.ilf_fields |= XFS_ILOG_CORE;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DEXT) &&
		    (ip->i_d.di_nextents > 0) &&
		    (ip->i_df.if_bytes > 0)) {
			ASSERT(ip->i_df.if_u1.if_extents != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DEXT;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		ASSERT(ip->i_df.if_ext_max ==
		       XFS_IFORK_DSIZE(ip) / (uint)sizeof(xfs_bmbt_rec_t));
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DBROOT) &&
		    (ip->i_df.if_broot_bytes > 0)) {
			ASSERT(ip->i_df.if_broot != NULL);
			nvecs++;
		} else {
			ASSERT(!(iip->ili_format.ilf_fields &
				 XFS_ILOG_DBROOT));
#ifdef XFS_TRANS_DEBUG
			if (iip->ili_root_size > 0) {
				ASSERT(iip->ili_root_size ==
				       ip->i_df.if_broot_bytes);
				ASSERT(memcmp(iip->ili_orig_root,
					    ip->i_df.if_broot,
					    iip->ili_root_size) == 0);
			} else {
				ASSERT(ip->i_df.if_broot_bytes == 0);
			}
#endif
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DBROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DEXT | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DDATA) &&
		    (ip->i_df.if_bytes > 0)) {
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DDATA;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_UUID);
		break;

	case XFS_DINODE_FMT_UUID:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_DEV);
		break;

	default:
		ASSERT(0);
		break;
	}

	/*
	 * If there are no attributes associated with this file,
	 * then there cannot be anything more to log.
	 * Clear all attribute-related log flags.
	 */
	if (!XFS_IFORK_Q(ip)) {
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT | XFS_ILOG_AEXT);
		return nvecs;
	}

	/*
	 * Log any necessary attribute data.
	 */
	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_AEXT) &&
		    (ip->i_d.di_anextents > 0) &&
		    (ip->i_afp->if_bytes > 0)) {
			ASSERT(ip->i_afp->if_u1.if_extents != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_AEXT;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_AEXT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_ABROOT) &&
		    (ip->i_afp->if_broot_bytes > 0)) {
			ASSERT(ip->i_afp->if_broot != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_ABROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_AEXT | XFS_ILOG_ABROOT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_ADATA) &&
		    (ip->i_afp->if_bytes > 0)) {
			ASSERT(ip->i_afp->if_u1.if_data != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_ADATA;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	return nvecs;
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given inode log item.  It fills the first item with an inode
 * log format structure, the second with the on-disk inode structure,
 * and a possible third and/or fourth with the inode data/extents/b-tree
 * root and inode attributes data/extents/b-tree root.
 */
STATIC void
xfs_inode_item_format(
	xfs_inode_log_item_t	*iip,
	xfs_log_iovec_t		*log_vector)
{
	uint			nvecs;
	xfs_log_iovec_t		*vecp;
	xfs_inode_t		*ip;
	size_t			data_bytes;
	xfs_bmbt_rec_t		*ext_buffer;
	int			nrecs;
	xfs_mount_t		*mp;

	ip = iip->ili_inode;
	vecp = log_vector;

	vecp->i_addr = (xfs_caddr_t)&iip->ili_format;
	vecp->i_len  = sizeof(xfs_inode_log_format_t);
	XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IFORMAT);
	vecp++;
	nvecs	     = 1;

	/*
	 * Clear i_update_core if the timestamps (or any other
	 * non-transactional modification) need flushing/logging
	 * and we're about to log them with the rest of the core.
	 *
	 * This is the same logic as xfs_iflush() but this code can't
	 * run at the same time as xfs_iflush because we're in commit
	 * processing here and so we have the inode lock held in
	 * exclusive mode.  Although it doesn't really matter
	 * for the timestamps if both routines were to grab the
	 * timestamps or not.  That would be ok.
	 *
	 * We clear i_update_core before copying out the data.
	 * This is for coordination with our timestamp updates
	 * that don't hold the inode lock. They will always
	 * update the timestamps BEFORE setting i_update_core,
	 * so if we clear i_update_core after they set it we
	 * are guaranteed to see their updates to the timestamps
	 * either here.  Likewise, if they set it after we clear it
	 * here, we'll see it either on the next commit of this
	 * inode or the next time the inode gets flushed via
	 * xfs_iflush().  This depends on strongly ordered memory
	 * semantics, but we have that.  We use the SYNCHRONIZE
	 * macro to make sure that the compiler does not reorder
	 * the i_update_core access below the data copy below.
	 */
	if (ip->i_update_core)  {
		ip->i_update_core = 0;
		SYNCHRONIZE();
	}

	/*
	 * We don't have to worry about re-ordering here because
	 * the update_size field is protected by the inode lock
	 * and we have that held in exclusive mode.
	 */
	if (ip->i_update_size)
		ip->i_update_size = 0;

	/*
	 * Make sure to get the latest atime from the Linux inode.
	 */
	xfs_synchronize_atime(ip);

	vecp->i_addr = (xfs_caddr_t)&ip->i_d;
	vecp->i_len  = sizeof(xfs_dinode_core_t);
	XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_ICORE);
	vecp++;
	nvecs++;
	iip->ili_format.ilf_fields |= XFS_ILOG_CORE;

	/*
	 * If this is really an old format inode, then we need to
	 * log it as such.  This means that we have to copy the link
	 * count from the new field to the old.  We don't have to worry
	 * about the new fields, because nothing trusts them as long as
	 * the old inode version number is there.  If the superblock already
	 * has a new version number, then we don't bother converting back.
	 */
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_version == XFS_DINODE_VERSION_1 ||
	       XFS_SB_VERSION_HASNLINK(&mp->m_sb));
	if (ip->i_d.di_version == XFS_DINODE_VERSION_1) {
		if (!XFS_SB_VERSION_HASNLINK(&mp->m_sb)) {
			/*
			 * Convert it back.
			 */
			ASSERT(ip->i_d.di_nlink <= XFS_MAXLINK_1);
			ip->i_d.di_onlink = ip->i_d.di_nlink;
		} else {
			/*
			 * The superblock version has already been bumped,
			 * so just make the conversion to the new inode
			 * format permanent.
			 */
			ip->i_d.di_version = XFS_DINODE_VERSION_2;
			ip->i_d.di_onlink = 0;
			memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
		}
	}

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEXT) {
			ASSERT(ip->i_df.if_bytes > 0);
			ASSERT(ip->i_df.if_u1.if_extents != NULL);
			ASSERT(ip->i_d.di_nextents > 0);
			ASSERT(iip->ili_extents_buf == NULL);
			nrecs = ip->i_df.if_bytes /
				(uint)sizeof(xfs_bmbt_rec_t);
			ASSERT(nrecs > 0);
#ifdef XFS_NATIVE_HOST
			if (nrecs == ip->i_d.di_nextents) {
				/*
				 * There are no delayed allocation
				 * extents, so just point to the
				 * real extents array.
				 */
				vecp->i_addr =
					(char *)(ip->i_df.if_u1.if_extents);
				vecp->i_len = ip->i_df.if_bytes;
				XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IEXT);
			} else
#endif
			{
				/*
				 * There are delayed allocation extents
				 * in the inode, or we need to convert
				 * the extents to on disk format.
				 * Use xfs_iextents_copy()
				 * to copy only the real extents into
				 * a separate buffer.  We'll free the
				 * buffer in the unlock routine.
				 */
				ext_buffer = kmem_alloc(ip->i_df.if_bytes,
					KM_SLEEP);
				iip->ili_extents_buf = ext_buffer;
				vecp->i_addr = (xfs_caddr_t)ext_buffer;
				vecp->i_len = xfs_iextents_copy(ip, ext_buffer,
						XFS_DATA_FORK);
				XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IEXT);
			}
			ASSERT(vecp->i_len <= ip->i_df.if_bytes);
			iip->ili_format.ilf_dsize = vecp->i_len;
			vecp++;
			nvecs++;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DDATA | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DBROOT) {
			ASSERT(ip->i_df.if_broot_bytes > 0);
			ASSERT(ip->i_df.if_broot != NULL);
			vecp->i_addr = (xfs_caddr_t)ip->i_df.if_broot;
			vecp->i_len = ip->i_df.if_broot_bytes;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IBROOT);
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = ip->i_df.if_broot_bytes;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DDATA) {
			ASSERT(ip->i_df.if_bytes > 0);
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);

			vecp->i_addr = (xfs_caddr_t)ip->i_df.if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_df.if_bytes, 4);
			ASSERT((ip->i_df.if_real_bytes == 0) ||
			       (ip->i_df.if_real_bytes == data_bytes));
			vecp->i_len = (int)data_bytes;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_ILOCAL);
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = (unsigned)data_bytes;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DDATA | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEV) {
			iip->ili_format.ilf_u.ilfu_rdev =
				ip->i_df.if_u2.if_rdev;
		}
		break;

	case XFS_DINODE_FMT_UUID:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DDATA | XFS_ILOG_DEV)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_UUID) {
			iip->ili_format.ilf_u.ilfu_uuid =
				ip->i_df.if_u2.if_uuid;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	/*
	 * If there are no attributes associated with the file,
	 * then we're done.
	 * Assert that no attribute-related log flags are set.
	 */
	if (!XFS_IFORK_Q(ip)) {
		ASSERT(nvecs == iip->ili_item.li_desc->lid_size);
		iip->ili_format.ilf_size = nvecs;
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ADATA | XFS_ILOG_ABROOT | XFS_ILOG_AEXT)));
		return;
	}

	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ADATA | XFS_ILOG_ABROOT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_AEXT) {
			ASSERT(ip->i_afp->if_bytes > 0);
			ASSERT(ip->i_afp->if_u1.if_extents != NULL);
			ASSERT(ip->i_d.di_anextents > 0);
#ifdef DEBUG
			nrecs = ip->i_afp->if_bytes /
				(uint)sizeof(xfs_bmbt_rec_t);
#endif
			ASSERT(nrecs > 0);
			ASSERT(nrecs == ip->i_d.di_anextents);
#ifdef XFS_NATIVE_HOST
			/*
			 * There are not delayed allocation extents
			 * for attributes, so just point at the array.
			 */
			vecp->i_addr = (char *)(ip->i_afp->if_u1.if_extents);
			vecp->i_len = ip->i_afp->if_bytes;
#else
			ASSERT(iip->ili_aextents_buf == NULL);
			/*
			 * Need to endian flip before logging
			 */
			ext_buffer = kmem_alloc(ip->i_afp->if_bytes,
				KM_SLEEP);
			iip->ili_aextents_buf = ext_buffer;
			vecp->i_addr = (xfs_caddr_t)ext_buffer;
			vecp->i_len = xfs_iextents_copy(ip, ext_buffer,
					XFS_ATTR_FORK);
#endif
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IATTR_EXT);
			iip->ili_format.ilf_asize = vecp->i_len;
			vecp++;
			nvecs++;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ADATA | XFS_ILOG_AEXT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_ABROOT) {
			ASSERT(ip->i_afp->if_broot_bytes > 0);
			ASSERT(ip->i_afp->if_broot != NULL);
			vecp->i_addr = (xfs_caddr_t)ip->i_afp->if_broot;
			vecp->i_len = ip->i_afp->if_broot_bytes;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IATTR_BROOT);
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = ip->i_afp->if_broot_bytes;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ABROOT | XFS_ILOG_AEXT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_ADATA) {
			ASSERT(ip->i_afp->if_bytes > 0);
			ASSERT(ip->i_afp->if_u1.if_data != NULL);

			vecp->i_addr = (xfs_caddr_t)ip->i_afp->if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_afp->if_bytes, 4);
			ASSERT((ip->i_afp->if_real_bytes == 0) ||
			       (ip->i_afp->if_real_bytes == data_bytes));
			vecp->i_len = (int)data_bytes;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_IATTR_LOCAL);
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = (unsigned)data_bytes;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	ASSERT(nvecs == iip->ili_item.li_desc->lid_size);
	iip->ili_format.ilf_size = nvecs;
}


/*
 * This is called to pin the inode associated with the inode log
 * item in memory so it cannot be written out.  Do this by calling
 * xfs_ipin() to bump the pin count in the inode while holding the
 * inode pin lock.
 */
STATIC void
xfs_inode_item_pin(
	xfs_inode_log_item_t	*iip)
{
	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	xfs_ipin(iip->ili_inode);
}


/*
 * This is called to unpin the inode associated with the inode log
 * item which was previously pinned with a call to xfs_inode_item_pin().
 * Just call xfs_iunpin() on the inode to do this.
 */
/* ARGSUSED */
STATIC void
xfs_inode_item_unpin(
	xfs_inode_log_item_t	*iip,
	int			stale)
{
	xfs_iunpin(iip->ili_inode);
}

/* ARGSUSED */
STATIC void
xfs_inode_item_unpin_remove(
	xfs_inode_log_item_t	*iip,
	xfs_trans_t		*tp)
{
	xfs_iunpin(iip->ili_inode);
}

/*
 * This is called to attempt to lock the inode associated with this
 * inode log item, in preparation for the push routine which does the actual
 * iflush.  Don't sleep on the inode lock or the flush lock.
 *
 * If the flush lock is already held, indicating that the inode has
 * been or is in the process of being flushed, then (ideally) we'd like to
 * see if the inode's buffer is still incore, and if so give it a nudge.
 * We delay doing so until the pushbuf routine, though, to avoid holding
 * the AIL lock across a call to the blackhole which is the buffer cache.
 * Also we don't want to sleep in any device strategy routines, which can happen
 * if we do the subsequent bawrite in here.
 */
STATIC uint
xfs_inode_item_trylock(
	xfs_inode_log_item_t	*iip)
{
	register xfs_inode_t	*ip;

	ip = iip->ili_inode;

	if (xfs_ipincount(ip) > 0) {
		return XFS_ITEM_PINNED;
	}

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
		return XFS_ITEM_LOCKED;
	}

	if (!xfs_iflock_nowait(ip)) {
		/*
		 * If someone else isn't already trying to push the inode
		 * buffer, we get to do it.
		 */
		if (iip->ili_pushbuf_flag == 0) {
			iip->ili_pushbuf_flag = 1;
#ifdef DEBUG
			iip->ili_push_owner = current_pid();
#endif
			/*
			 * Inode is left locked in shared mode.
			 * Pushbuf routine gets to unlock it.
			 */
			return XFS_ITEM_PUSHBUF;
		} else {
			/*
			 * We hold the AIL_LOCK, so we must specify the
			 * NONOTIFY flag so that we won't double trip.
			 */
			xfs_iunlock(ip, XFS_ILOCK_SHARED|XFS_IUNLOCK_NONOTIFY);
			return XFS_ITEM_FLUSHING;
		}
		/* NOTREACHED */
	}

	/* Stale items should force out the iclog */
	if (ip->i_flags & XFS_ISTALE) {
		xfs_ifunlock(ip);
		xfs_iunlock(ip, XFS_ILOCK_SHARED|XFS_IUNLOCK_NONOTIFY);
		return XFS_ITEM_PINNED;
	}

#ifdef DEBUG
	if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		ASSERT(iip->ili_format.ilf_fields != 0);
		ASSERT(iip->ili_logged == 0);
		ASSERT(iip->ili_item.li_flags & XFS_LI_IN_AIL);
	}
#endif
	return XFS_ITEM_SUCCESS;
}

/*
 * Unlock the inode associated with the inode log item.
 * Clear the fields of the inode and inode log item that
 * are specific to the current transaction.  If the
 * hold flags is set, do not unlock the inode.
 */
STATIC void
xfs_inode_item_unlock(
	xfs_inode_log_item_t	*iip)
{
	uint		hold;
	uint		iolocked;
	uint		lock_flags;
	xfs_inode_t	*ip;

	ASSERT(iip != NULL);
	ASSERT(iip->ili_inode->i_itemp != NULL);
	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_itemp->ili_flags &
		  XFS_ILI_IOLOCKED_EXCL)) ||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_itemp->ili_flags &
		  XFS_ILI_IOLOCKED_SHARED)) ||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_ACCESS));
	/*
	 * Clear the transaction pointer in the inode.
	 */
	ip = iip->ili_inode;
	ip->i_transp = NULL;

	/*
	 * If the inode needed a separate buffer with which to log
	 * its extents, then free it now.
	 */
	if (iip->ili_extents_buf != NULL) {
		ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
		ASSERT(ip->i_d.di_nextents > 0);
		ASSERT(iip->ili_format.ilf_fields & XFS_ILOG_DEXT);
		ASSERT(ip->i_df.if_bytes > 0);
		kmem_free(iip->ili_extents_buf, ip->i_df.if_bytes);
		iip->ili_extents_buf = NULL;
	}
	if (iip->ili_aextents_buf != NULL) {
		ASSERT(ip->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS);
		ASSERT(ip->i_d.di_anextents > 0);
		ASSERT(iip->ili_format.ilf_fields & XFS_ILOG_AEXT);
		ASSERT(ip->i_afp->if_bytes > 0);
		kmem_free(iip->ili_aextents_buf, ip->i_afp->if_bytes);
		iip->ili_aextents_buf = NULL;
	}

	/*
	 * Figure out if we should unlock the inode or not.
	 */
	hold = iip->ili_flags & XFS_ILI_HOLD;

	/*
	 * Before clearing out the flags, remember whether we
	 * are holding the inode's IO lock.
	 */
	iolocked = iip->ili_flags & XFS_ILI_IOLOCKED_ANY;

	/*
	 * Clear out the fields of the inode log item particular
	 * to the current transaction.
	 */
	iip->ili_ilock_recur = 0;
	iip->ili_iolock_recur = 0;
	iip->ili_flags = 0;

	/*
	 * Unlock the inode if XFS_ILI_HOLD was not set.
	 */
	if (!hold) {
		lock_flags = XFS_ILOCK_EXCL;
		if (iolocked & XFS_ILI_IOLOCKED_EXCL) {
			lock_flags |= XFS_IOLOCK_EXCL;
		} else if (iolocked & XFS_ILI_IOLOCKED_SHARED) {
			lock_flags |= XFS_IOLOCK_SHARED;
		}
		xfs_iput(iip->ili_inode, lock_flags);
	}
}

/*
 * This is called to find out where the oldest active copy of the
 * inode log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.  Since we always re-log
 * all dirty data in an inode, the latest copy in the on disk log
 * is the only one that matters.  Therefore, simply return the
 * given lsn.
 */
/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_inode_item_committed(
	xfs_inode_log_item_t	*iip,
	xfs_lsn_t		lsn)
{
	return (lsn);
}

/*
 * The transaction with the inode locked has aborted.  The inode
 * must not be dirty within the transaction (unless we're forcibly
 * shutting down).  We simply unlock just as if the transaction
 * had been cancelled.
 */
STATIC void
xfs_inode_item_abort(
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_item_unlock(iip);
	return;
}


/*
 * This gets called by xfs_trans_push_ail(), when IOP_TRYLOCK
 * failed to get the inode flush lock but did get the inode locked SHARED.
 * Here we're trying to see if the inode buffer is incore, and if so whether it's
 * marked delayed write. If that's the case, we'll initiate a bawrite on that
 * buffer to expedite the process.
 *
 * We aren't holding the AIL_LOCK (or the flush lock) when this gets called,
 * so it is inherently race-y.
 */
STATIC void
xfs_inode_item_pushbuf(
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	xfs_buf_t	*bp;
	uint		dopush;

	ip = iip->ili_inode;

	ASSERT(ismrlocked(&(ip->i_lock), MR_ACCESS));

	/*
	 * The ili_pushbuf_flag keeps others from
	 * trying to duplicate our effort.
	 */
	ASSERT(iip->ili_pushbuf_flag != 0);
	ASSERT(iip->ili_push_owner == current_pid());

	/*
	 * If flushlock isn't locked anymore, chances are that the
	 * inode flush completed and the inode was taken off the AIL.
	 * So, just get out.
	 */
	if ((valusema(&(ip->i_flock)) > 0)  ||
	    ((iip->ili_item.li_flags & XFS_LI_IN_AIL) == 0)) {
		iip->ili_pushbuf_flag = 0;
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return;
	}

	mp = ip->i_mount;
	bp = xfs_incore(mp->m_ddev_targp, iip->ili_format.ilf_blkno,
		    iip->ili_format.ilf_len, XFS_INCORE_TRYLOCK);

	if (bp != NULL) {
		if (XFS_BUF_ISDELAYWRITE(bp)) {
			/*
			 * We were racing with iflush because we don't hold
			 * the AIL_LOCK or the flush lock. However, at this point,
			 * we have the buffer, and we know that it's dirty.
			 * So, it's possible that iflush raced with us, and
			 * this item is already taken off the AIL.
			 * If not, we can flush it async.
			 */
			dopush = ((iip->ili_item.li_flags & XFS_LI_IN_AIL) &&
				  (valusema(&(ip->i_flock)) <= 0));
			iip->ili_pushbuf_flag = 0;
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			xfs_buftrace("INODE ITEM PUSH", bp);
			if (XFS_BUF_ISPINNED(bp)) {
				xfs_log_force(mp, (xfs_lsn_t)0,
					      XFS_LOG_FORCE);
			}
			if (dopush) {
				xfs_bawrite(mp, bp);
			} else {
				xfs_buf_relse(bp);
			}
		} else {
			iip->ili_pushbuf_flag = 0;
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			xfs_buf_relse(bp);
		}
		return;
	}
	/*
	 * We have to be careful about resetting pushbuf flag too early (above).
	 * Even though in theory we can do it as soon as we have the buflock,
	 * we don't want others to be doing work needlessly. They'll come to
	 * this function thinking that pushing the buffer is their
	 * responsibility only to find that the buffer is still locked by
	 * another doing the same thing
	 */
	iip->ili_pushbuf_flag = 0;
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return;
}


/*
 * This is called to asynchronously write the inode associated with this
 * inode log item out to disk. The inode will already have been locked by
 * a successful call to xfs_inode_item_trylock().
 */
STATIC void
xfs_inode_item_push(
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_t	*ip;

	ip = iip->ili_inode;

	ASSERT(ismrlocked(&(ip->i_lock), MR_ACCESS));
	ASSERT(valusema(&(ip->i_flock)) <= 0);
	/*
	 * Since we were able to lock the inode's flush lock and
	 * we found it on the AIL, the inode must be dirty.  This
	 * is because the inode is removed from the AIL while still
	 * holding the flush lock in xfs_iflush_done().  Thus, if
	 * we found it in the AIL and were able to obtain the flush
	 * lock without sleeping, then there must not have been
	 * anyone in the process of flushing the inode.
	 */
	ASSERT(XFS_FORCED_SHUTDOWN(ip->i_mount) ||
	       iip->ili_format.ilf_fields != 0);

	/*
	 * Write out the inode.  The completion routine ('iflush_done') will
	 * pull it from the AIL, mark it clean, unlock the flush lock.
	 */
	(void) xfs_iflush(ip, XFS_IFLUSH_ASYNC);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return;
}

/*
 * XXX rcc - this one really has to do something.  Probably needs
 * to stamp in a new field in the incore inode.
 */
/* ARGSUSED */
STATIC void
xfs_inode_item_committing(
	xfs_inode_log_item_t	*iip,
	xfs_lsn_t		lsn)
{
	iip->ili_last_lsn = lsn;
	return;
}

/*
 * This is the ops vector shared by all buf log items.
 */
STATIC struct xfs_item_ops xfs_inode_item_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_inode_item_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_inode_item_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_inode_item_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*, int))xfs_inode_item_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*, xfs_trans_t*))
					xfs_inode_item_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_inode_item_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_inode_item_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_inode_item_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_inode_item_push,
	.iop_abort	= (void(*)(xfs_log_item_t*))xfs_inode_item_abort,
	.iop_pushbuf	= (void(*)(xfs_log_item_t*))xfs_inode_item_pushbuf,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_inode_item_committing
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 */
void
xfs_inode_item_init(
	xfs_inode_t	*ip,
	xfs_mount_t	*mp)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(ip->i_itemp == NULL);
	iip = ip->i_itemp = kmem_zone_zalloc(xfs_ili_zone, KM_SLEEP);

	iip->ili_item.li_type = XFS_LI_INODE;
	iip->ili_item.li_ops = &xfs_inode_item_ops;
	iip->ili_item.li_mountp = mp;
	iip->ili_inode = ip;

	/*
	   We have zeroed memory. No need ...
	   iip->ili_extents_buf = NULL;
	   iip->ili_pushbuf_flag = 0;
	 */

	iip->ili_format.ilf_type = XFS_LI_INODE;
	iip->ili_format.ilf_ino = ip->i_ino;
	iip->ili_format.ilf_blkno = ip->i_blkno;
	iip->ili_format.ilf_len = ip->i_len;
	iip->ili_format.ilf_boffset = ip->i_boffset;
}

/*
 * Free the inode log item and any memory hanging off of it.
 */
void
xfs_inode_item_destroy(
	xfs_inode_t	*ip)
{
#ifdef XFS_TRANS_DEBUG
	if (ip->i_itemp->ili_root_size != 0) {
		kmem_free(ip->i_itemp->ili_orig_root,
			  ip->i_itemp->ili_root_size);
	}
#endif
	kmem_zone_free(xfs_ili_zone, ip->i_itemp);
}


/*
 * This is the inode flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the inode is
 * flushed to disk.  It is responsible for removing the inode item
 * from the AIL if it has not been re-logged, and unlocking the inode's
 * flush lock.
 */
/*ARGSUSED*/
void
xfs_iflush_done(
	xfs_buf_t		*bp,
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_t	*ip;
	SPLDECL(s);

	ip = iip->ili_inode;

	/*
	 * We only want to pull the item from the AIL if it is
	 * actually there and its location in the log has not
	 * changed since we started the flush.  Thus, we only bother
	 * if the ili_logged flag is set and the inode's lsn has not
	 * changed.  First we check the lsn outside
	 * the lock since it's cheaper, and then we recheck while
	 * holding the lock before removing the inode from the AIL.
	 */
	if (iip->ili_logged &&
	    (iip->ili_item.li_lsn == iip->ili_flush_lsn)) {
		AIL_LOCK(ip->i_mount, s);
		if (iip->ili_item.li_lsn == iip->ili_flush_lsn) {
			/*
			 * xfs_trans_delete_ail() drops the AIL lock.
			 */
			xfs_trans_delete_ail(ip->i_mount,
					     (xfs_log_item_t*)iip, s);
		} else {
			AIL_UNLOCK(ip->i_mount, s);
		}
	}

	iip->ili_logged = 0;

	/*
	 * Clear the ili_last_fields bits now that we know that the
	 * data corresponding to them is safely on disk.
	 */
	iip->ili_last_fields = 0;

	/*
	 * Release the inode's flush lock since we're done with it.
	 */
	xfs_ifunlock(ip);

	return;
}

/*
 * This is the inode flushing abort routine.  It is called
 * from xfs_iflush when the filesystem is shutting down to clean
 * up the inode state.
 * It is responsible for removing the inode item
 * from the AIL if it has not been re-logged, and unlocking the inode's
 * flush lock.
 */
void
xfs_iflush_abort(
	xfs_inode_t		*ip)
{
	xfs_inode_log_item_t	*iip;
	xfs_mount_t		*mp;
	SPLDECL(s);

	iip = ip->i_itemp;
	mp = ip->i_mount;
	if (iip) {
		if (iip->ili_item.li_flags & XFS_LI_IN_AIL) {
			AIL_LOCK(mp, s);
			if (iip->ili_item.li_flags & XFS_LI_IN_AIL) {
				/*
				 * xfs_trans_delete_ail() drops the AIL lock.
				 */
				xfs_trans_delete_ail(mp, (xfs_log_item_t *)iip,
					s);
			} else
				AIL_UNLOCK(mp, s);
		}
		iip->ili_logged = 0;
		/*
		 * Clear the ili_last_fields bits now that we know that the
		 * data corresponding to them is safely on disk.
		 */
		iip->ili_last_fields = 0;
		/*
		 * Clear the inode logging fields so no more flushes are
		 * attempted.
		 */
		iip->ili_format.ilf_fields = 0;
	}
	/*
	 * Release the inode's flush lock since we're done with it.
	 */
	xfs_ifunlock(ip);
}

void
xfs_istale_done(
	xfs_buf_t		*bp,
	xfs_inode_log_item_t	*iip)
{
	xfs_iflush_abort(iip->ili_inode);
}

/*
 * convert an xfs_inode_log_format struct from either 32 or 64 bit versions
 * (which can have different field alignments) to the native version
 */
int
xfs_inode_item_format_convert(
	xfs_log_iovec_t		*buf,
	xfs_inode_log_format_t	*in_f)
{
	if (buf->i_len == sizeof(xfs_inode_log_format_32_t)) {
		xfs_inode_log_format_32_t *in_f32;

		in_f32 = (xfs_inode_log_format_32_t *)buf->i_addr;
		in_f->ilf_type = in_f32->ilf_type;
		in_f->ilf_size = in_f32->ilf_size;
		in_f->ilf_fields = in_f32->ilf_fields;
		in_f->ilf_asize = in_f32->ilf_asize;
		in_f->ilf_dsize = in_f32->ilf_dsize;
		in_f->ilf_ino = in_f32->ilf_ino;
		/* copy biggest field of ilf_u */
		memcpy(in_f->ilf_u.ilfu_uuid.__u_bits,
		       in_f32->ilf_u.ilfu_uuid.__u_bits,
		       sizeof(uuid_t));
		in_f->ilf_blkno = in_f32->ilf_blkno;
		in_f->ilf_len = in_f32->ilf_len;
		in_f->ilf_boffset = in_f32->ilf_boffset;
		return 0;
	} else if (buf->i_len == sizeof(xfs_inode_log_format_64_t)){
		xfs_inode_log_format_64_t *in_f64;

		in_f64 = (xfs_inode_log_format_64_t *)buf->i_addr;
		in_f->ilf_type = in_f64->ilf_type;
		in_f->ilf_size = in_f64->ilf_size;
		in_f->ilf_fields = in_f64->ilf_fields;
		in_f->ilf_asize = in_f64->ilf_asize;
		in_f->ilf_dsize = in_f64->ilf_dsize;
		in_f->ilf_ino = in_f64->ilf_ino;
		/* copy biggest field of ilf_u */
		memcpy(in_f->ilf_u.ilfu_uuid.__u_bits,
		       in_f64->ilf_u.ilfu_uuid.__u_bits,
		       sizeof(uuid_t));
		in_f->ilf_blkno = in_f64->ilf_blkno;
		in_f->ilf_len = in_f64->ilf_len;
		in_f->ilf_boffset = in_f64->ilf_boffset;
		return 0;
	}
	return EFSCORRUPTED;
}
