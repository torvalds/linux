/*
 * fs/logfs/segment.c	- Handling the Object Store
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 *
 * Object store or ostore makes up the complete device with exception of
 * the superblock and journal areas.  Apart from its own metadata it stores
 * three kinds of objects: inodes, dentries and blocks, both data and indirect.
 */
#include "logfs.h"
#include <linux/slab.h>

static int logfs_mark_segment_bad(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct btree_head32 *head = &super->s_reserved_segments;
	int err;

	err = btree_insert32(head, segno, (void *)1, GFP_NOFS);
	if (err)
		return err;
	logfs_super(sb)->s_bad_segments++;
	/* FIXME: write to journal */
	return 0;
}

int logfs_erase_segment(struct super_block *sb, u32 segno, int ensure_erase)
{
	struct logfs_super *super = logfs_super(sb);

	super->s_gec++;

	return super->s_devops->erase(sb, (u64)segno << super->s_segshift,
			super->s_segsize, ensure_erase);
}

static s64 logfs_get_free_bytes(struct logfs_area *area, size_t bytes)
{
	s32 ofs;

	logfs_open_area(area, bytes);

	ofs = area->a_used_bytes;
	area->a_used_bytes += bytes;
	BUG_ON(area->a_used_bytes >= logfs_super(area->a_sb)->s_segsize);

	return dev_ofs(area->a_sb, area->a_segno, ofs);
}

static struct page *get_mapping_page(struct super_block *sb, pgoff_t index,
		int use_filler)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	filler_t *filler = super->s_devops->readpage;
	struct page *page;

	BUG_ON(mapping_gfp_mask(mapping) & __GFP_FS);
	if (use_filler)
		page = read_cache_page(mapping, index, filler, sb);
	else {
		page = find_or_create_page(mapping, index, GFP_NOFS);
		unlock_page(page);
	}
	return page;
}

int __logfs_buf_write(struct logfs_area *area, u64 ofs, void *buf, size_t len,
		int use_filler)
{
	pgoff_t index = ofs >> PAGE_SHIFT;
	struct page *page;
	long offset = ofs & (PAGE_SIZE-1);
	long copylen;

	/* Only logfs_wbuf_recover may use len==0 */
	BUG_ON(!len && !use_filler);
	do {
		copylen = min((ulong)len, PAGE_SIZE - offset);

		page = get_mapping_page(area->a_sb, index, use_filler);
		if (IS_ERR(page))
			return PTR_ERR(page);
		BUG_ON(!page); /* FIXME: reserve a pool */
		SetPageUptodate(page);
		memcpy(page_address(page) + offset, buf, copylen);

		if (!PagePrivate(page)) {
			SetPagePrivate(page);
			page_cache_get(page);
		}
		page_cache_release(page);

		buf += copylen;
		len -= copylen;
		offset = 0;
		index++;
	} while (len);
	return 0;
}

static void pad_partial_page(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	struct page *page;
	u64 ofs = dev_ofs(sb, area->a_segno, area->a_used_bytes);
	pgoff_t index = ofs >> PAGE_SHIFT;
	long offset = ofs & (PAGE_SIZE-1);
	u32 len = PAGE_SIZE - offset;

	if (len % PAGE_SIZE) {
		page = get_mapping_page(sb, index, 0);
		BUG_ON(!page); /* FIXME: reserve a pool */
		memset(page_address(page) + offset, 0xff, len);
		if (!PagePrivate(page)) {
			SetPagePrivate(page);
			page_cache_get(page);
		}
		page_cache_release(page);
	}
}

static void pad_full_pages(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	struct logfs_super *super = logfs_super(sb);
	u64 ofs = dev_ofs(sb, area->a_segno, area->a_used_bytes);
	u32 len = super->s_segsize - area->a_used_bytes;
	pgoff_t index = PAGE_CACHE_ALIGN(ofs) >> PAGE_CACHE_SHIFT;
	pgoff_t no_indizes = len >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (no_indizes) {
		page = get_mapping_page(sb, index, 0);
		BUG_ON(!page); /* FIXME: reserve a pool */
		SetPageUptodate(page);
		memset(page_address(page), 0xff, PAGE_CACHE_SIZE);
		if (!PagePrivate(page)) {
			SetPagePrivate(page);
			page_cache_get(page);
		}
		page_cache_release(page);
		index++;
		no_indizes--;
	}
}

