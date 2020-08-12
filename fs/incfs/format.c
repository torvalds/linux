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

#include "format.h"
#include "data_mgmt.h"

struct backing_file_context *incfs_alloc_bfc(struct mount_info *mi,
					     struct file *backing_file)
{
	struct backing_file_context *result = NULL;

	result = kzalloc(sizeof(*result), GFP_NOFS);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->bc_file = get_file(backing_file);
	result->bc_cred = mi->mi_owner;
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

static loff_t incfs_get_end_offset(struct file *f)
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

static int write_to_bf(struct backing_file_context *bfc, const void *buf,
			size_t count, loff_t pos)
{
	ssize_t res = incfs_kwrite(bfc, buf, count, pos);

	if (res < 0)
		return res;
	if (res != count)
		return -EIO;
	return 0;
}

static int append_zeros_no_fallocate(struct backing_file_context *bfc,
				     size_t file_size, size_t len)
{
	u8 buffer[256] = {};
	size_t i;

	for (i = 0; i < len; i += sizeof(buffer)) {
		int to_write = len - i > sizeof(buffer)
			? sizeof(buffer) : len - i;
		int err = write_to_bf(bfc, buffer, to_write, file_size + i);

		if (err)
			return err;
	}

	return 0;
}

/* Append a given number of zero bytes to the end of the backing file. */
static int append_zeros(struct backing_file_context *bfc, size_t len)
{
	loff_t file_size = 0;
	loff_t new_last_byte_offset = 0;
	int result;

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
	result = vfs_fallocate(bfc->bc_file, 0, new_last_byte_offset, 1);
	if (result != -EOPNOTSUPP)
		return result;

	return append_zeros_no_fallocate(bfc, file_size, len);
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
	record->h_next_md_offset = 0;

	/* Write the metadata record to the end of the backing file */
	record_offset = file_pos;
	new_md_offset = cpu_to_le64(record_offset);
	result = write_to_bf(bfc, record, record_size, file_pos);
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
			     file_pos);
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
					 u32 block_count)
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
	if (result)
		/* Error, rollback file changes */
		truncate_backing_file(bfc, file_end);

	return result;
}

int incfs_write_signature_to_backing_file(struct backing_file_context *bfc,
					  struct mem_range sig, u32 tree_size)
{
	struct incfs_file_signature sg = {};
	int result = 0;
	loff_t rollback_pos = 0;
	loff_t tree_area_pos = 0;
	size_t alignment = 0;

	if (!bfc)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);

	rollback_pos = incfs_get_end_offset(bfc->bc_file);

	sg.sg_header.h_md_entry_type = INCFS_MD_SIGNATURE;
	sg.sg_header.h_record_size = cpu_to_le16(sizeof(sg));
	sg.sg_header.h_next_md_offset = cpu_to_le64(0);
	if (sig.data != NULL && sig.len > 0) {
		loff_t pos = incfs_get_end_offset(bfc->bc_file);

		sg.sg_sig_size = cpu_to_le32(sig.len);
		sg.sg_sig_offset = cpu_to_le64(pos);

		result = write_to_bf(bfc, sig.data, sig.len, pos);
		if (result)
			goto err;
	}

	tree_area_pos = incfs_get_end_offset(bfc->bc_file);
	if (tree_size > 0) {
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

	/* Write a hash tree metadata record pointing to the hash tree above. */
	result = append_md_to_backing_file(bfc, &sg.sg_header);
err:
	if (result)
		/* Error, rollback file changes */
		truncate_backing_file(bfc, rollback_pos);
	return result;
}

static int write_new_status_to_backing_file(struct backing_file_context *bfc,
				       u32 data_blocks_written,
				       u32 hash_blocks_written)
{
	int result;
	loff_t rollback_pos;
	struct incfs_status is = {
		.is_header = {
			.h_md_entry_type = INCFS_MD_STATUS,
			.h_record_size = cpu_to_le16(sizeof(is)),
		},
		.is_data_blocks_written = cpu_to_le32(data_blocks_written),
		.is_hash_blocks_written = cpu_to_le32(hash_blocks_written),
	};

	LOCK_REQUIRED(bfc->bc_mutex);
	rollback_pos = incfs_get_end_offset(bfc->bc_file);
	result = append_md_to_backing_file(bfc, &is.is_header);
	if (result)
		truncate_backing_file(bfc, rollback_pos);

	return result;
}

