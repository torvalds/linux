// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/gfp.h>
#include <linux/ktime.h>
#include <linux/lz4.h>
#include <linux/mm.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"

static int incfs_scan_metadata_chain(struct data_file *df);

static void log_wake_up_all(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct read_log *rl = container_of(dw, struct read_log, ml_wakeup_work);
	wake_up_all(&rl->ml_notif_wq);
}

static void zstd_free_workspace(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct mount_info *mi =
		container_of(dw, struct mount_info, mi_zstd_cleanup_work);

	mutex_lock(&mi->mi_zstd_workspace_mutex);
	kvfree(mi->mi_zstd_workspace);
	mi->mi_zstd_workspace = NULL;
	mi->mi_zstd_stream = NULL;
	mutex_unlock(&mi->mi_zstd_workspace_mutex);
}

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
	mi->mi_backing_dir_path = *backing_dir_path;
	mi->mi_owner = get_current_cred();
	path_get(&mi->mi_backing_dir_path);
	mutex_init(&mi->mi_dir_struct_mutex);
	init_waitqueue_head(&mi->mi_pending_reads_notif_wq);
	init_waitqueue_head(&mi->mi_log.ml_notif_wq);
	init_waitqueue_head(&mi->mi_blocks_written_notif_wq);
	atomic_set(&mi->mi_blocks_written, 0);
	INIT_DELAYED_WORK(&mi->mi_log.ml_wakeup_work, log_wake_up_all);
	spin_lock_init(&mi->mi_log.rl_lock);
	spin_lock_init(&mi->pending_read_lock);
	INIT_LIST_HEAD(&mi->mi_reads_list_head);
	spin_lock_init(&mi->mi_per_uid_read_timeouts_lock);
	mutex_init(&mi->mi_zstd_workspace_mutex);
	INIT_DELAYED_WORK(&mi->mi_zstd_cleanup_work, zstd_free_workspace);

	error = incfs_realloc_mount_info(mi, options);
	if (error)
		goto err;

	return mi;

err:
	incfs_free_mount_info(mi);
	return ERR_PTR(error);
}

int incfs_realloc_mount_info(struct mount_info *mi,
			     struct mount_options *options)
{
	void *new_buffer = NULL;
	void *old_buffer;
	size_t new_buffer_size = 0;

	if (options->read_log_pages != mi->mi_options.read_log_pages) {
		struct read_log_state log_state;
		/*
		 * Even though having two buffers allocated at once isn't
		 * usually good, allocating a multipage buffer under a spinlock
		 * is even worse, so let's optimize for the shorter lock
		 * duration. It's not end of the world if we fail to increase
		 * the buffer size anyway.
		 */
		if (options->read_log_pages > 0) {
			new_buffer_size = PAGE_SIZE * options->read_log_pages;
			new_buffer = kzalloc(new_buffer_size, GFP_NOFS);
			if (!new_buffer)
				return -ENOMEM;
		}

		spin_lock(&mi->mi_log.rl_lock);
		old_buffer = mi->mi_log.rl_ring_buf;
		mi->mi_log.rl_ring_buf = new_buffer;
		mi->mi_log.rl_size = new_buffer_size;
		log_state = (struct read_log_state){
			.generation_id = mi->mi_log.rl_head.generation_id + 1,
		};
		mi->mi_log.rl_head = log_state;
		mi->mi_log.rl_tail = log_state;
		spin_unlock(&mi->mi_log.rl_lock);

		kfree(old_buffer);
	}

	mi->mi_options = *options;
	return 0;
}

void incfs_free_mount_info(struct mount_info *mi)
{
	int i;
	if (!mi)
		return;

	flush_delayed_work(&mi->mi_log.ml_wakeup_work);
	flush_delayed_work(&mi->mi_zstd_cleanup_work);

	dput(mi->mi_index_dir);
	dput(mi->mi_incomplete_dir);
	path_put(&mi->mi_backing_dir_path);
	mutex_destroy(&mi->mi_dir_struct_mutex);
	mutex_destroy(&mi->mi_zstd_workspace_mutex);
	put_cred(mi->mi_owner);
	kfree(mi->mi_log.rl_ring_buf);
	for (i = 0; i < ARRAY_SIZE(mi->pseudo_file_xattr); ++i)
		kfree(mi->pseudo_file_xattr[i].data);
	kfree(mi->mi_per_uid_read_timeouts);
	kfree(mi);
}

static void data_file_segment_init(struct data_file_segment *segment)
{
	init_waitqueue_head(&segment->new_data_arrival_wq);
	init_rwsem(&segment->rwsem);
	INIT_LIST_HEAD(&segment->reads_list_head);
}

char *file_id_to_str(incfs_uuid_t id)
{
	char *result = kmalloc(1 + sizeof(id.bytes) * 2, GFP_NOFS);
	char *end;

	if (!result)
		return NULL;

	end = bin2hex(result, id.bytes, sizeof(id.bytes));
	*end = 0;
	return result;
}

struct dentry *incfs_lookup_dentry(struct dentry *parent, const char *name)
{
	struct inode *inode;
	struct dentry *result = NULL;

