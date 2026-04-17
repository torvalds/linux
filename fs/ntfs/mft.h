/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for mft record handling in NTFS Linux kernel driver.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_MFT_H
#define _LINUX_NTFS_MFT_H

#include <linux/highmem.h>
#include <linux/pagemap.h>

#include "inode.h"

struct mft_record *map_mft_record(struct ntfs_inode *ni);
void unmap_mft_record(struct ntfs_inode *ni);
struct mft_record *map_extent_mft_record(struct ntfs_inode *base_ni, u64 mref,
		struct ntfs_inode **ntfs_ino);

static inline void unmap_extent_mft_record(struct ntfs_inode *ni)
{
	unmap_mft_record(ni);
}

void __mark_mft_record_dirty(struct ntfs_inode *ni);

/*
 * mark_mft_record_dirty - set the mft record and the page containing it dirty
 * @ni:		ntfs inode describing the mapped mft record
 *
 * Set the mapped (extent) mft record of the (base or extent) ntfs inode @ni,
 * as well as the page containing the mft record, dirty.  Also, mark the base
 * vfs inode dirty.  This ensures that any changes to the mft record are
 * written out to disk.
 *
 * NOTE:  Do not do anything if the mft record is already marked dirty.
 */
static inline void mark_mft_record_dirty(struct ntfs_inode *ni)
{
	if (!NInoTestSetDirty(ni))
		__mark_mft_record_dirty(ni);
}

int ntfs_sync_mft_mirror(struct ntfs_volume *vol, const u64 mft_no,
		struct mft_record *m);
int write_mft_record_nolock(struct ntfs_inode *ni, struct mft_record *m, int sync);

/*
 * write_mft_record - write out a mapped (extent) mft record
 * @ni:		ntfs inode describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * This is just a wrapper for write_mft_record_nolock() (see mft.c), which
 * locks the page for the duration of the write.  This ensures that there are
 * no race conditions between writing the mft record via the dirty inode code
 * paths and via the page cache write back code paths or between writing
 * neighbouring mft records residing in the same page.
 *
 * Locking the page also serializes us against ->read_folio() if the page is not
 * uptodate.
 *
 * On success, clean the mft record and return 0.  On error, leave the mft
 * record dirty and return -errno.
 */
static inline int write_mft_record(struct ntfs_inode *ni, struct mft_record *m, int sync)
{
	struct folio *folio = ni->folio;
	int err;

	folio_lock(folio);
	err = write_mft_record_nolock(ni, m, sync);
	folio_unlock(folio);

	return err;
}

int ntfs_mft_record_alloc(struct ntfs_volume *vol, const int mode,
		struct ntfs_inode **ni, struct ntfs_inode *base_ni,
		struct mft_record **ni_mrec);
int ntfs_mft_record_free(struct ntfs_volume *vol, struct ntfs_inode *ni);
int ntfs_mft_records_write(const struct ntfs_volume *vol, const u64 mref,
		const s64 count, struct mft_record *b);
int ntfs_mft_record_check(const struct ntfs_volume *vol, struct mft_record *m,
			  u64 mft_no);
int ntfs_mft_writepages(struct address_space *mapping,
		struct writeback_control *wbc);
void ntfs_mft_mark_dirty(struct folio *folio);

#endif /* _LINUX_NTFS_MFT_H */
