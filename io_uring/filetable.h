// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_FILE_TABLE_H
#define IOU_FILE_TABLE_H

struct io_ring_ctx;

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

struct io_fixed_file {
	/* file * with additional FFS_* flags */
	unsigned long file_ptr;
};

struct io_file_table {
	struct io_fixed_file *files;
	unsigned long *bitmap;
	unsigned int alloc_hint;
};

bool io_alloc_file_tables(struct io_file_table *table, unsigned nr_files);
void io_free_file_tables(struct io_file_table *table);
int io_file_bitmap_get(struct io_ring_ctx *ctx);

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

#endif