	if (!parent)
		return ERR_PTR(-EFAULT);

	inode = d_inode(parent);
	inode_lock_nested(inode, I_MUTEX_PARENT);
	result = lookup_one_len(name, parent, strlen(name));
	inode_unlock(inode);

	if (IS_ERR(result))
		pr_warn("%s err:%ld\n", __func__, PTR_ERR(result));

	return result;
}

static struct data_file *handle_mapped_file(struct mount_info *mi,
					    struct data_file *df)
{
	char *file_id_str;
	struct dentry *index_file_dentry;
	struct path path;
	struct file *bf;
	struct data_file *result = NULL;
	const struct cred *old_cred;

	file_id_str = file_id_to_str(df->df_id);
	if (!file_id_str)
		return ERR_PTR(-ENOENT);

	index_file_dentry = incfs_lookup_dentry(mi->mi_index_dir,
						file_id_str);
	kfree(file_id_str);
	if (!index_file_dentry)
		return ERR_PTR(-ENOENT);
	if (IS_ERR(index_file_dentry))
		return (struct data_file *)index_file_dentry;
	if (!d_really_is_positive(index_file_dentry)) {
		result = ERR_PTR(-ENOENT);
		goto out;
	}

	path = (struct path) {
		.mnt = mi->mi_backing_dir_path.mnt,
		.dentry = index_file_dentry
	};

	old_cred = override_creds(mi->mi_owner);
	bf = dentry_open(&path, O_RDWR | O_NOATIME | O_LARGEFILE,
			 current_cred());
	revert_creds(old_cred);

	if (IS_ERR(bf)) {
		result = (struct data_file *)bf;
		goto out;
	}

	result = incfs_open_data_file(mi, bf);
	fput(bf);
	if (IS_ERR(result))
		goto out;

	result->df_mapped_offset = df->df_metadata_off;

out:
	dput(index_file_dentry);
	return result;
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

	bfc = incfs_alloc_bfc(mi, bf);
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

	error = incfs_read_file_header(bfc, &df->df_metadata_off, &df->df_id,
				       &size, &df->df_header_flags);

	if (error)
		goto out;

	df->df_size = size;
	if (size > 0)
		df->df_data_block_count = get_blocks_count_for_size(size);

	if (df->df_header_flags & INCFS_FILE_MAPPED) {
		struct data_file *mapped_df = handle_mapped_file(mi, df);

		incfs_free_data_file(df);
		return mapped_df;
	}

	md_records = incfs_scan_metadata_chain(df);
	if (md_records < 0)
		error = md_records;

out:
	if (error) {
		incfs_free_bfc(bfc);
		if (df)
			df->df_backing_file_context = NULL;
		incfs_free_data_file(df);
		return ERR_PTR(error);
	}
	return df;
}

