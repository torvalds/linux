/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/buffer_head.h
 *
 * Everything to do with buffer_heads.
 */

#ifndef _LINUX_BUFFER_HEAD_H
#define _LINUX_BUFFER_HEAD_H

#include <linux/types.h>
#include <linux/blk_types.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#ifdef CONFIG_BLOCK

enum bh_state_bits {
	BH_Uptodate,	/* Contains valid data */
	BH_Dirty,	/* Is dirty */
	BH_Lock,	/* Is locked */
	BH_Req,		/* Has been submitted for I/O */

	BH_Mapped,	/* Has a disk mapping */
	BH_New,		/* Disk mapping was newly created by get_block */
	BH_Async_Read,	/* Is under end_buffer_async_read I/O */
	BH_Async_Write,	/* Is under end_buffer_async_write I/O */
	BH_Delay,	/* Buffer is not yet allocated on disk */
	BH_Boundary,	/* Block is followed by a discontiguity */
	BH_Write_EIO,	/* I/O error on write */
	BH_Unwritten,	/* Buffer is allocated on disk but not written */
	BH_Quiet,	/* Buffer Error Prinks to be quiet */
	BH_Meta,	/* Buffer contains metadata */
	BH_Prio,	/* Buffer should be submitted with REQ_PRIO */
	BH_Defer_Completion, /* Defer AIO completion to workqueue */

	BH_PrivateStart,/* not a state bit, but the first bit available
			 * for private allocation by other entities
			 */
};

#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)

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
	union {
		struct page *b_page;	/* the page this bh is mapped to */
		struct folio *b_folio;	/* the folio this bh is mapped to */
	};

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
	spinlock_t b_uptodate_lock;	/* Used by the first bh in a page, to
					 * serialise IO completion of other
					 * buffers in the page */
};

/*
 * macro tricks to expand the set_buffer_foo(), clear_buffer_foo()
 * and buffer_foo() functions.
 * To avoid reset buffer flags that are already set, because that causes
 * a costly cache line transition, check the flag first.
 */
