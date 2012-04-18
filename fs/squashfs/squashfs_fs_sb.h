#ifndef SQUASHFS_FS_SB
#define SQUASHFS_FS_SB
/*
 * Squashfs
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * squashfs_fs_sb.h
 */

#include "squashfs_fs.h"

struct squashfs_cache {
	char			*name;
	int			entries;
	int			curr_blk;
	int			next_blk;
	int			num_waiters;
	int			unused;
	int			block_size;
	int			pages;
	spinlock_t		lock;
	wait_queue_head_t	wait_queue;
	struct squashfs_cache_entry *entry;
};

struct squashfs_cache_entry {
	u64			block;
	int			length;
	int			refcount;
	u64			next_index;
	int			pending;
	int			error;
	int			num_waiters;
	wait_queue_head_t	wait_queue;
	struct squashfs_cache	*cache;
	void			**data;
};

struct squashfs_sb_info {
	const struct squashfs_decompressor	*decompressor;
	int					devblksize;
	int					devblksize_log2;
	struct squashfs_cache			*block_cache;
	struct squashfs_cache			*fragment_cache;
	struct squashfs_cache			*read_page;
	int					next_meta_index;
	__le64					*id_table;
	__le64					*fragment_index;
	__le64					*xattr_id_table;
	struct mutex				read_data_mutex;
	struct mutex				meta_index_mutex;
	struct meta_index			*meta_index;
	void					*stream;
	__le64					*inode_lookup_table;
	u64					inode_table;
	u64					directory_table;
	u64					xattr_table;
	unsigned int				block_size;
	unsigned short				block_log;
	long long				bytes_used;
	unsigned int				inodes;
	int					xattr_ids;
};
#endif
