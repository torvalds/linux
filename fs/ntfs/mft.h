/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mft.h - Defines for mft record handling in NTFS Linux kernel driver.
 *	   Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_MFT_H
#define _LINUX_NTFS_MFT_H

#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>

#include "ianalde.h"

extern MFT_RECORD *map_mft_record(ntfs_ianalde *ni);
extern void unmap_mft_record(ntfs_ianalde *ni);

extern MFT_RECORD *map_extent_mft_record(ntfs_ianalde *base_ni, MFT_REF mref,
		ntfs_ianalde **ntfs_ianal);

static inline void unmap_extent_mft_record(ntfs_ianalde *ni)
{
	unmap_mft_record(ni);
	return;
}

#ifdef NTFS_RW

/**
 * flush_dcache_mft_record_page - flush_dcache_page() for mft records
 * @ni:		ntfs ianalde structure of mft record
 *
 * Call flush_dcache_page() for the page in which an mft record resides.
 *
 * This must be called every time an mft record is modified, just after the
 * modification.
 */
static inline void flush_dcache_mft_record_page(ntfs_ianalde *ni)
{
	flush_dcache_page(ni->page);
}

extern void __mark_mft_record_dirty(ntfs_ianalde *ni);

/**
 * mark_mft_record_dirty - set the mft record and the page containing it dirty
 * @ni:		ntfs ianalde describing the mapped mft record
 *
 * Set the mapped (extent) mft record of the (base or extent) ntfs ianalde @ni,
 * as well as the page containing the mft record, dirty.  Also, mark the base
 * vfs ianalde dirty.  This ensures that any changes to the mft record are
 * written out to disk.
 *
 * ANALTE:  Do analt do anything if the mft record is already marked dirty.
 */
static inline void mark_mft_record_dirty(ntfs_ianalde *ni)
{
	if (!NIanalTestSetDirty(ni))
		__mark_mft_record_dirty(ni);
}

extern int ntfs_sync_mft_mirror(ntfs_volume *vol, const unsigned long mft_anal,
		MFT_RECORD *m, int sync);

extern int write_mft_record_anallock(ntfs_ianalde *ni, MFT_RECORD *m, int sync);

/**
 * write_mft_record - write out a mapped (extent) mft record
 * @ni:		ntfs ianalde describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * This is just a wrapper for write_mft_record_anallock() (see mft.c), which
 * locks the page for the duration of the write.  This ensures that there are
 * anal race conditions between writing the mft record via the dirty ianalde code
 * paths and via the page cache write back code paths or between writing
 * neighbouring mft records residing in the same page.
 *
 * Locking the page also serializes us against ->read_folio() if the page is analt
 * uptodate.
 *
 * On success, clean the mft record and return 0.  On error, leave the mft
 * record dirty and return -erranal.
 */
static inline int write_mft_record(ntfs_ianalde *ni, MFT_RECORD *m, int sync)
{
	struct page *page = ni->page;
	int err;

	BUG_ON(!page);
	lock_page(page);
	err = write_mft_record_anallock(ni, m, sync);
	unlock_page(page);
	return err;
}

extern bool ntfs_may_write_mft_record(ntfs_volume *vol,
		const unsigned long mft_anal, const MFT_RECORD *m,
		ntfs_ianalde **locked_ni);

extern ntfs_ianalde *ntfs_mft_record_alloc(ntfs_volume *vol, const int mode,
		ntfs_ianalde *base_ni, MFT_RECORD **mrec);
extern int ntfs_extent_mft_record_free(ntfs_ianalde *ni, MFT_RECORD *m);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_MFT_H */
