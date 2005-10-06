/*
 * lcnalloc.c - Cluster (de)allocation code.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef NTFS_RW

#include <linux/pagemap.h>

#include "lcnalloc.h"
#include "debug.h"
#include "bitmap.h"
#include "inode.h"
#include "volume.h"
#include "attrib.h"
#include "malloc.h"
#include "aops.h"
#include "ntfs.h"

/**
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
int ntfs_cluster_free_from_rl_nolock(ntfs_volume *vol,
		const runlist_element *rl)
{
	struct inode *lcnbmp_vi = vol->lcnbmp_ino;
	int ret = 0;

	ntfs_debug("Entering.");
	if (!rl)
		return 0;
	for (; rl->length; rl++) {
		int err;

		if (rl->lcn < 0)
			continue;
		err = ntfs_bitmap_clear_run(lcnbmp_vi, rl->lcn, rl->length);
		if (unlikely(err && (!ret || ret == -ENOMEM) && ret != err))
			ret = err;
	}
	ntfs_debug("Done.");
	return ret;
}

/**
 * ntfs_cluster_alloc - allocate clusters on an ntfs volume
 * @vol:	mounted ntfs volume on which to allocate the clusters
 * @start_vcn:	vcn to use for the first allocated cluster
 * @count:	number of clusters to allocate
 * @start_lcn:	starting lcn at which to allocate the clusters (or -1 if none)
 * @zone:	zone from which to allocate the clusters
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
 * FIXME: We should be monitoring cluster allocation and increment the MFT zone
 * size dynamically but this is something for the future.  We will just cause
 * heavier fragmentation by not doing it and I am not even sure Windows would
 * grow the MFT zone dynamically, so it might even be correct not to do this.
 * The overhead in doing dynamic MFT zone expansion would be very large and
 * unlikely worth the effort. (AIA)
 *
 * TODO: I have added in double the required zone position pointer wrap around
 * logic which can be optimized to having only one of the two logic sets.
 * However, having the double logic will work fine, but if we have only one of
 * the sets and we get it wrong somewhere, then we get into trouble, so
 * removing the duplicate logic requires _very_ careful consideration of _all_
 * possible code paths.  So at least for now, I am leaving the double logic -
 * better safe than sorry... (AIA)
 *
 * Locking: - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 */
