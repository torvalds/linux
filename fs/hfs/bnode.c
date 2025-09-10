// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/bnode.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handle basic btree node operations
 */

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include "btree.h"

static inline
bool is_bnode_offset_valid(struct hfs_bnode *node, int off)
{
	bool is_valid = off < node->tree->node_size;

	if (!is_valid) {
		pr_err("requested invalid offset: "
		       "NODE: id %u, type %#x, height %u, "
		       "node_size %u, offset %d\n",
		       node->this, node->type, node->height,
		       node->tree->node_size, off);
	}

	return is_valid;
}

static inline
int check_and_correct_requested_length(struct hfs_bnode *node, int off, int len)
{
	unsigned int node_size;

	if (!is_bnode_offset_valid(node, off))
		return 0;

	node_size = node->tree->node_size;

	if ((off + len) > node_size) {
		int new_len = (int)node_size - off;

		pr_err("requested length has been corrected: "
		       "NODE: id %u, type %#x, height %u, "
		       "node_size %u, offset %d, "
		       "requested_len %d, corrected_len %d\n",
		       node->this, node->type, node->height,
		       node->tree->node_size, off, len, new_len);

		return new_len;
	}

	return len;
}

void hfs_bnode_read(struct hfs_bnode *node, void *buf, int off, int len)
{
	struct page *page;
	int pagenum;
	int bytes_read;
	int bytes_to_read;

	if (!is_bnode_offset_valid(node, off))
		return;

	if (len == 0) {
		pr_err("requested zero length: "
		       "NODE: id %u, type %#x, height %u, "
		       "node_size %u, offset %d, len %d\n",
		       node->this, node->type, node->height,
		       node->tree->node_size, off, len);
		return;
	}

	len = check_and_correct_requested_length(node, off, len);

	off += node->page_offset;
	pagenum = off >> PAGE_SHIFT;
	off &= ~PAGE_MASK; /* compute page offset for the first page */

	for (bytes_read = 0; bytes_read < len; bytes_read += bytes_to_read) {
		if (pagenum >= node->tree->pages_per_bnode)
			break;
		page = node->page[pagenum];
		bytes_to_read = min_t(int, len - bytes_read, PAGE_SIZE - off);

		memcpy_from_page(buf + bytes_read, page, off, bytes_to_read);

		pagenum++;
		off = 0; /* page offset only applies to the first page */
	}
}

u16 hfs_bnode_read_u16(struct hfs_bnode *node, int off)
{
	__be16 data;
	// optimize later...
	hfs_bnode_read(node, &data, off, 2);
	return be16_to_cpu(data);
}

u8 hfs_bnode_read_u8(struct hfs_bnode *node, int off)
{
	u8 data;
	// optimize later...
	hfs_bnode_read(node, &data, off, 1);
	return data;
}

void hfs_bnode_read_key(struct hfs_bnode *node, void *key, int off)
{
	struct hfs_btree *tree;
	int key_len;

	tree = node->tree;
	if (node->type == HFS_NODE_LEAF ||
	    tree->attributes & HFS_TREE_VARIDXKEYS)
		key_len = hfs_bnode_read_u8(node, off) + 1;
	else
		key_len = tree->max_key_len + 1;

	if (key_len > sizeof(hfs_btree_key) || key_len < 1) {
		memset(key, 0, sizeof(hfs_btree_key));
		pr_err("hfs: Invalid key length: %d\n", key_len);
		return;
	}

	hfs_bnode_read(node, key, off, key_len);
}

void hfs_bnode_write(struct hfs_bnode *node, void *buf, int off, int len)
{
	struct page *page;

	if (!is_bnode_offset_valid(node, off))
		return;

	if (len == 0) {
		pr_err("requested zero length: "
		       "NODE: id %u, type %#x, height %u, "
		       "node_size %u, offset %d, len %d\n",
		       node->this, node->type, node->height,
		       node->tree->node_size, off, len);
		return;
	}

	len = check_and_correct_requested_length(node, off, len);

	off += node->page_offset;
	page = node->page[0];

	memcpy_to_page(page, off, buf, len);
	set_page_dirty(page);
}

void hfs_bnode_write_u16(struct hfs_bnode *node, int off, u16 data)
{
	__be16 v = cpu_to_be16(data);
	// optimize later...
	hfs_bnode_write(node, &v, off, 2);
}

void hfs_bnode_write_u8(struct hfs_bnode *node, int off, u8 data)
{
	// optimize later...
	hfs_bnode_write(node, &data, off, 1);
}

