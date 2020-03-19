// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/lz4.h>
#include <linux/crc32.h>

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"

struct mount_info *incfs_alloc_mount_info(struct super_block *sb,
					  struct mount_options *options,
					  struct path *backing_dir_path)
{
	struct mount_info *mi = NULL;
	int error = 0;

	mi = kzalloc(sizeof(*mi), GFP_NOFS);
	if (!mi)
		return ERR_PTR(-ENOMEM);

	mi->mi_sb = sb;
	mi->mi_options = *options;
	mi->mi_backing_dir_path = *backing_dir_path;
	mi->mi_owner = get_current_cred();
	path_get(&mi->mi_backing_dir_path);
	mutex_init(&mi->mi_dir_struct_mutex);
	mutex_init(&mi->mi_pending_reads_mutex);
	init_waitqueue_head(&mi->mi_pending_reads_notif_wq);
	INIT_LIST_HEAD(&mi->mi_reads_list_head);

	if (options->read_log_pages != 0) {
		size_t buf_size = PAGE_SIZE * options->read_log_pages;

		spin_lock_init(&mi->mi_log.rl_writer_lock);
		init_waitqueue_head(&mi->mi_log.ml_notif_wq);

		mi->mi_log.rl_size = buf_size / sizeof(*mi->mi_log.rl_ring_buf);
		mi->mi_log.rl_ring_buf = kzalloc(buf_size, GFP_NOFS);
		if (!mi->mi_log.rl_ring_buf) {
			error = -ENOMEM;
			goto err;
		}
	}

	return mi;

err:
	incfs_free_mount_info(mi);
	return ERR_PTR(error);
}

void incfs_free_mount_info(struct mount_info *mi)
{
	if (!mi)
		return;

	dput(mi->mi_index_dir);
	path_put(&mi->mi_backing_dir_path);
	mutex_destroy(&mi->mi_dir_struct_mutex);
	mutex_destroy(&mi->mi_pending_reads_mutex);
	put_cred(mi->mi_owner);
	kfree(mi->mi_log.rl_ring_buf);
	kfree(mi->log_xattr);
	kfree(mi->pending_read_xattr);
	kfree(mi);
}

static void data_file_segment_init(struct data_file_segment *segment)
{
	init_waitqueue_head(&segment->new_data_arrival_wq);
	mutex_init(&segment->blockmap_mutex);
	INIT_LIST_HEAD(&segment->reads_list_head);
}

static void data_file_segment_destroy(struct data_file_segment *segment)
{
	mutex_destroy(&segment->blockmap_mutex);
}

struct data_file *incfs_open_data_file(struct mount_info *mi, struct file *bf)
{
	struct data_file *df = NULL;
	struct backing_file_context *bfc = NULL;
	int md_records;
	u64 size;
	int error = 0;
	int i;

	if (!bf || !mi)
		return ERR_PTR(-EFAULT);

	if (!S_ISREG(bf->f_inode->i_mode))
		return ERR_PTR(-EBADF);

	bfc = incfs_alloc_bfc(bf);
	if (IS_ERR(bfc))
		return ERR_CAST(bfc);

	df = kzalloc(sizeof(*df), GFP_NOFS);
	if (!df) {
		error = -ENOMEM;
		goto out;
	}

	df->df_backing_file_context = bfc;
	df->df_mount_info = mi;
	for (i = 0; i < ARRAY_SIZE(df->df_segments); i++)
		data_file_segment_init(&df->df_segments[i]);

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		goto out;
	error = incfs_read_file_header(bfc, &df->df_metadata_off,
					&df->df_id, &size);
	mutex_unlock(&bfc->bc_mutex);

	if (error)
		goto out;

	df->df_size = size;
	if (size > 0)
		df->df_block_count = get_blocks_count_for_size(size);

	md_records = incfs_scan_metadata_chain(df);
	if (md_records < 0)
		error = md_records;

out:
	if (error) {
		incfs_free_bfc(bfc);
		df->df_backing_file_context = NULL;
		incfs_free_data_file(df);
		return ERR_PTR(error);
	}
	return df;
}

