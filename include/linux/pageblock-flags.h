/*
 * Macros for manipulating and testing flags related to a
 * MAX_ORDER_NR_PAGES block of pages.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Original author, Mel Gorman
 * Major cleanups and reduction of bit operations, Andy Whitcroft
 */
#ifndef PAGEBLOCK_FLAGS_H
#define PAGEBLOCK_FLAGS_H

#include <linux/types.h>

/* Macro to aid the definition of ranges of bits */
#define PB_range(name, required_bits) \
	name, name ## _end = (name + required_bits) - 1

/* Bit indices that affect a whole block of pages */
enum pageblock_bits {
	PB_range(PB_migrate, 2), /* 2 bits required for migrate types */
	NR_PAGEBLOCK_BITS
};

/* Forward declaration */
struct page;

/* Declarations for getting and setting flags. See mm/page_alloc.c */
unsigned long get_pageblock_flags_group(struct page *page,
					int start_bitidx, int end_bitidx);
void set_pageblock_flags_group(struct page *page, unsigned long flags,
					int start_bitidx, int end_bitidx);

#define get_pageblock_flags(page) \
			get_pageblock_flags_group(page, 0, NR_PAGEBLOCK_BITS-1)
#define set_pageblock_flags(page) \
			set_pageblock_flags_group(page, 0, NR_PAGEBLOCK_BITS-1)

#endif	/* PAGEBLOCK_FLAGS_H */
