// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/byesde.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 *
 * Handle basic btree yesde operations
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/swap.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/* Copy a specified range of bytes from the raw data of a yesde */
void hfs_byesde_read(struct hfs_byesde *yesde, void *buf, int off, int len)
{
	struct page **pagep;
	int l;

	off += yesde->page_offset;
	pagep = yesde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memcpy(buf, kmap(*pagep) + off, l);
	kunmap(*pagep);

	while ((len -= l) != 0) {
		buf += l;
		l = min_t(int, len, PAGE_SIZE);
		memcpy(buf, kmap(*++pagep), l);
		kunmap(*pagep);
	}
}

u16 hfs_byesde_read_u16(struct hfs_byesde *yesde, int off)
{
	__be16 data;
	/* TODO: optimize later... */
	hfs_byesde_read(yesde, &data, off, 2);
	return be16_to_cpu(data);
}

u8 hfs_byesde_read_u8(struct hfs_byesde *yesde, int off)
{
	u8 data;
	/* TODO: optimize later... */
	hfs_byesde_read(yesde, &data, off, 1);
	return data;
}

void hfs_byesde_read_key(struct hfs_byesde *yesde, void *key, int off)
{
	struct hfs_btree *tree;
	int key_len;

	tree = yesde->tree;
	if (yesde->type == HFS_NODE_LEAF ||
	    tree->attributes & HFS_TREE_VARIDXKEYS ||
	    yesde->tree->cnid == HFSPLUS_ATTR_CNID)
		key_len = hfs_byesde_read_u16(yesde, off) + 2;
	else
		key_len = tree->max_key_len + 2;

	hfs_byesde_read(yesde, key, off, key_len);
}

void hfs_byesde_write(struct hfs_byesde *yesde, void *buf, int off, int len)
{
	struct page **pagep;
	int l;

	off += yesde->page_offset;
	pagep = yesde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memcpy(kmap(*pagep) + off, buf, l);
	set_page_dirty(*pagep);
	kunmap(*pagep);

	while ((len -= l) != 0) {
		buf += l;
		l = min_t(int, len, PAGE_SIZE);
		memcpy(kmap(*++pagep), buf, l);
		set_page_dirty(*pagep);
		kunmap(*pagep);
	}
}

void hfs_byesde_write_u16(struct hfs_byesde *yesde, int off, u16 data)
{
	__be16 v = cpu_to_be16(data);
	/* TODO: optimize later... */
	hfs_byesde_write(yesde, &v, off, 2);
}

void hfs_byesde_clear(struct hfs_byesde *yesde, int off, int len)
{
	struct page **pagep;
	int l;

	off += yesde->page_offset;
	pagep = yesde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memset(kmap(*pagep) + off, 0, l);
	set_page_dirty(*pagep);
	kunmap(*pagep);

	while ((len -= l) != 0) {
		l = min_t(int, len, PAGE_SIZE);
		memset(kmap(*++pagep), 0, l);
		set_page_dirty(*pagep);
		kunmap(*pagep);
	}
}

void hfs_byesde_copy(struct hfs_byesde *dst_yesde, int dst,
		    struct hfs_byesde *src_yesde, int src, int len)
{
	struct page **src_page, **dst_page;
	int l;

