// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel mft record operations.
 * Part of this file is based on code from the NTFS-3G.
 *
 * Copyright (c) 2001-2012 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/writeback.h>
#include <linux/bio.h>
#include <linux/iomap.h>

#include "bitmap.h"
#include "lcnalloc.h"
#include "mft.h"
#include "ntfs.h"

/*
 * ntfs_mft_record_check - Check the consistency of an MFT record
 *
 * Make sure its general fields are safe, then examine all its
 * attributes and apply generic checks to them.
 *
 * Returns 0 if the checks are successful. If not, return -EIO.
 */
int ntfs_mft_record_check(const struct ntfs_volume *vol, struct mft_record *m,
		unsigned long mft_no)
{
	struct attr_record *a;
	struct super_block *sb = vol->sb;

	if (!ntfs_is_file_record(m->magic)) {
		ntfs_error(sb, "Record %llu has no FILE magic (0x%x)\n",
				(unsigned long long)mft_no, le32_to_cpu(*(__le32 *)m));
		goto err_out;
	}

	if (le16_to_cpu(m->usa_ofs) & 0x1 ||
	    (vol->mft_record_size >> NTFS_BLOCK_SIZE_BITS) + 1 != le16_to_cpu(m->usa_count) ||
	    le16_to_cpu(m->usa_ofs) + le16_to_cpu(m->usa_count) * 2 > vol->mft_record_size) {
		ntfs_error(sb, "Record %llu has corrupt fix-up values fields\n",
				(unsigned long long)mft_no);
		goto err_out;
	}

	if (le32_to_cpu(m->bytes_allocated) != vol->mft_record_size) {
		ntfs_error(sb, "Record %llu has corrupt allocation size (%u <> %u)\n",
				(unsigned long long)mft_no,
				vol->mft_record_size,
				le32_to_cpu(m->bytes_allocated));
		goto err_out;
	}

	if (le32_to_cpu(m->bytes_in_use) > vol->mft_record_size) {
		ntfs_error(sb, "Record %llu has corrupt in-use size (%u > %u)\n",
				(unsigned long long)mft_no,
				le32_to_cpu(m->bytes_in_use),
				vol->mft_record_size);
		goto err_out;
	}

	if (le16_to_cpu(m->attrs_offset) & 7) {
		ntfs_error(sb, "Attributes badly aligned in record %llu\n",
				(unsigned long long)mft_no);
		goto err_out;
	}

	a = (struct attr_record *)((char *)m + le16_to_cpu(m->attrs_offset));
	if ((char *)a < (char *)m || (char *)a > (char *)m + vol->mft_record_size) {
		ntfs_error(sb, "Record %llu is corrupt\n",
				(unsigned long long)mft_no);
		goto err_out;
	}

	return 0;

err_out:
	return -EIO;
}

/*
 * map_mft_record_folio - map the folio in which a specific mft record resides
 * @ni:		ntfs inode whose mft record page to map
 *
 * This maps the folio in which the mft record of the ntfs inode @ni is
 * situated.
 *
 * This allocates a new buffer (@ni->mrec), copies the MFT record data from
 * the mapped folio into this buffer, and applies the MST (Multi Sector
 * Transfer) fixups on the copy.
 *
 * The folio is pinned (referenced) in @ni->folio to ensure the data remains
 * valid in the page cache, but the returned pointer is the allocated copy.
 *
 * Return: A pointer to the allocated and fixed-up mft record (@ni->mrec).
 * The return value needs to be checked with IS_ERR(). If it is true,
 * PTR_ERR() contains the negative error code.
 */
static inline struct mft_record *map_mft_record_folio(struct ntfs_inode *ni)
{
	loff_t i_size;
	struct ntfs_volume *vol = ni->vol;
	struct inode *mft_vi = vol->mft_ino;
	struct folio *folio;
	unsigned long index, end_index;
	unsigned int ofs;

	WARN_ON(ni->folio);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record.
	 */
	index = NTFS_MFT_NR_TO_PIDX(vol, ni->mft_no);
	ofs = NTFS_MFT_NR_TO_POFS(vol, ni->mft_no);

	i_size = i_size_read(mft_vi);
	/* The maximum valid index into the page cache for $MFT's data. */
	end_index = i_size >> PAGE_SHIFT;

	/* If the wanted index is out of bounds the mft record doesn't exist. */
	if (unlikely(index >= end_index)) {
		if (index > end_index || (i_size & ~PAGE_MASK) < ofs +
				vol->mft_record_size) {
			folio = ERR_PTR(-ENOENT);
			ntfs_error(vol->sb,
				"Attempt to read mft record 0x%lx, which is beyond the end of the mft. This is probably a bug in the ntfs driver.",
				ni->mft_no);
			goto err_out;
		}
	}

	/* Read, map, and pin the folio. */
	folio = read_mapping_folio(mft_vi->i_mapping, index, NULL);
	if (!IS_ERR(folio)) {
		u8 *addr;

		ni->mrec = kmalloc(vol->mft_record_size, GFP_NOFS);
		if (!ni->mrec) {
			folio_put(folio);
			folio = ERR_PTR(-ENOMEM);
			goto err_out;
		}

		addr = kmap_local_folio(folio, 0);
		memcpy(ni->mrec, addr + ofs, vol->mft_record_size);
		post_read_mst_fixup((struct ntfs_record *)ni->mrec, vol->mft_record_size);

		/* Catch multi sector transfer fixup errors. */
		if (!ntfs_mft_record_check(vol, (struct mft_record *)ni->mrec, ni->mft_no)) {
			kunmap_local(addr);
			ni->folio = folio;
			ni->folio_ofs = ofs;
			return ni->mrec;
		}
		kunmap_local(addr);
		folio_put(folio);
		kfree(ni->mrec);
		ni->mrec = NULL;
		folio = ERR_PTR(-EIO);
		NVolSetErrors(vol);
	}
err_out:
	ni->folio = NULL;
	ni->folio_ofs = 0;
	return (struct mft_record *)folio;
}

/*
 * map_mft_record - map and pin an mft record
 * @ni:		ntfs inode whose MFT record to map
 *
 * This function ensures the MFT record for the given inode is mapped and
 * accessible.
 *
 * It increments the reference count of the ntfs inode. If the record is
 * already mapped (@ni->folio is set), it returns the cached record
 * immediately.
 *
 * Otherwise, it calls map_mft_record_folio() to read the folio from disk
 * (if necessary via read_mapping_folio), allocate a buffer, and copy the
 * record data.
 *
 * Return: A pointer to the mft record. You need to check the returned
 * pointer with IS_ERR().
 */
struct mft_record *map_mft_record(struct ntfs_inode *ni)
{
	struct mft_record *m;

	if (!ni)
		return ERR_PTR(-EINVAL);

	ntfs_debug("Entering for mft_no 0x%lx.", ni->mft_no);

	/* Make sure the ntfs inode doesn't go away. */
	atomic_inc(&ni->count);

	if (ni->folio)
		return (struct mft_record *)ni->mrec;

	m = map_mft_record_folio(ni);
	if (!IS_ERR(m))
		return m;

	atomic_dec(&ni->count);
	ntfs_error(ni->vol->sb, "Failed with error code %lu.", -PTR_ERR(m));
	return m;
}

/*
 * unmap_mft_record - release a reference to a mapped mft record
 * @ni:		ntfs inode whose MFT record to unmap
 *
 * This decrements the reference count of the ntfs inode.
 *
 * It releases the caller's hold on the inode. If the reference count indicates
 * that there are still other users (count > 1), the function returns
 * immediately, keeping the resources (folio and mrec buffer) pinned for
 * those users.
 *
 * NOTE: If caller has modified the mft record, it is imperative to set the mft
 * record dirty BEFORE calling unmap_mft_record().
 */
void unmap_mft_record(struct ntfs_inode *ni)
{
	struct folio *folio;

	if (!ni)
		return;

	ntfs_debug("Entering for mft_no 0x%lx.", ni->mft_no);

	folio = ni->folio;
	if (atomic_dec_return(&ni->count) > 1)
		return;
	WARN_ON(!folio);
}

/*
 * map_extent_mft_record - load an extent inode and attach it to its base
 * @base_ni:	base ntfs inode
 * @mref:	mft reference of the extent inode to load
 * @ntfs_ino:	on successful return, pointer to the struct ntfs_inode structure
 *
 * Load the extent mft record @mref and attach it to its base inode @base_ni.
 * Return the mapped extent mft record if IS_ERR(result) is false.  Otherwise
 * PTR_ERR(result) gives the negative error code.
 *
 * On successful return, @ntfs_ino contains a pointer to the ntfs_inode
 * structure of the mapped extent inode.
 */
struct mft_record *map_extent_mft_record(struct ntfs_inode *base_ni, u64 mref,
		struct ntfs_inode **ntfs_ino)
{
	struct mft_record *m;
	struct ntfs_inode *ni = NULL;
	struct ntfs_inode **extent_nis = NULL;
	int i;
	unsigned long mft_no = MREF(mref);
	u16 seq_no = MSEQNO(mref);
	bool destroy_ni = false;

	ntfs_debug("Mapping extent mft record 0x%lx (base mft record 0x%lx).",
			mft_no, base_ni->mft_no);
	/* Make sure the base ntfs inode doesn't go away. */
	atomic_inc(&base_ni->count);
	/*
	 * Check if this extent inode has already been added to the base inode,
	 * in which case just return it. If not found, add it to the base
	 * inode before returning it.
	 */
retry:
	mutex_lock(&base_ni->extent_lock);
	if (base_ni->nr_extents > 0) {
		extent_nis = base_ni->ext.extent_ntfs_inos;
		for (i = 0; i < base_ni->nr_extents; i++) {
			if (mft_no != extent_nis[i]->mft_no)
				continue;
			ni = extent_nis[i];
			/* Make sure the ntfs inode doesn't go away. */
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
			if (likely(le16_to_cpu(m->sequence_number) == seq_no)) {
				ntfs_debug("Done 1.");
				*ntfs_ino = ni;
				return m;
			}
			unmap_mft_record(ni);
			ntfs_error(base_ni->vol->sb,
					"Found stale extent mft reference! Corrupt filesystem. Run chkdsk.");
			return ERR_PTR(-EIO);
		}
map_err_out:
		ntfs_error(base_ni->vol->sb,
				"Failed to map extent mft record, error code %ld.",
				-PTR_ERR(m));
		return m;
	}
	mutex_unlock(&base_ni->extent_lock);

	/* Record wasn't there. Get a new ntfs inode and initialize it. */
	ni = ntfs_new_extent_inode(base_ni->vol->sb, mft_no);
	if (unlikely(!ni)) {
		atomic_dec(&base_ni->count);
		return ERR_PTR(-ENOMEM);
	}
	ni->vol = base_ni->vol;
	ni->seq_no = seq_no;
	ni->nr_extents = -1;
	ni->ext.base_ntfs_ino = base_ni;
	/* Now map the record. */
	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		atomic_dec(&base_ni->count);
		ntfs_clear_extent_inode(ni);
		goto map_err_out;
	}
	/* Verify the sequence number if it is present. */
	if (seq_no && (le16_to_cpu(m->sequence_number) != seq_no)) {
		ntfs_error(base_ni->vol->sb,
				"Found stale extent mft reference! Corrupt filesystem. Run chkdsk.");
		destroy_ni = true;
		m = ERR_PTR(-EIO);
		goto unm_nolock_err_out;
	}

	mutex_lock(&base_ni->extent_lock);
	for (i = 0; i < base_ni->nr_extents; i++) {
		if (mft_no == extent_nis[i]->mft_no) {
			mutex_unlock(&base_ni->extent_lock);
			ntfs_clear_extent_inode(ni);
			goto retry;
		}
	}
	/* Attach extent inode to base inode, reallocating memory if needed. */
	if (!(base_ni->nr_extents & 3)) {
		struct ntfs_inode **tmp;
		int new_size = (base_ni->nr_extents + 4) * sizeof(struct ntfs_inode *);

		tmp = kvzalloc(new_size, GFP_NOFS);
		if (unlikely(!tmp)) {
			ntfs_error(base_ni->vol->sb, "Failed to allocate internal buffer.");
			destroy_ni = true;
			m = ERR_PTR(-ENOMEM);
			goto unm_err_out;
		}
		if (base_ni->nr_extents) {
			WARN_ON(!base_ni->ext.extent_ntfs_inos);
			memcpy(tmp, base_ni->ext.extent_ntfs_inos, new_size -
					4 * sizeof(struct ntfs_inode *));
			kvfree(base_ni->ext.extent_ntfs_inos);
		}
		base_ni->ext.extent_ntfs_inos = tmp;
	}
	base_ni->ext.extent_ntfs_inos[base_ni->nr_extents++] = ni;
	mutex_unlock(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	ntfs_debug("Done 2.");
	*ntfs_ino = ni;
	return m;
unm_err_out:
	mutex_unlock(&base_ni->extent_lock);
unm_nolock_err_out:
	unmap_mft_record(ni);
	atomic_dec(&base_ni->count);
	/*
	 * If the extent inode was not attached to the base inode we need to
	 * release it or we will leak memory.
	 */
	if (destroy_ni)
		ntfs_clear_extent_inode(ni);
	return m;
}

