/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_IO_H
#define BTRFS_EXTENT_IO_H

#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/fiemap.h>
#include <linux/btrfs_tree.h>
#include "compression.h"
#include "ulist.h"

enum {
	EXTENT_BUFFER_UPTODATE,
	EXTENT_BUFFER_DIRTY,
	EXTENT_BUFFER_CORRUPT,
	/* this got triggered by readahead */
	EXTENT_BUFFER_READAHEAD,
	EXTENT_BUFFER_TREE_REF,
	EXTENT_BUFFER_STALE,
	EXTENT_BUFFER_WRITEBACK,
	/* read IO error */
	EXTENT_BUFFER_READ_ERR,
	EXTENT_BUFFER_UNMAPPED,
	EXTENT_BUFFER_IN_TREE,
	/* write IO error */
	EXTENT_BUFFER_WRITE_ERR,
	EXTENT_BUFFER_NO_CHECK,
};

/* these are flags for __process_pages_contig */
#define PAGE_UNLOCK		(1 << 0)
/* Page starts writeback, clear dirty bit and set writeback bit */
#define PAGE_START_WRITEBACK	(1 << 1)
#define PAGE_END_WRITEBACK	(1 << 2)
#define PAGE_SET_ORDERED	(1 << 3)
#define PAGE_SET_ERROR		(1 << 4)
#define PAGE_LOCK		(1 << 5)

/*
 * page->private values.  Every page that is controlled by the extent
 * map has page->private set to one.
 */
#define EXTENT_PAGE_PRIVATE 1

/*
 * The extent buffer bitmap operations are done with byte granularity instead of
 * word granularity for two reasons:
 * 1. The bitmaps must be little-endian on disk.
 * 2. Bitmap items are not guaranteed to be aligned to a word and therefore a
 *    single word in a bitmap may straddle two pages in the extent buffer.
 */
#define BIT_BYTE(nr) ((nr) / BITS_PER_BYTE)
#define BYTE_MASK ((1 << BITS_PER_BYTE) - 1)
#define BITMAP_FIRST_BYTE_MASK(start) \
	((BYTE_MASK << ((start) & (BITS_PER_BYTE - 1))) & BYTE_MASK)
#define BITMAP_LAST_BYTE_MASK(nbits) \
	(BYTE_MASK >> (-(nbits) & (BITS_PER_BYTE - 1)))

struct btrfs_bio;
struct btrfs_root;
struct btrfs_inode;
struct btrfs_fs_info;
struct io_failure_record;
struct extent_io_tree;

int __init extent_buffer_init_cachep(void);
void __cold extent_buffer_free_cachep(void);

typedef void (submit_bio_hook_t)(struct inode *inode, struct bio *bio,
					 int mirror_num,
					 enum btrfs_compression_type compress_type);

typedef blk_status_t (extent_submit_bio_start_t)(struct inode *inode,
		struct bio *bio, u64 dio_file_offset);

#define INLINE_EXTENT_BUFFER_PAGES     (BTRFS_MAX_METADATA_BLOCKSIZE / PAGE_SIZE)
struct extent_buffer {
	u64 start;
	unsigned long len;
	unsigned long bflags;
	struct btrfs_fs_info *fs_info;
	spinlock_t refs_lock;
	atomic_t refs;
	atomic_t io_pages;
	int read_mirror;
	struct rcu_head rcu_head;
	pid_t lock_owner;
	/* >= 0 if eb belongs to a log tree, -1 otherwise */
	s8 log_index;

	struct rw_semaphore lock;

	struct page *pages[INLINE_EXTENT_BUFFER_PAGES];
	struct list_head release_list;
#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

/*
 * Structure to record how many bytes and which ranges are set/cleared
 */
struct extent_changeset {
	/* How many bytes are set/cleared in this operation */
	u64 bytes_changed;

	/* Changed ranges */
	struct ulist range_changed;
};

static inline void extent_changeset_init(struct extent_changeset *changeset)
{
	changeset->bytes_changed = 0;
	ulist_init(&changeset->range_changed);
}

static inline struct extent_changeset *extent_changeset_alloc(void)
{
	struct extent_changeset *ret;

	ret = kmalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return NULL;

	extent_changeset_init(ret);
	return ret;
}

static inline void extent_changeset_release(struct extent_changeset *changeset)
{
	if (!changeset)
		return;
	changeset->bytes_changed = 0;
	ulist_release(&changeset->range_changed);
}

static inline void extent_changeset_free(struct extent_changeset *changeset)
{
	if (!changeset)
		return;
	extent_changeset_release(changeset);
	kfree(changeset);
}

struct extent_map_tree;

int try_release_extent_mapping(struct page *page, gfp_t mask);
int try_release_extent_buffer(struct page *page);

int btrfs_read_folio(struct file *file, struct folio *folio);
int extent_write_locked_range(struct inode *inode, u64 start, u64 end);
int extent_writepages(struct address_space *mapping,
		      struct writeback_control *wbc);
int btree_write_cache_pages(struct address_space *mapping,
			    struct writeback_control *wbc);
void extent_readahead(struct readahead_control *rac);
int extent_fiemap(struct btrfs_inode *inode, struct fiemap_extent_info *fieinfo,
		  u64 start, u64 len);
int set_page_extent_mapped(struct page *page);
void clear_page_extent_mapped(struct page *page);

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start, u64 owner_root, int level);
struct extent_buffer *__alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						  u64 start, unsigned long len);
struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start);
struct extent_buffer *btrfs_clone_extent_buffer(const struct extent_buffer *src);
struct extent_buffer *find_extent_buffer(struct btrfs_fs_info *fs_info,
					 u64 start);
