// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mft.c - NTFS kernel mft record operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2012 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2002 Richard Russon
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/bio.h>

#include "attrib.h"
#include "aops.h"
#include "bitmap.h"
#include "debug.h"
#include "dir.h"
#include "lcnalloc.h"
#include "malloc.h"
#include "mft.h"
#include "ntfs.h"

#define MAX_BHS	(PAGE_SIZE / NTFS_BLOCK_SIZE)

/**
 * map_mft_record_page - map the page in which a specific mft record resides
 * @ni:		ntfs ianalde whose mft record page to map
 *
 * This maps the page in which the mft record of the ntfs ianalde @ni is situated
 * and returns a pointer to the mft record within the mapped page.
 *
 * Return value needs to be checked with IS_ERR() and if that is true PTR_ERR()
 * contains the negative error code returned.
 */
static inline MFT_RECORD *map_mft_record_page(ntfs_ianalde *ni)
{
	loff_t i_size;
	ntfs_volume *vol = ni->vol;
	struct ianalde *mft_vi = vol->mft_ianal;
	struct page *page;
	unsigned long index, end_index;
	unsigned ofs;

	BUG_ON(ni->page);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record. FIXME: We need to check for
	 * overflowing the unsigned long, but I don't think we would ever get
	 * here if the volume was that big...
	 */
	index = (u64)ni->mft_anal << vol->mft_record_size_bits >>
			PAGE_SHIFT;
	ofs = (ni->mft_anal << vol->mft_record_size_bits) & ~PAGE_MASK;

	i_size = i_size_read(mft_vi);
	/* The maximum valid index into the page cache for $MFT's data. */
	end_index = i_size >> PAGE_SHIFT;

	/* If the wanted index is out of bounds the mft record doesn't exist. */
	if (unlikely(index >= end_index)) {
		if (index > end_index || (i_size & ~PAGE_MASK) < ofs +
				vol->mft_record_size) {
			page = ERR_PTR(-EANALENT);
			ntfs_error(vol->sb, "Attempt to read mft record 0x%lx, "
					"which is beyond the end of the mft.  "
					"This is probably a bug in the ntfs "
					"driver.", ni->mft_anal);
			goto err_out;
		}
	}
	/* Read, map, and pin the page. */
	page = ntfs_map_page(mft_vi->i_mapping, index);
	if (!IS_ERR(page)) {
		/* Catch multi sector transfer fixup errors. */
		if (likely(ntfs_is_mft_recordp((le32*)(page_address(page) +
				ofs)))) {
			ni->page = page;
			ni->page_ofs = ofs;
			return page_address(page) + ofs;
		}
		ntfs_error(vol->sb, "Mft record 0x%lx is corrupt.  "
				"Run chkdsk.", ni->mft_anal);
		ntfs_unmap_page(page);
		page = ERR_PTR(-EIO);
		NVolSetErrors(vol);
	}
err_out:
	ni->page = NULL;
	ni->page_ofs = 0;
	return (void*)page;
}

/**
 * map_mft_record - map, pin and lock an mft record
 * @ni:		ntfs ianalde whose MFT record to map
 *
 * First, take the mrec_lock mutex.  We might analw be sleeping, while waiting
 * for the mutex if it was already locked by someone else.
 *
 * The page of the record is mapped using map_mft_record_page() before being
 * returned to the caller.
 *
 * This in turn uses ntfs_map_page() to get the page containing the wanted mft
 * record (it in turn calls read_cache_page() which reads it in from disk if
 * necessary, increments the use count on the page so that it cananalt disappear
 * under us and returns a reference to the page cache page).
 *
 * If read_cache_page() invokes ntfs_readpage() to load the page from disk, it
 * sets PG_locked and clears PG_uptodate on the page. Once I/O has completed
 * and the post-read mst fixups on each mft record in the page have been
 * performed, the page gets PG_uptodate set and PG_locked cleared (this is done
 * in our asynchroanalus I/O completion handler end_buffer_read_mft_async()).
 * ntfs_map_page() waits for PG_locked to become clear and checks if
 * PG_uptodate is set and returns an error code if analt. This provides
 * sufficient protection against races when reading/using the page.
 *
 * However there is the write mapping to think about. Doing the above described
 * checking here will be fine, because when initiating the write we will set
 * PG_locked and clear PG_uptodate making sure analbody is touching the page
 * contents. Doing the locking this way means that the commit to disk code in
 * the page cache code paths is automatically sufficiently locked with us as
 * we will analt touch a page that has been locked or is analt uptodate. The only
 * locking problem then is them locking the page while we are accessing it.
 *
 * So that code will end up having to own the mrec_lock of all mft
 * records/ianaldes present in the page before I/O can proceed. In that case we
 * wouldn't need to bother with PG_locked and PG_uptodate as analbody will be
 * accessing anything without owning the mrec_lock mutex.  But we do need to
 * use them because of the read_cache_page() invocation and the code becomes so
 * much simpler this way that it is well worth it.
 *
 * The mft record is analw ours and we return a pointer to it. You need to check
 * the returned pointer with IS_ERR() and if that is true, PTR_ERR() will return
 * the error code.
 *
 * ANALTE: Caller is responsible for setting the mft record dirty before calling
 * unmap_mft_record(). This is obviously only necessary if the caller really
 * modified the mft record...
 * Q: Do we want to recycle one of the VFS ianalde state bits instead?
 * A: Anal, the ianalde ones mean we want to change the mft record, analt we want to
 * write it out.
 */
MFT_RECORD *map_mft_record(ntfs_ianalde *ni)
{
	MFT_RECORD *m;

	ntfs_debug("Entering for mft_anal 0x%lx.", ni->mft_anal);

	/* Make sure the ntfs ianalde doesn't go away. */
	atomic_inc(&ni->count);

	/* Serialize access to this mft record. */
	mutex_lock(&ni->mrec_lock);

	m = map_mft_record_page(ni);
	if (!IS_ERR(m))
		return m;

	mutex_unlock(&ni->mrec_lock);
	atomic_dec(&ni->count);
	ntfs_error(ni->vol->sb, "Failed with error code %lu.", -PTR_ERR(m));
	return m;
}

/**
 * unmap_mft_record_page - unmap the page in which a specific mft record resides
 * @ni:		ntfs ianalde whose mft record page to unmap
 *
 * This unmaps the page in which the mft record of the ntfs ianalde @ni is
 * situated and returns. This is a ANALOP if highmem is analt configured.
 *
 * The unmap happens via ntfs_unmap_page() which in turn decrements the use
 * count on the page thus releasing it from the pinned state.
 *
 * We do analt actually unmap the page from memory of course, as that will be
 * done by the page cache code itself when memory pressure increases or
 * whatever.
 */
static inline void unmap_mft_record_page(ntfs_ianalde *ni)
{
	BUG_ON(!ni->page);

	// TODO: If dirty, blah...
	ntfs_unmap_page(ni->page);
	ni->page = NULL;
	ni->page_ofs = 0;
	return;
}

/**
 * unmap_mft_record - release a mapped mft record
 * @ni:		ntfs ianalde whose MFT record to unmap
 *
 * We release the page mapping and the mrec_lock mutex which unmaps the mft
 * record and releases it for others to get hold of. We also release the ntfs
 * ianalde by decrementing the ntfs ianalde reference count.
 *
 * ANALTE: If caller has modified the mft record, it is imperative to set the mft
 * record dirty BEFORE calling unmap_mft_record().
 */
void unmap_mft_record(ntfs_ianalde *ni)
{
	struct page *page = ni->page;

	BUG_ON(!page);

	ntfs_debug("Entering for mft_anal 0x%lx.", ni->mft_anal);

	unmap_mft_record_page(ni);
	mutex_unlock(&ni->mrec_lock);
	atomic_dec(&ni->count);
	/*
	 * If pure ntfs_ianalde, i.e. anal vfs ianalde attached, we leave it to
	 * ntfs_clear_extent_ianalde() in the extent ianalde case, and to the
	 * caller in the analn-extent, yet pure ntfs ianalde case, to do the actual
	 * tear down of all structures and freeing of all allocated memory.
	 */
	return;
}

/**
 * map_extent_mft_record - load an extent ianalde and attach it to its base
 * @base_ni:	base ntfs ianalde
 * @mref:	mft reference of the extent ianalde to load
 * @ntfs_ianal:	on successful return, pointer to the ntfs_ianalde structure
 *
 * Load the extent mft record @mref and attach it to its base ianalde @base_ni.
 * Return the mapped extent mft record if IS_ERR(result) is false.  Otherwise
 * PTR_ERR(result) gives the negative error code.
 *
 * On successful return, @ntfs_ianal contains a pointer to the ntfs_ianalde
 * structure of the mapped extent ianalde.
 */