/*
 * __mark_mft_record_dirty - mark the base vfs inode dirty
 * @ni:		ntfs inode describing the mapped mft record
 *
 * Internal function.  Users should call mark_mft_record_dirty() instead.
 *
 * This function determines the base ntfs inode (in case @ni is an extent
 * inode) and marks the corresponding VFS inode dirty.
 *
 * NOTE:  We only set I_DIRTY_DATASYNC (and not I_DIRTY_PAGES)
 * on the base vfs inode, because even though file data may have been modified,
 * it is dirty in the inode meta data rather than the data page cache of the
 * inode, and thus there are no data pages that need writing out.  Therefore, a
 * full mark_inode_dirty() is overkill.  A mark_inode_dirty_sync(), on the
 * other hand, is not sufficient, because ->write_inode needs to be called even
 * in case of fdatasync. This needs to happen or the file data would not
 * necessarily hit the device synchronously, even though the vfs inode has the
 * O_SYNC flag set.  Also, I_DIRTY_DATASYNC simply "feels" better than just
 * I_DIRTY_SYNC, since the file data has not actually hit the block device yet,
 * which is not what I_DIRTY_SYNC on its own would suggest.
 */
void __mark_mft_record_dirty(struct ntfs_inode *ni)
{
	struct ntfs_inode *base_ni;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	WARN_ON(NInoAttr(ni));
	/* Determine the base vfs inode and mark it dirty, too. */
	if (likely(ni->nr_extents >= 0))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	__mark_inode_dirty(VFS_I(base_ni), I_DIRTY_DATASYNC);
}

/*
 * ntfs_bio_end_io - bio completion callback for MFT record writes
 *
 * Decrements the folio reference count that was incremented before
 * submit_bio(). This prevents a race condition where umount could
 * evict the inode and release the folio while I/O is still in flight,
 * potentially causing data corruption or use-after-free.
 */
static void ntfs_bio_end_io(struct bio *bio)
{
	if (bio->bi_private)
		folio_put((struct folio *)bio->bi_private);
	bio_put(bio);
}

/*
 * ntfs_sync_mft_mirror - synchronize an mft record to the mft mirror
 * @vol:	ntfs volume on which the mft record to synchronize resides
 * @mft_no:	mft record number of mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 *
 * Write the mapped, mst protected (extent) mft record @m with mft record
 * number @mft_no to the mft mirror ($MFTMirr) of the ntfs volume @vol.
 *
 * On success return 0.  On error return -errno and set the volume errors flag
 * in the ntfs volume @vol.
 *
 * NOTE:  We always perform synchronous i/o.
 */
int ntfs_sync_mft_mirror(struct ntfs_volume *vol, const unsigned long mft_no,
		struct mft_record *m)
{
	u8 *kmirr = NULL;
	struct folio *folio;
	unsigned int folio_ofs, lcn_folio_off = 0;
	int err = 0;
	struct bio *bio;

	ntfs_debug("Entering for inode 0x%lx.", mft_no);

	if (unlikely(!vol->mftmirr_ino)) {
		/* This could happen during umount... */
		err = -EIO;
		goto err_out;
	}
	/* Get the page containing the mirror copy of the mft record @m. */
	folio = read_mapping_folio(vol->mftmirr_ino->i_mapping,
			NTFS_MFT_NR_TO_PIDX(vol, mft_no), NULL);
	if (IS_ERR(folio)) {
		ntfs_error(vol->sb, "Failed to map mft mirror page.");
		err = PTR_ERR(folio);
		goto err_out;
	}

	folio_lock(folio);
	folio_clear_uptodate(folio);
	/* Offset of the mft mirror record inside the page. */
	folio_ofs = NTFS_MFT_NR_TO_POFS(vol, mft_no);
	/* The address in the page of the mirror copy of the mft record @m. */
	kmirr = kmap_local_folio(folio, 0) + folio_ofs;
	/* Copy the mst protected mft record to the mirror. */
	memcpy(kmirr, m, vol->mft_record_size);

	if (vol->cluster_size_bits > PAGE_SHIFT) {
		lcn_folio_off = folio->index << PAGE_SHIFT;
		lcn_folio_off &= vol->cluster_size_mask;
	}

	bio = bio_alloc(vol->sb->s_bdev, 1, REQ_OP_WRITE, GFP_NOIO);
	bio->bi_iter.bi_sector =
		NTFS_B_TO_SECTOR(vol, NTFS_CLU_TO_B(vol, vol->mftmirr_lcn) +
				 lcn_folio_off + folio_ofs);

	if (!bio_add_folio(bio, folio, vol->mft_record_size, folio_ofs)) {
		err = -EIO;
		bio_put(bio);
		goto unlock_folio;
	}

	bio->bi_end_io = ntfs_bio_end_io;
	submit_bio(bio);
	/* Current state: all buffers are clean, unlocked, and uptodate. */
	folio_mark_uptodate(folio);

unlock_folio:
	folio_unlock(folio);
	kunmap_local(kmirr);
	folio_put(folio);
	if (likely(!err)) {
		ntfs_debug("Done.");
	} else {
		ntfs_error(vol->sb, "I/O error while writing mft mirror record 0x%lx!", mft_no);
err_out:
		ntfs_error(vol->sb,
			"Failed to synchronize $MFTMirr (error code %i).  Volume will be left marked dirty on umount.  Run chkdsk on the partition after umounting to correct this.",
			err);
		NVolSetErrors(vol);
	}
	return err;
}

/*
 * write_mft_record_nolock - write out a mapped (extent) mft record
 * @ni:		ntfs inode describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped (extent) mft record @m described by the (regular or extent)
 * ntfs inode @ni to backing store.  If the mft record @m has a counterpart in
 * the mft mirror, that is also updated.
 *
 * We only write the mft record if the ntfs inode @ni is dirty.
 *
 * On success, clean the mft record and return 0.
 * On error (specifically ENOMEM), we redirty the record so it can be retried.
 * For other errors, we mark the volume with errors.
 */
int write_mft_record_nolock(struct ntfs_inode *ni, struct mft_record *m, int sync)
{
	struct ntfs_volume *vol = ni->vol;
	struct folio *folio = ni->folio;
	int err = 0, i = 0;
	u8 *kaddr;
	struct mft_record *fixup_m;
	struct bio *bio;
	unsigned int offset = 0, folio_size;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);

	WARN_ON(NInoAttr(ni));
	WARN_ON(!folio_test_locked(folio));

	/*
	 * If the struct ntfs_inode is clean no need to do anything.  If it is dirty,
	 * mark it as clean now so that it can be redirtied later on if needed.
	 * There is no danger of races since the caller is holding the locks
	 * for the mft record @m and the page it is in.
	 */
	if (!NInoTestClearDirty(ni))
		goto done;

	kaddr = kmap_local_folio(folio, 0);
	fixup_m = (struct mft_record *)(kaddr + ni->folio_ofs);
	memcpy(fixup_m, m, vol->mft_record_size);

	/* Apply the mst protection fixups. */
	err = pre_write_mst_fixup((struct ntfs_record *)fixup_m, vol->mft_record_size);
	if (err) {
		ntfs_error(vol->sb, "Failed to apply mst fixups!");
		goto err_out;
	}

	folio_size = vol->mft_record_size / ni->mft_lcn_count;
	while (i < ni->mft_lcn_count) {
		unsigned int clu_off;

		clu_off = (unsigned int)((s64)ni->mft_no * vol->mft_record_size + offset) &
			vol->cluster_size_mask;

		bio = bio_alloc(vol->sb->s_bdev, 1, REQ_OP_WRITE, GFP_NOIO);
		bio->bi_iter.bi_sector =
			NTFS_B_TO_SECTOR(vol, NTFS_CLU_TO_B(vol, ni->mft_lcn[i]) +
					 clu_off);

		if (!bio_add_folio(bio, folio, folio_size,
				   ni->folio_ofs + offset)) {
			err = -EIO;
			goto put_bio_out;
		}

		/* Synchronize the mft mirror now if not @sync. */
		if (!sync && ni->mft_no < vol->mftmirr_size)
			ntfs_sync_mft_mirror(vol, ni->mft_no, fixup_m);

		folio_get(folio);
		bio->bi_private = folio;
		bio->bi_end_io = ntfs_bio_end_io;
		submit_bio(bio);
		offset += vol->cluster_size;
		i++;
	}

	/* If @sync, now synchronize the mft mirror. */
	if (sync && ni->mft_no < vol->mftmirr_size)
		ntfs_sync_mft_mirror(vol, ni->mft_no, fixup_m);
	kunmap_local(kaddr);
	if (unlikely(err)) {
		/* I/O error during writing.  This is really bad! */
		ntfs_error(vol->sb,
			"I/O error while writing mft record 0x%lx!  Marking base inode as bad.  You should unmount the volume and run chkdsk.",
			ni->mft_no);
		goto err_out;
	}
done:
	ntfs_debug("Done.");
	return 0;
put_bio_out:
	bio_put(bio);
err_out:
	/*
	 * Current state: all buffers are clean, unlocked, and uptodate.
	 * The caller should mark the base inode as bad so that no more i/o
	 * happens.  ->drop_inode() will still be invoked so all extent inodes
	 * and other allocated memory will be freed.
	 */
	if (err == -ENOMEM) {
		ntfs_error(vol->sb,
			"Not enough memory to write mft record. Redirtying so the write is retried later.");
		mark_mft_record_dirty(ni);
		err = 0;
	} else
		NVolSetErrors(vol);
	return err;
}

static int ntfs_test_inode_wb(struct inode *vi, unsigned long ino, void *data)
{
	struct ntfs_attr *na = data;

	if (!ntfs_test_inode(vi, na))
		return 0;

	/*
	 * Without this, ntfs_write_mst_block() could call iput_final()
	 * , and ntfs_evict_big_inode() could try to unlink this inode
	 * and the contex could be blocked infinitly in map_mft_record().
	 */
	if (NInoBeingDeleted(NTFS_I(vi))) {
		na->state = NI_BeingDeleted;
		return -1;
	}

	/*
	 * This condition can prevent ntfs_write_mst_block()
	 * from applying/undo fixups while ntfs_create() being
	 * called
	 */
	spin_lock(&vi->i_lock);
	if (inode_state_read_once(vi) & I_CREATING) {
		spin_unlock(&vi->i_lock);
		na->state = NI_BeingCreated;
		return -1;
	}
	spin_unlock(&vi->i_lock);

	return igrab(vi) ? 1 : -1;
}

/*
 * ntfs_may_write_mft_record - check if an mft record may be written out
 * @vol:	[IN]  ntfs volume on which the mft record to check resides
 * @mft_no:	[IN]  mft record number of the mft record to check
 * @m:		[IN]  mapped mft record to check
 * @locked_ni:	[OUT] caller has to unlock this ntfs inode if one is returned
 * @ref_vi:	[OUT] caller has to drop this vfs inode if one is returned
 *
 * Check if the mapped (base or extent) mft record @m with mft record number
 * @mft_no belonging to the ntfs volume @vol may be written out.  If necessary
 * and possible the ntfs inode of the mft record is locked and the base vfs
 * inode is pinned.  The locked ntfs inode is then returned in @locked_ni.  The
 * caller is responsible for unlocking the ntfs inode and unpinning the base
 * vfs inode.
 *
 * To avoid deadlock when the caller holds a folio lock, if the function
 * returns @ref_vi it defers dropping the vfs inode reference by returning
 * it in @ref_vi instead of calling iput() directly.  The caller must call
 * iput() on @ref_vi after releasing the folio lock.
 *
 * Return 'true' if the mft record may be written out and 'false' if not.
 *
 * The caller has locked the page and cleared the uptodate flag on it which
 * means that we can safely write out any dirty mft records that do not have
 * their inodes in icache as determined by find_inode_nowait().
 *
 * Here is a description of the tests we perform:
 *
 * If the inode is found in icache we know the mft record must be a base mft
 * record.  If it is dirty, we do not write it and return 'false' as the vfs
 * inode write paths will result in the access times being updated which would
 * cause the base mft record to be redirtied and written out again.
 *
 * If the inode is in icache and not dirty, we attempt to lock the mft record
 * and if we find the lock was already taken, it is not safe to write the mft
 * record and we return 'false'.
 *
 * If we manage to obtain the lock we have exclusive access to the mft record,
 * which also allows us safe writeout of the mft record.  We then set
 * @locked_ni to the locked ntfs inode and return 'true'.
 *
 * Note we cannot just lock the mft record and sleep while waiting for the lock
 * because this would deadlock due to lock reversal.
 *
 * If the inode is not in icache we need to perform further checks.
 *
 * If the mft record is not a FILE record or it is a base mft record, we can
 * safely write it and return 'true'.
 *
 * We now know the mft record is an extent mft record.  We check if the inode
 * corresponding to its base mft record is in icache. If it is not, we cannot
 * safely determine the state of the extent inode, so we return 'false'.
 *
 * We now have the base inode for the extent mft record.  We check if it has an
 * ntfs inode for the extent mft record attached. If not, it is safe to write
 * the extent mft record and we return 'true'.
 *
 * If the extent inode is attached, we check if it is dirty. If so, we return
 * 'false' (letting the standard write_inode path handle it).
 *
 * If it is not dirty, we attempt to lock the extent mft record. If the lock
 * was already taken, it is not safe to write and we return 'false'.
 *
 * If we manage to obtain the lock we have exclusive access to the extent mft
 * record. We set @locked_ni to the now locked ntfs inode and return 'true'.
 */
