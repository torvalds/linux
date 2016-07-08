/*
 * logfile.c - NTFS kernel journal handling. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2002-2007 Anton Altaparmakov
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

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include "attrib.h"
#include "aops.h"
#include "debug.h"
#include "logfile.h"
#include "malloc.h"
#include "volume.h"
#include "ntfs.h"

/**
 * ntfs_check_restart_page_header - check the page header for consistency
 * @vi:		$LogFile inode to which the restart page header belongs
 * @rp:		restart page header to check
 * @pos:	position in @vi at which the restart page header resides
 *
 * Check the restart page header @rp for consistency and return 'true' if it is
 * consistent and 'false' otherwise.
 *
 * This function only needs NTFS_BLOCK_SIZE bytes in @rp, i.e. it does not
 * require the full restart page.
 */
static bool ntfs_check_restart_page_header(struct inode *vi,
		RESTART_PAGE_HEADER *rp, s64 pos)
{
	u32 logfile_system_page_size, logfile_log_page_size;
	u16 ra_ofs, usa_count, usa_ofs, usa_end = 0;
	bool have_usa = true;

	ntfs_debug("Entering.");
	/*
	 * If the system or log page sizes are smaller than the ntfs block size
	 * or either is not a power of 2 we cannot handle this log file.
	 */
	logfile_system_page_size = le32_to_cpu(rp->system_page_size);
	logfile_log_page_size = le32_to_cpu(rp->log_page_size);
	if (logfile_system_page_size < NTFS_BLOCK_SIZE ||
			logfile_log_page_size < NTFS_BLOCK_SIZE ||
			logfile_system_page_size &
			(logfile_system_page_size - 1) ||
			!is_power_of_2(logfile_log_page_size)) {
		ntfs_error(vi->i_sb, "$LogFile uses unsupported page size.");
		return false;
	}
	/*
	 * We must be either at !pos (1st restart page) or at pos = system page
	 * size (2nd restart page).
	 */
	if (pos && pos != logfile_system_page_size) {
		ntfs_error(vi->i_sb, "Found restart area in incorrect "
				"position in $LogFile.");
		return false;
	}
	/* We only know how to handle version 1.1. */
	if (sle16_to_cpu(rp->major_ver) != 1 ||
			sle16_to_cpu(rp->minor_ver) != 1) {
		ntfs_error(vi->i_sb, "$LogFile version %i.%i is not "
				"supported.  (This driver supports version "
				"1.1 only.)", (int)sle16_to_cpu(rp->major_ver),
				(int)sle16_to_cpu(rp->minor_ver));
		return false;
	}
	/*
	 * If chkdsk has been run the restart page may not be protected by an
	 * update sequence array.
	 */
	if (ntfs_is_chkd_record(rp->magic) && !le16_to_cpu(rp->usa_count)) {
		have_usa = false;
		goto skip_usa_checks;
	}
	/* Verify the size of the update sequence array. */
	usa_count = 1 + (logfile_system_page_size >> NTFS_BLOCK_SIZE_BITS);
	if (usa_count != le16_to_cpu(rp->usa_count)) {
		ntfs_error(vi->i_sb, "$LogFile restart page specifies "
				"inconsistent update sequence array count.");
		return false;
	}
	/* Verify the position of the update sequence array. */
	usa_ofs = le16_to_cpu(rp->usa_ofs);
	usa_end = usa_ofs + usa_count * sizeof(u16);
	if (usa_ofs < sizeof(RESTART_PAGE_HEADER) ||
			usa_end > NTFS_BLOCK_SIZE - sizeof(u16)) {
		ntfs_error(vi->i_sb, "$LogFile restart page specifies "
				"inconsistent update sequence array offset.");
		return false;
	}
skip_usa_checks:
	/*
	 * Verify the position of the restart area.  It must be:
	 *	- aligned to 8-byte boundary,
	 *	- after the update sequence array, and
	 *	- within the system page size.
	 */
	ra_ofs = le16_to_cpu(rp->restart_area_offset);
	if (ra_ofs & 7 || (have_usa ? ra_ofs < usa_end :
			ra_ofs < sizeof(RESTART_PAGE_HEADER)) ||
			ra_ofs > logfile_system_page_size) {
		ntfs_error(vi->i_sb, "$LogFile restart page specifies "
				"inconsistent restart area offset.");
		return false;
	}
	/*
	 * Only restart pages modified by chkdsk are allowed to have chkdsk_lsn
	 * set.
	 */
	if (!ntfs_is_chkd_record(rp->magic) && sle64_to_cpu(rp->chkdsk_lsn)) {
		ntfs_error(vi->i_sb, "$LogFile restart page is not modified "
				"by chkdsk but a chkdsk LSN is specified.");
		return false;
	}
	ntfs_debug("Done.");
	return true;
}

