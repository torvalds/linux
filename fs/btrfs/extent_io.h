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
#define EXTENT_IOBITS (EXTENT_LOCKED | EXTENT_WRITEBACK)
#define EXTENT_CTLBITS (EXTENT_DO_ACCOUNTING | EXTENT_FIRST_DELALLOC)

/*
 * flags for bio submission. The high bits indicate the compression
 * type for this bio
 */
#define EXTENT_BIO_COMPRESSED 1
#define EXTENT_BIO_FLAG_SHIFT 16

/* these are bit numbers for test/set bit */
#define EXTENT_BUFFER_UPTODATE 0
#define EXTENT_BUFFER_BLOCKING 1
#define EXTENT_BUFFER_DIRTY 2
#define EXTENT_BUFFER_CORRUPT 3

/* these are flags for extent_clear_unlock_delalloc */
#define EXTENT_CLEAR_UNLOCK_PAGE 0x1
#define EXTENT_CLEAR_UNLOCK	 0x2
#define EXTENT_CLEAR_DELALLOC	 0x4
#define EXTENT_CLEAR_DIRTY	 0x8
#define EXTENT_SET_WRITEBACK	 0x10
#define EXTENT_END_WRITEBACK	 0x20
#define EXTENT_SET_PRIVATE2	 0x40
#define EXTENT_CLEAR_ACCOUNTING  0x80

/*
 * page->private values.  Every page that is controlled by the extent
 * map has page->private set to one.
 */
#define EXTENT_PAGE_PRIVATE 1
#define EXTENT_PAGE_PRIVATE_FIRST_PAGE 3

struct extent_state;

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
	int (*merge_bio_hook)(struct page *page, unsigned long offset,
			      size_t size, struct bio *bio,
			      unsigned long bio_flags);
	int (*readpage_io_hook)(struct page *page, u64 start, u64 end);
	int (*readpage_io_failed_hook)(struct bio *bio, struct page *page,
				       u64 start, u64 end,
				       struct extent_state *state);
	int (*writepage_io_failed_hook)(struct bio *bio, struct page *page,
					u64 start, u64 end,
				       struct extent_state *state);
	int (*readpage_end_io_hook)(struct page *page, u64 start, u64 end,
				    struct extent_state *state);
	int (*writepage_end_io_hook)(struct page *page, u64 start, u64 end,
				      struct extent_state *state, int uptodate);
	int (*set_bit_hook)(struct inode *inode, struct extent_state *state,
			    int *bits);
	int (*clear_bit_hook)(struct inode *inode, struct extent_state *state,
			      int *bits);
	int (*merge_extent_hook)(struct inode *inode,
				 struct extent_state *new,
				 struct extent_state *other);
	int (*split_extent_hook)(struct inode *inode,
				 struct extent_state *orig, u64 split);
	int (*write_cache_pages_lock_hook)(struct page *page);
};

struct extent_io_tree {
	struct rb_root state;
	struct radix_tree_root buffer;
	struct address_space *mapping;
	u64 dirty_bytes;
	spinlock_t lock;
	spinlock_t buffer_lock;
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
	u64 split_start;
	u64 split_end;

	/* for use by the FS */
	u64 private;

	struct list_head leak_list;
};

struct extent_buffer {
	u64 start;
	unsigned long len;
	char *map_token;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	struct page *first_page;
	unsigned long bflags;
	atomic_t refs;
	struct list_head leak_list;
	struct rcu_head rcu_head;

	/* the spinlock is used to protect most operations */
	spinlock_t lock;

	/*
	 * when we keep the lock held while blocking, waiters go onto
	 * the wq
	 */
	wait_queue_head_t lock_wq;
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

static inline struct extent_state *extent_state_next(struct extent_state *state)
{
	struct rb_node *node;
	node = rb_next(&state->rb_node);
	if (!node)
		return NULL;
	return rb_entry(node, struct extent_state, rb_node);
}

typedef struct extent_map *(get_extent_t)(struct inode *inode,
					  struct page *page,
					  size_t page_offset,
					  u64 start, u64 len,
					  int create);

void extent_io_tree_init(struct extent_io_tree *tree,
			  struct address_space *mapping, gfp_t mask);
int try_release_extent_mapping(struct extent_map_tree *map,
			       struct extent_io_tree *tree, struct page *page,
			       gfp_t mask);
int try_release_extent_buffer(struct extent_io_tree *tree, struct page *page);
int try_release_extent_state(struct extent_map_tree *map,
			     struct extent_io_tree *tree, struct page *page,
			     gfp_t mask);
int lock_extent(struct extent_io_tree *tree, u64 start, u64 end, gfp_t mask);
int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     int bits, struct extent_state **cached, gfp_t mask);
int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end, gfp_t mask);
int unlock_extent_cached(struct extent_io_tree *tree, u64 start, u64 end,
			 struct extent_state **cached, gfp_t mask);
