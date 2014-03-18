/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
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

#ifndef __BTRFS_TESTS
#define __BTRFS_TESTS

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS

#define test_msg(fmt, ...) pr_info("BTRFS: selftest: " fmt, ##__VA_ARGS__)

int btrfs_test_free_space_cache(void);
int btrfs_test_extent_buffer_operations(void);
int btrfs_test_extent_io(void);
int btrfs_test_inodes(void);
int btrfs_init_test_fs(void);
void btrfs_destroy_test_fs(void);
struct inode *btrfs_new_test_inode(void);
#else
static inline int btrfs_test_free_space_cache(void)
{
	return 0;
}
static inline int btrfs_test_extent_buffer_operations(void)
{
	return 0;
}
static inline int btrfs_init_test_fs(void)
{
	return 0;
}
static inline void btrfs_destroy_test_fs(void)
{
}
static inline int btrfs_test_extent_io(void)
{
	return 0;
}
static inline int btrfs_test_inodes(void)
{
	return 0;
}
#endif

#endif
