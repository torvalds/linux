// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cluster (de)allocation code.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 *
 * Part of this file is based on code from the NTFS-3G.
 * and is copyrighted by the respective authors below:
 * Copyright (c) 2002-2004 Anton Altaparmakov
 * Copyright (c) 2004 Yura Pakhuchiy
 * Copyright (c) 2004-2008 Szabolcs Szakacsits
 * Copyright (c) 2008-2009 Jean-Pierre Andre
 */

#include <linux/blkdev.h>

#include "lcnalloc.h"
#include "bitmap.h"
#include "ntfs.h"

/*
 * ntfs_cluster_free_from_rl_nolock - free clusters from runlist
 * @vol:	mounted ntfs volume on which to free the clusters
 * @rl:		runlist describing the clusters to free
 *
 * Free all the clusters described by the runlist @rl on the volume @vol.  In
 * the case of an error being returned, at least some of the clusters were not
 * freed.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: - The volume lcn bitmap must be locked for writing on entry and is
 *	      left locked on return.
 */
int ntfs_cluster_free_from_rl_nolock(struct ntfs_volume *vol,
		const struct runlist_element *rl)
{
	struct inode *lcnbmp_vi = vol->lcnbmp_ino;
	int ret = 0;
	s64 nr_freed = 0;

	ntfs_debug("Entering.");
	if (!rl)
		return 0;

	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	for (; rl->length; rl++) {
		int err;

		if (rl->lcn < 0)
			continue;
		err = ntfs_bitmap_clear_run(lcnbmp_vi, rl->lcn, rl->length);
		if (unlikely(err && (!ret || ret == -ENOMEM) && ret != err))
			ret = err;
		else
			nr_freed += rl->length;
	}
	ntfs_inc_free_clusters(vol, nr_freed);
	ntfs_debug("Done.");
	return ret;
}

static s64 max_empty_bit_range(unsigned char *buf, int size)
{
	int i, j, run = 0;
	int max_range = 0;
	s64 start_pos = -1;

	ntfs_debug("Entering\n");

	i = 0;
	while (i < size) {
		switch (*buf) {
		case 0:
			do {
				buf++;
				run += 8;
				i++;
			} while ((i < size) && !*buf);
			break;
		case 255:
			if (run > max_range) {
				max_range = run;
				start_pos = (s64)i * 8 - run;
			}
			run = 0;
			do {
				buf++;
				i++;
			} while ((i < size) && (*buf == 255));
			break;
		default:
			for (j = 0; j < 8; j++) {
				int bit = *buf & (1 << j);

				if (bit) {
					if (run > max_range) {
						max_range = run;
						start_pos = (s64)i * 8 + (j - run);
					}
					run = 0;
				} else
					run++;
			}
			i++;
			buf++;
		}
	}

	if (run > max_range)
		start_pos = (s64)i * 8 - run;

	return start_pos;
}

/*
 * ntfs_cluster_alloc - allocate clusters on an ntfs volume
 * @vol:		mounted ntfs volume on which to allocate clusters
 * @start_vcn:		vcn of the first allocated cluster
 * @count:		number of clusters to allocate
 * @start_lcn:		starting lcn at which to allocate the clusters or -1 if none
 * @zone:		zone from which to allocate (MFT_ZONE or DATA_ZONE)
 * @is_extension:	if true, the caller is extending an attribute
 * @is_contig:		if true, require contiguous allocation
 * @is_dealloc:		if true, the allocation is for deallocation purposes
 *
 * Allocate @count clusters preferably starting at cluster @start_lcn or at the
 * current allocator position if @start_lcn is -1, on the mounted ntfs volume
 * @vol. @zone is either DATA_ZONE for allocation of normal clusters or
 * MFT_ZONE for allocation of clusters for the master file table, i.e. the
 * $MFT/$DATA attribute.
 *
 * @start_vcn specifies the vcn of the first allocated cluster.  This makes
 * merging the resulting runlist with the old runlist easier.
 *
 * If @is_extension is 'true', the caller is allocating clusters to extend an
 * attribute and if it is 'false', the caller is allocating clusters to fill a
 * hole in an attribute.  Practically the difference is that if @is_extension
 * is 'true' the returned runlist will be terminated with LCN_ENOENT and if
 * @is_extension is 'false' the runlist will be terminated with
 * LCN_RL_NOT_MAPPED.
 *
 * You need to check the return value with IS_ERR().  If this is false, the
 * function was successful and the return value is a runlist describing the
 * allocated cluster(s).  If IS_ERR() is true, the function failed and
 * PTR_ERR() gives you the error code.
 *
 * Notes on the allocation algorithm
 * =================================
 *
 * There are two data zones.  First is the area between the end of the mft zone
 * and the end of the volume, and second is the area between the start of the
 * volume and the start of the mft zone.  On unmodified/standard NTFS 1.x
 * volumes, the second data zone does not exist due to the mft zone being
 * expanded to cover the start of the volume in order to reserve space for the
 * mft bitmap attribute.
 *
 * This is not the prettiest function but the complexity stems from the need of
 * implementing the mft vs data zoned approach and from the fact that we have
 * access to the lcn bitmap in portions of up to 8192 bytes at a time, so we
 * need to cope with crossing over boundaries of two buffers.  Further, the
 * fact that the allocator allows for caller supplied hints as to the location
 * of where allocation should begin and the fact that the allocator keeps track
 * of where in the data zones the next natural allocation should occur,
 * contribute to the complexity of the function.  But it should all be
 * worthwhile, because this allocator should: 1) be a full implementation of
 * the MFT zone approach used by Windows NT, 2) cause reduction in
 * fragmentation, and 3) be speedy in allocations (the code is not optimized
 * for speed, but the algorithm is, so further speed improvements are probably
 * possible).
 *
 * Locking: - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 *
 * Return: Runlist describing the allocated cluster(s) on success, error pointer
 *         on failure.
 */
