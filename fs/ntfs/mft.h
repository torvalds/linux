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

int ntfs_mft_bioset_init(void);
void ntfs_mft_bioset_exit(void);
int write_mft_record_nolock(struct ntfs_inode *ni, struct mft_record *m, int sync);

/*
 * write_mft_record - write out a mapped (extent) mft record
 * @ni:		ntfs inode describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * This is just a wrapper for write_mft_record_nolock() (see mft.c), which
 * locks the folio while preparing the write.  write_mft_record_nolock() waits
 * for prior folio writeback before modifying the folio and keeps PG_writeback
 * set until the submitted I/O completes.  Together these serialize dirty
 * inode writes, page cache writeback, and neighbouring mft record writes in
 * the same folio.
 *
 * Locking the page also serializes us against ->read_folio() if the page is not
 * uptodate.
 *
 * On success, clean the mft record and return 0.  On allocation failure,
 * redirty the record for retry.  Asynchronous callers return 0 after
 * redirtying, while synchronous callers receive -ENOMEM.  On other errors,
 * return -errno and mark the volume with errors.
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
		struct mft_record **ni_mrec, const s64 mft_data_vcn);
int ntfs_mft_record_free(struct ntfs_volume *vol, struct ntfs_inode *ni);
int ntfs_mft_records_write(const struct ntfs_volume *vol, const u64 mref,
		const s64 count, struct mft_record *b);
int ntfs_mft_record_check(const struct ntfs_volume *vol, struct mft_record *m,
			  u64 mft_no);
int ntfs_mft_writepages(struct address_space *mapping,
		struct writeback_control *wbc);
void ntfs_mft_mark_dirty(struct folio *folio);

#endif /* _LINUX_NTFS_MFT_H */