void hfs_bnode_clear(struct hfs_bnode *node, int off, int len)
{
	struct page *page;

	if (!is_bnode_offset_valid(node, off))
		return;

	if (len == 0) {
		pr_err("requested zero length: "
		       "NODE: id %u, type %#x, height %u, "
		       "node_size %u, offset %d, len %d\n",
		       node->this, node->type, node->height,
		       node->tree->node_size, off, len);
		return;
	}

	len = check_and_correct_requested_length(node, off, len);

	off += node->page_offset;
	page = node->page[0];

	memzero_page(page, off, len);
	set_page_dirty(page);
}

void hfs_bnode_copy(struct hfs_bnode *dst_node, int dst,
		struct hfs_bnode *src_node, int src, int len)
{
	struct page *src_page, *dst_page;

	hfs_dbg(BNODE_MOD, "copybytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;

	len = check_and_correct_requested_length(src_node, src, len);
	len = check_and_correct_requested_length(dst_node, dst, len);

	src += src_node->page_offset;
	dst += dst_node->page_offset;
	src_page = src_node->page[0];
	dst_page = dst_node->page[0];

	memcpy_page(dst_page, dst, src_page, src, len);
	set_page_dirty(dst_page);
}

void hfs_bnode_move(struct hfs_bnode *node, int dst, int src, int len)
{
	struct page *page;
	void *ptr;

	hfs_dbg(BNODE_MOD, "movebytes: %u,%u,%u\n", dst, src, len);
	if (!len)
		return;

	len = check_and_correct_requested_length(node, src, len);
	len = check_and_correct_requested_length(node, dst, len);

	src += node->page_offset;
	dst += node->page_offset;
	page = node->page[0];
	ptr = kmap_local_page(page);
	memmove(ptr + dst, ptr + src, len);
	kunmap_local(ptr);
	set_page_dirty(page);
}

void hfs_bnode_dump(struct hfs_bnode *node)
{
	struct hfs_bnode_desc desc;
	__be32 cnid;
	int i, off, key_off;

	hfs_dbg(BNODE_MOD, "bnode: %d\n", node->this);
	hfs_bnode_read(node, &desc, 0, sizeof(desc));
	hfs_dbg(BNODE_MOD, "%d, %d, %d, %d, %d\n",
		be32_to_cpu(desc.next), be32_to_cpu(desc.prev),
		desc.type, desc.height, be16_to_cpu(desc.num_recs));

	off = node->tree->node_size - 2;
	for (i = be16_to_cpu(desc.num_recs); i >= 0; off -= 2, i--) {
		key_off = hfs_bnode_read_u16(node, off);
		hfs_dbg_cont(BNODE_MOD, " %d", key_off);
		if (i && node->type == HFS_NODE_INDEX) {
			int tmp;

			if (node->tree->attributes & HFS_TREE_VARIDXKEYS)
				tmp = (hfs_bnode_read_u8(node, key_off) | 1) + 1;
			else
				tmp = node->tree->max_key_len + 1;
			hfs_dbg_cont(BNODE_MOD, " (%d,%d",
				     tmp, hfs_bnode_read_u8(node, key_off));
			hfs_bnode_read(node, &cnid, key_off + tmp, 4);
			hfs_dbg_cont(BNODE_MOD, ",%d)", be32_to_cpu(cnid));
		} else if (i && node->type == HFS_NODE_LEAF) {
			int tmp;

			tmp = hfs_bnode_read_u8(node, key_off);
			hfs_dbg_cont(BNODE_MOD, " (%d)", tmp);
		}
	}
	hfs_dbg_cont(BNODE_MOD, "\n");
}

void hfs_bnode_unlink(struct hfs_bnode *node)
{
	struct hfs_btree *tree;
	struct hfs_bnode *tmp;
	__be32 cnid;

	tree = node->tree;
	if (node->prev) {
		tmp = hfs_bnode_find(tree, node->prev);
		if (IS_ERR(tmp))
			return;
		tmp->next = node->next;
		cnid = cpu_to_be32(tmp->next);
		hfs_bnode_write(tmp, &cnid, offsetof(struct hfs_bnode_desc, next), 4);
		hfs_bnode_put(tmp);
	} else if (node->type == HFS_NODE_LEAF)
		tree->leaf_head = node->next;

	if (node->next) {
		tmp = hfs_bnode_find(tree, node->next);
		if (IS_ERR(tmp))
			return;
		tmp->prev = node->prev;
		cnid = cpu_to_be32(tmp->prev);
		hfs_bnode_write(tmp, &cnid, offsetof(struct hfs_bnode_desc, prev), 4);
		hfs_bnode_put(tmp);
	} else if (node->type == HFS_NODE_LEAF)
		tree->leaf_tail = node->prev;

	// move down?
	if (!node->prev && !node->next) {
		printk(KERN_DEBUG "hfs_btree_del_level\n");
	}
	if (!node->parent) {
		tree->root = 0;
		tree->depth = 0;
	}
	set_bit(HFS_BNODE_DELETED, &node->flags);
}

static inline int hfs_bnode_hash(u32 num)
{
	num = (num >> 16) + num;
	num += num >> 8;
	return num & (NODE_HASH_SIZE - 1);
}

struct hfs_bnode *hfs_bnode_findhash(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_bnode *node;

	if (cnid >= tree->node_count) {
		pr_err("request for non-existent node %d in B*Tree\n", cnid);
		return NULL;
	}

	for (node = tree->node_hash[hfs_bnode_hash(cnid)];
	     node; node = node->next_hash) {
		if (node->this == cnid) {
			return node;
		}
	}
	return NULL;
}

static struct hfs_bnode *__hfs_bnode_create(struct hfs_btree *tree, u32 cnid)
{
	struct hfs_bnode *node, *node2;
	struct address_space *mapping;
	struct page *page;
	int size, block, i, hash;
	loff_t off;

