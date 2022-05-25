// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring_types.h"
#include "io_uring.h"

int io_file_bitmap_get(struct io_ring_ctx *ctx)
{
	struct io_file_table *table = &ctx->file_table;
	unsigned long nr = ctx->nr_user_files;
	int ret;

	do {
		ret = find_next_zero_bit(table->bitmap, nr, table->alloc_hint);
		if (ret != nr)
			return ret;

		if (!table->alloc_hint)
			break;

		nr = table->alloc_hint;
		table->alloc_hint = 0;
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
