/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _TTM_BACKUP_H_
#define _TTM_BACKUP_H_

#include <linux/mm_types.h>
#include <linux/shmem_fs.h>

struct ttm_backup;

/**
 * ttm_backup_handle_to_page_ptr() - Convert handle to struct page pointer
 * @handle: The handle to convert.
 *
 * Converts an opaque handle received from the
 * struct ttm_backoup_ops::backup_page() function to an (invalid)
 * struct page pointer suitable for a struct page array.
 *
 * Return: An (invalid) struct page pointer.
 */
static inline struct page *
ttm_backup_handle_to_page_ptr(unsigned long handle)
{
	return (struct page *)(handle << 1 | 1);
}

/**
 * ttm_backup_page_ptr_is_handle() - Whether a struct page pointer is a handle
 * @page: The struct page pointer to check.
 *
 * Return: true if the struct page pointer is a handld returned from
 * ttm_backup_handle_to_page_ptr(). False otherwise.
 */
static inline bool ttm_backup_page_ptr_is_handle(const struct page *page)
{
	return (unsigned long)page & 1;
}

/**
 * ttm_backup_page_ptr_to_handle() - Convert a struct page pointer to a handle
 * @page: The struct page pointer to convert
 *
 * Return: The handle that was previously used in
 * ttm_backup_handle_to_page_ptr() to obtain a struct page pointer, suitable
 * for use as argument in the struct ttm_backup_ops drop() or
 * copy_backed_up_page() functions.
 */
static inline unsigned long
ttm_backup_page_ptr_to_handle(const struct page *page)
{
	WARN_ON(!ttm_backup_page_ptr_is_handle(page));
	return (unsigned long)page >> 1;
}

void ttm_backup_drop(struct ttm_backup *backup, pgoff_t handle);

int ttm_backup_copy_page(struct ttm_backup *backup, struct page *dst,
			 pgoff_t handle, bool intr);

s64
ttm_backup_backup_page(struct ttm_backup *backup, struct page *page,
		       bool writeback, pgoff_t idx, gfp_t page_gfp,
		       gfp_t alloc_gfp);

void ttm_backup_fini(struct ttm_backup *backup);

u64 ttm_backup_bytes_avail(void);

struct ttm_backup *ttm_backup_shmem_create(loff_t size);

#endif