int incfs_write_status_to_backing_file(struct backing_file_context *bfc,
				       loff_t status_offset,
				       u32 data_blocks_written,
				       u32 hash_blocks_written)
{
	struct incfs_status is;
	int result;

	if (!bfc)
		return -EFAULT;

	if (status_offset == 0)
		return write_new_status_to_backing_file(bfc,
				data_blocks_written, hash_blocks_written);

	result = incfs_kread(bfc, &is, sizeof(is), status_offset);
	if (result != sizeof(is))
		return -EIO;

	is.is_data_blocks_written = cpu_to_le32(data_blocks_written);
	is.is_hash_blocks_written = cpu_to_le32(hash_blocks_written);
	result = incfs_kwrite(bfc, &is, sizeof(is), status_offset);
	if (result != sizeof(is))
		return -EIO;

	return 0;
}

int incfs_write_verity_signature_to_backing_file(
		struct backing_file_context *bfc, struct mem_range signature,
		loff_t *offset)
{
	struct incfs_file_verity_signature vs = {};
	int result;
	loff_t pos;

	/* No verity signature section is equivalent to an empty section */
	if (signature.data == NULL || signature.len == 0)
		return 0;

	pos = incfs_get_end_offset(bfc->bc_file);

	vs = (struct incfs_file_verity_signature) {
		.vs_header = (struct incfs_md_header) {
			.h_md_entry_type = INCFS_MD_VERITY_SIGNATURE,
			.h_record_size = cpu_to_le16(sizeof(vs)),
			.h_next_md_offset = cpu_to_le64(0),
		},
		.vs_size = cpu_to_le32(signature.len),
		.vs_offset = cpu_to_le64(pos),
	};

	result = write_to_bf(bfc, signature.data, signature.len, pos);
	if (result)
		goto err;

	result = append_md_to_backing_file(bfc, &vs.vs_header);
	if (result)
		goto err;

	*offset = pos;
err:
	if (result)
		/* Error, rollback file changes */
		truncate_backing_file(bfc, pos);
	return result;
}

/*
 * Write a backing file header
 * It should always be called only on empty file.
 * fh.fh_first_md_offset is 0 for now, but will be updated
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

	return write_to_bf(bfc, &fh, sizeof(fh), file_pos);
}

/*
 * Write a backing file header for a mapping file
 * It should always be called only on empty file.
 */
int incfs_write_mapping_fh_to_backing_file(struct backing_file_context *bfc,
				incfs_uuid_t *uuid, u64 file_size, u64 offset)
{
	struct incfs_file_header fh = {};
	loff_t file_pos = 0;

	if (!bfc)
		return -EFAULT;

	fh.fh_magic = cpu_to_le64(INCFS_MAGIC_NUMBER);
	fh.fh_version = cpu_to_le64(INCFS_FORMAT_CURRENT_VER);
	fh.fh_header_size = cpu_to_le16(sizeof(fh));
	fh.fh_original_offset = cpu_to_le64(offset);
	fh.fh_data_block_size = cpu_to_le16(INCFS_DATA_FILE_BLOCK_SIZE);

	fh.fh_mapped_file_size = cpu_to_le64(file_size);
	fh.fh_original_uuid = *uuid;
	fh.fh_flags = cpu_to_le32(INCFS_FILE_MAPPED);

	LOCK_REQUIRED(bfc->bc_mutex);

	file_pos = incfs_get_end_offset(bfc->bc_file);
	if (file_pos != 0)
		return -EEXIST;

	return write_to_bf(bfc, &fh, sizeof(fh), file_pos);
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
	result = write_to_bf(bfc, block.data, block.len, data_offset);
	if (result)
		return result;

	/* Update the blockmap to point to the newly written data. */
	bm_entry.me_data_offset_lo = cpu_to_le32((u32)data_offset);
	bm_entry.me_data_offset_hi = cpu_to_le16((u16)(data_offset >> 32));
	bm_entry.me_data_size = cpu_to_le16((u16)block.len);
	bm_entry.me_flags = cpu_to_le16(flags);

	return write_to_bf(bfc, &bm_entry, sizeof(bm_entry),
				bm_entry_off);
}