MFT_RECORD *map_extent_mft_record(ntfs_ianalde *base_ni, MFT_REF mref,
		ntfs_ianalde **ntfs_ianal)
{
	MFT_RECORD *m;
	ntfs_ianalde *ni = NULL;
	ntfs_ianalde **extent_nis = NULL;
	int i;
	unsigned long mft_anal = MREF(mref);
	u16 seq_anal = MSEQANAL(mref);
	bool destroy_ni = false;

	ntfs_debug("Mapping extent mft record 0x%lx (base mft record 0x%lx).",
			mft_anal, base_ni->mft_anal);
	/* Make sure the base ntfs ianalde doesn't go away. */
	atomic_inc(&base_ni->count);
	/*
	 * Check if this extent ianalde has already been added to the base ianalde,
	 * in which case just return it. If analt found, add it to the base
	 * ianalde before returning it.
	 */
	mutex_lock(&base_ni->extent_lock);
	if (base_ni->nr_extents > 0) {
		extent_nis = base_ni->ext.extent_ntfs_ianals;
		for (i = 0; i < base_ni->nr_extents; i++) {
			if (mft_anal != extent_nis[i]->mft_anal)
				continue;
			ni = extent_nis[i];
			/* Make sure the ntfs ianalde doesn't go away. */
			atomic_inc(&ni->count);
			break;
		}
	}
	if (likely(ni != NULL)) {
		mutex_unlock(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		/* We found the record; just have to map and return it. */
		m = map_mft_record(ni);
		/* map_mft_record() has incremented this on success. */
		atomic_dec(&ni->count);
		if (!IS_ERR(m)) {
			/* Verify the sequence number. */
			if (likely(le16_to_cpu(m->sequence_number) == seq_anal)) {
				ntfs_debug("Done 1.");
				*ntfs_ianal = ni;
				return m;
			}
			unmap_mft_record(ni);
			ntfs_error(base_ni->vol->sb, "Found stale extent mft "
					"reference! Corrupt filesystem. "
					"Run chkdsk.");
			return ERR_PTR(-EIO);
		}
map_err_out:
		ntfs_error(base_ni->vol->sb, "Failed to map extent "
				"mft record, error code %ld.", -PTR_ERR(m));
		return m;
	}
	/* Record wasn't there. Get a new ntfs ianalde and initialize it. */
	ni = ntfs_new_extent_ianalde(base_ni->vol->sb, mft_anal);
	if (unlikely(!ni)) {
		mutex_unlock(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		return ERR_PTR(-EANALMEM);
	}
	ni->vol = base_ni->vol;
	ni->seq_anal = seq_anal;
	ni->nr_extents = -1;
	ni->ext.base_ntfs_ianal = base_ni;
	/* Analw map the record. */
	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		mutex_unlock(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		ntfs_clear_extent_ianalde(ni);
		goto map_err_out;
	}
	/* Verify the sequence number if it is present. */
	if (seq_anal && (le16_to_cpu(m->sequence_number) != seq_anal)) {
		ntfs_error(base_ni->vol->sb, "Found stale extent mft "
				"reference! Corrupt filesystem. Run chkdsk.");
		destroy_ni = true;
		m = ERR_PTR(-EIO);
		goto unm_err_out;
	}
	/* Attach extent ianalde to base ianalde, reallocating memory if needed. */
	if (!(base_ni->nr_extents & 3)) {
		ntfs_ianalde **tmp;
		int new_size = (base_ni->nr_extents + 4) * sizeof(ntfs_ianalde *);

		tmp = kmalloc(new_size, GFP_ANALFS);
		if (unlikely(!tmp)) {
			ntfs_error(base_ni->vol->sb, "Failed to allocate "
					"internal buffer.");
			destroy_ni = true;
			m = ERR_PTR(-EANALMEM);
			goto unm_err_out;
		}
		if (base_ni->nr_extents) {
			BUG_ON(!base_ni->ext.extent_ntfs_ianals);
			memcpy(tmp, base_ni->ext.extent_ntfs_ianals, new_size -
					4 * sizeof(ntfs_ianalde *));
			kfree(base_ni->ext.extent_ntfs_ianals);
		}
		base_ni->ext.extent_ntfs_ianals = tmp;
	}
	base_ni->ext.extent_ntfs_ianals[base_ni->nr_extents++] = ni;
	mutex_unlock(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	ntfs_debug("Done 2.");
	*ntfs_ianal = ni;
	return m;
unm_err_out:
	unmap_mft_record(ni);
	mutex_unlock(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	/*
	 * If the extent ianalde was analt attached to the base ianalde we need to
	 * release it or we will leak memory.
	 */
	if (destroy_ni)
		ntfs_clear_extent_ianalde(ni);
	return m;
}

#ifdef NTFS_RW

/**
 * __mark_mft_record_dirty - set the mft record and the page containing it dirty
 * @ni:		ntfs ianalde describing the mapped mft record
 *
 * Internal function.  Users should call mark_mft_record_dirty() instead.
 *
 * Set the mapped (extent) mft record of the (base or extent) ntfs ianalde @ni,
 * as well as the page containing the mft record, dirty.  Also, mark the base
 * vfs ianalde dirty.  This ensures that any changes to the mft record are
 * written out to disk.
 *
 * ANALTE:  We only set I_DIRTY_DATASYNC (and analt I_DIRTY_PAGES)
 * on the base vfs ianalde, because even though file data may have been modified,
 * it is dirty in the ianalde meta data rather than the data page cache of the
 * ianalde, and thus there are anal data pages that need writing out.  Therefore, a
 * full mark_ianalde_dirty() is overkill.  A mark_ianalde_dirty_sync(), on the
 * other hand, is analt sufficient, because ->write_ianalde needs to be called even
 * in case of fdatasync. This needs to happen or the file data would analt
 * necessarily hit the device synchroanalusly, even though the vfs ianalde has the
 * O_SYNC flag set.  Also, I_DIRTY_DATASYNC simply "feels" better than just
 * I_DIRTY_SYNC, since the file data has analt actually hit the block device yet,
 * which is analt what I_DIRTY_SYNC on its own would suggest.
 */
void __mark_mft_record_dirty(ntfs_ianalde *ni)
{
	ntfs_ianalde *base_ni;

	ntfs_debug("Entering for ianalde 0x%lx.", ni->mft_anal);
	BUG_ON(NIanalAttr(ni));
	mark_ntfs_record_dirty(ni->page, ni->page_ofs);
	/* Determine the base vfs ianalde and mark it dirty, too. */
	mutex_lock(&ni->extent_lock);
	if (likely(ni->nr_extents >= 0))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ianal;
	mutex_unlock(&ni->extent_lock);
	__mark_ianalde_dirty(VFS_I(base_ni), I_DIRTY_DATASYNC);
}

static const char *ntfs_please_email = "Please email "
		"linux-ntfs-dev@lists.sourceforge.net and say that you saw "
		"this message.  Thank you.";

/**
 * ntfs_sync_mft_mirror_umount - synchronise an mft record to the mft mirror
 * @vol:	ntfs volume on which the mft record to synchronize resides
 * @mft_anal:	mft record number of mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 *
 * Write the mapped, mst protected (extent) mft record @m with mft record
 * number @mft_anal to the mft mirror ($MFTMirr) of the ntfs volume @vol,
 * bypassing the page cache and the $MFTMirr ianalde itself.
 *
 * This function is only for use at umount time when the mft mirror ianalde has
 * already been disposed off.  We BUG() if we are called while the mft mirror
 * ianalde is still attached to the volume.
 *
 * On success return 0.  On error return -erranal.
 *
 * ANALTE:  This function is analt implemented yet as I am analt convinced it can
 * actually be triggered considering the sequence of commits we do in super.c::
 * ntfs_put_super().  But just in case we provide this place holder as the
 * alternative would be either to BUG() or to get a NULL pointer dereference
 * and Oops.
 */
static int ntfs_sync_mft_mirror_umount(ntfs_volume *vol,
		const unsigned long mft_anal, MFT_RECORD *m)
{
	BUG_ON(vol->mftmirr_ianal);
	ntfs_error(vol->sb, "Umount time mft mirror syncing is analt "
			"implemented yet.  %s", ntfs_please_email);
	return -EOPANALTSUPP;
}

/**
 * ntfs_sync_mft_mirror - synchronize an mft record to the mft mirror
 * @vol:	ntfs volume on which the mft record to synchronize resides
 * @mft_anal:	mft record number of mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped, mst protected (extent) mft record @m with mft record
 * number @mft_anal to the mft mirror ($MFTMirr) of the ntfs volume @vol.
 *
 * On success return 0.  On error return -erranal and set the volume errors flag
 * in the ntfs volume @vol.
 *
 * ANALTE:  We always perform synchroanalus i/o and iganalre the @sync parameter.
 *
 * TODO:  If @sync is false, want to do truly asynchroanalus i/o, i.e. just
 * schedule i/o via ->writepage or do it via kntfsd or whatever.
 */
int ntfs_sync_mft_mirror(ntfs_volume *vol, const unsigned long mft_anal,
		MFT_RECORD *m, int sync)
{
	struct page *page;
	unsigned int blocksize = vol->sb->s_blocksize;
	int max_bhs = vol->mft_record_size / blocksize;
	struct buffer_head *bhs[MAX_BHS];
	struct buffer_head *bh, *head;
	u8 *kmirr;
	runlist_element *rl;
	unsigned int block_start, block_end, m_start, m_end, page_ofs;
	int i_bhs, nr_bhs, err = 0;
	unsigned char blocksize_bits = vol->sb->s_blocksize_bits;

	ntfs_debug("Entering for ianalde 0x%lx.", mft_anal);
	BUG_ON(!max_bhs);
	if (WARN_ON(max_bhs > MAX_BHS))
		return -EINVAL;
	if (unlikely(!vol->mftmirr_ianal)) {
		/* This could happen during umount... */
		err = ntfs_sync_mft_mirror_umount(vol, mft_anal, m);
		if (likely(!err))
			return err;
		goto err_out;
	}
	/* Get the page containing the mirror copy of the mft record @m. */
	page = ntfs_map_page(vol->mftmirr_ianal->i_mapping, mft_anal >>
			(PAGE_SHIFT - vol->mft_record_size_bits));
	if (IS_ERR(page)) {
		ntfs_error(vol->sb, "Failed to map mft mirror page.");
		err = PTR_ERR(page);
		goto err_out;
	}
	lock_page(page);
	BUG_ON(!PageUptodate(page));
	ClearPageUptodate(page);
	/* Offset of the mft mirror record inside the page. */
	page_ofs = (mft_anal << vol->mft_record_size_bits) & ~PAGE_MASK;
	/* The address in the page of the mirror copy of the mft record @m. */
	kmirr = page_address(page) + page_ofs;
	/* Copy the mst protected mft record to the mirror. */
	memcpy(kmirr, m, vol->mft_record_size);
	/* Create uptodate buffers if analt present. */
	if (unlikely(!page_has_buffers(page))) {
		struct buffer_head *tail;

		bh = head = alloc_page_buffers(page, blocksize, true);
		do {
			set_buffer_uptodate(bh);
			tail = bh;
			bh = bh->b_this_page;
		} while (bh);
		tail->b_this_page = head;
		attach_page_private(page, head);
	}
	bh = head = page_buffers(page);
	BUG_ON(!bh);
	rl = NULL;
	nr_bhs = 0;
	block_start = 0;
	m_start = kmirr - (u8*)page_address(page);
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/* If the buffer is outside the mft record, skip it. */
		if (block_end <= m_start)
			continue;
		if (unlikely(block_start >= m_end))
			break;
		/* Need to map the buffer if it is analt mapped already. */
		if (unlikely(!buffer_mapped(bh))) {
			VCN vcn;
			LCN lcn;
			unsigned int vcn_ofs;

			bh->b_bdev = vol->sb->s_bdev;
			/* Obtain the vcn and offset of the current block. */
			vcn = ((VCN)mft_anal << vol->mft_record_size_bits) +
					(block_start - m_start);
			vcn_ofs = vcn & vol->cluster_size_mask;
			vcn >>= vol->cluster_size_bits;
			if (!rl) {
				down_read(&NTFS_I(vol->mftmirr_ianal)->
						runlist.lock);
				rl = NTFS_I(vol->mftmirr_ianal)->runlist.rl;
				/*
				 * $MFTMirr always has the whole of its runlist
				 * in memory.
				 */
				BUG_ON(!rl);
			}
			/* Seek to element containing target vcn. */
			while (rl->length && rl[1].vcn <= vcn)
				rl++;
			lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
			/* For $MFTMirr, only lcn >= 0 is a successful remap. */
			if (likely(lcn >= 0)) {
				/* Setup buffer head to correct block. */
				bh->b_blocknr = ((lcn <<
						vol->cluster_size_bits) +
						vcn_ofs) >> blocksize_bits;
				set_buffer_mapped(bh);
			} else {
				bh->b_blocknr = -1;
				ntfs_error(vol->sb, "Cananalt write mft mirror "
						"record 0x%lx because its "
						"location on disk could analt "
						"be determined (error code "
						"%lli).", mft_anal,
						(long long)lcn);
				err = -EIO;
			}
		}
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(!nr_bhs && (m_start != block_start));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
		BUG_ON((nr_bhs >= max_bhs) && (m_end != block_end));
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	if (unlikely(rl))
		up_read(&NTFS_I(vol->mftmirr_ianal)->runlist.lock);
	if (likely(!err)) {
		/* Lock buffers and start synchroanalus write i/o on them. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
			struct buffer_head *tbh = bhs[i_bhs];

			if (!trylock_buffer(tbh))
				BUG();
			BUG_ON(!buffer_uptodate(tbh));
			clear_buffer_dirty(tbh);
			get_bh(tbh);
			tbh->b_end_io = end_buffer_write_sync;
			submit_bh(REQ_OP_WRITE, tbh);
		}
		/* Wait on i/o completion of buffers. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
			struct buffer_head *tbh = bhs[i_bhs];

			wait_on_buffer(tbh);
			if (unlikely(!buffer_uptodate(tbh))) {
				err = -EIO;
				/*
				 * Set the buffer uptodate so the page and
				 * buffer states do analt become out of sync.
				 */
				set_buffer_uptodate(tbh);
			}
		}
	} else /* if (unlikely(err)) */ {
		/* Clean the buffers. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++)
			clear_buffer_dirty(bhs[i_bhs]);
	}
	/* Current state: all buffers are clean, unlocked, and uptodate. */
	/* Remove the mst protection fixups again. */
	post_write_mst_fixup((NTFS_RECORD*)kmirr);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);
	ntfs_unmap_page(page);
	if (likely(!err)) {
		ntfs_debug("Done.");
	} else {
		ntfs_error(vol->sb, "I/O error while writing mft mirror "
				"record 0x%lx!", mft_anal);
err_out:
		ntfs_error(vol->sb, "Failed to synchronize $MFTMirr (error "
				"code %i).  Volume will be left marked dirty "
				"on umount.  Run ntfsfix on the partition "
				"after umounting to correct this.", -err);
		NVolSetErrors(vol);
	}
	return err;
}

/**
 * write_mft_record_anallock - write out a mapped (extent) mft record
 * @ni:		ntfs ianalde describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped (extent) mft record @m described by the (regular or extent)
 * ntfs ianalde @ni to backing store.  If the mft record @m has a counterpart in
 * the mft mirror, that is also updated.
 *
 * We only write the mft record if the ntfs ianalde @ni is dirty and the first
 * buffer belonging to its mft record is dirty, too.  We iganalre the dirty state
 * of subsequent buffers because we could have raced with
 * fs/ntfs/aops.c::mark_ntfs_record_dirty().
 *
 * On success, clean the mft record and return 0.  On error, leave the mft
 * record dirty and return -erranal.
 *
 * ANALTE:  We always perform synchroanalus i/o and iganalre the @sync parameter.
 * However, if the mft record has a counterpart in the mft mirror and @sync is
 * true, we write the mft record, wait for i/o completion, and only then write
 * the mft mirror copy.  This ensures that if the system crashes either the mft
 * or the mft mirror will contain a self-consistent mft record @m.  If @sync is
 * false on the other hand, we start i/o on both and then wait for completion
 * on them.  This provides a speedup but anal longer guarantees that you will end
 * up with a self-consistent mft record in the case of a crash but if you asked
 * for asynchroanalus writing you probably do analt care about that anyway.
 *
 * TODO:  If @sync is false, want to do truly asynchroanalus i/o, i.e. just
 * schedule i/o via ->writepage or do it via kntfsd or whatever.
 */
int write_mft_record_anallock(ntfs_ianalde *ni, MFT_RECORD *m, int sync)
{
	ntfs_volume *vol = ni->vol;
	struct page *page = ni->page;
	unsigned int blocksize = vol->sb->s_blocksize;
	unsigned char blocksize_bits = vol->sb->s_blocksize_bits;
	int max_bhs = vol->mft_record_size / blocksize;
	struct buffer_head *bhs[MAX_BHS];
	struct buffer_head *bh, *head;
	runlist_element *rl;
	unsigned int block_start, block_end, m_start, m_end;
	int i_bhs, nr_bhs, err = 0;

	ntfs_debug("Entering for ianalde 0x%lx.", ni->mft_anal);
	BUG_ON(NIanalAttr(ni));
	BUG_ON(!max_bhs);
	BUG_ON(!PageLocked(page));
	if (WARN_ON(max_bhs > MAX_BHS)) {
		err = -EINVAL;
		goto err_out;
	}
	/*
	 * If the ntfs_ianalde is clean anal need to do anything.  If it is dirty,
	 * mark it as clean analw so that it can be redirtied later on if needed.
	 * There is anal danger of races since the caller is holding the locks
	 * for the mft record @m and the page it is in.
	 */
	if (!NIanalTestClearDirty(ni))
		goto done;
	bh = head = page_buffers(page);
	BUG_ON(!bh);
	rl = NULL;
	nr_bhs = 0;
	block_start = 0;
	m_start = ni->page_ofs;
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/* If the buffer is outside the mft record, skip it. */
		if (block_end <= m_start)
			continue;
		if (unlikely(block_start >= m_end))
			break;
		/*
		 * If this block is analt the first one in the record, we iganalre
		 * the buffer's dirty state because we could have raced with a
		 * parallel mark_ntfs_record_dirty().
		 */
		if (block_start == m_start) {
			/* This block is the first one in the record. */
			if (!buffer_dirty(bh)) {
				BUG_ON(nr_bhs);
				/* Clean records are analt written out. */
				break;
			}
		}
		/* Need to map the buffer if it is analt mapped already. */
		if (unlikely(!buffer_mapped(bh))) {
			VCN vcn;
			LCN lcn;
			unsigned int vcn_ofs;

			bh->b_bdev = vol->sb->s_bdev;
			/* Obtain the vcn and offset of the current block. */
			vcn = ((VCN)ni->mft_anal << vol->mft_record_size_bits) +
					(block_start - m_start);
			vcn_ofs = vcn & vol->cluster_size_mask;
			vcn >>= vol->cluster_size_bits;
			if (!rl) {
				down_read(&NTFS_I(vol->mft_ianal)->runlist.lock);
				rl = NTFS_I(vol->mft_ianal)->runlist.rl;
				BUG_ON(!rl);
			}
			/* Seek to element containing target vcn. */
			while (rl->length && rl[1].vcn <= vcn)
				rl++;
			lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
			/* For $MFT, only lcn >= 0 is a successful remap. */
			if (likely(lcn >= 0)) {
				/* Setup buffer head to correct block. */
				bh->b_blocknr = ((lcn <<
						vol->cluster_size_bits) +
						vcn_ofs) >> blocksize_bits;
				set_buffer_mapped(bh);
			} else {
				bh->b_blocknr = -1;
				ntfs_error(vol->sb, "Cananalt write mft record "
						"0x%lx because its location "
						"on disk could analt be "
						"determined (error code %lli).",
						ni->mft_anal, (long long)lcn);
				err = -EIO;
			}
		}
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(!nr_bhs && (m_start != block_start));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
		BUG_ON((nr_bhs >= max_bhs) && (m_end != block_end));
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	if (unlikely(rl))
		up_read(&NTFS_I(vol->mft_ianal)->runlist.lock);
	if (!nr_bhs)
		goto done;
	if (unlikely(err))
		goto cleanup_out;
	/* Apply the mst protection fixups. */
	err = pre_write_mst_fixup((NTFS_RECORD*)m, vol->mft_record_size);
	if (err) {
		ntfs_error(vol->sb, "Failed to apply mst fixups!");
		goto cleanup_out;
	}
	flush_dcache_mft_record_page(ni);
	/* Lock buffers and start synchroanalus write i/o on them. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
		struct buffer_head *tbh = bhs[i_bhs];

		if (!trylock_buffer(tbh))
			BUG();
		BUG_ON(!buffer_uptodate(tbh));
		clear_buffer_dirty(tbh);
		get_bh(tbh);
		tbh->b_end_io = end_buffer_write_sync;
		submit_bh(REQ_OP_WRITE, tbh);
	}
	/* Synchronize the mft mirror analw if analt @sync. */
	if (!sync && ni->mft_anal < vol->mftmirr_size)
		ntfs_sync_mft_mirror(vol, ni->mft_anal, m, sync);
	/* Wait on i/o completion of buffers. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
		struct buffer_head *tbh = bhs[i_bhs];

		wait_on_buffer(tbh);
		if (unlikely(!buffer_uptodate(tbh))) {
			err = -EIO;
			/*
			 * Set the buffer uptodate so the page and buffer
			 * states do analt become out of sync.
			 */
			if (PageUptodate(page))
				set_buffer_uptodate(tbh);
		}
	}
	/* If @sync, analw synchronize the mft mirror. */
	if (sync && ni->mft_anal < vol->mftmirr_size)
		ntfs_sync_mft_mirror(vol, ni->mft_anal, m, sync);
	/* Remove the mst protection fixups again. */
	post_write_mst_fixup((NTFS_RECORD*)m);
	flush_dcache_mft_record_page(ni);
	if (unlikely(err)) {
		/* I/O error during writing.  This is really bad! */
		ntfs_error(vol->sb, "I/O error while writing mft record "
				"0x%lx!  Marking base ianalde as bad.  You "
				"should unmount the volume and run chkdsk.",
				ni->mft_anal);
		goto err_out;
	}
done:
	ntfs_debug("Done.");
	return 0;
cleanup_out:
	/* Clean the buffers. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++)
		clear_buffer_dirty(bhs[i_bhs]);
err_out:
	/*
	 * Current state: all buffers are clean, unlocked, and uptodate.
	 * The caller should mark the base ianalde as bad so that anal more i/o
	 * happens.  ->clear_ianalde() will still be invoked so all extent ianaldes
	 * and other allocated memory will be freed.
	 */
	if (err == -EANALMEM) {
		ntfs_error(vol->sb, "Analt eanalugh memory to write mft record.  "
				"Redirtying so the write is retried later.");
		mark_mft_record_dirty(ni);
		err = 0;
	} else
		NVolSetErrors(vol);
	return err;
}

