/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUBPAGE_H
#define BTRFS_SUBPAGE_H

#include <linux/spinlock.h>
#include <linux/atomic.h>

struct address_space;
struct folio;
struct btrfs_fs_info;

/*
 * Extra info for subpapge bitmap.
 *
 * For subpage we pack all uptodate/dirty/writeback/ordered bitmaps into
 * one larger bitmap.
 *
 * This structure records how they are organized in the bitmap:
 *
 * /- uptodate_offset	/- dirty_offset	/- ordered_offset
 * |			|		|
 * v			v		v
 * |u|u|u|u|........|u|u|d|d|.......|d|d|o|o|.......|o|o|
 * |<- bitmap_nr_bits ->|
 * |<----------------- total_nr_bits ------------------>|
 */
struct btrfs_subpage_info {
	/* Number of bits for each bitmap */
	unsigned int bitmap_nr_bits;

	/* Total number of bits for the whole bitmap */
	unsigned int total_nr_bits;

	/*
	 * *_offset indicates where the bitmap starts, the length is always
	 * @bitmap_size, which is calculated from PAGE_SIZE / sectorsize.
	 */
	unsigned int uptodate_offset;
	unsigned int dirty_offset;
	unsigned int writeback_offset;
	unsigned int ordered_offset;
	unsigned int checked_offset;

	/*
	 * For locked bitmaps, normally it's subpage representation for folio
	 * Locked flag, but metadata is different:
	 *
	 * - Metadata doesn't really lock the folio
	 *   It's just to prevent page::private get cleared before the last
	 *   end_page_read().
	 */
	unsigned int locked_offset;
};

/*
 * Structure to trace status of each sector inside a page, attached to
 * page::private for both data and metadata inodes.
 */
struct btrfs_subpage {
	/* Common members for both data and metadata pages */
	spinlock_t lock;
	/*
	 * Both data and metadata needs to track how many readers are for the
	 * page.
	 * Data relies on @readers to unlock the page when last reader finished.
	 * While metadata doesn't need page unlock, it needs to prevent
	 * page::private get cleared before the last end_page_read().
	 */
	atomic_t readers;
	union {
		/*
		 * Structures only used by metadata
		 *
		 * @eb_refs should only be operated under private_lock, as it
		 * manages whether the subpage can be detached.
		 */
		atomic_t eb_refs;

		/* Structures only used by data */
		atomic_t writers;
	};
	unsigned long bitmaps[];
};

enum btrfs_subpage_type {
	BTRFS_SUBPAGE_METADATA,
	BTRFS_SUBPAGE_DATA,
};

bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info, struct address_space *mapping);

void btrfs_init_subpage_info(struct btrfs_subpage_info *subpage_info, u32 sectorsize);
int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct folio *folio, enum btrfs_subpage_type type);
void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info, struct folio *folio);

/* Allocate additional data where page represents more than one sector */
struct btrfs_subpage *btrfs_alloc_subpage(const struct btrfs_fs_info *fs_info,
					  enum btrfs_subpage_type type);
void btrfs_free_subpage(struct btrfs_subpage *subpage);

void btrfs_folio_inc_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio);
void btrfs_folio_dec_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio);

void btrfs_subpage_start_reader(const struct btrfs_fs_info *fs_info,
				struct folio *folio, u64 start, u32 len);
void btrfs_subpage_end_reader(const struct btrfs_fs_info *fs_info,
			      struct folio *folio, u64 start, u32 len);

int btrfs_folio_start_writer_lock(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len);
void btrfs_folio_end_writer_lock(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len);
void btrfs_folio_set_writer_lock(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len);
bool btrfs_subpage_find_writer_locked(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 search_start,
				      u64 *found_start_ret, u32 *found_len_ret);
void btrfs_folio_end_all_writers(const struct btrfs_fs_info *fs_info, struct folio *folio);

/*
 * Template for subpage related operations.
 *
 * btrfs_subpage_*() are for call sites where the folio has subpage attached and
 * the range is ensured to be inside the folio's single page.
 *
 * btrfs_folio_*() are for call sites where the page can either be subpage
 * specific or regular folios. The function will handle both cases.
 * But the range still needs to be inside one single page.
 *
 * btrfs_folio_clamp_*() are similar to btrfs_folio_*(), except the range doesn't
 * need to be inside the page. Those functions will truncate the range
 * automatically.
 */
#define DECLARE_BTRFS_SUBPAGE_OPS(name)					\
void btrfs_subpage_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
void btrfs_subpage_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
bool btrfs_subpage_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
void btrfs_folio_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
void btrfs_folio_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
bool btrfs_folio_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
void btrfs_folio_clamp_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
void btrfs_folio_clamp_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);			\
bool btrfs_folio_clamp_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct folio *folio, u64 start, u32 len);

DECLARE_BTRFS_SUBPAGE_OPS(uptodate);
DECLARE_BTRFS_SUBPAGE_OPS(dirty);
DECLARE_BTRFS_SUBPAGE_OPS(writeback);
DECLARE_BTRFS_SUBPAGE_OPS(ordered);
DECLARE_BTRFS_SUBPAGE_OPS(checked);

bool btrfs_subpage_clear_and_test_dirty(const struct btrfs_fs_info *fs_info,
					struct folio *folio, u64 start, u32 len);

void btrfs_folio_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len);
void btrfs_folio_unlock_writer(struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len);
void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 start, u32 len);

#endif
