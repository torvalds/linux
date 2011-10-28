/*
 * fs/logfs/readwrite.c
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 *
 *
 * Actually contains five sets of very similar functions:
 * read		read blocks from a file
 * seek_hole	find next hole
 * seek_data	find next data block
 * valid	check whether a block still belongs to a file
 * write	write blocks to a file
 * delete	delete a block (for directories and ifile)
 * rewrite	move existing blocks of a file to a new location (gc helper)
 * truncate	truncate a file
 */
#include "logfs.h"
#include <linux/sched.h>
#include <linux/slab.h>

static u64 adjust_bix(u64 bix, level_t level)
{
	switch (level) {
	case 0:
		return bix;
	case LEVEL(1):
		return max_t(u64, bix, I0_BLOCKS);
	case LEVEL(2):
		return max_t(u64, bix, I1_BLOCKS);
	case LEVEL(3):
		return max_t(u64, bix, I2_BLOCKS);
	case LEVEL(4):
		return max_t(u64, bix, I3_BLOCKS);
	case LEVEL(5):
		return max_t(u64, bix, I4_BLOCKS);
	default:
		WARN_ON(1);
		return bix;
	}
}

static inline u64 maxbix(u8 height)
{
	return 1ULL << (LOGFS_BLOCK_BITS * height);
}

/**
 * The inode address space is cut in two halves.  Lower half belongs to data
 * pages, upper half to indirect blocks.  If the high bit (INDIRECT_BIT) is
 * set, the actual block index (bix) and level can be derived from the page
 * index.
 *
 * The lowest three bits of the block index are set to 0 after packing and
 * unpacking.  Since the lowest n bits (9 for 4KiB blocksize) are ignored
 * anyway this is harmless.
 */
#define ARCH_SHIFT	(BITS_PER_LONG - 32)
#define INDIRECT_BIT	(0x80000000UL << ARCH_SHIFT)
#define LEVEL_SHIFT	(28 + ARCH_SHIFT)
static inline pgoff_t first_indirect_block(void)
{
	return INDIRECT_BIT | (1ULL << LEVEL_SHIFT);
}

pgoff_t logfs_pack_index(u64 bix, level_t level)
{
	pgoff_t index;

	BUG_ON(bix >= INDIRECT_BIT);
	if (level == 0)
		return bix;

	index  = INDIRECT_BIT;
	index |= (__force long)level << LEVEL_SHIFT;
	index |= bix >> ((__force u8)level * LOGFS_BLOCK_BITS);
	return index;
}

void logfs_unpack_index(pgoff_t index, u64 *bix, level_t *level)
{
	u8 __level;

	if (!(index & INDIRECT_BIT)) {
		*bix = index;
		*level = 0;
		return;
	}

	__level = (index & ~INDIRECT_BIT) >> LEVEL_SHIFT;
	*level = LEVEL(__level);
	*bix = (index << (__level * LOGFS_BLOCK_BITS)) & ~INDIRECT_BIT;
	*bix = adjust_bix(*bix, *level);
	return;
}
#undef ARCH_SHIFT
#undef INDIRECT_BIT
#undef LEVEL_SHIFT

/*
 * Time is stored as nanoseconds since the epoch.
 */
static struct timespec be64_to_timespec(__be64 betime)
{
	return ns_to_timespec(be64_to_cpu(betime));
}

static __be64 timespec_to_be64(struct timespec tsp)
{
	return cpu_to_be64((u64)tsp.tv_sec * NSEC_PER_SEC + tsp.tv_nsec);
}

static void logfs_disk_to_inode(struct logfs_disk_inode *di, struct inode*inode)
{
	struct logfs_inode *li = logfs_inode(inode);
	int i;

	inode->i_mode	= be16_to_cpu(di->di_mode);
	li->li_height	= di->di_height;
	li->li_flags	= be32_to_cpu(di->di_flags);
	inode->i_uid	= be32_to_cpu(di->di_uid);
	inode->i_gid	= be32_to_cpu(di->di_gid);
	inode->i_size	= be64_to_cpu(di->di_size);
	logfs_set_blocks(inode, be64_to_cpu(di->di_used_bytes));
	inode->i_atime	= be64_to_timespec(di->di_atime);
	inode->i_ctime	= be64_to_timespec(di->di_ctime);
	inode->i_mtime	= be64_to_timespec(di->di_mtime);
	set_nlink(inode, be32_to_cpu(di->di_refcount));
	inode->i_generation = be32_to_cpu(di->di_generation);

	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:	/* fall through */
	case S_IFBLK:	/* fall through */
	case S_IFCHR:	/* fall through */
	case S_IFIFO:
		inode->i_rdev = be64_to_cpu(di->di_data[0]);
		break;
	case S_IFDIR:	/* fall through */
	case S_IFREG:	/* fall through */
	case S_IFLNK:
		for (i = 0; i < LOGFS_EMBEDDED_FIELDS; i++)
			li->li_data[i] = be64_to_cpu(di->di_data[i]);
		break;
	default:
		BUG();
	}
}

static void logfs_inode_to_disk(struct inode *inode, struct logfs_disk_inode*di)
{
	struct logfs_inode *li = logfs_inode(inode);
	int i;

	di->di_mode	= cpu_to_be16(inode->i_mode);
	di->di_height	= li->li_height;
	di->di_pad	= 0;
	di->di_flags	= cpu_to_be32(li->li_flags);
	di->di_uid	= cpu_to_be32(inode->i_uid);
	di->di_gid	= cpu_to_be32(inode->i_gid);
	di->di_size	= cpu_to_be64(i_size_read(inode));
	di->di_used_bytes = cpu_to_be64(li->li_used_bytes);
	di->di_atime	= timespec_to_be64(inode->i_atime);
	di->di_ctime	= timespec_to_be64(inode->i_ctime);
	di->di_mtime	= timespec_to_be64(inode->i_mtime);
	di->di_refcount	= cpu_to_be32(inode->i_nlink);
	di->di_generation = cpu_to_be32(inode->i_generation);

	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:	/* fall through */
	case S_IFBLK:	/* fall through */
	case S_IFCHR:	/* fall through */
	case S_IFIFO:
		di->di_data[0] = cpu_to_be64(inode->i_rdev);
		break;
	case S_IFDIR:	/* fall through */
	case S_IFREG:	/* fall through */
	case S_IFLNK:
		for (i = 0; i < LOGFS_EMBEDDED_FIELDS; i++)
			di->di_data[i] = cpu_to_be64(li->li_data[i]);
		break;
	default:
		BUG();
	}
}

static void __logfs_set_blocks(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_inode *li = logfs_inode(inode);

	inode->i_blocks = ULONG_MAX;
	if (li->li_used_bytes >> sb->s_blocksize_bits < ULONG_MAX)
		inode->i_blocks = ALIGN(li->li_used_bytes, 512) >> 9;
}

void logfs_set_blocks(struct inode *inode, u64 bytes)
{
	struct logfs_inode *li = logfs_inode(inode);

	li->li_used_bytes = bytes;
	__logfs_set_blocks(inode);
}

static void prelock_page(struct super_block *sb, struct page *page, int lock)
{
	struct logfs_super *super = logfs_super(sb);

	BUG_ON(!PageLocked(page));
	if (lock) {
		BUG_ON(PagePreLocked(page));
		SetPagePreLocked(page);
	} else {
		/* We are in GC path. */
		if (PagePreLocked(page))
			super->s_lock_count++;
		else
			SetPagePreLocked(page);
	}
}

static void preunlock_page(struct super_block *sb, struct page *page, int lock)
{
	struct logfs_super *super = logfs_super(sb);

	BUG_ON(!PageLocked(page));
	if (lock)
		ClearPagePreLocked(page);
	else {
		/* We are in GC path. */
		BUG_ON(!PagePreLocked(page));
		if (super->s_lock_count)
			super->s_lock_count--;
		else
			ClearPagePreLocked(page);
	}
}

/*
 * Logfs is prone to an AB-BA deadlock where one task tries to acquire
 * s_write_mutex with a locked page and GC tries to get that page while holding
 * s_write_mutex.
 * To solve this issue logfs will ignore the page lock iff the page in question
 * is waiting for s_write_mutex.  We annotate this fact by setting PG_pre_locked
 * in addition to PG_locked.
 */
static void logfs_get_wblocks(struct super_block *sb, struct page *page,
		int lock)
{
	struct logfs_super *super = logfs_super(sb);

	if (page)
		prelock_page(sb, page, lock);

	if (lock) {
		mutex_lock(&super->s_write_mutex);
		logfs_gc_pass(sb);
		/* FIXME: We also have to check for shadowed space
		 * and mempool fill grade */
	}
}