void incfs_free_data_file(struct data_file *df)
{
	int i;

	if (!df)
		return;

	incfs_free_mtree(df->df_hash_tree);
	for (i = 0; i < ARRAY_SIZE(df->df_segments); i++)
		data_file_segment_destroy(&df->df_segments[i]);
	incfs_free_bfc(df->df_backing_file_context);
	kfree(df);
}

int make_inode_ready_for_data_ops(struct mount_info *mi,
				struct inode *inode,
				struct file *backing_file)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df = NULL;
	int err = 0;

	inode_lock(inode);
	if (S_ISREG(inode->i_mode)) {
		if (!node->n_file) {
			df = incfs_open_data_file(mi, backing_file);

			if (IS_ERR(df))
				err = PTR_ERR(df);
			else
				node->n_file = df;
		}
	} else
		err = -EBADF;
	inode_unlock(inode);
	return err;
}

struct dir_file *incfs_open_dir_file(struct mount_info *mi, struct file *bf)
{
	struct dir_file *dir = NULL;

	if (!S_ISDIR(bf->f_inode->i_mode))
		return ERR_PTR(-EBADF);

	dir = kzalloc(sizeof(*dir), GFP_NOFS);
	if (!dir)
		return ERR_PTR(-ENOMEM);

	dir->backing_dir = get_file(bf);
	dir->mount_info = mi;
	return dir;
}

void incfs_free_dir_file(struct dir_file *dir)
{
	if (!dir)
		return;
	if (dir->backing_dir)
		fput(dir->backing_dir);
	kfree(dir);
}

static ssize_t decompress(struct mem_range src, struct mem_range dst)
{
	int result = LZ4_decompress_safe(src.data, dst.data, src.len, dst.len);

	if (result < 0)
		return -EBADMSG;

	return result;
}

static void log_block_read(struct mount_info *mi, incfs_uuid_t *id,
			int block_index, bool timed_out)
{
	struct read_log *log = &mi->mi_log;
	struct read_log_state state;
	s64 now_us = ktime_to_us(ktime_get());
	struct read_log_record record = {
		.file_id = *id,
		.block_index = block_index,
		.timed_out = timed_out,
		.timestamp_us = now_us
	};

	if (log->rl_size == 0)
		return;

	spin_lock(&log->rl_writer_lock);
	state = READ_ONCE(log->rl_state);
	log->rl_ring_buf[state.next_index] = record;
	if (++state.next_index == log->rl_size) {
		state.next_index = 0;
		++state.current_pass_no;
	}
	WRITE_ONCE(log->rl_state, state);
	spin_unlock(&log->rl_writer_lock);

	wake_up_all(&log->ml_notif_wq);
}