void incfs_free_data_file(struct data_file *df)
{
	u32 data_blocks_written, hash_blocks_written;

	if (!df)
		return;

	data_blocks_written = atomic_read(&df->df_data_blocks_written);
	hash_blocks_written = atomic_read(&df->df_hash_blocks_written);

	if (data_blocks_written != df->df_initial_data_blocks_written ||
	    hash_blocks_written != df->df_initial_hash_blocks_written) {
		struct backing_file_context *bfc = df->df_backing_file_context;
		int error = -1;

		if (bfc && !mutex_lock_interruptible(&bfc->bc_mutex)) {
			error = incfs_write_status_to_backing_file(
						df->df_backing_file_context,
						df->df_status_offset,
						data_blocks_written,
						hash_blocks_written);
			mutex_unlock(&bfc->bc_mutex);
		}

		if (error)
			/* Nothing can be done, just warn */
			pr_warn("incfs: failed to write status to backing file\n");
	}

	incfs_free_mtree(df->df_hash_tree);
	incfs_free_bfc(df->df_backing_file_context);
	kfree(df->df_signature);
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

static ssize_t zstd_decompress_safe(struct mount_info *mi,
				    struct mem_range src, struct mem_range dst)
{
	ssize_t result;
	ZSTD_inBuffer inbuf = {.src = src.data,	.size = src.len};
	ZSTD_outBuffer outbuf = {.dst = dst.data, .size = dst.len};

	result = mutex_lock_interruptible(&mi->mi_zstd_workspace_mutex);
	if (result)
		return result;

	if (!mi->mi_zstd_stream) {
		unsigned int workspace_size = ZSTD_DStreamWorkspaceBound(
						INCFS_DATA_FILE_BLOCK_SIZE);
		void *workspace = kvmalloc(workspace_size, GFP_NOFS);
		ZSTD_DStream *stream;

		if (!workspace) {
			result = -ENOMEM;
			goto out;
		}

		stream = ZSTD_initDStream(INCFS_DATA_FILE_BLOCK_SIZE, workspace,
				  workspace_size);
		if (!stream) {
			kvfree(workspace);
			result = -EIO;
			goto out;
		}

		mi->mi_zstd_workspace = workspace;
		mi->mi_zstd_stream = stream;
	}

	result = ZSTD_decompressStream(mi->mi_zstd_stream, &outbuf, &inbuf) ?
		-EBADMSG : outbuf.pos;

	mod_delayed_work(system_wq, &mi->mi_zstd_cleanup_work,
			 msecs_to_jiffies(5000));

out:
	mutex_unlock(&mi->mi_zstd_workspace_mutex);
	return result;
}

static ssize_t decompress(struct mount_info *mi,
			  struct mem_range src, struct mem_range dst, int alg)
{
	int result;

	switch (alg) {
	case INCFS_BLOCK_COMPRESSED_LZ4:
		result = LZ4_decompress_safe(src.data, dst.data, src.len,
					     dst.len);
		if (result < 0)
			return -EBADMSG;
		return result;

	case INCFS_BLOCK_COMPRESSED_ZSTD:
		return zstd_decompress_safe(mi, src, dst);

	default:
		WARN_ON(true);
		return -EOPNOTSUPP;
	}
}

static void log_read_one_record(struct read_log *rl, struct read_log_state *rs)
{
	union log_record *record =
		(union log_record *)((u8 *)rl->rl_ring_buf + rs->next_offset);
	size_t record_size;

	switch (record->full_record.type) {
	case FULL:
		rs->base_record = record->full_record;
		record_size = sizeof(record->full_record);
		break;

	case SAME_FILE:
		rs->base_record.block_index =
			record->same_file_record.block_index;
		rs->base_record.absolute_ts_us +=
			record->same_file_record.relative_ts_us;
		record_size = sizeof(record->same_file_record);
		break;

	case SAME_FILE_NEXT_BLOCK:
		++rs->base_record.block_index;
		rs->base_record.absolute_ts_us +=
			record->same_file_next_block.relative_ts_us;
		record_size = sizeof(record->same_file_next_block);
		break;

	case SAME_FILE_NEXT_BLOCK_SHORT:
		++rs->base_record.block_index;
		rs->base_record.absolute_ts_us +=
			record->same_file_next_block_short.relative_ts_us;
		record_size = sizeof(record->same_file_next_block_short);
		break;
	}

	rs->next_offset += record_size;
	if (rs->next_offset > rl->rl_size - sizeof(*record)) {
		rs->next_offset = 0;
		++rs->current_pass_no;
	}
	++rs->current_record_no;
}

static void log_block_read(struct mount_info *mi, incfs_uuid_t *id,
			   int block_index)
{
	struct read_log *log = &mi->mi_log;
	struct read_log_state *head, *tail;
	s64 now_us;
	s64 relative_us;
	union log_record record;
	size_t record_size;
	uid_t uid = current_uid().val;

	/*
	 * This may read the old value, but it's OK to delay the logging start
	 * right after the configuration update.
	 */
	if (READ_ONCE(log->rl_size) == 0)
		return;

	now_us = ktime_to_us(ktime_get());

	spin_lock(&log->rl_lock);
	if (log->rl_size == 0) {
		spin_unlock(&log->rl_lock);
		return;
	}

	head = &log->rl_head;
	tail = &log->rl_tail;
	relative_us = now_us - head->base_record.absolute_ts_us;

	if (memcmp(id, &head->base_record.file_id, sizeof(incfs_uuid_t)) ||
	    relative_us >= 1ll << 32 ||
	    uid != head->base_record.uid) {
		record.full_record = (struct full_record){
			.type = FULL,
			.block_index = block_index,
			.file_id = *id,
			.absolute_ts_us = now_us,
			.uid = uid,
		};
		head->base_record.file_id = *id;
		record_size = sizeof(struct full_record);
	} else if (block_index != head->base_record.block_index + 1 ||
		   relative_us >= 1 << 30) {
		record.same_file_record = (struct same_file_record){
			.type = SAME_FILE,
			.block_index = block_index,
			.relative_ts_us = relative_us,
		};
		record_size = sizeof(struct same_file_record);
	} else if (relative_us >= 1 << 14) {
		record.same_file_next_block = (struct same_file_next_block){
			.type = SAME_FILE_NEXT_BLOCK,
			.relative_ts_us = relative_us,
		};
		record_size = sizeof(struct same_file_next_block);
	} else {
		record.same_file_next_block_short =
			(struct same_file_next_block_short){
				.type = SAME_FILE_NEXT_BLOCK_SHORT,
				.relative_ts_us = relative_us,
			};
		record_size = sizeof(struct same_file_next_block_short);
	}

	head->base_record.block_index = block_index;
	head->base_record.absolute_ts_us = now_us;

	/* Advance tail beyond area we are going to overwrite */
	while (tail->current_pass_no < head->current_pass_no &&
	       tail->next_offset < head->next_offset + record_size)
		log_read_one_record(log, tail);

	memcpy(((u8 *)log->rl_ring_buf) + head->next_offset, &record,
	       record_size);
	head->next_offset += record_size;
	if (head->next_offset > log->rl_size - sizeof(record)) {
		head->next_offset = 0;
		++head->current_pass_no;
	}
	++head->current_record_no;

	spin_unlock(&log->rl_lock);
	schedule_delayed_work(&log->ml_wakeup_work, msecs_to_jiffies(16));
}

static int validate_hash_tree(struct backing_file_context *bfc, struct file *f,
			      int block_index, struct mem_range data, u8 *buf)
{
	struct data_file *df = get_incfs_data_file(f);
	u8 stored_digest[INCFS_MAX_HASH_SIZE] = {};
	u8 calculated_digest[INCFS_MAX_HASH_SIZE] = {};
	struct mtree *tree = NULL;
	struct incfs_df_signature *sig = NULL;
	int digest_size;
	int hash_block_index = block_index;
	int lvl;
	int res;
	loff_t hash_block_offset[INCFS_MAX_MTREE_LEVELS];
	size_t hash_offset_in_block[INCFS_MAX_MTREE_LEVELS];
	int hash_per_block;
	pgoff_t file_pages;

	tree = df->df_hash_tree;
	sig = df->df_signature;
	if (!tree || !sig)
		return 0;

	digest_size = tree->alg->digest_size;
	hash_per_block = INCFS_DATA_FILE_BLOCK_SIZE / digest_size;
	for (lvl = 0; lvl < tree->depth; lvl++) {
		loff_t lvl_off = tree->hash_level_suboffset[lvl];

		hash_block_offset[lvl] =
			lvl_off + round_down(hash_block_index * digest_size,
					     INCFS_DATA_FILE_BLOCK_SIZE);
		hash_offset_in_block[lvl] = hash_block_index * digest_size %
					    INCFS_DATA_FILE_BLOCK_SIZE;
		hash_block_index /= hash_per_block;
	}

	memcpy(stored_digest, tree->root_hash, digest_size);

	file_pages = DIV_ROUND_UP(df->df_size, INCFS_DATA_FILE_BLOCK_SIZE);
	for (lvl = tree->depth - 1; lvl >= 0; lvl--) {
		pgoff_t hash_page =
			file_pages +
			hash_block_offset[lvl] / INCFS_DATA_FILE_BLOCK_SIZE;
		struct page *page = find_get_page_flags(
			f->f_inode->i_mapping, hash_page, FGP_ACCESSED);

		if (page && PageChecked(page)) {
			u8 *addr = kmap_atomic(page);

			memcpy(stored_digest, addr + hash_offset_in_block[lvl],
			       digest_size);
			kunmap_atomic(addr);
			put_page(page);
			continue;
		}

		if (page)
			put_page(page);

		res = incfs_kread(bfc, buf, INCFS_DATA_FILE_BLOCK_SIZE,
				  hash_block_offset[lvl] + sig->hash_offset);
		if (res < 0)
			return res;
		if (res != INCFS_DATA_FILE_BLOCK_SIZE)
			return -EIO;
		res = incfs_calc_digest(tree->alg,
					range(buf, INCFS_DATA_FILE_BLOCK_SIZE),
					range(calculated_digest, digest_size));
		if (res)
			return res;

		if (memcmp(stored_digest, calculated_digest, digest_size)) {
			int i;
			bool zero = true;

			pr_warn("incfs: Hash mismatch lvl:%d blk:%d\n",
				lvl, block_index);
			for (i = 0; i < digest_size; i++)
				if (stored_digest[i]) {
					zero = false;
					break;
				}

			if (zero)
				pr_debug("Note saved_digest all zero - did you forget to load the hashes?\n");
			return -EBADMSG;
		}

		memcpy(stored_digest, buf + hash_offset_in_block[lvl],
		       digest_size);

		page = grab_cache_page(f->f_inode->i_mapping, hash_page);
		if (page) {
			u8 *addr = kmap_atomic(page);

			memcpy(addr, buf, INCFS_DATA_FILE_BLOCK_SIZE);
			kunmap_atomic(addr);
			SetPageChecked(page);
			unlock_page(page);
			put_page(page);
		}
	}

	res = incfs_calc_digest(tree->alg, data,
				range(calculated_digest, digest_size));
	if (res)
		return res;

	if (memcmp(stored_digest, calculated_digest, digest_size)) {
		pr_debug("Leaf hash mismatch blk:%d\n", block_index);
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

static void convert_data_file_block(struct incfs_blockmap_entry *bme,
				    struct data_file_block *res_block)
{
	u16 flags = le16_to_cpu(bme->me_flags);

	res_block->db_backing_file_data_offset =
		le16_to_cpu(bme->me_data_offset_hi);
	res_block->db_backing_file_data_offset <<= 32;
	res_block->db_backing_file_data_offset |=
		le32_to_cpu(bme->me_data_offset_lo);
	res_block->db_stored_size = le16_to_cpu(bme->me_data_size);
	res_block->db_comp_alg = flags & INCFS_BLOCK_COMPRESSED_MASK;
}

static int get_data_file_block(struct data_file *df, int index,
			       struct data_file_block *res_block)
{
	struct incfs_blockmap_entry bme = {};
	struct backing_file_context *bfc = NULL;
	loff_t blockmap_off = 0;
	int error = 0;

	if (!df || !res_block)
		return -EFAULT;

	blockmap_off = df->df_blockmap_off;
	bfc = df->df_backing_file_context;

	if (index < 0 || blockmap_off == 0)
		return -EINVAL;

	error = incfs_read_blockmap_entry(bfc, index, blockmap_off, &bme);
	if (error)
		return error;

	convert_data_file_block(&bme, res_block);
	return 0;
}

static int check_room_for_one_range(u32 size, u32 size_out)
{
	if (size_out + sizeof(struct incfs_filled_range) > size)
		return -ERANGE;
	return 0;
}

static int copy_one_range(struct incfs_filled_range *range, void __user *buffer,
			  u32 size, u32 *size_out)
{
	int error = check_room_for_one_range(size, *size_out);
	if (error)
		return error;

	if (copy_to_user(((char __user *)buffer) + *size_out, range,
				sizeof(*range)))
		return -EFAULT;

	*size_out += sizeof(*range);
	return 0;
}

#define READ_BLOCKMAP_ENTRIES 512
int incfs_get_filled_blocks(struct data_file *df,
			    struct incfs_file_data *fd,
			    struct incfs_get_filled_blocks_args *arg)
{
	int error = 0;
	bool in_range = false;
	struct incfs_filled_range range;
	void __user *buffer = u64_to_user_ptr(arg->range_buffer);
	u32 size = arg->range_buffer_size;
	u32 end_index =
		arg->end_index ? arg->end_index : df->df_total_block_count;
	u32 *size_out = &arg->range_buffer_size_out;
	int i = READ_BLOCKMAP_ENTRIES - 1;
	int entries_read = 0;
	struct incfs_blockmap_entry *bme;
	int data_blocks_filled = 0;
	int hash_blocks_filled = 0;

	*size_out = 0;
	if (end_index > df->df_total_block_count)
		end_index = df->df_total_block_count;
	arg->total_blocks_out = df->df_total_block_count;
	arg->data_blocks_out = df->df_data_block_count;

	if (atomic_read(&df->df_data_blocks_written) ==
	    df->df_data_block_count) {
		pr_debug("File marked full, fast get_filled_blocks");
		if (arg->start_index > end_index) {
			arg->index_out = arg->start_index;
			return 0;
		}
		arg->index_out = arg->start_index;

		error = check_room_for_one_range(size, *size_out);
		if (error)
			return error;

		range = (struct incfs_filled_range){
			.begin = arg->start_index,
			.end = end_index,
		};

		error = copy_one_range(&range, buffer, size, size_out);
		if (error)
			return error;
		arg->index_out = end_index;
		return 0;
	}

	bme = kzalloc(sizeof(*bme) * READ_BLOCKMAP_ENTRIES,
		      GFP_NOFS | __GFP_COMP);
	if (!bme)
		return -ENOMEM;

	for (arg->index_out = arg->start_index; arg->index_out < end_index;
	     ++arg->index_out) {
		struct data_file_block dfb;

		if (++i == READ_BLOCKMAP_ENTRIES) {
			entries_read = incfs_read_blockmap_entries(
				df->df_backing_file_context, bme,
				arg->index_out, READ_BLOCKMAP_ENTRIES,
				df->df_blockmap_off);
			if (entries_read < 0) {
				error = entries_read;
				break;
			}

			i = 0;
		}

		if (i >= entries_read) {
			error = -EIO;
			break;
		}

		convert_data_file_block(bme + i, &dfb);

		if (is_data_block_present(&dfb)) {
			if (arg->index_out >= df->df_data_block_count)
				++hash_blocks_filled;
			else
				++data_blocks_filled;
		}

		if (is_data_block_present(&dfb) == in_range)
			continue;

		if (!in_range) {
			error = check_room_for_one_range(size, *size_out);
			if (error)
				break;
			in_range = true;
			range.begin = arg->index_out;
		} else {
			range.end = arg->index_out;
			error = copy_one_range(&range, buffer, size, size_out);
			if (error) {
				/* there will be another try out of the loop,
				 * it will reset the index_out if it fails too
				 */
				break;
			}
			in_range = false;
		}
	}

	if (in_range) {
		range.end = arg->index_out;
		error = copy_one_range(&range, buffer, size, size_out);
		if (error)
			arg->index_out = range.begin;
	}

	if (arg->start_index == 0) {
		fd->fd_get_block_pos = 0;
		fd->fd_filled_data_blocks = 0;
		fd->fd_filled_hash_blocks = 0;
	}

	if (arg->start_index == fd->fd_get_block_pos) {
		fd->fd_get_block_pos = arg->index_out + 1;
		fd->fd_filled_data_blocks += data_blocks_filled;
		fd->fd_filled_hash_blocks += hash_blocks_filled;
	}

	if (fd->fd_get_block_pos == df->df_total_block_count + 1) {
		if (fd->fd_filled_data_blocks >
		   atomic_read(&df->df_data_blocks_written))
			atomic_set(&df->df_data_blocks_written,
				   fd->fd_filled_data_blocks);

		if (fd->fd_filled_hash_blocks >
		   atomic_read(&df->df_hash_blocks_written))
			atomic_set(&df->df_hash_blocks_written,
				   fd->fd_filled_hash_blocks);
	}

	kfree(bme);
	return error;
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
	result->uid = current_uid().val;

	spin_lock(&mi->pending_read_lock);

	result->serial_number = ++mi->mi_last_pending_read_number;
	mi->mi_pending_reads_count++;

	list_add_rcu(&result->mi_reads_list, &mi->mi_reads_list_head);
	list_add_rcu(&result->segment_reads_list, &segment->reads_list_head);

	spin_unlock(&mi->pending_read_lock);

	wake_up_all(&mi->mi_pending_reads_notif_wq);
	return result;
}

static void free_pending_read_entry(struct rcu_head *entry)
{
	struct pending_read *read;

	read = container_of(entry, struct pending_read, rcu);

	kfree(read);
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

	spin_lock(&mi->pending_read_lock);

	list_del_rcu(&read->mi_reads_list);
	list_del_rcu(&read->segment_reads_list);

	mi->mi_pending_reads_count--;

	spin_unlock(&mi->pending_read_lock);

	/* Don't free. Wait for readers */
	call_rcu(&read->rcu, free_pending_read_entry);
}

static void notify_pending_reads(struct mount_info *mi,
		struct data_file_segment *segment,
		int index)
{
	struct pending_read *entry = NULL;

	/* Notify pending reads waiting for this block. */
	rcu_read_lock();
	list_for_each_entry_rcu(entry, &segment->reads_list_head,
						segment_reads_list) {
		if (entry->block_index == index)
			set_read_done(entry);
	}
	rcu_read_unlock();
	wake_up_all(&segment->new_data_arrival_wq);

	atomic_inc(&mi->mi_blocks_written);
	wake_up_all(&mi->mi_blocks_written_notif_wq);
}

static int usleep_interruptible(u32 us)
{
	/* See:
	 * https://www.kernel.org/doc/Documentation/timers/timers-howto.txt
	 * for explanation
	 */
	if (us < 10) {
		udelay(us);
		return 0;
	} else if (us < 20000) {
		usleep_range(us, us + us / 10);
		return 0;
	} else
		return msleep_interruptible(us / 1000);
}

static int wait_for_data_block(struct data_file *df, int block_index,
			       u32 min_time_us, u32 min_pending_time_us,
			       u32 max_pending_time_us,
			       struct data_file_block *res_block)
{
	struct data_file_block block = {};
	struct data_file_segment *segment = NULL;
	struct pending_read *read = NULL;
	struct mount_info *mi = NULL;
	int error = 0;
	int wait_res = 0;
	u64 time;

	if (!df || !res_block)
		return -EFAULT;

	if (block_index < 0 || block_index >= df->df_data_block_count)
		return -EINVAL;

	if (df->df_blockmap_off <= 0 || !df->df_mount_info)
		return -ENODATA;

	mi = df->df_mount_info;
	segment = get_file_segment(df, block_index);

	error = down_read_killable(&segment->rwsem);
	if (error)
		return error;

	/* Look up the given block */
	error = get_data_file_block(df, block_index, &block);

	up_read(&segment->rwsem);

	if (error)
		return error;

	/* If the block was found, just return it. No need to wait. */
	if (is_data_block_present(&block)) {
		if (min_time_us)
			error = usleep_interruptible(min_time_us);
		*res_block = block;
		return error;
	} else {
		/* If it's not found, create a pending read */
		if (max_pending_time_us != 0) {
			read = add_pending_read(df, block_index);
			if (!read)
				return -ENOMEM;
		} else {
			log_block_read(mi, &df->df_id, block_index);
			return -ETIME;
		}
	}

	if (min_pending_time_us)
		time = ktime_get_ns();

	/* Wait for notifications about block's arrival */
	wait_res =
		wait_event_interruptible_timeout(segment->new_data_arrival_wq,
					(is_read_done(read)),
					usecs_to_jiffies(max_pending_time_us));

	/* Woke up, the pending read is no longer needed. */
	remove_pending_read(df, read);

	if (wait_res == 0) {
		/* Wait has timed out */
		log_block_read(mi, &df->df_id, block_index);
		return -ETIME;
	}
	if (wait_res < 0) {
		/*
		 * Only ERESTARTSYS is really expected here when a signal
		 * comes while we wait.
		 */
		return wait_res;
	}

	if (min_pending_time_us) {
		time = div_u64(ktime_get_ns() - time, 1000);
		if (min_pending_time_us > time) {
			error = usleep_interruptible(
						min_pending_time_us - time);
			if (error)
				return error;
		}
	}

	error = down_read_killable(&segment->rwsem);
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
			pr_warn("incfs: Wait succeeded but block not found.\n");
			error = -ENODATA;
		}
	}

	up_read(&segment->rwsem);
	return error;
}

ssize_t incfs_read_data_file_block(struct mem_range dst, struct file *f,
			int index, u32 min_time_us,
			u32 min_pending_time_us, u32 max_pending_time_us,
			struct mem_range tmp)
{
	loff_t pos;
	ssize_t result;
	size_t bytes_to_read;
	struct mount_info *mi = NULL;
	struct backing_file_context *bfc = NULL;
	struct data_file_block block = {};
	struct data_file *df = get_incfs_data_file(f);

	if (!dst.data || !df || !tmp.data)
		return -EFAULT;

	if (tmp.len < 2 * INCFS_DATA_FILE_BLOCK_SIZE)
		return -ERANGE;

	mi = df->df_mount_info;
	bfc = df->df_backing_file_context;

	result = wait_for_data_block(df, index, min_time_us,
			min_pending_time_us, max_pending_time_us, &block);
	if (result < 0)
		goto out;

	pos = block.db_backing_file_data_offset;
	if (block.db_comp_alg == COMPRESSION_NONE) {
		bytes_to_read = min(dst.len, block.db_stored_size);
		result = incfs_kread(bfc, dst.data, bytes_to_read, pos);

		/* Some data was read, but not enough */
		if (result >= 0 && result != bytes_to_read)
			result = -EIO;
	} else {
		bytes_to_read = min(tmp.len, block.db_stored_size);
		result = incfs_kread(bfc, tmp.data, bytes_to_read, pos);
		if (result == bytes_to_read) {
			result =
				decompress(mi, range(tmp.data, bytes_to_read),
					   dst, block.db_comp_alg);
			if (result < 0) {
				const char *name =
				    bfc->bc_file->f_path.dentry->d_name.name;

				pr_warn_once("incfs: Decompression error. %s",
					     name);
			}
		} else if (result >= 0) {
			/* Some data was read, but not enough */
			result = -EIO;
		}
	}

	if (result > 0) {
		int err = validate_hash_tree(bfc, f, index, dst, tmp.data);

		if (err < 0)
			result = err;
	}

	if (result >= 0)
		log_block_read(mi, &df->df_id, index);

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

	if (block->block_index >= df->df_data_block_count)
		return -ERANGE;

	segment = get_file_segment(df, block->block_index);
	if (!segment)
		return -EFAULT;

	if (block->compression == COMPRESSION_LZ4)
		flags |= INCFS_BLOCK_COMPRESSED_LZ4;
	else if (block->compression == COMPRESSION_ZSTD)
		flags |= INCFS_BLOCK_COMPRESSED_ZSTD;
	else if (block->compression)
		return -EINVAL;

	error = down_read_killable(&segment->rwsem);
	if (error)
		return error;

	error = get_data_file_block(df, block->block_index, &existing_block);

	up_read(&segment->rwsem);

	if (error)
		return error;
	if (is_data_block_present(&existing_block)) {
		/* Block is already present, nothing to do here */
		return 0;
	}

	error = down_write_killable(&segment->rwsem);
	if (error)
		return error;

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (!error) {
		error = incfs_write_data_block_to_backing_file(
			bfc, range(data, block->data_len), block->block_index,
			df->df_blockmap_off, flags);
		mutex_unlock(&bfc->bc_mutex);
	}
	if (!error) {
		notify_pending_reads(mi, segment, block->block_index);
		atomic_inc(&df->df_data_blocks_written);
	}

	up_write(&segment->rwsem);

	if (error)
		pr_debug("%d error: %d\n", block->block_index, error);
	return error;
}

int incfs_read_file_signature(struct data_file *df, struct mem_range dst)
{
	struct backing_file_context *bfc = df->df_backing_file_context;
	struct incfs_df_signature *sig;
	int read_res = 0;

	if (!dst.data)
		return -EFAULT;

	sig = df->df_signature;
	if (!sig)
		return 0;

	if (dst.len < sig->sig_size)
		return -E2BIG;

	read_res = incfs_kread(bfc, dst.data, sig->sig_size, sig->sig_offset);

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
	if (!error) {
		error = incfs_write_hash_block_to_backing_file(
			bfc, range(data, block->data_len), block->block_index,
			hash_area_base, df->df_blockmap_off, df->df_size);
		mutex_unlock(&bfc->bc_mutex);
	}
	if (!error)
		atomic_inc(&df->df_hash_blocks_written);

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

	if (df->df_data_block_count > block_count)
		return -EBADMSG;

	df->df_total_block_count = block_count;
	df->df_blockmap_off = base_off;
	return error;
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

	if (!signature)
		return -ENOMEM;

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

	read = incfs_kread(df->df_backing_file_context, buf,
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
				      df->df_data_block_count);
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

static int process_status_md(struct incfs_status *is,
			     struct metadata_handler *handler)
{
	struct data_file *df = handler->context;

	df->df_initial_data_blocks_written =
		le32_to_cpu(is->is_data_blocks_written);
	atomic_set(&df->df_data_blocks_written,
		   df->df_initial_data_blocks_written);

	df->df_initial_hash_blocks_written =
		le32_to_cpu(is->is_hash_blocks_written);
	atomic_set(&df->df_hash_blocks_written,
		   df->df_initial_hash_blocks_written);

	df->df_status_offset = handler->md_record_offset;

	return 0;
}

static int incfs_scan_metadata_chain(struct data_file *df)
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

	handler->md_record_offset = df->df_metadata_off;
	handler->context = df;
	handler->handle_blockmap = process_blockmap_md;
	handler->handle_signature = process_file_signature_md;
	handler->handle_status = process_status_md;

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
		pr_warn("incfs: Error %d after reading %d incfs-metadata records.\n",
			 -error, records_count);
		result = error;
	} else
		result = records_count;

	if (df->df_hash_tree) {
		int hash_block_count = get_blocks_count_for_size(
			df->df_hash_tree->hash_tree_area_size);

		if (df->df_data_block_count + hash_block_count !=
		    df->df_total_block_count)
			result = -EINVAL;
	} else if (df->df_data_block_count != df->df_total_block_count)
		result = -EINVAL;

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

	spin_lock(&mi->pending_read_lock);
	result = (mi->mi_last_pending_read_number > last_number) &&
		(mi->mi_pending_reads_count > 0);
	spin_unlock(&mi->pending_read_lock);
	return result;
}