static void logfs_put_wblocks(struct super_block *sb, struct page *page,
		int lock)
{
	struct logfs_super *super = logfs_super(sb);

	if (page)
		preunlock_page(sb, page, lock);
	/* Order matters - we must clear PG_pre_locked before releasing
	 * s_write_mutex or we could race against another task. */
	if (lock)
		mutex_unlock(&super->s_write_mutex);
}

static struct page *logfs_get_read_page(struct inode *inode, u64 bix,
		level_t level)
{
	return find_or_create_page(inode->i_mapping,
			logfs_pack_index(bix, level), GFP_NOFS);
}

static void logfs_put_read_page(struct page *page)
{
	unlock_page(page);
	page_cache_release(page);
}

static void logfs_lock_write_page(struct page *page)
{
	int loop = 0;

	while (unlikely(!trylock_page(page))) {
		if (loop++ > 0x1000) {
			/* Has been observed once so far... */
			printk(KERN_ERR "stack at %p\n", &loop);
			BUG();
		}
		if (PagePreLocked(page)) {
			/* Holder of page lock is waiting for us, it
			 * is safe to use this page. */
			break;
		}
		/* Some other process has this page locked and has
		 * nothing to do with us.  Wait for it to finish.
		 */
		schedule();
	}
	BUG_ON(!PageLocked(page));
}

static struct page *logfs_get_write_page(struct inode *inode, u64 bix,
		level_t level)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index = logfs_pack_index(bix, level);
	struct page *page;
	int err;

repeat:
	page = find_get_page(mapping, index);
	if (!page) {
		page = __page_cache_alloc(GFP_NOFS);
		if (!page)
			return NULL;
		err = add_to_page_cache_lru(page, mapping, index, GFP_NOFS);
		if (unlikely(err)) {
			page_cache_release(page);
			if (err == -EEXIST)
				goto repeat;
			return NULL;
		}
	} else logfs_lock_write_page(page);
	BUG_ON(!PageLocked(page));
	return page;
}

static void logfs_unlock_write_page(struct page *page)
{
	if (!PagePreLocked(page))
		unlock_page(page);
}

static void logfs_put_write_page(struct page *page)
{
	logfs_unlock_write_page(page);
	page_cache_release(page);
}

static struct page *logfs_get_page(struct inode *inode, u64 bix, level_t level,
		int rw)
{
	if (rw == READ)
		return logfs_get_read_page(inode, bix, level);
	else
		return logfs_get_write_page(inode, bix, level);
}

static void logfs_put_page(struct page *page, int rw)
{
	if (rw == READ)
		logfs_put_read_page(page);
	else
		logfs_put_write_page(page);
}

static unsigned long __get_bits(u64 val, int skip, int no)
{
	u64 ret = val;

	ret >>= skip * no;
	ret <<= 64 - no;
	ret >>= 64 - no;
	return ret;
}

static unsigned long get_bits(u64 val, level_t skip)
{
	return __get_bits(val, (__force int)skip, LOGFS_BLOCK_BITS);
}

static inline void init_shadow_tree(struct super_block *sb,
		struct shadow_tree *tree)
{
	struct logfs_super *super = logfs_super(sb);

	btree_init_mempool64(&tree->new, super->s_btree_pool);
	btree_init_mempool64(&tree->old, super->s_btree_pool);
}

static void indirect_write_block(struct logfs_block *block)
{
	struct page *page;
	struct inode *inode;
	int ret;

	page = block->page;
	inode = page->mapping->host;
	logfs_lock_write_page(page);
	ret = logfs_write_buf(inode, page, 0);
	logfs_unlock_write_page(page);
	/*
	 * This needs some rework.  Unless you want your filesystem to run
	 * completely synchronously (you don't), the filesystem will always
	 * report writes as 'successful' before the actual work has been
	 * done.  The actual work gets done here and this is where any errors
	 * will show up.  And there isn't much we can do about it, really.
	 *
	 * Some attempts to fix the errors (move from bad blocks, retry io,...)
	 * have already been done, so anything left should be either a broken
	 * device or a bug somewhere in logfs itself.  Being relatively new,
	 * the odds currently favor a bug, so for now the line below isn't
	 * entirely tasteles.
	 */
	BUG_ON(ret);
}

static void inode_write_block(struct logfs_block *block)
{
	struct inode *inode;
	int ret;

	inode = block->inode;
	if (inode->i_ino == LOGFS_INO_MASTER)
		logfs_write_anchor(inode->i_sb);
	else {
		ret = __logfs_write_inode(inode, 0);
		/* see indirect_write_block comment */
		BUG_ON(ret);
	}
}

/*
 * This silences a false, yet annoying gcc warning.  I hate it when my editor
 * jumps into bitops.h each time I recompile this file.
 * TODO: Complain to gcc folks about this and upgrade compiler.
 */
static unsigned long fnb(const unsigned long *addr,
		unsigned long size, unsigned long offset)
{
	return find_next_bit(addr, size, offset);
}

static __be64 inode_val0(struct inode *inode)
{
	struct logfs_inode *li = logfs_inode(inode);
	u64 val;

	/*
	 * Explicit shifting generates good code, but must match the format
	 * of the structure.  Add some paranoia just in case.
	 */
	BUILD_BUG_ON(offsetof(struct logfs_disk_inode, di_mode) != 0);
	BUILD_BUG_ON(offsetof(struct logfs_disk_inode, di_height) != 2);
	BUILD_BUG_ON(offsetof(struct logfs_disk_inode, di_flags) != 4);

	val =	(u64)inode->i_mode << 48 |
		(u64)li->li_height << 40 |
		(u64)li->li_flags;
	return cpu_to_be64(val);
}

static int inode_write_alias(struct super_block *sb,
		struct logfs_block *block, write_alias_t *write_one_alias)
{
	struct inode *inode = block->inode;
	struct logfs_inode *li = logfs_inode(inode);
	unsigned long pos;
	u64 ino , bix;
	__be64 val;
	level_t level;
	int err;

	for (pos = 0; ; pos++) {
		pos = fnb(block->alias_map, LOGFS_BLOCK_FACTOR, pos);
		if (pos >= LOGFS_EMBEDDED_FIELDS + INODE_POINTER_OFS)
			return 0;

		switch (pos) {
		case INODE_HEIGHT_OFS:
			val = inode_val0(inode);
			break;
		case INODE_USED_OFS:
			val = cpu_to_be64(li->li_used_bytes);
			break;
		case INODE_SIZE_OFS:
			val = cpu_to_be64(i_size_read(inode));
			break;
		case INODE_POINTER_OFS ... INODE_POINTER_OFS + LOGFS_EMBEDDED_FIELDS - 1:
			val = cpu_to_be64(li->li_data[pos - INODE_POINTER_OFS]);
			break;
		default:
			BUG();
		}

		ino = LOGFS_INO_MASTER;
		bix = inode->i_ino;
		level = LEVEL(0);
		err = write_one_alias(sb, ino, bix, level, pos, val);
		if (err)
			return err;
	}
}

static int indirect_write_alias(struct super_block *sb,
		struct logfs_block *block, write_alias_t *write_one_alias)
{
	unsigned long pos;
	struct page *page = block->page;
	u64 ino , bix;
	__be64 *child, val;
	level_t level;
	int err;

	for (pos = 0; ; pos++) {
		pos = fnb(block->alias_map, LOGFS_BLOCK_FACTOR, pos);
		if (pos >= LOGFS_BLOCK_FACTOR)
			return 0;

		ino = page->mapping->host->i_ino;
		logfs_unpack_index(page->index, &bix, &level);
		child = kmap_atomic(page, KM_USER0);
		val = child[pos];
		kunmap_atomic(child, KM_USER0);
		err = write_one_alias(sb, ino, bix, level, pos, val);
		if (err)
			return err;
	}
}

int logfs_write_obj_aliases_pagecache(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_block *block;
	int err;

	list_for_each_entry(block, &super->s_object_alias, alias_list) {
		err = block->ops->write_alias(sb, block, write_alias_journal);
		if (err)
			return err;
	}
	return 0;
}

void __free_block(struct super_block *sb, struct logfs_block *block)
{
	BUG_ON(!list_empty(&block->item_list));
	list_del(&block->alias_list);
	mempool_free(block, logfs_super(sb)->s_block_pool);
}

static void inode_free_block(struct super_block *sb, struct logfs_block *block)
{
	struct inode *inode = block->inode;

	logfs_inode(inode)->li_block = NULL;
	__free_block(sb, block);
}

