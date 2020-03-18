/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#ifndef _INCFS_DATA_MGMT_H
#define _INCFS_DATA_MGMT_H

#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <crypto/hash.h>

#include <uapi/linux/incrementalfs.h>

#include "internal.h"

#define SEGMENTS_PER_FILE 3

struct read_log_record {
	u32 block_index : 31;

	u32 timed_out : 1;

	u64 timestamp_us;

	incfs_uuid_t file_id;
} __packed;

struct read_log_state {
	/* Next slot in rl_ring_buf to write to. */
	u32 next_index;

	/* Current number of writer pass over rl_ring_buf */
	u32 current_pass_no;
};

/* A ring buffer to save records about data blocks which were recently read. */
struct read_log {
	struct read_log_record *rl_ring_buf;

	struct read_log_state rl_state;

	spinlock_t rl_writer_lock;

	int rl_size;

	/*
	 * A queue of waiters who want to be notified about reads.
	 */
	wait_queue_head_t ml_notif_wq;
};

struct mount_options {
	unsigned int read_timeout_ms;
	unsigned int readahead_pages;
	unsigned int read_log_pages;
	unsigned int read_log_wakeup_count;
	bool no_backing_file_cache;
	bool no_backing_file_readahead;
};

struct mount_info {
	struct super_block *mi_sb;

	struct path mi_backing_dir_path;

	struct dentry *mi_index_dir;

	const struct cred *mi_owner;

	struct mount_options mi_options;

	/* This mutex is to be taken before create, rename, delete */
	struct mutex mi_dir_struct_mutex;

	/*
	 * A queue of waiters who want to be notified about new pending reads.
	 */
	wait_queue_head_t mi_pending_reads_notif_wq;

	/*
	 * Protects:
	 *  - reads_list_head
	 *  - mi_pending_reads_count
	 *  - mi_last_pending_read_number
	 *  - data_file_segment.reads_list_head
	 */
	struct mutex mi_pending_reads_mutex;

	/* List of active pending_read objects */
	struct list_head mi_reads_list_head;

	/* Total number of items in reads_list_head */
	int mi_pending_reads_count;

	/*
	 * Last serial number that was assigned to a pending read.
	 * 0 means no pending reads have been seen yet.
	 */
	int mi_last_pending_read_number;

	/* Temporary buffer for read logger. */
	struct read_log mi_log;

	void *log_xattr;
	size_t log_xattr_size;

	void *pending_read_xattr;
	size_t pending_read_xattr_size;
};

struct data_file_block {
	loff_t db_backing_file_data_offset;

	size_t db_stored_size;

	enum incfs_compression_alg db_comp_alg;
};

struct pending_read {
	incfs_uuid_t file_id;

	s64 timestamp_us;

	atomic_t done;

	int block_index;

	int serial_number;

	struct list_head mi_reads_list;

	struct list_head segment_reads_list;
};

struct data_file_segment {
	wait_queue_head_t new_data_arrival_wq;

	/* Protects reads and writes from the blockmap */
	/* Good candidate for read/write mutex */
	struct mutex blockmap_mutex;

	/* List of active pending_read objects belonging to this segment */
	/* Protected by mount_info.pending_reads_mutex */
	struct list_head reads_list_head;
};

/*
 * Extra info associated with a file. Just a few bytes set by a user.
 */
struct file_attr {
	loff_t fa_value_offset;

	size_t fa_value_size;

	u32 fa_crc;
};


struct data_file {
	struct backing_file_context *df_backing_file_context;

	struct mount_info *df_mount_info;

	incfs_uuid_t df_id;

	/*
	 * Array of segments used to reduce lock contention for the file.
	 * Segment is chosen for a block depends on the block's index.
	 */
	struct data_file_segment df_segments[SEGMENTS_PER_FILE];

	/* Base offset of the first metadata record. */
	loff_t df_metadata_off;

	/* Base offset of the block map. */
	loff_t df_blockmap_off;

	/* File size in bytes */
	loff_t df_size;

	/* File header flags */
	u32 df_header_flags;

	/* File size in DATA_FILE_BLOCK_SIZE blocks */
	int df_data_block_count;

	/* Total number of blocks, data + hash */
	int df_total_block_count;

	struct file_attr n_attr;