struct runlist_element *ntfs_cluster_alloc(struct ntfs_volume *vol, const s64 start_vcn,
		const s64 count, const s64 start_lcn,
		const int zone,
		const bool is_extension,
		const bool is_contig,
		const bool is_dealloc)
{
	s64 zone_start, zone_end, bmp_pos, bmp_initial_pos, last_read_pos, lcn;
	s64 prev_lcn = 0, prev_run_len = 0, mft_zone_size;
	s64 clusters, free_clusters;
	loff_t i_size;
	struct inode *lcnbmp_vi;
	struct runlist_element *rl = NULL;
	struct address_space *mapping;
	struct folio *folio = NULL;
	u8 *buf = NULL, *byte;
	int err = 0, rlpos, rlsize, buf_size, pg_off;
	u8 pass, done_zones, search_zone, need_writeback = 0, bit;
	unsigned int memalloc_flags;
	u8 has_guess, used_zone_pos;
	pgoff_t index;

	ntfs_debug("Entering for start_vcn 0x%llx, count 0x%llx, start_lcn 0x%llx, zone %s_ZONE.",
			start_vcn, count, start_lcn,
			zone == MFT_ZONE ? "MFT" : "DATA");

	lcnbmp_vi = vol->lcnbmp_ino;
	if (start_vcn < 0 || start_lcn < LCN_HOLE ||
	    zone < FIRST_ZONE || zone > LAST_ZONE)
		return ERR_PTR(-EINVAL);

	/* Return NULL if @count is zero. */
	if (count < 0 || !count)
		return ERR_PTR(-EINVAL);

	memalloc_flags = memalloc_nofs_save();

	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));
	free_clusters = atomic64_read(&vol->free_clusters);

	/* Take the lcnbmp lock for writing. */
	down_write(&vol->lcnbmp_lock);
	if (is_dealloc == false)
		free_clusters -= atomic64_read(&vol->dirty_clusters);

	if (free_clusters < count) {
		err = -ENOSPC;
		goto out_restore;
	}

	/*
	 * If no specific @start_lcn was requested, use the current data zone
	 * position, otherwise use the requested @start_lcn but make sure it
	 * lies outside the mft zone.  Also set done_zones to 0 (no zones done)
	 * and pass depending on whether we are starting inside a zone (1) or
	 * at the beginning of a zone (2).  If requesting from the MFT_ZONE,
	 * we either start at the current position within the mft zone or at
	 * the specified position.  If the latter is out of bounds then we start
	 * at the beginning of the MFT_ZONE.
	 */
	done_zones = 0;
	pass = 1;
	/*
	 * zone_start and zone_end are the current search range.  search_zone
	 * is 1 for mft zone, 2 for data zone 1 (end of mft zone till end of
	 * volume) and 4 for data zone 2 (start of volume till start of mft
	 * zone).
	 */
	has_guess = 1;
	zone_start = start_lcn;

	if (zone_start < 0) {
		if (zone == DATA_ZONE)
			zone_start = vol->data1_zone_pos;
		else
			zone_start = vol->mft_zone_pos;
		if (!zone_start) {
			/*
			 * Zone starts at beginning of volume which means a
			 * single pass is sufficient.
			 */
			pass = 2;
		}
		has_guess = 0;
	}

	used_zone_pos = has_guess ? 0 : 1;

	if (!zone_start || zone_start == vol->mft_zone_start ||
			zone_start == vol->mft_zone_end)
		pass = 2;

	if (zone_start < vol->mft_zone_start) {
		zone_end = vol->mft_zone_start;
		search_zone = 4;
		/* Skip searching the mft zone. */
		done_zones |= 1;
	} else if (zone_start < vol->mft_zone_end) {
		zone_end = vol->mft_zone_end;
		search_zone = 1;
	} else {
		zone_end = vol->nr_clusters;
		search_zone = 2;
		/* Skip searching the mft zone. */
		done_zones |= 1;
	}

	/*
	 * bmp_pos is the current bit position inside the bitmap.  We use
	 * bmp_initial_pos to determine whether or not to do a zone switch.
	 */
	bmp_pos = bmp_initial_pos = zone_start;

	/* Loop until all clusters are allocated, i.e. clusters == 0. */
	clusters = count;
	rlpos = rlsize = 0;
	mapping = lcnbmp_vi->i_mapping;
	i_size = i_size_read(lcnbmp_vi);
	while (1) {
		ntfs_debug("Start of outer while loop: done_zones 0x%x, search_zone %i, pass %i, zone_start 0x%llx, zone_end 0x%llx, bmp_initial_pos 0x%llx, bmp_pos 0x%llx, rlpos %i, rlsize %i.",
				done_zones, search_zone, pass,
				zone_start, zone_end, bmp_initial_pos,
				bmp_pos, rlpos, rlsize);
		/* Loop until we run out of free clusters. */
		last_read_pos = bmp_pos >> 3;
		ntfs_debug("last_read_pos 0x%llx.", last_read_pos);
		if (last_read_pos >= i_size) {
			ntfs_debug("End of attribute reached. Skipping to zone_pass_done.");
			goto zone_pass_done;
		}
		if (likely(folio)) {
			if (need_writeback) {
				ntfs_debug("Marking page dirty.");
				folio_mark_dirty(folio);
				need_writeback = 0;
			}
			folio_unlock(folio);
			kunmap_local(buf);
			folio_put(folio);
			folio = NULL;
		}

		index = last_read_pos >> PAGE_SHIFT;
		pg_off = last_read_pos & ~PAGE_MASK;
		buf_size = PAGE_SIZE - pg_off;
		if (unlikely(last_read_pos + buf_size > i_size))
			buf_size = i_size - last_read_pos;
		buf_size <<= 3;
		lcn = bmp_pos & 7;
		bmp_pos &= ~(s64)7;

		if (vol->lcn_empty_bits_per_page[index] == 0)
			goto next_bmp_pos;

		folio = read_mapping_folio(mapping, index, NULL);
		if (IS_ERR(folio)) {
			err = PTR_ERR(folio);
			ntfs_error(vol->sb, "Failed to map page.");
			goto out;
		}

		folio_lock(folio);
		buf = kmap_local_folio(folio, 0) + pg_off;
		ntfs_debug("Before inner while loop: buf_size %i, lcn 0x%llx, bmp_pos 0x%llx, need_writeback %i.",
				buf_size, lcn, bmp_pos, need_writeback);
		while (lcn < buf_size && lcn + bmp_pos < zone_end) {
			byte = buf + (lcn >> 3);
			ntfs_debug("In inner while loop: buf_size %i, lcn 0x%llx, bmp_pos 0x%llx, need_writeback %i, byte ofs 0x%x, *byte 0x%x.",
					buf_size, lcn, bmp_pos, need_writeback,
					(unsigned int)(lcn >> 3),
					(unsigned int)*byte);
			bit = 1 << (lcn & 7);
			ntfs_debug("bit 0x%x.", bit);

			if (has_guess) {
				if (*byte & bit) {
					if (is_contig == true && prev_run_len > 0)
						goto done;

					has_guess = 0;
					break;
				}
			} else {
				lcn = max_empty_bit_range(buf, buf_size >> 3);
				if (lcn < 0)
					break;
				has_guess = 1;
				continue;
			}
			/*
			 * Allocate more memory if needed, including space for
			 * the terminator element.
			 * kvzalloc() operates on whole pages only.
			 */
			if ((rlpos + 2) * sizeof(*rl) > rlsize) {
				struct runlist_element *rl2;

				ntfs_debug("Reallocating memory.");
				if (!rl)
					ntfs_debug("First free bit is at s64 0x%llx.",
							lcn + bmp_pos);
				rl2 = kvzalloc(rlsize + PAGE_SIZE, GFP_NOFS);
				if (unlikely(!rl2)) {
					err = -ENOMEM;
					ntfs_error(vol->sb, "Failed to allocate memory.");
					goto out;
				}
				memcpy(rl2, rl, rlsize);
				kvfree(rl);
				rl = rl2;
				rlsize += PAGE_SIZE;
				ntfs_debug("Reallocated memory, rlsize 0x%x.",
						rlsize);
			}
			/* Allocate the bitmap bit. */
			*byte |= bit;
			/* We need to write this bitmap page to disk. */
			need_writeback = 1;
			ntfs_debug("*byte 0x%x, need_writeback is set.",
					(unsigned int)*byte);
			ntfs_dec_free_clusters(vol, 1);
			ntfs_set_lcn_empty_bits(vol, index, 1, 1);

			/*
			 * Coalesce with previous run if adjacent LCNs.
			 * Otherwise, append a new run.
			 */
			ntfs_debug("Adding run (lcn 0x%llx, len 0x%llx), prev_lcn 0x%llx, lcn 0x%llx, bmp_pos 0x%llx, prev_run_len 0x%llx, rlpos %i.",
					lcn + bmp_pos, 1ULL, prev_lcn,
					lcn, bmp_pos, prev_run_len, rlpos);
			if (prev_lcn == lcn + bmp_pos - prev_run_len && rlpos) {
				ntfs_debug("Coalescing to run (lcn 0x%llx, len 0x%llx).",
						rl[rlpos - 1].lcn,
						rl[rlpos - 1].length);
				rl[rlpos - 1].length = ++prev_run_len;
				ntfs_debug("Run now (lcn 0x%llx, len 0x%llx), prev_run_len 0x%llx.",
						rl[rlpos - 1].lcn,
						rl[rlpos - 1].length,
						prev_run_len);
			} else {
				if (likely(rlpos)) {
					ntfs_debug("Adding new run, (previous run lcn 0x%llx, len 0x%llx).",
							rl[rlpos - 1].lcn, rl[rlpos - 1].length);
					rl[rlpos].vcn = rl[rlpos - 1].vcn +
							prev_run_len;
				} else {
					ntfs_debug("Adding new run, is first run.");
					rl[rlpos].vcn = start_vcn;
				}
				rl[rlpos].lcn = prev_lcn = lcn + bmp_pos;
				rl[rlpos].length = prev_run_len = 1;
				rlpos++;
			}
			/* Done? */
			if (!--clusters) {
				s64 tc;
done:
				if (!used_zone_pos)
					goto out;
				/*
				 * Update the current zone position.  Positions
				 * of already scanned zones have been updated
				 * during the respective zone switches.
				 */
				tc = lcn + bmp_pos + 1;
				ntfs_debug("Done. Updating current zone position, tc 0x%llx, search_zone %i.",
						tc, search_zone);
				switch (search_zone) {
				case 1:
					ntfs_debug("Before checks, vol->mft_zone_pos 0x%llx.",
							vol->mft_zone_pos);
					if (tc >= vol->mft_zone_end) {
						vol->mft_zone_pos =
								vol->mft_lcn;
						if (!vol->mft_zone_end)
							vol->mft_zone_pos = 0;
					} else if ((bmp_initial_pos >=
							vol->mft_zone_pos ||
							tc > vol->mft_zone_pos)
							&& tc >= vol->mft_lcn)
						vol->mft_zone_pos = tc;
					ntfs_debug("After checks, vol->mft_zone_pos 0x%llx.",
							vol->mft_zone_pos);
					break;
				case 2:
					ntfs_debug("Before checks, vol->data1_zone_pos 0x%llx.",
							vol->data1_zone_pos);
					if (tc >= vol->nr_clusters)
						vol->data1_zone_pos =
							     vol->mft_zone_end;
					else if ((bmp_initial_pos >=
						    vol->data1_zone_pos ||
						    tc > vol->data1_zone_pos)
						    && tc >= vol->mft_zone_end)
						vol->data1_zone_pos = tc;
					ntfs_debug("After checks, vol->data1_zone_pos 0x%llx.",
							vol->data1_zone_pos);
					break;
				case 4:
					ntfs_debug("Before checks, vol->data2_zone_pos 0x%llx.",
							vol->data2_zone_pos);
					if (tc >= vol->mft_zone_start)
						vol->data2_zone_pos = 0;
					else if (bmp_initial_pos >=
						      vol->data2_zone_pos ||
						      tc > vol->data2_zone_pos)
						vol->data2_zone_pos = tc;
					ntfs_debug("After checks, vol->data2_zone_pos 0x%llx.",
							vol->data2_zone_pos);
					break;
				default:
					WARN_ON(1);
				}
				ntfs_debug("Finished.  Going to out.");
				goto out;
			}
			lcn++;
		}

		if (!used_zone_pos) {
			used_zone_pos = 1;
			if (search_zone == 1)
				zone_start = vol->mft_zone_pos;
			else if (search_zone == 2)
				zone_start = vol->data1_zone_pos;
			else
				zone_start = vol->data2_zone_pos;

			if (!zone_start || zone_start == vol->mft_zone_start ||
			    zone_start == vol->mft_zone_end)
				pass = 2;
			bmp_pos = zone_start;
		} else {
next_bmp_pos:
			bmp_pos += buf_size;
		}

		ntfs_debug("After inner while loop: buf_size 0x%x, lcn 0x%llx, bmp_pos 0x%llx, need_writeback %i.",
				buf_size, lcn, bmp_pos, need_writeback);
		if (bmp_pos < zone_end) {
			ntfs_debug("Continuing outer while loop, bmp_pos 0x%llx, zone_end 0x%llx.",
					bmp_pos, zone_end);
			continue;
		}
zone_pass_done:	/* Finished with the current zone pass. */
		ntfs_debug("At zone_pass_done, pass %i.", pass);
		if (pass == 1) {
			/*
			 * Now do pass 2, scanning the first part of the zone
			 * we omitted in pass 1.
			 */
			pass = 2;
			zone_end = zone_start;
			switch (search_zone) {
			case 1: /* mft_zone */
				zone_start = vol->mft_zone_start;
				break;
			case 2: /* data1_zone */
				zone_start = vol->mft_zone_end;
				break;
			case 4: /* data2_zone */
				zone_start = 0;
				break;
			default:
				WARN_ON(1);
			}
			/* Sanity check. */
			if (zone_end < zone_start)
				zone_end = zone_start;
			bmp_pos = zone_start;
			ntfs_debug("Continuing outer while loop, pass 2, zone_start 0x%llx, zone_end 0x%llx, bmp_pos 0x%llx.",
					zone_start, zone_end, bmp_pos);
			continue;
		} /* pass == 2 */
done_zones_check:
		ntfs_debug("At done_zones_check, search_zone %i, done_zones before 0x%x, done_zones after 0x%x.",
				search_zone, done_zones,
				done_zones | search_zone);
		done_zones |= search_zone;
		if (done_zones < 7) {
			ntfs_debug("Switching zone.");
			/* Now switch to the next zone we haven't done yet. */
			pass = 1;
			switch (search_zone) {
			case 1:
				ntfs_debug("Switching from mft zone to data1 zone.");
				/* Update mft zone position. */
				if (rlpos && used_zone_pos) {
					s64 tc;

					ntfs_debug("Before checks, vol->mft_zone_pos 0x%llx.",
							vol->mft_zone_pos);
					tc = rl[rlpos - 1].lcn +
							rl[rlpos - 1].length;
					if (tc >= vol->mft_zone_end) {
						vol->mft_zone_pos =
								vol->mft_lcn;
						if (!vol->mft_zone_end)
							vol->mft_zone_pos = 0;
					} else if ((bmp_initial_pos >=
							vol->mft_zone_pos ||
							tc > vol->mft_zone_pos)
							&& tc >= vol->mft_lcn)
						vol->mft_zone_pos = tc;
					ntfs_debug("After checks, vol->mft_zone_pos 0x%llx.",
							vol->mft_zone_pos);
				}
				/* Switch from mft zone to data1 zone. */
switch_to_data1_zone:		search_zone = 2;
				zone_start = bmp_initial_pos =
						vol->data1_zone_pos;
				zone_end = vol->nr_clusters;
				if (zone_start == vol->mft_zone_end)
					pass = 2;
				if (zone_start >= zone_end) {
					vol->data1_zone_pos = zone_start =
							vol->mft_zone_end;
					pass = 2;
				}
				break;
			case 2:
				ntfs_debug("Switching from data1 zone to data2 zone.");
				/* Update data1 zone position. */
				if (rlpos && used_zone_pos) {
					s64 tc;

					ntfs_debug("Before checks, vol->data1_zone_pos 0x%llx.",
							vol->data1_zone_pos);
					tc = rl[rlpos - 1].lcn +
							rl[rlpos - 1].length;
					if (tc >= vol->nr_clusters)
						vol->data1_zone_pos =
							     vol->mft_zone_end;
					else if ((bmp_initial_pos >=
						    vol->data1_zone_pos ||
						    tc > vol->data1_zone_pos)
						    && tc >= vol->mft_zone_end)
						vol->data1_zone_pos = tc;
					ntfs_debug("After checks, vol->data1_zone_pos 0x%llx.",
							vol->data1_zone_pos);
				}
				/* Switch from data1 zone to data2 zone. */
				search_zone = 4;
				zone_start = bmp_initial_pos =
						vol->data2_zone_pos;
				zone_end = vol->mft_zone_start;
				if (!zone_start)
					pass = 2;
				if (zone_start >= zone_end) {
					vol->data2_zone_pos = zone_start =
							bmp_initial_pos = 0;
					pass = 2;
				}
				break;
			case 4:
				ntfs_debug("Switching from data2 zone to data1 zone.");
				/* Update data2 zone position. */
				if (rlpos && used_zone_pos) {
					s64 tc;

					ntfs_debug("Before checks, vol->data2_zone_pos 0x%llx.",
							vol->data2_zone_pos);
					tc = rl[rlpos - 1].lcn +
							rl[rlpos - 1].length;
					if (tc >= vol->mft_zone_start)
						vol->data2_zone_pos = 0;
					else if (bmp_initial_pos >=
						      vol->data2_zone_pos ||
						      tc > vol->data2_zone_pos)
						vol->data2_zone_pos = tc;
					ntfs_debug("After checks, vol->data2_zone_pos 0x%llx.",
							vol->data2_zone_pos);
				}
				/* Switch from data2 zone to data1 zone. */
				goto switch_to_data1_zone;
			default:
				WARN_ON(1);
			}
			ntfs_debug("After zone switch, search_zone %i, pass %i, bmp_initial_pos 0x%llx, zone_start 0x%llx, zone_end 0x%llx.",
					search_zone, pass,
					bmp_initial_pos,
					zone_start,
					zone_end);
			bmp_pos = zone_start;
			if (zone_start == zone_end) {
				ntfs_debug("Empty zone, going to done_zones_check.");
				/* Empty zone. Don't bother searching it. */
				goto done_zones_check;
			}
			ntfs_debug("Continuing outer while loop.");
			continue;
		} /* done_zones == 7 */
		ntfs_debug("All zones are finished.");
		/*
		 * All zones are finished!  If DATA_ZONE, shrink mft zone.  If
		 * MFT_ZONE, we have really run out of space.
		 */
		mft_zone_size = vol->mft_zone_end - vol->mft_zone_start;
		ntfs_debug("vol->mft_zone_start 0x%llx, vol->mft_zone_end 0x%llx, mft_zone_size 0x%llx.",
				vol->mft_zone_start, vol->mft_zone_end,
				mft_zone_size);
		if (zone == MFT_ZONE || mft_zone_size <= 0) {
			ntfs_debug("No free clusters left, going to out.");
			/* Really no more space left on device. */
			err = -ENOSPC;
			goto out;
		} /* zone == DATA_ZONE && mft_zone_size > 0 */
		ntfs_debug("Shrinking mft zone.");
		zone_end = vol->mft_zone_end;
		mft_zone_size >>= 1;
		if (mft_zone_size > 0)
			vol->mft_zone_end = vol->mft_zone_start + mft_zone_size;
		else /* mft zone and data2 zone no longer exist. */
			vol->data2_zone_pos = vol->mft_zone_start =
					vol->mft_zone_end = 0;
		if (vol->mft_zone_pos >= vol->mft_zone_end) {
			vol->mft_zone_pos = vol->mft_lcn;
			if (!vol->mft_zone_end)
				vol->mft_zone_pos = 0;
		}
		bmp_pos = zone_start = bmp_initial_pos =
				vol->data1_zone_pos = vol->mft_zone_end;
		search_zone = 2;
		pass = 2;
		done_zones &= ~2;
		ntfs_debug("After shrinking mft zone, mft_zone_size 0x%llx, vol->mft_zone_start 0x%llx, vol->mft_zone_end 0x%llx, vol->mft_zone_pos 0x%llx, search_zone 2, pass 2, dones_zones 0x%x, zone_start 0x%llx, zone_end 0x%llx, vol->data1_zone_pos 0x%llx, continuing outer while loop.",
				mft_zone_size, vol->mft_zone_start,
				vol->mft_zone_end, vol->mft_zone_pos,
				done_zones, zone_start, zone_end,
				vol->data1_zone_pos);
	}
	ntfs_debug("After outer while loop.");
