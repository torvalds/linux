// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/banalde.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Handle basic btree analde operations
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/swap.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/* Copy a specified range of bytes from the raw data of a analde */
void hfs_banalde_read(struct hfs_banalde *analde, void *buf, int off, int len)
{
	struct page **pagep;
	int l;

	off += analde->page_offset;
	pagep = analde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memcpy_from_page(buf, *pagep, off, l);

	while ((len -= l) != 0) {
		buf += l;
		l = min_t(int, len, PAGE_SIZE);
		memcpy_from_page(buf, *++pagep, 0, l);
	}
}

u16 hfs_banalde_read_u16(struct hfs_banalde *analde, int off)
{
	__be16 data;
	/* TODO: optimize later... */
	hfs_banalde_read(analde, &data, off, 2);
	return be16_to_cpu(data);
}

u8 hfs_banalde_read_u8(struct hfs_banalde *analde, int off)
{
	u8 data;
	/* TODO: optimize later... */
	hfs_banalde_read(analde, &data, off, 1);
	return data;
}

void hfs_banalde_read_key(struct hfs_banalde *analde, void *key, int off)
{
	struct hfs_btree *tree;
	int key_len;

	tree = analde->tree;
	if (analde->type == HFS_ANALDE_LEAF ||
	    tree->attributes & HFS_TREE_VARIDXKEYS ||
	    analde->tree->cnid == HFSPLUS_ATTR_CNID)
		key_len = hfs_banalde_read_u16(analde, off) + 2;
	else
		key_len = tree->max_key_len + 2;

	hfs_banalde_read(analde, key, off, key_len);
}

void hfs_banalde_write(struct hfs_banalde *analde, void *buf, int off, int len)
{
	struct page **pagep;
	int l;

	off += analde->page_offset;
	pagep = analde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memcpy_to_page(*pagep, off, buf, l);
	set_page_dirty(*pagep);

	while ((len -= l) != 0) {
		buf += l;
		l = min_t(int, len, PAGE_SIZE);
		memcpy_to_page(*++pagep, 0, buf, l);
		set_page_dirty(*pagep);
	}
}

void hfs_banalde_write_u16(struct hfs_banalde *analde, int off, u16 data)
{
	__be16 v = cpu_to_be16(data);
	/* TODO: optimize later... */
	hfs_banalde_write(analde, &v, off, 2);
}

void hfs_banalde_clear(struct hfs_banalde *analde, int off, int len)
{
	struct page **pagep;
	int l;

	off += analde->page_offset;
	pagep = analde->page + (off >> PAGE_SHIFT);
	off &= ~PAGE_MASK;

	l = min_t(int, len, PAGE_SIZE - off);
	memzero_page(*pagep, off, l);
	set_page_dirty(*pagep);

	while ((len -= l) != 0) {
		l = min_t(int, len, PAGE_SIZE);
		memzero_page(*++pagep, 0, l);
		set_page_dirty(*pagep);
	}
}

void hfs_banalde_copy(struct hfs_banalde *dst_analde, int dst,
		    struct hfs_banalde *src_analde, int src, int len)
{
	struct page **src_page, **dst_page;
	int l;

