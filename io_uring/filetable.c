// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/nospec.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "rsrc.h"
#include "filetable.h"

static int io_file_bitmap_get(struct io_ring_ctx *ctx)
{
	struct io_file_table *table = &ctx->file_table;
	unsigned long nr = ctx->file_alloc_end;
	int ret;

	if (!table->bitmap)
		return -ENFILE;

	do {
		ret = find_next_zero_bit(table->bitmap, nr, table->alloc_hint);
		if (ret != nr)
			return ret;

		if (table->alloc_hint == ctx->file_alloc_start)
			break;
		nr = table->alloc_hint;
		table->alloc_hint = ctx->file_alloc_start;
	} while (1);

	return -ENFILE;
}

bool io_alloc_file_tables(struct io_file_table *table, unsigned nr_files)
{
	table->files = kvcalloc(nr_files, sizeof(table->files[0]),
				GFP_KERNEL_ACCOUNT);
	if (unlikely(!table->files))
		return false;

	table->bitmap = bitmap_zalloc(nr_files, GFP_KERNEL_ACCOUNT);
	if (unlikely(!table->bitmap)) {
		kvfree(table->files);
		return false;
	}

	return true;
}

void io_free_file_tables(struct io_file_table *table)
{
	kvfree(table->files);
	bitmap_free(table->bitmap);
	table->files = NULL;
	table->bitmap = NULL;
}

static int io_install_fixed_file(struct io_ring_ctx *ctx, struct file *file,
				 u32 slot_index)
	__must_hold(&req->ctx->uring_lock)
{
	bool needs_switch = false;
	struct io_fixed_file *file_slot;
	int ret;

	if (io_is_uring_fops(file))
		return -EBADF;
	if (!ctx->file_data)
		return -ENXIO;
	if (slot_index >= ctx->nr_user_files)
		return -EINVAL;

	slot_index = array_index_nospec(slot_index, ctx->nr_user_files);
	file_slot = io_fixed_file_slot(&ctx->file_table, slot_index);

	if (file_slot->file_ptr) {
		struct file *old_file;

		ret = io_rsrc_node_switch_start(ctx);
		if (ret)
			goto err;

		old_file = (struct file *)(file_slot->file_ptr & FFS_MASK);
		ret = io_queue_rsrc_removal(ctx->file_data, slot_index,
					    ctx->rsrc_node, old_file);
		if (ret)
			goto err;
		file_slot->file_ptr = 0;
		io_file_bitmap_clear(&ctx->file_table, slot_index);
		needs_switch = true;
	}

	*io_get_tag_slot(ctx->file_data, slot_index) = 0;
	io_fixed_file_set(file_slot, file);
	io_file_bitmap_set(&ctx->file_table, slot_index);
	return 0;
err:
	if (needs_switch)
		io_rsrc_node_switch(ctx, ctx->file_data);
	return ret;
}

int __io_fixed_fd_install(struct io_ring_ctx *ctx, struct file *file,
			  unsigned int file_slot)
{
	bool alloc_slot = file_slot == IORING_FILE_INDEX_ALLOC;
	int ret;

	if (alloc_slot) {
		ret = io_file_bitmap_get(ctx);
		if (unlikely(ret < 0))
			return ret;
		file_slot = ret;
	} else {
		file_slot--;
	}

	ret = io_install_fixed_file(ctx, file, file_slot);
	if (!ret && alloc_slot)
		ret = file_slot;
	return ret;
}
/*
 * Note when io_fixed_fd_install() returns error value, it will ensure
 * fput() is called correspondingly.
 */
int io_fixed_fd_install(struct io_kiocb *req, unsigned int issue_flags,
			struct file *file, unsigned int file_slot)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	io_ring_submit_lock(ctx, issue_flags);
	ret = __io_fixed_fd_install(ctx, file, file_slot);
	io_ring_submit_unlock(ctx, issue_flags);

	if (unlikely(ret < 0))
		fput(file);
	return ret;
}

int io_fixed_fd_remove(struct io_ring_ctx *ctx, unsigned int offset)
{
	struct io_fixed_file *file_slot;
	struct file *file;
	int ret;

	if (unlikely(!ctx->file_data))
		return -ENXIO;
	if (offset >= ctx->nr_user_files)
		return -EINVAL;
	ret = io_rsrc_node_switch_start(ctx);
	if (ret)
		return ret;

	offset = array_index_nospec(offset, ctx->nr_user_files);
	file_slot = io_fixed_file_slot(&ctx->file_table, offset);
	if (!file_slot->file_ptr)
		return -EBADF;

	file = (struct file *)(file_slot->file_ptr & FFS_MASK);
	ret = io_queue_rsrc_removal(ctx->file_data, offset, ctx->rsrc_node, file);
	if (ret)
		return ret;

	file_slot->file_ptr = 0;
	io_file_bitmap_clear(&ctx->file_table, offset);
	io_rsrc_node_switch(ctx, ctx->file_data);
	return 0;
}

int io_register_file_alloc_range(struct io_ring_ctx *ctx,
				 struct io_uring_file_index_range __user *arg)
{
	struct io_uring_file_index_range range;
	u32 end;

	if (copy_from_user(&range, arg, sizeof(range)))
		return -EFAULT;
	if (check_add_overflow(range.off, range.len, &end))
		return -EOVERFLOW;
	if (range.resv || end > ctx->nr_user_files)
		return -EINVAL;

	io_file_table_set_alloc_range(ctx, range.off, range.len);
	return 0;
}
