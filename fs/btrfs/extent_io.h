#ifndef __EXTENTIO__
#define __EXTENTIO__

#include <linux/rbtree.h>

/* bits for the extent state */
#define EXTENT_DIRTY 1
#define EXTENT_WRITEBACK (1 << 1)
#define EXTENT_UPTODATE (1 << 2)
#define EXTENT_LOCKED (1 << 3)
#define EXTENT_NEW (1 << 4)
#define EXTENT_DELALLOC (1 << 5)
#define EXTENT_DEFRAG (1 << 6)
#define EXTENT_DEFRAG_DONE (1 << 7)
#define EXTENT_BUFFER_FILLED (1 << 8)
#define EXTENT_BOUNDARY (1 << 9)
#define EXTENT_NODATASUM (1 << 10)
#define EXTENT_DO_ACCOUNTING (1 << 11)
#define EXTENT_FIRST_DELALLOC (1 << 12)
#define EXTENT_NEED_WAIT (1 << 13)
#define EXTENT_DAMAGED (1 << 14)
#define EXTENT_NORESERVE (1 << 15)
#define EXTENT_IOBITS (EXTENT_LOCKED | EXTENT_WRITEBACK)
#define EXTENT_CTLBITS (EXTENT_DO_ACCOUNTING | EXTENT_FIRST_DELALLOC)

/*
 * flags for bio submission. The high bits indicate the compression
 * type for this bio
 */
#define EXTENT_BIO_COMPRESSED 1
#define EXTENT_BIO_TREE_LOG 2
#define EXTENT_BIO_PARENT_LOCKED 4
#define EXTENT_BIO_FLAG_SHIFT 16

/* these are bit numbers for test/set bit */
#define EXTENT_BUFFER_UPTODATE 0
#define EXTENT_BUFFER_BLOCKING 1
#define EXTENT_BUFFER_DIRTY 2
#define EXTENT_BUFFER_CORRUPT 3
#define EXTENT_BUFFER_READAHEAD 4	/* this got triggered by readahead */
#define EXTENT_BUFFER_TREE_REF 5
#define EXTENT_BUFFER_STALE 6
#define EXTENT_BUFFER_WRITEBACK 7
#define EXTENT_BUFFER_IOERR 8
#define EXTENT_BUFFER_DUMMY 9
#define EXTENT_BUFFER_IN_TREE 10

/* these are flags for extent_clear_unlock_delalloc */
#define PAGE_UNLOCK		(1 << 0)
#define PAGE_CLEAR_DIRTY	(1 << 1)
#define PAGE_SET_WRITEBACK	(1 << 2)
#define PAGE_END_WRITEBACK	(1 << 3)
#define PAGE_SET_PRIVATE2	(1 << 4)

/*
 * page->private values.  Every page that is controlled by the extent
 * map has page->private set to one.
 */
#define EXTENT_PAGE_PRIVATE 1
#define EXTENT_PAGE_PRIVATE_FIRST_PAGE 3

struct extent_state;
struct btrfs_root;
struct btrfs_io_bio;

typedef	int (extent_submit_bio_hook_t)(struct inode *inode, int rw,
				       struct bio *bio, int mirror_num,
				       unsigned long bio_flags, u64 bio_offset);
struct extent_io_ops {
	int (*fill_delalloc)(struct inode *inode, struct page *locked_page,
			     u64 start, u64 end, int *page_started,
			     unsigned long *nr_written);
	int (*writepage_start_hook)(struct page *page, u64 start, u64 end);
	int (*writepage_io_hook)(struct page *page, u64 start, u64 end);
	extent_submit_bio_hook_t *submit_bio_hook;
	int (*merge_bio_hook)(int rw, struct page *page, unsigned long offset,
			      size_t size, struct bio *bio,
			      unsigned long bio_flags);
	int (*readpage_io_failed_hook)(struct page *page, int failed_mirror);
	int (*readpage_end_io_hook)(struct btrfs_io_bio *io_bio, u64 phy_offset,
				    struct page *page, u64 start, u64 end,
				    int mirror);
	int (*writepage_end_io_hook)(struct page *page, u64 start, u64 end,
				      struct extent_state *state, int uptodate);
	void (*set_bit_hook)(struct inode *inode, struct extent_state *state,
			     unsigned long *bits);
	void (*clear_bit_hook)(struct inode *inode, struct extent_state *state,
			       unsigned long *bits);
	void (*merge_extent_hook)(struct inode *inode,
				  struct extent_state *new,
				  struct extent_state *other);
	void (*split_extent_hook)(struct inode *inode,
				  struct extent_state *orig, u64 split);
};