static void indirect_free_block(struct super_block *sb,
		struct logfs_block *block)
{
	ClearPagePrivate(block->page);
	block->page->private = 0;
	__free_block(sb, block);
}


static struct logfs_block_ops inode_block_ops = {
	.write_block = inode_write_block,
	.free_block = inode_free_block,
	.write_alias = inode_write_alias,
};

struct logfs_block_ops indirect_block_ops = {
	.write_block = indirect_write_block,
	.free_block = indirect_free_block,
	.write_alias = indirect_write_alias,
};

struct logfs_block *__alloc_block(struct super_block *sb,
		u64 ino, u64 bix, level_t level)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_block *block;

	block = mempool_alloc(super->s_block_pool, GFP_NOFS);
	memset(block, 0, sizeof(*block));
	INIT_LIST_HEAD(&block->alias_list);
	INIT_LIST_HEAD(&block->item_list);
	block->sb = sb;
	block->ino = ino;
	block->bix = bix;
	block->level = level;
	return block;
}

static void alloc_inode_block(struct inode *inode)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct logfs_block *block;

	if (li->li_block)
		return;

	block = __alloc_block(inode->i_sb, LOGFS_INO_MASTER, inode->i_ino, 0);
	block->inode = inode;
	li->li_block = block;
	block->ops = &inode_block_ops;
}

void initialize_block_counters(struct page *page, struct logfs_block *block,
		__be64 *array, int page_is_empty)
{
	u64 ptr;
	int i, start;

	block->partial = 0;
	block->full = 0;
	start = 0;
	if (page->index < first_indirect_block()) {
		/* Counters are pointless on level 0 */
		return;
	}
	if (page->index == first_indirect_block()) {
		/* Skip unused pointers */
		start = I0_BLOCKS;
		block->full = I0_BLOCKS;
	}
	if (!page_is_empty) {
		for (i = start; i < LOGFS_BLOCK_FACTOR; i++) {
			ptr = be64_to_cpu(array[i]);
			if (ptr)
				block->partial++;
			if (ptr & LOGFS_FULLY_POPULATED)
				block->full++;
		}
	}
}

static void alloc_data_block(struct inode *inode, struct page *page)
{
	struct logfs_block *block;
	u64 bix;
	level_t level;

	if (PagePrivate(page))
		return;

	logfs_unpack_index(page->index, &bix, &level);
	block = __alloc_block(inode->i_sb, inode->i_ino, bix, level);
	block->page = page;
	SetPagePrivate(page);
	page->private = (unsigned long)block;
	block->ops = &indirect_block_ops;
}

static void alloc_indirect_block(struct inode *inode, struct page *page,
		int page_is_empty)
{
	struct logfs_block *block;
	__be64 *array;

	if (PagePrivate(page))
		return;

	alloc_data_block(inode, page);

	block = logfs_block(page);
	array = kmap_atomic(page, KM_USER0);
	initialize_block_counters(page, block, array, page_is_empty);
	kunmap_atomic(array, KM_USER0);
}

static void block_set_pointer(struct page *page, int index, u64 ptr)
{
	struct logfs_block *block = logfs_block(page);
	__be64 *array;
	u64 oldptr;

	BUG_ON(!block);
	array = kmap_atomic(page, KM_USER0);
	oldptr = be64_to_cpu(array[index]);
	array[index] = cpu_to_be64(ptr);
	kunmap_atomic(array, KM_USER0);
	SetPageUptodate(page);

	block->full += !!(ptr & LOGFS_FULLY_POPULATED)
		- !!(oldptr & LOGFS_FULLY_POPULATED);
	block->partial += !!ptr - !!oldptr;
}

static u64 block_get_pointer(struct page *page, int index)
{
	__be64 *block;
	u64 ptr;

	block = kmap_atomic(page, KM_USER0);
	ptr = be64_to_cpu(block[index]);
	kunmap_atomic(block, KM_USER0);
	return ptr;
}

static int logfs_read_empty(struct page *page)
{
	zero_user_segment(page, 0, PAGE_CACHE_SIZE);
	return 0;
}

static int logfs_read_direct(struct inode *inode, struct page *page)
{
	struct logfs_inode *li = logfs_inode(inode);
	pgoff_t index = page->index;
	u64 block;

	block = li->li_data[index];
	if (!block)
		return logfs_read_empty(page);

	return logfs_segment_read(inode, page, block, index, 0);
}

static int logfs_read_loop(struct inode *inode, struct page *page,
		int rw_context)
{
	struct logfs_inode *li = logfs_inode(inode);
	u64 bix, bofs = li->li_data[INDIRECT_INDEX];
	level_t level, target_level;
	int ret;
	struct page *ipage;

	logfs_unpack_index(page->index, &bix, &target_level);
	if (!bofs)
		return logfs_read_empty(page);

	if (bix >= maxbix(li->li_height))
		return logfs_read_empty(page);

	for (level = LEVEL(li->li_height);
			(__force u8)level > (__force u8)target_level;
			level = SUBLEVEL(level)){
		ipage = logfs_get_page(inode, bix, level, rw_context);
		if (!ipage)
			return -ENOMEM;

		ret = logfs_segment_read(inode, ipage, bofs, bix, level);
		if (ret) {
			logfs_put_read_page(ipage);
			return ret;
		}

		bofs = block_get_pointer(ipage, get_bits(bix, SUBLEVEL(level)));
		logfs_put_page(ipage, rw_context);
		if (!bofs)
			return logfs_read_empty(page);
	}

	return logfs_segment_read(inode, page, bofs, bix, 0);
}

static int logfs_read_block(struct inode *inode, struct page *page,
		int rw_context)
{
	pgoff_t index = page->index;

	if (index < I0_BLOCKS)
		return logfs_read_direct(inode, page);
	return logfs_read_loop(inode, page, rw_context);
}

static int logfs_exist_loop(struct inode *inode, u64 bix)
{
	struct logfs_inode *li = logfs_inode(inode);
	u64 bofs = li->li_data[INDIRECT_INDEX];
	level_t level;
	int ret;
	struct page *ipage;

	if (!bofs)
		return 0;
	if (bix >= maxbix(li->li_height))
		return 0;

	for (level = LEVEL(li->li_height); level != 0; level = SUBLEVEL(level)) {
		ipage = logfs_get_read_page(inode, bix, level);
		if (!ipage)
			return -ENOMEM;

		ret = logfs_segment_read(inode, ipage, bofs, bix, level);
		if (ret) {
			logfs_put_read_page(ipage);
			return ret;
		}

		bofs = block_get_pointer(ipage, get_bits(bix, SUBLEVEL(level)));
		logfs_put_read_page(ipage);
		if (!bofs)
			return 0;
	}

	return 1;
}

int logfs_exist_block(struct inode *inode, u64 bix)
{
	struct logfs_inode *li = logfs_inode(inode);

	if (bix < I0_BLOCKS)
		return !!li->li_data[bix];
	return logfs_exist_loop(inode, bix);
}

static u64 seek_holedata_direct(struct inode *inode, u64 bix, int data)
{
	struct logfs_inode *li = logfs_inode(inode);

	for (; bix < I0_BLOCKS; bix++)
		if (data ^ (li->li_data[bix] == 0))
			return bix;
	return I0_BLOCKS;
}

static u64 seek_holedata_loop(struct inode *inode, u64 bix, int data)
{
	struct logfs_inode *li = logfs_inode(inode);
	__be64 *rblock;
	u64 increment, bofs = li->li_data[INDIRECT_INDEX];
	level_t level;
	int ret, slot;
	struct page *page;

	BUG_ON(!bofs);

	for (level = LEVEL(li->li_height); level != 0; level = SUBLEVEL(level)) {
		increment = 1 << (LOGFS_BLOCK_BITS * ((__force u8)level-1));
		page = logfs_get_read_page(inode, bix, level);
		if (!page)
			return bix;

		ret = logfs_segment_read(inode, page, bofs, bix, level);
		if (ret) {
			logfs_put_read_page(page);
			return bix;
		}

		slot = get_bits(bix, SUBLEVEL(level));
		rblock = kmap_atomic(page, KM_USER0);
		while (slot < LOGFS_BLOCK_FACTOR) {
			if (data && (rblock[slot] != 0))
				break;
			if (!data && !(be64_to_cpu(rblock[slot]) & LOGFS_FULLY_POPULATED))
				break;
			slot++;
			bix += increment;
			bix &= ~(increment - 1);
		}
		if (slot >= LOGFS_BLOCK_FACTOR) {
			kunmap_atomic(rblock, KM_USER0);
			logfs_put_read_page(page);
			return bix;
		}
		bofs = be64_to_cpu(rblock[slot]);
		kunmap_atomic(rblock, KM_USER0);
		logfs_put_read_page(page);
		if (!bofs) {
			BUG_ON(data);
			return bix;
		}
	}
	return bix;
}

