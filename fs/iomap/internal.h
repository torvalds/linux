/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IOMAP_INTERNAL_H
#define _IOMAP_INTERNAL_H 1

#define IOEND_BATCH_SIZE	4096

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
