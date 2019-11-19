// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/kernel.h>

#include "compat.h"
#include "format.h"

struct backing_file_context *incfs_alloc_bfc(struct file *backing_file)
{
	struct backing_file_context *result = NULL;

	result = kzalloc(sizeof(*result), GFP_NOFS);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->bc_file = get_file(backing_file);
	mutex_init(&result->bc_mutex);
	return result;
}

void incfs_free_bfc(struct backing_file_context *bfc)
{
	if (!bfc)
		return;

	if (bfc->bc_file)
		fput(bfc->bc_file);

	mutex_destroy(&bfc->bc_mutex);
	kfree(bfc);
}

loff_t incfs_get_end_offset(struct file *f)
{
	/*
	 * This function assumes that file size and the end-offset
	 * are the same. This is not always true.
	 */
	return i_size_read(file_inode(f));
}

/*
 * Truncate the tail of the file to the given length.
 * Used to rollback partially successful multistep writes.
 */
static int truncate_backing_file(struct backing_file_context *bfc,
				loff_t new_end)
{
	struct inode *inode = NULL;
	struct dentry *dentry = NULL;
	loff_t old_end = 0;
	struct iattr attr;
	int result = 0;

	if (!bfc)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);

	if (!bfc->bc_file)
		return -EFAULT;

	old_end = incfs_get_end_offset(bfc->bc_file);
	if (old_end == new_end)
		return 0;
	if (old_end < new_end)
		return -EINVAL;

	inode = bfc->bc_file->f_inode;
	dentry = bfc->bc_file->f_path.dentry;

	attr.ia_size = new_end;
	attr.ia_valid = ATTR_SIZE;

	inode_lock(inode);
	result = notify_change(dentry, &attr, NULL);
	inode_unlock(inode);

	return result;
}

/* Append a given number of zero bytes to the end of the backing file. */
static int append_zeros(struct backing_file_context *bfc, size_t len)
{
	loff_t file_size = 0;
	loff_t new_last_byte_offset = 0;
	int res = 0;

	if (!bfc)
		return -EFAULT;

	if (len == 0)
		return 0;

	LOCK_REQUIRED(bfc->bc_mutex);

	/*
	 * Allocate only one byte at the new desired end of the file.
	 * It will increase file size and create a zeroed area of
	 * a given size.
	 */
	file_size = incfs_get_end_offset(bfc->bc_file);
	new_last_byte_offset = file_size + len - 1;
	res = vfs_fallocate(bfc->bc_file, 0, new_last_byte_offset, 1);
	if (res)
		return res;

	res = vfs_fsync_range(bfc->bc_file, file_size, file_size + len, 1);
	return res;
}

static int write_to_bf(struct backing_file_context *bfc, const void *buf,
			size_t count, loff_t pos, bool sync)
{
	ssize_t res = 0;

	res = incfs_kwrite(bfc->bc_file, buf, count, pos);
	if (res < 0)
		return res;
	if (res != count)
		return -EIO;

	if (sync)
		return vfs_fsync_range(bfc->bc_file, pos, pos + count, 1);

	return 0;
}

static u32 calc_md_crc(struct incfs_md_header *record)
{
	u32 result = 0;
	__le32 saved_crc = record->h_record_crc;
	__le64 saved_md_offset = record->h_next_md_offset;
	size_t record_size = min_t(size_t, le16_to_cpu(record->h_record_size),
				INCFS_MAX_METADATA_RECORD_SIZE);

	/* Zero fields which needs to be excluded from CRC calculation. */
	record->h_record_crc = 0;
	record->h_next_md_offset = 0;
	result = crc32(0, record, record_size);

	/* Restore excluded fields. */
	record->h_record_crc = saved_crc;
	record->h_next_md_offset = saved_md_offset;

	return result;
}

/*
 * Append a given metadata record to the backing file and update a previous
 * record to add the new record the the metadata list.
 */
static int append_md_to_backing_file(struct backing_file_context *bfc,
			      struct incfs_md_header *record)
{
	int result = 0;
	loff_t record_offset;
	loff_t file_pos;
	__le64 new_md_offset;
	size_t record_size;

	if (!bfc || !record)
		return -EFAULT;

	if (bfc->bc_last_md_record_offset < 0)
		return -EINVAL;

	LOCK_REQUIRED(bfc->bc_mutex);

