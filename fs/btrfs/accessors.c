// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/unaligned.h>
#include "messages.h"
#include "extent_io.h"
#include "fs.h"
#include "accessors.h"

static void __cold report_setget_bounds(const struct extent_buffer *eb,
					const void *ptr, unsigned off, int size)
{
	unsigned long member_offset = (unsigned long)ptr + off;

	btrfs_warn(eb->fs_info,
		   "bad eb member %s: ptr 0x%lx start %llu member offset %lu size %d",
		   (member_offset > eb->len ? "start" : "end"),
		   (unsigned long)ptr, eb->start, member_offset, size);
}

/* Copy bytes from @src1 and @src2 to @dest. */
static __always_inline void memcpy_split_src(char *dest, const char *src1,
					     const char *src2, const size_t len1,
					     const size_t total)
{
	memcpy(dest, src1, len1);
	memcpy(dest + len1, src2, total - len1);
}

/*
 * Macro templates that define helpers to read/write extent buffer data of a
 * given size, that are also used via ctree.h for access to item members by
 * specialized helpers.
 *
 * Generic helpers:
 * - btrfs_set_8 (for 8/16/32/64)
 * - btrfs_get_8 (for 8/16/32/64)
 *
 * The set/get functions handle data spanning two pages transparently, in case
 * metadata block size is larger than page.  Every pointer to metadata items is
 * an offset into the extent buffer page array, cast to a specific type.  This
 * gives us all the type checking.
 *
 * The extent buffer pages stored in the array folios may not form a contiguous
 * physical range, but the API functions assume the linear offset to the range
 * from 0 to metadata node size.
 */

#define DEFINE_BTRFS_SETGET_BITS(bits)					\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off)		\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(eb, member_offset);\
	const unsigned long oif = get_eb_offset_in_folio(eb,		\
							 member_offset);\
	char *kaddr = folio_address(eb->folios[idx]) + oif;		\
	const int part = eb->folio_size - oif;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	if (unlikely(member_offset + sizeof(u##bits) > eb->len)) {	\
		report_setget_bounds(eb, ptr, off, sizeof(u##bits));	\
		return 0;						\
	}								\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 || sizeof(u##bits) == 1 ||	\
	    likely(sizeof(u##bits) <= part))				\
		return get_unaligned_le##bits(kaddr);			\
									\
	if (sizeof(u##bits) == 2) {					\
		lebytes[0] = *kaddr;					\
		kaddr = folio_address(eb->folios[idx + 1]);		\
		lebytes[1] = *kaddr;					\
	} else {							\
		memcpy_split_src(lebytes, kaddr,			\
				 folio_address(eb->folios[idx + 1]),	\
				 part, sizeof(u##bits));		\
	}								\
	return get_unaligned_le##bits(lebytes);				\
}									\
void btrfs_set_##bits(const struct extent_buffer *eb, void *ptr,	\
		      unsigned long off, u##bits val)			\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(eb, member_offset);\
	const unsigned long oif = get_eb_offset_in_folio(eb,		\
							 member_offset);\
	char *kaddr = folio_address(eb->folios[idx]) + oif;		\
	const int part = eb->folio_size - oif;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	if (unlikely(member_offset + sizeof(u##bits) > eb->len)) {	\
		report_setget_bounds(eb, ptr, off, sizeof(u##bits));	\
		return;							\
	}								\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 || sizeof(u##bits) == 1 ||	\
	    likely(sizeof(u##bits) <= part)) {				\
		put_unaligned_le##bits(val, kaddr);			\
		return;							\
	}								\
	put_unaligned_le##bits(val, lebytes);				\
	if (sizeof(u##bits) == 2) {					\
		*kaddr = lebytes[0];					\
		kaddr = folio_address(eb->folios[idx + 1]);		\
		*kaddr = lebytes[1];					\
	} else {							\
		memcpy(kaddr, lebytes, part);				\
		kaddr = folio_address(eb->folios[idx + 1]);		\
		memcpy(kaddr, lebytes + part, sizeof(u##bits) - part);	\
	}								\
}

DEFINE_BTRFS_SETGET_BITS(8)
DEFINE_BTRFS_SETGET_BITS(16)
DEFINE_BTRFS_SETGET_BITS(32)
DEFINE_BTRFS_SETGET_BITS(64)

void btrfs_node_key(const struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr = btrfs_node_key_ptr_offset(eb, nr);
	read_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}
