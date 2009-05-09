/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

#ifndef __BTRFS_FREE_SPACE_CACHE
#define __BTRFS_FREE_SPACE_CACHE

int btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
			 u64 bytenr, u64 size);
int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 bytenr, u64 size);
void btrfs_remove_free_space_cache(struct btrfs_block_group_cache
				   *block_group);
u64 btrfs_find_space_for_alloc(struct btrfs_block_group_cache *block_group,
			       u64 offset, u64 bytes, u64 empty_size);
void btrfs_dump_free_space(struct btrfs_block_group_cache *block_group,
			   u64 bytes);
u64 btrfs_block_group_free_space(struct btrfs_block_group_cache *block_group);
int btrfs_find_space_cluster(struct btrfs_trans_handle *trans,
			     struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster,
			     u64 offset, u64 bytes, u64 empty_size);
void btrfs_init_free_cluster(struct btrfs_free_cluster *cluster);
u64 btrfs_alloc_from_cluster(struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster, u64 bytes,
			     u64 min_start);
int btrfs_return_cluster_to_free_space(
			       struct btrfs_block_group_cache *block_group,
			       struct btrfs_free_cluster *cluster);
#endif
