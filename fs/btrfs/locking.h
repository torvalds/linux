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

#ifndef __BTRFS_LOCKING_
#define __BTRFS_LOCKING_

int btrfs_tree_lock(struct extent_buffer *eb);
int btrfs_tree_unlock(struct extent_buffer *eb);
int btrfs_tree_locked(struct extent_buffer *eb);
int btrfs_try_tree_lock(struct extent_buffer *eb);
#endif
