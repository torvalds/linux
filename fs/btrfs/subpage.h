/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUBPAGE_H
#define BTRFS_SUBPAGE_H

#include <linux/spinlock.h>

/*
 * Maximum page size we support is 64K, minimum sector size is 4K, u16 bitmap
 * is sufficient. Regular bitmap_* is not used due to size reasons.
 */
#define BTRFS_SUBPAGE_BITMAP_SIZE	16

/*
 * Structure to trace status of each sector inside a page, attached to
 * page::private for both data and metadata inodes.
 */
struct btrfs_subpage {
	/* Common members for both data and metadata pages */
	spinlock_t lock;
	u16 uptodate_bitmap;
	u16 error_bitmap;
	u16 dirty_bitmap;
	u16 writeback_bitmap;
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
		struct {
			atomic_t writers;

			/* Tracke pending ordered extent in this sector */
			u16 ordered_bitmap;
		};
	};
};

enum btrfs_subpage_type {
	BTRFS_SUBPAGE_METADATA,
	BTRFS_SUBPAGE_DATA,
};

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct page *page, enum btrfs_subpage_type type);
void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info,
			  struct page *page);

/* Allocate additional data where page represents more than one sector */
int btrfs_alloc_subpage(const struct btrfs_fs_info *fs_info,
			struct btrfs_subpage **ret,
			enum btrfs_subpage_type type);
void btrfs_free_subpage(struct btrfs_subpage *subpage);

void btrfs_page_inc_eb_refs(const struct btrfs_fs_info *fs_info,
			    struct page *page);
void btrfs_page_dec_eb_refs(const struct btrfs_fs_info *fs_info,
			    struct page *page);

void btrfs_subpage_start_reader(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);
void btrfs_subpage_end_reader(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);

void btrfs_subpage_start_writer(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);
bool btrfs_subpage_end_and_test_writer(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);
int btrfs_page_start_writer_lock(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);
void btrfs_page_end_writer_lock(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);

/*
 * Template for subpage related operations.
 *
 * btrfs_subpage_*() are for call sites where the page has subpage attached and
 * the range is ensured to be inside the page.
 *
 * btrfs_page_*() are for call sites where the page can either be subpage
 * specific or regular page. The function will handle both cases.
 * But the range still needs to be inside the page.
 *
 * btrfs_page_clamp_*() are similar to btrfs_page_*(), except the range doesn't
 * need to be inside the page. Those functions will truncate the range
 * automatically.
 */
#define DECLARE_BTRFS_SUBPAGE_OPS(name)					\
void btrfs_subpage_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
void btrfs_subpage_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
bool btrfs_subpage_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
void btrfs_page_set_##name(const struct btrfs_fs_info *fs_info,		\
		struct page *page, u64 start, u32 len);			\
void btrfs_page_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
bool btrfs_page_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
void btrfs_page_clamp_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
void btrfs_page_clamp_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);			\
bool btrfs_page_clamp_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len);

DECLARE_BTRFS_SUBPAGE_OPS(uptodate);
DECLARE_BTRFS_SUBPAGE_OPS(error);
DECLARE_BTRFS_SUBPAGE_OPS(dirty);
DECLARE_BTRFS_SUBPAGE_OPS(writeback);
DECLARE_BTRFS_SUBPAGE_OPS(ordered);

bool btrfs_subpage_clear_and_test_dirty(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len);

void btrfs_page_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				 struct page *page);

#endif