/**
 * logfs_seek_hole - find next hole starting at a given block index
 * @inode:		inode to search in
 * @bix:		block index to start searching
 *
 * Returns next hole.  If the file doesn't contain any further holes, the
 * block address next to eof is returned instead.
 */
u64 logfs_seek_hole(struct inode *inode, u64 bix)
{
	struct logfs_inode *li = logfs_inode(inode);

	if (bix < I0_BLOCKS) {
		bix = seek_holedata_direct(inode, bix, 0);
		if (bix < I0_BLOCKS)
			return bix;
	}

	if (!li->li_data[INDIRECT_INDEX])
		return bix;
	else if (li->li_data[INDIRECT_INDEX] & LOGFS_FULLY_POPULATED)
		bix = maxbix(li->li_height);
	else if (bix >= maxbix(li->li_height))
		return bix;
	else {
		bix = seek_holedata_loop(inode, bix, 0);
		if (bix < maxbix(li->li_height))
			return bix;
		/* Should not happen anymore.  But if some port writes semi-
		 * corrupt images (as this one used to) we might run into it.
		 */
		WARN_ON_ONCE(bix == maxbix(li->li_height));
	}

	return bix;
}

static u64 __logfs_seek_data(struct inode *inode, u64 bix)
{
	struct logfs_inode *li = logfs_inode(inode);

	if (bix < I0_BLOCKS) {
		bix = seek_holedata_direct(inode, bix, 1);
		if (bix < I0_BLOCKS)
			return bix;
	}

	if (bix < maxbix(li->li_height)) {
		if (!li->li_data[INDIRECT_INDEX])
			bix = maxbix(li->li_height);
		else
			return seek_holedata_loop(inode, bix, 1);
	}

	return bix;
}

/**
 * logfs_seek_data - find next data block after a given block index
 * @inode:		inode to search in
 * @bix:		block index to start searching
 *
 * Returns next data block.  If the file doesn't contain any further data
 * blocks, the last block in the file is returned instead.
 */
u64 logfs_seek_data(struct inode *inode, u64 bix)
{
	struct super_block *sb = inode->i_sb;
	u64 ret, end;

	ret = __logfs_seek_data(inode, bix);
	end = i_size_read(inode) >> sb->s_blocksize_bits;
	if (ret >= end)
		ret = max(bix, end);
	return ret;
}

static int logfs_is_valid_direct(struct logfs_inode *li, u64 bix, u64 ofs)
{
	return pure_ofs(li->li_data[bix]) == ofs;
}

static int __logfs_is_valid_loop(struct inode *inode, u64 bix,
		u64 ofs, u64 bofs)
{
	struct logfs_inode *li = logfs_inode(inode);
	level_t level;
	int ret;
	struct page *page;

	for (level = LEVEL(li->li_height); level != 0; level = SUBLEVEL(level)){
		page = logfs_get_write_page(inode, bix, level);
		BUG_ON(!page);

		ret = logfs_segment_read(inode, page, bofs, bix, level);
		if (ret) {
			logfs_put_write_page(page);
			return 0;
		}

		bofs = block_get_pointer(page, get_bits(bix, SUBLEVEL(level)));
		logfs_put_write_page(page);
		if (!bofs)
			return 0;

		if (pure_ofs(bofs) == ofs)
			return 1;
	}
	return 0;
}

static int logfs_is_valid_loop(struct inode *inode, u64 bix, u64 ofs)
{
	struct logfs_inode *li = logfs_inode(inode);
	u64 bofs = li->li_data[INDIRECT_INDEX];

	if (!bofs)
		return 0;

	if (bix >= maxbix(li->li_height))
		return 0;

	if (pure_ofs(bofs) == ofs)
		return 1;

	return __logfs_is_valid_loop(inode, bix, ofs, bofs);
}

static int __logfs_is_valid_block(struct inode *inode, u64 bix, u64 ofs)
{
	struct logfs_inode *li = logfs_inode(inode);

	if ((inode->i_nlink == 0) && atomic_read(&inode->i_count) == 1)
		return 0;

	if (bix < I0_BLOCKS)
		return logfs_is_valid_direct(li, bix, ofs);
	return logfs_is_valid_loop(inode, bix, ofs);
}

/**
 * logfs_is_valid_block - check whether this block is still valid
 *
 * @sb	- superblock
 * @ofs	- block physical offset
 * @ino	- block inode number
 * @bix	- block index
 * @level - block level
 *
 * Returns 0 if the block is invalid, 1 if it is valid and 2 if it will
 * become invalid once the journal is written.
 */
int logfs_is_valid_block(struct super_block *sb, u64 ofs, u64 ino, u64 bix,
		gc_level_t gc_level)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode;
	int ret, cookie;

	/* Umount closes a segment with free blocks remaining.  Those
	 * blocks are by definition invalid. */
	if (ino == -1)
		return 0;

	LOGFS_BUG_ON((u64)(u_long)ino != ino, sb);

	inode = logfs_safe_iget(sb, ino, &cookie);
	if (IS_ERR(inode))
		goto invalid;

	ret = __logfs_is_valid_block(inode, bix, ofs);
	logfs_safe_iput(inode, cookie);
	if (ret)
		return ret;

invalid:
	/* Block is nominally invalid, but may still sit in the shadow tree,
	 * waiting for a journal commit.
	 */
	if (btree_lookup64(&super->s_shadow_tree.old, ofs))
		return 2;
	return 0;
}

int logfs_readpage_nolock(struct page *page)
{
	struct inode *inode = page->mapping->host;
	int ret = -EIO;

	ret = logfs_read_block(inode, page, READ);

	if (ret) {
		ClearPageUptodate(page);
		SetPageError(page);
	} else {
		SetPageUptodate(page);
		ClearPageError(page);
	}
	flush_dcache_page(page);

	return ret;
}

static int logfs_reserve_bytes(struct inode *inode, int bytes)
{
	struct logfs_super *super = logfs_super(inode->i_sb);
	u64 available = super->s_free_bytes + super->s_dirty_free_bytes
			- super->s_dirty_used_bytes - super->s_dirty_pages;

	if (!bytes)
		return 0;

	if (available < bytes)
		return -ENOSPC;

	if (available < bytes + super->s_root_reserve &&
			!capable(CAP_SYS_RESOURCE))
		return -ENOSPC;

	return 0;
}

int get_page_reserve(struct inode *inode, struct page *page)
{
	struct logfs_super *super = logfs_super(inode->i_sb);
	struct logfs_block *block = logfs_block(page);
	int ret;

	if (block && block->reserved_bytes)
		return 0;

	logfs_get_wblocks(inode->i_sb, page, WF_LOCK);
	while ((ret = logfs_reserve_bytes(inode, 6 * LOGFS_MAX_OBJECTSIZE)) &&
			!list_empty(&super->s_writeback_list)) {
		block = list_entry(super->s_writeback_list.next,
				struct logfs_block, alias_list);
		block->ops->write_block(block);
	}
	if (!ret) {
		alloc_data_block(inode, page);
		block = logfs_block(page);
		block->reserved_bytes += 6 * LOGFS_MAX_OBJECTSIZE;
		super->s_dirty_pages += 6 * LOGFS_MAX_OBJECTSIZE;
		list_move_tail(&block->alias_list, &super->s_writeback_list);
	}
	logfs_put_wblocks(inode->i_sb, page, WF_LOCK);
	return ret;
}

/*
 * We are protected by write lock.  Push victims up to superblock level
 * and release transaction when appropriate.
 */
/* FIXME: This is currently called from the wrong spots. */
static void logfs_handle_transaction(struct inode *inode,
		struct logfs_transaction *ta)
{
	struct logfs_super *super = logfs_super(inode->i_sb);

	if (!ta)
		return;
	logfs_inode(inode)->li_block->ta = NULL;

	if (inode->i_ino != LOGFS_INO_MASTER) {
		BUG(); /* FIXME: Yes, this needs more thought */
		/* just remember the transaction until inode is written */
		//BUG_ON(logfs_inode(inode)->li_transaction);
		//logfs_inode(inode)->li_transaction = ta;
		return;
	}

