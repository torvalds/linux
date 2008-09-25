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

#ifndef __BIT_RADIX__
#define __BIT_RADIX__
#include <linux/radix-tree.h>

int set_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int test_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int clear_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int find_first_radix_bit(struct radix_tree_root *radix, unsigned long *retbits,
			 unsigned long start, int nr);

static inline void init_bit_radix(struct radix_tree_root *radix)
{
	INIT_RADIX_TREE(radix, GFP_NOFS);
}
#endif