static int validate_hash_tree(struct file *bf, struct data_file *df,
			      int block_index, struct mem_range data, u8 *buf)
{
	u8 digest[INCFS_MAX_HASH_SIZE] = {};
	struct mtree *tree = NULL;
	struct incfs_df_signature *sig = NULL;
	struct mem_range calc_digest_rng;
	struct mem_range saved_digest_rng;
	struct mem_range root_hash_rng;
	int digest_size;
	int hash_block_index = block_index;
	int hash_per_block;
	int lvl = 0;
	int res;

	tree = df->df_hash_tree;
	sig = df->df_signature;
	if (!tree || !sig)
		return 0;

	digest_size = tree->alg->digest_size;
	hash_per_block = INCFS_DATA_FILE_BLOCK_SIZE / digest_size;
	calc_digest_rng = range(digest, digest_size);
	res = incfs_calc_digest(tree->alg, data, calc_digest_rng);
	if (res)
		return res;

	for (lvl = 0; lvl < tree->depth; lvl++) {
		loff_t lvl_off =
			tree->hash_level_suboffset[lvl] + sig->hash_offset;
		loff_t hash_block_off = lvl_off +
			round_down(hash_block_index * digest_size,
				INCFS_DATA_FILE_BLOCK_SIZE);
		size_t hash_off_in_block = hash_block_index * digest_size
			% INCFS_DATA_FILE_BLOCK_SIZE;
		struct mem_range buf_range = range(buf,
					INCFS_DATA_FILE_BLOCK_SIZE);
		ssize_t read_res = incfs_kread(bf, buf,
				INCFS_DATA_FILE_BLOCK_SIZE, hash_block_off);

		if (read_res < 0)
			return read_res;
		if (read_res != INCFS_DATA_FILE_BLOCK_SIZE)
			return -EIO;

		saved_digest_rng = range(buf + hash_off_in_block, digest_size);
		if (!incfs_equal_ranges(calc_digest_rng, saved_digest_rng)) {
			int i;
			bool zero = true;

			pr_debug("incfs: Hash mismatch lvl:%d blk:%d\n",
				lvl, block_index);
			for (i = 0; i < saved_digest_rng.len; ++i)
				if (saved_digest_rng.data[i]) {
					zero = false;
					break;
				}

			if (zero)
				pr_debug("incfs: Note saved_digest all zero - did you forget to load the hashes?\n");
			return -EBADMSG;
		}

		res = incfs_calc_digest(tree->alg, buf_range, calc_digest_rng);
		if (res)
			return res;
		hash_block_index /= hash_per_block;
	}

	root_hash_rng = range(tree->root_hash, digest_size);
	if (!incfs_equal_ranges(calc_digest_rng, root_hash_rng)) {
		pr_debug("incfs: Root hash mismatch blk:%d\n", block_index);
		return -EBADMSG;
	}
	return 0;
}

static struct data_file_segment *get_file_segment(struct data_file *df,
						  int block_index)
{
	int seg_idx = block_index % ARRAY_SIZE(df->df_segments);

	return &df->df_segments[seg_idx];
}

static bool is_data_block_present(struct data_file_block *block)
{
	return (block->db_backing_file_data_offset != 0) &&
	       (block->db_stored_size != 0);
}

static int get_data_file_block(struct data_file *df, int index,
			       struct data_file_block *res_block)
{
	struct incfs_blockmap_entry bme = {};
	struct backing_file_context *bfc = NULL;
	loff_t blockmap_off = 0;
	u16 flags = 0;
	int error = 0;

	if (!df || !res_block)
		return -EFAULT;

	blockmap_off = df->df_blockmap_off;
	bfc = df->df_backing_file_context;

	if (index < 0 || index >= df->df_block_count || blockmap_off == 0)
		return -EINVAL;

	error = incfs_read_blockmap_entry(bfc, index, blockmap_off, &bme);
	if (error)
		return error;

	flags = le16_to_cpu(bme.me_flags);
	res_block->db_backing_file_data_offset =
		le16_to_cpu(bme.me_data_offset_hi);
	res_block->db_backing_file_data_offset <<= 32;
	res_block->db_backing_file_data_offset |=
		le32_to_cpu(bme.me_data_offset_lo);
	res_block->db_stored_size = le16_to_cpu(bme.me_data_size);
	res_block->db_comp_alg = (flags & INCFS_BLOCK_COMPRESSED_LZ4) ?
					 COMPRESSION_LZ4 :
					 COMPRESSION_NONE;
	return 0;
}

static bool is_read_done(struct pending_read *read)
{
	return atomic_read_acquire(&read->done) != 0;
}

static void set_read_done(struct pending_read *read)
{
	atomic_set_release(&read->done, 1);
}

/*
 * Notifies a given data file about pending read from a given block.
 * Returns a new pending read entry.
 */
