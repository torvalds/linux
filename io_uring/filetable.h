// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_FILE_TABLE_H
#define IOU_FILE_TABLE_H

#include <linux/file.h>
#include <linux/io_uring_types.h>

/*
 * FFS_SCM is only available on 64-bit archs, for 32-bit we just define it as 0
 * and define IO_URING_SCM_ALL. For this case, we use SCM for all files as we
 * can't safely always dereference the file when the task has exited and ring
 * cleanup is done. If a file is tracked and part of SCM, then unix gc on
 * process exit may reap it before __io_sqe_files_unregister() is run.
 */
#define FFS_NOWAIT		0x1UL
#define FFS_ISREG		0x2UL
#if defined(CONFIG_64BIT)
#define FFS_SCM			0x4UL
#else
#define IO_URING_SCM_ALL
#define FFS_SCM			0x0UL
#endif
#define FFS_MASK		~(FFS_NOWAIT|FFS_ISREG|FFS_SCM)

bool io_alloc_file_tables(struct io_file_table *table, unsigned nr_files);
void io_free_file_tables(struct io_file_table *table);

int io_fixed_fd_install(struct io_kiocb *req, unsigned int issue_flags,
			struct file *file, unsigned int file_slot);
int __io_fixed_fd_install(struct io_ring_ctx *ctx, struct file *file,
				unsigned int file_slot);
int io_fixed_fd_remove(struct io_ring_ctx *ctx, unsigned int offset);

int io_register_file_alloc_range(struct io_ring_ctx *ctx,
				 struct io_uring_file_index_range __user *arg);

unsigned int io_file_get_flags(struct file *file);

static inline void io_file_bitmap_clear(struct io_file_table *table, int bit)
{
	__clear_bit(bit, table->bitmap);
	table->alloc_hint = bit;
}

static inline void io_file_bitmap_set(struct io_file_table *table, int bit)
{
	WARN_ON_ONCE(test_bit(bit, table->bitmap));
	__set_bit(bit, table->bitmap);
	table->alloc_hint = bit + 1;
}

static inline struct io_fixed_file *
io_fixed_file_slot(struct io_file_table *table, unsigned i)
{
	return &table->files[i];
}

static inline struct file *io_file_from_index(struct io_file_table *table,
					      int index)
{
	struct io_fixed_file *slot = io_fixed_file_slot(table, index);

	return (struct file *) (slot->file_ptr & FFS_MASK);
}

static inline void io_fixed_file_set(struct io_fixed_file *file_slot,
				     struct file *file)
{
	unsigned long file_ptr = (unsigned long) file;

	file_ptr |= io_file_get_flags(file);
	file_slot->file_ptr = file_ptr;
}

static inline void io_reset_alloc_hint(struct io_ring_ctx *ctx)
{
	ctx->file_table.alloc_hint = ctx->file_alloc_start;
}

static inline void io_file_table_set_alloc_range(struct io_ring_ctx *ctx,
						 unsigned off, unsigned len)
{
	ctx->file_alloc_start = off;
	ctx->file_alloc_end = off + len;
	io_reset_alloc_hint(ctx);
}

#endif