/**
 * ntfs_check_restart_area - check the restart area for consistency
 * @vi:		$LogFile inode to which the restart page belongs
 * @rp:		restart page whose restart area to check
 *
 * Check the restart area of the restart page @rp for consistency and return
 * 'true' if it is consistent and 'false' otherwise.
 *
 * This function assumes that the restart page header has already been
 * consistency checked.
 *
 * This function only needs NTFS_BLOCK_SIZE bytes in @rp, i.e. it does not
 * require the full restart page.
 */
static bool ntfs_check_restart_area(struct inode *vi, RESTART_PAGE_HEADER *rp)
{
	u64 file_size;
	RESTART_AREA *ra;
	u16 ra_ofs, ra_len, ca_ofs;
	u8 fs_bits;

	ntfs_debug("Entering.");
	ra_ofs = le16_to_cpu(rp->restart_area_offset);
	ra = (RESTART_AREA*)((u8*)rp + ra_ofs);
	/*
	 * Everything before ra->file_size must be before the first word
	 * protected by an update sequence number.  This ensures that it is
	 * safe to access ra->client_array_offset.
	 */
	if (ra_ofs + offsetof(RESTART_AREA, file_size) >
			NTFS_BLOCK_SIZE - sizeof(u16)) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"inconsistent file offset.");
		return false;
	}
	/*
	 * Now that we can access ra->client_array_offset, make sure everything
	 * up to the log client array is before the first word protected by an
	 * update sequence number.  This ensures we can access all of the
	 * restart area elements safely.  Also, the client array offset must be
	 * aligned to an 8-byte boundary.
	 */
	ca_ofs = le16_to_cpu(ra->client_array_offset);
	if (((ca_ofs + 7) & ~7) != ca_ofs ||
			ra_ofs + ca_ofs > NTFS_BLOCK_SIZE - sizeof(u16)) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"inconsistent client array offset.");
		return false;
	}
	/*
	 * The restart area must end within the system page size both when
	 * calculated manually and as specified by ra->restart_area_length.
	 * Also, the calculated length must not exceed the specified length.
	 */
	ra_len = ca_ofs + le16_to_cpu(ra->log_clients) *
			sizeof(LOG_CLIENT_RECORD);
	if (ra_ofs + ra_len > le32_to_cpu(rp->system_page_size) ||
			ra_ofs + le16_to_cpu(ra->restart_area_length) >
			le32_to_cpu(rp->system_page_size) ||
			ra_len > le16_to_cpu(ra->restart_area_length)) {
		ntfs_error(vi->i_sb, "$LogFile restart area is out of bounds "
				"of the system page size specified by the "
				"restart page header and/or the specified "
				"restart area length is inconsistent.");
		return false;
	}
	/*
	 * The ra->client_free_list and ra->client_in_use_list must be either
	 * LOGFILE_NO_CLIENT or less than ra->log_clients or they are
	 * overflowing the client array.
	 */
	if ((ra->client_free_list != LOGFILE_NO_CLIENT &&
			le16_to_cpu(ra->client_free_list) >=
			le16_to_cpu(ra->log_clients)) ||
			(ra->client_in_use_list != LOGFILE_NO_CLIENT &&
			le16_to_cpu(ra->client_in_use_list) >=
			le16_to_cpu(ra->log_clients))) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"overflowing client free and/or in use lists.");
		return false;
	}
	/*
	 * Check ra->seq_number_bits against ra->file_size for consistency.
	 * We cannot just use ffs() because the file size is not a power of 2.
	 */
	file_size = (u64)sle64_to_cpu(ra->file_size);
	fs_bits = 0;
	while (file_size) {
		file_size >>= 1;
		fs_bits++;
	}
	if (le32_to_cpu(ra->seq_number_bits) != 67 - fs_bits) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"inconsistent sequence number bits.");
		return false;
	}
	/* The log record header length must be a multiple of 8. */
	if (((le16_to_cpu(ra->log_record_header_length) + 7) & ~7) !=
			le16_to_cpu(ra->log_record_header_length)) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"inconsistent log record header length.");
		return false;
	}
	/* Dito for the log page data offset. */
	if (((le16_to_cpu(ra->log_page_data_offset) + 7) & ~7) !=
			le16_to_cpu(ra->log_page_data_offset)) {
		ntfs_error(vi->i_sb, "$LogFile restart area specifies "
				"inconsistent log page data offset.");
		return false;
	}
	ntfs_debug("Done.");
	return true;
}