	if (cnid >= tree->node_count) {
		pr_err("request for non-existent node %d in B*Tree\n", cnid);
		return NULL;
	}

	size = sizeof(struct hfs_bnode) + tree->pages_per_bnode *
		sizeof(struct page *);
	node = kzalloc(size, GFP_KERNEL);
	if (!node)
		return NULL;
	node->tree = tree;
	node->this = cnid;
	set_bit(HFS_BNODE_NEW, &node->flags);
	atomic_set(&node->refcnt, 1);
	hfs_dbg(BNODE_REFS, "new_node(%d:%d): 1\n",
		node->tree->cnid, node->this);
	init_waitqueue_head(&node->lock_wq);
	spin_lock(&tree->hash_lock);
	node2 = hfs_bnode_findhash(tree, cnid);
	if (!node2) {
		hash = hfs_bnode_hash(cnid);
		node->next_hash = tree->node_hash[hash];
		tree->node_hash[hash] = node;
		tree->node_hash_cnt++;
	} else {
		hfs_bnode_get(node2);
		spin_unlock(&tree->hash_lock);
		kfree(node);
		wait_event(node2->lock_wq, !test_bit(HFS_BNODE_NEW, &node2->flags));
		return node2;
	}
	spin_unlock(&tree->hash_lock);

	mapping = tree->inode->i_mapping;
	off = (loff_t)cnid * tree->node_size;
	block = off >> PAGE_SHIFT;
	node->page_offset = off & ~PAGE_MASK;
	for (i = 0; i < tree->pages_per_bnode; i++) {
		page = read_mapping_page(mapping, block++, NULL);
		if (IS_ERR(page))
			goto fail;
		node->page[i] = page;
	}

	return node;
fail:
	set_bit(HFS_BNODE_ERROR, &node->flags);
	return node;
}

void hfs_bnode_unhash(struct hfs_bnode *node)
{
	struct hfs_bnode **p;

	hfs_dbg(BNODE_REFS, "remove_node(%d:%d): %d\n",
		node->tree->cnid, node->this, atomic_read(&node->refcnt));
	for (p = &node->tree->node_hash[hfs_bnode_hash(node->this)];
	     *p && *p != node; p = &(*p)->next_hash)
		;
	BUG_ON(!*p);
	*p = node->next_hash;
	node->tree->node_hash_cnt--;
}

/* Load a particular node out of a tree */
struct hfs_bnode *hfs_bnode_find(struct hfs_btree *tree, u32 num)
{
	struct hfs_bnode *node;
	struct hfs_bnode_desc *desc;
	int i, rec_off, off, next_off;
	int entry_size, key_size;

	spin_lock(&tree->hash_lock);
	node = hfs_bnode_findhash(tree, num);
	if (node) {
		hfs_bnode_get(node);
		spin_unlock(&tree->hash_lock);
		wait_event(node->lock_wq, !test_bit(HFS_BNODE_NEW, &node->flags));
		if (test_bit(HFS_BNODE_ERROR, &node->flags))
			goto node_error;
		return node;
	}
	spin_unlock(&tree->hash_lock);
	node = __hfs_bnode_create(tree, num);
	if (!node)
		return ERR_PTR(-ENOMEM);
	if (test_bit(HFS_BNODE_ERROR, &node->flags))
		goto node_error;
	if (!test_bit(HFS_BNODE_NEW, &node->flags))
		return node;

