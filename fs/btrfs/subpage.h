/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUBPAGE_H
#define BTRFS_SUBPAGE_H

#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/sizes.h>

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
 * /- uptodate          /- dirty        /- ordered
 * |			|		|
 * v			v		v
 * |u|u|u|u|........|u|u|d|d|.......|d|d|o|o|.......|o|o|
 * |< sectors_per_page >|
 *
 * Unlike regular macro-like enums, here we do not go upper-case names, as
 * these names will be utilized in various macros to define function names.
 */
enum {
	btrfs_bitmap_nr_uptodate = 0,
	btrfs_bitmap_nr_dirty,
	btrfs_bitmap_nr_writeback,
	btrfs_bitmap_nr_ordered,
	btrfs_bitmap_nr_checked,
	btrfs_bitmap_nr_locked,
	btrfs_bitmap_nr_max
};

/*
 * Structure to trace status of each sector inside a page, attached to
 * page::private for both data and metadata inodes.
 */
struct btrfs_subpage {
	/* Common members for both data and metadata pages */
	spinlock_t lock;
	union {
		/*
		 * Structures only used by metadata
		 *
		 * @eb_refs should only be operated under private_lock, as it
		 * manages whether the subpage can be detached.
		 */
		atomic_t eb_refs;

		/*
		 * Structures only used by data,
		 *
		 * How many sectors inside the page is locked.
		 */
		atomic_t nr_locked;
	};
	unsigned long bitmaps[];
};

enum btrfs_subpage_type {
	BTRFS_SUBPAGE_METADATA,
	BTRFS_SUBPAGE_DATA,
};

#if PAGE_SIZE > SZ_4K
bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info, struct address_space *mapping);
#else
static inline bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info,
				    struct address_space *mapping)
{
	return false;
}
#endif

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct folio *folio, enum btrfs_subpage_type type);
void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info, struct folio *folio);

/* Allocate additional data where page represents more than one sector */
struct btrfs_subpage *btrfs_alloc_subpage(const struct btrfs_fs_info *fs_info,
					  enum btrfs_subpage_type type);
void btrfs_free_subpage(struct btrfs_subpage *subpage);

void btrfs_folio_inc_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio);
void btrfs_folio_dec_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio);

void btrfs_folio_end_lock(const struct btrfs_fs_info *fs_info,
			  struct folio *folio, u64 start, u32 len);
void btrfs_folio_set_lock(const struct btrfs_fs_info *fs_info,
			  struct folio *folio, u64 start, u32 len);
void btrfs_folio_end_lock_bitmap(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, unsigned long bitmap);
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
void btrfs_get_subpage_dirty_bitmap(struct btrfs_fs_info *fs_info,
				    struct folio *folio,
				    unsigned long *ret_bitmap);
void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 start, u32 len);

#endif
