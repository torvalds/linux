/*
 * include/linux/buffer_head.h
 *
 * Everything to do with buffer_heads.
 */

#ifndef _LINUX_BUFFER_HEAD_H
#define _LINUX_BUFFER_HEAD_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <asm/atomic.h>

#ifdef CONFIG_BLOCK

enum bh_state_bits {
	BH_Uptodate,	/* Contains valid data */
	BH_Dirty,	/* Is dirty */
	BH_Lock,	/* Is locked */
	BH_Req,		/* Has been submitted for I/O */
	BH_Uptodate_Lock,/* Used by the first bh in a page, to serialise
			  * IO completion of other buffers in the page
			  */

	BH_Mapped,	/* Has a disk mapping */
	BH_New,		/* Disk mapping was newly created by get_block */
	BH_Async_Read,	/* Is under end_buffer_async_read I/O */
	BH_Async_Write,	/* Is under end_buffer_async_write I/O */
	BH_Delay,	/* Buffer is not yet allocated on disk */
	BH_Boundary,	/* Block is followed by a discontiguity */
	BH_Write_EIO,	/* I/O error on write */
	BH_Ordered,	/* ordered write */
	BH_Eopnotsupp,	/* operation not supported (barrier) */
	BH_Unwritten,	/* Buffer is allocated on disk but not written */
	BH_Quiet,	/* Buffer Error Prinks to be quiet */

	BH_PrivateStart,/* not a state bit, but the first bit available
			 * for private allocation by other entities
			 */
};

#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)

struct page;
struct buffer_head;
struct address_space;
typedef void (bh_end_io_t)(struct buffer_head *bh, int uptodate);

/*
 * Historically, a buffer_head was used to map a single block
 * within a page, and of course as the unit of I/O through the
 * filesystem and block layers.  Nowadays the basic I/O unit
 * is the bio, and buffer_heads are used for extracting block
 * mappings (via a get_block_t call), for tracking state within
 * a page (via a page_mapping) and for wrapping bio submission
 * for backward compatibility reasons (e.g. submit_bh).
 */
struct buffer_head {
	unsigned long b_state;		/* buffer state bitmap (see above) */
	struct buffer_head *b_this_page;/* circular list of page's buffers */
	struct page *b_page;		/* the page this bh is mapped to */

	sector_t b_blocknr;		/* start block number */
	size_t b_size;			/* size of mapping */
	char *b_data;			/* pointer to data within the page */

	struct block_device *b_bdev;
	bh_end_io_t *b_end_io;		/* I/O completion */
 	void *b_private;		/* reserved for b_end_io */
	struct list_head b_assoc_buffers; /* associated with another mapping */
	struct address_space *b_assoc_map;	/* mapping this buffer is
						   associated with */
	atomic_t b_count;		/* users using this buffer_head */
};

/*
 * macro tricks to expand the set_buffer_foo(), clear_buffer_foo()
 * and buffer_foo() functions.
 */
