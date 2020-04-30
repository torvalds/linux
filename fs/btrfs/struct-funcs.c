// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <asm/unaligned.h>

#include "ctree.h"

static inline u8 get_unaligned_le8(const void *p)
{
       return *(u8 *)p;
}

static inline void put_unaligned_le8(u8 val, void *p)
{
       *(u8 *)p = val;
}

static bool check_setget_bounds(const struct extent_buffer *eb,
				const void *ptr, unsigned off, int size)
{
	const unsigned long member_offset = (unsigned long)ptr + off;

	if (member_offset > eb->len) {
		btrfs_warn(eb->fs_info,
	"bad eb member start: ptr 0x%lx start %llu member offset %lu size %d",
			(unsigned long)ptr, eb->start, member_offset, size);
		return false;
	}
	if (member_offset + size > eb->len) {
		btrfs_warn(eb->fs_info,
	"bad eb member end: ptr 0x%lx start %llu member offset %lu size %d",
			(unsigned long)ptr, eb->start, member_offset, size);
		return false;
	}

	return true;
}

/*
 * this is some deeply nasty code.
 *
 * The end result is that anyone who #includes ctree.h gets a
 * declaration for the btrfs_set_foo functions and btrfs_foo functions,
 * which are wrappers of btrfs_set_token_#bits functions and
 * btrfs_get_token_#bits functions, which are defined in this file.
 *
 * These setget functions do all the extent_buffer related mapping
 * required to efficiently read and write specific fields in the extent
 * buffers.  Every pointer to metadata items in btrfs is really just
 * an unsigned long offset into the extent buffer which has been
 * cast to a specific type.  This gives us all the gcc type checking.
 *
 * The extent buffer api is used to do the page spanning work required to
 * have a metadata blocksize different from the page size.
 *
 * There are 2 variants defined, one with a token pointer and one without.
 */

#define DEFINE_BTRFS_SETGET_BITS(bits)					\
u##bits btrfs_get_token_##bits(struct btrfs_map_token *token,		\
			       const void *ptr, unsigned long off)	\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = member_offset >> PAGE_SHIFT;		\
	const unsigned long oip = offset_in_page(member_offset);	\
	const int size = sizeof(u##bits);				\
	u8 lebytes[sizeof(u##bits)];					\
	const int part = PAGE_SIZE - oip;				\
									\
	ASSERT(token);							\
	ASSERT(token->kaddr);						\
	ASSERT(check_setget_bounds(token->eb, ptr, off, size));		\
	if (token->offset <= member_offset &&				\
	    member_offset + size <= token->offset + PAGE_SIZE) {	\
		return get_unaligned_le##bits(token->kaddr + oip);	\
	}								\
	token->kaddr = page_address(token->eb->pages[idx]);		\
	token->offset = idx << PAGE_SHIFT;				\
	if (oip + size <= PAGE_SIZE)					\
		return get_unaligned_le##bits(token->kaddr + oip);	\
									\
	memcpy(lebytes, token->kaddr + oip, part);			\
	token->kaddr = page_address(token->eb->pages[idx + 1]);		\
	token->offset = (idx + 1) << PAGE_SHIFT;			\
	memcpy(lebytes + part, token->kaddr, size - part);		\
	return get_unaligned_le##bits(lebytes);				\
}									\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off)		\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long oip = offset_in_page(member_offset);	\
	const unsigned long idx = member_offset >> PAGE_SHIFT;		\
	char *kaddr = page_address(eb->pages[idx]);			\
	const int size = sizeof(u##bits);				\
	const int part = PAGE_SIZE - oip;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	ASSERT(check_setget_bounds(eb, ptr, off, size));		\
	if (oip + size <= PAGE_SIZE)					\
		return get_unaligned_le##bits(kaddr + oip);		\
									\
	memcpy(lebytes, kaddr + oip, part);				\
	kaddr = page_address(eb->pages[idx + 1]);			\
	memcpy(lebytes + part, kaddr, size - part);			\
	return get_unaligned_le##bits(lebytes);				\
}									\
void btrfs_set_token_##bits(struct btrfs_map_token *token,		\
			    const void *ptr, unsigned long off,		\
			    u##bits val)				\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long idx = member_offset >> PAGE_SHIFT;		\
	const unsigned long oip = offset_in_page(member_offset);	\
	const int size = sizeof(u##bits);				\
	__le##bits leres;						\
									\
	ASSERT(token);							\
	ASSERT(token->kaddr);						\
	ASSERT(check_setget_bounds(token->eb, ptr, off, size));		\
	if (token->offset <= member_offset &&				\
	    member_offset + size <= token->offset + PAGE_SIZE) {	\
		put_unaligned_le##bits(val, token->kaddr + oip);	\
		return;							\
	}								\
	if (oip + size <= PAGE_SIZE) {					\
		token->kaddr = page_address(token->eb->pages[idx]);	\
		token->offset = idx << PAGE_SHIFT;			\
		put_unaligned_le##bits(val, token->kaddr + oip);	\
		return;							\
	}								\
	token->kaddr = page_address(token->eb->pages[idx + 1]);		\
	token->offset = (idx + 1) << PAGE_SHIFT;			\
	leres = cpu_to_le##bits(val);					\
	write_extent_buffer(token->eb, &leres, member_offset, size);	\
}									\
void btrfs_set_##bits(const struct extent_buffer *eb, void *ptr,	\
		      unsigned long off, u##bits val)			\
{									\
	const unsigned long member_offset = (unsigned long)ptr + off;	\
	const unsigned long oip = offset_in_page(member_offset);	\
	const unsigned long idx = member_offset >> PAGE_SHIFT;		\
	char *kaddr = page_address(eb->pages[idx]);			\
	const int size = sizeof(u##bits);				\
	const int part = PAGE_SIZE - oip;				\
	u8 lebytes[sizeof(u##bits)];					\
									\
	ASSERT(check_setget_bounds(eb, ptr, off, size));		\
	if (oip + size <= PAGE_SIZE) {					\
		put_unaligned_le##bits(val, kaddr + oip);		\
		return;							\
	}								\
									\
	put_unaligned_le##bits(val, lebytes);				\
	memcpy(kaddr + oip, lebytes, part);				\
	kaddr = page_address(eb->pages[idx + 1]);			\
	memcpy(kaddr, lebytes + part, size - part);			\
}

DEFINE_BTRFS_SETGET_BITS(8)
DEFINE_BTRFS_SETGET_BITS(16)
DEFINE_BTRFS_SETGET_BITS(32)
DEFINE_BTRFS_SETGET_BITS(64)

void btrfs_node_key(const struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr = btrfs_node_key_ptr_offset(nr);
	read_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}