#define BUFFER_FNS(bit, name)						\
static __always_inline void set_buffer_##name(struct buffer_head *bh)	\
{									\
	if (!test_bit(BH_##bit, &(bh)->b_state))			\
		set_bit(BH_##bit, &(bh)->b_state);			\
}									\
static __always_inline void clear_buffer_##name(struct buffer_head *bh)	\
{									\
	clear_bit(BH_##bit, &(bh)->b_state);				\
}									\
static __always_inline int buffer_##name(const struct buffer_head *bh)	\
{									\
	return test_bit(BH_##bit, &(bh)->b_state);			\
}

/*
 * test_set_buffer_foo() and test_clear_buffer_foo()
 */
#define TAS_BUFFER_FNS(bit, name)					\
static __always_inline int test_set_buffer_##name(struct buffer_head *bh) \
{									\
	return test_and_set_bit(BH_##bit, &(bh)->b_state);		\
}									\
static __always_inline int test_clear_buffer_##name(struct buffer_head *bh) \
{									\
	return test_and_clear_bit(BH_##bit, &(bh)->b_state);		\
}									\

/*
 * Emit the buffer bitops functions.   Note that there are also functions
 * of the form "mark_buffer_foo()".  These are higher-level functions which
 * do something in addition to setting a b_state bit.
 */
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
BUFFER_FNS(Unwritten, unwritten)
BUFFER_FNS(Meta, meta)
BUFFER_FNS(Prio, prio)
BUFFER_FNS(Defer_Completion, defer_completion)

static __always_inline void set_buffer_uptodate(struct buffer_head *bh)
{
	/*
	 * If somebody else already set this uptodate, they will
	 * have done the memory barrier, and a reader will thus
	 * see *some* valid buffer state.
	 *
	 * Any other serialization (with IO errors or whatever that
	 * might clear the bit) has to come from other state (eg BH_Lock).
	 */
	if (test_bit(BH_Uptodate, &bh->b_state))
		return;

	/*
	 * make it consistent with folio_mark_uptodate
	 * pairs with smp_load_acquire in buffer_uptodate
	 */
	smp_mb__before_atomic();
	set_bit(BH_Uptodate, &bh->b_state);
}

static __always_inline void clear_buffer_uptodate(struct buffer_head *bh)
{
	clear_bit(BH_Uptodate, &bh->b_state);
}

static __always_inline int buffer_uptodate(const struct buffer_head *bh)
{
	/*
	 * make it consistent with folio_test_uptodate
	 * pairs with smp_mb__before_atomic in set_buffer_uptodate
	 */
	return test_bit_acquire(BH_Uptodate, &bh->b_state);
}

#define bh_offset(bh)		((unsigned long)(bh)->b_data & ~PAGE_MASK)

/* If we *know* page->private refers to buffer_heads */
#define page_buffers(page)					\
	({							\
		BUG_ON(!PagePrivate(page));			\
		((struct buffer_head *)page_private(page));	\
	})
#define page_has_buffers(page)	PagePrivate(page)
#define folio_buffers(folio)		folio_get_private(folio)

void buffer_check_dirty_writeback(struct folio *folio,
				     bool *dirty, bool *writeback);

/*
 * Declarations
 */

void mark_buffer_dirty(struct buffer_head *bh);
void mark_buffer_write_io_error(struct buffer_head *bh);
void touch_buffer(struct buffer_head *bh);
void set_bh_page(struct buffer_head *bh,
		struct page *page, unsigned long offset);
void folio_set_bh(struct buffer_head *bh, struct folio *folio,
		  unsigned long offset);
bool try_to_free_buffers(struct folio *);
struct buffer_head *folio_alloc_buffers(struct folio *folio, unsigned long size,
					bool retry);
struct buffer_head *alloc_page_buffers(struct page *page, unsigned long size,
		bool retry);
void create_empty_buffers(struct page *, unsigned long,
			unsigned long b_state);
void folio_create_empty_buffers(struct folio *folio, unsigned long blocksize,
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
void clean_bdev_aliases(struct block_device *bdev, sector_t block,
			sector_t len);
static inline void clean_bdev_bh_alias(struct buffer_head *bh)
{
	clean_bdev_aliases(bh->b_bdev, bh->b_blocknr, 1);
}

void mark_buffer_async_write(struct buffer_head *bh);
void __wait_on_buffer(struct buffer_head *);
wait_queue_head_t *bh_waitq_head(struct buffer_head *bh);
struct buffer_head *__find_get_block(struct block_device *bdev, sector_t block,
			unsigned size);
struct buffer_head *__getblk_gfp(struct block_device *bdev, sector_t block,
				  unsigned size, gfp_t gfp);
void __brelse(struct buffer_head *);
void __bforget(struct buffer_head *);
void __breadahead(struct block_device *, sector_t block, unsigned int size);
struct buffer_head *__bread_gfp(struct block_device *,
				sector_t block, unsigned size, gfp_t gfp);
void invalidate_bh_lrus(void);
void invalidate_bh_lrus_cpu(void);
bool has_bh_in_lru(int cpu, void *dummy);
struct buffer_head *alloc_buffer_head(gfp_t gfp_flags);
void free_buffer_head(struct buffer_head * bh);
void unlock_buffer(struct buffer_head *bh);
void __lock_buffer(struct buffer_head *bh);
int sync_dirty_buffer(struct buffer_head *bh);
int __sync_dirty_buffer(struct buffer_head *bh, blk_opf_t op_flags);
void write_dirty_buffer(struct buffer_head *bh, blk_opf_t op_flags);
void submit_bh(blk_opf_t, struct buffer_head *);
void write_boundary_block(struct block_device *bdev,
			sector_t bblock, unsigned blocksize);
int bh_uptodate_or_lock(struct buffer_head *bh);
int __bh_read(struct buffer_head *bh, blk_opf_t op_flags, bool wait);
void __bh_read_batch(int nr, struct buffer_head *bhs[],
		     blk_opf_t op_flags, bool force_lock);

extern int buffer_heads_over_limit;

/*
 * Generic address_space_operations implementations for buffer_head-backed
 * address_spaces.
 */
void block_invalidate_folio(struct folio *folio, size_t offset, size_t length);
int block_write_full_page(struct page *page, get_block_t *get_block,
				struct writeback_control *wbc);
int __block_write_full_folio(struct inode *inode, struct folio *folio,
			get_block_t *get_block, struct writeback_control *wbc,
			bh_end_io_t *handler);
int block_read_full_folio(struct folio *, get_block_t *);
bool block_is_partially_uptodate(struct folio *, size_t from, size_t count);
int block_write_begin(struct address_space *mapping, loff_t pos, unsigned len,
		struct page **pagep, get_block_t *get_block);
int __block_write_begin(struct page *page, loff_t pos, unsigned len,
		get_block_t *get_block);
int block_write_end(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page *, void *);
int generic_write_end(struct file *, struct address_space *,
				loff_t, unsigned, unsigned,
				struct page *, void *);
void folio_zero_new_buffers(struct folio *folio, size_t from, size_t to);
void clean_page_buffers(struct page *page);
int cont_write_begin(struct file *, struct address_space *, loff_t,
			unsigned, struct page **, void **,
			get_block_t *, loff_t *);
int generic_cont_expand_simple(struct inode *inode, loff_t size);
int block_commit_write(struct page *page, unsigned from, unsigned to);
int block_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf,
				get_block_t get_block);
/* Convert errno to return value from ->page_mkwrite() call */
static inline vm_fault_t block_page_mkwrite_return(int err)
{
	if (err == 0)
		return VM_FAULT_LOCKED;
	if (err == -EFAULT || err == -EAGAIN)
		return VM_FAULT_NOPAGE;
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	/* -ENOSPC, -EDQUOT, -EIO ... */
	return VM_FAULT_SIGBUS;
}
sector_t generic_block_bmap(struct address_space *, sector_t, get_block_t *);
int block_truncate_page(struct address_space *, loff_t, get_block_t *);

#ifdef CONFIG_MIGRATION
extern int buffer_migrate_folio(struct address_space *,
		struct folio *dst, struct folio *src, enum migrate_mode);
extern int buffer_migrate_folio_norefs(struct address_space *,
		struct folio *dst, struct folio *src, enum migrate_mode);
#else
#define buffer_migrate_folio NULL
#define buffer_migrate_folio_norefs NULL
#endif

void buffer_init(void);

/*
 * inline definitions
 */

static inline void get_bh(struct buffer_head *bh)
{
        atomic_inc(&bh->b_count);
}

static inline void put_bh(struct buffer_head *bh)
{
        smp_mb__before_atomic();
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
	return __bread_gfp(sb->s_bdev, block, sb->s_blocksize, __GFP_MOVABLE);
}

static inline struct buffer_head *
sb_bread_unmovable(struct super_block *sb, sector_t block)
{
	return __bread_gfp(sb->s_bdev, block, sb->s_blocksize, 0);
}

static inline void
sb_breadahead(struct super_block *sb, sector_t block)
{
	__breadahead(sb->s_bdev, block, sb->s_blocksize);
}

static inline struct buffer_head *
sb_getblk(struct super_block *sb, sector_t block)
{
	return __getblk_gfp(sb->s_bdev, block, sb->s_blocksize, __GFP_MOVABLE);
}


static inline struct buffer_head *
sb_getblk_gfp(struct super_block *sb, sector_t block, gfp_t gfp)
{
	return __getblk_gfp(sb->s_bdev, block, sb->s_blocksize, gfp);
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

static inline void wait_on_buffer(struct buffer_head *bh)
{
	might_sleep();
	if (buffer_locked(bh))
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

static inline struct buffer_head *getblk_unmovable(struct block_device *bdev,
						   sector_t block,
						   unsigned size)
{
	return __getblk_gfp(bdev, block, size, 0);
}

static inline struct buffer_head *__getblk(struct block_device *bdev,
					   sector_t block,
					   unsigned size)
{
	return __getblk_gfp(bdev, block, size, __GFP_MOVABLE);
}

static inline void bh_readahead(struct buffer_head *bh, blk_opf_t op_flags)
{
	if (!buffer_uptodate(bh) && trylock_buffer(bh)) {
		if (!buffer_uptodate(bh))
			__bh_read(bh, op_flags, false);
		else
			unlock_buffer(bh);
	}
}

static inline void bh_read_nowait(struct buffer_head *bh, blk_opf_t op_flags)
{
	if (!bh_uptodate_or_lock(bh))
		__bh_read(bh, op_flags, false);
}

/* Returns 1 if buffer uptodated, 0 on success, and -EIO on error. */
static inline int bh_read(struct buffer_head *bh, blk_opf_t op_flags)
{
	if (bh_uptodate_or_lock(bh))
		return 1;
	return __bh_read(bh, op_flags, true);
}

static inline void bh_read_batch(int nr, struct buffer_head *bhs[])
{
	__bh_read_batch(nr, bhs, 0, true);
}

static inline void bh_readahead_batch(int nr, struct buffer_head *bhs[],
				      blk_opf_t op_flags)
{
	__bh_read_batch(nr, bhs, op_flags, false);
}

/**
 *  __bread() - reads a specified block and returns the bh
 *  @bdev: the block_device to read from
 *  @block: number of block
 *  @size: size (in bytes) to read
 *
 *  Reads a specified block, and returns buffer head that contains it.
 *  The page cache is allocated from movable area so that it can be migrated.
 *  It returns NULL if the block was unreadable.
 */
static inline struct buffer_head *
__bread(struct block_device *bdev, sector_t block, unsigned size)
{
	return __bread_gfp(bdev, block, size, __GFP_MOVABLE);
}

bool block_dirty_folio(struct address_space *mapping, struct folio *folio);

#else /* CONFIG_BLOCK */

static inline void buffer_init(void) {}
static inline bool try_to_free_buffers(struct folio *folio) { return true; }
static inline int inode_has_buffers(struct inode *inode) { return 0; }
static inline void invalidate_inode_buffers(struct inode *inode) {}
static inline int remove_inode_buffers(struct inode *inode) { return 1; }
static inline int sync_mapping_buffers(struct address_space *mapping) { return 0; }
static inline void invalidate_bh_lrus_cpu(void) {}
static inline bool has_bh_in_lru(int cpu, void *dummy) { return false; }
#define buffer_heads_over_limit 0

#endif /* CONFIG_BLOCK */
#endif /* _LINUX_BUFFER_HEAD_H */