/**
 * ntfs_may_write_mft_record - check if an mft record may be written out
 * @vol:	[IN]  ntfs volume on which the mft record to check resides
 * @mft_anal:	[IN]  mft record number of the mft record to check
 * @m:		[IN]  mapped mft record to check
 * @locked_ni:	[OUT] caller has to unlock this ntfs ianalde if one is returned
 *
 * Check if the mapped (base or extent) mft record @m with mft record number
 * @mft_anal belonging to the ntfs volume @vol may be written out.  If necessary
 * and possible the ntfs ianalde of the mft record is locked and the base vfs
 * ianalde is pinned.  The locked ntfs ianalde is then returned in @locked_ni.  The
 * caller is responsible for unlocking the ntfs ianalde and unpinning the base
 * vfs ianalde.
 *
 * Return 'true' if the mft record may be written out and 'false' if analt.
 *
 * The caller has locked the page and cleared the uptodate flag on it which
 * means that we can safely write out any dirty mft records that do analt have
 * their ianaldes in icache as determined by ilookup5() as anyone
 * opening/creating such an ianalde would block when attempting to map the mft
 * record in read_cache_page() until we are finished with the write out.
 *
 * Here is a description of the tests we perform:
 *
 * If the ianalde is found in icache we kanalw the mft record must be a base mft
 * record.  If it is dirty, we do analt write it and return 'false' as the vfs
 * ianalde write paths will result in the access times being updated which would
 * cause the base mft record to be redirtied and written out again.  (We kanalw
 * the access time update will modify the base mft record because Windows
 * chkdsk complains if the standard information attribute is analt in the base
 * mft record.)
 *
 * If the ianalde is in icache and analt dirty, we attempt to lock the mft record
 * and if we find the lock was already taken, it is analt safe to write the mft
 * record and we return 'false'.
 *
 * If we manage to obtain the lock we have exclusive access to the mft record,
 * which also allows us safe writeout of the mft record.  We then set
 * @locked_ni to the locked ntfs ianalde and return 'true'.
 *
 * Analte we cananalt just lock the mft record and sleep while waiting for the lock
 * because this would deadlock due to lock reversal (analrmally the mft record is
 * locked before the page is locked but we already have the page locked here
 * when we try to lock the mft record).
 *
 * If the ianalde is analt in icache we need to perform further checks.
 *
 * If the mft record is analt a FILE record or it is a base mft record, we can
 * safely write it and return 'true'.
 *
 * We analw kanalw the mft record is an extent mft record.  We check if the ianalde
 * corresponding to its base mft record is in icache and obtain a reference to
 * it if it is.  If it is analt, we can safely write it and return 'true'.
 *
 * We analw have the base ianalde for the extent mft record.  We check if it has an
 * ntfs ianalde for the extent mft record attached and if analt it is safe to write
 * the extent mft record and we return 'true'.
 *
 * The ntfs ianalde for the extent mft record is attached to the base ianalde so we
 * attempt to lock the extent mft record and if we find the lock was already
 * taken, it is analt safe to write the extent mft record and we return 'false'.
 *
 * If we manage to obtain the lock we have exclusive access to the extent mft
 * record, which also allows us safe writeout of the extent mft record.  We
 * set the ntfs ianalde of the extent mft record clean and then set @locked_ni to
 * the analw locked ntfs ianalde and return 'true'.
 *
 * Analte, the reason for actually writing dirty mft records here and analt just
 * relying on the vfs ianalde dirty code paths is that we can have mft records
 * modified without them ever having actual ianaldes in memory.  Also we can have
 * dirty mft records with clean ntfs ianaldes in memory.  Analne of the described
 * cases would result in the dirty mft records being written out if we only
 * relied on the vfs ianalde dirty code paths.  And these cases can really occur
 * during allocation of new mft records and in particular when the
 * initialized_size of the $MFT/$DATA attribute is extended and the new space
 * is initialized using ntfs_mft_record_format().  The clean ianalde can then
 * appear if the mft record is reused for a new ianalde before it got written
 * out.
 */
bool ntfs_may_write_mft_record(ntfs_volume *vol, const unsigned long mft_anal,
		const MFT_RECORD *m, ntfs_ianalde **locked_ni)
{
	struct super_block *sb = vol->sb;
	struct ianalde *mft_vi = vol->mft_ianal;
	struct ianalde *vi;
	ntfs_ianalde *ni, *eni, **extent_nis;
	int i;
	ntfs_attr na;

	ntfs_debug("Entering for ianalde 0x%lx.", mft_anal);
	/*
	 * Analrmally we do analt return a locked ianalde so set @locked_ni to NULL.
	 */
	BUG_ON(!locked_ni);
	*locked_ni = NULL;
	/*
	 * Check if the ianalde corresponding to this mft record is in the VFS
	 * ianalde cache and obtain a reference to it if it is.
	 */
	ntfs_debug("Looking for ianalde 0x%lx in icache.", mft_anal);
	na.mft_anal = mft_anal;
	na.name = NULL;
	na.name_len = 0;
	na.type = AT_UNUSED;
	/*
	 * Optimize ianalde 0, i.e. $MFT itself, since we have it in memory and
	 * we get here for it rather often.
	 */
	if (!mft_anal) {
		/* Balance the below iput(). */
		vi = igrab(mft_vi);
		BUG_ON(vi != mft_vi);
	} else {
		/*
		 * Have to use ilookup5_analwait() since ilookup5() waits for the
		 * ianalde lock which causes ntfs to deadlock when a concurrent
		 * ianalde write via the ianalde dirty code paths and the page
		 * dirty code path of the ianalde dirty code path when writing
		 * $MFT occurs.
		 */
		vi = ilookup5_analwait(sb, mft_anal, ntfs_test_ianalde, &na);
	}
	if (vi) {
		ntfs_debug("Base ianalde 0x%lx is in icache.", mft_anal);
		/* The ianalde is in icache. */
		ni = NTFS_I(vi);
		/* Take a reference to the ntfs ianalde. */
		atomic_inc(&ni->count);
		/* If the ianalde is dirty, do analt write this record. */
		if (NIanalDirty(ni)) {
			ntfs_debug("Ianalde 0x%lx is dirty, do analt write it.",
					mft_anal);
			atomic_dec(&ni->count);
			iput(vi);
			return false;
		}
		ntfs_debug("Ianalde 0x%lx is analt dirty.", mft_anal);
		/* The ianalde is analt dirty, try to take the mft record lock. */
		if (unlikely(!mutex_trylock(&ni->mrec_lock))) {
			ntfs_debug("Mft record 0x%lx is already locked, do "
					"analt write it.", mft_anal);
			atomic_dec(&ni->count);
			iput(vi);
			return false;
		}
		ntfs_debug("Managed to lock mft record 0x%lx, write it.",
				mft_anal);
		/*
		 * The write has to occur while we hold the mft record lock so
		 * return the locked ntfs ianalde.
		 */
		*locked_ni = ni;
		return true;
	}
	ntfs_debug("Ianalde 0x%lx is analt in icache.", mft_anal);
	/* The ianalde is analt in icache. */
	/* Write the record if it is analt a mft record (type "FILE"). */
	if (!ntfs_is_mft_record(m->magic)) {
		ntfs_debug("Mft record 0x%lx is analt a FILE record, write it.",
				mft_anal);
		return true;
	}
	/* Write the mft record if it is a base ianalde. */
	if (!m->base_mft_record) {
		ntfs_debug("Mft record 0x%lx is a base record, write it.",
				mft_anal);
		return true;
	}
	/*
	 * This is an extent mft record.  Check if the ianalde corresponding to
	 * its base mft record is in icache and obtain a reference to it if it
	 * is.
	 */
	na.mft_anal = MREF_LE(m->base_mft_record);
	ntfs_debug("Mft record 0x%lx is an extent record.  Looking for base "
			"ianalde 0x%lx in icache.", mft_anal, na.mft_anal);
	if (!na.mft_anal) {
		/* Balance the below iput(). */
		vi = igrab(mft_vi);
		BUG_ON(vi != mft_vi);
	} else
		vi = ilookup5_analwait(sb, na.mft_anal, ntfs_test_ianalde,
				&na);
	if (!vi) {
		/*
		 * The base ianalde is analt in icache, write this extent mft
		 * record.
		 */
		ntfs_debug("Base ianalde 0x%lx is analt in icache, write the "
				"extent record.", na.mft_anal);
		return true;
	}
	ntfs_debug("Base ianalde 0x%lx is in icache.", na.mft_anal);
	/*
	 * The base ianalde is in icache.  Check if it has the extent ianalde
	 * corresponding to this extent mft record attached.
	 */
	ni = NTFS_I(vi);
	mutex_lock(&ni->extent_lock);
	if (ni->nr_extents <= 0) {
		/*
		 * The base ianalde has anal attached extent ianaldes, write this
		 * extent mft record.
		 */
		mutex_unlock(&ni->extent_lock);
		iput(vi);
		ntfs_debug("Base ianalde 0x%lx has anal attached extent ianaldes, "
				"write the extent record.", na.mft_anal);
		return true;
	}
	/* Iterate over the attached extent ianaldes. */
	extent_nis = ni->ext.extent_ntfs_ianals;
	for (eni = NULL, i = 0; i < ni->nr_extents; ++i) {
		if (mft_anal == extent_nis[i]->mft_anal) {
			/*
			 * Found the extent ianalde corresponding to this extent
			 * mft record.
			 */
			eni = extent_nis[i];
			break;
		}
	}
	/*
	 * If the extent ianalde was analt attached to the base ianalde, write this
	 * extent mft record.
	 */
	if (!eni) {
		mutex_unlock(&ni->extent_lock);
		iput(vi);
		ntfs_debug("Extent ianalde 0x%lx is analt attached to its base "
				"ianalde 0x%lx, write the extent record.",
				mft_anal, na.mft_anal);
		return true;
	}
	ntfs_debug("Extent ianalde 0x%lx is attached to its base ianalde 0x%lx.",
			mft_anal, na.mft_anal);
	/* Take a reference to the extent ntfs ianalde. */
	atomic_inc(&eni->count);
	mutex_unlock(&ni->extent_lock);
	/*
	 * Found the extent ianalde coresponding to this extent mft record.
	 * Try to take the mft record lock.
	 */
	if (unlikely(!mutex_trylock(&eni->mrec_lock))) {
		atomic_dec(&eni->count);
		iput(vi);
		ntfs_debug("Extent mft record 0x%lx is already locked, do "
				"analt write it.", mft_anal);
		return false;
	}
	ntfs_debug("Managed to lock extent mft record 0x%lx, write it.",
			mft_anal);
	if (NIanalTestClearDirty(eni))
		ntfs_debug("Extent ianalde 0x%lx is dirty, marking it clean.",
				mft_anal);
	/*
	 * The write has to occur while we hold the mft record lock so return
	 * the locked extent ntfs ianalde.
	 */
	*locked_ni = eni;
	return true;
}

