/**
 * aops.h - Defines for NTFS kernel address space operations and page cache
 *	    handling.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
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

#ifndef _LINUX_NTFS_AOPS_H
#define _LINUX_NTFS_AOPS_H

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/fs.h>

#include "inode.h"

/**
 * ntfs_unmap_page - release a page that was mapped using ntfs_map_page()
 * @page:	the page to release
 *
 * Unpin, unmap and release a page that was obtained from ntfs_map_page().
 */
static inline void ntfs_unmap_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

/**
 * ntfs_map_page - map a page into accessible memory, reading it if necessary
 * @mapping:	address space for which to obtain the page
 * @index:	index into the page cache for @mapping of the page to map
 *
 * Read a page from the page cache of the address space @mapping at position
 * @index, where @index is in units of PAGE_CACHE_SIZE, and not in bytes.
 *
 * If the page is not in memory it is loaded from disk first using the readpage
 * method defined in the address space operations of @mapping and the page is
 * added to the page cache of @mapping in the process.
 *
 * If the page belongs to an mst protected attribute and it is marked as such
 * in its ntfs inode (NInoMstProtected()) the mst fixups are applied but no
 * error checking is performed.  This means the caller has to verify whether
 * the ntfs record(s) contained in the page are valid or not using one of the
 * ntfs_is_XXXX_record{,p}() macros, where XXXX is the record type you are
 * expecting to see.  (For details of the macros, see fs/ntfs/layout.h.)
 *
 * If the page is in high memory it is mapped into memory directly addressible
 * by the kernel.
 *
 * Finally the page count is incremented, thus pinning the page into place.
 *
 * The above means that page_address(page) can be used on all pages obtained
 * with ntfs_map_page() to get the kernel virtual address of the page.
 *
 * When finished with the page, the caller has to call ntfs_unmap_page() to
 * unpin, unmap and release the page.
 *
 * Note this does not grant exclusive access. If such is desired, the caller
 * must provide it independently of the ntfs_{un}map_page() calls by using
 * a {rw_}semaphore or other means of serialization. A spin lock cannot be
 * used as ntfs_map_page() can block.
 *
 * The unlocked and uptodate page is returned on success or an encoded error
 * on failure. Caller has to test for error using the IS_ERR() macro on the
 * return value. If that evaluates to TRUE, the negative error code can be
 * obtained using PTR_ERR() on the return value of ntfs_map_page().
 */
static inline struct page *ntfs_map_page(struct address_space *mapping,
		unsigned long index)
{
	struct page *page = read_cache_page(mapping, index,
			(filler_t*)mapping->a_ops->readpage, NULL);

	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		kmap(page);
		if (PageUptodate(page) && !PageError(page))
			return page;
		ntfs_unmap_page(page);
		return ERR_PTR(-EIO);
	}
	return page;
}

#ifdef NTFS_RW

extern void mark_ntfs_record_dirty(struct page *page, const unsigned int ofs);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_AOPS_H */