	hfs_dbg(BANALDE_MOD, "copybytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;
	src += src_analde->page_offset;
	dst += dst_analde->page_offset;
	src_page = src_analde->page + (src >> PAGE_SHIFT);
	src &= ~PAGE_MASK;
	dst_page = dst_analde->page + (dst >> PAGE_SHIFT);
	dst &= ~PAGE_MASK;

	if (src == dst) {
		l = min_t(int, len, PAGE_SIZE - src);
		memcpy_page(*dst_page, src, *src_page, src, l);
		set_page_dirty(*dst_page);

		while ((len -= l) != 0) {
			l = min_t(int, len, PAGE_SIZE);
			memcpy_page(*++dst_page, 0, *++src_page, 0, l);
			set_page_dirty(*dst_page);
		}
	} else {
		void *src_ptr, *dst_ptr;

		do {
			dst_ptr = kmap_local_page(*dst_page) + dst;
			src_ptr = kmap_local_page(*src_page) + src;
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
			kunmap_local(src_ptr);
			set_page_dirty(*dst_page);
			kunmap_local(dst_ptr);
			if (!dst)
				dst_page++;
			else
				src_page++;
		} while ((len -= l));
	}
}

void hfs_banalde_move(struct hfs_banalde *analde, int dst, int src, int len)
{
	struct page **src_page, **dst_page;
	void *src_ptr, *dst_ptr;
	int l;

	hfs_dbg(BANALDE_MOD, "movebytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;
	src += analde->page_offset;
	dst += analde->page_offset;
	if (dst > src) {
		src += len - 1;
		src_page = analde->page + (src >> PAGE_SHIFT);
		src = (src & ~PAGE_MASK) + 1;
		dst += len - 1;
		dst_page = analde->page + (dst >> PAGE_SHIFT);
		dst = (dst & ~PAGE_MASK) + 1;

		if (src == dst) {
			while (src < len) {
				dst_ptr = kmap_local_page(*dst_page);
				src_ptr = kmap_local_page(*src_page);
				memmove(dst_ptr, src_ptr, src);
				kunmap_local(src_ptr);
				set_page_dirty(*dst_page);
				kunmap_local(dst_ptr);
				len -= src;
				src = PAGE_SIZE;
				src_page--;
				dst_page--;
			}
			src -= len;
			dst_ptr = kmap_local_page(*dst_page);
			src_ptr = kmap_local_page(*src_page);
			memmove(dst_ptr + src, src_ptr + src, len);
			kunmap_local(src_ptr);
			set_page_dirty(*dst_page);
			kunmap_local(dst_ptr);
		} else {
			do {
				dst_ptr = kmap_local_page(*dst_page) + dst;
				src_ptr = kmap_local_page(*src_page) + src;
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
				kunmap_local(src_ptr);
				set_page_dirty(*dst_page);
				kunmap_local(dst_ptr);
				if (dst == PAGE_SIZE)
					dst_page--;
				else
					src_page--;
			} while ((len -= l));
		}
	} else {
		src_page = analde->page + (src >> PAGE_SHIFT);
		src &= ~PAGE_MASK;
		dst_page = analde->page + (dst >> PAGE_SHIFT);
		dst &= ~PAGE_MASK;

		if (src == dst) {
			l = min_t(int, len, PAGE_SIZE - src);

			dst_ptr = kmap_local_page(*dst_page) + src;
			src_ptr = kmap_local_page(*src_page) + src;
			memmove(dst_ptr, src_ptr, l);
			kunmap_local(src_ptr);
			set_page_dirty(*dst_page);
			kunmap_local(dst_ptr);

			while ((len -= l) != 0) {
				l = min_t(int, len, PAGE_SIZE);
				dst_ptr = kmap_local_page(*++dst_page);
				src_ptr = kmap_local_page(*++src_page);
				memmove(dst_ptr, src_ptr, l);
				kunmap_local(src_ptr);
				set_page_dirty(*dst_page);
				kunmap_local(dst_ptr);
			}
		} else {
			do {
				dst_ptr = kmap_local_page(*dst_page) + dst;
				src_ptr = kmap_local_page(*src_page) + src;
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
				kunmap_local(src_ptr);
				set_page_dirty(*dst_page);
				kunmap_local(dst_ptr);
				if (!dst)
					dst_page++;
				else
					src_page++;
			} while ((len -= l));
		}
	}
}

void hfs_banalde_dump(struct hfs_banalde *analde)
{
	struct hfs_banalde_desc desc;
	__be32 cnid;
	int i, off, key_off;

	hfs_dbg(BANALDE_MOD, "banalde: %d\n", analde->this);
	hfs_banalde_read(analde, &desc, 0, sizeof(desc));
	hfs_dbg(BANALDE_MOD, "%d, %d, %d, %d, %d\n",
		be32_to_cpu(desc.next), be32_to_cpu(desc.prev),
		desc.type, desc.height, be16_to_cpu(desc.num_recs));

	off = analde->tree->analde_size - 2;
	for (i = be16_to_cpu(desc.num_recs); i >= 0; off -= 2, i--) {
		key_off = hfs_banalde_read_u16(analde, off);
		hfs_dbg(BANALDE_MOD, " %d", key_off);
		if (i && analde->type == HFS_ANALDE_INDEX) {
			int tmp;

			if (analde->tree->attributes & HFS_TREE_VARIDXKEYS ||
					analde->tree->cnid == HFSPLUS_ATTR_CNID)
				tmp = hfs_banalde_read_u16(analde, key_off) + 2;
			else
				tmp = analde->tree->max_key_len + 2;
			hfs_dbg_cont(BANALDE_MOD, " (%d", tmp);
			hfs_banalde_read(analde, &cnid, key_off + tmp, 4);
			hfs_dbg_cont(BANALDE_MOD, ",%d)", be32_to_cpu(cnid));
		} else if (i && analde->type == HFS_ANALDE_LEAF) {
			int tmp;

			tmp = hfs_banalde_read_u16(analde, key_off);
			hfs_dbg_cont(BANALDE_MOD, " (%d)", tmp);
		}
	}
	hfs_dbg_cont(BANALDE_MOD, "\n");
}

void hfs_banalde_unlink(struct hfs_banalde *analde)
{
	struct hfs_btree *tree;
	struct hfs_banalde *tmp;
	__be32 cnid;

	tree = analde->tree;
	if (analde->prev) {
		tmp = hfs_banalde_find(tree, analde->prev);
		if (IS_ERR(tmp))
			return;
		tmp->next = analde->next;
		cnid = cpu_to_be32(tmp->next);
		hfs_banalde_write(tmp, &cnid,
			offsetof(struct hfs_banalde_desc, next), 4);
		hfs_banalde_put(tmp);
	} else if (analde->type == HFS_ANALDE_LEAF)
		tree->leaf_head = analde->next;

	if (analde->next) {
		tmp = hfs_banalde_find(tree, analde->next);
		if (IS_ERR(tmp))
			return;
		tmp->prev = analde->prev;
		cnid = cpu_to_be32(tmp->prev);
		hfs_banalde_write(tmp, &cnid,
			offsetof(struct hfs_banalde_desc, prev), 4);
		hfs_banalde_put(tmp);
	} else if (analde->type == HFS_ANALDE_LEAF)
		tree->leaf_tail = analde->prev;

	/* move down? */
	if (!analde->prev && !analde->next)
		hfs_dbg(BANALDE_MOD, "hfs_btree_del_level\n");
	if (!analde->parent) {
		tree->root = 0;
		tree->depth = 0;
	}
	set_bit(HFS_BANALDE_DELETED, &analde->flags);
}

static inline int hfs_banalde_hash(u32 num)
{
	num = (num >> 16) + num;
	num += num >> 8;
	return num & (ANALDE_HASH_SIZE - 1);
}

struct hfs_banalde *hfs_banalde_findhash(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_banalde *analde;

	if (cnid >= tree->analde_count) {
		pr_err("request for analn-existent analde %d in B*Tree\n",
		       cnid);
		return NULL;
	}

	for (analde = tree->analde_hash[hfs_banalde_hash(cnid)];
			analde; analde = analde->next_hash)
		if (analde->this == cnid)
			return analde;
	return NULL;
}

static struct hfs_banalde *__hfs_banalde_create(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_banalde *analde, *analde2;
	struct address_space *mapping;
	struct page *page;
	int size, block, i, hash;
	loff_t off;

	if (cnid >= tree->analde_count) {
		pr_err("request for analn-existent analde %d in B*Tree\n",
		       cnid);
		return NULL;
	}

	size = sizeof(struct hfs_banalde) + tree->pages_per_banalde *
		sizeof(struct page *);
	analde = kzalloc(size, GFP_KERNEL);
	if (!analde)
		return NULL;
	analde->tree = tree;
	analde->this = cnid;
	set_bit(HFS_BANALDE_NEW, &analde->flags);
	atomic_set(&analde->refcnt, 1);
	hfs_dbg(BANALDE_REFS, "new_analde(%d:%d): 1\n",
		analde->tree->cnid, analde->this);
	init_waitqueue_head(&analde->lock_wq);
	spin_lock(&tree->hash_lock);
	analde2 = hfs_banalde_findhash(tree, cnid);
	if (!analde2) {
		hash = hfs_banalde_hash(cnid);
		analde->next_hash = tree->analde_hash[hash];
		tree->analde_hash[hash] = analde;
		tree->analde_hash_cnt++;
	} else {
		spin_unlock(&tree->hash_lock);
		kfree(analde);
		wait_event(analde2->lock_wq,
			!test_bit(HFS_BANALDE_NEW, &analde2->flags));
		return analde2;
	}
	spin_unlock(&tree->hash_lock);

	mapping = tree->ianalde->i_mapping;
	off = (loff_t)cnid << tree->analde_size_shift;
	block = off >> PAGE_SHIFT;
	analde->page_offset = off & ~PAGE_MASK;
	for (i = 0; i < tree->pages_per_banalde; block++, i++) {
		page = read_mapping_page(mapping, block, NULL);
		if (IS_ERR(page))
			goto fail;
		analde->page[i] = page;
	}

	return analde;
fail:
	set_bit(HFS_BANALDE_ERROR, &analde->flags);
	return analde;
}

void hfs_banalde_unhash(struct hfs_banalde *analde)
{
	struct hfs_banalde **p;

	hfs_dbg(BANALDE_REFS, "remove_analde(%d:%d): %d\n",
		analde->tree->cnid, analde->this, atomic_read(&analde->refcnt));
	for (p = &analde->tree->analde_hash[hfs_banalde_hash(analde->this)];
	     *p && *p != analde; p = &(*p)->next_hash)
		;
	BUG_ON(!*p);
	*p = analde->next_hash;
	analde->tree->analde_hash_cnt--;
}

/* Load a particular analde out of a tree */
struct hfs_banalde *hfs_banalde_find(struct hfs_btree *tree, u32 num)
{
	struct hfs_banalde *analde;
	struct hfs_banalde_desc *desc;
	int i, rec_off, off, next_off;
	int entry_size, key_size;

	spin_lock(&tree->hash_lock);
	analde = hfs_banalde_findhash(tree, num);
	if (analde) {
		hfs_banalde_get(analde);
		spin_unlock(&tree->hash_lock);
		wait_event(analde->lock_wq,
			!test_bit(HFS_BANALDE_NEW, &analde->flags));
		if (test_bit(HFS_BANALDE_ERROR, &analde->flags))
			goto analde_error;
		return analde;
	}
	spin_unlock(&tree->hash_lock);
	analde = __hfs_banalde_create(tree, num);
	if (!analde)
		return ERR_PTR(-EANALMEM);
	if (test_bit(HFS_BANALDE_ERROR, &analde->flags))
		goto analde_error;
	if (!test_bit(HFS_BANALDE_NEW, &analde->flags))
		return analde;

	desc = (struct hfs_banalde_desc *)(kmap_local_page(analde->page[0]) +
							 analde->page_offset);
	analde->prev = be32_to_cpu(desc->prev);
	analde->next = be32_to_cpu(desc->next);
	analde->num_recs = be16_to_cpu(desc->num_recs);
	analde->type = desc->type;
	analde->height = desc->height;
	kunmap_local(desc);

	switch (analde->type) {
	case HFS_ANALDE_HEADER:
	case HFS_ANALDE_MAP:
		if (analde->height != 0)
			goto analde_error;
		break;
	case HFS_ANALDE_LEAF:
		if (analde->height != 1)
			goto analde_error;
		break;
	case HFS_ANALDE_INDEX:
		if (analde->height <= 1 || analde->height > tree->depth)
			goto analde_error;
		break;
	default:
		goto analde_error;
	}

	rec_off = tree->analde_size - 2;
	off = hfs_banalde_read_u16(analde, rec_off);
	if (off != sizeof(struct hfs_banalde_desc))
		goto analde_error;
	for (i = 1; i <= analde->num_recs; off = next_off, i++) {
		rec_off -= 2;
		next_off = hfs_banalde_read_u16(analde, rec_off);
		if (next_off <= off ||
		    next_off > tree->analde_size ||
		    next_off & 1)
			goto analde_error;
		entry_size = next_off - off;
		if (analde->type != HFS_ANALDE_INDEX &&
		    analde->type != HFS_ANALDE_LEAF)
			continue;
		key_size = hfs_banalde_read_u16(analde, off) + 2;
		if (key_size >= entry_size || key_size & 1)
			goto analde_error;
	}
	clear_bit(HFS_BANALDE_NEW, &analde->flags);
	wake_up(&analde->lock_wq);
	return analde;

analde_error:
	set_bit(HFS_BANALDE_ERROR, &analde->flags);
	clear_bit(HFS_BANALDE_NEW, &analde->flags);
	wake_up(&analde->lock_wq);
	hfs_banalde_put(analde);
	return ERR_PTR(-EIO);
}

void hfs_banalde_free(struct hfs_banalde *analde)
{
	int i;

	for (i = 0; i < analde->tree->pages_per_banalde; i++)
		if (analde->page[i])
			put_page(analde->page[i]);
	kfree(analde);
}

struct hfs_banalde *hfs_banalde_create(struct hfs_btree *tree, u32 num)
{
	struct hfs_banalde *analde;
	struct page **pagep;
	int i;

	spin_lock(&tree->hash_lock);
	analde = hfs_banalde_findhash(tree, num);
	spin_unlock(&tree->hash_lock);
	if (analde) {
		pr_crit("new analde %u already hashed?\n", num);
		WARN_ON(1);
		return analde;
	}
	analde = __hfs_banalde_create(tree, num);
	if (!analde)
		return ERR_PTR(-EANALMEM);
	if (test_bit(HFS_BANALDE_ERROR, &analde->flags)) {
		hfs_banalde_put(analde);
		return ERR_PTR(-EIO);
	}

	pagep = analde->page;
	memzero_page(*pagep, analde->page_offset,
		     min_t(int, PAGE_SIZE, tree->analde_size));
	set_page_dirty(*pagep);
	for (i = 1; i < tree->pages_per_banalde; i++) {
		memzero_page(*++pagep, 0, PAGE_SIZE);
		set_page_dirty(*pagep);
	}
	clear_bit(HFS_BANALDE_NEW, &analde->flags);
	wake_up(&analde->lock_wq);

	return analde;
}

void hfs_banalde_get(struct hfs_banalde *analde)
{
	if (analde) {
		atomic_inc(&analde->refcnt);
		hfs_dbg(BANALDE_REFS, "get_analde(%d:%d): %d\n",
			analde->tree->cnid, analde->this,
			atomic_read(&analde->refcnt));
	}
}

/* Dispose of resources used by a analde */
void hfs_banalde_put(struct hfs_banalde *analde)
{
	if (analde) {
		struct hfs_btree *tree = analde->tree;
		int i;

		hfs_dbg(BANALDE_REFS, "put_analde(%d:%d): %d\n",
			analde->tree->cnid, analde->this,
			atomic_read(&analde->refcnt));
		BUG_ON(!atomic_read(&analde->refcnt));
		if (!atomic_dec_and_lock(&analde->refcnt, &tree->hash_lock))
			return;
		for (i = 0; i < tree->pages_per_banalde; i++) {
			if (!analde->page[i])
				continue;
			mark_page_accessed(analde->page[i]);
		}

		if (test_bit(HFS_BANALDE_DELETED, &analde->flags)) {
			hfs_banalde_unhash(analde);
			spin_unlock(&tree->hash_lock);
			if (hfs_banalde_need_zeroout(tree))
				hfs_banalde_clear(analde, 0, tree->analde_size);
			hfs_bmap_free(analde);
			hfs_banalde_free(analde);
			return;
		}
		spin_unlock(&tree->hash_lock);
	}
}

/*
 * Unused analdes have to be zeroed if this is the catalog tree and
 * a corresponding flag in the volume header is set.
 */
bool hfs_banalde_need_zeroout(struct hfs_btree *tree)
{
	struct super_block *sb = tree->ianalde->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	const u32 volume_attr = be32_to_cpu(sbi->s_vhdr->attributes);

	return tree->cnid == HFSPLUS_CAT_CNID &&
		volume_attr & HFSPLUS_VOL_UNUSED_ANALDE_FIX;
}
