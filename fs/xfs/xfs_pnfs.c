// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#include "xfs_platform.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_iomap.h"
#include "xfs_pnfs.h"
#include <linux/exportfs_block.h>

/*
 * Ensure that we do not have any outstanding pNFS layouts that can be used by
 * clients to directly read from or write to this inode.  This must be called
 * before every operation that can remove blocks from the extent map.
 * Additionally we call it during the write operation, where aren't concerned
 * about exposing unallocated blocks but just want to provide basic
 * synchronization between a local writer and pNFS clients.  mmap writes would
 * also benefit from this sort of synchronization, but due to the tricky locking
 * rules in the page fault path we don't bother.
 */
int
xfs_break_leased_layouts(
	struct inode		*inode,
	uint			*iolock,
	bool			*did_unlock)
{
	struct xfs_inode	*ip = XFS_I(inode);
	int			error;

	while ((error = break_layout(inode, false)) == -EWOULDBLOCK) {
		xfs_iunlock(ip, *iolock);
		*did_unlock = true;
		error = break_layout(inode, true);
		*iolock &= ~XFS_IOLOCK_SHARED;
		*iolock |= XFS_IOLOCK_EXCL;
		xfs_ilock(ip, *iolock);
	}

	return error;
}

static expfs_block_layouts_t
xfs_fs_layouts_supported(
	struct super_block	*sb)
{
	expfs_block_layouts_t	supported = EXPFS_BLOCK_IN_BAND_ID;

	if (exportfs_bdev_supports_out_of_band_id(sb->s_bdev))
		supported |= EXPFS_BLOCK_OUT_OF_BAND_ID;
	return supported;
}

/*
 * Get a unique ID including its location so that the client can identify
 * the exported device.
 */
static int
xfs_fs_get_uuid(
	struct super_block	*sb,
	u8			*buf,
	u32			*len,
	u64			*offset)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (*len < sizeof(uuid_t))
		return -EINVAL;

	memcpy(buf, &mp->m_sb.sb_uuid, sizeof(uuid_t));
	*len = sizeof(uuid_t);
	*offset = offsetof(struct xfs_dsb, sb_uuid);
	return 0;
}

/*
 * We cannot use file based VFS helpers such as file_modified() to update
 * inode state as we modify the data/metadata in the inode here. Hence we have
 * to open code the timestamp updates and SUID/SGID stripping. We also need
 * to set the inode prealloc flag to ensure that the extents we allocate are not
 * removed if the inode is reclaimed from memory before xfs_fs_block_commit()
 * is from the client to indicate that data has been written and the file size
 * can be extended.
 */
static int
xfs_fs_map_update_inode(
	struct xfs_inode	*ip)
{
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_writeid,
			0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	VFS_I(ip)->i_mode &= ~S_ISUID;
	if (VFS_I(ip)->i_mode & S_IXGRP)
		VFS_I(ip)->i_mode &= ~S_ISGID;
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	ip->i_diflags |= XFS_DIFLAG_PREALLOC;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return xfs_trans_commit(tp);
}

/*
 * Get a layout for the pNFS client.
 */
