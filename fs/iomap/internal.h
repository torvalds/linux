/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IOMAP_INTERNAL_H
#define _IOMAP_INTERNAL_H 1

#define IOEND_BATCH_SIZE	4096

/*
 * Normally we can build bios as big as the data structure supports.
 *
 * But for integrity protected I/O we need to respect the maximum size of the
 * single contiguous allocation for the integrity buffer.
 */
static inline size_t iomap_max_bio_size(const struct iomap *iomap)
{
	if (iomap->flags & IOMAP_F_INTEGRITY)
		return max_integrity_io_size(bdev_limits(iomap->bdev));
	return BIO_MAX_SIZE;
}

u32 iomap_finish_ioend_buffered_read(struct iomap_ioend *ioend);
u32 iomap_finish_ioend_direct(struct iomap_ioend *ioend);

#ifdef CONFIG_BLOCK
int iomap_bio_read_folio_range_sync(const struct iomap_iter *iter,
		struct folio *folio, loff_t pos, size_t len);
#else
static inline int iomap_bio_read_folio_range_sync(const struct iomap_iter *iter,
		struct folio *folio, loff_t pos, size_t len)
{
	WARN_ON_ONCE(1);
	return -EIO;
}
#endif /* CONFIG_BLOCK */

#endif /* _IOMAP_INTERNAL_H */