/**
 * ntfs_check_log_client_array - check the log client array for consistency
 * @vi:		$LogFile inode to which the restart page belongs
 * @rp:		restart page whose log client array to check
 *
 * Check the log client array of the restart page @rp for consistency and
 * return 'true' if it is consistent and 'false' otherwise.
 *
 * This function assumes that the restart page header and the restart area have
 * already been consistency checked.
 *
 * Unlike ntfs_check_restart_page_header() and ntfs_check_restart_area(), this
 * function needs @rp->system_page_size bytes in @rp, i.e. it requires the full
 * restart page and the page must be multi sector transfer deprotected.
 */
static bool ntfs_check_log_client_array(struct inode *vi,
		RESTART_PAGE_HEADER *rp)
{
	RESTART_AREA *ra;
	LOG_CLIENT_RECORD *ca, *cr;
	u16 nr_clients, idx;
	bool in_free_list, idx_is_first;

	ntfs_debug("Entering.");
	ra = (RESTART_AREA*)((u8*)rp + le16_to_cpu(rp->restart_area_offset));
	ca = (LOG_CLIENT_RECORD*)((u8*)ra +
			le16_to_cpu(ra->client_array_offset));
	/*
	 * Check the ra->client_free_list first and then check the
	 * ra->client_in_use_list.  Check each of the log client records in
	 * each of the lists and check that the array does not overflow the
	 * ra->log_clients value.  Also keep track of the number of records
	 * visited as there cannot be more than ra->log_clients records and
	 * that way we detect eventual loops in within a list.
	 */
	nr_clients = le16_to_cpu(ra->log_clients);
	idx = le16_to_cpu(ra->client_free_list);
	in_free_list = true;
check_list:
	for (idx_is_first = true; idx != LOGFILE_NO_CLIENT_CPU; nr_clients--,
			idx = le16_to_cpu(cr->next_client)) {
		if (!nr_clients || idx >= le16_to_cpu(ra->log_clients))
			goto err_out;
		/* Set @cr to the current log client record. */
		cr = ca + idx;
		/* The first log client record must not have a prev_client. */
		if (idx_is_first) {
			if (cr->prev_client != LOGFILE_NO_CLIENT)
				goto err_out;
			idx_is_first = false;
		}
	}
	/* Switch to and check the in use list if we just did the free list. */
	if (in_free_list) {
		in_free_list = false;
		idx = le16_to_cpu(ra->client_in_use_list);
		goto check_list;
	}
	ntfs_debug("Done.");
	return true;
err_out:
	ntfs_error(vi->i_sb, "$LogFile log client array is corrupt.");
	return false;
}