bool ntfs_may_write_mft_record(struct ntfs_volume *vol, const unsigned long mft_no,
		const struct mft_record *m, struct ntfs_inode **locked_ni,
		struct inode **ref_vi)
{
	struct super_block *sb = vol->sb;
	struct inode *mft_vi = vol->mft_ino;
	struct inode *vi;
	struct ntfs_inode *ni, *eni, **extent_nis;
	int i;
	struct ntfs_attr na = {0};

	ntfs_debug("Entering for inode 0x%lx.", mft_no);
	/*
	 * Normally we do not return a locked inode so set @locked_ni to NULL.
	 */
	*locked_ni = NULL;
	*ref_vi = NULL;

	/*
	 * Check if the inode corresponding to this mft record is in the VFS
	 * inode cache and obtain a reference to it if it is.
	 */
	ntfs_debug("Looking for inode 0x%lx in icache.", mft_no);
	na.mft_no = mft_no;
	na.type = AT_UNUSED;
	/*
	 * Optimize inode 0, i.e. $MFT itself, since we have it in memory and
	 * we get here for it rather often.
	 */
	if (!mft_no) {
		/* Balance the below iput(). */
		vi = igrab(mft_vi);
		WARN_ON(vi != mft_vi);
	} else {
		/*
		 * Have to use find_inode_nowait() since ilookup5_nowait()
		 * waits for inode with I_FREEING, which causes ntfs to deadlock
		 * when inodes are unlinked concurrently
		 */
		vi = find_inode_nowait(sb, mft_no, ntfs_test_inode_wb, &na);
		if (na.state == NI_BeingDeleted || na.state == NI_BeingCreated)
			return false;
	}
	if (vi) {
		ntfs_debug("Base inode 0x%lx is in icache.", mft_no);
		/* The inode is in icache. */
		ni = NTFS_I(vi);
		/* Take a reference to the ntfs inode. */
		atomic_inc(&ni->count);
		/* If the inode is dirty, do not write this record. */
		if (NInoDirty(ni)) {
			ntfs_debug("Inode 0x%lx is dirty, do not write it.",
					mft_no);
			atomic_dec(&ni->count);
			*ref_vi = vi;
			return false;
		}
		ntfs_debug("Inode 0x%lx is not dirty.", mft_no);
		/* The inode is not dirty, try to take the mft record lock. */
		if (unlikely(!mutex_trylock(&ni->mrec_lock))) {
			ntfs_debug("Mft record 0x%lx is already locked, do not write it.", mft_no);
			atomic_dec(&ni->count);
			*ref_vi = vi;
			return false;
		}
		ntfs_debug("Managed to lock mft record 0x%lx, write it.",
				mft_no);
		/*
		 * The write has to occur while we hold the mft record lock so
		 * return the locked ntfs inode.
		 */
		*locked_ni = ni;
		return true;
	}
	ntfs_debug("Inode 0x%lx is not in icache.", mft_no);
	/* The inode is not in icache. */
	/* Write the record if it is not a mft record (type "FILE"). */
	if (!ntfs_is_mft_record(m->magic)) {
		ntfs_debug("Mft record 0x%lx is not a FILE record, write it.",
				mft_no);
		return true;
	}
	/* Write the mft record if it is a base inode. */
	if (!m->base_mft_record) {
		ntfs_debug("Mft record 0x%lx is a base record, write it.",
				mft_no);
		return true;
	}
	/*
	 * This is an extent mft record.  Check if the inode corresponding to
	 * its base mft record is in icache and obtain a reference to it if it
	 * is.
	 */
	na.mft_no = MREF_LE(m->base_mft_record);
	na.state = 0;
	ntfs_debug("Mft record 0x%lx is an extent record.  Looking for base inode 0x%lx in icache.",
			mft_no, na.mft_no);
	if (!na.mft_no) {
		/* Balance the below iput(). */
		vi = igrab(mft_vi);
		WARN_ON(vi != mft_vi);
	} else {
		vi = find_inode_nowait(sb, mft_no, ntfs_test_inode_wb, &na);
		if (na.state == NI_BeingDeleted || na.state == NI_BeingCreated)
			return false;
	}

	if (!vi)
		return false;
	ntfs_debug("Base inode 0x%lx is in icache.", na.mft_no);
	/*
	 * The base inode is in icache.  Check if it has the extent inode
	 * corresponding to this extent mft record attached.
	 */
	ni = NTFS_I(vi);
	mutex_lock(&ni->extent_lock);
	if (ni->nr_extents <= 0) {
		/*
		 * The base inode has no attached extent inodes, write this
		 * extent mft record.
		 */
		mutex_unlock(&ni->extent_lock);
		*ref_vi = vi;
		ntfs_debug("Base inode 0x%lx has no attached extent inodes, write the extent record.",
				na.mft_no);
		return true;
	}
	/* Iterate over the attached extent inodes. */
	extent_nis = ni->ext.extent_ntfs_inos;
	for (eni = NULL, i = 0; i < ni->nr_extents; ++i) {
		if (mft_no == extent_nis[i]->mft_no) {
			/*
			 * Found the extent inode corresponding to this extent
			 * mft record.
			 */
			eni = extent_nis[i];
			break;
		}
	}
	/*
	 * If the extent inode was not attached to the base inode, write this
	 * extent mft record.
	 */
	if (!eni) {
		mutex_unlock(&ni->extent_lock);
		*ref_vi = vi;
		ntfs_debug("Extent inode 0x%lx is not attached to its base inode 0x%lx, write the extent record.",
				mft_no, na.mft_no);
		return true;
	}
	ntfs_debug("Extent inode 0x%lx is attached to its base inode 0x%lx.",
			mft_no, na.mft_no);
	/* Take a reference to the extent ntfs inode. */
	atomic_inc(&eni->count);
	mutex_unlock(&ni->extent_lock);

	/* if extent inode is dirty, write_inode will write it */
	if (NInoDirty(eni)) {
		atomic_dec(&eni->count);
		*ref_vi = vi;
		return false;
	}

	/*
	 * Found the extent inode coresponding to this extent mft record.
	 * Try to take the mft record lock.
	 */
	if (unlikely(!mutex_trylock(&eni->mrec_lock))) {
		atomic_dec(&eni->count);
		*ref_vi = vi;
		ntfs_debug("Extent mft record 0x%lx is already locked, do not write it.",
				mft_no);
		return false;
	}
	ntfs_debug("Managed to lock extent mft record 0x%lx, write it.",
			mft_no);
	/*
	 * The write has to occur while we hold the mft record lock so return
	 * the locked extent ntfs inode.
	 */
	*locked_ni = eni;
	return true;
}

static const char *es = "  Leaving inconsistent metadata.  Unmount and run chkdsk.";

#define RESERVED_MFT_RECORDS	64

/*
 * ntfs_mft_bitmap_find_and_alloc_free_rec_nolock - see name
 * @vol:	volume on which to search for a free mft record
 * @base_ni:	open base inode if allocating an extent mft record or NULL
 *
 * Search for a free mft record in the mft bitmap attribute on the ntfs volume
 * @vol.
 *
 * If @base_ni is NULL start the search at the default allocator position.
 *
 * If @base_ni is not NULL start the search at the mft record after the base
 * mft record @base_ni.
 *
 * Return the free mft record on success and -errno on error.  An error code of
 * -ENOSPC means that there are no free mft records in the currently
 * initialized mft bitmap.
 *
 * Locking: Caller must hold vol->mftbmp_lock for writing.
 */
static int ntfs_mft_bitmap_find_and_alloc_free_rec_nolock(struct ntfs_volume *vol,
		struct ntfs_inode *base_ni)
{
	s64 pass_end, ll, data_pos, pass_start, ofs, bit;
	unsigned long flags;
	struct address_space *mftbmp_mapping;
	u8 *buf = NULL, *byte;
	struct folio *folio;
	unsigned int folio_ofs, size;
	u8 pass, b;

	ntfs_debug("Searching for free mft record in the currently initialized mft bitmap.");
	mftbmp_mapping = vol->mftbmp_ino->i_mapping;
	/*
	 * Set the end of the pass making sure we do not overflow the mft
	 * bitmap.
	 */
	read_lock_irqsave(&NTFS_I(vol->mft_ino)->size_lock, flags);
	pass_end = NTFS_I(vol->mft_ino)->allocated_size >>
			vol->mft_record_size_bits;
	read_unlock_irqrestore(&NTFS_I(vol->mft_ino)->size_lock, flags);
	read_lock_irqsave(&NTFS_I(vol->mftbmp_ino)->size_lock, flags);
	ll = NTFS_I(vol->mftbmp_ino)->initialized_size << 3;
	read_unlock_irqrestore(&NTFS_I(vol->mftbmp_ino)->size_lock, flags);
	if (pass_end > ll)
		pass_end = ll;
	pass = 1;
	if (!base_ni)
		data_pos = vol->mft_data_pos;
	else
		data_pos = base_ni->mft_no + 1;
	if (data_pos < RESERVED_MFT_RECORDS)
		data_pos = RESERVED_MFT_RECORDS;
	if (data_pos >= pass_end) {
		data_pos = RESERVED_MFT_RECORDS;
		pass = 2;
		/* This happens on a freshly formatted volume. */
		if (data_pos >= pass_end)
			return -ENOSPC;
	}

	if (base_ni && base_ni->mft_no == FILE_MFT) {
		data_pos = 0;
		pass = 2;
	}