/*
 * bdev_writeseg will write full pages.  Memset the tail to prevent data leaks.
 * Also make sure we allocate (and memset) all pages for final writeout.
 */
static void pad_wbuf(struct logfs_area *area, int final)
{
	pad_partial_page(area);
	if (final)
		pad_full_pages(area);
}

/*
 * We have to be careful with the alias tree.  Since lookup is done by bix,
 * it needs to be normalized, so 14, 15, 16, etc. all match when dealing with
 * indirect blocks.  So always use it through accessor functions.
 */
static void *alias_tree_lookup(struct super_block *sb, u64 ino, u64 bix,
		level_t level)
{
	struct btree_head128 *head = &logfs_super(sb)->s_object_alias_tree;
	pgoff_t index = logfs_pack_index(bix, level);

	return btree_lookup128(head, ino, index);
}

static int alias_tree_insert(struct super_block *sb, u64 ino, u64 bix,
		level_t level, void *val)
{
	struct btree_head128 *head = &logfs_super(sb)->s_object_alias_tree;
	pgoff_t index = logfs_pack_index(bix, level);

	return btree_insert128(head, ino, index, val, GFP_NOFS);
}

static int btree_write_alias(struct super_block *sb, struct logfs_block *block,
		write_alias_t *write_one_alias)
{
	struct object_alias_item *item;
	int err;

	list_for_each_entry(item, &block->item_list, list) {
		err = write_alias_journal(sb, block->ino, block->bix,
				block->level, item->child_no, item->val);
		if (err)
			return err;
	}
	return 0;
}

static struct logfs_block_ops btree_block_ops = {
	.write_block	= btree_write_block,
	.free_block	= __free_block,
	.write_alias	= btree_write_alias,
};

int logfs_load_object_aliases(struct super_block *sb,
		struct logfs_obj_alias *oa, int count)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_block *block;
	struct object_alias_item *item;
	u64 ino, bix;
	level_t level;
	int i, err;

	super->s_flags |= LOGFS_SB_FLAG_OBJ_ALIAS;
	count /= sizeof(*oa);
	for (i = 0; i < count; i++) {
		item = mempool_alloc(super->s_alias_pool, GFP_NOFS);
		if (!item)
			return -ENOMEM;
		memset(item, 0, sizeof(*item));

		super->s_no_object_aliases++;
		item->val = oa[i].val;
		item->child_no = be16_to_cpu(oa[i].child_no);

		ino = be64_to_cpu(oa[i].ino);
		bix = be64_to_cpu(oa[i].bix);
		level = LEVEL(oa[i].level);

		log_aliases("logfs_load_object_aliases(%llx, %llx, %x, %x) %llx\n",
				ino, bix, level, item->child_no,
				be64_to_cpu(item->val));
		block = alias_tree_lookup(sb, ino, bix, level);
		if (!block) {
			block = __alloc_block(sb, ino, bix, level);
			block->ops = &btree_block_ops;
			err = alias_tree_insert(sb, ino, bix, level, block);
			BUG_ON(err); /* mempool empty */
		}
		if (test_and_set_bit(item->child_no, block->alias_map)) {
			printk(KERN_ERR"LogFS: Alias collision detected\n");
			return -EIO;
		}
		list_move_tail(&block->alias_list, &super->s_object_alias);
		list_add(&item->list, &block->item_list);
	}
	return 0;
}

static void kill_alias(void *_block, unsigned long ignore0,
		u64 ignore1, u64 ignore2, size_t ignore3)
{
	struct logfs_block *block = _block;
	struct super_block *sb = block->sb;
	struct logfs_super *super = logfs_super(sb);
	struct object_alias_item *item;

	while (!list_empty(&block->item_list)) {
		item = list_entry(block->item_list.next, typeof(*item), list);
		list_del(&item->list);
		mempool_free(item, super->s_alias_pool);
	}
	block->ops->free_block(sb, block);
}

static int obj_type(struct inode *inode, level_t level)
{
	if (level == 0) {
		if (S_ISDIR(inode->i_mode))
			return OBJ_DENTRY;
		if (inode->i_ino == LOGFS_INO_MASTER)
			return OBJ_INODE;
	}
	return OBJ_BLOCK;
}