static struct pending_read *add_pending_read(struct data_file *df,
					     int block_index)
{
	struct pending_read *result = NULL;
	struct data_file_segment *segment = NULL;
	struct mount_info *mi = NULL;

	segment = get_file_segment(df, block_index);
	mi = df->df_mount_info;

	result = kzalloc(sizeof(*result), GFP_NOFS);
	if (!result)
		return NULL;

	result->file_id = df->df_id;
	result->block_index = block_index;
	result->timestamp_us = ktime_to_us(ktime_get());

	mutex_lock(&mi->mi_pending_reads_mutex);

	result->serial_number = ++mi->mi_last_pending_read_number;
	mi->mi_pending_reads_count++;

	list_add(&result->mi_reads_list, &mi->mi_reads_list_head);
	list_add(&result->segment_reads_list, &segment->reads_list_head);
	mutex_unlock(&mi->mi_pending_reads_mutex);

	wake_up_all(&mi->mi_pending_reads_notif_wq);
	return result;
}

/* Notifies a given data file that pending read is completed. */
static void remove_pending_read(struct data_file *df, struct pending_read *read)
{
	struct mount_info *mi = NULL;

	if (!df || !read) {
		WARN_ON(!df);
		WARN_ON(!read);
		return;
	}

	mi = df->df_mount_info;

	mutex_lock(&mi->mi_pending_reads_mutex);
	list_del(&read->mi_reads_list);
	list_del(&read->segment_reads_list);

	mi->mi_pending_reads_count--;
	mutex_unlock(&mi->mi_pending_reads_mutex);

	kfree(read);
}

static void notify_pending_reads(struct mount_info *mi,
		struct data_file_segment *segment,
		int index)
{
	struct pending_read *entry = NULL;

	/* Notify pending reads waiting for this block. */
	mutex_lock(&mi->mi_pending_reads_mutex);
	list_for_each_entry(entry, &segment->reads_list_head,
						segment_reads_list) {
		if (entry->block_index == index)
			set_read_done(entry);
	}
	mutex_unlock(&mi->mi_pending_reads_mutex);
	wake_up_all(&segment->new_data_arrival_wq);
}

static int wait_for_data_block(struct data_file *df, int block_index,
			       int timeout_ms,
			       struct data_file_block *res_block)
{
	struct data_file_block block = {};
	struct data_file_segment *segment = NULL;
	struct pending_read *read = NULL;
	struct mount_info *mi = NULL;
	int error = 0;
	int wait_res = 0;

	if (!df || !res_block)
		return -EFAULT;

	if (block_index < 0 || block_index >= df->df_block_count)
		return -EINVAL;

	if (df->df_blockmap_off <= 0)
		return -ENODATA;

	segment = get_file_segment(df, block_index);
	error = mutex_lock_interruptible(&segment->blockmap_mutex);
	if (error)
		return error;

	/* Look up the given block */
	error = get_data_file_block(df, block_index, &block);

	/* If it's not found, create a pending read */
	if (!error && !is_data_block_present(&block) && timeout_ms != 0)
		read = add_pending_read(df, block_index);

	mutex_unlock(&segment->blockmap_mutex);
	if (error)
		return error;

	/* If the block was found, just return it. No need to wait. */
	if (is_data_block_present(&block)) {
		*res_block = block;
		return 0;
	}

	mi = df->df_mount_info;

	if (timeout_ms == 0) {
		log_block_read(mi, &df->df_id, block_index,
			       true /*timed out*/);
		return -ETIME;
	}

	if (!read)
		return -ENOMEM;

	/* Wait for notifications about block's arrival */
	wait_res =
		wait_event_interruptible_timeout(segment->new_data_arrival_wq,
						 (is_read_done(read)),
						 msecs_to_jiffies(timeout_ms));

	/* Woke up, the pending read is no longer needed. */
	remove_pending_read(df, read);
	read = NULL;

	if (wait_res == 0) {
		/* Wait has timed out */
		log_block_read(mi, &df->df_id, block_index,
			       true /*timed out*/);
		return -ETIME;
	}
	if (wait_res < 0) {
		/*
		 * Only ERESTARTSYS is really expected here when a signal
		 * comes while we wait.
		 */
		return wait_res;
	}

	error = mutex_lock_interruptible(&segment->blockmap_mutex);
	if (error)
		return error;

