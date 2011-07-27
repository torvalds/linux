/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/highmem.h>

/* this is some deeply nasty code.  ctree.h has a different
 * definition for this BTRFS_SETGET_FUNCS macro, behind a #ifndef
 *
 * The end result is that anyone who #includes ctree.h gets a
 * declaration for the btrfs_set_foo functions and btrfs_foo functions
 *
 * This file declares the macros and then #includes ctree.h, which results
 * in cpp creating the function here based on the template below.
 *
 * These setget functions do all the extent_buffer related mapping
 * required to efficiently read and write specific fields in the extent
 * buffers.  Every pointer to metadata items in btrfs is really just
 * an unsigned long offset into the extent buffer which has been
 * cast to a specific type.  This gives us all the gcc type checking.
 *
 * The extent buffer api is used to do all the kmapping and page
 * spanning work required to get extent buffers in highmem and have
 * a metadata blocksize different from the page size.
 *
 * The macro starts with a simple function prototype declaration so that
 * sparse won't complain about it being static.
 */

#define BTRFS_SETGET_FUNCS(name, type, member, bits)			\
u##bits btrfs_##name(struct extent_buffer *eb, type *s);		\
void btrfs_set_##name(struct extent_buffer *eb, type *s, u##bits val);	\
u##bits btrfs_##name(struct extent_buffer *eb,				\
				   type *s)				\
{									\
	unsigned long part_offset = (unsigned long)s;			\
	unsigned long offset = part_offset + offsetof(type, member);	\
	type *p;							\
	int err;						\
	char *kaddr;						\
	unsigned long map_start;				\
	unsigned long map_len;					\
	u##bits res;						\
	err = map_private_extent_buffer(eb, offset,		\
			sizeof(((type *)0)->member),		\
			&kaddr, &map_start, &map_len);		\
	if (err) {						\
		__le##bits leres;				\
		read_eb_member(eb, s, type, member, &leres);	\
		return le##bits##_to_cpu(leres);		\
	}							\
	p = (type *)(kaddr + part_offset - map_start);		\
	res = le##bits##_to_cpu(p->member);			\
	return res;						\
}									\
void btrfs_set_##name(struct extent_buffer *eb,				\
				    type *s, u##bits val)		\
{									\
	unsigned long part_offset = (unsigned long)s;			\
	unsigned long offset = part_offset + offsetof(type, member);	\
	type *p;							\
	int err;						\
	char *kaddr;						\
	unsigned long map_start;				\
	unsigned long map_len;					\
	err = map_private_extent_buffer(eb, offset,		\
			sizeof(((type *)0)->member),		\
			&kaddr, &map_start, &map_len);		\
	if (err) {						\
		__le##bits val2;				\
		val2 = cpu_to_le##bits(val);			\
		write_eb_member(eb, s, type, member, &val2);	\
		return;						\
	}							\
	p = (type *)(kaddr + part_offset - map_start);		\
	p->member = cpu_to_le##bits(val);			\
}

#include "ctree.h"

void btrfs_node_key(struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr = btrfs_node_key_ptr_offset(nr);
	read_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}
