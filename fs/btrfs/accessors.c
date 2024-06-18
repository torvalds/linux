// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <asm/unaligned.h>
#include "messages.h"
#include "extent_io.h"
#include "fs.h"
#include "accessors.h"

static bool check_setget_bounds(const struct extent_buffer *eb,
				const void *ptr, unsigned off, int size)
{
	const unsigned long member_offset = (unsigned long)ptr + off;

	if (unlikely(member_offset + size > eb->len)) {
		btrfs_warn(eb->fs_info,
		"bad eb member %s: ptr 0x%lx start %llu member offset %lu size %d",
			(member_offset > eb->len ? "start" : "end"),
			(unsigned long)ptr, eb->start, member_offset, size);
		return false;
	}

	return true;
}

void btrfs_init_map_token(struct btrfs_map_token *token, struct extent_buffer *eb)
{
	token->eb = eb;
	token->kaddr = folio_address(eb->folios[0]);
	token->offset = 0;
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
 * Generic helpers with a token (cached address of the most recently accessed
 * page):
 * - btrfs_set_token_8 (for 8/16/32/64)
 * - btrfs_get_token_8 (for 8/16/32/64)
 *
 * The set/get functions handle data spanning two pages transparently, in case
 * metadata block size is larger than page.  Every pointer to metadata items is
 * an offset into the extent buffer page array, cast to a specific type.  This
 * gives us all the type checking.
 *
 * The extent buffer pages stored in the array folios may not form a contiguous
 * phyusical range, but the API functions assume the linear offset to the range
 * from 0 to metadata node size.
 */

#define DEFINE_BTRFS_SETGET_BITS(bits)					\
u##bits btrfs_get_token_##bits(struct btrfs_map_token *token,		\
			       const void *ptr, unsigned long off)	\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(token->eb, member_offset); \
	const unsigned long oil = get_eb_offset_in_folio(token->eb,	\
							 member_offset);\
	const int unit_size = token->eb->folio_size;			\
	const int unit_shift = token->eb->folio_shift;			\
	const int size = sizeof(u##bits);				\
	u8 lebytes[sizeof(u##bits)];					\
	const int part = unit_size - oil;				\
									\
	ASSERT(token);							\
	ASSERT(token->kaddr);						\
	ASSERT(check_setget_bounds(token->eb, ptr, off, size));		\
	if (token->offset <= member_offset &&				\
	    member_offset + size <= token->offset + unit_size) {	\
		return get_unaligned_le##bits(token->kaddr + oil);	\
	}								\
	token->kaddr = folio_address(token->eb->folios[idx]);		\
	token->offset = idx << unit_shift;				\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 || oil + size <= unit_size) \
		return get_unaligned_le##bits(token->kaddr + oil);	\
									\
	memcpy(lebytes, token->kaddr + oil, part);			\
	token->kaddr = folio_address(token->eb->folios[idx + 1]);	\
	token->offset = (idx + 1) << unit_shift;			\
	memcpy(lebytes + part, token->kaddr, size - part);		\
	return get_unaligned_le##bits(lebytes);				\
}									\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off)		\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(eb, member_offset);\
	const unsigned long oil = get_eb_offset_in_folio(eb,		\
							 member_offset);\
	const int unit_size = eb->folio_size;				\
	char *kaddr = folio_address(eb->folios[idx]);			\
	const int size = sizeof(u##bits);				\
	const int part = unit_size - oil;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	ASSERT(check_setget_bounds(eb, ptr, off, size));		\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 || oil + size <= unit_size)	\
		return get_unaligned_le##bits(kaddr + oil);		\
									\
	memcpy(lebytes, kaddr + oil, part);				\
	kaddr = folio_address(eb->folios[idx + 1]);			\
	memcpy(lebytes + part, kaddr, size - part);			\
	return get_unaligned_le##bits(lebytes);				\
}									\
void btrfs_set_token_##bits(struct btrfs_map_token *token,		\
			    const void *ptr, unsigned long off,		\
			    u##bits val)				\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(token->eb, member_offset); \
	const unsigned long oil = get_eb_offset_in_folio(token->eb,	\
							 member_offset);\
	const int unit_size = token->eb->folio_size;			\
	const int unit_shift = token->eb->folio_shift;			\
	const int size = sizeof(u##bits);				\
	u8 lebytes[sizeof(u##bits)];					\
	const int part = unit_size - oil;				\
									\
	ASSERT(token);							\
	ASSERT(token->kaddr);						\
	ASSERT(check_setget_bounds(token->eb, ptr, off, size));		\
	if (token->offset <= member_offset &&				\
	    member_offset + size <= token->offset + unit_size) {	\
		put_unaligned_le##bits(val, token->kaddr + oil);	\
		return;							\
	}								\
	token->kaddr = folio_address(token->eb->folios[idx]);		\
	token->offset = idx << unit_shift;				\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 ||				\
	    oil + size <= unit_size) {					\
		put_unaligned_le##bits(val, token->kaddr + oil);	\
		return;							\
	}								\
	put_unaligned_le##bits(val, lebytes);				\
	memcpy(token->kaddr + oil, lebytes, part);			\
	token->kaddr = folio_address(token->eb->folios[idx + 1]);	\
	token->offset = (idx + 1) << unit_shift;			\
	memcpy(token->kaddr, lebytes + part, size - part);		\
}									\
void btrfs_set_##bits(const struct extent_buffer *eb, void *ptr,	\
		      unsigned long off, u##bits val)			\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = get_eb_folio_index(eb, member_offset);\
	const unsigned long oil = get_eb_offset_in_folio(eb,		\
							 member_offset);\
	const int unit_size = eb->folio_size;				\
	char *kaddr = folio_address(eb->folios[idx]);			\
	const int size = sizeof(u##bits);				\
	const int part = unit_size - oil;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	ASSERT(check_setget_bounds(eb, ptr, off, size));		\
	if (INLINE_EXTENT_BUFFER_PAGES == 1 ||				\
	    oil + size <= unit_size) {					\
		put_unaligned_le##bits(val, kaddr + oil);		\
		return;							\
	}								\
									\
	put_unaligned_le##bits(val, lebytes);				\
	memcpy(kaddr + oil, lebytes, part);				\
	kaddr = folio_address(eb->folios[idx + 1]);			\
	memcpy(kaddr, lebytes + part, size - part);			\
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
