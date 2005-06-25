/*
 * usnjrnl.h - NTFS kernel transaction log ($UsnJrnl) handling.  Part of the
 *	       Linux-NTFS project.
 *
 * Copyright (c) 2005 Anton Altaparmakov
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

#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/mm.h>

#include "aops.h"
#include "debug.h"
#include "endian.h"
#include "time.h"
#include "types.h"
#include "usnjrnl.h"
#include "volume.h"

/**
 * ntfs_stamp_usnjrnl - stamp the transaction log ($UsnJrnl) on an ntfs volume
 * @vol:	ntfs volume on which to stamp the transaction log
 *
 * Stamp the transaction log ($UsnJrnl) on the ntfs volume @vol and return
 * TRUE on success and FALSE on error.
 *
 * This function assumes that the transaction log has already been loaded and
 * consistency checked by a call to fs/ntfs/super.c::load_and_init_usnjrnl().
 */
BOOL ntfs_stamp_usnjrnl(ntfs_volume *vol)
{
	ntfs_debug("Entering.");
	if (likely(!NVolUsnJrnlStamped(vol))) {
		sle64 stamp;
		struct page *page;
		USN_HEADER *uh;

		page = ntfs_map_page(vol->usnjrnl_max_ino->i_mapping, 0);
		if (IS_ERR(page)) {
			ntfs_error(vol->sb, "Failed to read from "
					"$UsnJrnl/$DATA/$Max attribute.");
			return FALSE;
		}
		uh = (USN_HEADER*)page_address(page);
		stamp = get_current_ntfs_time();
		ntfs_debug("Stamping transaction log ($UsnJrnl): old "
				"journal_id 0x%llx, old lowest_valid_usn "
				"0x%llx, new journal_id 0x%llx, new "
				"lowest_valid_usn 0x%llx.",
				(long long)sle64_to_cpu(uh->journal_id),
				(long long)sle64_to_cpu(uh->lowest_valid_usn),
				(long long)sle64_to_cpu(stamp),
				i_size_read(vol->usnjrnl_j_ino));
		uh->lowest_valid_usn =
				cpu_to_sle64(i_size_read(vol->usnjrnl_j_ino));
		uh->journal_id = stamp;
		flush_dcache_page(page);
		set_page_dirty(page);
		ntfs_unmap_page(page);
		/* Set the flag so we do not have to do it again on remount. */
		NVolSetUsnJrnlStamped(vol);
	}
	ntfs_debug("Done.");
	return TRUE;
}

#endif /* NTFS_RW */