	desc = (struct hfs_bnode_desc *)(kmap_local_page(node->page[0]) +
					 node->page_offset);
	node->prev = be32_to_cpu(desc->prev);
	node->next = be32_to_cpu(desc->next);
	node->num_recs = be16_to_cpu(desc->num_recs);
	node->type = desc->type;
	node->height = desc->height;
	kunmap_local(desc);

	switch (node->type) {
	case HFS_NODE_HEADER:
	case HFS_NODE_MAP:
		if (node->height != 0)
			goto node_error;
		break;
	case HFS_NODE_LEAF:
		if (node->height != 1)
			goto node_error;
		break;
	case HFS_NODE_INDEX:
		if (node->height <= 1 || node->height > tree->depth)
			goto node_error;
		break;
	default:
		goto node_error;
	}

	rec_off = tree->node_size - 2;
	off = hfs_bnode_read_u16(node, rec_off);
	if (off != sizeof(struct hfs_bnode_desc))
		goto node_error;
	for (i = 1; i <= node->num_recs; off = next_off, i++) {
		rec_off -= 2;
		next_off = hfs_bnode_read_u16(node, rec_off);
		if (next_off <= off ||
		    next_off > tree->node_size ||
		    next_off & 1)
			goto node_error;
		entry_size = next_off - off;
		if (node->type != HFS_NODE_INDEX &&
		    node->type != HFS_NODE_LEAF)
			continue;
		key_size = hfs_bnode_read_u8(node, off) + 1;
		if (key_size >= entry_size /*|| key_size & 1*/)
			goto node_error;
	}
	clear_bit(HFS_BNODE_NEW, &node->flags);
	wake_up(&node->lock_wq);
	return node;

node_error:
	set_bit(HFS_BNODE_ERROR, &node->flags);
	clear_bit(HFS_BNODE_NEW, &node->flags);
	wake_up(&node->lock_wq);
	hfs_bnode_put(node);
	return ERR_PTR(-EIO);
}

void hfs_bnode_free(struct hfs_bnode *node)
{
	int i;

	for (i = 0; i < node->tree->pages_per_bnode; i++)
		if (node->page[i])
			put_page(node->page[i]);
	kfree(node);
}

struct hfs_bnode *hfs_bnode_create(struct hfs_btree *tree, u32 num)
{
	struct hfs_bnode *node;
	struct page **pagep;
	int i;

	spin_lock(&tree->hash_lock);
	node = hfs_bnode_findhash(tree, num);
	spin_unlock(&tree->hash_lock);
	if (node) {
		pr_crit("new node %u already hashed?\n", num);
		WARN_ON(1);
		return node;
	}
	node = __hfs_bnode_create(tree, num);
	if (!node)
		return ERR_PTR(-ENOMEM);
	if (test_bit(HFS_BNODE_ERROR, &node->flags)) {
		hfs_bnode_put(node);
		return ERR_PTR(-EIO);
	}

	pagep = node->page;
	memzero_page(*pagep, node->page_offset,
		     min((int)PAGE_SIZE, (int)tree->node_size));
	set_page_dirty(*pagep);
	for (i = 1; i < tree->pages_per_bnode; i++) {
		memzero_page(*++pagep, 0, PAGE_SIZE);
		set_page_dirty(*pagep);
	}
	clear_bit(HFS_BNODE_NEW, &node->flags);
	wake_up(&node->lock_wq);

	return node;
}

void hfs_bnode_get(struct hfs_bnode *node)
{
	if (node) {
		atomic_inc(&node->refcnt);
		hfs_dbg(BNODE_REFS, "get_node(%d:%d): %d\n",
			node->tree->cnid, node->this,
			atomic_read(&node->refcnt));
	}
}

/* Dispose of resources used by a node */
void hfs_bnode_put(struct hfs_bnode *node)
{
	if (node) {
		struct hfs_btree *tree = node->tree;
		int i;

		hfs_dbg(BNODE_REFS, "put_node(%d:%d): %d\n",
			node->tree->cnid, node->this,
			atomic_read(&node->refcnt));
		BUG_ON(!atomic_read(&node->refcnt));
		if (!atomic_dec_and_lock(&node->refcnt, &tree->hash_lock))
			return;
		for (i = 0; i < tree->pages_per_bnode; i++) {
			if (!node->page[i])
				continue;
			mark_page_accessed(node->page[i]);
		}

		if (test_bit(HFS_BNODE_DELETED, &node->flags)) {
			hfs_bnode_unhash(node);
			spin_unlock(&tree->hash_lock);
			hfs_bnode_clear(node, 0, tree->node_size);
			hfs_bmap_free(node);
			hfs_bnode_free(node);
			return;
		}
		spin_unlock(&tree->hash_lock);
	}
}