static int
xfs_fs_map_blocks(
	struct inode		*inode,
	loff_t			offset,
	u64			length,
	struct iomap		*iomap,
	bool			write,
	u32			*device_generation)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap;
	xfs_fileoff_t		offset_fsb, end_fsb;
	loff_t			limit;
	int			nimaps = 1;
	uint			lock_flags;
	int			error = 0;
	u64			seq;

	if (xfs_is_shutdown(mp))
		return -EIO;

	/*
	 * We can't export inodes residing on the realtime device.  The realtime
	 * device doesn't have a UUID to identify it, so the client has no way
	 * to find it.
	 */
	if (XFS_IS_REALTIME_INODE(ip))
		return -ENXIO;

	/*
	 * The pNFS block layout spec actually supports reflink like
	 * functionality, but the Linux pNFS server doesn't implement it yet.
	 */
	if (xfs_is_reflink_inode(ip))
		return -ENXIO;

	/*
	 * Lock out any other I/O before we flush and invalidate the pagecache,
	 * and then hand out a layout to the remote system.  This is very
	 * similar to direct I/O, except that the synchronization is much more
	 * complicated.  See the comment near xfs_break_leased_layouts
	 * for a detailed explanation.
	 */
	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	error = -EINVAL;
	limit = mp->m_super->s_maxbytes;
	if (!write)
		limit = max(limit, round_up(i_size_read(inode),
				     inode->i_sb->s_blocksize));
	if (offset > limit)
		goto out_unlock;
	if (offset > limit - length)
		length = limit - offset;

	error = filemap_write_and_wait(inode->i_mapping);
	if (error)
		goto out_unlock;
	error = invalidate_inode_pages2(inode->i_mapping);
	if (WARN_ON_ONCE(error))
		goto out_unlock;

	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + length);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	lock_flags = xfs_ilock_data_map_shared(ip);
	/* request mappings for the specified range only */
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb,
				&imap, &nimaps, 0);
	if (error) {
		xfs_iunlock(ip, lock_flags);
		goto out_unlock;
	}
	seq = xfs_iomap_inode_sequence(ip, 0);

	ASSERT(!nimaps || imap.br_startblock != DELAYSTARTBLOCK);

	if (write && (!nimaps || imap.br_startblock == HOLESTARTBLOCK)) {
		if (offset + length > XFS_ISIZE(ip))
			end_fsb = xfs_iomap_eof_align_last_fsb(ip, end_fsb);
		else if (nimaps && imap.br_startblock == HOLESTARTBLOCK)
			end_fsb = min(end_fsb, imap.br_startoff +
					       imap.br_blockcount);
		xfs_iunlock(ip, lock_flags);

		error = xfs_iomap_write_direct(ip, offset_fsb,
				end_fsb - offset_fsb, 0, &imap, &seq);
		if (error)
			goto out_unlock;

		/*
		 * Ensure the next transaction is committed synchronously so
		 * that the blocks allocated and handed out to the client are
		 * guaranteed to be present even after a server crash.
		 */
		error = xfs_fs_map_update_inode(ip);
		if (!error)
			error = xfs_log_force_inode(ip);
		if (error)
			goto out_unlock;

	} else {
		xfs_iunlock(ip, lock_flags);
	}
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);

	error = xfs_bmbt_to_iomap(ip, iomap, &imap, 0, 0, seq);
	*device_generation = mp->m_generation;
	return error;
out_unlock:
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * Ensure the size update falls into a valid allocated block.
 */
static int
xfs_pnfs_validate_isize(
	struct xfs_inode	*ip,
	xfs_off_t		isize)
{
	struct xfs_bmbt_irec	imap;
	int			nimaps = 1;
	int			error = 0;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_bmapi_read(ip, XFS_B_TO_FSBT(ip->i_mount, isize - 1), 1,
				&imap, &nimaps, 0);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	if (error)
		return error;

	if (imap.br_startblock == HOLESTARTBLOCK ||
	    imap.br_startblock == DELAYSTARTBLOCK ||
	    imap.br_state == XFS_EXT_UNWRITTEN)
		return -EIO;
	return 0;
}

/*
 * Make sure the blocks described by maps are stable on disk.  This includes
 * converting any unwritten extents, flushing the disk cache and updating the
 * time stamps.
 *
 * Note that we rely on the caller to always send us a timestamp update so that
 * we always commit a transaction here.  If that stops being true we will have
 * to manually flush the cache here similar to what the fsync code path does
 * for datasyncs on files that have no dirty metadata.
 */
static int
xfs_fs_commit_blocks(
	struct inode		*inode,
	struct iomap		*maps,
	int			nr_maps,
	loff_t			new_size)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct timespec64	now;
	bool			update_isize = false;
	int			error, i;
	loff_t			size;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	size = i_size_read(inode);
	if (new_size > size) {
		update_isize = true;
		size = new_size;
	}

	for (i = 0; i < nr_maps; i++) {
		u64 start, length, end;

		start = maps[i].offset;
		if (start > size)
			continue;

		end = start + maps[i].length;
		if (end > size)
			end = size;

		length = end - start;
		if (!length)
			continue;

		/*
		 * Make sure reads through the pagecache see the new data.
		 */
		error = invalidate_inode_pages2_range(inode->i_mapping,
					start >> PAGE_SHIFT,
					(end - 1) >> PAGE_SHIFT);
		WARN_ON_ONCE(error);

		error = xfs_iomap_write_unwritten(ip, start, length, false);
		if (error)
			goto out_drop_iolock;
	}

	if (update_isize) {
		error = xfs_pnfs_validate_isize(ip, size);
		if (error)
			goto out_drop_iolock;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		goto out_drop_iolock;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	now = inode_set_ctime_current(inode);
	inode_set_atime_to_ts(inode, now);
	inode_set_mtime_to_ts(inode, now);

	if (update_isize) {
		i_size_write(inode, new_size);
		ip->i_disk_size = new_size;
	}

	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);

out_drop_iolock:
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

const struct exportfs_block_ops xfs_export_block_ops = {
	.layouts_supported	= xfs_fs_layouts_supported,
	.get_uuid		= xfs_fs_get_uuid,
	.map_blocks		= xfs_fs_map_blocks,
	.commit_blocks		= xfs_fs_commit_blocks,
};