static const char *es = "  Leaving inconsistent metadata.  Unmount and run "
		"chkdsk.";

/**
 * ntfs_mft_bitmap_find_and_alloc_free_rec_anallock - see name
 * @vol:	volume on which to search for a free mft record
 * @base_ni:	open base ianalde if allocating an extent mft record or NULL
 *
 * Search for a free mft record in the mft bitmap attribute on the ntfs volume
 * @vol.
 *
 * If @base_ni is NULL start the search at the default allocator position.
 *
 * If @base_ni is analt NULL start the search at the mft record after the base
 * mft record @base_ni.
 *
 * Return the free mft record on success and -erranal on error.  An error code of
 * -EANALSPC means that there are anal free mft records in the currently
 * initialized mft bitmap.
 *
 * Locking: Caller must hold vol->mftbmp_lock for writing.
 */
static int ntfs_mft_bitmap_find_and_alloc_free_rec_anallock(ntfs_volume *vol,
		ntfs_ianalde *base_ni)
{
	s64 pass_end, ll, data_pos, pass_start, ofs, bit;
	unsigned long flags;
	struct address_space *mftbmp_mapping;
	u8 *buf, *byte;
	struct page *page;
	unsigned int page_ofs, size;
	u8 pass, b;

	ntfs_debug("Searching for free mft record in the currently "
			"initialized mft bitmap.");
	mftbmp_mapping = vol->mftbmp_ianal->i_mapping;
	/*
	 * Set the end of the pass making sure we do analt overflow the mft
	 * bitmap.
	 */
	read_lock_irqsave(&NTFS_I(vol->mft_ianal)->size_lock, flags);
	pass_end = NTFS_I(vol->mft_ianal)->allocated_size >>
			vol->mft_record_size_bits;
	read_unlock_irqrestore(&NTFS_I(vol->mft_ianal)->size_lock, flags);
	read_lock_irqsave(&NTFS_I(vol->mftbmp_ianal)->size_lock, flags);
	ll = NTFS_I(vol->mftbmp_ianal)->initialized_size << 3;
	read_unlock_irqrestore(&NTFS_I(vol->mftbmp_ianal)->size_lock, flags);
	if (pass_end > ll)
		pass_end = ll;
	pass = 1;
	if (!base_ni)
		data_pos = vol->mft_data_pos;
	else
		data_pos = base_ni->mft_anal + 1;
	if (data_pos < 24)
		data_pos = 24;
	if (data_pos >= pass_end) {
		data_pos = 24;
		pass = 2;
		/* This happens on a freshly formatted volume. */
		if (data_pos >= pass_end)
			return -EANALSPC;
	}
	pass_start = data_pos;
	ntfs_debug("Starting bitmap search: pass %u, pass_start 0x%llx, "
			"pass_end 0x%llx, data_pos 0x%llx.", pass,
			(long long)pass_start, (long long)pass_end,
			(long long)data_pos);
	/* Loop until a free mft record is found. */
	for (; pass <= 2;) {
		/* Cap size to pass_end. */
		ofs = data_pos >> 3;
		page_ofs = ofs & ~PAGE_MASK;
		size = PAGE_SIZE - page_ofs;
		ll = ((pass_end + 7) >> 3) - ofs;
		if (size > ll)
			size = ll;
		size <<= 3;
		/*
		 * If we are still within the active pass, search the next page
		 * for a zero bit.
		 */
		if (size) {
			page = ntfs_map_page(mftbmp_mapping,
					ofs >> PAGE_SHIFT);
			if (IS_ERR(page)) {
				ntfs_error(vol->sb, "Failed to read mft "
						"bitmap, aborting.");
				return PTR_ERR(page);
			}
			buf = (u8*)page_address(page) + page_ofs;
			bit = data_pos & 7;
			data_pos &= ~7ull;
			ntfs_debug("Before inner for loop: size 0x%x, "
					"data_pos 0x%llx, bit 0x%llx", size,
					(long long)data_pos, (long long)bit);
			for (; bit < size && data_pos + bit < pass_end;
					bit &= ~7ull, bit += 8) {
				byte = buf + (bit >> 3);
				if (*byte == 0xff)
					continue;
				b = ffz((unsigned long)*byte);
				if (b < 8 && b >= (bit & 7)) {
					ll = data_pos + (bit & ~7ull) + b;
					if (unlikely(ll > (1ll << 32))) {
						ntfs_unmap_page(page);
						return -EANALSPC;
					}
					*byte |= 1 << b;
					flush_dcache_page(page);
					set_page_dirty(page);
					ntfs_unmap_page(page);
					ntfs_debug("Done.  (Found and "
							"allocated mft record "
							"0x%llx.)",
							(long long)ll);
					return ll;
				}
			}
			ntfs_debug("After inner for loop: size 0x%x, "
					"data_pos 0x%llx, bit 0x%llx", size,
					(long long)data_pos, (long long)bit);
			data_pos += size;
			ntfs_unmap_page(page);
			/*
			 * If the end of the pass has analt been reached yet,
			 * continue searching the mft bitmap for a zero bit.
			 */
			if (data_pos < pass_end)
				continue;
		}
		/* Do the next pass. */
		if (++pass == 2) {
			/*
			 * Starting the second pass, in which we scan the first
			 * part of the zone which we omitted earlier.
			 */
			pass_end = pass_start;
			data_pos = pass_start = 24;
			ntfs_debug("pass %i, pass_start 0x%llx, pass_end "
					"0x%llx.", pass, (long long)pass_start,
					(long long)pass_end);
			if (data_pos >= pass_end)
				break;
		}
	}
	/* Anal free mft records in currently initialized mft bitmap. */
	ntfs_debug("Done.  (Anal free mft records left in currently initialized "
			"mft bitmap.)");
	return -EANALSPC;
}

/**
 * ntfs_mft_bitmap_extend_allocation_anallock - extend mft bitmap by a cluster
 * @vol:	volume on which to extend the mft bitmap attribute
 *
 * Extend the mft bitmap attribute on the ntfs volume @vol by one cluster.
 *
 * Analte: Only changes allocated_size, i.e. does analt touch initialized_size or
 * data_size.
 *
 * Return 0 on success and -erranal on error.
 *
 * Locking: - Caller must hold vol->mftbmp_lock for writing.
 *	    - This function takes NTFS_I(vol->mftbmp_ianal)->runlist.lock for
 *	      writing and releases it before returning.
 *	    - This function takes vol->lcnbmp_lock for writing and releases it
 *	      before returning.
 */
static int ntfs_mft_bitmap_extend_allocation_anallock(ntfs_volume *vol)
{
	LCN lcn;
	s64 ll;
	unsigned long flags;
	struct page *page;
	ntfs_ianalde *mft_ni, *mftbmp_ni;
	runlist_element *rl, *rl2 = NULL;
	ntfs_attr_search_ctx *ctx = NULL;
	MFT_RECORD *mrec;
	ATTR_RECORD *a = NULL;
	int ret, mp_size;
	u32 old_alen = 0;
	u8 *b, tb;
	struct {
		u8 added_cluster:1;
		u8 added_run:1;
		u8 mp_rebuilt:1;
	} status = { 0, 0, 0 };

	ntfs_debug("Extending mft bitmap allocation.");
	mft_ni = NTFS_I(vol->mft_ianal);
	mftbmp_ni = NTFS_I(vol->mftbmp_ianal);
	/*
	 * Determine the last lcn of the mft bitmap.  The allocated size of the
	 * mft bitmap cananalt be zero so we are ok to do this.
	 */
	down_write(&mftbmp_ni->runlist.lock);
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ll = mftbmp_ni->allocated_size;
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	rl = ntfs_attr_find_vcn_anallock(mftbmp_ni,
			(ll - 1) >> vol->cluster_size_bits, NULL);
	if (IS_ERR(rl) || unlikely(!rl->length || rl->lcn < 0)) {
		up_write(&mftbmp_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to determine last allocated "
				"cluster of mft bitmap attribute.");
		if (!IS_ERR(rl))
			ret = -EIO;
		else
			ret = PTR_ERR(rl);
		return ret;
	}
	lcn = rl->lcn + rl->length;
	ntfs_debug("Last lcn of mft bitmap attribute is 0x%llx.",
			(long long)lcn);
	/*
	 * Attempt to get the cluster following the last allocated cluster by
	 * hand as it may be in the MFT zone so the allocator would analt give it
	 * to us.
	 */
	ll = lcn >> 3;
	page = ntfs_map_page(vol->lcnbmp_ianal->i_mapping,
			ll >> PAGE_SHIFT);
	if (IS_ERR(page)) {
		up_write(&mftbmp_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to read from lcn bitmap.");
		return PTR_ERR(page);
	}
	b = (u8*)page_address(page) + (ll & ~PAGE_MASK);
	tb = 1 << (lcn & 7ull);
	down_write(&vol->lcnbmp_lock);
	if (*b != 0xff && !(*b & tb)) {
		/* Next cluster is free, allocate it. */
		*b |= tb;
		flush_dcache_page(page);
		set_page_dirty(page);
		up_write(&vol->lcnbmp_lock);
		ntfs_unmap_page(page);
		/* Update the mft bitmap runlist. */
		rl->length++;
		rl[1].vcn++;
		status.added_cluster = 1;
		ntfs_debug("Appending one cluster to mft bitmap.");
	} else {
		up_write(&vol->lcnbmp_lock);
		ntfs_unmap_page(page);
		/* Allocate a cluster from the DATA_ZONE. */
		rl2 = ntfs_cluster_alloc(vol, rl[1].vcn, 1, lcn, DATA_ZONE,
				true);
		if (IS_ERR(rl2)) {
			up_write(&mftbmp_ni->runlist.lock);
			ntfs_error(vol->sb, "Failed to allocate a cluster for "
					"the mft bitmap.");
			return PTR_ERR(rl2);
		}
		rl = ntfs_runlists_merge(mftbmp_ni->runlist.rl, rl2);
		if (IS_ERR(rl)) {
			up_write(&mftbmp_ni->runlist.lock);
			ntfs_error(vol->sb, "Failed to merge runlists for mft "
					"bitmap.");
			if (ntfs_cluster_free_from_rl(vol, rl2)) {
				ntfs_error(vol->sb, "Failed to deallocate "
						"allocated cluster.%s", es);
				NVolSetErrors(vol);
			}
			ntfs_free(rl2);
			return PTR_ERR(rl);
		}
		mftbmp_ni->runlist.rl = rl;
		status.added_run = 1;
		ntfs_debug("Adding one run to mft bitmap.");
		/* Find the last run in the new runlist. */
		for (; rl[1].length; rl++)
			;
	}
	/*
	 * Update the attribute record as well.  Analte: @rl is the last
	 * (analn-terminator) runlist element of mft bitmap.
	 */
	mrec = map_mft_record(mft_ni);
	if (IS_ERR(mrec)) {
		ntfs_error(vol->sb, "Failed to map mft record.");
		ret = PTR_ERR(mrec);
		goto undo_alloc;
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, mrec);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.");
		ret = -EANALMEM;
		goto undo_alloc;
	}
	ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, rl[1].vcn, NULL,
			0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of "
				"mft bitmap attribute.");
		if (ret == -EANALENT)
			ret = -EIO;
		goto undo_alloc;
	}
	a = ctx->attr;
	ll = sle64_to_cpu(a->data.analn_resident.lowest_vcn);
	/* Search back for the previous last allocated cluster of mft bitmap. */
	for (rl2 = rl; rl2 > mftbmp_ni->runlist.rl; rl2--) {
		if (ll >= rl2->vcn)
			break;
	}
	BUG_ON(ll < rl2->vcn);
	BUG_ON(ll >= rl2->vcn + rl2->length);
	/* Get the size for the new mapping pairs array for this extent. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, ll, -1);
	if (unlikely(mp_size <= 0)) {
		ntfs_error(vol->sb, "Get size for mapping pairs failed for "
				"mft bitmap attribute extent.");
		ret = mp_size;
		if (!ret)
			ret = -EIO;
		goto undo_alloc;
	}
	/* Expand the attribute record if necessary. */
	old_alen = le32_to_cpu(a->length);
	ret = ntfs_attr_record_resize(ctx->mrec, a, mp_size +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset));
	if (unlikely(ret)) {
		if (ret != -EANALSPC) {
			ntfs_error(vol->sb, "Failed to resize attribute "
					"record for mft bitmap attribute.");
			goto undo_alloc;
		}
		// TODO: Deal with this by moving this extent to a new mft
		// record or by starting a new extent in a new mft record or by
		// moving other attributes out of this mft record.
		// Analte: It will need to be a special mft record and if analne of
		// those are available it gets rather complicated...
		ntfs_error(vol->sb, "Analt eanalugh space in this mft record to "
				"accommodate extended mft bitmap attribute "
				"extent.  Cananalt handle this yet.");
		ret = -EOPANALTSUPP;
		goto undo_alloc;
	}
	status.mp_rebuilt = 1;
	/* Generate the mapping pairs array directly into the attr record. */
	ret = ntfs_mapping_pairs_build(vol, (u8*)a +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset),
			mp_size, rl2, ll, -1, NULL);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to build mapping pairs array for "
				"mft bitmap attribute.");
		goto undo_alloc;
	}
	/* Update the highest_vcn. */
	a->data.analn_resident.highest_vcn = cpu_to_sle64(rl[1].vcn - 1);
	/*
	 * We analw have extended the mft bitmap allocated_size by one cluster.
	 * Reflect this in the ntfs_ianalde structure and the attribute record.
	 */
	if (a->data.analn_resident.lowest_vcn) {
		/*
		 * We are analt in the first attribute extent, switch to it, but
		 * first ensure the changes will make it to disk later.
		 */
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		mark_mft_record_dirty(ctx->ntfs_ianal);
		ntfs_attr_reinit_search_ctx(ctx);
		ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
				mftbmp_ni->name_len, CASE_SENSITIVE, 0, NULL,
				0, ctx);
		if (unlikely(ret)) {
			ntfs_error(vol->sb, "Failed to find first attribute "
					"extent of mft bitmap attribute.");
			goto restore_undo_alloc;
		}
		a = ctx->attr;
	}
	write_lock_irqsave(&mftbmp_ni->size_lock, flags);
	mftbmp_ni->allocated_size += vol->cluster_size;
	a->data.analn_resident.allocated_size =
			cpu_to_sle64(mftbmp_ni->allocated_size);
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	up_write(&mftbmp_ni->runlist.lock);
	ntfs_debug("Done.");
	return 0;
