/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
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
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <linux/bug.h>
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

int btrfs_tree_lock(struct extent_buffer *eb)
{
	int i;

	if (!TestSetPageLocked(eb->first_page))
		return 0;
	for (i = 0; i < 512; i++) {
		cpu_relax();
		if (!TestSetPageLocked(eb->first_page))
			return 0;
	}
	cpu_relax();
	lock_page(eb->first_page);
	return 0;
}

int btrfs_try_tree_lock(struct extent_buffer *eb)
{
	return TestSetPageLocked(eb->first_page);
}

int btrfs_tree_unlock(struct extent_buffer *eb)
{
	WARN_ON(!PageLocked(eb->first_page));
	unlock_page(eb->first_page);
	return 0;
}

int btrfs_tree_locked(struct extent_buffer *eb)
{
	return PageLocked(eb->first_page);
}