	hfs_dbg(BNODE_MOD, "copybytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;
	src += src_yesde->page_offset;
	dst += dst_yesde->page_offset;
	src_page = src_yesde->page + (src >> PAGE_SHIFT);
	src &= ~PAGE_MASK;
	dst_page = dst_yesde->page + (dst >> PAGE_SHIFT);
	dst &= ~PAGE_MASK;

	if (src == dst) {
		l = min_t(int, len, PAGE_SIZE - src);
		memcpy(kmap(*dst_page) + src, kmap(*src_page) + src, l);
		kunmap(*src_page);
		set_page_dirty(*dst_page);
		kunmap(*dst_page);

		while ((len -= l) != 0) {
			l = min_t(int, len, PAGE_SIZE);
			memcpy(kmap(*++dst_page), kmap(*++src_page), l);
			kunmap(*src_page);
			set_page_dirty(*dst_page);
			kunmap(*dst_page);
		}
	} else {
		void *src_ptr, *dst_ptr;

		do {
			src_ptr = kmap(*src_page) + src;
			dst_ptr = kmap(*dst_page) + dst;
			if (PAGE_SIZE - src < PAGE_SIZE - dst) {
				l = PAGE_SIZE - src;
				src = 0;
				dst += l;
			} else {
				l = PAGE_SIZE - dst;
				src += l;
				dst = 0;
			}
			l = min(len, l);
			memcpy(dst_ptr, src_ptr, l);
			kunmap(*src_page);
			set_page_dirty(*dst_page);
			kunmap(*dst_page);
			if (!dst)
				dst_page++;
			else
				src_page++;
		} while ((len -= l));
	}
}

void hfs_byesde_move(struct hfs_byesde *yesde, int dst, int src, int len)
{
	struct page **src_page, **dst_page;
	int l;

	hfs_dbg(BNODE_MOD, "movebytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;
	src += yesde->page_offset;
	dst += yesde->page_offset;
	if (dst > src) {
		src += len - 1;
		src_page = yesde->page + (src >> PAGE_SHIFT);
		src = (src & ~PAGE_MASK) + 1;
		dst += len - 1;
		dst_page = yesde->page + (dst >> PAGE_SHIFT);
		dst = (dst & ~PAGE_MASK) + 1;

		if (src == dst) {
			while (src < len) {
				memmove(kmap(*dst_page), kmap(*src_page), src);
				kunmap(*src_page);
				set_page_dirty(*dst_page);
				kunmap(*dst_page);
				len -= src;
				src = PAGE_SIZE;
				src_page--;
				dst_page--;
			}
			src -= len;
			memmove(kmap(*dst_page) + src,
				kmap(*src_page) + src, len);
			kunmap(*src_page);
			set_page_dirty(*dst_page);
			kunmap(*dst_page);
		} else {
			void *src_ptr, *dst_ptr;

			do {
				src_ptr = kmap(*src_page) + src;
				dst_ptr = kmap(*dst_page) + dst;
				if (src < dst) {
					l = src;
					src = PAGE_SIZE;
					dst -= l;
				} else {
					l = dst;
					src -= l;
					dst = PAGE_SIZE;
				}
				l = min(len, l);
				memmove(dst_ptr - l, src_ptr - l, l);
				kunmap(*src_page);
				set_page_dirty(*dst_page);
				kunmap(*dst_page);
				if (dst == PAGE_SIZE)
					dst_page--;
				else
					src_page--;
			} while ((len -= l));
		}
	} else {
		src_page = yesde->page + (src >> PAGE_SHIFT);
		src &= ~PAGE_MASK;
		dst_page = yesde->page + (dst >> PAGE_SHIFT);
		dst &= ~PAGE_MASK;

		if (src == dst) {
			l = min_t(int, len, PAGE_SIZE - src);
			memmove(kmap(*dst_page) + src,
				kmap(*src_page) + src, l);
			kunmap(*src_page);
			set_page_dirty(*dst_page);
			kunmap(*dst_page);

			while ((len -= l) != 0) {
				l = min_t(int, len, PAGE_SIZE);
				memmove(kmap(*++dst_page),
					kmap(*++src_page), l);
				kunmap(*src_page);
				set_page_dirty(*dst_page);
				kunmap(*dst_page);
			}
		} else {
			void *src_ptr, *dst_ptr;

			do {
				src_ptr = kmap(*src_page) + src;
				dst_ptr = kmap(*dst_page) + dst;
				if (PAGE_SIZE - src <
						PAGE_SIZE - dst) {
					l = PAGE_SIZE - src;
					src = 0;
					dst += l;
				} else {
					l = PAGE_SIZE - dst;
					src += l;
					dst = 0;
				}
				l = min(len, l);
				memmove(dst_ptr, src_ptr, l);
				kunmap(*src_page);
				set_page_dirty(*dst_page);
				kunmap(*dst_page);
				if (!dst)
					dst_page++;
				else
					src_page++;
			} while ((len -= l));
		}
	}
}

void hfs_byesde_dump(struct hfs_byesde *yesde)
{
	struct hfs_byesde_desc desc;
	__be32 cnid;
	int i, off, key_off;

	hfs_dbg(BNODE_MOD, "byesde: %d\n", yesde->this);
	hfs_byesde_read(yesde, &desc, 0, sizeof(desc));
	hfs_dbg(BNODE_MOD, "%d, %d, %d, %d, %d\n",
		be32_to_cpu(desc.next), be32_to_cpu(desc.prev),
		desc.type, desc.height, be16_to_cpu(desc.num_recs));

	off = yesde->tree->yesde_size - 2;
	for (i = be16_to_cpu(desc.num_recs); i >= 0; off -= 2, i--) {
		key_off = hfs_byesde_read_u16(yesde, off);
		hfs_dbg(BNODE_MOD, " %d", key_off);
		if (i && yesde->type == HFS_NODE_INDEX) {
			int tmp;

			if (yesde->tree->attributes & HFS_TREE_VARIDXKEYS ||
					yesde->tree->cnid == HFSPLUS_ATTR_CNID)
				tmp = hfs_byesde_read_u16(yesde, key_off) + 2;
			else
				tmp = yesde->tree->max_key_len + 2;
			hfs_dbg_cont(BNODE_MOD, " (%d", tmp);
			hfs_byesde_read(yesde, &cnid, key_off + tmp, 4);
			hfs_dbg_cont(BNODE_MOD, ",%d)", be32_to_cpu(cnid));
		} else if (i && yesde->type == HFS_NODE_LEAF) {
			int tmp;

			tmp = hfs_byesde_read_u16(yesde, key_off);
			hfs_dbg_cont(BNODE_MOD, " (%d)", tmp);
		}
	}
	hfs_dbg_cont(BNODE_MOD, "\n");
}

void hfs_byesde_unlink(struct hfs_byesde *yesde)
{
	struct hfs_btree *tree;
	struct hfs_byesde *tmp;
	__be32 cnid;

	tree = yesde->tree;
	if (yesde->prev) {
		tmp = hfs_byesde_find(tree, yesde->prev);
		if (IS_ERR(tmp))
			return;
		tmp->next = yesde->next;
		cnid = cpu_to_be32(tmp->next);
		hfs_byesde_write(tmp, &cnid,
			offsetof(struct hfs_byesde_desc, next), 4);
		hfs_byesde_put(tmp);
	} else if (yesde->type == HFS_NODE_LEAF)
		tree->leaf_head = yesde->next;

	if (yesde->next) {
		tmp = hfs_byesde_find(tree, yesde->next);
		if (IS_ERR(tmp))
			return;
		tmp->prev = yesde->prev;
		cnid = cpu_to_be32(tmp->prev);
		hfs_byesde_write(tmp, &cnid,
			offsetof(struct hfs_byesde_desc, prev), 4);
		hfs_byesde_put(tmp);
	} else if (yesde->type == HFS_NODE_LEAF)
		tree->leaf_tail = yesde->prev;

	/* move down? */
	if (!yesde->prev && !yesde->next)
		hfs_dbg(BNODE_MOD, "hfs_btree_del_level\n");
	if (!yesde->parent) {
		tree->root = 0;
		tree->depth = 0;
	}
	set_bit(HFS_BNODE_DELETED, &yesde->flags);
}

static inline int hfs_byesde_hash(u32 num)
{
	num = (num >> 16) + num;
	num += num >> 8;
	return num & (NODE_HASH_SIZE - 1);
}

struct hfs_byesde *hfs_byesde_findhash(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_byesde *yesde;

	if (cnid >= tree->yesde_count) {
		pr_err("request for yesn-existent yesde %d in B*Tree\n",
		       cnid);
		return NULL;
	}

	for (yesde = tree->yesde_hash[hfs_byesde_hash(cnid)];
			yesde; yesde = yesde->next_hash)
		if (yesde->this == cnid)
			return yesde;
	return NULL;
}

static struct hfs_byesde *__hfs_byesde_create(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_byesde *yesde, *yesde2;
	struct address_space *mapping;
	struct page *page;
	int size, block, i, hash;
	loff_t off;

	if (cnid >= tree->yesde_count) {
		pr_err("request for yesn-existent yesde %d in B*Tree\n",
		       cnid);
		return NULL;
	}

	size = sizeof(struct hfs_byesde) + tree->pages_per_byesde *
		sizeof(struct page *);
	yesde = kzalloc(size, GFP_KERNEL);
	if (!yesde)
		return NULL;
	yesde->tree = tree;
	yesde->this = cnid;
	set_bit(HFS_BNODE_NEW, &yesde->flags);
	atomic_set(&yesde->refcnt, 1);
	hfs_dbg(BNODE_REFS, "new_yesde(%d:%d): 1\n",
		yesde->tree->cnid, yesde->this);
	init_waitqueue_head(&yesde->lock_wq);
	spin_lock(&tree->hash_lock);
	yesde2 = hfs_byesde_findhash(tree, cnid);
	if (!yesde2) {
		hash = hfs_byesde_hash(cnid);
		yesde->next_hash = tree->yesde_hash[hash];
		tree->yesde_hash[hash] = yesde;
		tree->yesde_hash_cnt++;
	} else {
		spin_unlock(&tree->hash_lock);
		kfree(yesde);
		wait_event(yesde2->lock_wq,
			!test_bit(HFS_BNODE_NEW, &yesde2->flags));
		return yesde2;
	}
	spin_unlock(&tree->hash_lock);

	mapping = tree->iyesde->i_mapping;
	off = (loff_t)cnid << tree->yesde_size_shift;
	block = off >> PAGE_SHIFT;
	yesde->page_offset = off & ~PAGE_MASK;
	for (i = 0; i < tree->pages_per_byesde; block++, i++) {
		page = read_mapping_page(mapping, block, NULL);
		if (IS_ERR(page))
			goto fail;
		if (PageError(page)) {
			put_page(page);
			goto fail;
		}
		yesde->page[i] = page;
	}

	return yesde;
fail:
	set_bit(HFS_BNODE_ERROR, &yesde->flags);
	return yesde;
}

void hfs_byesde_unhash(struct hfs_byesde *yesde)
{
	struct hfs_byesde **p;

	hfs_dbg(BNODE_REFS, "remove_yesde(%d:%d): %d\n",
		yesde->tree->cnid, yesde->this, atomic_read(&yesde->refcnt));
	for (p = &yesde->tree->yesde_hash[hfs_byesde_hash(yesde->this)];
	     *p && *p != yesde; p = &(*p)->next_hash)
		;
	BUG_ON(!*p);
	*p = yesde->next_hash;
	yesde->tree->yesde_hash_cnt--;
}

/* Load a particular yesde out of a tree */
struct hfs_byesde *hfs_byesde_find(struct hfs_btree *tree, u32 num)
{
	struct hfs_byesde *yesde;
	struct hfs_byesde_desc *desc;
	int i, rec_off, off, next_off;
	int entry_size, key_size;

	spin_lock(&tree->hash_lock);
	yesde = hfs_byesde_findhash(tree, num);
	if (yesde) {
		hfs_byesde_get(yesde);
		spin_unlock(&tree->hash_lock);
		wait_event(yesde->lock_wq,
			!test_bit(HFS_BNODE_NEW, &yesde->flags));
		if (test_bit(HFS_BNODE_ERROR, &yesde->flags))
			goto yesde_error;
		return yesde;
	}
	spin_unlock(&tree->hash_lock);
	yesde = __hfs_byesde_create(tree, num);
	if (!yesde)
		return ERR_PTR(-ENOMEM);
	if (test_bit(HFS_BNODE_ERROR, &yesde->flags))
		goto yesde_error;
	if (!test_bit(HFS_BNODE_NEW, &yesde->flags))
		return yesde;

	desc = (struct hfs_byesde_desc *)(kmap(yesde->page[0]) +
			yesde->page_offset);
	yesde->prev = be32_to_cpu(desc->prev);
	yesde->next = be32_to_cpu(desc->next);
	yesde->num_recs = be16_to_cpu(desc->num_recs);
	yesde->type = desc->type;
	yesde->height = desc->height;
	kunmap(yesde->page[0]);

	switch (yesde->type) {
	case HFS_NODE_HEADER:
	case HFS_NODE_MAP:
		if (yesde->height != 0)
			goto yesde_error;
		break;
	case HFS_NODE_LEAF:
		if (yesde->height != 1)
			goto yesde_error;
		break;
	case HFS_NODE_INDEX:
		if (yesde->height <= 1 || yesde->height > tree->depth)
			goto yesde_error;
		break;
	default:
		goto yesde_error;
	}

	rec_off = tree->yesde_size - 2;
	off = hfs_byesde_read_u16(yesde, rec_off);
	if (off != sizeof(struct hfs_byesde_desc))
		goto yesde_error;
	for (i = 1; i <= yesde->num_recs; off = next_off, i++) {
		rec_off -= 2;
		next_off = hfs_byesde_read_u16(yesde, rec_off);
		if (next_off <= off ||
		    next_off > tree->yesde_size ||
		    next_off & 1)
			goto yesde_error;
		entry_size = next_off - off;
		if (yesde->type != HFS_NODE_INDEX &&
		    yesde->type != HFS_NODE_LEAF)
			continue;
		key_size = hfs_byesde_read_u16(yesde, off) + 2;
		if (key_size >= entry_size || key_size & 1)
			goto yesde_error;
	}
	clear_bit(HFS_BNODE_NEW, &yesde->flags);
	wake_up(&yesde->lock_wq);
	return yesde;

yesde_error:
	set_bit(HFS_BNODE_ERROR, &yesde->flags);
	clear_bit(HFS_BNODE_NEW, &yesde->flags);
	wake_up(&yesde->lock_wq);
	hfs_byesde_put(yesde);
	return ERR_PTR(-EIO);
}

void hfs_byesde_free(struct hfs_byesde *yesde)
{
	int i;

	for (i = 0; i < yesde->tree->pages_per_byesde; i++)
		if (yesde->page[i])
			put_page(yesde->page[i]);
	kfree(yesde);
}

struct hfs_byesde *hfs_byesde_create(struct hfs_btree *tree, u32 num)
{
	struct hfs_byesde *yesde;
	struct page **pagep;
	int i;

	spin_lock(&tree->hash_lock);
	yesde = hfs_byesde_findhash(tree, num);
	spin_unlock(&tree->hash_lock);
	if (yesde) {
		pr_crit("new yesde %u already hashed?\n", num);
		WARN_ON(1);
		return yesde;
	}
	yesde = __hfs_byesde_create(tree, num);
	if (!yesde)
		return ERR_PTR(-ENOMEM);
	if (test_bit(HFS_BNODE_ERROR, &yesde->flags)) {
		hfs_byesde_put(yesde);
		return ERR_PTR(-EIO);
	}

	pagep = yesde->page;
	memset(kmap(*pagep) + yesde->page_offset, 0,
	       min_t(int, PAGE_SIZE, tree->yesde_size));
	set_page_dirty(*pagep);
	kunmap(*pagep);
	for (i = 1; i < tree->pages_per_byesde; i++) {
		memset(kmap(*++pagep), 0, PAGE_SIZE);
		set_page_dirty(*pagep);
		kunmap(*pagep);
	}
	clear_bit(HFS_BNODE_NEW, &yesde->flags);
	wake_up(&yesde->lock_wq);

	return yesde;
}

void hfs_byesde_get(struct hfs_byesde *yesde)
{
	if (yesde) {
		atomic_inc(&yesde->refcnt);
		hfs_dbg(BNODE_REFS, "get_yesde(%d:%d): %d\n",
			yesde->tree->cnid, yesde->this,
			atomic_read(&yesde->refcnt));
	}
}

/* Dispose of resources used by a yesde */
void hfs_byesde_put(struct hfs_byesde *yesde)
{
	if (yesde) {
		struct hfs_btree *tree = yesde->tree;
		int i;

		hfs_dbg(BNODE_REFS, "put_yesde(%d:%d): %d\n",
			yesde->tree->cnid, yesde->this,
			atomic_read(&yesde->refcnt));
		BUG_ON(!atomic_read(&yesde->refcnt));
		if (!atomic_dec_and_lock(&yesde->refcnt, &tree->hash_lock))
			return;
		for (i = 0; i < tree->pages_per_byesde; i++) {
			if (!yesde->page[i])
				continue;
			mark_page_accessed(yesde->page[i]);
		}

		if (test_bit(HFS_BNODE_DELETED, &yesde->flags)) {
			hfs_byesde_unhash(yesde);
			spin_unlock(&tree->hash_lock);
			if (hfs_byesde_need_zeroout(tree))
				hfs_byesde_clear(yesde, 0, tree->yesde_size);
			hfs_bmap_free(yesde);
			hfs_byesde_free(yesde);
			return;
		}
		spin_unlock(&tree->hash_lock);
	}
}

/*
 * Unused yesdes have to be zeroed if this is the catalog tree and
 * a corresponding flag in the volume header is set.
 */
bool hfs_byesde_need_zeroout(struct hfs_btree *tree)
{
	struct super_block *sb = tree->iyesde->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	const u32 volume_attr = be32_to_cpu(sbi->s_vhdr->attributes);

	return tree->cnid == HFSPLUS_CAT_CNID &&
		volume_attr & HFSPLUS_VOL_UNUSED_NODE_FIX;
}