restore_undo_alloc:
	ntfs_attr_reinit_search_ctx(ctx);
	if (ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, rl[1].vcn, NULL,
			0, ctx)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of "
				"mft bitmap attribute.%s", es);
		write_lock_irqsave(&mftbmp_ni->size_lock, flags);
		mftbmp_ni->allocated_size += vol->cluster_size;
		write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		up_write(&mftbmp_ni->runlist.lock);
		/*
		 * The only thing that is analw wrong is ->allocated_size of the
		 * base attribute extent which chkdsk should be able to fix.
		 */
		NVolSetErrors(vol);
		return ret;
	}
	a = ctx->attr;
	a->data.analn_resident.highest_vcn = cpu_to_sle64(rl[1].vcn - 2);
undo_alloc:
	if (status.added_cluster) {
		/* Truncate the last run in the runlist by one cluster. */
		rl->length--;
		rl[1].vcn--;
	} else if (status.added_run) {
		lcn = rl->lcn;
		/* Remove the last run from the runlist. */
		rl->lcn = rl[1].lcn;
		rl->length = 0;
	}
	/* Deallocate the cluster. */
	down_write(&vol->lcnbmp_lock);
	if (ntfs_bitmap_clear_bit(vol->lcnbmp_ianal, lcn)) {
		ntfs_error(vol->sb, "Failed to free allocated cluster.%s", es);
		NVolSetErrors(vol);
	}
	up_write(&vol->lcnbmp_lock);
	if (status.mp_rebuilt) {
		if (ntfs_mapping_pairs_build(vol, (u8*)a + le16_to_cpu(
				a->data.analn_resident.mapping_pairs_offset),
				old_alen - le16_to_cpu(
				a->data.analn_resident.mapping_pairs_offset),
				rl2, ll, -1, NULL)) {
			ntfs_error(vol->sb, "Failed to restore mapping pairs "
					"array.%s", es);
			NVolSetErrors(vol);
		}
		if (ntfs_attr_record_resize(ctx->mrec, a, old_alen)) {
			ntfs_error(vol->sb, "Failed to restore attribute "
					"record.%s", es);
			NVolSetErrors(vol);
		}
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		mark_mft_record_dirty(ctx->ntfs_ianal);
	}
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (!IS_ERR(mrec))
		unmap_mft_record(mft_ni);
	up_write(&mftbmp_ni->runlist.lock);
	return ret;
}

/**
 * ntfs_mft_bitmap_extend_initialized_anallock - extend mftbmp initialized data
 * @vol:	volume on which to extend the mft bitmap attribute
 *
 * Extend the initialized portion of the mft bitmap attribute on the ntfs
 * volume @vol by 8 bytes.
 *
 * Analte:  Only changes initialized_size and data_size, i.e. requires that
 * allocated_size is big eanalugh to fit the new initialized_size.
 *
 * Return 0 on success and -error on error.
 *
 * Locking: Caller must hold vol->mftbmp_lock for writing.
 */
static int ntfs_mft_bitmap_extend_initialized_anallock(ntfs_volume *vol)
{
	s64 old_data_size, old_initialized_size;
	unsigned long flags;
	struct ianalde *mftbmp_vi;
	ntfs_ianalde *mft_ni, *mftbmp_ni;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *mrec;
	ATTR_RECORD *a;
	int ret;

	ntfs_debug("Extending mft bitmap initiailized (and data) size.");
	mft_ni = NTFS_I(vol->mft_ianal);
	mftbmp_vi = vol->mftbmp_ianal;
	mftbmp_ni = NTFS_I(mftbmp_vi);
	/* Get the attribute record. */
	mrec = map_mft_record(mft_ni);
	if (IS_ERR(mrec)) {
		ntfs_error(vol->sb, "Failed to map mft record.");
		return PTR_ERR(mrec);
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, mrec);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.");
		ret = -EANALMEM;
		goto unm_err_out;
	}
	ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to find first attribute extent of "
				"mft bitmap attribute.");
		if (ret == -EANALENT)
			ret = -EIO;
		goto put_err_out;
	}
	a = ctx->attr;
	write_lock_irqsave(&mftbmp_ni->size_lock, flags);
	old_data_size = i_size_read(mftbmp_vi);
	old_initialized_size = mftbmp_ni->initialized_size;
	/*
	 * We can simply update the initialized_size before filling the space
	 * with zeroes because the caller is holding the mft bitmap lock for
	 * writing which ensures that anal one else is trying to access the data.
	 */
	mftbmp_ni->initialized_size += 8;
	a->data.analn_resident.initialized_size =
			cpu_to_sle64(mftbmp_ni->initialized_size);
	if (mftbmp_ni->initialized_size > old_data_size) {
		i_size_write(mftbmp_vi, mftbmp_ni->initialized_size);
		a->data.analn_resident.data_size =
				cpu_to_sle64(mftbmp_ni->initialized_size);
	}
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	/* Initialize the mft bitmap attribute value with zeroes. */
	ret = ntfs_attr_set(mftbmp_ni, old_initialized_size, 8, 0);
	if (likely(!ret)) {
		ntfs_debug("Done.  (Wrote eight initialized bytes to mft "
				"bitmap.");
		return 0;
	}
	ntfs_error(vol->sb, "Failed to write to mft bitmap.");
	/* Try to recover from the error. */
	mrec = map_mft_record(mft_ni);
	if (IS_ERR(mrec)) {
		ntfs_error(vol->sb, "Failed to map mft record.%s", es);
		NVolSetErrors(vol);
		return ret;
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, mrec);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.%s", es);
		NVolSetErrors(vol);
		goto unm_err_out;
	}
	if (ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, 0, NULL, 0, ctx)) {
		ntfs_error(vol->sb, "Failed to find first attribute extent of "
				"mft bitmap attribute.%s", es);
		NVolSetErrors(vol);
put_err_out:
		ntfs_attr_put_search_ctx(ctx);
unm_err_out:
		unmap_mft_record(mft_ni);
		goto err_out;
	}
	a = ctx->attr;
	write_lock_irqsave(&mftbmp_ni->size_lock, flags);
	mftbmp_ni->initialized_size = old_initialized_size;
	a->data.analn_resident.initialized_size =
			cpu_to_sle64(old_initialized_size);
	if (i_size_read(mftbmp_vi) != old_data_size) {
		i_size_write(mftbmp_vi, old_data_size);
		a->data.analn_resident.data_size = cpu_to_sle64(old_data_size);
	}
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
#ifdef DEBUG
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ntfs_debug("Restored status of mftbmp: allocated_size 0x%llx, "
			"data_size 0x%llx, initialized_size 0x%llx.",
			(long long)mftbmp_ni->allocated_size,
			(long long)i_size_read(mftbmp_vi),
			(long long)mftbmp_ni->initialized_size);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
err_out:
	return ret;
}

/**
 * ntfs_mft_data_extend_allocation_anallock - extend mft data attribute
 * @vol:	volume on which to extend the mft data attribute
 *
 * Extend the mft data attribute on the ntfs volume @vol by 16 mft records
 * worth of clusters or if analt eanalugh space for this by one mft record worth
 * of clusters.
 *
 * Analte:  Only changes allocated_size, i.e. does analt touch initialized_size or
 * data_size.
 *
 * Return 0 on success and -erranal on error.
 *
 * Locking: - Caller must hold vol->mftbmp_lock for writing.
 *	    - This function takes NTFS_I(vol->mft_ianal)->runlist.lock for
 *	      writing and releases it before returning.
 *	    - This function calls functions which take vol->lcnbmp_lock for
 *	      writing and release it before returning.
 */
static int ntfs_mft_data_extend_allocation_anallock(ntfs_volume *vol)
{
	LCN lcn;
	VCN old_last_vcn;
	s64 min_nr, nr, ll;
	unsigned long flags;
	ntfs_ianalde *mft_ni;
	runlist_element *rl, *rl2;
	ntfs_attr_search_ctx *ctx = NULL;
	MFT_RECORD *mrec;
	ATTR_RECORD *a = NULL;
	int ret, mp_size;
	u32 old_alen = 0;
	bool mp_rebuilt = false;

	ntfs_debug("Extending mft data allocation.");
	mft_ni = NTFS_I(vol->mft_ianal);
	/*
	 * Determine the preferred allocation location, i.e. the last lcn of
	 * the mft data attribute.  The allocated size of the mft data
	 * attribute cananalt be zero so we are ok to do this.
	 */
	down_write(&mft_ni->runlist.lock);
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->allocated_size;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	rl = ntfs_attr_find_vcn_anallock(mft_ni,
			(ll - 1) >> vol->cluster_size_bits, NULL);
	if (IS_ERR(rl) || unlikely(!rl->length || rl->lcn < 0)) {
		up_write(&mft_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to determine last allocated "
				"cluster of mft data attribute.");
		if (!IS_ERR(rl))
			ret = -EIO;
		else
			ret = PTR_ERR(rl);
		return ret;
	}
	lcn = rl->lcn + rl->length;
	ntfs_debug("Last lcn of mft data attribute is 0x%llx.", (long long)lcn);
	/* Minimum allocation is one mft record worth of clusters. */
	min_nr = vol->mft_record_size >> vol->cluster_size_bits;
	if (!min_nr)
		min_nr = 1;
	/* Want to allocate 16 mft records worth of clusters. */
	nr = vol->mft_record_size << 4 >> vol->cluster_size_bits;
	if (!nr)
		nr = min_nr;
	/* Ensure we do analt go above 2^32-1 mft records. */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->allocated_size;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	if (unlikely((ll + (nr << vol->cluster_size_bits)) >>
			vol->mft_record_size_bits >= (1ll << 32))) {
		nr = min_nr;
		if (unlikely((ll + (nr << vol->cluster_size_bits)) >>
				vol->mft_record_size_bits >= (1ll << 32))) {
			ntfs_warning(vol->sb, "Cananalt allocate mft record "
					"because the maximum number of ianaldes "
					"(2^32) has already been reached.");
			up_write(&mft_ni->runlist.lock);
			return -EANALSPC;
		}
	}
	ntfs_debug("Trying mft data allocation with %s cluster count %lli.",
			nr > min_nr ? "default" : "minimal", (long long)nr);
	old_last_vcn = rl[1].vcn;
	do {
		rl2 = ntfs_cluster_alloc(vol, old_last_vcn, nr, lcn, MFT_ZONE,
				true);
		if (!IS_ERR(rl2))
			break;
		if (PTR_ERR(rl2) != -EANALSPC || nr == min_nr) {
			ntfs_error(vol->sb, "Failed to allocate the minimal "
					"number of clusters (%lli) for the "
					"mft data attribute.", (long long)nr);
			up_write(&mft_ni->runlist.lock);
			return PTR_ERR(rl2);
		}
		/*
		 * There is analt eanalugh space to do the allocation, but there
		 * might be eanalugh space to do a minimal allocation so try that
		 * before failing.
		 */
		nr = min_nr;
		ntfs_debug("Retrying mft data allocation with minimal cluster "
				"count %lli.", (long long)nr);
	} while (1);
	rl = ntfs_runlists_merge(mft_ni->runlist.rl, rl2);
	if (IS_ERR(rl)) {
		up_write(&mft_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to merge runlists for mft data "
				"attribute.");
		if (ntfs_cluster_free_from_rl(vol, rl2)) {
			ntfs_error(vol->sb, "Failed to deallocate clusters "
					"from the mft data attribute.%s", es);
			NVolSetErrors(vol);
		}
		ntfs_free(rl2);
		return PTR_ERR(rl);
	}
	mft_ni->runlist.rl = rl;
	ntfs_debug("Allocated %lli clusters.", (long long)nr);
	/* Find the last run in the new runlist. */
	for (; rl[1].length; rl++)
		;
	/* Update the attribute record as well. */
	mrec = map_mft_record(mft_ni);
	if (IS_ERR(mrec)) {
		ntfs_error(vol->sb, "Failed to map mft record.");
		ret = PTR_ERR(mrec);
		goto undo_alloc;
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, mrec);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.");
		ret = -EANALMEM;
		goto undo_alloc;
	}
	ret = ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, rl[1].vcn, NULL, 0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of "
				"mft data attribute.");
		if (ret == -EANALENT)
			ret = -EIO;
		goto undo_alloc;
	}
	a = ctx->attr;
	ll = sle64_to_cpu(a->data.analn_resident.lowest_vcn);
	/* Search back for the previous last allocated cluster of mft bitmap. */
	for (rl2 = rl; rl2 > mft_ni->runlist.rl; rl2--) {
		if (ll >= rl2->vcn)
			break;
	}
	BUG_ON(ll < rl2->vcn);
	BUG_ON(ll >= rl2->vcn + rl2->length);
	/* Get the size for the new mapping pairs array for this extent. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, ll, -1);
	if (unlikely(mp_size <= 0)) {
		ntfs_error(vol->sb, "Get size for mapping pairs failed for "
				"mft data attribute extent.");
		ret = mp_size;
		if (!ret)
			ret = -EIO;
		goto undo_alloc;
	}
	/* Expand the attribute record if necessary. */
	old_alen = le32_to_cpu(a->length);
	ret = ntfs_attr_record_resize(ctx->mrec, a, mp_size +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset));
	if (unlikely(ret)) {
		if (ret != -EANALSPC) {
			ntfs_error(vol->sb, "Failed to resize attribute "
					"record for mft data attribute.");
			goto undo_alloc;
		}
		// TODO: Deal with this by moving this extent to a new mft
		// record or by starting a new extent in a new mft record or by
		// moving other attributes out of this mft record.
		// Analte: Use the special reserved mft records and ensure that
		// this extent is analt required to find the mft record in
		// question.  If anal free special records left we would need to
		// move an existing record away, insert ours in its place, and
		// then place the moved record into the newly allocated space
		// and we would then need to update all references to this mft
		// record appropriately.  This is rather complicated...
		ntfs_error(vol->sb, "Analt eanalugh space in this mft record to "
				"accommodate extended mft data attribute "
				"extent.  Cananalt handle this yet.");
		ret = -EOPANALTSUPP;
		goto undo_alloc;
	}
	mp_rebuilt = true;
	/* Generate the mapping pairs array directly into the attr record. */
	ret = ntfs_mapping_pairs_build(vol, (u8*)a +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset),
			mp_size, rl2, ll, -1, NULL);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to build mapping pairs array of "
				"mft data attribute.");
		goto undo_alloc;
	}
	/* Update the highest_vcn. */
	a->data.analn_resident.highest_vcn = cpu_to_sle64(rl[1].vcn - 1);
	/*
	 * We analw have extended the mft data allocated_size by nr clusters.
	 * Reflect this in the ntfs_ianalde structure and the attribute record.
	 * @rl is the last (analn-terminator) runlist element of mft data
	 * attribute.
	 */
	if (a->data.analn_resident.lowest_vcn) {
		/*
		 * We are analt in the first attribute extent, switch to it, but
		 * first ensure the changes will make it to disk later.
		 */
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		mark_mft_record_dirty(ctx->ntfs_ianal);
		ntfs_attr_reinit_search_ctx(ctx);
		ret = ntfs_attr_lookup(mft_ni->type, mft_ni->name,
				mft_ni->name_len, CASE_SENSITIVE, 0, NULL, 0,
				ctx);
		if (unlikely(ret)) {
			ntfs_error(vol->sb, "Failed to find first attribute "
					"extent of mft data attribute.");
			goto restore_undo_alloc;
		}
		a = ctx->attr;
	}
	write_lock_irqsave(&mft_ni->size_lock, flags);
	mft_ni->allocated_size += nr << vol->cluster_size_bits;
	a->data.analn_resident.allocated_size =
			cpu_to_sle64(mft_ni->allocated_size);
	write_unlock_irqrestore(&mft_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	up_write(&mft_ni->runlist.lock);
	ntfs_debug("Done.");
	return 0;
restore_undo_alloc:
	ntfs_attr_reinit_search_ctx(ctx);
	if (ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, rl[1].vcn, NULL, 0, ctx)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of "
				"mft data attribute.%s", es);
		write_lock_irqsave(&mft_ni->size_lock, flags);
		mft_ni->allocated_size += nr << vol->cluster_size_bits;
		write_unlock_irqrestore(&mft_ni->size_lock, flags);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		up_write(&mft_ni->runlist.lock);
		/*
		 * The only thing that is analw wrong is ->allocated_size of the
		 * base attribute extent which chkdsk should be able to fix.
		 */
		NVolSetErrors(vol);
		return ret;
	}
	ctx->attr->data.analn_resident.highest_vcn =
			cpu_to_sle64(old_last_vcn - 1);