static int obj_len(struct super_block *sb, int obj_type)
{
	switch (obj_type) {
	case OBJ_DENTRY:
		return sizeof(struct logfs_disk_dentry);
	case OBJ_INODE:
		return sizeof(struct logfs_disk_inode);
	case OBJ_BLOCK:
		return sb->s_blocksize;
	default:
		BUG();
	}
}

static int __logfs_segment_write(struct inode *inode, void *buf,
		struct logfs_shadow *shadow, int type, int len, int compr)
{
	struct logfs_area *area;
	struct super_block *sb = inode->i_sb;
	s64 ofs;
	struct logfs_object_header h;
	int acc_len;

	if (shadow->gc_level == 0)
		acc_len = len;
	else
		acc_len = obj_len(sb, type);

	area = get_area(sb, shadow->gc_level);
	ofs = logfs_get_free_bytes(area, len + LOGFS_OBJECT_HEADERSIZE);
	LOGFS_BUG_ON(ofs <= 0, sb);
	/*
	 * Order is important.  logfs_get_free_bytes(), by modifying the
	 * segment file, may modify the content of the very page we're about
	 * to write now.  Which is fine, as long as the calculated crc and
	 * written data still match.  So do the modifications _before_
	 * calculating the crc.
	 */

	h.len	= cpu_to_be16(len);
	h.type	= type;
	h.compr	= compr;
	h.ino	= cpu_to_be64(inode->i_ino);
	h.bix	= cpu_to_be64(shadow->bix);
	h.crc	= logfs_crc32(&h, sizeof(h) - 4, 4);
	h.data_crc = logfs_crc32(buf, len, 0);

	logfs_buf_write(area, ofs, &h, sizeof(h));
	logfs_buf_write(area, ofs + LOGFS_OBJECT_HEADERSIZE, buf, len);

	shadow->new_ofs = ofs;
	shadow->new_len = acc_len + LOGFS_OBJECT_HEADERSIZE;

	return 0;
}

static s64 logfs_segment_write_compress(struct inode *inode, void *buf,
		struct logfs_shadow *shadow, int type, int len)
{
	struct super_block *sb = inode->i_sb;
	void *compressor_buf = logfs_super(sb)->s_compressed_je;
	ssize_t compr_len;
	int ret;

	mutex_lock(&logfs_super(sb)->s_journal_mutex);
	compr_len = logfs_compress(buf, compressor_buf, len, len);

	if (compr_len >= 0) {
		ret = __logfs_segment_write(inode, compressor_buf, shadow,
				type, compr_len, COMPR_ZLIB);
	} else {
		ret = __logfs_segment_write(inode, buf, shadow, type, len,
				COMPR_NONE);
	}
	mutex_unlock(&logfs_super(sb)->s_journal_mutex);
	return ret;
}

/**
 * logfs_segment_write - write data block to object store
 * @inode:		inode containing data
 *
 * Returns an errno or zero.
 */
int logfs_segment_write(struct inode *inode, struct page *page,
		struct logfs_shadow *shadow)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_super *super = logfs_super(sb);
	int do_compress, type, len;
	int ret;
	void *buf;

	super->s_flags |= LOGFS_SB_FLAG_DIRTY;
	BUG_ON(super->s_flags & LOGFS_SB_FLAG_SHUTDOWN);
	do_compress = logfs_inode(inode)->li_flags & LOGFS_IF_COMPRESSED;
	if (shadow->gc_level != 0) {
		/* temporarily disable compression for indirect blocks */
		do_compress = 0;
	}

	type = obj_type(inode, shrink_level(shadow->gc_level));
	len = obj_len(sb, type);
	buf = kmap(page);
	if (do_compress)
		ret = logfs_segment_write_compress(inode, buf, shadow, type,
				len);
	else
		ret = __logfs_segment_write(inode, buf, shadow, type, len,
				COMPR_NONE);
	kunmap(page);

	log_segment("logfs_segment_write(%llx, %llx, %x) %llx->%llx %x->%x\n",
			shadow->ino, shadow->bix, shadow->gc_level,
			shadow->old_ofs, shadow->new_ofs,
			shadow->old_len, shadow->new_len);
	/* this BUG_ON did catch a locking bug.  useful */
	BUG_ON(!(shadow->new_ofs & (super->s_segsize - 1)));
	return ret;
}