	struct mtree *df_hash_tree;

	struct incfs_df_signature *df_signature;
};

struct dir_file {
	struct mount_info *mount_info;

	struct file *backing_dir;
};

struct inode_info {
	struct mount_info *n_mount_info; /* A mount, this file belongs to */

	struct inode *n_backing_inode;

	struct data_file *n_file;

	struct inode n_vfs_inode;
};

struct dentry_info {
	struct path backing_path;
};

struct mount_info *incfs_alloc_mount_info(struct super_block *sb,
					  struct mount_options *options,
					  struct path *backing_dir_path);

void incfs_free_mount_info(struct mount_info *mi);

struct data_file *incfs_open_data_file(struct mount_info *mi, struct file *bf);
void incfs_free_data_file(struct data_file *df);

int incfs_scan_metadata_chain(struct data_file *df);

struct dir_file *incfs_open_dir_file(struct mount_info *mi, struct file *bf);
void incfs_free_dir_file(struct dir_file *dir);

ssize_t incfs_read_data_file_block(struct mem_range dst, struct data_file *df,
				   int index, int timeout_ms,
				   struct mem_range tmp);

int incfs_get_filled_blocks(struct data_file *df,
			    struct incfs_get_filled_blocks_args *arg);

int incfs_read_file_signature(struct data_file *df, struct mem_range dst);

int incfs_process_new_data_block(struct data_file *df,
				 struct incfs_fill_block *block, u8 *data);

int incfs_process_new_hash_block(struct data_file *df,
				 struct incfs_fill_block *block, u8 *data);

bool incfs_fresh_pending_reads_exist(struct mount_info *mi, int last_number);

/*
 * Collects pending reads and saves them into the array (reads/reads_size).
 * Only reads with serial_number > sn_lowerbound are reported.
 * Returns how many reads were saved into the array.
 */
int incfs_collect_pending_reads(struct mount_info *mi, int sn_lowerbound,
				struct incfs_pending_read_info *reads,
				int reads_size);

int incfs_collect_logged_reads(struct mount_info *mi,
			       struct read_log_state *start_state,
			       struct incfs_pending_read_info *reads,
			       int reads_size);
struct read_log_state incfs_get_log_state(struct mount_info *mi);
int incfs_get_uncollected_logs_count(struct mount_info *mi,
				     struct read_log_state state);

static inline struct inode_info *get_incfs_node(struct inode *inode)
{
	if (!inode)
		return NULL;

	if (inode->i_sb->s_magic != INCFS_MAGIC_NUMBER) {
		/* This inode doesn't belong to us. */
		pr_warn_once("incfs: %s on an alien inode.", __func__);
		return NULL;
	}

	return container_of(inode, struct inode_info, n_vfs_inode);
}

static inline struct data_file *get_incfs_data_file(struct file *f)
{
	struct inode_info *node = NULL;

	if (!f)
		return NULL;

	if (!S_ISREG(f->f_inode->i_mode))
		return NULL;

	node = get_incfs_node(f->f_inode);
	if (!node)
		return NULL;

	return node->n_file;
}

static inline struct dir_file *get_incfs_dir_file(struct file *f)
{
	if (!f)
		return NULL;

	if (!S_ISDIR(f->f_inode->i_mode))
		return NULL;

	return (struct dir_file *)f->private_data;
}

/*
 * Make sure that inode_info.n_file is initialized and inode can be used
 * for reading and writing data from/to the backing file.
 */
int make_inode_ready_for_data_ops(struct mount_info *mi,
				struct inode *inode,
				struct file *backing_file);

static inline struct dentry_info *get_incfs_dentry(const struct dentry *d)
{
	if (!d)
		return NULL;

	return (struct dentry_info *)d->d_fsdata;
}

static inline void get_incfs_backing_path(const struct dentry *d,
					  struct path *path)
{
	struct dentry_info *di = get_incfs_dentry(d);

	if (!di) {
		*path = (struct path) {};
		return;
	}

	*path = di->backing_path;
	path_get(path);
}

static inline int get_blocks_count_for_size(u64 size)
{
	if (size == 0)
		return 0;
	return 1 + (size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
}

bool incfs_equal_ranges(struct mem_range lhs, struct mem_range rhs);

#endif /* _INCFS_DATA_MGMT_H */
