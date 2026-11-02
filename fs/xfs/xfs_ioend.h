/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XFS_IOEND_H
#define __XFS_IOEND_H

/*
 * Fast and loose check if this write could update the on-disk inode size.
 */
static inline bool xfs_ioend_is_append(struct iomap_ioend *ioend)
{
	return ioend->io_offset + ioend->io_size >
		XFS_I(ioend->io_inode)->i_disk_size;
}

void xfs_end_bio(struct bio *bio);
void xfs_ioend_submit_read(struct inode *inode, struct bio *bio,
		loff_t file_offset, u16 ioend_flags);

#endif /* __XFS_IOEND_H */