	/*
	 * Re-read block's info now, it has just arrived and
	 * should be available.
	 */
	error = get_data_file_block(df, block_index, &block);
	if (!error) {
		if (is_data_block_present(&block))
			*res_block = block;
		else {
			/*
			 * Somehow wait finished successfully bug block still
			 * can't be found. It's not normal.
			 */
			pr_warn("incfs:Wait succeeded, but block not found.\n");
			error = -ENODATA;
		}
	}

	mutex_unlock(&segment->blockmap_mutex);
	return error;
}

ssize_t incfs_read_data_file_block(struct mem_range dst, struct data_file *df,
				   int index, int timeout_ms,
				   struct mem_range tmp)
{
	loff_t pos;
	ssize_t result;
	size_t bytes_to_read;
	struct mount_info *mi = NULL;
	struct file *bf = NULL;
	struct data_file_block block = {};

	if (!dst.data || !df)
		return -EFAULT;

	if (tmp.len < 2 * INCFS_DATA_FILE_BLOCK_SIZE)
		return -ERANGE;

	mi = df->df_mount_info;
	bf = df->df_backing_file_context->bc_file;

	result = wait_for_data_block(df, index, timeout_ms, &block);
	if (result < 0)
		goto out;

	pos = block.db_backing_file_data_offset;
	if (block.db_comp_alg == COMPRESSION_NONE) {
		bytes_to_read = min(dst.len, block.db_stored_size);
		result = incfs_kread(bf, dst.data, bytes_to_read, pos);

		/* Some data was read, but not enough */
		if (result >= 0 && result != bytes_to_read)
			result = -EIO;
	} else {
		bytes_to_read = min(tmp.len, block.db_stored_size);
		result = incfs_kread(bf, tmp.data, bytes_to_read, pos);
		if (result == bytes_to_read) {
			result =
				decompress(range(tmp.data, bytes_to_read), dst);
			if (result < 0) {
				const char *name =
					bf->f_path.dentry->d_name.name;

				pr_warn_once("incfs: Decompression error. %s",
					     name);
			}
		} else if (result >= 0) {
			/* Some data was read, but not enough */
			result = -EIO;
		}
	}

	if (result > 0) {
		int err = validate_hash_tree(bf, df, index, dst, tmp.data);

		if (err < 0)
			result = err;
	}

	if (result >= 0)
		log_block_read(mi, &df->df_id, index, false /*timed out*/);

out:
	return result;
}

int incfs_process_new_data_block(struct data_file *df,
				 struct incfs_fill_block *block, u8 *data)
{
	struct mount_info *mi = NULL;
	struct backing_file_context *bfc = NULL;
	struct data_file_segment *segment = NULL;
	struct data_file_block existing_block = {};
	u16 flags = 0;
	int error = 0;

	if (!df || !block)
		return -EFAULT;

	bfc = df->df_backing_file_context;
	mi = df->df_mount_info;

	if (block->block_index >= df->df_block_count)
		return -ERANGE;

	segment = get_file_segment(df, block->block_index);
	if (!segment)
		return -EFAULT;
	if (block->compression == COMPRESSION_LZ4)
		flags |= INCFS_BLOCK_COMPRESSED_LZ4;

	error = mutex_lock_interruptible(&segment->blockmap_mutex);
	if (error)
		return error;

	error = get_data_file_block(df, block->block_index, &existing_block);
	if (error)
		goto unlock;
	if (is_data_block_present(&existing_block)) {
		/* Block is already present, nothing to do here */
		goto unlock;
	}

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (!error) {
		error = incfs_write_data_block_to_backing_file(
			bfc, range(data, block->data_len), block->block_index,
			df->df_blockmap_off, flags);
		mutex_unlock(&bfc->bc_mutex);
	}
	if (!error)
		notify_pending_reads(mi, segment, block->block_index);

unlock:
	mutex_unlock(&segment->blockmap_mutex);
	if (error)
		pr_debug("incfs: %s %d error: %d\n", __func__,
				block->block_index, error);
	return error;
}