out:
	ntfs_debug("At out.");
	/* Add runlist terminator element. */
	if (likely(rl)) {
		rl[rlpos].vcn = rl[rlpos - 1].vcn + rl[rlpos - 1].length;
		rl[rlpos].lcn = is_extension ? LCN_ENOENT : LCN_RL_NOT_MAPPED;
		rl[rlpos].length = 0;
	}
	if (likely(folio && !IS_ERR(folio))) {
		if (need_writeback) {
			ntfs_debug("Marking page dirty.");
			folio_mark_dirty(folio);
			need_writeback = 0;
		}
		folio_unlock(folio);
		kunmap_local(buf);
		folio_put(folio);
	}
	if (likely(!err)) {
		if (is_dealloc == true)
			ntfs_release_dirty_clusters(vol, rl->length);
		ntfs_debug("Done.");
		if (rl == NULL)
			err = -EIO;
		goto out_restore;
	}
	if (err != -ENOSPC)
		ntfs_error(vol->sb,
			"Failed to allocate clusters, aborting (error %i).",
			err);
	if (rl) {
		int err2;

		if (err == -ENOSPC)
			ntfs_debug("Not enough space to complete allocation, err -ENOSPC, first free lcn 0x%llx, could allocate up to 0x%llx clusters.",
					rl[0].lcn, count - clusters);
		/* Deallocate all allocated clusters. */
		ntfs_debug("Attempting rollback...");
		err2 = ntfs_cluster_free_from_rl_nolock(vol, rl);
		if (err2) {
			ntfs_error(vol->sb,
				"Failed to rollback (error %i). Leaving inconsistent metadata! Unmount and run chkdsk.",
				err2);
			NVolSetErrors(vol);
		}
		/* Free the runlist. */
		kvfree(rl);
	} else if (err == -ENOSPC)
		ntfs_debug("No space left at all, err = -ENOSPC, first free lcn = 0x%llx.",
				vol->data1_zone_pos);
	atomic64_set(&vol->dirty_clusters, 0);