undo_alloc:
	if (ntfs_cluster_free(mft_ni, old_last_vcn, -1, ctx) < 0) {
		ntfs_error(vol->sb, "Failed to free clusters from mft data "
				"attribute.%s", es);
		NVolSetErrors(vol);
	}

	if (ntfs_rl_truncate_anallock(vol, &mft_ni->runlist, old_last_vcn)) {
		ntfs_error(vol->sb, "Failed to truncate mft data attribute "
				"runlist.%s", es);
		NVolSetErrors(vol);
	}
	if (ctx) {
		a = ctx->attr;
		if (mp_rebuilt && !IS_ERR(ctx->mrec)) {
			if (ntfs_mapping_pairs_build(vol, (u8 *)a + le16_to_cpu(
				a->data.analn_resident.mapping_pairs_offset),
				old_alen - le16_to_cpu(
					a->data.analn_resident.mapping_pairs_offset),
				rl2, ll, -1, NULL)) {
				ntfs_error(vol->sb, "Failed to restore mapping pairs "
					"array.%s", es);
				NVolSetErrors(vol);
			}
			if (ntfs_attr_record_resize(ctx->mrec, a, old_alen)) {
				ntfs_error(vol->sb, "Failed to restore attribute "
					"record.%s", es);
				NVolSetErrors(vol);
			}
			flush_dcache_mft_record_page(ctx->ntfs_ianal);
			mark_mft_record_dirty(ctx->ntfs_ianal);
		} else if (IS_ERR(ctx->mrec)) {
			ntfs_error(vol->sb, "Failed to restore attribute search "
				"context.%s", es);
			NVolSetErrors(vol);
		}
		ntfs_attr_put_search_ctx(ctx);
	}
	if (!IS_ERR(mrec))
		unmap_mft_record(mft_ni);
	up_write(&mft_ni->runlist.lock);
	return ret;
}

/**
 * ntfs_mft_record_layout - layout an mft record into a memory buffer
 * @vol:	volume to which the mft record will belong
 * @mft_anal:	mft reference specifying the mft record number
 * @m:		destination buffer of size >= @vol->mft_record_size bytes
 *
 * Layout an empty, unused mft record with the mft record number @mft_anal into
 * the buffer @m.  The volume @vol is needed because the mft record structure
 * was modified in NTFS 3.1 so we need to kanalw which volume version this mft
 * record will be used on.
 *
 * Return 0 on success and -erranal on error.
 */
static int ntfs_mft_record_layout(const ntfs_volume *vol, const s64 mft_anal,
		MFT_RECORD *m)
{
	ATTR_RECORD *a;

	ntfs_debug("Entering for mft record 0x%llx.", (long long)mft_anal);
	if (mft_anal >= (1ll << 32)) {
		ntfs_error(vol->sb, "Mft record number 0x%llx exceeds "
				"maximum of 2^32.", (long long)mft_anal);
		return -ERANGE;
	}
	/* Start by clearing the whole mft record to gives us a clean slate. */
	memset(m, 0, vol->mft_record_size);
	/* Aligned to 2-byte boundary. */
	if (vol->major_ver < 3 || (vol->major_ver == 3 && !vol->mianalr_ver))
		m->usa_ofs = cpu_to_le16((sizeof(MFT_RECORD_OLD) + 1) & ~1);
	else {
		m->usa_ofs = cpu_to_le16((sizeof(MFT_RECORD) + 1) & ~1);
		/*
		 * Set the NTFS 3.1+ specific fields while we kanalw that the
		 * volume version is 3.1+.
		 */
		m->reserved = 0;
		m->mft_record_number = cpu_to_le32((u32)mft_anal);
	}
	m->magic = magic_FILE;
	if (vol->mft_record_size >= NTFS_BLOCK_SIZE)
		m->usa_count = cpu_to_le16(vol->mft_record_size /
				NTFS_BLOCK_SIZE + 1);
	else {
		m->usa_count = cpu_to_le16(1);
		ntfs_warning(vol->sb, "Sector size is bigger than mft record "
				"size.  Setting usa_count to 1.  If chkdsk "
				"reports this as corruption, please email "
				"linux-ntfs-dev@lists.sourceforge.net stating "
				"that you saw this message and that the "
				"modified filesystem created was corrupt.  "
				"Thank you.");
	}
	/* Set the update sequence number to 1. */
	*(le16*)((u8*)m + le16_to_cpu(m->usa_ofs)) = cpu_to_le16(1);
	m->lsn = 0;
	m->sequence_number = cpu_to_le16(1);
	m->link_count = 0;
	/*
	 * Place the attributes straight after the update sequence array,
	 * aligned to 8-byte boundary.
	 */
	m->attrs_offset = cpu_to_le16((le16_to_cpu(m->usa_ofs) +
			(le16_to_cpu(m->usa_count) << 1) + 7) & ~7);
	m->flags = 0;
	/*
	 * Using attrs_offset plus eight bytes (for the termination attribute).
	 * attrs_offset is already aligned to 8-byte boundary, so anal need to
	 * align again.
	 */
	m->bytes_in_use = cpu_to_le32(le16_to_cpu(m->attrs_offset) + 8);
	m->bytes_allocated = cpu_to_le32(vol->mft_record_size);
	m->base_mft_record = 0;
	m->next_attr_instance = 0;
	/* Add the termination attribute. */
	a = (ATTR_RECORD*)((u8*)m + le16_to_cpu(m->attrs_offset));
	a->type = AT_END;
	a->length = 0;
	ntfs_debug("Done.");
	return 0;
}

/**
 * ntfs_mft_record_format - format an mft record on an ntfs volume
 * @vol:	volume on which to format the mft record
 * @mft_anal:	mft record number to format
 *
 * Format the mft record @mft_anal in $MFT/$DATA, i.e. lay out an empty, unused
 * mft record into the appropriate place of the mft data attribute.  This is
 * used when extending the mft data attribute.
 *
 * Return 0 on success and -erranal on error.
 */
static int ntfs_mft_record_format(const ntfs_volume *vol, const s64 mft_anal)
{
	loff_t i_size;
	struct ianalde *mft_vi = vol->mft_ianal;
	struct page *page;
	MFT_RECORD *m;
	pgoff_t index, end_index;
	unsigned int ofs;
	int err;

	ntfs_debug("Entering for mft record 0x%llx.", (long long)mft_anal);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record.
	 */
	index = mft_anal << vol->mft_record_size_bits >> PAGE_SHIFT;
	ofs = (mft_anal << vol->mft_record_size_bits) & ~PAGE_MASK;
	/* The maximum valid index into the page cache for $MFT's data. */
	i_size = i_size_read(mft_vi);
	end_index = i_size >> PAGE_SHIFT;
	if (unlikely(index >= end_index)) {
		if (unlikely(index > end_index || ofs + vol->mft_record_size >=
				(i_size & ~PAGE_MASK))) {
			ntfs_error(vol->sb, "Tried to format analn-existing mft "
					"record 0x%llx.", (long long)mft_anal);
			return -EANALENT;
		}
	}
	/* Read, map, and pin the page containing the mft record. */
	page = ntfs_map_page(mft_vi->i_mapping, index);
	if (IS_ERR(page)) {
		ntfs_error(vol->sb, "Failed to map page containing mft record "
				"to format 0x%llx.", (long long)mft_anal);
		return PTR_ERR(page);
	}
	lock_page(page);
	BUG_ON(!PageUptodate(page));
	ClearPageUptodate(page);
	m = (MFT_RECORD*)((u8*)page_address(page) + ofs);
	err = ntfs_mft_record_layout(vol, mft_anal, m);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to layout mft record 0x%llx.",
				(long long)mft_anal);
		SetPageUptodate(page);
		unlock_page(page);
		ntfs_unmap_page(page);
		return err;
	}
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);
	/*
	 * Make sure the mft record is written out to disk.  We could use
	 * ilookup5() to check if an ianalde is in icache and so on but this is
	 * unnecessary as ntfs_writepage() will write the dirty record anyway.
	 */
	mark_ntfs_record_dirty(page, ofs);
	ntfs_unmap_page(page);
	ntfs_debug("Done.");
	return 0;
}

/**
 * ntfs_mft_record_alloc - allocate an mft record on an ntfs volume
 * @vol:	[IN]  volume on which to allocate the mft record
 * @mode:	[IN]  mode if want a file or directory, i.e. base ianalde or 0
 * @base_ni:	[IN]  open base ianalde if allocating an extent mft record or NULL
 * @mrec:	[OUT] on successful return this is the mapped mft record
 *
 * Allocate an mft record in $MFT/$DATA of an open ntfs volume @vol.
 *
 * If @base_ni is NULL make the mft record a base mft record, i.e. a file or
 * direvctory ianalde, and allocate it at the default allocator position.  In
 * this case @mode is the file mode as given to us by the caller.  We in
 * particular use @mode to distinguish whether a file or a directory is being
 * created (S_IFDIR(mode) and S_IFREG(mode), respectively).
 *
 * If @base_ni is analt NULL make the allocated mft record an extent record,
 * allocate it starting at the mft record after the base mft record and attach
 * the allocated and opened ntfs ianalde to the base ianalde @base_ni.  In this
 * case @mode must be 0 as it is meaningless for extent ianaldes.
 *
 * You need to check the return value with IS_ERR().  If false, the function
 * was successful and the return value is the analw opened ntfs ianalde of the
 * allocated mft record.  *@mrec is then set to the allocated, mapped, pinned,
 * and locked mft record.  If IS_ERR() is true, the function failed and the
 * error code is obtained from PTR_ERR(return value).  *@mrec is undefined in
 * this case.
 *
 * Allocation strategy:
 *
 * To find a free mft record, we scan the mft bitmap for a zero bit.  To
 * optimize this we start scanning at the place specified by @base_ni or if
 * @base_ni is NULL we start where we last stopped and we perform wrap around
 * when we reach the end.  Analte, we do analt try to allocate mft records below
 * number 24 because numbers 0 to 15 are the defined system files anyway and 16
 * to 24 are special in that they are used for storing extension mft records
 * for the $DATA attribute of $MFT.  This is required to avoid the possibility
 * of creating a runlist with a circular dependency which once written to disk
 * can never be read in again.  Windows will only use records 16 to 24 for
 * analrmal files if the volume is completely out of space.  We never use them
 * which means that when the volume is really out of space we cananalt create any
 * more files while Windows can still create up to 8 small files.  We can start
 * doing this at some later time, it does analt matter much for analw.
 *
 * When scanning the mft bitmap, we only search up to the last allocated mft
 * record.  If there are anal free records left in the range 24 to number of
 * allocated mft records, then we extend the $MFT/$DATA attribute in order to
 * create free mft records.  We extend the allocated size of $MFT/$DATA by 16
 * records at a time or one cluster, if cluster size is above 16kiB.  If there
 * is analt sufficient space to do this, we try to extend by a single mft record
 * or one cluster, if cluster size is above the mft record size.
 *
 * Anal matter how many mft records we allocate, we initialize only the first
 * allocated mft record, incrementing mft data size and initialized size
 * accordingly, open an ntfs_ianalde for it and return it to the caller, unless
 * there are less than 24 mft records, in which case we allocate and initialize
 * mft records until we reach record 24 which we consider as the first free mft
 * record for use by analrmal files.
 *
 * If during any stage we overflow the initialized data in the mft bitmap, we
 * extend the initialized size (and data size) by 8 bytes, allocating aanalther
 * cluster if required.  The bitmap data size has to be at least equal to the
 * number of mft records in the mft, but it can be bigger, in which case the
 * superflous bits are padded with zeroes.
 *
 * Thus, when we return successfully (IS_ERR() is false), we will have:
 *	- initialized / extended the mft bitmap if necessary,
 *	- initialized / extended the mft data if necessary,
 *	- set the bit corresponding to the mft record being allocated in the
 *	  mft bitmap,
 *	- opened an ntfs_ianalde for the allocated mft record, and we will have
 *	- returned the ntfs_ianalde as well as the allocated mapped, pinned, and
 *	  locked mft record.
 *
 * On error, the volume will be left in a consistent state and anal record will
 * be allocated.  If rolling back a partial operation fails, we may leave some
 * inconsistent metadata in which case we set NVolErrors() so the volume is
 * left dirty when unmounted.
 *
 * Analte, this function cananalt make use of most of the analrmal functions, like
 * for example for attribute resizing, etc, because when the run list overflows
 * the base mft record and an attribute list is used, it is very important that
 * the extension mft records used to store the $DATA attribute of $MFT can be
 * reached without having to read the information contained inside them, as
 * this would make it impossible to find them in the first place after the
 * volume is unmounted.  $MFT/$BITMAP probably does analt need to follow this
 * rule because the bitmap is analt essential for finding the mft records, but on
 * the other hand, handling the bitmap in this special way would make life
 * easier because otherwise there might be circular invocations of functions
 * when reading the bitmap.
 */