int wbuf_read(struct super_block *sb, u64 ofs, size_t len, void *buf)
{
	pgoff_t index = ofs >> PAGE_SHIFT;
	struct page *page;
	long offset = ofs & (PAGE_SIZE-1);
	long copylen;

	while (len) {
		copylen = min((ulong)len, PAGE_SIZE - offset);

		page = get_mapping_page(sb, index, 1);
		if (IS_ERR(page))
			return PTR_ERR(page);
		memcpy(buf, page_address(page) + offset, copylen);
		page_cache_release(page);

		buf += copylen;
		len -= copylen;
		offset = 0;
		index++;
	}
	return 0;
}

/*
 * The "position" of indirect blocks is ambiguous.  It can be the position
 * of any data block somewhere behind this indirect block.  So we need to
 * normalize the positions through logfs_block_mask() before comparing.
 */
static int check_pos(struct super_block *sb, u64 pos1, u64 pos2, level_t level)
{
	return	(pos1 & logfs_block_mask(sb, level)) !=
		(pos2 & logfs_block_mask(sb, level));
}

#if 0
static int read_seg_header(struct super_block *sb, u64 ofs,
		struct logfs_segment_header *sh)
{
	__be32 crc;
	int err;

	err = wbuf_read(sb, ofs, sizeof(*sh), sh);
	if (err)
		return err;
	crc = logfs_crc32(sh, sizeof(*sh), 4);
	if (crc != sh->crc) {
		printk(KERN_ERR"LOGFS: header crc error at %llx: expected %x, "
				"got %x\n", ofs, be32_to_cpu(sh->crc),
				be32_to_cpu(crc));
		return -EIO;
	}
	return 0;
}
#endif

static int read_obj_header(struct super_block *sb, u64 ofs,
		struct logfs_object_header *oh)
{
	__be32 crc;
	int err;

	err = wbuf_read(sb, ofs, sizeof(*oh), oh);
	if (err)
		return err;
	crc = logfs_crc32(oh, sizeof(*oh) - 4, 4);
	if (crc != oh->crc) {
		printk(KERN_ERR"LOGFS: header crc error at %llx: expected %x, "
				"got %x\n", ofs, be32_to_cpu(oh->crc),
				be32_to_cpu(crc));
		return -EIO;
	}
	return 0;
}

static void move_btree_to_page(struct inode *inode, struct page *page,
		__be64 *data)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_super *super = logfs_super(sb);
	struct btree_head128 *head = &super->s_object_alias_tree;
	struct logfs_block *block;
	struct object_alias_item *item, *next;

	if (!(super->s_flags & LOGFS_SB_FLAG_OBJ_ALIAS))
		return;

	block = btree_remove128(head, inode->i_ino, page->index);
	if (!block)
		return;

	log_blockmove("move_btree_to_page(%llx, %llx, %x)\n",
			block->ino, block->bix, block->level);
	list_for_each_entry_safe(item, next, &block->item_list, list) {
		data[item->child_no] = item->val;
		list_del(&item->list);
		mempool_free(item, super->s_alias_pool);
	}
	block->page = page;

	if (!PagePrivate(page)) {
		SetPagePrivate(page);
		page_cache_get(page);
		set_page_private(page, (unsigned long) block);
	}
	block->ops = &indirect_block_ops;
	initialize_block_counters(page, block, data, 0);
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

void move_page_to_btree(struct page *page)
{
	struct logfs_block *block = logfs_block(page);
	struct super_block *sb = block->sb;
	struct logfs_super *super = logfs_super(sb);
	struct object_alias_item *item;
	unsigned long pos;
	__be64 *child;
	int err;

	if (super->s_flags & LOGFS_SB_FLAG_SHUTDOWN) {
		block->ops->free_block(sb, block);
		return;
	}
	log_blockmove("move_page_to_btree(%llx, %llx, %x)\n",
			block->ino, block->bix, block->level);
	super->s_flags |= LOGFS_SB_FLAG_OBJ_ALIAS;

	for (pos = 0; ; pos++) {
		pos = fnb(block->alias_map, LOGFS_BLOCK_FACTOR, pos);
		if (pos >= LOGFS_BLOCK_FACTOR)
			break;

		item = mempool_alloc(super->s_alias_pool, GFP_NOFS);
		BUG_ON(!item); /* mempool empty */
		memset(item, 0, sizeof(*item));

		child = kmap_atomic(page);
		item->val = child[pos];
		kunmap_atomic(child);
		item->child_no = pos;
		list_add(&item->list, &block->item_list);
	}
	block->page = NULL;

	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		page_cache_release(page);
		set_page_private(page, 0);
	}
	block->ops = &btree_block_ops;
	err = alias_tree_insert(block->sb, block->ino, block->bix, block->level,
			block);
	BUG_ON(err); /* mempool empty */
	ClearPageUptodate(page);
}