	record_size = le16_to_cpu(record->h_record_size);
	file_pos = incfs_get_end_offset(bfc->bc_file);
	record->h_prev_md_offset = bfc->bc_last_md_record_offset;
	record->h_next_md_offset = 0;
	record->h_record_crc = cpu_to_le32(calc_md_crc(record));

	/* Write the metadata record to the end of the backing file */
	record_offset = file_pos;
	new_md_offset = cpu_to_le64(record_offset);
	result = write_to_bf(bfc, record, record_size, file_pos, true);
	if (result)
		return result;

	/* Update next metadata offset in a previous record or a superblock. */
	if (bfc->bc_last_md_record_offset) {
		/*
		 * Find a place in the previous md record where new record's
		 * offset needs to be saved.
		 */
		file_pos = bfc->bc_last_md_record_offset +
			offsetof(struct incfs_md_header, h_next_md_offset);
	} else {
		/*
		 * No metadata yet, file a place to update in the
		 * file_header.
		 */
		file_pos = offsetof(struct incfs_file_header,
				    fh_first_md_offset);
	}
	result = write_to_bf(bfc, &new_md_offset, sizeof(new_md_offset),
				file_pos, true);
	if (result)
		return result;

	bfc->bc_last_md_record_offset = record_offset;
	return result;
}

/*
 * Reserve 0-filled space for the blockmap body, and append
 * incfs_blockmap metadata record pointing to it.
 */
int incfs_write_blockmap_to_backing_file(struct backing_file_context *bfc,
				u32 block_count, loff_t *map_base_off)
{
	struct incfs_blockmap blockmap = {};
	int result = 0;
	loff_t file_end = 0;
	size_t map_size = block_count * sizeof(struct incfs_blockmap_entry);

	if (!bfc)
		return -EFAULT;

	blockmap.m_header.h_md_entry_type = INCFS_MD_BLOCK_MAP;
	blockmap.m_header.h_record_size = cpu_to_le16(sizeof(blockmap));
	blockmap.m_header.h_next_md_offset = cpu_to_le64(0);
	blockmap.m_block_count = cpu_to_le32(block_count);

	LOCK_REQUIRED(bfc->bc_mutex);

	/* Reserve 0-filled space for the blockmap body in the backing file. */
	file_end = incfs_get_end_offset(bfc->bc_file);
	result = append_zeros(bfc, map_size);
	if (result)
		return result;

	/* Write blockmap metadata record pointing to the body written above. */
	blockmap.m_base_offset = cpu_to_le64(file_end);
	result = append_md_to_backing_file(bfc, &blockmap.m_header);
	if (result) {
		/* Error, rollback file changes */
		truncate_backing_file(bfc, file_end);
	} else if (map_base_off) {
		*map_base_off = file_end;
	}

	return result;
}

/*
 * Write file attribute data and metadata record to the backing file.
 */
int incfs_write_file_attr_to_backing_file(struct backing_file_context *bfc,
		struct mem_range value, struct incfs_file_attr *attr)
{
	struct incfs_file_attr file_attr = {};
	int result = 0;
	u32 crc = 0;
	loff_t value_offset = 0;

	if (!bfc)
		return -EFAULT;

	if (value.len > INCFS_MAX_FILE_ATTR_SIZE)
		return -ENOSPC;

	LOCK_REQUIRED(bfc->bc_mutex);

	crc = crc32(0, value.data, value.len);
	value_offset = incfs_get_end_offset(bfc->bc_file);
	file_attr.fa_header.h_md_entry_type = INCFS_MD_FILE_ATTR;
	file_attr.fa_header.h_record_size = cpu_to_le16(sizeof(file_attr));
	file_attr.fa_header.h_next_md_offset = cpu_to_le64(0);
	file_attr.fa_size = cpu_to_le16((u16)value.len);
	file_attr.fa_offset = cpu_to_le64(value_offset);
	file_attr.fa_crc = cpu_to_le64(crc);

	result = write_to_bf(bfc, value.data, value.len, value_offset, true);
	if (result)
		return result;

	result = append_md_to_backing_file(bfc, &file_attr.fa_header);
	if (result) {
		/* Error, rollback file changes */
		truncate_backing_file(bfc, value_offset);
	} else if (attr) {
		*attr = file_attr;
	}

	return result;
}