/**
 * ntfs_check_and_load_restart_page - check the restart page for consistency
 * @vi:		$LogFile inode to which the restart page belongs
 * @rp:		restart page to check
 * @pos:	position in @vi at which the restart page resides
 * @wrp:	[OUT] copy of the multi sector transfer deprotected restart page
 * @lsn:	[OUT] set to the current logfile lsn on success
 *
 * Check the restart page @rp for consistency and return 0 if it is consistent
 * and -errno otherwise.  The restart page may have been modified by chkdsk in
 * which case its magic is CHKD instead of RSTR.
 *
 * This function only needs NTFS_BLOCK_SIZE bytes in @rp, i.e. it does not
 * require the full restart page.
 *
 * If @wrp is not NULL, on success, *@wrp will point to a buffer containing a
 * copy of the complete multi sector transfer deprotected page.  On failure,
 * *@wrp is undefined.
 *
 * Simillarly, if @lsn is not NULL, on success *@lsn will be set to the current
 * logfile lsn according to this restart page.  On failure, *@lsn is undefined.
 *
 * The following error codes are defined:
 *	-EINVAL	- The restart page is inconsistent.
 *	-ENOMEM	- Not enough memory to load the restart page.
 *	-EIO	- Failed to reading from $LogFile.
 */
static int ntfs_check_and_load_restart_page(struct inode *vi,
		RESTART_PAGE_HEADER *rp, s64 pos, RESTART_PAGE_HEADER **wrp,
		LSN *lsn)
{
	RESTART_AREA *ra;
	RESTART_PAGE_HEADER *trp;
	int size, err;

	ntfs_debug("Entering.");
	/* Check the restart page header for consistency. */
	if (!ntfs_check_restart_page_header(vi, rp, pos)) {
		/* Error output already done inside the function. */
		return -EINVAL;
	}
	/* Check the restart area for consistency. */
	if (!ntfs_check_restart_area(vi, rp)) {
		/* Error output already done inside the function. */
		return -EINVAL;
	}
	ra = (RESTART_AREA*)((u8*)rp + le16_to_cpu(rp->restart_area_offset));
	/*
	 * Allocate a buffer to store the whole restart page so we can multi
	 * sector transfer deprotect it.
	 */
	trp = ntfs_malloc_nofs(le32_to_cpu(rp->system_page_size));
	if (!trp) {
		ntfs_error(vi->i_sb, "Failed to allocate memory for $LogFile "
				"restart page buffer.");
		return -ENOMEM;
	}
	/*
	 * Read the whole of the restart page into the buffer.  If it fits
	 * completely inside @rp, just copy it from there.  Otherwise map all
	 * the required pages and copy the data from them.
	 */
	size = PAGE_SIZE - (pos & ~PAGE_MASK);
	if (size >= le32_to_cpu(rp->system_page_size)) {
		memcpy(trp, rp, le32_to_cpu(rp->system_page_size));
	} else {
		pgoff_t idx;
		struct page *page;
		int have_read, to_read;

		/* First copy what we already have in @rp. */
		memcpy(trp, rp, size);
		/* Copy the remaining data one page at a time. */
		have_read = size;
		to_read = le32_to_cpu(rp->system_page_size) - size;
		idx = (pos + size) >> PAGE_SHIFT;
		BUG_ON((pos + size) & ~PAGE_MASK);
		do {
			page = ntfs_map_page(vi->i_mapping, idx);
			if (IS_ERR(page)) {
				ntfs_error(vi->i_sb, "Error mapping $LogFile "
						"page (index %lu).", idx);
				err = PTR_ERR(page);
				if (err != -EIO && err != -ENOMEM)
					err = -EIO;
				goto err_out;
			}
			size = min_t(int, to_read, PAGE_SIZE);
			memcpy((u8*)trp + have_read, page_address(page), size);
			ntfs_unmap_page(page);
			have_read += size;
			to_read -= size;
			idx++;
		} while (to_read > 0);
	}
	/*
	 * Perform the multi sector transfer deprotection on the buffer if the
	 * restart page is protected.
	 */
	if ((!ntfs_is_chkd_record(trp->magic) || le16_to_cpu(trp->usa_count))
			&& post_read_mst_fixup((NTFS_RECORD*)trp,
			le32_to_cpu(rp->system_page_size))) {
		/*
		 * A multi sector tranfer error was detected.  We only need to
		 * abort if the restart page contents exceed the multi sector
		 * transfer fixup of the first sector.
		 */
		if (le16_to_cpu(rp->restart_area_offset) +
				le16_to_cpu(ra->restart_area_length) >
				NTFS_BLOCK_SIZE - sizeof(u16)) {
			ntfs_error(vi->i_sb, "Multi sector transfer error "
					"detected in $LogFile restart page.");
			err = -EINVAL;
			goto err_out;
		}
	}
	/*
	 * If the restart page is modified by chkdsk or there are no active
	 * logfile clients, the logfile is consistent.  Otherwise, need to
	 * check the log client records for consistency, too.
	 */
	err = 0;
	if (ntfs_is_rstr_record(rp->magic) &&
			ra->client_in_use_list != LOGFILE_NO_CLIENT) {
		if (!ntfs_check_log_client_array(vi, trp)) {
			err = -EINVAL;
			goto err_out;
		}
	}
	if (lsn) {
		if (ntfs_is_rstr_record(rp->magic))
			*lsn = sle64_to_cpu(ra->current_lsn);
		else /* if (ntfs_is_chkd_record(rp->magic)) */
			*lsn = sle64_to_cpu(rp->chkdsk_lsn);
	}
	ntfs_debug("Done.");
	if (wrp)
		*wrp = trp;
	else {
err_out:
		ntfs_free(trp);
	}
	return err;
}