int incfs_read_file_signature(struct data_file *df, struct mem_range dst)
{
	struct file *bf = df->df_backing_file_context->bc_file;
	struct incfs_df_signature *sig;
	int read_res = 0;

	if (!dst.data)
		return -EFAULT;

	sig = df->df_signature;
	if (!sig)
		return 0;

	if (dst.len < sig->sig_size)
		return -E2BIG;

	read_res = incfs_kread(bf, dst.data, sig->sig_size, sig->sig_offset);

	if (read_res < 0)
		return read_res;

	if (read_res != sig->sig_size)
		return -EIO;

	return read_res;
}

int incfs_process_new_hash_block(struct data_file *df,
				 struct incfs_fill_block *block, u8 *data)
{
	struct backing_file_context *bfc = NULL;
	struct mount_info *mi = NULL;
	struct mtree *hash_tree = NULL;
	struct incfs_df_signature *sig = NULL;
	loff_t hash_area_base = 0;
	loff_t hash_area_size = 0;
	int error = 0;

	if (!df || !block)
		return -EFAULT;

	if (!(block->flags & INCFS_BLOCK_FLAGS_HASH))
		return -EINVAL;

	bfc = df->df_backing_file_context;
	mi = df->df_mount_info;

	if (!df)
		return -ENOENT;

	hash_tree = df->df_hash_tree;
	sig = df->df_signature;
	if (!hash_tree || !sig || sig->hash_offset == 0)
		return -ENOTSUPP;

	hash_area_base = sig->hash_offset;
	hash_area_size = sig->hash_size;
	if (hash_area_size < block->block_index * INCFS_DATA_FILE_BLOCK_SIZE
				+ block->data_len) {
		/* Hash block goes beyond dedicated hash area of this file. */
		return -ERANGE;
	}

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (!error)
		error = incfs_write_hash_block_to_backing_file(
			bfc, range(data, block->data_len), block->block_index,
			hash_area_base);
	mutex_unlock(&bfc->bc_mutex);
	return error;
}

static int process_blockmap_md(struct incfs_blockmap *bm,
			       struct metadata_handler *handler)
{
	struct data_file *df = handler->context;
	int error = 0;
	loff_t base_off = le64_to_cpu(bm->m_base_offset);
	u32 block_count = le32_to_cpu(bm->m_block_count);

	if (!df)
		return -EFAULT;

	if (df->df_block_count != block_count)
		return -EBADMSG;

	df->df_blockmap_off = base_off;
	return error;
}

static int process_file_attr_md(struct incfs_file_attr *fa,
				struct metadata_handler *handler)
{
	struct data_file *df = handler->context;
	u16 attr_size = le16_to_cpu(fa->fa_size);

	if (!df)
		return -EFAULT;

	if (attr_size > INCFS_MAX_FILE_ATTR_SIZE)
		return -E2BIG;

	df->n_attr.fa_value_offset = le64_to_cpu(fa->fa_offset);
	df->n_attr.fa_value_size = attr_size;
	df->n_attr.fa_crc = le32_to_cpu(fa->fa_crc);

	return 0;
}

static int process_file_signature_md(struct incfs_file_signature *sg,
				struct metadata_handler *handler)
{
	struct data_file *df = handler->context;
	struct mtree *hash_tree = NULL;
	int error = 0;
	struct incfs_df_signature *signature =
		kzalloc(sizeof(*signature), GFP_NOFS);
	void *buf = NULL;
	ssize_t read;

	if (!df || !df->df_backing_file_context ||
	    !df->df_backing_file_context->bc_file) {
		error = -ENOENT;
		goto out;
	}

	signature->hash_offset = le64_to_cpu(sg->sg_hash_tree_offset);
	signature->hash_size = le32_to_cpu(sg->sg_hash_tree_size);
	signature->sig_offset = le64_to_cpu(sg->sg_sig_offset);
	signature->sig_size = le32_to_cpu(sg->sg_sig_size);