	switch (ta->state) {
	case CREATE_1: /* fall through */
	case UNLINK_1:
		BUG_ON(super->s_victim_ino);
		super->s_victim_ino = ta->ino;
		break;
	case CREATE_2: /* fall through */
	case UNLINK_2:
		BUG_ON(super->s_victim_ino != ta->ino);
		super->s_victim_ino = 0;
		/* transaction ends here - free it */
		kfree(ta);
		break;
	case CROSS_RENAME_1:
		BUG_ON(super->s_rename_dir);
		BUG_ON(super->s_rename_pos);
		super->s_rename_dir = ta->dir;
		super->s_rename_pos = ta->pos;
		break;
	case CROSS_RENAME_2:
		BUG_ON(super->s_rename_dir != ta->dir);
		BUG_ON(super->s_rename_pos != ta->pos);
		super->s_rename_dir = 0;
		super->s_rename_pos = 0;
		kfree(ta);
		break;
	case TARGET_RENAME_1:
		BUG_ON(super->s_rename_dir);
		BUG_ON(super->s_rename_pos);
		BUG_ON(super->s_victim_ino);
		super->s_rename_dir = ta->dir;
		super->s_rename_pos = ta->pos;
		super->s_victim_ino = ta->ino;
		break;
	case TARGET_RENAME_2:
		BUG_ON(super->s_rename_dir != ta->dir);
		BUG_ON(super->s_rename_pos != ta->pos);
		BUG_ON(super->s_victim_ino != ta->ino);
		super->s_rename_dir = 0;
		super->s_rename_pos = 0;
		break;
	case TARGET_RENAME_3:
		BUG_ON(super->s_rename_dir);
		BUG_ON(super->s_rename_pos);
		BUG_ON(super->s_victim_ino != ta->ino);
		super->s_victim_ino = 0;
		kfree(ta);
		break;
	default:
		BUG();
	}
}

/*
 * Not strictly a reservation, but rather a check that we still have enough
 * space to satisfy the write.
 */
static int logfs_reserve_blocks(struct inode *inode, int blocks)
{
	return logfs_reserve_bytes(inode, blocks * LOGFS_MAX_OBJECTSIZE);
}

struct write_control {
	u64 ofs;
	long flags;
};

static struct logfs_shadow *alloc_shadow(struct inode *inode, u64 bix,
		level_t level, u64 old_ofs)
{
	struct logfs_super *super = logfs_super(inode->i_sb);
	struct logfs_shadow *shadow;

	shadow = mempool_alloc(super->s_shadow_pool, GFP_NOFS);
	memset(shadow, 0, sizeof(*shadow));
	shadow->ino = inode->i_ino;
	shadow->bix = bix;
	shadow->gc_level = expand_level(inode->i_ino, level);
	shadow->old_ofs = old_ofs & ~LOGFS_FULLY_POPULATED;
	return shadow;
}

static void free_shadow(struct inode *inode, struct logfs_shadow *shadow)
{
	struct logfs_super *super = logfs_super(inode->i_sb);

	mempool_free(shadow, super->s_shadow_pool);
}

static void mark_segment(struct shadow_tree *tree, u32 segno)
{
	int err;

	if (!btree_lookup32(&tree->segment_map, segno)) {
		err = btree_insert32(&tree->segment_map, segno, (void *)1,
				GFP_NOFS);
		BUG_ON(err);
		tree->no_shadowed_segments++;
	}
}

/**
 * fill_shadow_tree - Propagate shadow tree changes due to a write
 * @inode:	Inode owning the page
 * @page:	Struct page that was written
 * @shadow:	Shadow for the current write
 *
 * Writes in logfs can result in two semi-valid objects.  The old object
 * is still valid as long as it can be reached by following pointers on
 * the medium.  Only when writes propagate all the way up to the journal
 * has the new object safely replaced the old one.
 *
 * To handle this problem, a struct logfs_shadow is used to represent
 * every single write.  It is attached to the indirect block, which is
 * marked dirty.  When the indirect block is written, its shadows are
 * handed up to the next indirect block (or inode).  Untimately they
 * will reach the master inode and be freed upon journal commit.
 *
 * This function handles a single step in the propagation.  It adds the
 * shadow for the current write to the tree, along with any shadows in
 * the page's tree, in case it was an indirect block.  If a page is
 * written, the inode parameter is left NULL, if an inode is written,
 * the page parameter is left NULL.
 */
static void fill_shadow_tree(struct inode *inode, struct page *page,
		struct logfs_shadow *shadow)
{
	struct logfs_super *super = logfs_super(inode->i_sb);
	struct logfs_block *block = logfs_block(page);
	struct shadow_tree *tree = &super->s_shadow_tree;

	if (PagePrivate(page)) {
		if (block->alias_map)
			super->s_no_object_aliases -= bitmap_weight(
					block->alias_map, LOGFS_BLOCK_FACTOR);
		logfs_handle_transaction(inode, block->ta);
		block->ops->free_block(inode->i_sb, block);
	}
	if (shadow) {
		if (shadow->old_ofs)
			btree_insert64(&tree->old, shadow->old_ofs, shadow,
					GFP_NOFS);
		else
			btree_insert64(&tree->new, shadow->new_ofs, shadow,
					GFP_NOFS);

		super->s_dirty_used_bytes += shadow->new_len;
		super->s_dirty_free_bytes += shadow->old_len;
		mark_segment(tree, shadow->old_ofs >> super->s_segshift);
		mark_segment(tree, shadow->new_ofs >> super->s_segshift);
	}
}

static void logfs_set_alias(struct super_block *sb, struct logfs_block *block,
		long child_no)
{
	struct logfs_super *super = logfs_super(sb);

	if (block->inode && block->inode->i_ino == LOGFS_INO_MASTER) {
		/* Aliases in the master inode are pointless. */
		return;
	}

	if (!test_bit(child_no, block->alias_map)) {
		set_bit(child_no, block->alias_map);
		super->s_no_object_aliases++;
	}
	list_move_tail(&block->alias_list, &super->s_object_alias);
}

/*
 * Object aliases can and often do change the size and occupied space of a
 * file.  So not only do we have to change the pointers, we also have to
 * change inode->i_size and li->li_used_bytes.  Which is done by setting
 * another two object aliases for the inode itself.
 */
static void set_iused(struct inode *inode, struct logfs_shadow *shadow)
{
	struct logfs_inode *li = logfs_inode(inode);

	if (shadow->new_len == shadow->old_len)
		return;

	alloc_inode_block(inode);
	li->li_used_bytes += shadow->new_len - shadow->old_len;
	__logfs_set_blocks(inode);
	logfs_set_alias(inode->i_sb, li->li_block, INODE_USED_OFS);
	logfs_set_alias(inode->i_sb, li->li_block, INODE_SIZE_OFS);
}

static int logfs_write_i0(struct inode *inode, struct page *page,
		struct write_control *wc)
{
	struct logfs_shadow *shadow;
	u64 bix;
	level_t level;
	int full, err = 0;

	logfs_unpack_index(page->index, &bix, &level);
	if (wc->ofs == 0)
		if (logfs_reserve_blocks(inode, 1))
			return -ENOSPC;

	shadow = alloc_shadow(inode, bix, level, wc->ofs);
	if (wc->flags & WF_WRITE)
		err = logfs_segment_write(inode, page, shadow);
	if (wc->flags & WF_DELETE)
		logfs_segment_delete(inode, shadow);
	if (err) {
		free_shadow(inode, shadow);
		return err;
	}

	set_iused(inode, shadow);
	full = 1;
	if (level != 0) {
		alloc_indirect_block(inode, page, 0);
		full = logfs_block(page)->full == LOGFS_BLOCK_FACTOR;
	}
	fill_shadow_tree(inode, page, shadow);
	wc->ofs = shadow->new_ofs;
	if (wc->ofs && full)
		wc->ofs |= LOGFS_FULLY_POPULATED;
	return 0;
}

static int logfs_write_direct(struct inode *inode, struct page *page,
		long flags)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct write_control wc = {
		.ofs = li->li_data[page->index],
		.flags = flags,
	};
	int err;

	alloc_inode_block(inode);

	err = logfs_write_i0(inode, page, &wc);
	if (err)
		return err;

	li->li_data[page->index] = wc.ofs;
	logfs_set_alias(inode->i_sb, li->li_block,
			page->index + INODE_POINTER_OFS);
	return 0;
}

static int ptr_change(u64 ofs, struct page *page)
{
	struct logfs_block *block = logfs_block(page);
	int empty0, empty1, full0, full1;

	empty0 = ofs == 0;
	empty1 = block->partial == 0;
	if (empty0 != empty1)
		return 1;

	/* The !! is necessary to shrink result to int */
	full0 = !!(ofs & LOGFS_FULLY_POPULATED);
	full1 = block->full == LOGFS_BLOCK_FACTOR;
	if (full0 != full1)
		return 1;
	return 0;
}