/**
 * ntfs_check_logfile - check the journal for consistency
 * @log_vi:	struct inode of loaded journal $LogFile to check
 * @rp:		[OUT] on success this is a copy of the current restart page
 *
 * Check the $LogFile journal for consistency and return 'true' if it is
 * consistent and 'false' if not.  On success, the current restart page is
 * returned in *@rp.  Caller must call ntfs_free(*@rp) when finished with it.
 *
 * At present we only check the two restart pages and ignore the log record
 * pages.
 *
 * Note that the MstProtected flag is not set on the $LogFile inode and hence
 * when reading pages they are not deprotected.  This is because we do not know
 * if the $LogFile was created on a system with a different page size to ours
 * yet and mst deprotection would fail if our page size is smaller.
 */
bool ntfs_check_logfile(struct inode *log_vi, RESTART_PAGE_HEADER **rp)
{
	s64 size, pos;
	LSN rstr1_lsn, rstr2_lsn;
	ntfs_volume *vol = NTFS_SB(log_vi->i_sb);
	struct address_space *mapping = log_vi->i_mapping;
	struct page *page = NULL;
	u8 *kaddr = NULL;
	RESTART_PAGE_HEADER *rstr1_ph = NULL;
	RESTART_PAGE_HEADER *rstr2_ph = NULL;
	int log_page_size, log_page_mask, err;
	bool logfile_is_empty = true;
	u8 log_page_bits;

	ntfs_debug("Entering.");
	/* An empty $LogFile must have been clean before it got emptied. */
	if (NVolLogFileEmpty(vol))
		goto is_empty;
	size = i_size_read(log_vi);
	/* Make sure the file doesn't exceed the maximum allowed size. */
	if (size > MaxLogFileSize)
		size = MaxLogFileSize;
	/*
	 * Truncate size to a multiple of the page cache size or the default
	 * log page size if the page cache size is between the default log page
	 * log page size if the page cache size is between the default log page
	 * size and twice that.
	 */
	if (PAGE_SIZE >= DefaultLogPageSize && PAGE_SIZE <=
			DefaultLogPageSize * 2)
		log_page_size = DefaultLogPageSize;
	else
		log_page_size = PAGE_SIZE;
	log_page_mask = log_page_size - 1;
	/*
	 * Use ntfs_ffs() instead of ffs() to enable the compiler to
	 * optimize log_page_size and log_page_bits into constants.
	 */
	log_page_bits = ntfs_ffs(log_page_size) - 1;
	size &= ~(s64)(log_page_size - 1);
	/*
	 * Ensure the log file is big enough to store at least the two restart
	 * pages and the minimum number of log record pages.
	 */
	if (size < log_page_size * 2 || (size - log_page_size * 2) >>
			log_page_bits < MinLogRecordPages) {
		ntfs_error(vol->sb, "$LogFile is too small.");
		return false;
	}
	/*
	 * Read through the file looking for a restart page.  Since the restart
	 * page header is at the beginning of a page we only need to search at
	 * what could be the beginning of a page (for each page size) rather
	 * than scanning the whole file byte by byte.  If all potential places
	 * contain empty and uninitialzed records, the log file can be assumed
	 * to be empty.
	 */
	for (pos = 0; pos < size; pos <<= 1) {
		pgoff_t idx = pos >> PAGE_SHIFT;
		if (!page || page->index != idx) {
			if (page)
				ntfs_unmap_page(page);
			page = ntfs_map_page(mapping, idx);
			if (IS_ERR(page)) {
				ntfs_error(vol->sb, "Error mapping $LogFile "
						"page (index %lu).", idx);
				goto err_out;
			}
		}
		kaddr = (u8*)page_address(page) + (pos & ~PAGE_MASK);
		/*
		 * A non-empty block means the logfile is not empty while an
		 * empty block after a non-empty block has been encountered
		 * means we are done.
		 */
		if (!ntfs_is_empty_recordp((le32*)kaddr))
			logfile_is_empty = false;
		else if (!logfile_is_empty)
			break;
		/*
		 * A log record page means there cannot be a restart page after
		 * this so no need to continue searching.
		 */
		if (ntfs_is_rcrd_recordp((le32*)kaddr))
			break;
		/* If not a (modified by chkdsk) restart page, continue. */
		if (!ntfs_is_rstr_recordp((le32*)kaddr) &&
				!ntfs_is_chkd_recordp((le32*)kaddr)) {
			if (!pos)
				pos = NTFS_BLOCK_SIZE >> 1;
			continue;
		}
		/*
		 * Check the (modified by chkdsk) restart page for consistency
		 * and get a copy of the complete multi sector transfer
		 * deprotected restart page.
		 */
		err = ntfs_check_and_load_restart_page(log_vi,
				(RESTART_PAGE_HEADER*)kaddr, pos,
				!rstr1_ph ? &rstr1_ph : &rstr2_ph,
				!rstr1_ph ? &rstr1_lsn : &rstr2_lsn);
		if (!err) {
			/*
			 * If we have now found the first (modified by chkdsk)
			 * restart page, continue looking for the second one.
			 */
			if (!pos) {
				pos = NTFS_BLOCK_SIZE >> 1;
				continue;
			}
			/*
			 * We have now found the second (modified by chkdsk)
			 * restart page, so we can stop looking.
			 */
			break;
		}
		/*
		 * Error output already done inside the function.  Note, we do
		 * not abort if the restart page was invalid as we might still
		 * find a valid one further in the file.
		 */
		if (err != -EINVAL) {
			ntfs_unmap_page(page);
			goto err_out;
		}
		/* Continue looking. */
		if (!pos)
			pos = NTFS_BLOCK_SIZE >> 1;
	}
	if (page)
		ntfs_unmap_page(page);
	if (logfile_is_empty) {
		NVolSetLogFileEmpty(vol);
is_empty:
		ntfs_debug("Done.  ($LogFile is empty.)");
		return true;
	}
	if (!rstr1_ph) {
		BUG_ON(rstr2_ph);
		ntfs_error(vol->sb, "Did not find any restart pages in "
				"$LogFile and it was not empty.");
		return false;
	}
	/* If both restart pages were found, use the more recent one. */
	if (rstr2_ph) {
		/*
		 * If the second restart area is more recent, switch to it.
		 * Otherwise just throw it away.
		 */
		if (rstr2_lsn > rstr1_lsn) {
			ntfs_debug("Using second restart page as it is more "
					"recent.");
			ntfs_free(rstr1_ph);
			rstr1_ph = rstr2_ph;
			/* rstr1_lsn = rstr2_lsn; */
		} else {
			ntfs_debug("Using first restart page as it is more "
					"recent.");
			ntfs_free(rstr2_ph);
		}
		rstr2_ph = NULL;
	}
	/* All consistency checks passed. */
	if (rp)
		*rp = rstr1_ph;
	else
		ntfs_free(rstr1_ph);
	ntfs_debug("Done.");
	return true;
err_out:
	if (rstr1_ph)
		ntfs_free(rstr1_ph);
	return false;
}