	buf = kzalloc(signature->sig_size, GFP_NOFS);
	if (!buf) {
		error = -ENOMEM;
		goto out;
	}

	read = incfs_kread(df->df_backing_file_context->bc_file, buf,
			   signature->sig_size, signature->sig_offset);
	if (read < 0) {
		error = read;
		goto out;
	}

	if (read != signature->sig_size) {
		error = -EINVAL;
		goto out;
	}

	hash_tree = incfs_alloc_mtree(range(buf, signature->sig_size),
				      df->df_block_count);
	if (IS_ERR(hash_tree)) {
		error = PTR_ERR(hash_tree);
		hash_tree = NULL;
		goto out;
	}
	if (hash_tree->hash_tree_area_size != signature->hash_size) {
		error = -EINVAL;
		goto out;
	}
	if (signature->hash_size > 0 &&
	    handler->md_record_offset <= signature->hash_offset) {
		error = -EINVAL;
		goto out;
	}
	if (handler->md_record_offset <= signature->sig_offset) {
		error = -EINVAL;
		goto out;
	}
	df->df_hash_tree = hash_tree;
	hash_tree = NULL;
	df->df_signature = signature;
	signature = NULL;
out:
	incfs_free_mtree(hash_tree);
	kfree(signature);
	kfree(buf);

	return error;
}

int incfs_scan_metadata_chain(struct data_file *df)
{
	struct metadata_handler *handler = NULL;
	int result = 0;
	int records_count = 0;
	int error = 0;
	struct backing_file_context *bfc = NULL;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	bfc = df->df_backing_file_context;

	handler = kzalloc(sizeof(*handler), GFP_NOFS);
	if (!handler)
		return -ENOMEM;

	/* No writing to the backing file while it's being scanned. */
	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		goto out;

	/* Reading superblock */
	handler->md_record_offset = df->df_metadata_off;
	handler->context = df;
	handler->handle_blockmap = process_blockmap_md;
	handler->handle_file_attr = process_file_attr_md;
	handler->handle_signature = process_file_signature_md;

	pr_debug("incfs: Starting reading incfs-metadata records at offset %lld\n",
		 handler->md_record_offset);
	while (handler->md_record_offset > 0) {
		error = incfs_read_next_metadata_record(bfc, handler);
		if (error) {
			pr_warn("incfs: Error during reading incfs-metadata record. Offset: %lld Record #%d Error code: %d\n",
				handler->md_record_offset, records_count + 1,
				-error);
			break;
		}
		records_count++;
	}
	if (error) {
		pr_debug("incfs: Error %d after reading %d incfs-metadata records.\n",
			 -error, records_count);
		result = error;
	} else {
		pr_debug("incfs: Finished reading %d incfs-metadata records.\n",
			 records_count);
		result = records_count;
	}
	mutex_unlock(&bfc->bc_mutex);
out:
	kfree(handler);
	return result;
}

/*
 * Quickly checks if there are pending reads with a serial number larger
 * than a given one.
 */
bool incfs_fresh_pending_reads_exist(struct mount_info *mi, int last_number)
{
	bool result = false;

	mutex_lock(&mi->mi_pending_reads_mutex);
	result = (mi->mi_last_pending_read_number > last_number) &&
		 (mi->mi_pending_reads_count > 0);
	mutex_unlock(&mi->mi_pending_reads_mutex);
	return result;
}

int incfs_collect_pending_reads(struct mount_info *mi, int sn_lowerbound,
				struct incfs_pending_read_info *reads,
				int reads_size)
{
	int reported_reads = 0;
	struct pending_read *entry = NULL;

	if (!mi)
		return -EFAULT;

	if (reads_size <= 0)
		return 0;

	mutex_lock(&mi->mi_pending_reads_mutex);

	if (mi->mi_last_pending_read_number <= sn_lowerbound
	    || mi->mi_pending_reads_count == 0)
		goto unlock;

	list_for_each_entry(entry, &mi->mi_reads_list_head, mi_reads_list) {
		if (entry->serial_number <= sn_lowerbound)
			continue;

		reads[reported_reads].file_id = entry->file_id;
		reads[reported_reads].block_index = entry->block_index;
		reads[reported_reads].serial_number = entry->serial_number;
		reads[reported_reads].timestamp_us = entry->timestamp_us;
		/* reads[reported_reads].kind = INCFS_READ_KIND_PENDING; */

		reported_reads++;
		if (reported_reads >= reads_size)
			break;
	}

unlock:
	mutex_unlock(&mi->mi_pending_reads_mutex);

	return reported_reads;
}