out_restore:
	up_write(&vol->lcnbmp_lock);
	memalloc_nofs_restore(memalloc_flags);

	return err < 0 ? ERR_PTR(err) : rl;
}

/*
 * __ntfs_cluster_free - free clusters on an ntfs volume
 * @ni:		ntfs inode whose runlist describes the clusters to free
 * @start_vcn:	vcn in the runlist of @ni at which to start freeing clusters
 * @count:	number of clusters to free or -1 for all clusters
 * @ctx:	active attribute search context if present or NULL if not
 * @is_rollback:	true if this is a rollback operation
 *
 * Free @count clusters starting at the cluster @start_vcn in the runlist
 * described by the vfs inode @ni.
 *
 * If @count is -1, all clusters from @start_vcn to the end of the runlist are
 * deallocated.  Thus, to completely free all clusters in a runlist, use
 * @start_vcn = 0 and @count = -1.
 *
 * If @ctx is specified, it is an active search context of @ni and its base mft
 * record.  This is needed when __ntfs_cluster_free() encounters unmapped
 * runlist fragments and allows their mapping.  If you do not have the mft
 * record mapped, you can specify @ctx as NULL and __ntfs_cluster_free() will
 * perform the necessary mapping and unmapping.
 *
 * Note, __ntfs_cluster_free() saves the state of @ctx on entry and restores it
 * before returning.  Thus, @ctx will be left pointing to the same attribute on
 * return as on entry.  However, the actual pointers in @ctx may point to
 * different memory locations on return, so you must remember to reset any
 * cached pointers from the @ctx, i.e. after the call to __ntfs_cluster_free(),
 * you will probably want to do:
 *	m = ctx->mrec;
 *	a = ctx->attr;
 * Assuming you cache ctx->attr in a variable @a of type attr_record * and that
 * you cache ctx->mrec in a variable @m of type struct mft_record *.
 *
 * @is_rollback should always be 'false', it is for internal use to rollback
 * errors.  You probably want to use ntfs_cluster_free() instead.
 *
 * Note, __ntfs_cluster_free() does not modify the runlist, so you have to
 * remove from the runlist or mark sparse the freed runs later.
 *
 * Return the number of deallocated clusters (not counting sparse ones) on
 * success and -errno on error.
 *
 * WARNING: If @ctx is supplied, regardless of whether success or failure is
 *	    returned, you need to check IS_ERR(@ctx->mrec) and if 'true' the @ctx
 *	    is no longer valid, i.e. you need to either call
 *	    ntfs_attr_reinit_search_ctx() or ntfs_attr_put_search_ctx() on it.
 *	    In that case PTR_ERR(@ctx->mrec) will give you the error code for
 *	    why the mapping of the old inode failed.
 *
 * Locking: - The runlist described by @ni must be locked for writing on entry
 *	      and is locked on return.  Note the runlist may be modified when
 *	      needed runlist fragments need to be mapped.
 *	    - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 *	    - If @ctx is NULL, the base mft record of @ni must not be mapped on
 *	      entry and it will be left unmapped on return.
 *	    - If @ctx is not NULL, the base mft record must be mapped on entry
 *	      and it will be left mapped on return.
 */