int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end,
		    gfp_t mask);
int extent_read_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent);
int __init extent_io_init(void);
void extent_io_exit(void);

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end,
		     u64 max_bytes, unsigned long bits, int contig);

void free_extent_state(struct extent_state *state);
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   int bits, int filled, struct extent_state *cached_state);
int clear_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		      int bits, gfp_t mask);
int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     int bits, int wake, int delete, struct extent_state **cached,
		     gfp_t mask);
int set_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		    int bits, gfp_t mask);
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   int bits, int exclusive_bits, u64 *failed_start,
		   struct extent_state **cached_state, gfp_t mask);
int set_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask);
int set_extent_new(struct extent_io_tree *tree, u64 start, u64 end,
		   gfp_t mask);
int set_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask);
int clear_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask);
int clear_extent_ordered(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask);
int clear_extent_ordered_metadata(struct extent_io_tree *tree, u64 start,
				  u64 end, gfp_t mask);
int set_extent_delalloc(struct extent_io_tree *tree, u64 start, u64 end,
			struct extent_state **cached_state, gfp_t mask);
int set_extent_ordered(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask);
int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, int bits);
struct extent_state *find_first_extent_bit_state(struct extent_io_tree *tree,
						 u64 start, int bits);
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
int extent_readpages(struct extent_io_tree *tree,
		     struct address_space *mapping,
		     struct list_head *pages, unsigned nr_pages,
		     get_extent_t get_extent);
int extent_prepare_write(struct extent_io_tree *tree,
			 struct inode *inode, struct page *page,
			 unsigned from, unsigned to, get_extent_t *get_extent);
int extent_commit_write(struct extent_io_tree *tree,
			struct inode *inode, struct page *page,
			unsigned from, unsigned to);
sector_t extent_bmap(struct address_space *mapping, sector_t iblock,
		get_extent_t *get_extent);
int extent_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len, get_extent_t *get_extent);
int set_range_dirty(struct extent_io_tree *tree, u64 start, u64 end);
int set_state_private(struct extent_io_tree *tree, u64 start, u64 private);
int get_state_private(struct extent_io_tree *tree, u64 start, u64 *private);
void set_page_extent_mapped(struct page *page);

struct extent_buffer *alloc_extent_buffer(struct extent_io_tree *tree,
					  u64 start, unsigned long len,
					  struct page *page0,
					  gfp_t mask);
struct extent_buffer *find_extent_buffer(struct extent_io_tree *tree,
					 u64 start, unsigned long len,
					  gfp_t mask);
void free_extent_buffer(struct extent_buffer *eb);
int read_extent_buffer_pages(struct extent_io_tree *tree,
			     struct extent_buffer *eb, u64 start, int wait,
			     get_extent_t *get_extent, int mirror_num);

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
int wait_on_extent_buffer_writeback(struct extent_io_tree *tree,
				    struct extent_buffer *eb);
int wait_on_extent_writeback(struct extent_io_tree *tree, u64 start, u64 end);
int wait_extent_bit(struct extent_io_tree *tree, u64 start, u64 end, int bits);
int clear_extent_buffer_dirty(struct extent_io_tree *tree,
			      struct extent_buffer *eb);
int set_extent_buffer_dirty(struct extent_io_tree *tree,
			     struct extent_buffer *eb);
int test_extent_buffer_dirty(struct extent_io_tree *tree,
			     struct extent_buffer *eb);
int set_extent_buffer_uptodate(struct extent_io_tree *tree,
			       struct extent_buffer *eb);
int clear_extent_buffer_uptodate(struct extent_io_tree *tree,
				struct extent_buffer *eb,
				struct extent_state **cached_state);
int extent_buffer_uptodate(struct extent_io_tree *tree,
			   struct extent_buffer *eb,
			   struct extent_state *cached_state);
int map_extent_buffer(struct extent_buffer *eb, unsigned long offset,
		      unsigned long min_len, char **token, char **map,
		      unsigned long *map_start,
		      unsigned long *map_len, int km);
int map_private_extent_buffer(struct extent_buffer *eb, unsigned long offset,
		      unsigned long min_len, char **token, char **map,
		      unsigned long *map_start,
		      unsigned long *map_len, int km);
void unmap_extent_buffer(struct extent_buffer *eb, char *token, int km);
int release_extent_buffer_tail_pages(struct extent_buffer *eb);
int extent_range_uptodate(struct extent_io_tree *tree,
			  u64 start, u64 end);
int extent_clear_unlock_delalloc(struct inode *inode,
				struct extent_io_tree *tree,
				u64 start, u64 end, struct page *locked_page,
				unsigned long op);
struct bio *
btrfs_bio_alloc(struct block_device *bdev, u64 first_sector, int nr_vecs,
		gfp_t gfp_flags);
#endif
