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
#define BTRFS_SETGET_FUNCS(name, type, member, bits)			\
u##bits btrfs_##name(struct extent_buffer *eb,				\
				   type *s)				\
{									\
	unsigned long offset = (unsigned long)s +			\
				offsetof(type, member);			\
	__le##bits *tmp;						\
	/* ugly, but we want the fast path here */			\
	if (eb->map_token && offset >= eb->map_start &&			\
	    offset + sizeof(((type *)0)->member) <= eb->map_start +	\
	    eb->map_len) {						\
		tmp = (__le##bits *)(eb->kaddr + offset -		\
				     eb->map_start);			\
		return le##bits##_to_cpu(*tmp);				\
	}								\
	{								\
		int err;						\
		char *map_token;					\
		char *kaddr;						\
		int unmap_on_exit = (eb->map_token == NULL);		\
		unsigned long map_start;				\
		unsigned long map_len;					\
		__le##bits res;						\
		err = map_extent_buffer(eb, offset,			\
			        sizeof(((type *)0)->member),		\
				&map_token, &kaddr,			\
				&map_start, &map_len, KM_USER1);	\
		if (err) {						\
			read_eb_member(eb, s, type, member, &res);	\
			return le##bits##_to_cpu(res);			\
		}							\
		tmp = (__le##bits *)(kaddr + offset - map_start);	\
		res = le##bits##_to_cpu(*tmp);				\
		if (unmap_on_exit)					\
			unmap_extent_buffer(eb, map_token, KM_USER1);	\
		return res;						\
	}								\
}									\
void btrfs_set_##name(struct extent_buffer *eb,				\
				    type *s, u##bits val)		\
{									\
	unsigned long offset = (unsigned long)s +			\
				offsetof(type, member);			\
	__le##bits *tmp;						\
	/* ugly, but we want the fast path here */			\
	if (eb->map_token && offset >= eb->map_start &&			\
	    offset + sizeof(((type *)0)->member) <= eb->map_start +	\
	    eb->map_len) {						\
		tmp = (__le##bits *)(eb->kaddr + offset -		\
				     eb->map_start);			\
		*tmp = cpu_to_le##bits(val);				\
		return;							\
	}								\
	{								\
		int err;						\
		char *map_token;					\
		char *kaddr;						\
		int unmap_on_exit = (eb->map_token == NULL);		\
		unsigned long map_start;				\
		unsigned long map_len;					\
		err = map_extent_buffer(eb, offset,			\
			        sizeof(((type *)0)->member),		\
				&map_token, &kaddr,			\
				&map_start, &map_len, KM_USER1);	\
		if (err) {						\
			val = cpu_to_le##bits(val);			\
			write_eb_member(eb, s, type, member, &val);	\
			return;						\
		}							\
		tmp = (__le##bits *)(kaddr + offset - map_start);	\
		*tmp = cpu_to_le##bits(val);				\
		if (unmap_on_exit)					\
			unmap_extent_buffer(eb, map_token, KM_USER1);	\
	}								\
}

#include "ctree.h"