s64 __ntfs_cluster_free(struct ntfs_inode *ni, const s64 start_vcn, s64 count,
		struct ntfs_attr_search_ctx *ctx, const bool is_rollback)
{
	s64 delta, to_free, total_freed, real_freed;
	struct ntfs_volume *vol;
	struct inode *lcnbmp_vi;
	struct runlist_element *rl;
	int err;
	unsigned int memalloc_flags;

	ntfs_debug("Entering for i_ino 0x%llx, start_vcn 0x%llx, count 0x%llx.%s",
			ni->mft_no, start_vcn, count,
			is_rollback ? " (rollback)" : "");
	vol = ni->vol;
	lcnbmp_vi = vol->lcnbmp_ino;
	if (start_vcn < 0 || count < -1)
		return -EINVAL;

	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	/*
	 * Lock the lcn bitmap for writing but only if not rolling back.  We
	 * must hold the lock all the way including through rollback otherwise
	 * rollback is not possible because once we have cleared a bit and
	 * dropped the lock, anyone could have set the bit again, thus
	 * allocating the cluster for another use.
	 */
	if (likely(!is_rollback)) {
		memalloc_flags = memalloc_nofs_save();
		down_write(&vol->lcnbmp_lock);
	}

	total_freed = real_freed = 0;

	rl = ntfs_attr_find_vcn_nolock(ni, start_vcn, ctx);
	if (IS_ERR(rl)) {
		err = PTR_ERR(rl);
		if (err == -ENOENT) {
			if (likely(!is_rollback)) {
				up_write(&vol->lcnbmp_lock);
				memalloc_nofs_restore(memalloc_flags);
			}
			return 0;
		}

		if (!is_rollback)
			ntfs_error(vol->sb,
				"Failed to find first runlist element (error %d), aborting.",
				err);
		goto err_out;
	}
	if (unlikely(rl->lcn < LCN_HOLE)) {
		if (!is_rollback)
			ntfs_error(vol->sb, "First runlist element has invalid lcn, aborting.");
		err = -EIO;
		goto err_out;
	}
	/* Find the starting cluster inside the run that needs freeing. */
	delta = start_vcn - rl->vcn;