#define BUFFER_FNS(bit, name)						\
static inline void set_buffer_##name(struct buffer_head *bh)		\
{									\
	set_bit(BH_##bit, &(bh)->b_state);				\
}									\
static inline void clear_buffer_##name(struct buffer_head *bh)		\
{									\
	clear_bit(BH_##bit, &(bh)->b_state);				\
}									\
static inline int buffer_##name(const struct buffer_head *bh)		\
{									\
	return test_bit(BH_##bit, &(bh)->b_state);			\
}

/*
 * test_set_buffer_foo() and test_clear_buffer_foo()
 */
#define TAS_BUFFER_FNS(bit, name)					\
static inline int test_set_buffer_##name(struct buffer_head *bh)	\
{									\
	return test_and_set_bit(BH_##bit, &(bh)->b_state);		\
}									\
static inline int test_clear_buffer_##name(struct buffer_head *bh)	\
{									\
	return test_and_clear_bit(BH_##bit, &(bh)->b_state);		\
}									\

/*
 * Emit the buffer bitops functions.   Note that there are also functions
 * of the form "mark_buffer_foo()".  These are higher-level functions which
 * do something in addition to setting a b_state bit.
 */
BUFFER_FNS(Uptodate, uptodate)
BUFFER_FNS(Dirty, dirty)
TAS_BUFFER_FNS(Dirty, dirty)
BUFFER_FNS(Lock, locked)
BUFFER_FNS(Req, req)
TAS_BUFFER_FNS(Req, req)
BUFFER_FNS(Mapped, mapped)
BUFFER_FNS(New, new)
BUFFER_FNS(Async_Read, async_read)
BUFFER_FNS(Async_Write, async_write)
BUFFER_FNS(Delay, delay)
BUFFER_FNS(Boundary, boundary)
BUFFER_FNS(Write_EIO, write_io_error)
BUFFER_FNS(Ordered, ordered)
BUFFER_FNS(Eopnotsupp, eopnotsupp)
BUFFER_FNS(Unwritten, unwritten)

#define bh_offset(bh)		((unsigned long)(bh)->b_data & ~PAGE_MASK)
#define touch_buffer(bh)	mark_page_accessed(bh->b_page)

/* If we *know* page->private refers to buffer_heads */
#define page_buffers(page)					\
	({							\
		BUG_ON(!PagePrivate(page));			\
		((struct buffer_head *)page_private(page));	\
	})
#define page_has_buffers(page)	PagePrivate(page)

/*
 * Declarations
 */

void mark_buffer_dirty(struct buffer_head *bh);
void init_buffer(struct buffer_head *, bh_end_io_t *, void *);
void set_bh_page(struct buffer_head *bh,
		struct page *page, unsigned long offset);
int try_to_free_buffers(struct page *);
struct buffer_head *alloc_page_buffers(struct page *page, unsigned long size,
		int retry);
void create_empty_buffers(struct page *, unsigned long,
			unsigned long b_state);
void end_buffer_read_sync(struct buffer_head *bh, int uptodate);
void end_buffer_write_sync(struct buffer_head *bh, int uptodate);
void end_buffer_async_write(struct buffer_head *bh, int uptodate);

/* Things to do with buffers at mapping->private_list */
void mark_buffer_dirty_inode(struct buffer_head *bh, struct inode *inode);
int inode_has_buffers(struct inode *);
void invalidate_inode_buffers(struct inode *);
int remove_inode_buffers(struct inode *inode);
int sync_mapping_buffers(struct address_space *mapping);
void unmap_underlying_metadata(struct block_device *bdev, sector_t block);

void mark_buffer_async_write(struct buffer_head *bh);
void __wait_on_buffer(struct buffer_head *);
wait_queue_head_t *bh_waitq_head(struct buffer_head *bh);
struct buffer_head *__find_get_block(struct block_device *bdev, sector_t block,
			unsigned size);
struct buffer_head *__getblk(struct block_device *bdev, sector_t block,
			unsigned size);
void __brelse(struct buffer_head *);
void __bforget(struct buffer_head *);
void __breadahead(struct block_device *, sector_t block, unsigned int size);
struct buffer_head *__bread(struct block_device *, sector_t block, unsigned size);
void invalidate_bh_lrus(void);
struct buffer_head *alloc_buffer_head(gfp_t gfp_flags);
void free_buffer_head(struct buffer_head * bh);
void unlock_buffer(struct buffer_head *bh);
void __lock_buffer(struct buffer_head *bh);
void ll_rw_block(int, int, struct buffer_head * bh[]);
int sync_dirty_buffer(struct buffer_head *bh);
int submit_bh(int, struct buffer_head *);
void write_boundary_block(struct block_device *bdev,
			sector_t bblock, unsigned blocksize);
int bh_uptodate_or_lock(struct buffer_head *bh);
int bh_submit_read(struct buffer_head *bh);

extern int buffer_heads_over_limit;

/*
 * Generic address_space_operations implementations for buffer_head-backed
 * address_spaces.
 */
void block_invalidatepage(struct page *page, unsigned long offset);
int block_write_full_page(struct page *page, get_block_t *get_block,
				struct writeback_control *wbc);
int block_write_full_page_endio(struct page *page, get_block_t *get_block,
			struct writeback_control *wbc, bh_end_io_t *handler);
int block_read_full_page(struct page*, get_block_t*);
int block_is_partially_uptodate(struct page *page, read_descriptor_t *desc,
				unsigned long from);
int block_write_begin(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page **, void **, get_block_t*);
int block_write_end(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page *, void *);
int generic_write_end(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page *, void *);
void page_zero_new_buffers(struct page *page, unsigned from, unsigned to);
int block_prepare_write(struct page*, unsigned, unsigned, get_block_t*);
int cont_write_begin(struct file *, struct address_space *, loff_t,
			unsigned, unsigned, struct page **, void **,
			get_block_t *, loff_t *);
int generic_cont_expand_simple(struct inode *inode, loff_t size);
int block_commit_write(struct page *page, unsigned from, unsigned to);
int block_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf,
				get_block_t get_block);
void block_sync_page(struct page *);
sector_t generic_block_bmap(struct address_space *, sector_t, get_block_t *);
int block_truncate_page(struct address_space *, loff_t, get_block_t *);
int file_fsync(struct file *, struct dentry *, int);
int nobh_write_begin(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page **, void **, get_block_t*);
int nobh_write_end(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page *, void *);
int nobh_truncate_page(struct address_space *, loff_t, get_block_t *);
int nobh_writepage(struct page *page, get_block_t *get_block,
                        struct writeback_control *wbc);

void buffer_init(void);

/*
 * inline definitions
 */

static inline void attach_page_buffers(struct page *page,
		struct buffer_head *head)
{
	page_cache_get(page);
	SetPagePrivate(page);
	set_page_private(page, (unsigned long)head);
}

static inline void get_bh(struct buffer_head *bh)
{
        atomic_inc(&bh->b_count);
}

static inline void put_bh(struct buffer_head *bh)
{
        smp_mb__before_atomic_dec();
        atomic_dec(&bh->b_count);
}

static inline void brelse(struct buffer_head *bh)
{
	if (bh)
		__brelse(bh);
}

static inline void bforget(struct buffer_head *bh)
{
	if (bh)
		__bforget(bh);
}

static inline struct buffer_head *
sb_bread(struct super_block *sb, sector_t block)
{
	return __bread(sb->s_bdev, block, sb->s_blocksize);
}

static inline void
sb_breadahead(struct super_block *sb, sector_t block)
{
	__breadahead(sb->s_bdev, block, sb->s_blocksize);
}

static inline struct buffer_head *
sb_getblk(struct super_block *sb, sector_t block)
{
	return __getblk(sb->s_bdev, block, sb->s_blocksize);
}

static inline struct buffer_head *
sb_find_get_block(struct super_block *sb, sector_t block)
{
	return __find_get_block(sb->s_bdev, block, sb->s_blocksize);
}

static inline void
map_bh(struct buffer_head *bh, struct super_block *sb, sector_t block)
{
	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = block;
	bh->b_size = sb->s_blocksize;
}

/*
 * Calling wait_on_buffer() for a zero-ref buffer is illegal, so we call into
 * __wait_on_buffer() just to trip a debug check.  Because debug code in inline
 * functions is bloaty.
 */
static inline void wait_on_buffer(struct buffer_head *bh)
{
	might_sleep();
	if (buffer_locked(bh) || atomic_read(&bh->b_count) == 0)
		__wait_on_buffer(bh);
}

static inline int trylock_buffer(struct buffer_head *bh)
{
	return likely(!test_and_set_bit_lock(BH_Lock, &bh->b_state));
}

static inline void lock_buffer(struct buffer_head *bh)
{
	might_sleep();
	if (!trylock_buffer(bh))
		__lock_buffer(bh);
}

extern int __set_page_dirty_buffers(struct page *page);

#else /* CONFIG_BLOCK */

static inline void buffer_init(void) {}
static inline int try_to_free_buffers(struct page *page) { return 1; }
static inline int inode_has_buffers(struct inode *inode) { return 0; }
static inline void invalidate_inode_buffers(struct inode *inode) {}
static inline int remove_inode_buffers(struct inode *inode) { return 1; }
static inline int sync_mapping_buffers(struct address_space *mapping) { return 0; }

#endif /* CONFIG_BLOCK */
#endif /* _LINUX_BUFFER_HEAD_H */