static int __logfs_segment_read(struct inode *inode, void *buf,
		u64 ofs, u64 bix, level_t level)
{
	struct super_block *sb = inode->i_sb;
	void *compressor_buf = logfs_super(sb)->s_compressed_je;
	struct logfs_object_header oh;
	__be32 crc;
	u16 len;
	int err, block_len;

	block_len = obj_len(sb, obj_type(inode, level));
	err = read_obj_header(sb, ofs, &oh);
	if (err)
		goto out_err;

	err = -EIO;
	if (be64_to_cpu(oh.ino) != inode->i_ino
			|| check_pos(sb, be64_to_cpu(oh.bix), bix, level)) {
		printk(KERN_ERR"LOGFS: (ino, bix) don't match at %llx: "
				"expected (%lx, %llx), got (%llx, %llx)\n",
				ofs, inode->i_ino, bix,
				be64_to_cpu(oh.ino), be64_to_cpu(oh.bix));
		goto out_err;
	}

	len = be16_to_cpu(oh.len);

	switch (oh.compr) {
	case COMPR_NONE:
		err = wbuf_read(sb, ofs + LOGFS_OBJECT_HEADERSIZE, len, buf);
		if (err)
			goto out_err;
		crc = logfs_crc32(buf, len, 0);
		if (crc != oh.data_crc) {
			printk(KERN_ERR"LOGFS: uncompressed data crc error at "
					"%llx: expected %x, got %x\n", ofs,
					be32_to_cpu(oh.data_crc),
					be32_to_cpu(crc));
			goto out_err;
		}
		break;
	case COMPR_ZLIB:
		mutex_lock(&logfs_super(sb)->s_journal_mutex);
		err = wbuf_read(sb, ofs + LOGFS_OBJECT_HEADERSIZE, len,
				compressor_buf);
		if (err) {
			mutex_unlock(&logfs_super(sb)->s_journal_mutex);
			goto out_err;
		}
		crc = logfs_crc32(compressor_buf, len, 0);
		if (crc != oh.data_crc) {
			printk(KERN_ERR"LOGFS: compressed data crc error at "
					"%llx: expected %x, got %x\n", ofs,
					be32_to_cpu(oh.data_crc),
					be32_to_cpu(crc));
			mutex_unlock(&logfs_super(sb)->s_journal_mutex);
			goto out_err;
		}
		err = logfs_uncompress(compressor_buf, buf, len, block_len);
		mutex_unlock(&logfs_super(sb)->s_journal_mutex);
		if (err) {
			printk(KERN_ERR"LOGFS: uncompress error at %llx\n", ofs);
			goto out_err;
		}
		break;
	default:
		LOGFS_BUG(sb);
		err = -EIO;
		goto out_err;
	}
	return 0;

out_err:
	logfs_set_ro(sb);
	printk(KERN_ERR"LOGFS: device is read-only now\n");
	LOGFS_BUG(sb);
	return err;
}

/**
 * logfs_segment_read - read data block from object store
 * @inode:		inode containing data
 * @buf:		data buffer
 * @ofs:		physical data offset
 * @bix:		block index
 * @level:		block level
 *
 * Returns 0 on success or a negative errno.
 */
