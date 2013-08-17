/*
 * attrib.h - Defines for attribute handling in NTFS Linux kernel driver.
 *	      Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
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

#ifndef _LINUX_NTFS_ATTRIB_H
#define _LINUX_NTFS_ATTRIB_H

#include "endian.h"
#include "types.h"
#include "layout.h"
#include "inode.h"
#include "runlist.h"
#include "volume.h"

/**
 * ntfs_attr_search_ctx - used in attribute search functions
 * @mrec:	buffer containing mft record to search
 * @attr:	attribute record in @mrec where to begin/continue search
 * @is_first:	if true ntfs_attr_lookup() begins search with @attr, else after
 *
 * Structure must be initialized to zero before the first call to one of the
 * attribute search functions. Initialize @mrec to point to the mft record to
 * search, and @attr to point to the first attribute within @mrec (not necessary
 * if calling the _first() functions), and set @is_first to 'true' (not necessary
 * if calling the _first() functions).
 *
 * If @is_first is 'true', the search begins with @attr. If @is_first is 'false',
 * the search begins after @attr. This is so that, after the first call to one
 * of the search attribute functions, we can call the function again, without
 * any modification of the search context, to automagically get the next
 * matching attribute.
 */
typedef struct {
	MFT_RECORD *mrec;
	ATTR_RECORD *attr;
	bool is_first;
	ntfs_inode *ntfs_ino;
	ATTR_LIST_ENTRY *al_entry;
	ntfs_inode *base_ntfs_ino;
	MFT_RECORD *base_mrec;
	ATTR_RECORD *base_attr;
} ntfs_attr_search_ctx;

extern int ntfs_map_runlist_nolock(ntfs_inode *ni, VCN vcn,
		ntfs_attr_search_ctx *ctx);
extern int ntfs_map_runlist(ntfs_inode *ni, VCN vcn);

extern LCN ntfs_attr_vcn_to_lcn_nolock(ntfs_inode *ni, const VCN vcn,
		const bool write_locked);

extern runlist_element *ntfs_attr_find_vcn_nolock(ntfs_inode *ni,
		const VCN vcn, ntfs_attr_search_ctx *ctx);

int ntfs_attr_lookup(const ATTR_TYPE type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		ntfs_attr_search_ctx *ctx);

extern int load_attribute_list(ntfs_volume *vol, runlist *rl, u8 *al_start,
		const s64 size, const s64 initialized_size);

static inline s64 ntfs_attr_size(const ATTR_RECORD *a)
{
	if (!a->non_resident)
		return (s64)le32_to_cpu(a->data.resident.value_length);
	return sle64_to_cpu(a->data.non_resident.data_size);
}

extern void ntfs_attr_reinit_search_ctx(ntfs_attr_search_ctx *ctx);
extern ntfs_attr_search_ctx *ntfs_attr_get_search_ctx(ntfs_inode *ni,
		MFT_RECORD *mrec);
extern void ntfs_attr_put_search_ctx(ntfs_attr_search_ctx *ctx);

#ifdef NTFS_RW

extern int ntfs_attr_size_bounds_check(const ntfs_volume *vol,
		const ATTR_TYPE type, const s64 size);
extern int ntfs_attr_can_be_non_resident(const ntfs_volume *vol,
		const ATTR_TYPE type);
extern int ntfs_attr_can_be_resident(const ntfs_volume *vol,
		const ATTR_TYPE type);

extern int ntfs_attr_record_resize(MFT_RECORD *m, ATTR_RECORD *a, u32 new_size);
extern int ntfs_resident_attr_value_resize(MFT_RECORD *m, ATTR_RECORD *a,
		const u32 new_size);

extern int ntfs_attr_make_non_resident(ntfs_inode *ni, const u32 data_size);

extern s64 ntfs_attr_extend_allocation(ntfs_inode *ni, s64 new_alloc_size,
		const s64 new_data_size, const s64 data_start);

extern int ntfs_attr_set(ntfs_inode *ni, const s64 ofs, const s64 cnt,
		const u8 val);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_ATTRIB_H */