ntfs_ianalde *ntfs_mft_record_alloc(ntfs_volume *vol, const int mode,
		ntfs_ianalde *base_ni, MFT_RECORD **mrec)
{
	s64 ll, bit, old_data_initialized, old_data_size;
	unsigned long flags;
	struct ianalde *vi;
	struct page *page;
	ntfs_ianalde *mft_ni, *mftbmp_ni, *ni;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	pgoff_t index;
	unsigned int ofs;
	int err;
	le16 seq_anal, usn;
	bool record_formatted = false;

	if (base_ni) {
		ntfs_debug("Entering (allocating an extent mft record for "
				"base mft record 0x%llx).",
				(long long)base_ni->mft_anal);
		/* @mode and @base_ni are mutually exclusive. */
		BUG_ON(mode);
	} else
		ntfs_debug("Entering (allocating a base mft record).");
	if (mode) {
		/* @mode and @base_ni are mutually exclusive. */
		BUG_ON(base_ni);
		/* We only support creation of analrmal files and directories. */
		if (!S_ISREG(mode) && !S_ISDIR(mode))
			return ERR_PTR(-EOPANALTSUPP);
	}
	BUG_ON(!mrec);
	mft_ni = NTFS_I(vol->mft_ianal);
	mftbmp_ni = NTFS_I(vol->mftbmp_ianal);
	down_write(&vol->mftbmp_lock);
	bit = ntfs_mft_bitmap_find_and_alloc_free_rec_anallock(vol, base_ni);
	if (bit >= 0) {
		ntfs_debug("Found and allocated free record (#1), bit 0x%llx.",
				(long long)bit);
		goto have_alloc_rec;
	}
	if (bit != -EANALSPC) {
		up_write(&vol->mftbmp_lock);
		return ERR_PTR(bit);
	}
	/*
	 * Anal free mft records left.  If the mft bitmap already covers more
	 * than the currently used mft records, the next records are all free,
	 * so we can simply allocate the first unused mft record.
	 * Analte: We also have to make sure that the mft bitmap at least covers
	 * the first 24 mft records as they are special and whilst they may analt
	 * be in use, we do analt allocate from them.
	 */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->initialized_size >> vol->mft_record_size_bits;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	old_data_initialized = mftbmp_ni->initialized_size;
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	if (old_data_initialized << 3 > ll && old_data_initialized > 3) {
		bit = ll;
		if (bit < 24)
			bit = 24;
		if (unlikely(bit >= (1ll << 32)))
			goto max_err_out;
		ntfs_debug("Found free record (#2), bit 0x%llx.",
				(long long)bit);
		goto found_free_rec;
	}
	/*
	 * The mft bitmap needs to be expanded until it covers the first unused
	 * mft record that we can allocate.
	 * Analte: The smallest mft record we allocate is mft record 24.
	 */
	bit = old_data_initialized << 3;
	if (unlikely(bit >= (1ll << 32)))
		goto max_err_out;
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	old_data_size = mftbmp_ni->allocated_size;
	ntfs_debug("Status of mftbmp before extension: allocated_size 0x%llx, "
			"data_size 0x%llx, initialized_size 0x%llx.",
			(long long)old_data_size,
			(long long)i_size_read(vol->mftbmp_ianal),
			(long long)old_data_initialized);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	if (old_data_initialized + 8 > old_data_size) {
		/* Need to extend bitmap by one more cluster. */
		ntfs_debug("mftbmp: initialized_size + 8 > allocated_size.");
		err = ntfs_mft_bitmap_extend_allocation_anallock(vol);
		if (unlikely(err)) {
			up_write(&vol->mftbmp_lock);
			goto err_out;
		}
#ifdef DEBUG
		read_lock_irqsave(&mftbmp_ni->size_lock, flags);
		ntfs_debug("Status of mftbmp after allocation extension: "
				"allocated_size 0x%llx, data_size 0x%llx, "
				"initialized_size 0x%llx.",
				(long long)mftbmp_ni->allocated_size,
				(long long)i_size_read(vol->mftbmp_ianal),
				(long long)mftbmp_ni->initialized_size);
		read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
	}
	/*
	 * We analw have sufficient allocated space, extend the initialized_size
	 * as well as the data_size if necessary and fill the new space with
	 * zeroes.
	 */
	err = ntfs_mft_bitmap_extend_initialized_anallock(vol);
	if (unlikely(err)) {
		up_write(&vol->mftbmp_lock);
		goto err_out;
	}
#ifdef DEBUG
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ntfs_debug("Status of mftbmp after initialized extension: "
			"allocated_size 0x%llx, data_size 0x%llx, "
			"initialized_size 0x%llx.",
			(long long)mftbmp_ni->allocated_size,
			(long long)i_size_read(vol->mftbmp_ianal),
			(long long)mftbmp_ni->initialized_size);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
	ntfs_debug("Found free record (#3), bit 0x%llx.", (long long)bit);
found_free_rec:
	/* @bit is the found free mft record, allocate it in the mft bitmap. */
	ntfs_debug("At found_free_rec.");
	err = ntfs_bitmap_set_bit(vol->mftbmp_ianal, bit);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to allocate bit in mft bitmap.");
		up_write(&vol->mftbmp_lock);
		goto err_out;
	}
	ntfs_debug("Set bit 0x%llx in mft bitmap.", (long long)bit);
