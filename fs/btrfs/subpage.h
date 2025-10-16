/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUBPAGE_H
#define BTRFS_SUBPAGE_H

#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/sizes.h>
#include "btrfs_inode.h"
#include "fs.h"

struct address_space;
struct folio;

/*
 * Extra info for subpage bitmap.
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

	/*
	 * This can be changed to atomic eventually.  But this change will rely
	 * on the async delalloc range rework for locked bitmap.  As async
	 * delalloc can unlock its range and mark blocks writeback at random
	 * timing.
	 */
	btrfs_bitmap_nr_writeback,

	/*
	 * The ordered and checked flags are for COW fixup, already marked
	 * deprecated, and will be removed eventually.
	 */
	btrfs_bitmap_nr_ordered,
	btrfs_bitmap_nr_checked,

	/*
	 * The locked bit is for async delalloc range (compression), currently
	 * async extent is queued with the range locked, until the compression
	 * is done.
	 * So an async extent can unlock the range at any random timing.
	 *
	 * This will need a rework on the async extent lifespan (mark writeback
	 * and do compression) before deprecating this flag.
	 */
	btrfs_bitmap_nr_locked,
	btrfs_bitmap_nr_max
};

/*
 * Structure to trace status of each sector inside a page, attached to
 * page::private for both data and metadata inodes.
 */
struct btrfs_folio_state {
	/* Common members for both data and metadata pages */
	spinlock_t lock;
	union {
		/*
		 * Structures only used by metadata
		 *
		 * @eb_refs should only be operated under private_lock, as it
		 * manages whether the btrfs_folio_state can be detached.
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

enum btrfs_folio_type {
	BTRFS_SUBPAGE_METADATA,
	BTRFS_SUBPAGE_DATA,
};

/*
 * Subpage support for metadata is more complex, as we can have dummy extent
 * buffers, where folios have no mapping to determine the owning inode.
 *
 * Thankfully we only need to check if node size is smaller than page size.
 * Even with larger folio support, we will only allocate a folio as large as
 * node size.
 * Thus if nodesize < PAGE_SIZE, we know metadata needs need to subpage routine.
 */
static inline bool btrfs_meta_is_subpage(const struct btrfs_fs_info *fs_info)
{
	return fs_info->nodesize < PAGE_SIZE;
}
static inline bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info,
				    struct folio *folio)
{
	if (folio->mapping && folio->mapping->host)
		ASSERT(is_data_inode(BTRFS_I(folio->mapping->host)));
	return fs_info->sectorsize < folio_size(folio);
}

int btrfs_attach_folio_state(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, enum btrfs_folio_type type);
void btrfs_detach_folio_state(const struct btrfs_fs_info *fs_info, struct folio *folio,
			      enum btrfs_folio_type type);

/* Allocate additional data where page represents more than one sector */
struct btrfs_folio_state *btrfs_alloc_folio_state(const struct btrfs_fs_info *fs_info,
						  size_t fsize, enum btrfs_folio_type type);
static inline void btrfs_free_folio_state(struct btrfs_folio_state *bfs)
{
	kfree(bfs);
}

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
 *
 * Both btrfs_folio_*() and btrfs_folio_clamp_*() are for data folios.
 *
 * For metadata, one should use btrfs_meta_folio_*() helpers instead, and there
 * is no clamp version for metadata helpers, as we either go subpage
 * (nodesize < PAGE_SIZE) or go regular folio helpers (nodesize >= PAGE_SIZE,
 * and our folio is never larger than nodesize).
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
		struct folio *folio, u64 start, u32 len);		\
void btrfs_meta_folio_set_##name(struct folio *folio, const struct extent_buffer *eb); \
void btrfs_meta_folio_clear_##name(struct folio *folio, const struct extent_buffer *eb); \
bool btrfs_meta_folio_test_##name(struct folio *folio, const struct extent_buffer *eb);

DECLARE_BTRFS_SUBPAGE_OPS(uptodate);
DECLARE_BTRFS_SUBPAGE_OPS(dirty);
DECLARE_BTRFS_SUBPAGE_OPS(writeback);
DECLARE_BTRFS_SUBPAGE_OPS(ordered);
DECLARE_BTRFS_SUBPAGE_OPS(checked);

/*
 * Helper for error cleanup, where a folio will have its dirty flag cleared,
 * with writeback started and finished.
 */
static inline void btrfs_folio_clamp_finish_io(struct btrfs_fs_info *fs_info,
					       struct folio *locked_folio,
					       u64 start, u32 len)
{
	btrfs_folio_clamp_clear_dirty(fs_info, locked_folio, start, len);
	btrfs_folio_clamp_set_writeback(fs_info, locked_folio, start, len);
	btrfs_folio_clamp_clear_writeback(fs_info, locked_folio, start, len);
}

bool btrfs_subpage_clear_and_test_dirty(const struct btrfs_fs_info *fs_info,
					struct folio *folio, u64 start, u32 len);

void btrfs_folio_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len);
bool btrfs_meta_folio_clear_and_test_dirty(struct folio *folio, const struct extent_buffer *eb);
void btrfs_get_subpage_dirty_bitmap(struct btrfs_fs_info *fs_info,
				    struct folio *folio,
				    unsigned long *ret_bitmap);
void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 start, u32 len);

#endif
