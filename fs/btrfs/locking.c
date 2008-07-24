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
#include <asm/bug.h>
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

int btrfs_tree_lock(struct extent_buffer *eb)
{
	int i;

	if (mutex_trylock(&eb->mutex))
		return 0;
	for (i = 0; i < 512; i++) {
		cpu_relax();
		if (mutex_trylock(&eb->mutex))
			return 0;
	}
	cpu_relax();
	mutex_lock_nested(&eb->mutex, BTRFS_MAX_LEVEL - btrfs_header_level(eb));
	return 0;
}

int btrfs_try_tree_lock(struct extent_buffer *eb)
{
	return mutex_trylock(&eb->mutex);
}

int btrfs_tree_unlock(struct extent_buffer *eb)
{
	mutex_unlock(&eb->mutex);
	return 0;
}

int btrfs_tree_locked(struct extent_buffer *eb)
{
	return mutex_is_locked(&eb->mutex);
}