struct extent_io_tree {
	struct rb_root state;
	struct address_space *mapping;
	u64 dirty_bytes;
	int track_uptodate;
	spinlock_t lock;
	struct extent_io_ops *ops;
};

struct extent_state {
	u64 start;
	u64 end; /* inclusive */
	struct rb_node rb_node;

	/* ADD NEW ELEMENTS AFTER THIS */
	struct extent_io_tree *tree;
	wait_queue_head_t wq;
	atomic_t refs;
	unsigned long state;

	/* for use by the FS */
	u64 private;

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

#define INLINE_EXTENT_BUFFER_PAGES 16
#define MAX_INLINE_EXTENT_BUFFER_SIZE (INLINE_EXTENT_BUFFER_PAGES * PAGE_CACHE_SIZE)
struct extent_buffer {
	u64 start;
	unsigned long len;
	unsigned long map_start;
	unsigned long map_len;
	unsigned long bflags;
	struct btrfs_fs_info *fs_info;
	spinlock_t refs_lock;
	atomic_t refs;
	atomic_t io_pages;
	int read_mirror;
	struct rcu_head rcu_head;
	pid_t lock_owner;

	/* count of read lock holders on the extent buffer */
	atomic_t write_locks;
	atomic_t read_locks;
	atomic_t blocking_writers;
	atomic_t blocking_readers;
	atomic_t spinning_readers;
	atomic_t spinning_writers;
	int lock_nested;

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
	wait_queue_head_t lock_wq;
	struct page *pages[INLINE_EXTENT_BUFFER_PAGES];
#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

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

typedef struct extent_map *(get_extent_t)(struct inode *inode,
					  struct page *page,
					  size_t pg_offset,
					  u64 start, u64 len,
					  int create);

void extent_io_tree_init(struct extent_io_tree *tree,
			 struct address_space *mapping);
int try_release_extent_mapping(struct extent_map_tree *map,
			       struct extent_io_tree *tree, struct page *page,
			       gfp_t mask);
int try_release_extent_buffer(struct page *page);
int lock_extent(struct extent_io_tree *tree, u64 start, u64 end);
int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned long bits, struct extent_state **cached);
int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end);
int unlock_extent_cached(struct extent_io_tree *tree, u64 start, u64 end,
			 struct extent_state **cached, gfp_t mask);
int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end);
int extent_read_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent, int mirror_num);
int extent_read_full_page_nolock(struct extent_io_tree *tree, struct page *page,
				 get_extent_t *get_extent, int mirror_num);
int __init extent_io_init(void);
void extent_io_exit(void);

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end,
		     u64 max_bytes, unsigned long bits, int contig);

void free_extent_state(struct extent_state *state);
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned long bits, int filled,
		   struct extent_state *cached_state);
int clear_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		      unsigned long bits, gfp_t mask);
int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned long bits, int wake, int delete,
		     struct extent_state **cached, gfp_t mask);
int set_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		    unsigned long bits, gfp_t mask);
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned long bits, u64 *failed_start,
		   struct extent_state **cached_state, gfp_t mask);
int set_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask);
int clear_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			  struct extent_state **cached_state, gfp_t mask);
int set_extent_new(struct extent_io_tree *tree, u64 start, u64 end,
		   gfp_t mask);
int set_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask);
int clear_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask);
int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       unsigned long bits, unsigned long clear_bits,
		       struct extent_state **cached_state, gfp_t mask);
int set_extent_delalloc(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask);
int set_extent_defrag(struct extent_io_tree *tree, u64 start, u64 end,
		      struct extent_state **cached_state, gfp_t mask);
int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, unsigned long bits,
			  struct extent_state **cached_state);
int extent_invalidatepage(struct extent_io_tree *tree,
			  struct page *page, unsigned long offset);
int extent_write_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent,
			  struct writeback_control *wbc);