	pass_start = data_pos;
	ntfs_debug("Starting bitmap search: pass %u, pass_start 0x%llx, pass_end 0x%llx, data_pos 0x%llx.",
			pass, pass_start, pass_end, data_pos);
	/* Loop until a free mft record is found. */
	for (; pass <= 2;) {
		/* Cap size to pass_end. */
		ofs = data_pos >> 3;
		folio_ofs = ofs & ~PAGE_MASK;
		size = PAGE_SIZE - folio_ofs;
		ll = ((pass_end + 7) >> 3) - ofs;
		if (size > ll)
			size = ll;
		size <<= 3;
		/*
		 * If we are still within the active pass, search the next page
		 * for a zero bit.
		 */
		if (size) {
			folio = read_mapping_folio(mftbmp_mapping,
					ofs >> PAGE_SHIFT, NULL);
			if (IS_ERR(folio)) {
				ntfs_error(vol->sb, "Failed to read mft bitmap, aborting.");
				return PTR_ERR(folio);
			}
			folio_lock(folio);
			buf = (u8 *)kmap_local_folio(folio, 0) + folio_ofs;
			bit = data_pos & 7;
			data_pos &= ~7ull;
			ntfs_debug("Before inner for loop: size 0x%x, data_pos 0x%llx, bit 0x%llx",
					size, data_pos, bit);
			for (; bit < size && data_pos + bit < pass_end;
					bit &= ~7ull, bit += 8) {
				/*
				 * If we're extending $MFT and running out of the first
				 * mft record (base record) then give up searching since
				 * no guarantee that the found record will be accessible.
				 */
				if (base_ni && base_ni->mft_no == FILE_MFT && bit > 400) {
					folio_unlock(folio);
					kunmap_local(buf);
					folio_put(folio);
					return -ENOSPC;
				}

				byte = buf + (bit >> 3);
				if (*byte == 0xff)
					continue;
				b = ffz((unsigned long)*byte);
				if (b < 8 && b >= (bit & 7)) {
					ll = data_pos + (bit & ~7ull) + b;
					if (unlikely(ll > (1ll << 32))) {
						folio_unlock(folio);
						kunmap_local(buf);
						folio_put(folio);
						return -ENOSPC;
					}
					*byte |= 1 << b;
					folio_mark_dirty(folio);
					folio_unlock(folio);
					kunmap_local(buf);
					folio_put(folio);
					ntfs_debug("Done.  (Found and allocated mft record 0x%llx.)",
							ll);
					return ll;
				}
			}
			ntfs_debug("After inner for loop: size 0x%x, data_pos 0x%llx, bit 0x%llx",
					size, data_pos, bit);
			data_pos += size;
			folio_unlock(folio);
			kunmap_local(buf);
			folio_put(folio);
			/*
			 * If the end of the pass has not been reached yet,
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
			data_pos = pass_start = RESERVED_MFT_RECORDS;
			ntfs_debug("pass %i, pass_start 0x%llx, pass_end 0x%llx.",
					pass, pass_start, pass_end);
			if (data_pos >= pass_end)
				break;
		}
	}
	/* No free mft records in currently initialized mft bitmap. */
	ntfs_debug("Done.  (No free mft records left in currently initialized mft bitmap.)");
	return -ENOSPC;
}

static int ntfs_mft_attr_extend(struct ntfs_inode *ni)
{
	int ret = 0;
	struct ntfs_inode *base_ni;

	if (NInoAttr(ni))
		base_ni = ni->ext.base_ntfs_ino;
	else
		base_ni = ni;

	if (!NInoAttrList(base_ni)) {
		ret = ntfs_inode_add_attrlist(base_ni);
		if (ret) {
			pr_err("Can not add attrlist\n");
			goto out;
		} else {
			ret = -EAGAIN;
			goto out;
		}
	}

	ret = ntfs_attr_update_mapping_pairs(ni, 0);
	if (ret)
		pr_err("MP update failed\n");

out:
	return ret;
}

/*
 * ntfs_mft_bitmap_extend_allocation_nolock - extend mft bitmap by a cluster
 * @vol:	volume on which to extend the mft bitmap attribute
 *
 * Extend the mft bitmap attribute on the ntfs volume @vol by one cluster.
 *
 * Note: Only changes allocated_size, i.e. does not touch initialized_size or
 * data_size.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: - Caller must hold vol->mftbmp_lock for writing.
 *	    - This function takes NTFS_I(vol->mftbmp_ino)->runlist.lock for
 *	      writing and releases it before returning.
 *	    - This function takes vol->lcnbmp_lock for writing and releases it
 *	      before returning.
 */
static int ntfs_mft_bitmap_extend_allocation_nolock(struct ntfs_volume *vol)
{
	s64 lcn;
	s64 ll;
	unsigned long flags;
	struct folio *folio;
	struct ntfs_inode *mft_ni, *mftbmp_ni;
	struct runlist_element *rl, *rl2 = NULL;
	struct ntfs_attr_search_ctx *ctx = NULL;
	struct mft_record *mrec;
	struct attr_record *a = NULL;
	int ret, mp_size;
	u32 old_alen = 0;
	u8 *b, tb;
	struct {
		u8 added_cluster:1;
		u8 added_run:1;
		u8 mp_rebuilt:1;
		u8 mp_extended:1;
	} status = { 0, 0, 0, 0 };
	size_t new_rl_count;

	ntfs_debug("Extending mft bitmap allocation.");
	mft_ni = NTFS_I(vol->mft_ino);
	mftbmp_ni = NTFS_I(vol->mftbmp_ino);
	/*
	 * Determine the last lcn of the mft bitmap.  The allocated size of the
	 * mft bitmap cannot be zero so we are ok to do this.
	 */
	down_write(&mftbmp_ni->runlist.lock);
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ll = mftbmp_ni->allocated_size;
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	rl = ntfs_attr_find_vcn_nolock(mftbmp_ni,
			NTFS_B_TO_CLU(vol, ll - 1), NULL);
	if (IS_ERR(rl) || unlikely(!rl->length || rl->lcn < 0)) {
		up_write(&mftbmp_ni->runlist.lock);
		ntfs_error(vol->sb,
			"Failed to determine last allocated cluster of mft bitmap attribute.");
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
	 * hand as it may be in the MFT zone so the allocator would not give it
	 * to us.
	 */
	ll = lcn >> 3;
	folio = read_mapping_folio(vol->lcnbmp_ino->i_mapping,
			ll >> PAGE_SHIFT, NULL);
	if (IS_ERR(folio)) {
		up_write(&mftbmp_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to read from lcn bitmap.");
		return PTR_ERR(folio);
	}

	down_write(&vol->lcnbmp_lock);
	folio_lock(folio);
	b = (u8 *)kmap_local_folio(folio, 0) + (ll & ~PAGE_MASK);
	tb = 1 << (lcn & 7ull);
	if (*b != 0xff && !(*b & tb)) {
		/* Next cluster is free, allocate it. */
		*b |= tb;
		folio_mark_dirty(folio);
		folio_unlock(folio);
		kunmap_local(b);
		folio_put(folio);
		up_write(&vol->lcnbmp_lock);
		/* Update the mft bitmap runlist. */
		rl->length++;
		rl[1].vcn++;
		status.added_cluster = 1;
		ntfs_debug("Appending one cluster to mft bitmap.");
	} else {
		folio_unlock(folio);
		kunmap_local(b);
		folio_put(folio);
		up_write(&vol->lcnbmp_lock);
		/* Allocate a cluster from the DATA_ZONE. */
		rl2 = ntfs_cluster_alloc(vol, rl[1].vcn, 1, lcn, DATA_ZONE,
				true, false, false);
		if (IS_ERR(rl2)) {
			up_write(&mftbmp_ni->runlist.lock);
			ntfs_error(vol->sb,
					"Failed to allocate a cluster for the mft bitmap.");
			return PTR_ERR(rl2);
		}
		rl = ntfs_runlists_merge(&mftbmp_ni->runlist, rl2, 0, &new_rl_count);
		if (IS_ERR(rl)) {
			up_write(&mftbmp_ni->runlist.lock);
			ntfs_error(vol->sb, "Failed to merge runlists for mft bitmap.");
			if (ntfs_cluster_free_from_rl(vol, rl2)) {
				ntfs_error(vol->sb, "Failed to deallocate allocated cluster.%s",
						es);
				NVolSetErrors(vol);
			}
			kvfree(rl2);
			return PTR_ERR(rl);
		}
		mftbmp_ni->runlist.rl = rl;
		mftbmp_ni->runlist.count = new_rl_count;
		status.added_run = 1;
		ntfs_debug("Adding one run to mft bitmap.");
		/* Find the last run in the new runlist. */
		for (; rl[1].length; rl++)
			;
	}
	/*
	 * Update the attribute record as well.  Note: @rl is the last
	 * (non-terminator) runlist element of mft bitmap.
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
		ret = -ENOMEM;
		goto undo_alloc;
	}
	ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, rl[1].vcn, NULL,
			0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb,
			"Failed to find last attribute extent of mft bitmap attribute.");
		if (ret == -ENOENT)
			ret = -EIO;
		goto undo_alloc;
	}
	a = ctx->attr;
	ll = le64_to_cpu(a->data.non_resident.lowest_vcn);
	/* Search back for the previous last allocated cluster of mft bitmap. */
	for (rl2 = rl; rl2 > mftbmp_ni->runlist.rl; rl2--) {
		if (ll >= rl2->vcn)
			break;
	}
	WARN_ON(ll < rl2->vcn);
	WARN_ON(ll >= rl2->vcn + rl2->length);
	/* Get the size for the new mapping pairs array for this extent. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, ll, -1, -1);
	if (unlikely(mp_size <= 0)) {
		ntfs_error(vol->sb,
			"Get size for mapping pairs failed for mft bitmap attribute extent.");
		ret = mp_size;
		if (!ret)
			ret = -EIO;
		goto undo_alloc;
	}
	/* Expand the attribute record if necessary. */
	old_alen = le32_to_cpu(a->length);
	ret = ntfs_attr_record_resize(ctx->mrec, a, mp_size +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset));
	if (unlikely(ret)) {
		ret = ntfs_mft_attr_extend(mftbmp_ni);
		if (!ret)
			goto extended_ok;
		if (ret != -EAGAIN)
			status.mp_extended = 1;
		goto undo_alloc;
	}
	status.mp_rebuilt = 1;
	/* Generate the mapping pairs array directly into the attr record. */
	ret = ntfs_mapping_pairs_build(vol, (u8 *)a +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset),
			mp_size, rl2, ll, -1, NULL, NULL, NULL);
	if (unlikely(ret)) {
		ntfs_error(vol->sb,
			"Failed to build mapping pairs array for mft bitmap attribute.");
		goto undo_alloc;
	}
	/* Update the highest_vcn. */
	a->data.non_resident.highest_vcn = cpu_to_le64(rl[1].vcn - 1);
	/*
	 * We now have extended the mft bitmap allocated_size by one cluster.
	 * Reflect this in the struct ntfs_inode structure and the attribute record.
	 */
	if (a->data.non_resident.lowest_vcn) {
		/*
		 * We are not in the first attribute extent, switch to it, but
		 * first ensure the changes will make it to disk later.
		 */
		mark_mft_record_dirty(ctx->ntfs_ino);
extended_ok:
		ntfs_attr_reinit_search_ctx(ctx);
		ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
				mftbmp_ni->name_len, CASE_SENSITIVE, 0, NULL,
				0, ctx);
		if (unlikely(ret)) {
			ntfs_error(vol->sb,
				"Failed to find first attribute extent of mft bitmap attribute.");
			goto restore_undo_alloc;
		}
		a = ctx->attr;
	}

	write_lock_irqsave(&mftbmp_ni->size_lock, flags);
	mftbmp_ni->allocated_size += vol->cluster_size;
	a->data.non_resident.allocated_size =
			cpu_to_le64(mftbmp_ni->allocated_size);
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	mark_mft_record_dirty(ctx->ntfs_ino);
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
		ntfs_error(vol->sb,
			"Failed to find last attribute extent of mft bitmap attribute.%s", es);
		write_lock_irqsave(&mftbmp_ni->size_lock, flags);
		mftbmp_ni->allocated_size += vol->cluster_size;
		write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		up_write(&mftbmp_ni->runlist.lock);
		/*
		 * The only thing that is now wrong is ->allocated_size of the
		 * base attribute extent which chkdsk should be able to fix.
		 */
		NVolSetErrors(vol);
		return ret;
	}
	a = ctx->attr;
	a->data.non_resident.highest_vcn = cpu_to_le64(rl[1].vcn - 2);
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
		mftbmp_ni->runlist.count--;
	}
	/* Deallocate the cluster. */
	down_write(&vol->lcnbmp_lock);
	if (ntfs_bitmap_clear_bit(vol->lcnbmp_ino, lcn)) {
		ntfs_error(vol->sb, "Failed to free allocated cluster.%s", es);
		NVolSetErrors(vol);
	} else
		ntfs_inc_free_clusters(vol, 1);
	up_write(&vol->lcnbmp_lock);
	if (status.mp_rebuilt) {
		if (ntfs_mapping_pairs_build(vol, (u8 *)a + le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset),
				old_alen - le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset),
				rl2, ll, -1, NULL, NULL, NULL)) {
			ntfs_error(vol->sb, "Failed to restore mapping pairs array.%s", es);
			NVolSetErrors(vol);
		}
		if (ntfs_attr_record_resize(ctx->mrec, a, old_alen)) {
			ntfs_error(vol->sb, "Failed to restore attribute record.%s", es);
			NVolSetErrors(vol);
		}
		mark_mft_record_dirty(ctx->ntfs_ino);
	} else if (status.mp_extended && ntfs_attr_update_mapping_pairs(mftbmp_ni, 0)) {
		ntfs_error(vol->sb, "Failed to restore mapping pairs.%s", es);
		NVolSetErrors(vol);
	}
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (!IS_ERR(mrec))
		unmap_mft_record(mft_ni);
	up_write(&mftbmp_ni->runlist.lock);
	return ret;
}

/*
 * ntfs_mft_bitmap_extend_initialized_nolock - extend mftbmp initialized data
 * @vol:	volume on which to extend the mft bitmap attribute
 *
 * Extend the initialized portion of the mft bitmap attribute on the ntfs
 * volume @vol by 8 bytes.
 *
 * Note:  Only changes initialized_size and data_size, i.e. requires that
 * allocated_size is big enough to fit the new initialized_size.
 *
 * Return 0 on success and -error on error.
 *
 * Locking: Caller must hold vol->mftbmp_lock for writing.
 */
static int ntfs_mft_bitmap_extend_initialized_nolock(struct ntfs_volume *vol)
{
	s64 old_data_size, old_initialized_size;
	unsigned long flags;
	struct inode *mftbmp_vi;
	struct ntfs_inode *mft_ni, *mftbmp_ni;
	struct ntfs_attr_search_ctx *ctx;
	struct mft_record *mrec;
	struct attr_record *a;
	int ret;

	ntfs_debug("Extending mft bitmap initialized (and data) size.");
	mft_ni = NTFS_I(vol->mft_ino);
	mftbmp_vi = vol->mftbmp_ino;
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
		ret = -ENOMEM;
		goto unm_err_out;
	}
	ret = ntfs_attr_lookup(mftbmp_ni->type, mftbmp_ni->name,
			mftbmp_ni->name_len, CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb,
			"Failed to find first attribute extent of mft bitmap attribute.");
		if (ret == -ENOENT)
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
	 * writing which ensures that no one else is trying to access the data.
	 */
	mftbmp_ni->initialized_size += 8;
	a->data.non_resident.initialized_size =
			cpu_to_le64(mftbmp_ni->initialized_size);
	if (mftbmp_ni->initialized_size > old_data_size) {
		i_size_write(mftbmp_vi, mftbmp_ni->initialized_size);
		a->data.non_resident.data_size =
				cpu_to_le64(mftbmp_ni->initialized_size);
	}
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	/* Initialize the mft bitmap attribute value with zeroes. */
	ret = ntfs_attr_set(mftbmp_ni, old_initialized_size, 8, 0);
	if (likely(!ret)) {
		ntfs_debug("Done.  (Wrote eight initialized bytes to mft bitmap.");
		ntfs_inc_free_mft_records(vol, 8 * 8);
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
		ntfs_error(vol->sb,
			"Failed to find first attribute extent of mft bitmap attribute.%s", es);
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
	a->data.non_resident.initialized_size =
			cpu_to_le64(old_initialized_size);
	if (i_size_read(mftbmp_vi) != old_data_size) {
		i_size_write(mftbmp_vi, old_data_size);
		a->data.non_resident.data_size = cpu_to_le64(old_data_size);
	}
	write_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
#ifdef DEBUG
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ntfs_debug("Restored status of mftbmp: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
			mftbmp_ni->allocated_size, i_size_read(mftbmp_vi),
			mftbmp_ni->initialized_size);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
err_out:
	return ret;
}