int incfs_write_signature_to_backing_file(struct backing_file_context *bfc,
		u8 hash_alg, u32 tree_size,
		struct mem_range root_hash, struct mem_range add_data,
		struct mem_range sig)
{
	struct incfs_file_signature sg = {};
	int result = 0;
	loff_t rollback_pos = 0;
	loff_t tree_area_pos = 0;
	size_t alignment = 0;

	if (!bfc)
		return -EFAULT;
	if (root_hash.len > sizeof(sg.sg_root_hash))
		return -E2BIG;

	LOCK_REQUIRED(bfc->bc_mutex);

	rollback_pos = incfs_get_end_offset(bfc->bc_file);

	sg.sg_header.h_md_entry_type = INCFS_MD_SIGNATURE;
	sg.sg_header.h_record_size = cpu_to_le16(sizeof(sg));
	sg.sg_header.h_next_md_offset = cpu_to_le64(0);
	sg.sg_hash_alg = hash_alg;
	if (sig.data != NULL && sig.len > 0) {
		loff_t pos = incfs_get_end_offset(bfc->bc_file);

		sg.sg_sig_size = cpu_to_le32(sig.len);
		sg.sg_sig_offset = cpu_to_le64(pos);

		result = write_to_bf(bfc, sig.data, sig.len, pos, false);
		if (result)
			goto err;
	}

	if (add_data.len > 0) {
		loff_t pos = incfs_get_end_offset(bfc->bc_file);

		sg.sg_add_data_size = cpu_to_le32(add_data.len);
		sg.sg_add_data_offset = cpu_to_le64(pos);

		result = write_to_bf(bfc, add_data.data,
			add_data.len, pos, false);
		if (result)
			goto err;
	}

	tree_area_pos = incfs_get_end_offset(bfc->bc_file);
	if (hash_alg && tree_size > 0) {
		if (tree_size > 5 * INCFS_DATA_FILE_BLOCK_SIZE) {
			/*
			 * If hash tree is big enough, it makes sense to
			 * align in the backing file for faster access.
			 */
			loff_t offset = round_up(tree_area_pos, PAGE_SIZE);

			alignment = offset - tree_area_pos;
			tree_area_pos = offset;
		}

		/*
		 * If root hash is not the only hash in the tree.
		 * reserve 0-filled space for the tree.
		 */
		result = append_zeros(bfc, tree_size + alignment);
		if (result)
			goto err;

		sg.sg_hash_tree_size = cpu_to_le32(tree_size);
		sg.sg_hash_tree_offset = cpu_to_le64(tree_area_pos);
	}
	memcpy(sg.sg_root_hash, root_hash.data, root_hash.len);

	/* Write a hash tree metadata record pointing to the hash tree above. */
	result = append_md_to_backing_file(bfc, &sg.sg_header);
err:
	if (result) {
		/* Error, rollback file changes */
		truncate_backing_file(bfc, rollback_pos);
	}
	return result;
}

/*
 * Write a backing file header
 * It should always be called only on empty file.
 * incfs_super_block.s_first_md_offset is 0 for now, but will be updated
 * once first metadata record is added.
 */
int incfs_write_fh_to_backing_file(struct backing_file_context *bfc,
				   incfs_uuid_t *uuid, u64 file_size)
{
	struct incfs_file_header fh = {};
	loff_t file_pos = 0;

	if (!bfc)
		return -EFAULT;

	fh.fh_magic = cpu_to_le64(INCFS_MAGIC_NUMBER);
	fh.fh_version = cpu_to_le64(INCFS_FORMAT_CURRENT_VER);
	fh.fh_header_size = cpu_to_le16(sizeof(fh));
	fh.fh_first_md_offset = cpu_to_le64(0);
	fh.fh_data_block_size = cpu_to_le16(INCFS_DATA_FILE_BLOCK_SIZE);

	fh.fh_file_size = cpu_to_le64(file_size);
	fh.fh_uuid = *uuid;

	LOCK_REQUIRED(bfc->bc_mutex);

	file_pos = incfs_get_end_offset(bfc->bc_file);
	if (file_pos != 0)
		return -EEXIST;

	return write_to_bf(bfc, &fh, sizeof(fh), file_pos, true);
}