runlist_element *ntfs_cluster_alloc(ntfs_volume *vol, const VCN start_vcn,
		const s64 count, const LCN start_lcn,
		const NTFS_CLUSTER_ALLOCATION_ZONES zone)
{
	LCN zone_start, zone_end, bmp_pos, bmp_initial_pos, last_read_pos, lcn;
	LCN prev_lcn = 0, prev_run_len = 0, mft_zone_size;
	s64 clusters;
	loff_t i_size;
	struct inode *lcnbmp_vi;
	runlist_element *rl = NULL;
	struct address_space *mapping;
	struct page *page = NULL;
	u8 *buf, *byte;
	int err = 0, rlpos, rlsize, buf_size;
	u8 pass, done_zones, search_zone, need_writeback = 0, bit;

	ntfs_debug("Entering for start_vcn 0x%llx, count 0x%llx, start_lcn "
			"0x%llx, zone %s_ZONE.", (unsigned long long)start_vcn,
			(unsigned long long)count,
			(unsigned long long)start_lcn,
			zone == MFT_ZONE ? "MFT" : "DATA");
	BUG_ON(!vol);
	lcnbmp_vi = vol->lcnbmp_ino;
	BUG_ON(!lcnbmp_vi);
	BUG_ON(start_vcn < 0);
	BUG_ON(count < 0);
	BUG_ON(start_lcn < -1);
	BUG_ON(zone < FIRST_ZONE);
	BUG_ON(zone > LAST_ZONE);

	/* Return NULL if @count is zero. */
	if (!count)
		return NULL;
	/* Take the lcnbmp lock for writing. */
	down_write(&vol->lcnbmp_lock);
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
	} else if (zone == DATA_ZONE && zone_start >= vol->mft_zone_start &&
			zone_start < vol->mft_zone_end) {
		zone_start = vol->mft_zone_end;
		/*
		 * Starting at beginning of data1_zone which means a single
		 * pass in this zone is sufficient.
		 */
		pass = 2;
	} else if (zone == MFT_ZONE && (zone_start < vol->mft_zone_start ||
			zone_start >= vol->mft_zone_end)) {
		zone_start = vol->mft_lcn;
		if (!vol->mft_zone_end)
			zone_start = 0;
		/*
		 * Starting at beginning of volume which means a single pass
		 * is sufficient.
		 */
		pass = 2;
	}
	if (zone == MFT_ZONE) {
		zone_end = vol->mft_zone_end;
		search_zone = 1;
	} else /* if (zone == DATA_ZONE) */ {
		/* Skip searching the mft zone. */
		done_zones |= 1;
		if (zone_start >= vol->mft_zone_end) {
			zone_end = vol->nr_clusters;
			search_zone = 2;
		} else {
			zone_end = vol->mft_zone_start;
			search_zone = 4;
		}
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
		ntfs_debug("Start of outer while loop: done_zones 0x%x, "
				"search_zone %i, pass %i, zone_start 0x%llx, "
				"zone_end 0x%llx, bmp_initial_pos 0x%llx, "
				"bmp_pos 0x%llx, rlpos %i, rlsize %i.",
				done_zones, search_zone, pass,
				(unsigned long long)zone_start,
				(unsigned long long)zone_end,
				(unsigned long long)bmp_initial_pos,
				(unsigned long long)bmp_pos, rlpos, rlsize);
		/* Loop until we run out of free clusters. */
		last_read_pos = bmp_pos >> 3;
		ntfs_debug("last_read_pos 0x%llx.",
				(unsigned long long)last_read_pos);
		if (last_read_pos > i_size) {
			ntfs_debug("End of attribute reached.  "
					"Skipping to zone_pass_done.");
			goto zone_pass_done;
		}
		if (likely(page)) {
			if (need_writeback) {
				ntfs_debug("Marking page dirty.");
				flush_dcache_page(page);
				set_page_dirty(page);
				need_writeback = 0;
			}
			ntfs_unmap_page(page);
		}
		page = ntfs_map_page(mapping, last_read_pos >>
				PAGE_CACHE_SHIFT);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			ntfs_error(vol->sb, "Failed to map page.");
			goto out;
		}
		buf_size = last_read_pos & ~PAGE_CACHE_MASK;
		buf = page_address(page) + buf_size;
		buf_size = PAGE_CACHE_SIZE - buf_size;
		if (unlikely(last_read_pos + buf_size > i_size))
			buf_size = i_size - last_read_pos;
		buf_size <<= 3;
		lcn = bmp_pos & 7;
		bmp_pos &= ~(LCN)7;
		ntfs_debug("Before inner while loop: buf_size %i, lcn 0x%llx, "
				"bmp_pos 0x%llx, need_writeback %i.", buf_size,
				(unsigned long long)lcn,
				(unsigned long long)bmp_pos, need_writeback);
		while (lcn < buf_size && lcn + bmp_pos < zone_end) {
			byte = buf + (lcn >> 3);
			ntfs_debug("In inner while loop: buf_size %i, "
					"lcn 0x%llx, bmp_pos 0x%llx, "
					"need_writeback %i, byte ofs 0x%x, "
					"*byte 0x%x.", buf_size,
					(unsigned long long)lcn,
					(unsigned long long)bmp_pos,
					need_writeback,
					(unsigned int)(lcn >> 3),
					(unsigned int)*byte);
			/* Skip full bytes. */
			if (*byte == 0xff) {
				lcn = (lcn + 8) & ~(LCN)7;
				ntfs_debug("Continuing while loop 1.");
				continue;
			}
			bit = 1 << (lcn & 7);
			ntfs_debug("bit %i.", bit);
			/* If the bit is already set, go onto the next one. */
			if (*byte & bit) {
				lcn++;
				ntfs_debug("Continuing while loop 2.");
				continue;
			}
			/*
			 * Allocate more memory if needed, including space for
			 * the terminator element.
			 * ntfs_malloc_nofs() operates on whole pages only.
			 */
			if ((rlpos + 2) * sizeof(*rl) > rlsize) {
				runlist_element *rl2;

				ntfs_debug("Reallocating memory.");
				if (!rl)
					ntfs_debug("First free bit is at LCN "
							"0x%llx.",
							(unsigned long long)
							(lcn + bmp_pos));
				rl2 = ntfs_malloc_nofs(rlsize + (int)PAGE_SIZE);
				if (unlikely(!rl2)) {
					err = -ENOMEM;
					ntfs_error(vol->sb, "Failed to "
							"allocate memory.");
					goto out;
				}
				memcpy(rl2, rl, rlsize);
				ntfs_free(rl);
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
			/*
			 * Coalesce with previous run if adjacent LCNs.
			 * Otherwise, append a new run.
			 */
			ntfs_debug("Adding run (lcn 0x%llx, len 0x%llx), "
					"prev_lcn 0x%llx, lcn 0x%llx, "
					"bmp_pos 0x%llx, prev_run_len 0x%llx, "
					"rlpos %i.",
					(unsigned long long)(lcn + bmp_pos),
					1ULL, (unsigned long long)prev_lcn,
					(unsigned long long)lcn,
					(unsigned long long)bmp_pos,
					(unsigned long long)prev_run_len,
					rlpos);
			if (prev_lcn == lcn + bmp_pos - prev_run_len && rlpos) {
				ntfs_debug("Coalescing to run (lcn 0x%llx, "
						"len 0x%llx).",
						(unsigned long long)
						rl[rlpos - 1].lcn,
						(unsigned long long)
						rl[rlpos - 1].length);
				rl[rlpos - 1].length = ++prev_run_len;
				ntfs_debug("Run now (lcn 0x%llx, len 0x%llx), "
						"prev_run_len 0x%llx.",
						(unsigned long long)
						rl[rlpos - 1].lcn,
						(unsigned long long)
						rl[rlpos - 1].length,
						(unsigned long long)
						prev_run_len);
			} else {
				if (likely(rlpos)) {
					ntfs_debug("Adding new run, (previous "
							"run lcn 0x%llx, "
							"len 0x%llx).",
							(unsigned long long)
							rl[rlpos - 1].lcn,
							(unsigned long long)
							rl[rlpos - 1].length);
					rl[rlpos].vcn = rl[rlpos - 1].vcn +
							prev_run_len;
				} else {
					ntfs_debug("Adding new run, is first "
							"run.");
					rl[rlpos].vcn = start_vcn;
				}
				rl[rlpos].lcn = prev_lcn = lcn + bmp_pos;
				rl[rlpos].length = prev_run_len = 1;
				rlpos++;
			}
			/* Done? */
			if (!--clusters) {
				LCN tc;
				/*
				 * Update the current zone position.  Positions
				 * of already scanned zones have been updated
				 * during the respective zone switches.
				 */
				tc = lcn + bmp_pos + 1;
				ntfs_debug("Done. Updating current zone "
						"position, tc 0x%llx, "
						"search_zone %i.",
						(unsigned long long)tc,
						search_zone);
				switch (search_zone) {
				case 1:
					ntfs_debug("Before checks, "
							"vol->mft_zone_pos "
							"0x%llx.",
							(unsigned long long)
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
					ntfs_debug("After checks, "
							"vol->mft_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->mft_zone_pos);
					break;
				case 2:
					ntfs_debug("Before checks, "
							"vol->data1_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data1_zone_pos);
					if (tc >= vol->nr_clusters)
						vol->data1_zone_pos =
							     vol->mft_zone_end;
					else if ((bmp_initial_pos >=
						    vol->data1_zone_pos ||
						    tc > vol->data1_zone_pos)
						    && tc >= vol->mft_zone_end)
						vol->data1_zone_pos = tc;
					ntfs_debug("After checks, "
							"vol->data1_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data1_zone_pos);
					break;
				case 4:
					ntfs_debug("Before checks, "
							"vol->data2_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data2_zone_pos);
					if (tc >= vol->mft_zone_start)
						vol->data2_zone_pos = 0;
					else if (bmp_initial_pos >=
						      vol->data2_zone_pos ||
						      tc > vol->data2_zone_pos)
						vol->data2_zone_pos = tc;
					ntfs_debug("After checks, "
							"vol->data2_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data2_zone_pos);
					break;
				default:
					BUG();
				}
				ntfs_debug("Finished.  Going to out.");
				goto out;
			}
			lcn++;
		}
		bmp_pos += buf_size;
		ntfs_debug("After inner while loop: buf_size 0x%x, lcn "
				"0x%llx, bmp_pos 0x%llx, need_writeback %i.",
				buf_size, (unsigned long long)lcn,
				(unsigned long long)bmp_pos, need_writeback);
		if (bmp_pos < zone_end) {
			ntfs_debug("Continuing outer while loop, "
					"bmp_pos 0x%llx, zone_end 0x%llx.",
					(unsigned long long)bmp_pos,
					(unsigned long long)zone_end);
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
				BUG();
			}
			/* Sanity check. */
			if (zone_end < zone_start)
				zone_end = zone_start;
			bmp_pos = zone_start;
			ntfs_debug("Continuing outer while loop, pass 2, "
					"zone_start 0x%llx, zone_end 0x%llx, "
					"bmp_pos 0x%llx.",
					(unsigned long long)zone_start,
					(unsigned long long)zone_end,
					(unsigned long long)bmp_pos);
			continue;
		} /* pass == 2 */
done_zones_check:
		ntfs_debug("At done_zones_check, search_zone %i, done_zones "
				"before 0x%x, done_zones after 0x%x.",
				search_zone, done_zones,
				done_zones | search_zone);
		done_zones |= search_zone;
		if (done_zones < 7) {
			ntfs_debug("Switching zone.");
			/* Now switch to the next zone we haven't done yet. */
			pass = 1;
			switch (search_zone) {
			case 1:
				ntfs_debug("Switching from mft zone to data1 "
						"zone.");
				/* Update mft zone position. */
				if (rlpos) {
					LCN tc;

					ntfs_debug("Before checks, "
							"vol->mft_zone_pos "
							"0x%llx.",
							(unsigned long long)
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
					ntfs_debug("After checks, "
							"vol->mft_zone_pos "
							"0x%llx.",
							(unsigned long long)
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
				ntfs_debug("Switching from data1 zone to "
						"data2 zone.");
				/* Update data1 zone position. */
				if (rlpos) {
					LCN tc;

					ntfs_debug("Before checks, "
							"vol->data1_zone_pos "
							"0x%llx.",
							(unsigned long long)
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
					ntfs_debug("After checks, "
							"vol->data1_zone_pos "
							"0x%llx.",
							(unsigned long long)
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
				ntfs_debug("Switching from data2 zone to "
						"data1 zone.");
				/* Update data2 zone position. */
				if (rlpos) {
					LCN tc;

					ntfs_debug("Before checks, "
							"vol->data2_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data2_zone_pos);
					tc = rl[rlpos - 1].lcn +
							rl[rlpos - 1].length;
					if (tc >= vol->mft_zone_start)
						vol->data2_zone_pos = 0;
					else if (bmp_initial_pos >=
						      vol->data2_zone_pos ||
						      tc > vol->data2_zone_pos)
						vol->data2_zone_pos = tc;
					ntfs_debug("After checks, "
							"vol->data2_zone_pos "
							"0x%llx.",
							(unsigned long long)
							vol->data2_zone_pos);
				}
				/* Switch from data2 zone to data1 zone. */
				goto switch_to_data1_zone;
			default:
				BUG();
			}
			ntfs_debug("After zone switch, search_zone %i, "
					"pass %i, bmp_initial_pos 0x%llx, "
					"zone_start 0x%llx, zone_end 0x%llx.",
					search_zone, pass,
					(unsigned long long)bmp_initial_pos,
					(unsigned long long)zone_start,
					(unsigned long long)zone_end);
			bmp_pos = zone_start;
			if (zone_start == zone_end) {
				ntfs_debug("Empty zone, going to "
						"done_zones_check.");
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
		ntfs_debug("vol->mft_zone_start 0x%llx, vol->mft_zone_end "
				"0x%llx, mft_zone_size 0x%llx.",
				(unsigned long long)vol->mft_zone_start,
				(unsigned long long)vol->mft_zone_end,
				(unsigned long long)mft_zone_size);
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
		ntfs_debug("After shrinking mft zone, mft_zone_size 0x%llx, "
				"vol->mft_zone_start 0x%llx, "
				"vol->mft_zone_end 0x%llx, "
				"vol->mft_zone_pos 0x%llx, search_zone 2, "
				"pass 2, dones_zones 0x%x, zone_start 0x%llx, "
				"zone_end 0x%llx, vol->data1_zone_pos 0x%llx, "
				"continuing outer while loop.",
				(unsigned long long)mft_zone_size,
				(unsigned long long)vol->mft_zone_start,
				(unsigned long long)vol->mft_zone_end,
				(unsigned long long)vol->mft_zone_pos,
				done_zones, (unsigned long long)zone_start,
				(unsigned long long)zone_end,
				(unsigned long long)vol->data1_zone_pos);
	}
	ntfs_debug("After outer while loop.");
out:
	ntfs_debug("At out.");
	/* Add runlist terminator element. */
	if (likely(rl)) {
		rl[rlpos].vcn = rl[rlpos - 1].vcn + rl[rlpos - 1].length;
		rl[rlpos].lcn = LCN_RL_NOT_MAPPED;
		rl[rlpos].length = 0;
	}
	if (likely(page && !IS_ERR(page))) {
		if (need_writeback) {
			ntfs_debug("Marking page dirty.");
			flush_dcache_page(page);
			set_page_dirty(page);
			need_writeback = 0;
		}
		ntfs_unmap_page(page);
	}
	if (likely(!err)) {
		up_write(&vol->lcnbmp_lock);
		ntfs_debug("Done.");
		return rl;
	}
	ntfs_error(vol->sb, "Failed to allocate clusters, aborting "
			"(error %i).", err);
	if (rl) {
		int err2;

		if (err == -ENOSPC)
			ntfs_debug("Not enough space to complete allocation, "
					"err -ENOSPC, first free lcn 0x%llx, "
					"could allocate up to 0x%llx "
					"clusters.",
					(unsigned long long)rl[0].lcn,
					(unsigned long long)(count - clusters));
		/* Deallocate all allocated clusters. */
		ntfs_debug("Attempting rollback...");
		err2 = ntfs_cluster_free_from_rl_nolock(vol, rl);
		if (err2) {
			ntfs_error(vol->sb, "Failed to rollback (error %i).  "
					"Leaving inconsistent metadata!  "
					"Unmount and run chkdsk.", err2);
			NVolSetErrors(vol);
		}
		/* Free the runlist. */
		ntfs_free(rl);
	} else if (err == -ENOSPC)
		ntfs_debug("No space left at all, err = -ENOSPC, first free "
				"lcn = 0x%llx.",
				(long long)vol->data1_zone_pos);
	up_write(&vol->lcnbmp_lock);
	return ERR_PTR(err);
}

/**
 * __ntfs_cluster_free - free clusters on an ntfs volume
 * @ni:		ntfs inode whose runlist describes the clusters to free
 * @start_vcn:	vcn in the runlist of @ni at which to start freeing clusters
 * @count:	number of clusters to free or -1 for all clusters
 * @is_rollback:	true if this is a rollback operation
 *
 * Free @count clusters starting at the cluster @start_vcn in the runlist
 * described by the vfs inode @ni.
 *
 * If @count is -1, all clusters from @start_vcn to the end of the runlist are
 * deallocated.  Thus, to completely free all clusters in a runlist, use
 * @start_vcn = 0 and @count = -1.
 *
 * @is_rollback should always be FALSE, it is for internal use to rollback
 * errors.  You probably want to use ntfs_cluster_free() instead.
 *
 * Note, ntfs_cluster_free() does not modify the runlist at all, so the caller
 * has to deal with it later.
 *
 * Return the number of deallocated clusters (not counting sparse ones) on
 * success and -errno on error.
 *
 * Locking: - The runlist described by @ni must be locked for writing on entry
 *	      and is locked on return.  Note the runlist may be modified when
 *	      needed runlist fragments need to be mapped.
 *	    - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 */
s64 __ntfs_cluster_free(ntfs_inode *ni, const VCN start_vcn, s64 count,
		const BOOL is_rollback)
{
	s64 delta, to_free, total_freed, real_freed;
	ntfs_volume *vol;
	struct inode *lcnbmp_vi;
	runlist_element *rl;
	int err;

	BUG_ON(!ni);
	ntfs_debug("Entering for i_ino 0x%lx, start_vcn 0x%llx, count "
			"0x%llx.%s", ni->mft_no, (unsigned long long)start_vcn,
			(unsigned long long)count,
			is_rollback ? " (rollback)" : "");
	vol = ni->vol;
	lcnbmp_vi = vol->lcnbmp_ino;
	BUG_ON(!lcnbmp_vi);
	BUG_ON(start_vcn < 0);
	BUG_ON(count < -1);
	/*
	 * Lock the lcn bitmap for writing but only if not rolling back.  We
	 * must hold the lock all the way including through rollback otherwise
	 * rollback is not possible because once we have cleared a bit and
	 * dropped the lock, anyone could have set the bit again, thus
	 * allocating the cluster for another use.
	 */
	if (likely(!is_rollback))
		down_write(&vol->lcnbmp_lock);

	total_freed = real_freed = 0;

	rl = ntfs_attr_find_vcn_nolock(ni, start_vcn, TRUE);
	if (IS_ERR(rl)) {
		if (!is_rollback)
			ntfs_error(vol->sb, "Failed to find first runlist "
					"element (error %li), aborting.",
					PTR_ERR(rl));
		err = PTR_ERR(rl);
		goto err_out;
	}
	if (unlikely(rl->lcn < LCN_HOLE)) {
		if (!is_rollback)
			ntfs_error(vol->sb, "First runlist element has "
					"invalid lcn, aborting.");
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
				ntfs_error(vol->sb, "Failed to clear first run "
						"(error %i), aborting.", err);
			goto err_out;
		}
		/* We have freed @to_free real clusters. */
		real_freed = to_free;
	};
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
			VCN vcn;

			/* Attempt to map runlist. */
			vcn = rl->vcn;
			rl = ntfs_attr_find_vcn_nolock(ni, vcn, TRUE);
			if (IS_ERR(rl)) {
				err = PTR_ERR(rl);
				if (!is_rollback)
					ntfs_error(vol->sb, "Failed to map "
							"runlist fragment or "
							"failed to find "
							"subsequent runlist "
							"element.");
				goto err_out;
			}
			if (unlikely(rl->lcn < LCN_HOLE)) {
				if (!is_rollback)
					ntfs_error(vol->sb, "Runlist element "
							"has invalid lcn "
							"(0x%llx).",
							(unsigned long long)
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
					ntfs_error(vol->sb, "Failed to clear "
							"subsequent run.");
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
	if (likely(!is_rollback))
		up_write(&vol->lcnbmp_lock);

	BUG_ON(count > 0);

	/* We are done.  Return the number of actually freed clusters. */
	ntfs_debug("Done.");
	return real_freed;
err_out:
	if (is_rollback)
		return err;
	/* If no real clusters were freed, no need to rollback. */
	if (!real_freed) {
		up_write(&vol->lcnbmp_lock);
		return err;
	}
	/*
	 * Attempt to rollback and if that succeeds just return the error code.
	 * If rollback fails, set the volume errors flag, emit an error
	 * message, and return the error code.
	 */
	delta = __ntfs_cluster_free(ni, start_vcn, total_freed, TRUE);
	if (delta < 0) {
		ntfs_error(vol->sb, "Failed to rollback (error %i).  Leaving "
				"inconsistent metadata!  Unmount and run "
				"chkdsk.", (int)delta);
		NVolSetErrors(vol);
	}
	up_write(&vol->lcnbmp_lock);
	ntfs_error(vol->sb, "Aborting (error %i).", err);
	return err;
}

#endif /* NTFS_RW */