/*
 * ntfs_mft_data_extend_allocation_nolock - extend mft data attribute
 * @vol:	volume on which to extend the mft data attribute
 *
 * Extend the mft data attribute on the ntfs volume @vol by 16 mft records
 * worth of clusters or if not enough space for this by one mft record worth
 * of clusters.
 *
 * Note:  Only changes allocated_size, i.e. does not touch initialized_size or
 * data_size.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: - Caller must hold vol->mftbmp_lock for writing.
 *	    - This function takes NTFS_I(vol->mft_ino)->runlist.lock for
 *	      writing and releases it before returning.
 *	    - This function calls functions which take vol->lcnbmp_lock for
 *	      writing and release it before returning.
 */
static int ntfs_mft_data_extend_allocation_nolock(struct ntfs_volume *vol)
{
	s64 lcn;
	s64 old_last_vcn;
	s64 min_nr, nr, ll;
	unsigned long flags;
	struct ntfs_inode *mft_ni;
	struct runlist_element *rl, *rl2;
	struct ntfs_attr_search_ctx *ctx = NULL;
	struct mft_record *mrec;
	struct attr_record *a = NULL;
	int ret, mp_size;
	u32 old_alen = 0;
	bool mp_rebuilt = false, mp_extended = false;
	size_t new_rl_count;

	ntfs_debug("Extending mft data allocation.");
	mft_ni = NTFS_I(vol->mft_ino);
	/*
	 * Determine the preferred allocation location, i.e. the last lcn of
	 * the mft data attribute.  The allocated size of the mft data
	 * attribute cannot be zero so we are ok to do this.
	 */
	down_write(&mft_ni->runlist.lock);
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->allocated_size;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	rl = ntfs_attr_find_vcn_nolock(mft_ni,
			NTFS_B_TO_CLU(vol, ll - 1), NULL);
	if (IS_ERR(rl) || unlikely(!rl->length || rl->lcn < 0)) {
		up_write(&mft_ni->runlist.lock);
		ntfs_error(vol->sb,
			"Failed to determine last allocated cluster of mft data attribute.");
		if (!IS_ERR(rl))
			ret = -EIO;
		else
			ret = PTR_ERR(rl);
		return ret;
	}
	lcn = rl->lcn + rl->length;
	ntfs_debug("Last lcn of mft data attribute is 0x%llx.", lcn);
	/* Minimum allocation is one mft record worth of clusters. */
	min_nr = NTFS_B_TO_CLU(vol, vol->mft_record_size);
	if (!min_nr)
		min_nr = 1;
	/* Want to allocate 16 mft records worth of clusters. */
	nr = vol->mft_record_size << 4 >> vol->cluster_size_bits;
	if (!nr)
		nr = min_nr;
	/* Ensure we do not go above 2^32-1 mft records. */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->allocated_size;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	if (unlikely((ll + NTFS_CLU_TO_B(vol, nr)) >>
			vol->mft_record_size_bits >= (1ll << 32))) {
		nr = min_nr;
		if (unlikely((ll + NTFS_CLU_TO_B(vol, nr)) >>
				vol->mft_record_size_bits >= (1ll << 32))) {
			ntfs_warning(vol->sb,
				"Cannot allocate mft record because the maximum number of inodes (2^32) has already been reached.");
			up_write(&mft_ni->runlist.lock);
			return -ENOSPC;
		}
	}
	ntfs_debug("Trying mft data allocation with %s cluster count %lli.",
			nr > min_nr ? "default" : "minimal", (long long)nr);
	old_last_vcn = rl[1].vcn;
	/*
	 * We can release the mft_ni runlist lock, Because this function is
	 * the only one that expends $MFT data attribute and is called with
	 * mft_ni->mrec_lock.
	 * This is required for the lock order, vol->lcnbmp_lock =>
	 * mft_ni->runlist.lock.
	 */
	up_write(&mft_ni->runlist.lock);

	do {
		rl2 = ntfs_cluster_alloc(vol, old_last_vcn, nr, lcn, MFT_ZONE,
				true, false, false);
		if (!IS_ERR(rl2))
			break;
		if (PTR_ERR(rl2) != -ENOSPC || nr == min_nr) {
			ntfs_error(vol->sb,
				"Failed to allocate the minimal number of clusters (%lli) for the mft data attribute.",
				nr);
			return PTR_ERR(rl2);
		}
		/*
		 * There is not enough space to do the allocation, but there
		 * might be enough space to do a minimal allocation so try that
		 * before failing.
		 */
		nr = min_nr;
		ntfs_debug("Retrying mft data allocation with minimal cluster count %lli.", nr);
	} while (1);

	down_write(&mft_ni->runlist.lock);
	rl = ntfs_runlists_merge(&mft_ni->runlist, rl2, 0, &new_rl_count);
	if (IS_ERR(rl)) {
		up_write(&mft_ni->runlist.lock);
		ntfs_error(vol->sb, "Failed to merge runlists for mft data attribute.");
		if (ntfs_cluster_free_from_rl(vol, rl2)) {
			ntfs_error(vol->sb,
				"Failed to deallocate clusters from the mft data attribute.%s", es);
			NVolSetErrors(vol);
		}
		kvfree(rl2);
		return PTR_ERR(rl);
	}
	mft_ni->runlist.rl = rl;
	mft_ni->runlist.count = new_rl_count;
	ntfs_debug("Allocated %lli clusters.", (long long)nr);
	/* Find the last run in the new runlist. */
	for (; rl[1].length; rl++)
		;
	up_write(&mft_ni->runlist.lock);

	/* Update the attribute record as well. */
	mrec = map_mft_record(mft_ni);
	if (IS_ERR(mrec)) {
		ntfs_error(vol->sb, "Failed to map mft record.");
		ret = PTR_ERR(mrec);
		down_write(&mft_ni->runlist.lock);
		goto undo_alloc;
	}
	ctx = ntfs_attr_get_search_ctx(mft_ni, mrec);
	if (unlikely(!ctx)) {
		ntfs_error(vol->sb, "Failed to get search context.");
		ret = -ENOMEM;
		goto undo_alloc;
	}
	ret = ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, rl[1].vcn, NULL, 0, ctx);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of mft data attribute.");
		if (ret == -ENOENT)
			ret = -EIO;
		goto undo_alloc;
	}
	a = ctx->attr;
	ll = le64_to_cpu(a->data.non_resident.lowest_vcn);

	down_write(&mft_ni->runlist.lock);
	/* Search back for the previous last allocated cluster of mft bitmap. */
	for (rl2 = rl; rl2 > mft_ni->runlist.rl; rl2--) {
		if (ll >= rl2->vcn)
			break;
	}
	WARN_ON(ll < rl2->vcn);
	WARN_ON(ll >= rl2->vcn + rl2->length);
	/* Get the size for the new mapping pairs array for this extent. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, ll, -1, -1);
	if (unlikely(mp_size <= 0)) {
		ntfs_error(vol->sb,
			"Get size for mapping pairs failed for mft data attribute extent.");
		ret = mp_size;
		if (!ret)
			ret = -EIO;
		up_write(&mft_ni->runlist.lock);
		goto undo_alloc;
	}
	up_write(&mft_ni->runlist.lock);

	/* Expand the attribute record if necessary. */
	old_alen = le32_to_cpu(a->length);
	ret = ntfs_attr_record_resize(ctx->mrec, a, mp_size +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset));
	if (unlikely(ret)) {
		ret = ntfs_mft_attr_extend(mft_ni);
		if (!ret)
			goto extended_ok;
		if (ret != -EAGAIN)
			mp_extended = true;
		goto undo_alloc;
	}
	mp_rebuilt = true;
	/* Generate the mapping pairs array directly into the attr record. */
	ret = ntfs_mapping_pairs_build(vol, (u8 *)a +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset),
			mp_size, rl2, ll, -1, NULL, NULL, NULL);
	if (unlikely(ret)) {
		ntfs_error(vol->sb, "Failed to build mapping pairs array of mft data attribute.");
		goto undo_alloc;
	}
	/* Update the highest_vcn. */
	a->data.non_resident.highest_vcn = cpu_to_le64(rl[1].vcn - 1);
	/*
	 * We now have extended the mft data allocated_size by nr clusters.
	 * Reflect this in the struct ntfs_inode structure and the attribute record.
	 * @rl is the last (non-terminator) runlist element of mft data
	 * attribute.
	 */
	if (a->data.non_resident.lowest_vcn) {
		/*
		 * We are not in the first attribute extent, switch to it, but
		 * first ensure the changes will make it to disk later.
		 */
		mark_mft_record_dirty(ctx->ntfs_ino);
extended_ok:
		ntfs_attr_reinit_search_ctx(ctx);
		ret = ntfs_attr_lookup(mft_ni->type, mft_ni->name,
				mft_ni->name_len, CASE_SENSITIVE, 0, NULL, 0,
				ctx);
		if (unlikely(ret)) {
			ntfs_error(vol->sb,
				"Failed to find first attribute extent of mft data attribute.");
			goto restore_undo_alloc;
		}
		a = ctx->attr;
	}

	write_lock_irqsave(&mft_ni->size_lock, flags);
	mft_ni->allocated_size += NTFS_CLU_TO_B(vol, nr);
	a->data.non_resident.allocated_size =
			cpu_to_le64(mft_ni->allocated_size);
	write_unlock_irqrestore(&mft_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	ntfs_debug("Done.");
	return 0;
restore_undo_alloc:
	ntfs_attr_reinit_search_ctx(ctx);
	if (ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, rl[1].vcn, NULL, 0, ctx)) {
		ntfs_error(vol->sb,
			"Failed to find last attribute extent of mft data attribute.%s", es);
		write_lock_irqsave(&mft_ni->size_lock, flags);
		mft_ni->allocated_size += NTFS_CLU_TO_B(vol, nr);
		write_unlock_irqrestore(&mft_ni->size_lock, flags);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		up_write(&mft_ni->runlist.lock);
		/*
		 * The only thing that is now wrong is ->allocated_size of the
		 * base attribute extent which chkdsk should be able to fix.
		 */
		NVolSetErrors(vol);
		return ret;
	}
	ctx->attr->data.non_resident.highest_vcn =
			cpu_to_le64(old_last_vcn - 1);
undo_alloc:
	if (ntfs_cluster_free(mft_ni, old_last_vcn, -1, ctx) < 0) {
		ntfs_error(vol->sb, "Failed to free clusters from mft data attribute.%s", es);
		NVolSetErrors(vol);
	}

	if (ntfs_rl_truncate_nolock(vol, &mft_ni->runlist, old_last_vcn)) {
		ntfs_error(vol->sb, "Failed to truncate mft data attribute runlist.%s", es);
		NVolSetErrors(vol);
	}
	if (mp_extended && ntfs_attr_update_mapping_pairs(mft_ni, 0)) {
		ntfs_error(vol->sb, "Failed to restore mapping pairs.%s",
			   es);
		NVolSetErrors(vol);
	}
	if (ctx) {
		a = ctx->attr;
		if (mp_rebuilt && !IS_ERR(ctx->mrec)) {
			if (ntfs_mapping_pairs_build(vol, (u8 *)a + le16_to_cpu(
				a->data.non_resident.mapping_pairs_offset),
				old_alen - le16_to_cpu(
					a->data.non_resident.mapping_pairs_offset),
				rl2, ll, -1, NULL, NULL, NULL)) {
				ntfs_error(vol->sb, "Failed to restore mapping pairs array.%s", es);
				NVolSetErrors(vol);
			}
			if (ntfs_attr_record_resize(ctx->mrec, a, old_alen)) {
				ntfs_error(vol->sb, "Failed to restore attribute record.%s", es);
				NVolSetErrors(vol);
			}
			mark_mft_record_dirty(ctx->ntfs_ino);
		} else if (IS_ERR(ctx->mrec)) {
			ntfs_error(vol->sb, "Failed to restore attribute search context.%s", es);
			NVolSetErrors(vol);
		}
		ntfs_attr_put_search_ctx(ctx);
	}
	if (!IS_ERR(mrec))
		unmap_mft_record(mft_ni);
	return ret;
}