int logfs_segment_read(struct inode *inode, struct page *page,
		u64 ofs, u64 bix, level_t level)
{
	int err;
	void *buf;

	if (PageUptodate(page))
		return 0;

	ofs &= ~LOGFS_FULLY_POPULATED;

	buf = kmap(page);
	err = __logfs_segment_read(inode, buf, ofs, bix, level);
	if (!err) {
		move_btree_to_page(inode, page, buf);
		SetPageUptodate(page);
	}
	kunmap(page);
	log_segment("logfs_segment_read(%lx, %llx, %x) %llx (%d)\n",
			inode->i_ino, bix, level, ofs, err);
	return err;
}

int logfs_segment_delete(struct inode *inode, struct logfs_shadow *shadow)
{
	struct super_block *sb = inode->i_sb;
	struct logfs_super *super = logfs_super(sb);
	struct logfs_object_header h;
	u16 len;
	int err;

	super->s_flags |= LOGFS_SB_FLAG_DIRTY;
	BUG_ON(super->s_flags & LOGFS_SB_FLAG_SHUTDOWN);
	BUG_ON(shadow->old_ofs & LOGFS_FULLY_POPULATED);
	if (!shadow->old_ofs)
		return 0;

	log_segment("logfs_segment_delete(%llx, %llx, %x) %llx->%llx %x->%x\n",
			shadow->ino, shadow->bix, shadow->gc_level,
			shadow->old_ofs, shadow->new_ofs,
			shadow->old_len, shadow->new_len);
	err = read_obj_header(sb, shadow->old_ofs, &h);
	LOGFS_BUG_ON(err, sb);
	LOGFS_BUG_ON(be64_to_cpu(h.ino) != inode->i_ino, sb);
	LOGFS_BUG_ON(check_pos(sb, shadow->bix, be64_to_cpu(h.bix),
				shrink_level(shadow->gc_level)), sb);

	if (shadow->gc_level == 0)
		len = be16_to_cpu(h.len);
	else
		len = obj_len(sb, h.type);
	shadow->old_len = len + sizeof(h);
	return 0;
}

void freeseg(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	struct page *page;
	u64 ofs, start, end;

	start = dev_ofs(sb, segno, 0);
	end = dev_ofs(sb, segno + 1, 0);
	for (ofs = start; ofs < end; ofs += PAGE_SIZE) {
		page = find_get_page(mapping, ofs >> PAGE_SHIFT);
		if (!page)
			continue;
		if (PagePrivate(page)) {
			ClearPagePrivate(page);
			page_cache_release(page);
		}
		page_cache_release(page);
	}
}

int logfs_open_area(struct logfs_area *area, size_t bytes)
{
	struct super_block *sb = area->a_sb;
	struct logfs_super *super = logfs_super(sb);
	int err, closed = 0;

	if (area->a_is_open && area->a_used_bytes + bytes <= super->s_segsize)
		return 0;

	if (area->a_is_open) {
		u64 ofs = dev_ofs(sb, area->a_segno, area->a_written_bytes);
		u32 len = super->s_segsize - area->a_written_bytes;

		log_gc("logfs_close_area(%x)\n", area->a_segno);
		pad_wbuf(area, 1);
		super->s_devops->writeseg(area->a_sb, ofs, len);
		freeseg(sb, area->a_segno);
		closed = 1;
	}

	area->a_used_bytes = 0;
	area->a_written_bytes = 0;
again:
	area->a_ops->get_free_segment(area);
	area->a_ops->get_erase_count(area);

	log_gc("logfs_open_area(%x, %x)\n", area->a_segno, area->a_level);
	err = area->a_ops->erase_segment(area);
	if (err) {
		printk(KERN_WARNING "LogFS: Error erasing segment %x\n",
				area->a_segno);
		logfs_mark_segment_bad(sb, area->a_segno);
		goto again;
	}
	area->a_is_open = 1;
	return closed;
}

void logfs_sync_area(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	struct logfs_super *super = logfs_super(sb);
	u64 ofs = dev_ofs(sb, area->a_segno, area->a_written_bytes);
	u32 len = (area->a_used_bytes - area->a_written_bytes);

	if (super->s_writesize)
		len &= ~(super->s_writesize - 1);
	if (len == 0)
		return;
	pad_wbuf(area, 0);
	super->s_devops->writeseg(sb, ofs, len);
	area->a_written_bytes += len;
}

void logfs_sync_segments(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i;

	for_each_area(i)
		logfs_sync_area(super->s_area[i]);
}

/*
 * Pick a free segment to be used for this area.  Effectively takes a
 * candidate from the free list (not really a candidate anymore).
 */