void free_extent_buffer(struct extent_buffer *eb);
void free_extent_buffer_stale(struct extent_buffer *eb);
#define WAIT_NONE	0
#define WAIT_COMPLETE	1
#define WAIT_PAGE_LOCK	2
int read_extent_buffer_pages(struct extent_buffer *eb, int wait,
			     int mirror_num);
void wait_on_extent_buffer_writeback(struct extent_buffer *eb);
void btrfs_readahead_tree_block(struct btrfs_fs_info *fs_info,
				u64 bytenr, u64 owner_root, u64 gen, int level);
void btrfs_readahead_node_child(struct extent_buffer *node, int slot);

static inline int num_extent_pages(const struct extent_buffer *eb)
{
	/*
	 * For sectorsize == PAGE_SIZE case, since nodesize is always aligned to
	 * sectorsize, it's just eb->len >> PAGE_SHIFT.
	 *
	 * For sectorsize < PAGE_SIZE case, we could have nodesize < PAGE_SIZE,
	 * thus have to ensure we get at least one page.
	 */
	return (eb->len >> PAGE_SHIFT) ?: 1;
}

static inline int extent_buffer_uptodate(const struct extent_buffer *eb)
{
	return test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
}

int memcmp_extent_buffer(const struct extent_buffer *eb, const void *ptrv,
			 unsigned long start, unsigned long len);
void read_extent_buffer(const struct extent_buffer *eb, void *dst,
			unsigned long start,
			unsigned long len);
int read_extent_buffer_to_user_nofault(const struct extent_buffer *eb,
				       void __user *dst, unsigned long start,
				       unsigned long len);
void write_extent_buffer_fsid(const struct extent_buffer *eb, const void *src);
void write_extent_buffer_chunk_tree_uuid(const struct extent_buffer *eb,
		const void *src);
void write_extent_buffer(const struct extent_buffer *eb, const void *src,
			 unsigned long start, unsigned long len);
void copy_extent_buffer_full(const struct extent_buffer *dst,
			     const struct extent_buffer *src);
void copy_extent_buffer(const struct extent_buffer *dst,
			const struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len);
void memcpy_extent_buffer(const struct extent_buffer *dst,
			  unsigned long dst_offset, unsigned long src_offset,
			  unsigned long len);
void memmove_extent_buffer(const struct extent_buffer *dst,
			   unsigned long dst_offset, unsigned long src_offset,
			   unsigned long len);
void memzero_extent_buffer(const struct extent_buffer *eb, unsigned long start,
			   unsigned long len);
int extent_buffer_test_bit(const struct extent_buffer *eb, unsigned long start,
			   unsigned long pos);
void extent_buffer_bitmap_set(const struct extent_buffer *eb, unsigned long start,
			      unsigned long pos, unsigned long len);
void extent_buffer_bitmap_clear(const struct extent_buffer *eb,
				unsigned long start, unsigned long pos,
				unsigned long len);
void clear_extent_buffer_dirty(const struct extent_buffer *eb);
bool set_extent_buffer_dirty(struct extent_buffer *eb);
void set_extent_buffer_uptodate(struct extent_buffer *eb);
void clear_extent_buffer_uptodate(struct extent_buffer *eb);
int extent_buffer_under_io(const struct extent_buffer *eb);
void extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end);
void extent_range_redirty_for_io(struct inode *inode, u64 start, u64 end);
void extent_clear_unlock_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
				  struct page *locked_page,
				  u32 bits_to_clear, unsigned long page_ops);
int extent_invalidate_folio(struct extent_io_tree *tree,
			    struct folio *folio, size_t offset);

int btrfs_alloc_page_array(unsigned int nr_pages, struct page **page_array);

void end_extent_writepage(struct page *page, int err, u64 start, u64 end);
int btrfs_repair_eb_io_failure(const struct extent_buffer *eb, int mirror_num);

/*
 * When IO fails, either with EIO or csum verification fails, we
 * try other mirrors that might have a good copy of the data.  This
 * io_failure_record is used to record state as we go through all the
 * mirrors.  If another mirror has good data, the sector is set up to date
 * and things continue.  If a good mirror can't be found, the original
 * bio end_io callback is called to indicate things have failed.
 */
struct io_failure_record {
	/* Use rb_simple_node for search/insert */
	struct {
		struct rb_node rb_node;
		u64 bytenr;
	};
	struct page *page;
	u64 len;
	u64 logical;
	int this_mirror;
	int failed_mirror;
	int num_copies;
};

int btrfs_repair_one_sector(struct inode *inode, struct btrfs_bio *failed_bbio,
			    u32 bio_offset, struct page *page, unsigned int pgoff,
			    submit_bio_hook_t *submit_bio_hook);
void btrfs_free_io_failure_record(struct btrfs_inode *inode, u64 start, u64 end);
int btrfs_clean_io_failure(struct btrfs_inode *inode, u64 start,
			   struct page *page, unsigned int pg_offset);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
bool find_lock_delalloc_range(struct inode *inode,
			     struct page *locked_page, u64 *start,
			     u64 *end);
#endif
struct extent_buffer *alloc_test_extent_buffer(struct btrfs_fs_info *fs_info,
					       u64 start);

#ifdef CONFIG_BTRFS_DEBUG
void btrfs_extent_buffer_leak_debug_check(struct btrfs_fs_info *fs_info);
#else
#define btrfs_extent_buffer_leak_debug_check(fs_info)	do {} while (0)
#endif

#endif