static int __logfs_write_rec(struct inode *inode, struct page *page,
		struct write_control *this_wc,
		pgoff_t bix, level_t target_level, level_t level)
{
	int ret, page_empty = 0;
	int child_no = get_bits(bix, SUBLEVEL(level));
	struct page *ipage;
	struct write_control child_wc = {
		.flags = this_wc->flags,
	};

	ipage = logfs_get_write_page(inode, bix, level);
	if (!ipage)
		return -ENOMEM;

	if (this_wc->ofs) {
		ret = logfs_segment_read(inode, ipage, this_wc->ofs, bix, level);
		if (ret)
			goto out;
	} else if (!PageUptodate(ipage)) {
		page_empty = 1;
		logfs_read_empty(ipage);
	}

	child_wc.ofs = block_get_pointer(ipage, child_no);

	if ((__force u8)level-1 > (__force u8)target_level)
		ret = __logfs_write_rec(inode, page, &child_wc, bix,
				target_level, SUBLEVEL(level));
	else
		ret = logfs_write_i0(inode, page, &child_wc);

	if (ret)
		goto out;

	alloc_indirect_block(inode, ipage, page_empty);
	block_set_pointer(ipage, child_no, child_wc.ofs);
	/* FIXME: first condition seems superfluous */
	if (child_wc.ofs || logfs_block(ipage)->partial)
		this_wc->flags |= WF_WRITE;
	/* the condition on this_wc->ofs ensures that we won't consume extra
	 * space for indirect blocks in the future, which we cannot reserve */
	if (!this_wc->ofs || ptr_change(this_wc->ofs, ipage))
		ret = logfs_write_i0(inode, ipage, this_wc);
	else
		logfs_set_alias(inode->i_sb, logfs_block(ipage), child_no);
out:
	logfs_put_write_page(ipage);
	return ret;
}

static int logfs_write_rec(struct inode *inode, struct page *page,
		pgoff_t bix, level_t target_level, long flags)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct write_control wc = {
		.ofs = li->li_data[INDIRECT_INDEX],
		.flags = flags,
	};
	int ret;

	alloc_inode_block(inode);

	if (li->li_height > (__force u8)target_level)
		ret = __logfs_write_rec(inode, page, &wc, bix, target_level,
				LEVEL(li->li_height));
	else
		ret = logfs_write_i0(inode, page, &wc);
	if (ret)
		return ret;

	if (li->li_data[INDIRECT_INDEX] != wc.ofs) {
		li->li_data[INDIRECT_INDEX] = wc.ofs;
		logfs_set_alias(inode->i_sb, li->li_block,
				INDIRECT_INDEX + INODE_POINTER_OFS);
	}
	return ret;
}

void logfs_add_transaction(struct inode *inode, struct logfs_transaction *ta)
{
	alloc_inode_block(inode);
	logfs_inode(inode)->li_block->ta = ta;
}

void logfs_del_transaction(struct inode *inode, struct logfs_transaction *ta)
{
	struct logfs_block *block = logfs_inode(inode)->li_block;

	if (block && block->ta)
		block->ta = NULL;
}

static int grow_inode(struct inode *inode, u64 bix, level_t level)
{
	struct logfs_inode *li = logfs_inode(inode);
	u8 height = (__force u8)level;
	struct page *page;
	struct write_control wc = {
		.flags = WF_WRITE,
	};
	int err;

	BUG_ON(height > 5 || li->li_height > 5);
	while (height > li->li_height || bix >= maxbix(li->li_height)) {
		page = logfs_get_write_page(inode, I0_BLOCKS + 1,
				LEVEL(li->li_height + 1));
		if (!page)
			return -ENOMEM;
		logfs_read_empty(page);
		alloc_indirect_block(inode, page, 1);
		block_set_pointer(page, 0, li->li_data[INDIRECT_INDEX]);
		err = logfs_write_i0(inode, page, &wc);
		logfs_put_write_page(page);
		if (err)
			return err;
		li->li_data[INDIRECT_INDEX] = wc.ofs;
		wc.ofs = 0;
		li->li_height++;
		logfs_set_alias(inode->i_sb, li->li_block, INODE_HEIGHT_OFS);
	}
	return 0;
}

static int __logfs_write_buf(struct inode *inode, struct page *page, long flags)
{
	struct logfs_super *super = logfs_super(inode->i_sb);
	pgoff_t index = page->index;
	u64 bix;
	level_t level;
	int err;

	flags |= WF_WRITE | WF_DELETE;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	logfs_unpack_index(index, &bix, &level);
	if (logfs_block(page) && logfs_block(page)->reserved_bytes)
		super->s_dirty_pages -= logfs_block(page)->reserved_bytes;

	if (index < I0_BLOCKS)
		return logfs_write_direct(inode, page, flags);

	bix = adjust_bix(bix, level);
	err = grow_inode(inode, bix, level);
	if (err)
		return err;
	return logfs_write_rec(inode, page, bix, level, flags);
}

int logfs_write_buf(struct inode *inode, struct page *page, long flags)
{
	struct super_block *sb = inode->i_sb;
	int ret;

	logfs_get_wblocks(sb, page, flags & WF_LOCK);
	ret = __logfs_write_buf(inode, page, flags);
	logfs_put_wblocks(sb, page, flags & WF_LOCK);
	return ret;
}

static int __logfs_delete(struct inode *inode, struct page *page)
{
	long flags = WF_DELETE;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	if (page->index < I0_BLOCKS)
		return logfs_write_direct(inode, page, flags);
	return logfs_write_rec(inode, page, page->index, 0, flags);
}

int logfs_delete(struct inode *inode, pgoff_t index,
		struct shadow_tree *shadow_tree)
{
	struct super_block *sb = inode->i_sb;
	struct page *page;
	int ret;

	page = logfs_get_read_page(inode, index, 0);
	if (!page)
		return -ENOMEM;

	logfs_get_wblocks(sb, page, 1);
	ret = __logfs_delete(inode, page);
	logfs_put_wblocks(sb, page, 1);

	logfs_put_read_page(page);

	return ret;
}

int logfs_rewrite_block(struct inode *inode, u64 bix, u64 ofs,
		gc_level_t gc_level, long flags)
{
	level_t level = shrink_level(gc_level);
	struct page *page;
	int err;

	page = logfs_get_write_page(inode, bix, level);
	if (!page)
		return -ENOMEM;

	err = logfs_segment_read(inode, page, ofs, bix, level);
	if (!err) {
		if (level != 0)
			alloc_indirect_block(inode, page, 0);
		err = logfs_write_buf(inode, page, flags);
		if (!err && shrink_level(gc_level) == 0) {
			/* Rewrite cannot mark the inode dirty but has to
			 * write it immediately.
			 * Q: Can't we just create an alias for the inode
			 * instead?  And if not, why not?
			 */
			if (inode->i_ino == LOGFS_INO_MASTER)
				logfs_write_anchor(inode->i_sb);
			else {
				err = __logfs_write_inode(inode, flags);
			}
		}
	}
	logfs_put_write_page(page);
	return err;
}

static int truncate_data_block(struct inode *inode, struct page *page,
		u64 ofs, struct logfs_shadow *shadow, u64 size)
{
	loff_t pageofs = page->index << inode->i_sb->s_blocksize_bits;
	u64 bix;
	level_t level;
	int err;

	/* Does truncation happen within this page? */
	if (size <= pageofs || size - pageofs >= PAGE_SIZE)
		return 0;

	logfs_unpack_index(page->index, &bix, &level);
	BUG_ON(level != 0);

	err = logfs_segment_read(inode, page, ofs, bix, level);
	if (err)
		return err;

	zero_user_segment(page, size - pageofs, PAGE_CACHE_SIZE);
	return logfs_segment_write(inode, page, shadow);
}

static int logfs_truncate_i0(struct inode *inode, struct page *page,
		struct write_control *wc, u64 size)
{
	struct logfs_shadow *shadow;
	u64 bix;
	level_t level;
	int err = 0;

	logfs_unpack_index(page->index, &bix, &level);
	BUG_ON(level != 0);
	shadow = alloc_shadow(inode, bix, level, wc->ofs);

	err = truncate_data_block(inode, page, wc->ofs, shadow, size);
	if (err) {
		free_shadow(inode, shadow);
		return err;
	}

	logfs_segment_delete(inode, shadow);
	set_iused(inode, shadow);
	fill_shadow_tree(inode, page, shadow);
	wc->ofs = shadow->new_ofs;
	return 0;
}