/*
 * ntfs_mft_record_layout - layout an mft record into a memory buffer
 * @vol:	volume to which the mft record will belong
 * @mft_no:	mft reference specifying the mft record number
 * @m:		destination buffer of size >= @vol->mft_record_size bytes
 *
 * Layout an empty, unused mft record with the mft record number @mft_no into
 * the buffer @m.  The volume @vol is needed because the mft record structure
 * was modified in NTFS 3.1 so we need to know which volume version this mft
 * record will be used on.
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_mft_record_layout(const struct ntfs_volume *vol, const s64 mft_no,
		struct mft_record *m)
{
	struct attr_record *a;

	ntfs_debug("Entering for mft record 0x%llx.", (long long)mft_no);
	if (mft_no >= (1ll << 32)) {
		ntfs_error(vol->sb, "Mft record number 0x%llx exceeds maximum of 2^32.",
				(long long)mft_no);
		return -ERANGE;
	}
	/* Start by clearing the whole mft record to gives us a clean slate. */
	memset(m, 0, vol->mft_record_size);
	/* Aligned to 2-byte boundary. */
	if (vol->major_ver < 3 || (vol->major_ver == 3 && !vol->minor_ver))
		m->usa_ofs = cpu_to_le16((sizeof(struct mft_record_old) + 1) & ~1);
	else {
		m->usa_ofs = cpu_to_le16((sizeof(struct mft_record) + 1) & ~1);
		/*
		 * Set the NTFS 3.1+ specific fields while we know that the
		 * volume version is 3.1+.
		 */
		m->reserved = 0;
		m->mft_record_number = cpu_to_le32((u32)mft_no);
	}
	m->magic = magic_FILE;
	if (vol->mft_record_size >= NTFS_BLOCK_SIZE)
		m->usa_count = cpu_to_le16(vol->mft_record_size /
				NTFS_BLOCK_SIZE + 1);
	else {
		m->usa_count = cpu_to_le16(1);
		ntfs_warning(vol->sb,
			"Sector size is bigger than mft record size.  Setting usa_count to 1.  If chkdsk reports this as corruption");
	}
	/* Set the update sequence number to 1. */
	*(__le16 *)((u8 *)m + le16_to_cpu(m->usa_ofs)) = cpu_to_le16(1);
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
	 * attrs_offset is already aligned to 8-byte boundary, so no need to
	 * align again.
	 */
	m->bytes_in_use = cpu_to_le32(le16_to_cpu(m->attrs_offset) + 8);
	m->bytes_allocated = cpu_to_le32(vol->mft_record_size);
	m->base_mft_record = 0;
	m->next_attr_instance = 0;
	/* Add the termination attribute. */
	a = (struct attr_record *)((u8 *)m + le16_to_cpu(m->attrs_offset));
	a->type = AT_END;
	a->length = 0;
	ntfs_debug("Done.");
	return 0;
}

/*
 * ntfs_mft_record_format - format an mft record on an ntfs volume
 * @vol:	volume on which to format the mft record
 * @mft_no:	mft record number to format
 *
 * Format the mft record @mft_no in $MFT/$DATA, i.e. lay out an empty, unused
 * mft record into the appropriate place of the mft data attribute.  This is
 * used when extending the mft data attribute.
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_mft_record_format(const struct ntfs_volume *vol, const s64 mft_no)
{
	loff_t i_size;
	struct inode *mft_vi = vol->mft_ino;
	struct folio *folio;
	struct mft_record *m;
	pgoff_t index, end_index;
	unsigned int ofs;
	int err;

	ntfs_debug("Entering for mft record 0x%llx.", (long long)mft_no);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record.
	 */
	index = NTFS_MFT_NR_TO_PIDX(vol, mft_no);
	ofs = NTFS_MFT_NR_TO_POFS(vol, mft_no);
	/* The maximum valid index into the page cache for $MFT's data. */
	i_size = i_size_read(mft_vi);
	end_index = i_size >> PAGE_SHIFT;
	if (unlikely(index >= end_index)) {
		if (unlikely(index > end_index ||
			     ofs + vol->mft_record_size > (i_size & ~PAGE_MASK))) {
			ntfs_error(vol->sb, "Tried to format non-existing mft record 0x%llx.",
					(long long)mft_no);
			return -ENOENT;
		}
	}

	/* Read, map, and pin the folio containing the mft record. */
	folio = read_mapping_folio(mft_vi->i_mapping, index, NULL);
	if (IS_ERR(folio)) {
		ntfs_error(vol->sb, "Failed to map page containing mft record to format 0x%llx.",
				(long long)mft_no);
		return PTR_ERR(folio);
	}
	folio_lock(folio);
	folio_clear_uptodate(folio);
	m = (struct mft_record *)((u8 *)kmap_local_folio(folio, 0) + ofs);
	err = ntfs_mft_record_layout(vol, mft_no, m);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to layout mft record 0x%llx.",
				(long long)mft_no);
		folio_mark_uptodate(folio);
		folio_unlock(folio);
		kunmap_local(m);
		folio_put(folio);
		return err;
	}
	pre_write_mst_fixup((struct ntfs_record *)m, vol->mft_record_size);
	folio_mark_uptodate(folio);
	/*
	 * Make sure the mft record is written out to disk.  We could use
	 * ilookup5() to check if an inode is in icache and so on but this is
	 * unnecessary as ntfs_writepage() will write the dirty record anyway.
	 */
	ntfs_mft_mark_dirty(folio);
	folio_unlock(folio);
	kunmap_local(m);
	folio_put(folio);
	ntfs_debug("Done.");
	return 0;
}

/*
 * ntfs_mft_record_alloc - allocate an mft record on an ntfs volume
 * @vol:	[IN]  volume on which to allocate the mft record
 * @mode:	[IN]  mode if want a file or directory, i.e. base inode or 0
 * @ni:		[OUT] on success, set to the allocated ntfs inode
 * @base_ni:	[IN]  open base inode if allocating an extent mft record or NULL
 * @ni_mrec:	[OUT] on successful return this is the mapped mft record
 *
 * Allocate an mft record in $MFT/$DATA of an open ntfs volume @vol.
 *
 * If @base_ni is NULL make the mft record a base mft record, i.e. a file or
 * direvctory inode, and allocate it at the default allocator position.  In
 * this case @mode is the file mode as given to us by the caller.  We in
 * particular use @mode to distinguish whether a file or a directory is being
 * created (S_IFDIR(mode) and S_IFREG(mode), respectively).
 *
 * If @base_ni is not NULL make the allocated mft record an extent record,
 * allocate it starting at the mft record after the base mft record and attach
 * the allocated and opened ntfs inode to the base inode @base_ni.  In this
 * case @mode must be 0 as it is meaningless for extent inodes.
 *
 * You need to check the return value with IS_ERR().  If false, the function
 * was successful and the return value is the now opened ntfs inode of the
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
 * when we reach the end.  Note, we do not try to allocate mft records below
 * number 64 because numbers 0 to 15 are the defined system files anyway and 16
 * to 64 are special in that they are used for storing extension mft records
 * for the $DATA attribute of $MFT.  This is required to avoid the possibility
 * of creating a runlist with a circular dependency which once written to disk
 * can never be read in again.  Windows will only use records 16 to 24 for
 * normal files if the volume is completely out of space.  We never use them
 * which means that when the volume is really out of space we cannot create any
 * more files while Windows can still create up to 8 small files.  We can start
 * doing this at some later time, it does not matter much for now.
 *
 * When scanning the mft bitmap, we only search up to the last allocated mft
 * record.  If there are no free records left in the range 64 to number of
 * allocated mft records, then we extend the $MFT/$DATA attribute in order to
 * create free mft records.  We extend the allocated size of $MFT/$DATA by 16
 * records at a time or one cluster, if cluster size is above 16kiB.  If there
 * is not sufficient space to do this, we try to extend by a single mft record
 * or one cluster, if cluster size is above the mft record size.
 *
 * No matter how many mft records we allocate, we initialize only the first
 * allocated mft record, incrementing mft data size and initialized size
 * accordingly, open an struct ntfs_inode for it and return it to the caller, unless
 * there are less than 64 mft records, in which case we allocate and initialize
 * mft records until we reach record 64 which we consider as the first free mft
 * record for use by normal files.
 *
 * If during any stage we overflow the initialized data in the mft bitmap, we
 * extend the initialized size (and data size) by 8 bytes, allocating another
 * cluster if required.  The bitmap data size has to be at least equal to the
 * number of mft records in the mft, but it can be bigger, in which case the
 * superfluous bits are padded with zeroes.
 *
 * Thus, when we return successfully (IS_ERR() is false), we will have:
 *	- initialized / extended the mft bitmap if necessary,
 *	- initialized / extended the mft data if necessary,
 *	- set the bit corresponding to the mft record being allocated in the
 *	  mft bitmap,
 *	- opened an struct ntfs_inode for the allocated mft record, and we will have
 *	- returned the struct ntfs_inode as well as the allocated mapped, pinned, and
 *	  locked mft record.
 *
 * On error, the volume will be left in a consistent state and no record will
 * be allocated.  If rolling back a partial operation fails, we may leave some
 * inconsistent metadata in which case we set NVolErrors() so the volume is
 * left dirty when unmounted.
 *
 * Note, this function cannot make use of most of the normal functions, like
 * for example for attribute resizing, etc, because when the run list overflows
 * the base mft record and an attribute list is used, it is very important that
 * the extension mft records used to store the $DATA attribute of $MFT can be
 * reached without having to read the information contained inside them, as
 * this would make it impossible to find them in the first place after the
 * volume is unmounted.  $MFT/$BITMAP probably does not need to follow this
 * rule because the bitmap is not essential for finding the mft records, but on
 * the other hand, handling the bitmap in this special way would make life
 * easier because otherwise there might be circular invocations of functions
 * when reading the bitmap.
 */
int ntfs_mft_record_alloc(struct ntfs_volume *vol, const int mode,
			  struct ntfs_inode **ni, struct ntfs_inode *base_ni,
			  struct mft_record **ni_mrec)
{
	s64 ll, bit, old_data_initialized, old_data_size;
	unsigned long flags;
	struct folio *folio;
	struct ntfs_inode *mft_ni, *mftbmp_ni;
	struct ntfs_attr_search_ctx *ctx;
	struct mft_record *m = NULL;
	struct attr_record *a;
	pgoff_t index;
	unsigned int ofs;
	int err;
	__le16 seq_no, usn;
	bool record_formatted = false;
	unsigned int memalloc_flags;

	if (base_ni && *ni)
		return -EINVAL;

	/* @mode and @base_ni are mutually exclusive. */
	if (mode && base_ni)
		return -EINVAL;

	if (base_ni)
		ntfs_debug("Entering (allocating an extent mft record for base mft record 0x%llx).",
				(long long)base_ni->mft_no);
	else
		ntfs_debug("Entering (allocating a base mft record).");

	memalloc_flags = memalloc_nofs_save();

	mft_ni = NTFS_I(vol->mft_ino);
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		mutex_lock(&mft_ni->mrec_lock);
	mftbmp_ni = NTFS_I(vol->mftbmp_ino);
search_free_rec:
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		down_write(&vol->mftbmp_lock);
	bit = ntfs_mft_bitmap_find_and_alloc_free_rec_nolock(vol, base_ni);
	if (bit >= 0) {
		ntfs_debug("Found and allocated free record (#1), bit 0x%llx.",
				(long long)bit);
		goto have_alloc_rec;
	}
	if (bit != -ENOSPC) {
		if (!base_ni || base_ni->mft_no != FILE_MFT) {
			up_write(&vol->mftbmp_lock);
			mutex_unlock(&mft_ni->mrec_lock);
		}
		memalloc_nofs_restore(memalloc_flags);
		return bit;
	}

	if (base_ni && base_ni->mft_no == FILE_MFT) {
		memalloc_nofs_restore(memalloc_flags);
		return bit;
	}

	/*
	 * No free mft records left.  If the mft bitmap already covers more
	 * than the currently used mft records, the next records are all free,
	 * so we can simply allocate the first unused mft record.
	 * Note: We also have to make sure that the mft bitmap at least covers
	 * the first 24 mft records as they are special and whilst they may not
	 * be in use, we do not allocate from them.
	 */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ll = mft_ni->initialized_size >> vol->mft_record_size_bits;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	old_data_initialized = mftbmp_ni->initialized_size;
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	if (old_data_initialized << 3 > ll &&
	    old_data_initialized > RESERVED_MFT_RECORDS / 8) {
		bit = ll;
		if (bit < RESERVED_MFT_RECORDS)
			bit = RESERVED_MFT_RECORDS;
		if (unlikely(bit >= (1ll << 32)))
			goto max_err_out;
		ntfs_debug("Found free record (#2), bit 0x%llx.",
				(long long)bit);
		goto found_free_rec;
	}
	/*
	 * The mft bitmap needs to be expanded until it covers the first unused
	 * mft record that we can allocate.
	 * Note: The smallest mft record we allocate is mft record 24.
	 */
	bit = old_data_initialized << 3;
	if (unlikely(bit >= (1ll << 32)))
		goto max_err_out;
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	old_data_size = mftbmp_ni->allocated_size;
	ntfs_debug("Status of mftbmp before extension: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
			old_data_size, i_size_read(vol->mftbmp_ino),
			old_data_initialized);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
	if (old_data_initialized + 8 > old_data_size) {
		/* Need to extend bitmap by one more cluster. */
		ntfs_debug("mftbmp: initialized_size + 8 > allocated_size.");
		err = ntfs_mft_bitmap_extend_allocation_nolock(vol);
		if (err == -EAGAIN)
			err = ntfs_mft_bitmap_extend_allocation_nolock(vol);

		if (unlikely(err)) {
			if (!base_ni || base_ni->mft_no != FILE_MFT)
				up_write(&vol->mftbmp_lock);
			goto err_out;
		}
#ifdef DEBUG
		read_lock_irqsave(&mftbmp_ni->size_lock, flags);
		ntfs_debug("Status of mftbmp after allocation extension: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
				mftbmp_ni->allocated_size,
				i_size_read(vol->mftbmp_ino),
				mftbmp_ni->initialized_size);
		read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
	}
	/*
	 * We now have sufficient allocated space, extend the initialized_size
	 * as well as the data_size if necessary and fill the new space with
	 * zeroes.
	 */
	err = ntfs_mft_bitmap_extend_initialized_nolock(vol);
	if (unlikely(err)) {
		if (!base_ni || base_ni->mft_no != FILE_MFT)
			up_write(&vol->mftbmp_lock);
		goto err_out;
	}
#ifdef DEBUG
	read_lock_irqsave(&mftbmp_ni->size_lock, flags);
	ntfs_debug("Status of mftbmp after initialized extension: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
			mftbmp_ni->allocated_size,
			i_size_read(vol->mftbmp_ino),
			mftbmp_ni->initialized_size);
	read_unlock_irqrestore(&mftbmp_ni->size_lock, flags);
#endif /* DEBUG */
	ntfs_debug("Found free record (#3), bit 0x%llx.", (long long)bit);
found_free_rec:
	/* @bit is the found free mft record, allocate it in the mft bitmap. */
	ntfs_debug("At found_free_rec.");
	err = ntfs_bitmap_set_bit(vol->mftbmp_ino, bit);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to allocate bit in mft bitmap.");
		if (!base_ni || base_ni->mft_no != FILE_MFT)
			up_write(&vol->mftbmp_lock);
		goto err_out;
	}
	ntfs_debug("Set bit 0x%llx in mft bitmap.", (long long)bit);
