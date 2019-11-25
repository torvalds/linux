/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_IO_H
#define BTRFS_EXTENT_IO_H

#include <linux/rbtree.h>
#include <linux/refcount.h>
#include "ulist.h"

/* bits for the extent state */
#define EXTENT_DIRTY		(1U << 0)
#define EXTENT_UPTODATE		(1U << 1)
#define EXTENT_LOCKED		(1U << 2)
#define EXTENT_NEW		(1U << 3)
#define EXTENT_DELALLOC		(1U << 4)
#define EXTENT_DEFRAG		(1U << 5)
#define EXTENT_BOUNDARY		(1U << 6)
#define EXTENT_NODATASUM	(1U << 7)
#define EXTENT_CLEAR_META_RESV	(1U << 8)
#define EXTENT_NEED_WAIT	(1U << 9)
#define EXTENT_DAMAGED		(1U << 10)
#define EXTENT_NORESERVE	(1U << 11)
#define EXTENT_QGROUP_RESERVED	(1U << 12)
#define EXTENT_CLEAR_DATA_RESV	(1U << 13)
#define EXTENT_DELALLOC_NEW	(1U << 14)
#define EXTENT_DO_ACCOUNTING    (EXTENT_CLEAR_META_RESV | \
				 EXTENT_CLEAR_DATA_RESV)
#define EXTENT_CTLBITS		(EXTENT_DO_ACCOUNTING)

/*
 * Redefined bits above which are used only in the device allocation tree,
 * shouldn't be using EXTENT_LOCKED / EXTENT_BOUNDARY / EXTENT_CLEAR_META_RESV
 * / EXTENT_CLEAR_DATA_RESV because they have special meaning to the bit
 * manipulation functions
 */
#define CHUNK_ALLOCATED EXTENT_DIRTY
#define CHUNK_TRIMMED   EXTENT_DEFRAG

/*
 * flags for bio submission. The high bits indicate the compression
 * type for this bio
 */
#define EXTENT_BIO_COMPRESSED 1
#define EXTENT_BIO_FLAG_SHIFT 16

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
};

/* these are flags for __process_pages_contig */
#define PAGE_UNLOCK		(1 << 0)
#define PAGE_CLEAR_DIRTY	(1 << 1)
#define PAGE_SET_WRITEBACK	(1 << 2)
#define PAGE_END_WRITEBACK	(1 << 3)
#define PAGE_SET_PRIVATE2	(1 << 4)
#define PAGE_SET_ERROR		(1 << 5)
#define PAGE_LOCK		(1 << 6)

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

struct extent_state;
struct btrfs_root;
struct btrfs_inode;
struct btrfs_io_bio;
struct io_failure_record;


typedef blk_status_t (extent_submit_bio_start_t)(void *private_data,
		struct bio *bio, u64 bio_offset);

struct extent_io_ops {
	/*
	 * The following callbacks must be always defined, the function
	 * pointer will be called unconditionally.
	 */
	blk_status_t (*submit_bio_hook)(struct inode *inode, struct bio *bio,
					int mirror_num, unsigned long bio_flags);
	int (*readpage_end_io_hook)(struct btrfs_io_bio *io_bio, u64 phy_offset,
				    struct page *page, u64 start, u64 end,
				    int mirror);
};

enum {
	IO_TREE_FS_INFO_FREED_EXTENTS0,
	IO_TREE_FS_INFO_FREED_EXTENTS1,
	IO_TREE_INODE_IO,
	IO_TREE_INODE_IO_FAILURE,
	IO_TREE_RELOC_BLOCKS,
	IO_TREE_TRANS_DIRTY_PAGES,
	IO_TREE_ROOT_DIRTY_LOG_PAGES,
	IO_TREE_SELFTEST,
};

struct extent_io_tree {
	struct rb_root state;
	struct btrfs_fs_info *fs_info;
	void *private_data;
	u64 dirty_bytes;
	bool track_uptodate;