static int logfs_truncate_direct(struct inode *inode, u64 size)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct write_control wc;
	struct page *page;
	int e;
	int err;

	alloc_inode_block(inode);

	for (e = I0_BLOCKS - 1; e >= 0; e--) {
		if (size > (e+1) * LOGFS_BLOCKSIZE)
			break;

		wc.ofs = li->li_data[e];
		if (!wc.ofs)
			continue;

		page = logfs_get_write_page(inode, e, 0);
		if (!page)
			return -ENOMEM;
		err = logfs_segment_read(inode, page, wc.ofs, e, 0);
		if (err) {
			logfs_put_write_page(page);
			return err;
		}
		err = logfs_truncate_i0(inode, page, &wc, size);
		logfs_put_write_page(page);
		if (err)
			return err;

		li->li_data[e] = wc.ofs;
	}
	return 0;
}

/* FIXME: these need to become per-sb once we support different blocksizes */
static u64 __logfs_step[] = {
	1,
	I1_BLOCKS,
	I2_BLOCKS,
	I3_BLOCKS,
};

static u64 __logfs_start_index[] = {
	I0_BLOCKS,
	I1_BLOCKS,
	I2_BLOCKS,
	I3_BLOCKS
};

static inline u64 logfs_step(level_t level)
{
	return __logfs_step[(__force u8)level];
}

static inline u64 logfs_factor(u8 level)
{
	return __logfs_step[level] * LOGFS_BLOCKSIZE;
}

static inline u64 logfs_start_index(level_t level)
{
	return __logfs_start_index[(__force u8)level];
}

static void logfs_unpack_raw_index(pgoff_t index, u64 *bix, level_t *level)
{
	logfs_unpack_index(index, bix, level);
	if (*bix <= logfs_start_index(SUBLEVEL(*level)))
		*bix = 0;
}

static int __logfs_truncate_rec(struct inode *inode, struct page *ipage,
		struct write_control *this_wc, u64 size)
{
	int truncate_happened = 0;
	int e, err = 0;
	u64 bix, child_bix, next_bix;
	level_t level;
	struct page *page;
	struct write_control child_wc = { /* FIXME: flags */ };

	logfs_unpack_raw_index(ipage->index, &bix, &level);
	err = logfs_segment_read(inode, ipage, this_wc->ofs, bix, level);
	if (err)
		return err;

	for (e = LOGFS_BLOCK_FACTOR - 1; e >= 0; e--) {
		child_bix = bix + e * logfs_step(SUBLEVEL(level));
		next_bix = child_bix + logfs_step(SUBLEVEL(level));
		if (size > next_bix * LOGFS_BLOCKSIZE)
			break;

		child_wc.ofs = pure_ofs(block_get_pointer(ipage, e));
		if (!child_wc.ofs)
			continue;

		page = logfs_get_write_page(inode, child_bix, SUBLEVEL(level));
		if (!page)
			return -ENOMEM;

		if ((__force u8)level > 1)
			err = __logfs_truncate_rec(inode, page, &child_wc, size);
		else
			err = logfs_truncate_i0(inode, page, &child_wc, size);
		logfs_put_write_page(page);
		if (err)
			return err;

		truncate_happened = 1;
		alloc_indirect_block(inode, ipage, 0);
		block_set_pointer(ipage, e, child_wc.ofs);
	}

	if (!truncate_happened) {
		printk("ineffectual truncate (%lx, %lx, %llx)\n", inode->i_ino, ipage->index, size);
		return 0;
	}

	this_wc->flags = WF_DELETE;
	if (logfs_block(ipage)->partial)
		this_wc->flags |= WF_WRITE;

	return logfs_write_i0(inode, ipage, this_wc);
}

static int logfs_truncate_rec(struct inode *inode, u64 size)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct write_control wc = {
		.ofs = li->li_data[INDIRECT_INDEX],
	};
	struct page *page;
	int err;

	alloc_inode_block(inode);

	if (!wc.ofs)
		return 0;

	page = logfs_get_write_page(inode, 0, LEVEL(li->li_height));
	if (!page)
		return -ENOMEM;

	err = __logfs_truncate_rec(inode, page, &wc, size);
	logfs_put_write_page(page);
	if (err)
		return err;

	if (li->li_data[INDIRECT_INDEX] != wc.ofs)
		li->li_data[INDIRECT_INDEX] = wc.ofs;
	return 0;
}

static int __logfs_truncate(struct inode *inode, u64 size)
{
	int ret;

	if (size >= logfs_factor(logfs_inode(inode)->li_height))
		return 0;

	ret = logfs_truncate_rec(inode, size);
	if (ret)
		return ret;

	return logfs_truncate_direct(inode, size);
}

/*
 * Truncate, by changing the segment file, can consume a fair amount
 * of resources.  So back off from time to time and do some GC.
 * 8 or 2048 blocks should be well within safety limits even if
 * every single block resided in a different segment.
 */
#define TRUNCATE_STEP	(8 * 1024 * 1024)
int logfs_truncate(struct inode *inode, u64 target)
{
	struct super_block *sb = inode->i_sb;
	u64 size = i_size_read(inode);
	int err = 0;

	size = ALIGN(size, TRUNCATE_STEP);
	while (size > target) {
		if (size > TRUNCATE_STEP)
			size -= TRUNCATE_STEP;
		else
			size = 0;
		if (size < target)
			size = target;

		logfs_get_wblocks(sb, NULL, 1);
		err = __logfs_truncate(inode, size);
		if (!err)
			err = __logfs_write_inode(inode, 0);
		logfs_put_wblocks(sb, NULL, 1);
	}

	if (!err)
		err = vmtruncate(inode, target);

	/* I don't trust error recovery yet. */
	WARN_ON(err);
	return err;
}

static void move_page_to_inode(struct inode *inode, struct page *page)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct logfs_block *block = logfs_block(page);

	if (!block)
		return;

	log_blockmove("move_page_to_inode(%llx, %llx, %x)\n",
			block->ino, block->bix, block->level);
	BUG_ON(li->li_block);
	block->ops = &inode_block_ops;
	block->inode = inode;
	li->li_block = block;

	block->page = NULL;
	page->private = 0;
	ClearPagePrivate(page);
}

static void move_inode_to_page(struct page *page, struct inode *inode)
{
	struct logfs_inode *li = logfs_inode(inode);
	struct logfs_block *block = li->li_block;

	if (!block)
		return;

	log_blockmove("move_inode_to_page(%llx, %llx, %x)\n",
			block->ino, block->bix, block->level);
	BUG_ON(PagePrivate(page));
	block->ops = &indirect_block_ops;
	block->page = page;
	page->private = (unsigned long)block;
	SetPagePrivate(page);

	block->inode = NULL;
	li->li_block = NULL;
}

int logfs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_super *super = logfs_super(sb);
	struct inode *master_inode = super->s_master_inode;
	struct page *page;
	struct logfs_disk_inode *di;
	u64 ino = inode->i_ino;

	if (ino << sb->s_blocksize_bits > i_size_read(master_inode))
		return -ENODATA;
	if (!logfs_exist_block(master_inode, ino))
		return -ENODATA;

	page = read_cache_page(master_inode->i_mapping, ino,
			(filler_t *)logfs_readpage, NULL);
	if (IS_ERR(page))
		return PTR_ERR(page);

	di = kmap_atomic(page, KM_USER0);
	logfs_disk_to_inode(di, inode);
	kunmap_atomic(di, KM_USER0);
	move_page_to_inode(inode, page);
	page_cache_release(page);
	return 0;
}

/* Caller must logfs_put_write_page(page); */
static struct page *inode_to_page(struct inode *inode)
{
	struct inode *master_inode = logfs_super(inode->i_sb)->s_master_inode;
	struct logfs_disk_inode *di;
	struct page *page;

	BUG_ON(inode->i_ino == LOGFS_INO_MASTER);

	page = logfs_get_write_page(master_inode, inode->i_ino, 0);
	if (!page)
		return NULL;

	di = kmap_atomic(page, KM_USER0);
	logfs_inode_to_disk(inode, di);
	kunmap_atomic(di, KM_USER0);
	move_inode_to_page(page, inode);
	return page;
}

static int do_write_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct inode *master_inode = logfs_super(sb)->s_master_inode;
	loff_t size = (inode->i_ino + 1) << inode->i_sb->s_blocksize_bits;
	struct page *page;
	int err;

	BUG_ON(inode->i_ino == LOGFS_INO_MASTER);
	/* FIXME: lock inode */

	if (i_size_read(master_inode) < size)
		i_size_write(master_inode, size);

	/* TODO: Tell vfs this inode is clean now */

	page = inode_to_page(inode);
	if (!page)
		return -ENOMEM;

	/* FIXME: transaction is part of logfs_block now.  Is that enough? */
	err = logfs_write_buf(master_inode, page, 0);
	if (err)
		move_page_to_inode(inode, page);

	logfs_put_write_page(page);
	return err;
}