struct read_log_state incfs_get_log_state(struct mount_info *mi)
{
	struct read_log *log = &mi->mi_log;
	struct read_log_state result;

	spin_lock(&log->rl_writer_lock);
	result = READ_ONCE(log->rl_state);
	spin_unlock(&log->rl_writer_lock);
	return result;
}

static u64 calc_record_count(const struct read_log_state *state, int rl_size)
{
	return state->current_pass_no * (u64)rl_size + state->next_index;
}

int incfs_get_uncollected_logs_count(struct mount_info *mi,
				     struct read_log_state state)
{
	struct read_log *log = &mi->mi_log;

	u64 count = calc_record_count(&log->rl_state, log->rl_size) -
		    calc_record_count(&state, log->rl_size);
	return min_t(int, count, log->rl_size);
}

static void fill_pending_read_from_log_record(
	struct incfs_pending_read_info *dest, const struct read_log_record *src,
	struct read_log_state *state, u64 log_size)
{
	dest->file_id = src->file_id;
	dest->block_index = src->block_index;
	dest->serial_number =
		state->current_pass_no * log_size + state->next_index;
	dest->timestamp_us = src->timestamp_us;
}

int incfs_collect_logged_reads(struct mount_info *mi,
			       struct read_log_state *reader_state,
			       struct incfs_pending_read_info *reads,
			       int reads_size)
{
	struct read_log *log = &mi->mi_log;
	struct read_log_state live_state = incfs_get_log_state(mi);
	u64 read_count = calc_record_count(reader_state, log->rl_size);
	u64 written_count = calc_record_count(&live_state, log->rl_size);
	int dst_idx;

	if (reader_state->next_index >= log->rl_size ||
	    read_count > written_count)
		return -ERANGE;

	if (read_count == written_count)
		return 0;

	if (read_count > written_count) {
		/* This reader is somehow ahead of the writer. */
		pr_debug("incfs: Log reader is ahead of writer\n");
		*reader_state = live_state;
	}

	if (written_count - read_count > log->rl_size) {
		/*
		 * Reading pointer is too far behind,
		 * start from the record following the write pointer.
		 */
		pr_debug("incfs: read pointer is behind, moving: %u/%u -> %u/%u / %u\n",
			(u32)reader_state->next_index,
			(u32)reader_state->current_pass_no,
			(u32)live_state.next_index,
			(u32)live_state.current_pass_no - 1, (u32)log->rl_size);

		*reader_state = (struct read_log_state){
			.next_index = live_state.next_index,
			.current_pass_no = live_state.current_pass_no - 1,
		};
	}

	for (dst_idx = 0; dst_idx < reads_size; dst_idx++) {
		if (reader_state->next_index == live_state.next_index &&
		    reader_state->current_pass_no == live_state.current_pass_no)
			break;

		fill_pending_read_from_log_record(
			&reads[dst_idx],
			&log->rl_ring_buf[reader_state->next_index],
			reader_state, log->rl_size);

		reader_state->next_index++;
		if (reader_state->next_index == log->rl_size) {
			reader_state->next_index = 0;
			reader_state->current_pass_no++;
		}
	}
	return dst_idx;
}

bool incfs_equal_ranges(struct mem_range lhs, struct mem_range rhs)
{
	if (lhs.len != rhs.len)
		return false;
	return memcmp(lhs.data, rhs.data, lhs.len) == 0;
}