have_alloc_rec:
	/*
	 * The mft bitmap is now uptodate.  Deal with mft data attribute now.
	 * Note, we keep hold of the mft bitmap lock for writing until all
	 * modifications to the mft data attribute are complete, too, as they
	 * will impact decisions for mft bitmap and mft record allocation done
	 * by a parallel allocation and if the lock is not maintained a
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
	if (!base_ni || base_ni->mft_no != FILE_MFT) {
		read_lock_irqsave(&mft_ni->size_lock, flags);
		ntfs_debug("Status of mft data before extension: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
				mft_ni->allocated_size, i_size_read(vol->mft_ino),
				mft_ni->initialized_size);
		while (ll > mft_ni->allocated_size) {
			read_unlock_irqrestore(&mft_ni->size_lock, flags);
			err = ntfs_mft_data_extend_allocation_nolock(vol);
			if (err == -EAGAIN)
				err = ntfs_mft_data_extend_allocation_nolock(vol);

			if (unlikely(err)) {
				ntfs_error(vol->sb, "Failed to extend mft data allocation.");
				goto undo_mftbmp_alloc_nolock;
			}
			read_lock_irqsave(&mft_ni->size_lock, flags);
			ntfs_debug("Status of mft data after allocation extension: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
					mft_ni->allocated_size, i_size_read(vol->mft_ino),
					mft_ni->initialized_size);
		}
		read_unlock_irqrestore(&mft_ni->size_lock, flags);
	} else if (ll > mft_ni->allocated_size) {
		err = -ENOSPC;
		goto undo_mftbmp_alloc_nolock;
	}
	/*
	 * Extend mft data initialized size (and data size of course) to reach
	 * the allocated mft record, formatting the mft records allong the way.
	 * Note: We only modify the struct ntfs_inode structure as that is all that is
	 * needed by ntfs_mft_record_format().  We will update the attribute
	 * record itself in one fell swoop later on.
	 */
	write_lock_irqsave(&mft_ni->size_lock, flags);
	old_data_initialized = mft_ni->initialized_size;
	old_data_size = vol->mft_ino->i_size;
	while (ll > mft_ni->initialized_size) {
		s64 new_initialized_size, mft_no;

		new_initialized_size = mft_ni->initialized_size +
				vol->mft_record_size;
		mft_no = mft_ni->initialized_size >> vol->mft_record_size_bits;
		if (new_initialized_size > i_size_read(vol->mft_ino))
			i_size_write(vol->mft_ino, new_initialized_size);
		write_unlock_irqrestore(&mft_ni->size_lock, flags);
		ntfs_debug("Initializing mft record 0x%llx.",
				(long long)mft_no);
		err = ntfs_mft_record_format(vol, mft_no);
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
		err = -ENOMEM;
		unmap_mft_record(mft_ni);
		goto undo_data_init;
	}
	err = ntfs_attr_lookup(mft_ni->type, mft_ni->name, mft_ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to find first attribute extent of mft data attribute.");
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(mft_ni);
		goto undo_data_init;
	}
	a = ctx->attr;
	read_lock_irqsave(&mft_ni->size_lock, flags);
	a->data.non_resident.initialized_size =
			cpu_to_le64(mft_ni->initialized_size);
	a->data.non_resident.data_size =
			cpu_to_le64(i_size_read(vol->mft_ino));
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
	/* Ensure the changes make it to disk. */
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(mft_ni);
	read_lock_irqsave(&mft_ni->size_lock, flags);
	ntfs_debug("Status of mft data after mft record initialization: allocated_size 0x%llx, data_size 0x%llx, initialized_size 0x%llx.",
			mft_ni->allocated_size,	i_size_read(vol->mft_ino),
			mft_ni->initialized_size);
	WARN_ON(i_size_read(vol->mft_ino) > mft_ni->allocated_size);
	WARN_ON(mft_ni->initialized_size > i_size_read(vol->mft_ino));
	read_unlock_irqrestore(&mft_ni->size_lock, flags);
mft_rec_already_initialized:
	/*
	 * We can finally drop the mft bitmap lock as the mft data attribute
	 * has been fully updated.  The only disparity left is that the
	 * allocated mft record still needs to be marked as in use to match the
	 * set bit in the mft bitmap but this is actually not a problem since
	 * this mft record is not referenced from anywhere yet and the fact
	 * that it is allocated in the mft bitmap means that no-one will try to
	 * allocate it either.
	 */
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		up_write(&vol->mftbmp_lock);
	/*
	 * We now have allocated and initialized the mft record.  Calculate the
	 * index of and the offset within the page cache page the record is in.
	 */
	index = NTFS_MFT_NR_TO_PIDX(vol, bit);
	ofs = NTFS_MFT_NR_TO_POFS(vol, bit);
	/* Read, map, and pin the folio containing the mft record. */
	folio = read_mapping_folio(vol->mft_ino->i_mapping, index, NULL);
	if (IS_ERR(folio)) {
		ntfs_error(vol->sb, "Failed to map page containing allocated mft record 0x%llx.",
				bit);
		err = PTR_ERR(folio);
		goto undo_mftbmp_alloc;
	}
	folio_lock(folio);
	folio_clear_uptodate(folio);
	m = (struct mft_record *)((u8 *)kmap_local_folio(folio, 0) + ofs);
	/* If we just formatted the mft record no need to do it again. */
	if (!record_formatted) {
		/* Sanity check that the mft record is really not in use. */
		if (ntfs_is_file_record(m->magic) &&
				(m->flags & MFT_RECORD_IN_USE)) {
			ntfs_warning(vol->sb,
				"Mft record 0x%llx was marked free in mft bitmap but is marked used itself. Unmount and run chkdsk.",
				bit);
			folio_mark_uptodate(folio);
			folio_unlock(folio);
			kunmap_local(m);
			folio_put(folio);
			NVolSetErrors(vol);
			goto search_free_rec;
		}
		/*
		 * We need to (re-)format the mft record, preserving the
		 * sequence number if it is not zero as well as the update
		 * sequence number if it is not zero or -1 (0xffff).  This
		 * means we do not need to care whether or not something went
		 * wrong with the previous mft record.
		 */
		seq_no = m->sequence_number;
		usn = *(__le16 *)((u8 *)m + le16_to_cpu(m->usa_ofs));
		err = ntfs_mft_record_layout(vol, bit, m);
		if (unlikely(err)) {
			ntfs_error(vol->sb, "Failed to layout allocated mft record 0x%llx.",
					bit);
			folio_mark_uptodate(folio);
			folio_unlock(folio);
			kunmap_local(m);
			folio_put(folio);
			goto undo_mftbmp_alloc;
		}
		if (seq_no)
			m->sequence_number = seq_no;
		if (usn && le16_to_cpu(usn) != 0xffff)
			*(__le16 *)((u8 *)m + le16_to_cpu(m->usa_ofs)) = usn;
		pre_write_mst_fixup((struct ntfs_record *)m, vol->mft_record_size);
	}
	/* Set the mft record itself in use. */
	m->flags |= MFT_RECORD_IN_USE;
	if (S_ISDIR(mode))
		m->flags |= MFT_RECORD_IS_DIRECTORY;
	folio_mark_uptodate(folio);
	if (base_ni) {
		struct mft_record *m_tmp;

		/*
		 * Setup the base mft record in the extent mft record.  This
		 * completes initialization of the allocated extent mft record
		 * and we can simply use it with map_extent_mft_record().
		 */
		m->base_mft_record = MK_LE_MREF(base_ni->mft_no,
				base_ni->seq_no);
		/*
		 * Allocate an extent inode structure for the new mft record,
		 * attach it to the base inode @base_ni and map, pin, and lock
		 * its, i.e. the allocated, mft record.
		 */
		m_tmp = map_extent_mft_record(base_ni,
					      MK_MREF(bit, le16_to_cpu(m->sequence_number)),
					      ni);
		if (IS_ERR(m_tmp)) {
			ntfs_error(vol->sb, "Failed to map allocated extent mft record 0x%llx.",
					bit);
			err = PTR_ERR(m_tmp);
			/* Set the mft record itself not in use. */
			m->flags &= cpu_to_le16(
					~le16_to_cpu(MFT_RECORD_IN_USE));
			/* Make sure the mft record is written out to disk. */
			   ntfs_mft_mark_dirty(folio);
			folio_unlock(folio);
			kunmap_local(m);
			folio_put(folio);
			goto undo_mftbmp_alloc;
		}

		/*
		 * Make sure the allocated mft record is written out to disk.
		 * No need to set the inode dirty because the caller is going
		 * to do that anyway after finishing with the new extent mft
		 * record (e.g. at a minimum a new attribute will be added to
		 * the mft record.
		 */
		ntfs_mft_mark_dirty(folio);
		folio_unlock(folio);
		/*
		 * Need to unmap the page since map_extent_mft_record() mapped
		 * it as well so we have it mapped twice at the moment.
		 */
		kunmap_local(m);
		folio_put(folio);
	} else {
		/*
		 * Manually map, pin, and lock the mft record as we already
		 * have its page mapped and it is very easy to do.
		 */
		(*ni)->seq_no = le16_to_cpu(m->sequence_number);
		/*
		 * Make sure the allocated mft record is written out to disk.
		 * NOTE: We do not set the ntfs inode dirty because this would
		 * fail in ntfs_write_inode() because the inode does not have a
		 * standard information attribute yet.  Also, there is no need
		 * to set the inode dirty because the caller is going to do
		 * that anyway after finishing with the new mft record (e.g. at
		 * a minimum some new attributes will be added to the mft
		 * record.
		 */

		(*ni)->mrec = kmalloc(vol->mft_record_size, GFP_NOFS);
		if (!(*ni)->mrec) {
			folio_unlock(folio);
			kunmap_local(m);
			folio_put(folio);
			goto undo_mftbmp_alloc;
		}

		memcpy((*ni)->mrec, m, vol->mft_record_size);
		post_read_mst_fixup((struct ntfs_record *)(*ni)->mrec, vol->mft_record_size);
		ntfs_mft_mark_dirty(folio);
		folio_unlock(folio);
		(*ni)->folio = folio;
		(*ni)->folio_ofs = ofs;
		atomic_inc(&(*ni)->count);
		/* Update the default mft allocation position. */
		vol->mft_data_pos = bit + 1;
	}
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		mutex_unlock(&mft_ni->mrec_lock);
	memalloc_nofs_restore(memalloc_flags);

	/*
	 * Return the opened, allocated inode of the allocated mft record as
	 * well as the mapped, pinned, and locked mft record.
	 */
	ntfs_debug("Returning opened, allocated %sinode 0x%llx.",
			base_ni ? "extent " : "", bit);
	(*ni)->mft_no = bit;
	if (ni_mrec)
		*ni_mrec = (*ni)->mrec;
	ntfs_dec_free_mft_records(vol, 1);
	return 0;
undo_data_init:
	write_lock_irqsave(&mft_ni->size_lock, flags);
	mft_ni->initialized_size = old_data_initialized;
	i_size_write(vol->mft_ino, old_data_size);
	write_unlock_irqrestore(&mft_ni->size_lock, flags);
	goto undo_mftbmp_alloc_nolock;
undo_mftbmp_alloc:
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		down_write(&vol->mftbmp_lock);
undo_mftbmp_alloc_nolock:
	if (ntfs_bitmap_clear_bit(vol->mftbmp_ino, bit)) {
		ntfs_error(vol->sb, "Failed to clear bit in mft bitmap.%s", es);
		NVolSetErrors(vol);
	}
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		up_write(&vol->mftbmp_lock);
err_out:
	if (!base_ni || base_ni->mft_no != FILE_MFT)
		mutex_unlock(&mft_ni->mrec_lock);
	memalloc_nofs_restore(memalloc_flags);
	return err;