	/* The number of clusters in this run that need freeing. */
	to_free = rl->length - delta;
	if (count >= 0 && to_free > count)
		to_free = count;

	if (likely(rl->lcn >= 0)) {
		/* Do the actual freeing of the clusters in this run. */
		err = ntfs_bitmap_set_bits_in_run(lcnbmp_vi, rl->lcn + delta,
				to_free, likely(!is_rollback) ? 0 : 1);
		if (unlikely(err)) {
			if (!is_rollback)
				ntfs_error(vol->sb,
					"Failed to clear first run (error %i), aborting.",
					err);
			goto err_out;
		}
		/* We have freed @to_free real clusters. */
		real_freed = to_free;
	}
	/* Go to the next run and adjust the number of clusters left to free. */
	++rl;
	if (count >= 0)
		count -= to_free;

	/* Keep track of the total "freed" clusters, including sparse ones. */
	total_freed = to_free;
	/*
	 * Loop over the remaining runs, using @count as a capping value, and
	 * free them.
	 */
	for (; rl->length && count != 0; ++rl) {
		if (unlikely(rl->lcn < LCN_HOLE)) {
			s64 vcn;

			/* Attempt to map runlist. */
			vcn = rl->vcn;
			rl = ntfs_attr_find_vcn_nolock(ni, vcn, ctx);
			if (IS_ERR(rl)) {
				err = PTR_ERR(rl);
				if (!is_rollback)
					ntfs_error(vol->sb,
						"Failed to map runlist fragment or failed to find subsequent runlist element.");
				goto err_out;
			}
			if (unlikely(rl->lcn < LCN_HOLE)) {
				if (!is_rollback)
					ntfs_error(vol->sb,
						"Runlist element has invalid lcn (0x%llx).",
						rl->lcn);
				err = -EIO;
				goto err_out;
			}
		}
		/* The number of clusters in this run that need freeing. */
		to_free = rl->length;
		if (count >= 0 && to_free > count)
			to_free = count;

		if (likely(rl->lcn >= 0)) {
			/* Do the actual freeing of the clusters in the run. */
			err = ntfs_bitmap_set_bits_in_run(lcnbmp_vi, rl->lcn,
					to_free, likely(!is_rollback) ? 0 : 1);
			if (unlikely(err)) {
				if (!is_rollback)
					ntfs_error(vol->sb, "Failed to clear subsequent run.");
				goto err_out;
			}
			/* We have freed @to_free real clusters. */
			real_freed += to_free;
		}
		/* Adjust the number of clusters left to free. */
		if (count >= 0)
			count -= to_free;

		/* Update the total done clusters. */
		total_freed += to_free;
	}
	ntfs_inc_free_clusters(vol, real_freed);
	if (likely(!is_rollback)) {
		up_write(&vol->lcnbmp_lock);
		memalloc_nofs_restore(memalloc_flags);
	}