int incfs_collect_pending_reads(struct mount_info *mi, int sn_lowerbound,
				struct incfs_pending_read_info *reads,
				struct incfs_pending_read_info2 *reads2,
				int reads_size, int *new_max_sn)
{
	int reported_reads = 0;
	struct pending_read *entry = NULL;

	if (!mi)
		return -EFAULT;

	if (reads_size <= 0)
		return 0;

	if (!incfs_fresh_pending_reads_exist(mi, sn_lowerbound))
		return 0;

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &mi->mi_reads_list_head, mi_reads_list) {
		if (entry->serial_number <= sn_lowerbound)
			continue;

		if (reads) {
			reads[reported_reads].file_id = entry->file_id;
			reads[reported_reads].block_index = entry->block_index;
			reads[reported_reads].serial_number =
				entry->serial_number;
			reads[reported_reads].timestamp_us =
				entry->timestamp_us;
		}

		if (reads2) {
			reads2[reported_reads].file_id = entry->file_id;
			reads2[reported_reads].block_index = entry->block_index;
			reads2[reported_reads].serial_number =
				entry->serial_number;
			reads2[reported_reads].timestamp_us =
				entry->timestamp_us;
			reads2[reported_reads].uid = entry->uid;
		}

		if (entry->serial_number > *new_max_sn)
			*new_max_sn = entry->serial_number;

		reported_reads++;
		if (reported_reads >= reads_size)
			break;
	}

	rcu_read_unlock();

	return reported_reads;
}