/**
 * ntfs_is_logfile_clean - check in the journal if the volume is clean
 * @log_vi:	struct inode of loaded journal $LogFile to check
 * @rp:		copy of the current restart page
 *
 * Analyze the $LogFile journal and return 'true' if it indicates the volume was
 * shutdown cleanly and 'false' if not.
 *
 * At present we only look at the two restart pages and ignore the log record
 * pages.  This is a little bit crude in that there will be a very small number
 * of cases where we think that a volume is dirty when in fact it is clean.
 * This should only affect volumes that have not been shutdown cleanly but did
 * not have any pending, non-check-pointed i/o, i.e. they were completely idle
 * at least for the five seconds preceding the unclean shutdown.
 *
 * This function assumes that the $LogFile journal has already been consistency
 * checked by a call to ntfs_check_logfile() and in particular if the $LogFile
 * is empty this function requires that NVolLogFileEmpty() is true otherwise an
 * empty volume will be reported as dirty.
 */
bool ntfs_is_logfile_clean(struct inode *log_vi, const RESTART_PAGE_HEADER *rp)
{
	ntfs_volume *vol = NTFS_SB(log_vi->i_sb);
	RESTART_AREA *ra;

	ntfs_debug("Entering.");
	/* An empty $LogFile must have been clean before it got emptied. */
	if (NVolLogFileEmpty(vol)) {
		ntfs_debug("Done.  ($LogFile is empty.)");
		return true;
	}
	BUG_ON(!rp);
	if (!ntfs_is_rstr_record(rp->magic) &&
			!ntfs_is_chkd_record(rp->magic)) {
		ntfs_error(vol->sb, "Restart page buffer is invalid.  This is "
				"probably a bug in that the $LogFile should "
				"have been consistency checked before calling "
				"this function.");
		return false;
	}
	ra = (RESTART_AREA*)((u8*)rp + le16_to_cpu(rp->restart_area_offset));
	/*
	 * If the $LogFile has active clients, i.e. it is open, and we do not
	 * have the RESTART_VOLUME_IS_CLEAN bit set in the restart area flags,
	 * we assume there was an unclean shutdown.
	 */
	if (ra->client_in_use_list != LOGFILE_NO_CLIENT &&
			!(ra->flags & RESTART_VOLUME_IS_CLEAN)) {
		ntfs_debug("Done.  $LogFile indicates a dirty shutdown.");
		return false;
	}
	/* $LogFile indicates a clean shutdown. */
	ntfs_debug("Done.  $LogFile indicates a clean shutdown.");
	return true;
}