max_err_out:
	ntfs_warning(vol->sb,
		"Cannot allocate mft record because the maximum number of inodes (2^32) has already been reached.");
	if (!base_ni || base_ni->mft_no != FILE_MFT) {
		up_write(&vol->mftbmp_lock);
		mutex_unlock(&mft_ni->mrec_lock);
	}
	memalloc_nofs_restore(memalloc_flags);
	return -ENOSPC;
}

/*
 * ntfs_mft_record_free - free an mft record on an ntfs volume
 * @vol:	volume on which to free the mft record
 * @ni:		open ntfs inode of the mft record to free
 *
 * Free the mft record of the open inode @ni on the mounted ntfs volume @vol.
 * Note that this function calls ntfs_inode_close() internally and hence you
 * cannot use the pointer @ni any more after this function returns success.
 *
 * On success return 0 and on error return -1 with errno set to the error code.
 */
int ntfs_mft_record_free(struct ntfs_volume *vol, struct ntfs_inode *ni)
{
	u64 mft_no;
	int err;
	u16 seq_no;
	__le16 old_seq_no;
	struct mft_record *ni_mrec;
	unsigned int memalloc_flags;
	struct ntfs_inode *base_ni;

	if (!vol || !ni)
		return -EINVAL;

	ntfs_debug("Entering for inode 0x%llx.\n", (long long)ni->mft_no);

	ni_mrec = map_mft_record(ni);
	if (IS_ERR(ni_mrec))
		return -EIO;

	/* Cache the mft reference for later. */
	mft_no = ni->mft_no;

	/* Mark the mft record as not in use. */
	ni_mrec->flags &= ~MFT_RECORD_IN_USE;

	/* Increment the sequence number, skipping zero, if it is not zero. */
	old_seq_no = ni_mrec->sequence_number;
	seq_no = le16_to_cpu(old_seq_no);
	if (seq_no == 0xffff)
		seq_no = 1;
	else if (seq_no)
		seq_no++;
	ni_mrec->sequence_number = cpu_to_le16(seq_no);

	down_read(&NTFS_I(vol->mft_ino)->runlist.lock);
	err = ntfs_get_block_mft_record(NTFS_I(vol->mft_ino), ni);
	up_read(&NTFS_I(vol->mft_ino)->runlist.lock);
	if (err) {
		unmap_mft_record(ni);
		return err;
	}

	/*
	 * Set the ntfs inode dirty and write it out.  We do not need to worry
	 * about the base inode here since whatever caused the extent mft
	 * record to be freed is guaranteed to do it already.
	 */
	NInoSetDirty(ni);
	err = write_mft_record(ni, ni_mrec, 0);
	if (err)
		goto sync_rollback;

	if (likely(ni->nr_extents >= 0))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;

	/* Clear the bit in the $MFT/$BITMAP corresponding to this record. */
	memalloc_flags = memalloc_nofs_save();
	if (base_ni->mft_no != FILE_MFT)
		down_write(&vol->mftbmp_lock);
	err = ntfs_bitmap_clear_bit(vol->mftbmp_ino, mft_no);
	if (base_ni->mft_no != FILE_MFT)
		up_write(&vol->mftbmp_lock);
	memalloc_nofs_restore(memalloc_flags);
	if (err)
		goto bitmap_rollback;

	unmap_mft_record(ni);
	ntfs_inc_free_mft_records(vol, 1);
	return 0;

	/* Rollback what we did... */
bitmap_rollback:
	memalloc_flags = memalloc_nofs_save();
	if (base_ni->mft_no != FILE_MFT)
		down_write(&vol->mftbmp_lock);
	if (ntfs_bitmap_set_bit(vol->mftbmp_ino, mft_no))
		ntfs_error(vol->sb, "ntfs_bitmap_set_bit failed in bitmap_rollback\n");
	if (base_ni->mft_no != FILE_MFT)
		up_write(&vol->mftbmp_lock);
	memalloc_nofs_restore(memalloc_flags);
sync_rollback:
	ntfs_error(vol->sb,
		"Eeek! Rollback failed in %s. Leaving inconsistent metadata!\n", __func__);
	ni_mrec->flags |= MFT_RECORD_IN_USE;
	ni_mrec->sequence_number = old_seq_no;
	NInoSetDirty(ni);
	write_mft_record(ni, ni_mrec, 0);
	unmap_mft_record(ni);
	return err;
}

static s64 lcn_from_index(struct ntfs_volume *vol, struct ntfs_inode *ni,
		unsigned long index)
{
	s64 vcn;
	s64 lcn;

	vcn = ntfs_pidx_to_cluster(vol, index);

	down_read(&ni->runlist.lock);
	lcn = ntfs_attr_vcn_to_lcn_nolock(ni, vcn, false);
	up_read(&ni->runlist.lock);

	return lcn;
}

/*
 * ntfs_write_mft_block - Write back a folio containing MFT records
 * @folio:	The folio to write back (contains one or more MFT records)
 * @wbc:	Writeback control structure
 *
 * This function is called as part of the address_space_operations
 * .writepages implementation for the $MFT inode (or $MFTMirr).
 * It handles writing one folio (normally 4KiB page) worth of MFT records
 * to the underlying block device.
 *
 * Return: 0 on success, or -errno on error.
 */
static int ntfs_write_mft_block(struct folio *folio, struct writeback_control *wbc)
{
	struct address_space *mapping = folio->mapping;
	struct inode *vi = mapping->host;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;
	u8 *kaddr;
	struct ntfs_inode **locked_nis __free(kfree) = kmalloc_array(PAGE_SIZE / NTFS_BLOCK_SIZE,
							sizeof(struct ntfs_inode *), GFP_NOFS);
	int nr_locked_nis = 0, err = 0, mft_ofs, prev_mft_ofs;
	struct inode **ref_inos __free(kfree) = kmalloc_array(PAGE_SIZE / NTFS_BLOCK_SIZE,
							      sizeof(struct inode *), GFP_NOFS);
	int nr_ref_inos = 0;
	struct bio *bio = NULL;
	unsigned long mft_no;
	struct ntfs_inode *tni;
	s64 lcn;
	s64 vcn = ntfs_pidx_to_cluster(vol, folio->index);
	s64 end_vcn = ntfs_bytes_to_cluster(vol, ni->allocated_size);
	unsigned int folio_sz;
	struct runlist_element *rl;
	loff_t i_size = i_size_read(vi);

	ntfs_debug("Entering for inode 0x%lx, attribute type 0x%x, folio index 0x%lx.",
			vi->i_ino, ni->type, folio->index);

	if (!locked_nis || !ref_inos)
		return -ENOMEM;

	/* We have to zero every time due to mmap-at-end-of-file. */
	if (folio->index >= (i_size >> folio_shift(folio)))
		/* The page straddles i_size. */
		folio_zero_segment(folio,
				   offset_in_folio(folio, i_size),
				   folio_size(folio));

	lcn = lcn_from_index(vol, ni, folio->index);
	if (lcn <= LCN_HOLE) {
		folio_start_writeback(folio);
		folio_unlock(folio);
		folio_end_writeback(folio);
		return -EIO;
	}

	/* Map folio so we can access its contents. */
	kaddr = kmap_local_folio(folio, 0);
	/* Clear the page uptodate flag whilst the mst fixups are applied. */
	folio_clear_uptodate(folio);

	for (mft_ofs = 0; mft_ofs < PAGE_SIZE && vcn < end_vcn;
	     mft_ofs += vol->mft_record_size) {
		/* Get the mft record number. */
		mft_no = (((s64)folio->index << PAGE_SHIFT) + mft_ofs) >>
			vol->mft_record_size_bits;
		vcn = ntfs_mft_no_to_cluster(vol, mft_no);
		/* Check whether to write this mft record. */
		tni = NULL;
		if (ntfs_may_write_mft_record(vol, mft_no,
					(struct mft_record *)(kaddr + mft_ofs),
					&tni, &ref_inos[nr_ref_inos])) {
			unsigned int mft_record_off = 0;
			s64 vcn_off = vcn;

			/*
			 * Skip $MFT extent mft records and let them being written
			 * by writeback to avioid deadlocks. the $MFT runlist
			 * lock must be taken before $MFT extent mrec_lock is taken.
			 */
			if (tni && tni->nr_extents < 0 &&
				tni->ext.base_ntfs_ino == NTFS_I(vol->mft_ino)) {
				mutex_unlock(&tni->mrec_lock);
				atomic_dec(&tni->count);
				iput(vol->mft_ino);
				continue;
			}

			/*
			 * The record should be written.  If a locked ntfs
			 * inode was returned, add it to the array of locked
			 * ntfs inodes.
			 */
			if (tni)
				locked_nis[nr_locked_nis++] = tni;
			else if (ref_inos[nr_ref_inos])
				nr_ref_inos++;

			if (bio && (mft_ofs != prev_mft_ofs + vol->mft_record_size)) {
flush_bio:
				bio->bi_end_io = ntfs_bio_end_io;
				submit_bio(bio);
				bio = NULL;
			}

			if (vol->cluster_size < folio_size(folio)) {
				down_write(&ni->runlist.lock);
				rl = ntfs_attr_vcn_to_rl(ni, vcn_off, &lcn);
				up_write(&ni->runlist.lock);
				if (IS_ERR(rl) || lcn < 0) {
					err = -EIO;
					goto unm_done;
				}

				if (bio &&
				   (bio_end_sector(bio) >> (vol->cluster_size_bits - 9)) !=
				    lcn) {
					bio->bi_end_io = ntfs_bio_end_io;
					submit_bio(bio);
					bio = NULL;
				}
			}

			if (!bio) {
				unsigned int off;

				off = ((mft_no << vol->mft_record_size_bits) +
				       mft_record_off) & vol->cluster_size_mask;

				bio = bio_alloc(vol->sb->s_bdev, 1, REQ_OP_WRITE,
						GFP_NOIO);
				bio->bi_iter.bi_sector =
					ntfs_bytes_to_sector(vol,
							ntfs_cluster_to_bytes(vol, lcn) + off);
			}

			if (vol->cluster_size == NTFS_BLOCK_SIZE &&
			    (mft_record_off ||
			     rl->length - (vcn_off - rl->vcn) == 1 ||
			     mft_ofs + NTFS_BLOCK_SIZE >= PAGE_SIZE))
				folio_sz = NTFS_BLOCK_SIZE;
			else
				folio_sz = vol->mft_record_size;
			if (!bio_add_folio(bio, folio, folio_sz,
					   mft_ofs + mft_record_off)) {
				err = -EIO;
				bio_put(bio);
				goto unm_done;
			}
			mft_record_off += folio_sz;

			if (mft_record_off != vol->mft_record_size) {
				vcn_off++;
				goto flush_bio;
			}
			prev_mft_ofs = mft_ofs;

			if (mft_no < vol->mftmirr_size)
				ntfs_sync_mft_mirror(vol, mft_no,
						(struct mft_record *)(kaddr + mft_ofs));
		} else if (ref_inos[nr_ref_inos])
			nr_ref_inos++;
	}

	if (bio) {
		bio->bi_end_io = ntfs_bio_end_io;
		submit_bio(bio);
	}
unm_done:
	folio_mark_uptodate(folio);
	kunmap_local(kaddr);

	folio_start_writeback(folio);
	folio_unlock(folio);
	folio_end_writeback(folio);

	/* Unlock any locked inodes. */
	while (nr_locked_nis-- > 0) {
		struct ntfs_inode *base_tni;

		tni = locked_nis[nr_locked_nis];
		mutex_unlock(&tni->mrec_lock);

		/* Get the base inode. */
		mutex_lock(&tni->extent_lock);
		if (tni->nr_extents >= 0)
			base_tni = tni;
		else
			base_tni = tni->ext.base_ntfs_ino;
		mutex_unlock(&tni->extent_lock);
		ntfs_debug("Unlocking %s inode 0x%lx.",
				tni == base_tni ? "base" : "extent",
				tni->mft_no);
		atomic_dec(&tni->count);
		iput(VFS_I(base_tni));
	}

	/* Dropping deferred references */
	while (nr_ref_inos-- > 0) {
		if (ref_inos[nr_ref_inos])
			iput(ref_inos[nr_ref_inos]);
	}

	if (unlikely(err && err != -ENOMEM))
		NVolSetErrors(vol);
	if (likely(!err))
		ntfs_debug("Done.");
	return err;
}

/*
 * ntfs_mft_writepages - Write back dirty folios for the $MFT inode
 * @mapping:	address space of the $MFT inode
 * @wbc:	writeback control
 *
 * Writeback iterator for MFT records. Iterates over dirty folios and
 * delegates actual writing to ntfs_write_mft_block() for each folio.
 * Called from the address_space_operations .writepages vector of the
 * $MFT inode.
 *
 * Returns 0 on success, or the first error encountered.
 */
int ntfs_mft_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct folio *folio = NULL;
	int error;

	if (NVolShutdown(NTFS_I(mapping->host)->vol))
		return -EIO;

	while ((folio = writeback_iter(mapping, wbc, folio, &error)))
		error = ntfs_write_mft_block(folio, wbc);
	return error;
}

void ntfs_mft_mark_dirty(struct folio *folio)
{
	iomap_dirty_folio(folio->mapping, folio);
}