struct read_log_state incfs_get_log_state(struct mount_info *mi)
{
	struct read_log *log = &mi->mi_log;
	struct read_log_state result;

	spin_lock(&log->rl_lock);
	result = log->rl_head;
	spin_unlock(&log->rl_lock);
	return result;
}

int incfs_get_uncollected_logs_count(struct mount_info *mi,
				     const struct read_log_state *state)
{
	struct read_log *log = &mi->mi_log;
	u32 generation;
	u64 head_no, tail_no;

	spin_lock(&log->rl_lock);
	tail_no = log->rl_tail.current_record_no;
	head_no = log->rl_head.current_record_no;
	generation = log->rl_head.generation_id;
	spin_unlock(&log->rl_lock);

	if (generation != state->generation_id)
		return head_no - tail_no;
	else
		return head_no - max_t(u64, tail_no, state->current_record_no);
}

int incfs_collect_logged_reads(struct mount_info *mi,
			       struct read_log_state *state,
			       struct incfs_pending_read_info *reads,
			       struct incfs_pending_read_info2 *reads2,
			       int reads_size)
{
	int dst_idx;
	struct read_log *log = &mi->mi_log;
	struct read_log_state *head, *tail;

	spin_lock(&log->rl_lock);
	head = &log->rl_head;
	tail = &log->rl_tail;

	if (state->generation_id != head->generation_id) {
		pr_debug("read ptr is wrong generation: %u/%u",
			 state->generation_id, head->generation_id);

		*state = (struct read_log_state){
			.generation_id = head->generation_id,
		};
	}

	if (state->current_record_no < tail->current_record_no) {
		pr_debug("read ptr is behind, moving: %u/%u -> %u/%u\n",
			 (u32)state->next_offset,
			 (u32)state->current_pass_no,
			 (u32)tail->next_offset, (u32)tail->current_pass_no);

		*state = *tail;
	}

	for (dst_idx = 0; dst_idx < reads_size; dst_idx++) {
		if (state->current_record_no == head->current_record_no)
			break;

		log_read_one_record(log, state);

		if (reads)
			reads[dst_idx] = (struct incfs_pending_read_info) {
				.file_id = state->base_record.file_id,
				.block_index = state->base_record.block_index,
				.serial_number = state->current_record_no,
				.timestamp_us =
					state->base_record.absolute_ts_us,
			};

		if (reads2)
			reads2[dst_idx] = (struct incfs_pending_read_info2) {
				.file_id = state->base_record.file_id,
				.block_index = state->base_record.block_index,
				.serial_number = state->current_record_no,
				.timestamp_us =
					state->base_record.absolute_ts_us,
				.uid = state->base_record.uid,
			};
	}

	spin_unlock(&log->rl_lock);
	return dst_idx;
}

