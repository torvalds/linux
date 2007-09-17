#ifndef __EXTENTMAP__
#define __EXTENTMAP__

#include <linux/rbtree.h>

#define EXTENT_MAP_INLINE (u64)-2
#define EXTENT_MAP_DELALLOC (u64)-1

struct extent_map_ops {
	int (*fill_delalloc)(struct inode *inode, u64 start, u64 end);
	int (*writepage_io_hook)(struct page *page, u64 start, u64 end);
	int (*readpage_io_hook)(struct page *page, u64 start, u64 end);
	int (*readpage_end_io_hook)(struct page *page, u64 start, u64 end);
	void (*writepage_end_io_hook)(struct page *page, u64 start, u64 end);
};

struct extent_map_tree {
	struct rb_root map;
	struct rb_root state;
	struct address_space *mapping;
	rwlock_t lock;
	struct extent_map_ops *ops;
};

/* note, this must start with the same fields as fs/extent_map.c:tree_entry */
struct extent_map {
	u64 start;
	u64 end; /* inclusive */
	int in_tree;
	struct rb_node rb_node;
	/* block_start and block_end are in bytes */
	u64 block_start;
	u64 block_end; /* inclusive */
	struct block_device *bdev;
	atomic_t refs;
};

/* note, this must start with the same fields as fs/extent_map.c:tree_entry */
struct extent_state {
	u64 start;
	u64 end; /* inclusive */
	int in_tree;
	struct rb_node rb_node;
	wait_queue_head_t wq;
	atomic_t refs;
	unsigned long state;

	/* for use by the FS */
	u64 private;

	struct list_head list;
};

struct extent_buffer {
	u64 start;
	u64 end; /* inclusive */
	char *addr;
	struct page *pages[];
};

typedef struct extent_map *(get_extent_t)(struct inode *inode,
					  struct page *page,
					  size_t page_offset,
					  u64 start, u64 end,
					  int create);

void extent_map_tree_init(struct extent_map_tree *tree,
			  struct address_space *mapping, gfp_t mask);
struct extent_map *lookup_extent_mapping(struct extent_map_tree *tree,
					 u64 start, u64 end);
int add_extent_mapping(struct extent_map_tree *tree,
		       struct extent_map *em);
int remove_extent_mapping(struct extent_map_tree *tree, struct extent_map *em);
int try_release_extent_mapping(struct extent_map_tree *tree, struct page *page);
int lock_extent(struct extent_map_tree *tree, u64 start, u64 end, gfp_t mask);
int unlock_extent(struct extent_map_tree *tree, u64 start, u64 end, gfp_t mask);
struct extent_map *alloc_extent_map(gfp_t mask);
void free_extent_map(struct extent_map *em);
int extent_read_full_page(struct extent_map_tree *tree, struct page *page,
			  get_extent_t *get_extent);
void __init extent_map_init(void);
void __exit extent_map_exit(void);
int extent_clean_all_trees(struct extent_map_tree *tree);
int set_extent_uptodate(struct extent_map_tree *tree, u64 start, u64 end,
			gfp_t mask);
int set_extent_new(struct extent_map_tree *tree, u64 start, u64 end,
		   gfp_t mask);
int set_extent_dirty(struct extent_map_tree *tree, u64 start, u64 end,
		     gfp_t mask);
int set_extent_delalloc(struct extent_map_tree *tree, u64 start, u64 end,
		     gfp_t mask);
int extent_invalidatepage(struct extent_map_tree *tree,
			  struct page *page, unsigned long offset);
int extent_write_full_page(struct extent_map_tree *tree, struct page *page,
			  get_extent_t *get_extent,
			  struct writeback_control *wbc);
int extent_prepare_write(struct extent_map_tree *tree,
			 struct inode *inode, struct page *page,
			 unsigned from, unsigned to, get_extent_t *get_extent);
int extent_commit_write(struct extent_map_tree *tree,
			struct inode *inode, struct page *page,
			unsigned from, unsigned to);
sector_t extent_bmap(struct address_space *mapping, sector_t iblock,
		get_extent_t *get_extent);
int set_range_dirty(struct extent_map_tree *tree, u64 start, u64 end);
int set_state_private(struct extent_map_tree *tree, u64 start, u64 private);
int get_state_private(struct extent_map_tree *tree, u64 start, u64 *private);
void set_page_extent_mapped(struct page *page);
#endif