	/* Who owns this io tree, should be one of IO_TREE_* */
	u8 owner;

	spinlock_t lock;
	const struct extent_io_ops *ops;
};

struct extent_state {
	u64 start;
	u64 end; /* inclusive */
	struct rb_node rb_node;

	/* ADD NEW ELEMENTS AFTER THIS */
	wait_queue_head_t wq;
	refcount_t refs;
	unsigned state;

	struct io_failure_record *failrec;

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

#define INLINE_EXTENT_BUFFER_PAGES 16
#define MAX_INLINE_EXTENT_BUFFER_SIZE (INLINE_EXTENT_BUFFER_PAGES * PAGE_SIZE)
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

	int blocking_writers;
	atomic_t blocking_readers;
	bool lock_nested;
	/* >= 0 if eb belongs to a log tree, -1 otherwise */
	short log_index;

	/* protects write locks */
	rwlock_t lock;

	/* readers use lock_wq while they wait for the write
	 * lock holders to unlock
	 */
	wait_queue_head_t write_lock_wq;

	/* writers use read_lock_wq while they wait for readers
	 * to unlock
	 */
	wait_queue_head_t read_lock_wq;
	struct page *pages[INLINE_EXTENT_BUFFER_PAGES];
#ifdef CONFIG_BTRFS_DEBUG
	int spinning_writers;
	atomic_t spinning_readers;
	atomic_t read_locks;
	int write_locks;
	struct list_head leak_list;
#endif
};

/*
 * Structure to record how many bytes and which ranges are set/cleared
 */
struct extent_changeset {
	/* How many bytes are set/cleared in this operation */
	unsigned int bytes_changed;

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

static inline void extent_set_compress_type(unsigned long *bio_flags,
					    int compress_type)
{
	*bio_flags |= compress_type << EXTENT_BIO_FLAG_SHIFT;
}

static inline int extent_compress_type(unsigned long bio_flags)
{
	return bio_flags >> EXTENT_BIO_FLAG_SHIFT;
}

struct extent_map_tree;

typedef struct extent_map *(get_extent_t)(struct btrfs_inode *inode,
					  struct page *page,
					  size_t pg_offset,
					  u64 start, u64 len,
					  int create);

void extent_io_tree_init(struct btrfs_fs_info *fs_info,
			 struct extent_io_tree *tree, unsigned int owner,
			 void *private_data);
void extent_io_tree_release(struct extent_io_tree *tree);
int try_release_extent_mapping(struct page *page, gfp_t mask);
int try_release_extent_buffer(struct page *page);
int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     struct extent_state **cached);

static inline int lock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return lock_extent_bits(tree, start, end, NULL);
}

int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end);
int extent_read_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent, int mirror_num);
int __init extent_io_init(void);
void __cold extent_io_exit(void);

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end,
		     u64 max_bytes, unsigned bits, int contig);

void free_extent_state(struct extent_state *state);
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, int filled,
		   struct extent_state *cached_state);
int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		unsigned bits, struct extent_changeset *changeset);
int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, int wake, int delete,
		     struct extent_state **cached);
int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, int wake, int delete,
		     struct extent_state **cached, gfp_t mask,
		     struct extent_changeset *changeset);

static inline int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, NULL);
}

static inline int unlock_extent_cached(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, cached,
				GFP_NOFS, NULL);
}

static inline int unlock_extent_cached_atomic(struct extent_io_tree *tree,
		u64 start, u64 end, struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, cached,
				GFP_ATOMIC, NULL);
}

static inline int clear_extent_bits(struct extent_io_tree *tree, u64 start,
		u64 end, unsigned bits)
{
	int wake = 0;

	if (bits & EXTENT_LOCKED)
		wake = 1;

	return clear_extent_bit(tree, start, end, bits, wake, 0, NULL);
}

int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   unsigned bits, struct extent_changeset *changeset);
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, u64 *failed_start,
		   struct extent_state **cached_state, gfp_t mask);