	WARN_ON(count > 0);

	if (NVolDiscard(vol) && !is_rollback) {
		s64 total_discarded = 0, rl_off;
		u32 gran = bdev_discard_granularity(vol->sb->s_bdev);

		rl = ntfs_attr_find_vcn_nolock(ni, start_vcn, ctx);
		if (IS_ERR(rl))
			return real_freed;
		rl_off = start_vcn - rl->vcn;
		while (rl->length && total_discarded < total_freed) {
			s64 to_discard = rl->length - rl_off;

			if (to_discard + total_discarded > total_freed)
				to_discard = total_freed - total_discarded;
			if (rl->lcn >= 0) {
				sector_t start_sector, end_sector;
				int ret;

				start_sector = ALIGN(NTFS_CLU_TO_B(vol, rl->lcn + rl_off),
						     gran) >> SECTOR_SHIFT;
				end_sector = ALIGN_DOWN(NTFS_CLU_TO_B(vol,
							rl->lcn + rl_off + to_discard),
							gran) >> SECTOR_SHIFT;
				if (start_sector < end_sector) {
					ret = blkdev_issue_discard(vol->sb->s_bdev, start_sector,
								   end_sector - start_sector,
								   GFP_NOFS);
					if (ret)
						break;
				}
			}

			total_discarded += to_discard;
			++rl;
			rl_off = 0;
		}
	}

	/* We are done.  Return the number of actually freed clusters. */
	ntfs_debug("Done.");
	return real_freed;
err_out:
	if (is_rollback)
		return err;
	/* If no real clusters were freed, no need to rollback. */
	if (!real_freed) {
		up_write(&vol->lcnbmp_lock);
		memalloc_nofs_restore(memalloc_flags);
		return err;
	}
	/*
	 * Attempt to rollback and if that succeeds just return the error code.
	 * If rollback fails, set the volume errors flag, emit an error
	 * message, and return the error code.
	 */
	delta = __ntfs_cluster_free(ni, start_vcn, total_freed, ctx, true);
	if (delta < 0) {
		ntfs_error(vol->sb,
			"Failed to rollback (error %i).  Leaving inconsistent metadata!  Unmount and run chkdsk.",
			(int)delta);
		NVolSetErrors(vol);
	}
	ntfs_dec_free_clusters(vol, delta);
	up_write(&vol->lcnbmp_lock);
	memalloc_nofs_restore(memalloc_flags);
	ntfs_error(vol->sb, "Aborting (error %i).", err);
	return err;
}