int extent_write_locked_range(struct extent_io_tree *tree, struct inode *inode,
			      u64 start, u64 end, get_extent_t *get_extent,
			      int mode);
int extent_writepages(struct extent_io_tree *tree,
		      struct address_space *mapping,
		      get_extent_t *get_extent,
		      struct writeback_control *wbc);
int btree_write_cache_pages(struct address_space *mapping,
			    struct writeback_control *wbc);
int extent_readpages(struct extent_io_tree *tree,
		     struct address_space *mapping,
		     struct list_head *pages, unsigned nr_pages,
		     get_extent_t get_extent);
int extent_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len, get_extent_t *get_extent);
int get_state_private(struct extent_io_tree *tree, u64 start, u64 *private);
void set_page_extent_mapped(struct page *page);

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start, unsigned long len);
struct extent_buffer *alloc_dummy_extent_buffer(u64 start, unsigned long len);
struct extent_buffer *btrfs_clone_extent_buffer(struct extent_buffer *src);
struct extent_buffer *find_extent_buffer(struct btrfs_fs_info *fs_info,
					 u64 start);
void free_extent_buffer(struct extent_buffer *eb);
void free_extent_buffer_stale(struct extent_buffer *eb);
#define WAIT_NONE	0
#define WAIT_COMPLETE	1
#define WAIT_PAGE_LOCK	2
int read_extent_buffer_pages(struct extent_io_tree *tree,
			     struct extent_buffer *eb, u64 start, int wait,
			     get_extent_t *get_extent, int mirror_num);
void wait_on_extent_buffer_writeback(struct extent_buffer *eb);

static inline unsigned long num_extent_pages(u64 start, u64 len)
{
	return ((start + len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT) -
		(start >> PAGE_CACHE_SHIFT);
}

static inline struct page *extent_buffer_page(struct extent_buffer *eb,
					      unsigned long i)
{
	return eb->pages[i];
}

static inline void extent_buffer_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->refs);
}

int memcmp_extent_buffer(struct extent_buffer *eb, const void *ptrv,
			  unsigned long start,
			  unsigned long len);
void read_extent_buffer(struct extent_buffer *eb, void *dst,
			unsigned long start,
			unsigned long len);
void write_extent_buffer(struct extent_buffer *eb, const void *src,
			 unsigned long start, unsigned long len);
void copy_extent_buffer(struct extent_buffer *dst, struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len);
void memcpy_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len);
void memmove_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len);
void memset_extent_buffer(struct extent_buffer *eb, char c,
			  unsigned long start, unsigned long len);
void clear_extent_buffer_dirty(struct extent_buffer *eb);
int set_extent_buffer_dirty(struct extent_buffer *eb);
int set_extent_buffer_uptodate(struct extent_buffer *eb);
int clear_extent_buffer_uptodate(struct extent_buffer *eb);
int extent_buffer_uptodate(struct extent_buffer *eb);
int map_private_extent_buffer(struct extent_buffer *eb, unsigned long offset,
		      unsigned long min_len, char **map,
		      unsigned long *map_start,
		      unsigned long *map_len);
int extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end);
int extent_range_redirty_for_io(struct inode *inode, u64 start, u64 end);
int extent_clear_unlock_delalloc(struct inode *inode, u64 start, u64 end,
				 struct page *locked_page,
				 unsigned long bits_to_clear,
				 unsigned long page_ops);
struct bio *
btrfs_bio_alloc(struct block_device *bdev, u64 first_sector, int nr_vecs,
		gfp_t gfp_flags);
struct bio *btrfs_io_bio_alloc(gfp_t gfp_mask, unsigned int nr_iovecs);
struct bio *btrfs_bio_clone(struct bio *bio, gfp_t gfp_mask);

struct btrfs_fs_info;

int repair_io_failure(struct btrfs_fs_info *fs_info, u64 start,
			u64 length, u64 logical, struct page *page,
			int mirror_num);
int end_extent_writepage(struct page *page, int err, u64 start, u64 end);
int repair_eb_io_failure(struct btrfs_root *root, struct extent_buffer *eb,
			 int mirror_num);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
noinline u64 find_lock_delalloc_range(struct inode *inode,
				      struct extent_io_tree *tree,
				      struct page *locked_page, u64 *start,
				      u64 *end, u64 max_bytes);
#endif
#endif