int set_extent_bits_nowait(struct extent_io_tree *tree, u64 start, u64 end,
			   unsigned bits);

static inline int set_extent_bits(struct extent_io_tree *tree, u64 start,
		u64 end, unsigned bits)
{
	return set_extent_bit(tree, start, end, bits, NULL, NULL, GFP_NOFS);
}

static inline int clear_extent_uptodate(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state)
{
	return __clear_extent_bit(tree, start, end, EXTENT_UPTODATE, 0, 0,
				cached_state, GFP_NOFS, NULL);
}

static inline int set_extent_dirty(struct extent_io_tree *tree, u64 start,
		u64 end, gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_DIRTY, NULL,
			      NULL, mask);
}

static inline int clear_extent_dirty(struct extent_io_tree *tree, u64 start,
				     u64 end, struct extent_state **cached)
{
	return clear_extent_bit(tree, start, end,
				EXTENT_DIRTY | EXTENT_DELALLOC |
				EXTENT_DO_ACCOUNTING, 0, 0, cached);
}

int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       unsigned bits, unsigned clear_bits,
		       struct extent_state **cached_state);

static inline int set_extent_delalloc(struct extent_io_tree *tree, u64 start,
				      u64 end, unsigned int extra_bits,
				      struct extent_state **cached_state)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE | extra_bits,
			      NULL, cached_state, GFP_NOFS);
}

static inline int set_extent_defrag(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE | EXTENT_DEFRAG,
			      NULL, cached_state, GFP_NOFS);
}

static inline int set_extent_new(struct extent_io_tree *tree, u64 start,
		u64 end)
{
	return set_extent_bit(tree, start, end, EXTENT_NEW, NULL, NULL,
			GFP_NOFS);
}

static inline int set_extent_uptodate(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state, gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_UPTODATE, NULL,
			      cached_state, mask);
}

int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, unsigned bits,
			  struct extent_state **cached_state);
void find_first_clear_extent_bit(struct extent_io_tree *tree, u64 start,
				 u64 *start_ret, u64 *end_ret, unsigned bits);
int extent_invalidatepage(struct extent_io_tree *tree,
			  struct page *page, unsigned long offset);
int extent_write_full_page(struct page *page, struct writeback_control *wbc);
int extent_write_locked_range(struct inode *inode, u64 start, u64 end,
			      int mode);
int extent_writepages(struct address_space *mapping,
		      struct writeback_control *wbc);
int btree_write_cache_pages(struct address_space *mapping,
			    struct writeback_control *wbc);
int extent_readpages(struct address_space *mapping, struct list_head *pages,
		     unsigned nr_pages);
int extent_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len);
void set_page_extent_mapped(struct page *page);

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start);
struct extent_buffer *__alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						  u64 start, unsigned long len);
struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start);
struct extent_buffer *btrfs_clone_extent_buffer(struct extent_buffer *src);
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

static inline int num_extent_pages(const struct extent_buffer *eb)
{
	return (round_up(eb->start + eb->len, PAGE_SIZE) >> PAGE_SHIFT) -
	       (eb->start >> PAGE_SHIFT);
}

static inline void extent_buffer_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->refs);
}

static inline int extent_buffer_uptodate(struct extent_buffer *eb)
{
	return test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
}

int memcmp_extent_buffer(const struct extent_buffer *eb, const void *ptrv,
			 unsigned long start, unsigned long len);
void read_extent_buffer(const struct extent_buffer *eb, void *dst,
			unsigned long start,
			unsigned long len);
int read_extent_buffer_to_user(const struct extent_buffer *eb,
			       void __user *dst, unsigned long start,
			       unsigned long len);
void write_extent_buffer_fsid(struct extent_buffer *eb, const void *src);
void write_extent_buffer_chunk_tree_uuid(struct extent_buffer *eb,
		const void *src);
void write_extent_buffer(struct extent_buffer *eb, const void *src,
			 unsigned long start, unsigned long len);
void copy_extent_buffer_full(struct extent_buffer *dst,
			     struct extent_buffer *src);