/* Write a given data block and update file's blockmap to point it. */
int incfs_write_data_block_to_backing_file(struct backing_file_context *bfc,
				     struct mem_range block, int block_index,
				     loff_t bm_base_off, u16 flags)
{
	struct incfs_blockmap_entry bm_entry = {};
	int result = 0;
	loff_t data_offset = 0;
	loff_t bm_entry_off =
		bm_base_off + sizeof(struct incfs_blockmap_entry) * block_index;

	if (!bfc)
		return -EFAULT;

	if (block.len >= (1 << 16) || block_index < 0)
		return -EINVAL;

	LOCK_REQUIRED(bfc->bc_mutex);

	data_offset = incfs_get_end_offset(bfc->bc_file);
	if (data_offset <= bm_entry_off) {
		/* Blockmap entry is beyond the file's end. It is not normal. */
		return -EINVAL;
	}

	/* Write the block data at the end of the backing file. */
	result = write_to_bf(bfc, block.data, block.len, data_offset, false);
	if (result)
		return result;

	/* Update the blockmap to point to the newly written data. */
	bm_entry.me_data_offset_lo = cpu_to_le32((u32)data_offset);
	bm_entry.me_data_offset_hi = cpu_to_le16((u16)(data_offset >> 32));
	bm_entry.me_data_size = cpu_to_le16((u16)block.len);
	bm_entry.me_flags = cpu_to_le16(flags);

	result = write_to_bf(bfc, &bm_entry, sizeof(bm_entry),
				bm_entry_off, false);
	return result;
}

int incfs_write_hash_block_to_backing_file(struct backing_file_context *bfc,
					struct mem_range block,
					int block_index, loff_t hash_area_off)
{
	loff_t data_offset = 0;
	loff_t file_end = 0;


	if (!bfc)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);

	data_offset = hash_area_off + block_index * INCFS_DATA_FILE_BLOCK_SIZE;
	file_end = incfs_get_end_offset(bfc->bc_file);
	if (data_offset + block.len > file_end) {
		/* Block is located beyond the file's end. It is not normal. */
		return -EINVAL;
	}

	return write_to_bf(bfc, block.data, block.len, data_offset, false);
}

/* Initialize a new image in a given backing file. */
int incfs_make_empty_backing_file(struct backing_file_context *bfc,
				  incfs_uuid_t *uuid, u64 file_size)
{
	int result = 0;

	if (!bfc || !bfc->bc_file)
		return -EFAULT;

	result = mutex_lock_interruptible(&bfc->bc_mutex);
	if (result)
		goto out;

	result = truncate_backing_file(bfc, 0);
	if (result)
		goto out;

	result = incfs_write_fh_to_backing_file(bfc, uuid, file_size);
out:
	mutex_unlock(&bfc->bc_mutex);
	return result;
}

int incfs_read_blockmap_entry(struct backing_file_context *bfc, int block_index,
			loff_t bm_base_off,
			struct incfs_blockmap_entry *bm_entry)
{
	return incfs_read_blockmap_entries(bfc, bm_entry, block_index, 1,
		bm_base_off);
}

int incfs_read_blockmap_entries(struct backing_file_context *bfc,
		struct incfs_blockmap_entry *entries,
		int start_index, int blocks_number,
		loff_t bm_base_off)
{
	loff_t bm_entry_off =
		bm_base_off + sizeof(struct incfs_blockmap_entry) * start_index;
	const size_t bytes_to_read = sizeof(struct incfs_blockmap_entry)
					* blocks_number;
	int result = 0;

	if (!bfc || !entries)
		return -EFAULT;

	if (start_index < 0 || bm_base_off <= 0)
		return -ENODATA;

	result = incfs_kread(bfc->bc_file, entries, bytes_to_read,
			     bm_entry_off);
	if (result < 0)
		return result;
	if (result < bytes_to_read)
		return -EIO;
	return 0;
}


