/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SQUASHFS_FS_SB
#define SQUASHFS_FS_SB
/*
 * Squashfs
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
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
	struct squashfs_page_actor	*actor;
};

struct squashfs_sb_info {
	const struct squashfs_decompressor	*decompressor;
	int					devblksize;
	int					devblksize_log2;
	struct squashfs_cache			*block_cache;
	struct squashfs_cache			*fragment_cache;
	struct squashfs_cache			*read_page;
	struct address_space			*cache_mapping;
	int					next_meta_index;
	__le64					*id_table;
	__le64					*fragment_index;
	__le64					*xattr_id_table;
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
	unsigned int				fragments;
	unsigned int				xattr_ids;
	unsigned int				ids;
	bool					panic_on_errors;
	const struct squashfs_decompressor_thread_ops *thread_ops;
	int					max_thread_num;
};
#endif
