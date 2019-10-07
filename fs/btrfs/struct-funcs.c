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
u##bits btrfs_get_token_##bits(const struct extent_buffer *eb,		\
			       const void *ptr, unsigned long off,	\
			       struct btrfs_map_token *token)		\
{									\
	unsigned long part_offset = (unsigned long)ptr;			\
	unsigned long offset = part_offset + off;			\
	void *p;							\
	int err;							\
	char *kaddr;							\
	unsigned long map_start;					\
	unsigned long map_len;						\
	int size = sizeof(u##bits);					\
	u##bits res;							\
									\
	ASSERT(token);							\
	ASSERT(token->eb == eb);					\
									\
	if (token->kaddr && token->offset <= offset &&			\
	   (token->offset + PAGE_SIZE >= offset + size)) {	\
		kaddr = token->kaddr;					\
		p = kaddr + part_offset - token->offset;		\
		res = get_unaligned_le##bits(p + off);			\
		return res;						\
	}								\
	err = map_private_extent_buffer(eb, offset, size,		\
					&kaddr, &map_start, &map_len);	\
	if (err) {							\
		__le##bits leres;					\
									\
		read_extent_buffer(eb, &leres, offset, size);		\
		return le##bits##_to_cpu(leres);			\
	}								\
	p = kaddr + part_offset - map_start;				\
	res = get_unaligned_le##bits(p + off);				\
	token->kaddr = kaddr;						\
	token->offset = map_start;					\
	return res;							\
}									\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off)		\
{									\
	unsigned long part_offset = (unsigned long)ptr;			\
	unsigned long offset = part_offset + off;			\
	void *p;							\
	int err;							\
	char *kaddr;							\
	unsigned long map_start;					\
	unsigned long map_len;						\
	int size = sizeof(u##bits);					\
	u##bits res;							\
									\
	err = map_private_extent_buffer(eb, offset, size,		\
					&kaddr, &map_start, &map_len);	\
	if (err) {							\
		__le##bits leres;					\
									\
		read_extent_buffer(eb, &leres, offset, size);		\
		return le##bits##_to_cpu(leres);			\
	}								\
	p = kaddr + part_offset - map_start;				\
	res = get_unaligned_le##bits(p + off);				\
	return res;							\
}									\
void btrfs_set_token_##bits(struct extent_buffer *eb,			\
			    const void *ptr, unsigned long off,		\
			    u##bits val,				\
			    struct btrfs_map_token *token)		\
{									\
	unsigned long part_offset = (unsigned long)ptr;			\
	unsigned long offset = part_offset + off;			\
	void *p;							\
	int err;							\
	char *kaddr;							\
	unsigned long map_start;					\
	unsigned long map_len;						\
	int size = sizeof(u##bits);					\
									\
	ASSERT(token);							\
	ASSERT(token->eb == eb);					\
									\
	if (token->kaddr && token->offset <= offset &&			\
	   (token->offset + PAGE_SIZE >= offset + size)) {	\
		kaddr = token->kaddr;					\
		p = kaddr + part_offset - token->offset;		\
		put_unaligned_le##bits(val, p + off);			\
		return;							\
	}								\
	err = map_private_extent_buffer(eb, offset, size,		\
			&kaddr, &map_start, &map_len);			\
	if (err) {							\
		__le##bits val2;					\
									\
		val2 = cpu_to_le##bits(val);				\
		write_extent_buffer(eb, &val2, offset, size);		\
		return;							\
	}								\
	p = kaddr + part_offset - map_start;				\
	put_unaligned_le##bits(val, p + off);				\
	token->kaddr = kaddr;						\
	token->offset = map_start;					\
}									\
void btrfs_set_##bits(struct extent_buffer *eb, void *ptr,		\
		      unsigned long off, u##bits val)			\
{									\
	unsigned long part_offset = (unsigned long)ptr;			\
	unsigned long offset = part_offset + off;			\
	void *p;							\
	int err;							\
	char *kaddr;							\
	unsigned long map_start;					\
	unsigned long map_len;						\
	int size = sizeof(u##bits);					\
									\
	err = map_private_extent_buffer(eb, offset, size,		\
			&kaddr, &map_start, &map_len);			\
	if (err) {							\
		__le##bits val2;					\
									\
		val2 = cpu_to_le##bits(val);				\
		write_extent_buffer(eb, &val2, offset, size);		\
		return;							\
	}								\
	p = kaddr + part_offset - map_start;				\
	put_unaligned_le##bits(val, p + off);				\
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