int incfs_read_file_header(struct backing_file_context *bfc,
			   loff_t *first_md_off, incfs_uuid_t *uuid,
			   u64 *file_size)
{
	ssize_t bytes_read = 0;
	struct incfs_file_header fh = {};

	if (!bfc || !first_md_off)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);
	bytes_read = incfs_kread(bfc->bc_file, &fh, sizeof(fh), 0);
	if (bytes_read < 0)
		return bytes_read;

	if (bytes_read < sizeof(fh))
		return -EBADMSG;

	if (le64_to_cpu(fh.fh_magic) != INCFS_MAGIC_NUMBER)
		return -EILSEQ;

	if (le64_to_cpu(fh.fh_version) > INCFS_FORMAT_CURRENT_VER)
		return -EILSEQ;

	if (le16_to_cpu(fh.fh_data_block_size) != INCFS_DATA_FILE_BLOCK_SIZE)
		return -EILSEQ;

	if (le16_to_cpu(fh.fh_header_size) != sizeof(fh))
		return -EILSEQ;

	if (first_md_off)
		*first_md_off = le64_to_cpu(fh.fh_first_md_offset);
	if (uuid)
		*uuid = fh.fh_uuid;
	if (file_size)
		*file_size = le64_to_cpu(fh.fh_file_size);
	return 0;
}

/*
 * Read through metadata records from the backing file one by one
 * and call provided metadata handlers.
 */
int incfs_read_next_metadata_record(struct backing_file_context *bfc,
			      struct metadata_handler *handler)
{
	const ssize_t max_md_size = INCFS_MAX_METADATA_RECORD_SIZE;
	ssize_t bytes_read = 0;
	size_t md_record_size = 0;
	loff_t next_record = 0;
	loff_t prev_record = 0;
	int res = 0;
	struct incfs_md_header *md_hdr = NULL;

	if (!bfc || !handler)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);

	if (handler->md_record_offset == 0)
		return -EPERM;

	memset(&handler->md_buffer, 0, max_md_size);
	bytes_read = incfs_kread(bfc->bc_file, &handler->md_buffer,
				 max_md_size, handler->md_record_offset);
	if (bytes_read < 0)
		return bytes_read;
	if (bytes_read < sizeof(*md_hdr))
		return -EBADMSG;

	md_hdr = &handler->md_buffer.md_header;
	next_record = le64_to_cpu(md_hdr->h_next_md_offset);
	prev_record = le64_to_cpu(md_hdr->h_prev_md_offset);
	md_record_size = le16_to_cpu(md_hdr->h_record_size);

	if (md_record_size > max_md_size) {
		pr_warn("incfs: The record is too large. Size: %ld",
				md_record_size);
		return -EBADMSG;
	}

	if (bytes_read < md_record_size) {
		pr_warn("incfs: The record hasn't been fully read.");
		return -EBADMSG;
	}

	if (next_record <= handler->md_record_offset && next_record != 0) {
		pr_warn("incfs: Next record (%lld) points back in file.",
			next_record);
		return -EBADMSG;
	}

	if (prev_record != handler->md_prev_record_offset) {
		pr_warn("incfs: Metadata chain has been corrupted.");
		return -EBADMSG;
	}

	if (le32_to_cpu(md_hdr->h_record_crc) != calc_md_crc(md_hdr)) {
		pr_warn("incfs: Metadata CRC mismatch.");
		return -EBADMSG;
	}

	switch (md_hdr->h_md_entry_type) {
	case INCFS_MD_NONE:
		break;
	case INCFS_MD_BLOCK_MAP:
		if (handler->handle_blockmap)
			res = handler->handle_blockmap(
				&handler->md_buffer.blockmap, handler);
		break;
	case INCFS_MD_FILE_ATTR:
		if (handler->handle_file_attr)
			res = handler->handle_file_attr(
				&handler->md_buffer.file_attr, handler);
		break;
	case INCFS_MD_SIGNATURE:
		if (handler->handle_signature)
			res = handler->handle_signature(
				&handler->md_buffer.signature, handler);
		break;
	default:
		res = -ENOTSUPP;
		break;
	}

	if (!res) {
		if (next_record == 0) {
			/*
			 * Zero offset for the next record means that the last
			 * metadata record has just been processed.
			 */
			bfc->bc_last_md_record_offset =
				handler->md_record_offset;
		}
		handler->md_prev_record_offset = handler->md_record_offset;
		handler->md_record_offset = next_record;
	}
	return res;
}

ssize_t incfs_kread(struct file *f, void *buf, size_t size, loff_t pos)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	return kernel_read(f, pos, (char *)buf, size);
#else
	return kernel_read(f, buf, size, &pos);
#endif
}

ssize_t incfs_kwrite(struct file *f, const void *buf, size_t size, loff_t pos)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	return kernel_write(f, buf, size, pos);
#else
	return kernel_write(f, buf, size, &pos);
#endif
}