void copy_extent_buffer(struct extent_buffer *dst, struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len);
void memcpy_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len);
void memmove_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len);
void memzero_extent_buffer(struct extent_buffer *eb, unsigned long start,
			   unsigned long len);
int extent_buffer_test_bit(struct extent_buffer *eb, unsigned long start,
			   unsigned long pos);
void extent_buffer_bitmap_set(struct extent_buffer *eb, unsigned long start,
			      unsigned long pos, unsigned long len);
void extent_buffer_bitmap_clear(struct extent_buffer *eb, unsigned long start,
				unsigned long pos, unsigned long len);
void clear_extent_buffer_dirty(struct extent_buffer *eb);
bool set_extent_buffer_dirty(struct extent_buffer *eb);
void set_extent_buffer_uptodate(struct extent_buffer *eb);
void clear_extent_buffer_uptodate(struct extent_buffer *eb);
int extent_buffer_under_io(struct extent_buffer *eb);
int map_private_extent_buffer(const struct extent_buffer *eb,
			      unsigned long offset, unsigned long min_len,
			      char **map, unsigned long *map_start,
			      unsigned long *map_len);
void extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end);
void extent_range_redirty_for_io(struct inode *inode, u64 start, u64 end);
void extent_clear_unlock_delalloc(struct inode *inode, u64 start, u64 end,
				  struct page *locked_page,
				  unsigned bits_to_clear,
				  unsigned long page_ops);
struct bio *btrfs_bio_alloc(u64 first_byte);
struct bio *btrfs_io_bio_alloc(unsigned int nr_iovecs);
struct bio *btrfs_bio_clone(struct bio *bio);
struct bio *btrfs_bio_clone_partial(struct bio *orig, int offset, int size);

struct btrfs_fs_info;
struct btrfs_inode;

int repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
		      u64 length, u64 logical, struct page *page,
		      unsigned int pg_offset, int mirror_num);
int clean_io_failure(struct btrfs_fs_info *fs_info,
		     struct extent_io_tree *failure_tree,
		     struct extent_io_tree *io_tree, u64 start,
		     struct page *page, u64 ino, unsigned int pg_offset);
void end_extent_writepage(struct page *page, int err, u64 start, u64 end);
int btrfs_repair_eb_io_failure(struct extent_buffer *eb, int mirror_num);

/*
 * When IO fails, either with EIO or csum verification fails, we
 * try other mirrors that might have a good copy of the data.  This
 * io_failure_record is used to record state as we go through all the
 * mirrors.  If another mirror has good data, the page is set up to date
 * and things continue.  If a good mirror can't be found, the original
 * bio end_io callback is called to indicate things have failed.
 */
struct io_failure_record {
	struct page *page;
	u64 start;
	u64 len;
	u64 logical;
	unsigned long bio_flags;
	int this_mirror;
	int failed_mirror;
	int in_validation;
};


void btrfs_free_io_failure_record(struct btrfs_inode *inode, u64 start,
		u64 end);
int btrfs_get_io_failure_record(struct inode *inode, u64 start, u64 end,
				struct io_failure_record **failrec_ret);
bool btrfs_check_repairable(struct inode *inode, unsigned failed_bio_pages,
			    struct io_failure_record *failrec, int fail_mirror);
struct bio *btrfs_create_repair_bio(struct inode *inode, struct bio *failed_bio,
				    struct io_failure_record *failrec,
				    struct page *page, int pg_offset, int icsum,
				    bio_end_io_t *endio_func, void *data);
int free_io_failure(struct extent_io_tree *failure_tree,
		    struct extent_io_tree *io_tree,
		    struct io_failure_record *rec);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
bool find_lock_delalloc_range(struct inode *inode,
			     struct page *locked_page, u64 *start,
			     u64 *end);
#endif
struct extent_buffer *alloc_test_extent_buffer(struct btrfs_fs_info *fs_info,
					       u64 start);

#endif