int incfs_write_hash_block_to_backing_file(struct backing_file_context *bfc,
					   struct mem_range block,
					   int block_index,
					   loff_t hash_area_off,
					   loff_t bm_base_off,
					   loff_t file_size)
{
	struct incfs_blockmap_entry bm_entry = {};
	int result;
	loff_t data_offset = 0;
	loff_t file_end = 0;
	loff_t bm_entry_off =
		bm_base_off +
		sizeof(struct incfs_blockmap_entry) *
			(block_index + get_blocks_count_for_size(file_size));

	if (!bfc)
		return -EFAULT;

	LOCK_REQUIRED(bfc->bc_mutex);

	data_offset = hash_area_off + block_index * INCFS_DATA_FILE_BLOCK_SIZE;
	file_end = incfs_get_end_offset(bfc->bc_file);
	if (data_offset + block.len > file_end) {
		/* Block is located beyond the file's end. It is not normal. */
		return -EINVAL;
	}

	result = write_to_bf(bfc, block.data, block.len, data_offset);
	if (result)
		return result;

	bm_entry.me_data_offset_lo = cpu_to_le32((u32)data_offset);
	bm_entry.me_data_offset_hi = cpu_to_le16((u16)(data_offset >> 32));
	bm_entry.me_data_size = cpu_to_le16(INCFS_DATA_FILE_BLOCK_SIZE);

	return write_to_bf(bfc, &bm_entry, sizeof(bm_entry), bm_entry_off);
}

int incfs_read_blockmap_entry(struct backing_file_context *bfc, int block_index,
			loff_t bm_base_off,
			struct incfs_blockmap_entry *bm_entry)
{
	int error = incfs_read_blockmap_entries(bfc, bm_entry, block_index, 1,
						bm_base_off);

	if (error < 0)
		return error;

	if (error == 0)
		return -EIO;

	if (error != 1)
		return -EFAULT;

	return 0;
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

	result = incfs_kread(bfc, entries, bytes_to_read, bm_entry_off);
	if (result < 0)
		return result;
	return result / sizeof(*entries);
}

int incfs_read_file_header(struct backing_file_context *bfc,
			   loff_t *first_md_off, incfs_uuid_t *uuid,
			   u64 *file_size, u32 *flags)
{
	ssize_t bytes_read = 0;
	struct incfs_file_header fh = {};

	if (!bfc || !first_md_off)
		return -EFAULT;

	bytes_read = incfs_kread(bfc, &fh, sizeof(fh), 0);
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
	if (flags)
		*flags = le32_to_cpu(fh.fh_flags);
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
	int res = 0;
	struct incfs_md_header *md_hdr = NULL;

	if (!bfc || !handler)
		return -EFAULT;

	if (handler->md_record_offset == 0)
		return -EPERM;

	memset(&handler->md_buffer, 0, max_md_size);
	bytes_read = incfs_kread(bfc, &handler->md_buffer, max_md_size,
				 handler->md_record_offset);
	if (bytes_read < 0)
		return bytes_read;
	if (bytes_read < sizeof(*md_hdr))
		return -EBADMSG;

	md_hdr = &handler->md_buffer.md_header;
	next_record = le64_to_cpu(md_hdr->h_next_md_offset);
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

	switch (md_hdr->h_md_entry_type) {
	case INCFS_MD_NONE:
		break;
	case INCFS_MD_BLOCK_MAP:
		if (handler->handle_blockmap)
			res = handler->handle_blockmap(
				&handler->md_buffer.blockmap, handler);
		break;
	case INCFS_MD_FILE_ATTR:
		/*
		 * File attrs no longer supported, ignore section for
		 * compatibility
		 */
		break;
	case INCFS_MD_SIGNATURE:
		if (handler->handle_signature)
			res = handler->handle_signature(
				&handler->md_buffer.signature, handler);
		break;
	case INCFS_MD_STATUS:
		if (handler->handle_status)
			res = handler->handle_status(
				&handler->md_buffer.status, handler);
		break;
	case INCFS_MD_VERITY_SIGNATURE:
		if (handler->handle_verity_signature)
			res = handler->handle_verity_signature(
				&handler->md_buffer.verity_signature, handler);
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

ssize_t incfs_kread(struct backing_file_context *bfc, void *buf, size_t size,
		    loff_t pos)
{
	const struct cred *old_cred = override_creds(bfc->bc_cred);
	int ret = kernel_read(bfc->bc_file, buf, size, &pos);

	revert_creds(old_cred);
	return ret;
}

ssize_t incfs_kwrite(struct backing_file_context *bfc, const void *buf,
		     size_t size, loff_t pos)
{
	const struct cred *old_cred = override_creds(bfc->bc_cred);
	int ret = kernel_write(bfc->bc_file, buf, size, &pos);

	revert_creds(old_cred);
	return ret;
}