static void ostore_get_free_segment(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	struct logfs_super *super = logfs_super(sb);

	if (super->s_free_list.count == 0) {
		printk(KERN_ERR"LOGFS: ran out of free segments\n");
		LOGFS_BUG(sb);
	}

	area->a_segno = get_best_cand(sb, &super->s_free_list, NULL);
}

static void ostore_get_erase_count(struct logfs_area *area)
{
	struct logfs_segment_entry se;
	u32 ec_level;

	logfs_get_segment_entry(area->a_sb, area->a_segno, &se);
	BUG_ON(se.ec_level == cpu_to_be32(BADSEG) ||
			se.valid == cpu_to_be32(RESERVED));

	ec_level = be32_to_cpu(se.ec_level);
	area->a_erase_count = (ec_level >> 4) + 1;
}

static int ostore_erase_segment(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	struct logfs_segment_header sh;
	u64 ofs;
	int err;

	err = logfs_erase_segment(sb, area->a_segno, 0);
	if (err)
		return err;

	sh.pad = 0;
	sh.type = SEG_OSTORE;
	sh.level = (__force u8)area->a_level;
	sh.segno = cpu_to_be32(area->a_segno);
	sh.ec = cpu_to_be32(area->a_erase_count);
	sh.gec = cpu_to_be64(logfs_super(sb)->s_gec);
	sh.crc = logfs_crc32(&sh, sizeof(sh), 4);

	logfs_set_segment_erased(sb, area->a_segno, area->a_erase_count,
			area->a_level);

	ofs = dev_ofs(sb, area->a_segno, 0);
	area->a_used_bytes = sizeof(sh);
	logfs_buf_write(area, ofs, &sh, sizeof(sh));
	return 0;
}

static const struct logfs_area_ops ostore_area_ops = {
	.get_free_segment	= ostore_get_free_segment,
	.get_erase_count	= ostore_get_erase_count,
	.erase_segment		= ostore_erase_segment,
};

static void free_area(struct logfs_area *area)
{
	if (area)
		freeseg(area->a_sb, area->a_segno);
	kfree(area);
}

void free_areas(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i;

	for_each_area(i)
		free_area(super->s_area[i]);
	free_area(super->s_journal_area);
}

static struct logfs_area *alloc_area(struct super_block *sb)
{
	struct logfs_area *area;

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;

	area->a_sb = sb;
	return area;
}

static void map_invalidatepage(struct page *page, unsigned long l)
{
	return;
}

static int map_releasepage(struct page *page, gfp_t g)
{
	/* Don't release these pages */
	return 0;
}

static const struct address_space_operations mapping_aops = {
	.invalidatepage = map_invalidatepage,
	.releasepage	= map_releasepage,
	.set_page_dirty = __set_page_dirty_nobuffers,
};

int logfs_init_mapping(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping;
	struct inode *inode;

	inode = logfs_new_meta_inode(sb, LOGFS_INO_MAPPING);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	super->s_mapping_inode = inode;
	mapping = inode->i_mapping;
	mapping->a_ops = &mapping_aops;
	/* Would it be possible to use __GFP_HIGHMEM as well? */
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	return 0;
}

int logfs_init_areas(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i = -1;

	super->s_alias_pool = mempool_create_kmalloc_pool(600,
			sizeof(struct object_alias_item));
	if (!super->s_alias_pool)
		return -ENOMEM;

	super->s_journal_area = alloc_area(sb);
	if (!super->s_journal_area)
		goto err;

	for_each_area(i) {
		super->s_area[i] = alloc_area(sb);
		if (!super->s_area[i])
			goto err;
		super->s_area[i]->a_level = GC_LEVEL(i);
		super->s_area[i]->a_ops = &ostore_area_ops;
	}
	btree_init_mempool128(&super->s_object_alias_tree,
			super->s_btree_pool);
	return 0;

err:
	for (i--; i >= 0; i--)
		free_area(super->s_area[i]);
	free_area(super->s_journal_area);
	logfs_mempool_destroy(super->s_alias_pool);
	return -ENOMEM;
}

void logfs_cleanup_areas(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	btree_grim_visitor128(&super->s_object_alias_tree, 0, kill_alias);
}