static void logfs_mod_segment_entry(struct super_block *sb, u32 segno,
		int write,
		void (*change_se)(struct logfs_segment_entry *, long),
		long arg)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode;
	struct page *page;
	struct logfs_segment_entry *se;
	pgoff_t page_no;
	int child_no;

	page_no = segno >> (sb->s_blocksize_bits - 3);
	child_no = segno & ((sb->s_blocksize >> 3) - 1);

	inode = super->s_segfile_inode;
	page = logfs_get_write_page(inode, page_no, 0);
	BUG_ON(!page); /* FIXME: We need some reserve page for this case */
	if (!PageUptodate(page))
		logfs_read_block(inode, page, WRITE);

	if (write)
		alloc_indirect_block(inode, page, 0);
	se = kmap_atomic(page, KM_USER0);
	change_se(se + child_no, arg);
	if (write) {
		logfs_set_alias(sb, logfs_block(page), child_no);
		BUG_ON((int)be32_to_cpu(se[child_no].valid) > super->s_segsize);
	}
	kunmap_atomic(se, KM_USER0);

	logfs_put_write_page(page);
}

static void __get_segment_entry(struct logfs_segment_entry *se, long _target)
{
	struct logfs_segment_entry *target = (void *)_target;

	*target = *se;
}

void logfs_get_segment_entry(struct super_block *sb, u32 segno,
		struct logfs_segment_entry *se)
{
	logfs_mod_segment_entry(sb, segno, 0, __get_segment_entry, (long)se);
}

static void __set_segment_used(struct logfs_segment_entry *se, long increment)
{
	u32 valid;

	valid = be32_to_cpu(se->valid);
	valid += increment;
	se->valid = cpu_to_be32(valid);
}

void logfs_set_segment_used(struct super_block *sb, u64 ofs, int increment)
{
	struct logfs_super *super = logfs_super(sb);
	u32 segno = ofs >> super->s_segshift;

	if (!increment)
		return;

	logfs_mod_segment_entry(sb, segno, 1, __set_segment_used, increment);
}

static void __set_segment_erased(struct logfs_segment_entry *se, long ec_level)
{
	se->ec_level = cpu_to_be32(ec_level);
}

void logfs_set_segment_erased(struct super_block *sb, u32 segno, u32 ec,
		gc_level_t gc_level)
{
	u32 ec_level = ec << 4 | (__force u8)gc_level;

	logfs_mod_segment_entry(sb, segno, 1, __set_segment_erased, ec_level);
}

static void __set_segment_reserved(struct logfs_segment_entry *se, long ignore)
{
	se->valid = cpu_to_be32(RESERVED);
}

void logfs_set_segment_reserved(struct super_block *sb, u32 segno)
{
	logfs_mod_segment_entry(sb, segno, 1, __set_segment_reserved, 0);
}

static void __set_segment_unreserved(struct logfs_segment_entry *se,
		long ec_level)
{
	se->valid = 0;
	se->ec_level = cpu_to_be32(ec_level);
}

void logfs_set_segment_unreserved(struct super_block *sb, u32 segno, u32 ec)
{
	u32 ec_level = ec << 4;

	logfs_mod_segment_entry(sb, segno, 1, __set_segment_unreserved,
			ec_level);
}

int __logfs_write_inode(struct inode *inode, long flags)
{
	struct super_block *sb = inode->i_sb;
	int ret;

	logfs_get_wblocks(sb, NULL, flags & WF_LOCK);
	ret = do_write_inode(inode);
	logfs_put_wblocks(sb, NULL, flags & WF_LOCK);
	return ret;
}

static int do_delete_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct inode *master_inode = logfs_super(sb)->s_master_inode;
	struct page *page;
	int ret;

	page = logfs_get_write_page(master_inode, inode->i_ino, 0);
	if (!page)
		return -ENOMEM;

	move_inode_to_page(page, inode);

	logfs_get_wblocks(sb, page, 1);
	ret = __logfs_delete(master_inode, page);
	logfs_put_wblocks(sb, page, 1);

	logfs_put_write_page(page);
	return ret;
}

/*
 * ZOMBIE inodes have already been deleted before and should remain dead,
 * if it weren't for valid checking.  No need to kill them again here.
 */
void logfs_evict_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_inode *li = logfs_inode(inode);
	struct logfs_block *block = li->li_block;
	struct page *page;

	if (!inode->i_nlink) {
		if (!(li->li_flags & LOGFS_IF_ZOMBIE)) {
			li->li_flags |= LOGFS_IF_ZOMBIE;
			if (i_size_read(inode) > 0)
				logfs_truncate(inode, 0);
			do_delete_inode(inode);
		}
	}
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);

	/* Cheaper version of write_inode.  All changes are concealed in
	 * aliases, which are moved back.  No write to the medium happens.
	 */
	/* Only deleted files may be dirty at this point */
	BUG_ON(inode->i_state & I_DIRTY && inode->i_nlink);
	if (!block)
		return;
	if ((logfs_super(sb)->s_flags & LOGFS_SB_FLAG_SHUTDOWN)) {
		block->ops->free_block(inode->i_sb, block);
		return;
	}

	BUG_ON(inode->i_ino < LOGFS_RESERVED_INOS);
	page = inode_to_page(inode);
	BUG_ON(!page); /* FIXME: Use emergency page */
	logfs_put_write_page(page);
}

void btree_write_block(struct logfs_block *block)
{
	struct inode *inode;
	struct page *page;
	int err, cookie;

	inode = logfs_safe_iget(block->sb, block->ino, &cookie);
	page = logfs_get_write_page(inode, block->bix, block->level);

	err = logfs_readpage_nolock(page);
	BUG_ON(err);
	BUG_ON(!PagePrivate(page));
	BUG_ON(logfs_block(page) != block);
	err = __logfs_write_buf(inode, page, 0);
	BUG_ON(err);
	BUG_ON(PagePrivate(page) || page->private);

	logfs_put_write_page(page);
	logfs_safe_iput(inode, cookie);
}

/**
 * logfs_inode_write - write inode or dentry objects
 *
 * @inode:		parent inode (ifile or directory)
 * @buf:		object to write (inode or dentry)
 * @n:			object size
 * @_pos:		object number (file position in blocks/objects)
 * @flags:		write flags
 * @lock:		0 if write lock is already taken, 1 otherwise
 * @shadow_tree:	shadow below this inode
 *
 * FIXME: All caller of this put a 200-300 byte variable on the stack,
 * only to call here and do a memcpy from that stack variable.  A good
 * example of wasted performance and stack space.
 */
int logfs_inode_write(struct inode *inode, const void *buf, size_t count,
		loff_t bix, long flags, struct shadow_tree *shadow_tree)
{
	loff_t pos = bix << inode->i_sb->s_blocksize_bits;
	int err;
	struct page *page;
	void *pagebuf;

	BUG_ON(pos & (LOGFS_BLOCKSIZE-1));
	BUG_ON(count > LOGFS_BLOCKSIZE);
	page = logfs_get_write_page(inode, bix, 0);
	if (!page)
		return -ENOMEM;

	pagebuf = kmap_atomic(page, KM_USER0);
	memcpy(pagebuf, buf, count);
	flush_dcache_page(page);
	kunmap_atomic(pagebuf, KM_USER0);

	if (i_size_read(inode) < pos + LOGFS_BLOCKSIZE)
		i_size_write(inode, pos + LOGFS_BLOCKSIZE);

	err = logfs_write_buf(inode, page, flags);
	logfs_put_write_page(page);
	return err;
}

int logfs_open_segfile(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode;

	inode = logfs_read_meta_inode(sb, LOGFS_INO_SEGFILE);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	super->s_segfile_inode = inode;
	return 0;
}

int logfs_init_rw(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int min_fill = 3 * super->s_no_blocks;

	INIT_LIST_HEAD(&super->s_object_alias);
	INIT_LIST_HEAD(&super->s_writeback_list);
	mutex_init(&super->s_write_mutex);
	super->s_block_pool = mempool_create_kmalloc_pool(min_fill,
			sizeof(struct logfs_block));
	super->s_shadow_pool = mempool_create_kmalloc_pool(min_fill,
			sizeof(struct logfs_shadow));
	return 0;
}

void logfs_cleanup_rw(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	logfs_mempool_destroy(super->s_block_pool);
	logfs_mempool_destroy(super->s_shadow_pool);
}