/**
 * ntfs_empty_logfile - empty the contents of the $LogFile journal
 * @log_vi:	struct inode of loaded journal $LogFile to empty
 *
 * Empty the contents of the $LogFile journal @log_vi and return 'true' on
 * success and 'false' on error.
 *
 * This function assumes that the $LogFile journal has already been consistency
 * checked by a call to ntfs_check_logfile() and that ntfs_is_logfile_clean()
 * has been used to ensure that the $LogFile is clean.
 */
bool ntfs_empty_logfile(struct inode *log_vi)
{
	VCN vcn, end_vcn;
	ntfs_inode *log_ni = NTFS_I(log_vi);
	ntfs_volume *vol = log_ni->vol;
	struct super_block *sb = vol->sb;
	runlist_element *rl;
	unsigned long flags;
	unsigned block_size, block_size_bits;
	int err;
	bool should_wait = true;

	ntfs_debug("Entering.");
	if (NVolLogFileEmpty(vol)) {
		ntfs_debug("Done.");
		return true;
	}
	/*
	 * We cannot use ntfs_attr_set() because we may be still in the middle
	 * of a mount operation.  Thus we do the emptying by hand by first
	 * zapping the page cache pages for the $LogFile/$DATA attribute and
	 * then emptying each of the buffers in each of the clusters specified
	 * by the runlist by hand.
	 */
	block_size = sb->s_blocksize;
	block_size_bits = sb->s_blocksize_bits;
	vcn = 0;
	read_lock_irqsave(&log_ni->size_lock, flags);
	end_vcn = (log_ni->initialized_size + vol->cluster_size_mask) >>
			vol->cluster_size_bits;
	read_unlock_irqrestore(&log_ni->size_lock, flags);
	truncate_inode_pages(log_vi->i_mapping, 0);
	down_write(&log_ni->runlist.lock);
	rl = log_ni->runlist.rl;
	if (unlikely(!rl || vcn < rl->vcn || !rl->length)) {
map_vcn:
		err = ntfs_map_runlist_nolock(log_ni, vcn, NULL);
		if (err) {
			ntfs_error(sb, "Failed to map runlist fragment (error "
					"%d).", -err);
			goto err;
		}
		rl = log_ni->runlist.rl;
		BUG_ON(!rl || vcn < rl->vcn || !rl->length);
	}
	/* Seek to the runlist element containing @vcn. */
	while (rl->length && vcn >= rl[1].vcn)
		rl++;
	do {
		LCN lcn;
		sector_t block, end_block;
		s64 len;

		/*
		 * If this run is not mapped map it now and start again as the
		 * runlist will have been updated.
		 */
		lcn = rl->lcn;
		if (unlikely(lcn == LCN_RL_NOT_MAPPED)) {
			vcn = rl->vcn;
			goto map_vcn;
		}
		/* If this run is not valid abort with an error. */
		if (unlikely(!rl->length || lcn < LCN_HOLE))
			goto rl_err;
		/* Skip holes. */
		if (lcn == LCN_HOLE)
			continue;
		block = lcn << vol->cluster_size_bits >> block_size_bits;
		len = rl->length;
		if (rl[1].vcn > end_vcn)
			len = end_vcn - rl->vcn;
		end_block = (lcn + len) << vol->cluster_size_bits >>
				block_size_bits;
		/* Iterate over the blocks in the run and empty them. */
		do {
			struct buffer_head *bh;

			/* Obtain the buffer, possibly not uptodate. */
			bh = sb_getblk(sb, block);
			BUG_ON(!bh);
			/* Setup buffer i/o submission. */
			lock_buffer(bh);
			bh->b_end_io = end_buffer_write_sync;
			get_bh(bh);
			/* Set the entire contents of the buffer to 0xff. */
			memset(bh->b_data, -1, block_size);
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
			if (buffer_dirty(bh))
				clear_buffer_dirty(bh);
			/*
			 * Submit the buffer and wait for i/o to complete but
			 * only for the first buffer so we do not miss really
			 * serious i/o errors.  Once the first buffer has
			 * completed ignore errors afterwards as we can assume
			 * that if one buffer worked all of them will work.
			 */
			submit_bh(WRITE, bh);
			if (should_wait) {
				should_wait = false;
				wait_on_buffer(bh);
				if (unlikely(!buffer_uptodate(bh)))
					goto io_err;
			}
			brelse(bh);
		} while (++block < end_block);
	} while ((++rl)->vcn < end_vcn);
	up_write(&log_ni->runlist.lock);
	/*
	 * Zap the pages again just in case any got instantiated whilst we were
	 * emptying the blocks by hand.  FIXME: We may not have completed
	 * writing to all the buffer heads yet so this may happen too early.
	 * We really should use a kernel thread to do the emptying
	 * asynchronously and then we can also set the volume dirty and output
	 * an error message if emptying should fail.
	 */
	truncate_inode_pages(log_vi->i_mapping, 0);
	/* Set the flag so we do not have to do it again on remount. */
	NVolSetLogFileEmpty(vol);
	ntfs_debug("Done.");
	return true;
io_err:
	ntfs_error(sb, "Failed to write buffer.  Unmount and run chkdsk.");
	goto dirty_err;
rl_err:
	ntfs_error(sb, "Runlist is corrupt.  Unmount and run chkdsk.");
dirty_err:
	NVolSetErrors(vol);
	err = -EIO;
err:
	up_write(&log_ni->runlist.lock);
	ntfs_error(sb, "Failed to fill $LogFile with 0xff bytes (error %d).",
			-err);
	return false;
}

#endif /* NTFS_RW */