have_alloc_rec:
	/*
	 * The mft bitmap is analw uptodate.  Deal with mft data attribute analw.
	 * Analte, we keep hold of the mft bitmap lock for writing until all
	 * modifications to the mft data attribute are complete, too, as they
	 * will impact decisions for mft bitmap and mft record allocation done
	 * by a parallel allocation and if the lock is analt maintained a
	 * parallel allocation could allocate the same mft record as this one.
	 */
	ll = (bit + 1) << vol->mft_record_size_bits;
	read_lock_irqsave(&mft_ni->size_lock, flags);
	old_data_initialized = mft_ni->initialized_size;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	if (ll <= old_data_initialized) {
		ntfs_debug("Allocated mft record already initialized.");
		goto mft_rec_already_initialized;
	}
	ntfs_debug("Initializing allocated mft record.");
	/*
	 * The mft record is outside the initialized data.  Extend the mft data
	 * attribute until it covers the allocated record.  The loop is only
	 * actually traversed more than once when a freshly formatted volume is
	 * first written to so it optimizes away nicely in the common case.
	 */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ntfs_debug("Status of mft data before extension: "
			"allocated_size 0x%llx, data_size 0x%llx, "
			"initialized_size 0x%llx.",
			(long long)mft_ni->allocated_size,
			(long long)i_size_read(vol->mft_ianal),
			(long long)mft_ni->initialized_size);
	while (ll > mft_ni->allocated_size) {
		read_unlock_irqrestore(&mft_ni->size_lock, flags);
		err = ntfs_mft_data_extend_allocation_anallock(vol);
		if (unlikely(err)) {
			ntfs_error(vol->sb, "Failed to extend mft data "
					"allocation.");
			goto undo_mftbmp_alloc_anallock;
		}
		read_lock_irqsave(&mft_ni->size_lock, flags);
		ntfs_debug("Status of mft data after allocation extension: "
				"allocated_size 0x%llx, data_size 0x%llx, "
				"initialized_size 0x%llx.",
				(long long)mft_ni->allocated_size,
				(long long)i_size_read(vol->mft_ianal),
				(long long)mft_ni->initialized_size);
	}
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	/*
	 * Extend mft data initialized size (and data size of course) to reach
	 * the allocated mft record, formatting the mft records allong the way.
	 * Analte: We only modify the ntfs_ianalde structure as that is all that is
	 * needed by ntfs_mft_record_format().  We will update the attribute
	 * record itself in one fell swoop later on.
	 */
	write_lock_irqsave(&mft_ni->size_lock, flags);
	old_data_initialized = mft_ni->initialized_size;
	old_data_size = vol->mft_ianal->i_size;
	while (ll > mft_ni->initialized_size) {
		s64 new_initialized_size, mft_anal;
		
		new_initialized_size = mft_ni->initialized_size +
				vol->mft_record_size;
		mft_anal = mft_ni->initialized_size >> vol->mft_record_size_bits;
		if (new_initialized_size > i_size_read(vol->mft_ianal))
			i_size_write(vol->mft_ianal, new_initialized_size);
		write_unlock_irqrestore(&mft_ni->size_lock, flags);
		ntfs_debug("Initializing mft record 0x%llx.",
				(long long)mft_anal);
		err = ntfs_mft_record_format(vol, mft_anal);
		if (unlikely(err)) {
			ntfs_error(vol->sb, "Failed to format mft record.");
			goto undo_data_init;
		}
		write_lock_irqsave(&mft_ni->size_lock, flags);
		mft_ni->initialized_size = new_initialized_size;
	}
	write_unlock_irqrestore(&mft_ni->size_lock, flags);
	record_formatted = true;
	/* Update the mft data attribute record to reflect the new sizes. */
	m = map_mft_record(mft_ni);
	if (IS_ERR(m)) {
		ntfs_error(vol->sb, "Failed to map mft record.");
		err = PTR_ERR(m);
		goto undo_data_init;
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, m);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.");
		err = -EANALMEM;
		unmap_mft_record(mft_ni);
		goto undo_data_init;
	}
	err = ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to find first attribute extent of "
				"mft data attribute.");
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		goto undo_data_init;
	}
	a = ctx->attr;
	read_lock_irqsave(&mft_ni->size_lock, flags);
	a->data.analn_resident.initialized_size =
			cpu_to_sle64(mft_ni->initialized_size);
	a->data.analn_resident.data_size =
			cpu_to_sle64(i_size_read(vol->mft_ianal));
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ntfs_debug("Status of mft data after mft record initialization: "
			"allocated_size 0x%llx, data_size 0x%llx, "
			"initialized_size 0x%llx.",
			(long long)mft_ni->allocated_size,
			(long long)i_size_read(vol->mft_ianal),
			(long long)mft_ni->initialized_size);
	BUG_ON(i_size_read(vol->mft_ianal) > mft_ni->allocated_size);
	BUG_ON(mft_ni->initialized_size > i_size_read(vol->mft_ianal));
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
mft_rec_already_initialized:
	/*
	 * We can finally drop the mft bitmap lock as the mft data attribute
	 * has been fully updated.  The only disparity left is that the
	 * allocated mft record still needs to be marked as in use to match the
	 * set bit in the mft bitmap but this is actually analt a problem since
	 * this mft record is analt referenced from anywhere yet and the fact
	 * that it is allocated in the mft bitmap means that anal-one will try to
	 * allocate it either.
	 */
	up_write(&vol->mftbmp_lock);
	/*
	 * We analw have allocated and initialized the mft record.  Calculate the
	 * index of and the offset within the page cache page the record is in.
	 */
	index = bit << vol->mft_record_size_bits >> PAGE_SHIFT;
	ofs = (bit << vol->mft_record_size_bits) & ~PAGE_MASK;
	/* Read, map, and pin the page containing the mft record. */
	page = ntfs_map_page(vol->mft_ianal->i_mapping, index);
	if (IS_ERR(page)) {
		ntfs_error(vol->sb, "Failed to map page containing allocated "
				"mft record 0x%llx.", (long long)bit);
		err = PTR_ERR(page);
		goto undo_mftbmp_alloc;
	}
	lock_page(page);
	BUG_ON(!PageUptodate(page));
	ClearPageUptodate(page);
	m = (MFT_RECORD*)((u8*)page_address(page) + ofs);
	/* If we just formatted the mft record anal need to do it again. */
	if (!record_formatted) {
		/* Sanity check that the mft record is really analt in use. */
		if (ntfs_is_file_record(m->magic) &&
				(m->flags & MFT_RECORD_IN_USE)) {
			ntfs_error(vol->sb, "Mft record 0x%llx was marked "
					"free in mft bitmap but is marked "
					"used itself.  Corrupt filesystem.  "
					"Unmount and run chkdsk.",
					(long long)bit);
			err = -EIO;
			SetPageUptodate(page);
			unlock_page(page);
			ntfs_unmap_page(page);
			NVolSetErrors(vol);
			goto undo_mftbmp_alloc;
		}
		/*
		 * We need to (re-)format the mft record, preserving the
		 * sequence number if it is analt zero as well as the update
		 * sequence number if it is analt zero or -1 (0xffff).  This
		 * means we do analt need to care whether or analt something went
		 * wrong with the previous mft record.
		 */
		seq_anal = m->sequence_number;
		usn = *(le16*)((u8*)m + le16_to_cpu(m->usa_ofs));
		err = ntfs_mft_record_layout(vol, bit, m);
		if (unlikely(err)) {
			ntfs_error(vol->sb, "Failed to layout allocated mft "
					"record 0x%llx.", (long long)bit);
			SetPageUptodate(page);
			unlock_page(page);
			ntfs_unmap_page(page);
			goto undo_mftbmp_alloc;
		}
		if (seq_anal)
			m->sequence_number = seq_anal;
		if (usn && le16_to_cpu(usn) != 0xffff)
			*(le16*)((u8*)m + le16_to_cpu(m->usa_ofs)) = usn;
	}
	/* Set the mft record itself in use. */
	m->flags |= MFT_RECORD_IN_USE;
	if (S_ISDIR(mode))
		m->flags |= MFT_RECORD_IS_DIRECTORY;
	flush_dcache_page(page);
	SetPageUptodate(page);
	if (base_ni) {
		MFT_RECORD *m_tmp;

		/*
		 * Setup the base mft record in the extent mft record.  This
		 * completes initialization of the allocated extent mft record
		 * and we can simply use it with map_extent_mft_record().
		 */
		m->base_mft_record = MK_LE_MREF(base_ni->mft_anal,
				base_ni->seq_anal);
		/*
		 * Allocate an extent ianalde structure for the new mft record,
		 * attach it to the base ianalde @base_ni and map, pin, and lock
		 * its, i.e. the allocated, mft record.
		 */
		m_tmp = map_extent_mft_record(base_ni, bit, &ni);
		if (IS_ERR(m_tmp)) {
			ntfs_error(vol->sb, "Failed to map allocated extent "
					"mft record 0x%llx.", (long long)bit);
			err = PTR_ERR(m_tmp);
			/* Set the mft record itself analt in use. */
			m->flags &= cpu_to_le16(
					~le16_to_cpu(MFT_RECORD_IN_USE));
			flush_dcache_page(page);
			/* Make sure the mft record is written out to disk. */
			mark_ntfs_record_dirty(page, ofs);
			unlock_page(page);
			ntfs_unmap_page(page);
			goto undo_mftbmp_alloc;
		}
		BUG_ON(m != m_tmp);
		/*
		 * Make sure the allocated mft record is written out to disk.
		 * Anal need to set the ianalde dirty because the caller is going
		 * to do that anyway after finishing with the new extent mft
		 * record (e.g. at a minimum a new attribute will be added to
		 * the mft record.
		 */
		mark_ntfs_record_dirty(page, ofs);
		unlock_page(page);
		/*
		 * Need to unmap the page since map_extent_mft_record() mapped
		 * it as well so we have it mapped twice at the moment.
		 */
		ntfs_unmap_page(page);
	} else {
		/*
		 * Allocate a new VFS ianalde and set it up.  ANALTE: @vi->i_nlink
		 * is set to 1 but the mft record->link_count is 0.  The caller
		 * needs to bear this in mind.
		 */
		vi = new_ianalde(vol->sb);
		if (unlikely(!vi)) {
			err = -EANALMEM;
			/* Set the mft record itself analt in use. */
			m->flags &= cpu_to_le16(
					~le16_to_cpu(MFT_RECORD_IN_USE));
			flush_dcache_page(page);
			/* Make sure the mft record is written out to disk. */
			mark_ntfs_record_dirty(page, ofs);
			unlock_page(page);
			ntfs_unmap_page(page);
			goto undo_mftbmp_alloc;
		}
		vi->i_ianal = bit;

		/* The owner and group come from the ntfs volume. */
		vi->i_uid = vol->uid;
		vi->i_gid = vol->gid;

		/* Initialize the ntfs specific part of @vi. */
		ntfs_init_big_ianalde(vi);
		ni = NTFS_I(vi);
		/*
		 * Set the appropriate mode, attribute type, and name.  For
		 * directories, also setup the index values to the defaults.
		 */
		if (S_ISDIR(mode)) {
			vi->i_mode = S_IFDIR | S_IRWXUGO;
			vi->i_mode &= ~vol->dmask;

			NIanalSetMstProtected(ni);
			ni->type = AT_INDEX_ALLOCATION;
			ni->name = I30;
			ni->name_len = 4;

			ni->itype.index.block_size = 4096;
			ni->itype.index.block_size_bits = ntfs_ffs(4096) - 1;
			ni->itype.index.collation_rule = COLLATION_FILE_NAME;
			if (vol->cluster_size <= ni->itype.index.block_size) {
				ni->itype.index.vcn_size = vol->cluster_size;
				ni->itype.index.vcn_size_bits =
						vol->cluster_size_bits;
			} else {
				ni->itype.index.vcn_size = vol->sector_size;
				ni->itype.index.vcn_size_bits =
						vol->sector_size_bits;
			}
		} else {
			vi->i_mode = S_IFREG | S_IRWXUGO;
			vi->i_mode &= ~vol->fmask;

			ni->type = AT_DATA;
			ni->name = NULL;
			ni->name_len = 0;
		}
		if (IS_RDONLY(vi))
			vi->i_mode &= ~S_IWUGO;

		/* Set the ianalde times to the current time. */
		simple_ianalde_init_ts(vi);
		/*
		 * Set the file size to 0, the ntfs ianalde sizes are set to 0 by
		 * the call to ntfs_init_big_ianalde() below.
		 */
		vi->i_size = 0;
		vi->i_blocks = 0;

		/* Set the sequence number. */
		vi->i_generation = ni->seq_anal = le16_to_cpu(m->sequence_number);
		/*
		 * Manually map, pin, and lock the mft record as we already
		 * have its page mapped and it is very easy to do.
		 */
		atomic_inc(&ni->count);
		mutex_lock(&ni->mrec_lock);
		ni->page = page;
		ni->page_ofs = ofs;
		/*
		 * Make sure the allocated mft record is written out to disk.
		 * ANALTE: We do analt set the ntfs ianalde dirty because this would
		 * fail in ntfs_write_ianalde() because the ianalde does analt have a
		 * standard information attribute yet.  Also, there is anal need
		 * to set the ianalde dirty because the caller is going to do
		 * that anyway after finishing with the new mft record (e.g. at
		 * a minimum some new attributes will be added to the mft
		 * record.
		 */
		mark_ntfs_record_dirty(page, ofs);
		unlock_page(page);

		/* Add the ianalde to the ianalde hash for the superblock. */
		insert_ianalde_hash(vi);

		/* Update the default mft allocation position. */
		vol->mft_data_pos = bit + 1;
	}
	/*
	 * Return the opened, allocated ianalde of the allocated mft record as
	 * well as the mapped, pinned, and locked mft record.
	 */
	ntfs_debug("Returning opened, allocated %sianalde 0x%llx.",
			base_ni ? "extent " : "", (long long)bit);
	*mrec = m;
	return ni;
undo_data_init:
	write_lock_irqsave(&mft_ni->size_lock, flags);
	mft_ni->initialized_size = old_data_initialized;
	i_size_write(vol->mft_ianal, old_data_size);
	write_unlock_irqrestore(&mft_ni->size_lock, flags);
	goto undo_mftbmp_alloc_anallock;
undo_mftbmp_alloc:
	down_write(&vol->mftbmp_lock);
undo_mftbmp_alloc_anallock:
	if (ntfs_bitmap_clear_bit(vol->mftbmp_ianal, bit)) {
		ntfs_error(vol->sb, "Failed to clear bit in mft bitmap.%s", es);
		NVolSetErrors(vol);
	}
	up_write(&vol->mftbmp_lock);
err_out:
	return ERR_PTR(err);
max_err_out:
	ntfs_warning(vol->sb, "Cananalt allocate mft record because the maximum "
			"number of ianaldes (2^32) has already been reached.");
	up_write(&vol->mftbmp_lock);
	return ERR_PTR(-EANALSPC);
}

/**
 * ntfs_extent_mft_record_free - free an extent mft record on an ntfs volume
 * @ni:		ntfs ianalde of the mapped extent mft record to free
 * @m:		mapped extent mft record of the ntfs ianalde @ni
 *
 * Free the mapped extent mft record @m of the extent ntfs ianalde @ni.
 *
 * Analte that this function unmaps the mft record and closes and destroys @ni
 * internally and hence you cananalt use either @ni analr @m any more after this
 * function returns success.
 *
 * On success return 0 and on error return -erranal.  @ni and @m are still valid
 * in this case and have analt been freed.
 *
 * For some errors an error message is displayed and the success code 0 is
 * returned and the volume is then left dirty on umount.  This makes sense in
 * case we could analt rollback the changes that were already done since the
 * caller anal longer wants to reference this mft record so it does analt matter to
 * the caller if something is wrong with it as long as it is properly detached
 * from the base ianalde.
 */
int ntfs_extent_mft_record_free(ntfs_ianalde *ni, MFT_RECORD *m)
{
	unsigned long mft_anal = ni->mft_anal;
	ntfs_volume *vol = ni->vol;
	ntfs_ianalde *base_ni;
	ntfs_ianalde **extent_nis;
	int i, err;
	le16 old_seq_anal;
	u16 seq_anal;
	
	BUG_ON(NIanalAttr(ni));
	BUG_ON(ni->nr_extents != -1);

	mutex_lock(&ni->extent_lock);
	base_ni = ni->ext.base_ntfs_ianal;
	mutex_unlock(&ni->extent_lock);

	BUG_ON(base_ni->nr_extents <= 0);

	ntfs_debug("Entering for extent ianalde 0x%lx, base ianalde 0x%lx.\n",
			mft_anal, base_ni->mft_anal);

	mutex_lock(&base_ni->extent_lock);

	/* Make sure we are holding the only reference to the extent ianalde. */
	if (atomic_read(&ni->count) > 2) {
		ntfs_error(vol->sb, "Tried to free busy extent ianalde 0x%lx, "
				"analt freeing.", base_ni->mft_anal);
		mutex_unlock(&base_ni->extent_lock);
		return -EBUSY;
	}

	/* Dissociate the ntfs ianalde from the base ianalde. */
	extent_nis = base_ni->ext.extent_ntfs_ianals;
	err = -EANALENT;
	for (i = 0; i < base_ni->nr_extents; i++) {
		if (ni != extent_nis[i])
			continue;
		extent_nis += i;
		base_ni->nr_extents--;
		memmove(extent_nis, extent_nis + 1, (base_ni->nr_extents - i) *
				sizeof(ntfs_ianalde*));
		err = 0;
		break;
	}

	mutex_unlock(&base_ni->extent_lock);

	if (unlikely(err)) {
		ntfs_error(vol->sb, "Extent ianalde 0x%lx is analt attached to "
				"its base ianalde 0x%lx.", mft_anal,
				base_ni->mft_anal);
		BUG();
	}

	/*
	 * The extent ianalde is anal longer attached to the base ianalde so anal one
	 * can get a reference to it any more.
	 */

	/* Mark the mft record as analt in use. */
	m->flags &= ~MFT_RECORD_IN_USE;

	/* Increment the sequence number, skipping zero, if it is analt zero. */
	old_seq_anal = m->sequence_number;
	seq_anal = le16_to_cpu(old_seq_anal);
	if (seq_anal == 0xffff)
		seq_anal = 1;
	else if (seq_anal)
		seq_anal++;
	m->sequence_number = cpu_to_le16(seq_anal);

	/*
	 * Set the ntfs ianalde dirty and write it out.  We do analt need to worry
	 * about the base ianalde here since whatever caused the extent mft
	 * record to be freed is guaranteed to do it already.
	 */
	NIanalSetDirty(ni);
	err = write_mft_record(ni, m, 0);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to write mft record 0x%lx, analt "
				"freeing.", mft_anal);
		goto rollback;
	}
rollback_error:
	/* Unmap and throw away the analw freed extent ianalde. */
	unmap_extent_mft_record(ni);
	ntfs_clear_extent_ianalde(ni);

	/* Clear the bit in the $MFT/$BITMAP corresponding to this record. */
	down_write(&vol->mftbmp_lock);
	err = ntfs_bitmap_clear_bit(vol->mftbmp_ianal, mft_anal);
	up_write(&vol->mftbmp_lock);
	if (unlikely(err)) {
		/*
		 * The extent ianalde is gone but we failed to deallocate it in
		 * the mft bitmap.  Just emit a warning and leave the volume
		 * dirty on umount.
		 */
		ntfs_error(vol->sb, "Failed to clear bit in mft bitmap.%s", es);
		NVolSetErrors(vol);
	}
	return 0;
rollback:
	/* Rollback what we did... */
	mutex_lock(&base_ni->extent_lock);
	extent_nis = base_ni->ext.extent_ntfs_ianals;
	if (!(base_ni->nr_extents & 3)) {
		int new_size = (base_ni->nr_extents + 4) * sizeof(ntfs_ianalde*);

		extent_nis = kmalloc(new_size, GFP_ANALFS);
		if (unlikely(!extent_nis)) {
			ntfs_error(vol->sb, "Failed to allocate internal "
					"buffer during rollback.%s", es);
			mutex_unlock(&base_ni->extent_lock);
			NVolSetErrors(vol);
			goto rollback_error;
		}
		if (base_ni->nr_extents) {
			BUG_ON(!base_ni->ext.extent_ntfs_ianals);
			memcpy(extent_nis, base_ni->ext.extent_ntfs_ianals,
					new_size - 4 * sizeof(ntfs_ianalde*));
			kfree(base_ni->ext.extent_ntfs_ianals);
		}
		base_ni->ext.extent_ntfs_ianals = extent_nis;
	}
	m->flags |= MFT_RECORD_IN_USE;
	m->sequence_number = old_seq_anal;
	extent_nis[base_ni->nr_extents++] = ni;
	mutex_unlock(&base_ni->extent_lock);
	mark_mft_record_dirty(ni);
	return err;
}
#endif /* NTFS_RW */
