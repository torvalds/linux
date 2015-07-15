/*
 * f2fs extent cache support
 *
 * Copyright (c) 2015 Motorola Mobility
 * Copyright (c) 2015 Samsung Electronics
 * Authors: Jaegeuk Kim <jaegeuk@kernel.org>
 *          Chao Yu <chao2.yu@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "node.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *extent_tree_slab;
static struct kmem_cache *extent_node_slab;

static struct extent_node *__attach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_node *parent, struct rb_node **p)
{
	struct extent_node *en;

	en = kmem_cache_alloc(extent_node_slab, GFP_ATOMIC);
	if (!en)
		return NULL;

	en->ei = *ei;
	INIT_LIST_HEAD(&en->list);

	rb_link_node(&en->rb_node, parent, p);
	rb_insert_color(&en->rb_node, &et->root);
	et->count++;
	atomic_inc(&sbi->total_ext_node);
	return en;
}

static void __detach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	rb_erase(&en->rb_node, &et->root);
	et->count--;
	atomic_dec(&sbi->total_ext_node);

	if (et->cached_en == en)
		et->cached_en = NULL;
}

static struct extent_tree *__grab_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	nid_t ino = inode->i_ino;

	down_write(&sbi->extent_tree_lock);
	et = radix_tree_lookup(&sbi->extent_tree_root, ino);
	if (!et) {
		et = f2fs_kmem_cache_alloc(extent_tree_slab, GFP_NOFS);
		f2fs_radix_tree_insert(&sbi->extent_tree_root, ino, et);
		memset(et, 0, sizeof(struct extent_tree));
		et->ino = ino;
		et->root = RB_ROOT;
		et->cached_en = NULL;
		rwlock_init(&et->lock);
		atomic_set(&et->refcount, 0);
		et->count = 0;
		sbi->total_ext_tree++;
	}
	atomic_inc(&et->refcount);
	up_write(&sbi->extent_tree_lock);

	/* never died until evict_inode */
	F2FS_I(inode)->extent_tree = et;

	return et;
}

static struct extent_node *__lookup_extent_tree(struct extent_tree *et,
							unsigned int fofs)
{
	struct rb_node *node = et->root.rb_node;
	struct extent_node *en;

	if (et->cached_en) {
		struct extent_info *cei = &et->cached_en->ei;

		if (cei->fofs <= fofs && cei->fofs + cei->len > fofs)
			return et->cached_en;
	}

	while (node) {
		en = rb_entry(node, struct extent_node, rb_node);

		if (fofs < en->ei.fofs)
			node = node->rb_left;
		else if (fofs >= en->ei.fofs + en->ei.len)
			node = node->rb_right;
		else
			return en;
	}
	return NULL;
}

static struct extent_node *__try_back_merge(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	struct extent_node *prev;
	struct rb_node *node;

	node = rb_prev(&en->rb_node);
	if (!node)
		return NULL;

	prev = rb_entry(node, struct extent_node, rb_node);
	if (__is_back_mergeable(&en->ei, &prev->ei)) {
		en->ei.fofs = prev->ei.fofs;
		en->ei.blk = prev->ei.blk;
		en->ei.len += prev->ei.len;
		__detach_extent_node(sbi, et, prev);
		return prev;
	}
	return NULL;
}

static struct extent_node *__try_front_merge(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	struct extent_node *next;
	struct rb_node *node;

	node = rb_next(&en->rb_node);
	if (!node)
		return NULL;

	next = rb_entry(node, struct extent_node, rb_node);
	if (__is_front_mergeable(&en->ei, &next->ei)) {
		en->ei.len += next->ei.len;
		__detach_extent_node(sbi, et, next);
		return next;
	}
	return NULL;
}

static struct extent_node *__insert_extent_tree(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct extent_node **den)
{
	struct rb_node **p = &et->root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_node *en;

	while (*p) {
		parent = *p;
		en = rb_entry(parent, struct extent_node, rb_node);

		if (ei->fofs < en->ei.fofs) {
			if (__is_front_mergeable(ei, &en->ei)) {
				f2fs_bug_on(sbi, !den);
				en->ei.fofs = ei->fofs;
				en->ei.blk = ei->blk;
				en->ei.len += ei->len;
				*den = __try_back_merge(sbi, et, en);
				goto update_out;
			}
			p = &(*p)->rb_left;
		} else if (ei->fofs >= en->ei.fofs + en->ei.len) {
			if (__is_back_mergeable(ei, &en->ei)) {
				f2fs_bug_on(sbi, !den);
				en->ei.len += ei->len;
				*den = __try_front_merge(sbi, et, en);
				goto update_out;
			}
			p = &(*p)->rb_right;
		} else {
			f2fs_bug_on(sbi, 1);
		}
	}

	en = __attach_extent_node(sbi, et, ei, parent, p);
	if (!en)
		return NULL;
update_out:
	if (en->ei.len > et->largest.len)
		et->largest = en->ei;
	et->cached_en = en;
	return en;
}

static unsigned int __free_extent_tree(struct f2fs_sb_info *sbi,
					struct extent_tree *et, bool free_all)
{
	struct rb_node *node, *next;
	struct extent_node *en;
	unsigned int count = et->count;

	node = rb_first(&et->root);
	while (node) {
		next = rb_next(node);
		en = rb_entry(node, struct extent_node, rb_node);

		if (free_all) {
			spin_lock(&sbi->extent_lock);
			if (!list_empty(&en->list))
				list_del_init(&en->list);
			spin_unlock(&sbi->extent_lock);
		}

		if (free_all || list_empty(&en->list)) {
			__detach_extent_node(sbi, et, en);
			kmem_cache_free(extent_node_slab, en);
		}
		node = next;
	}

	return count - et->count;
}

void f2fs_drop_largest_extent(struct inode *inode, pgoff_t fofs)
{
	struct extent_info *largest = &F2FS_I(inode)->extent_tree->largest;

	if (largest->fofs <= fofs && largest->fofs + largest->len > fofs)
		largest->len = 0;
}

void f2fs_init_extent_tree(struct inode *inode, struct f2fs_extent *i_ext)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et;
	struct extent_node *en;
	struct extent_info ei;

	if (!f2fs_may_extent_tree(inode))
		return;

	et = __grab_extent_tree(inode);

	if (!i_ext || le32_to_cpu(i_ext->len) < F2FS_MIN_EXTENT_LEN)
		return;

	set_extent_info(&ei, le32_to_cpu(i_ext->fofs),
		le32_to_cpu(i_ext->blk), le32_to_cpu(i_ext->len));

	write_lock(&et->lock);
	if (et->count)
		goto out;

	en = __insert_extent_tree(sbi, et, &ei, NULL);
	if (en) {
		spin_lock(&sbi->extent_lock);
		list_add_tail(&en->list, &sbi->extent_list);
		spin_unlock(&sbi->extent_lock);
	}
out:
	write_unlock(&et->lock);
}

static bool f2fs_lookup_extent_tree(struct inode *inode, pgoff_t pgofs,
							struct extent_info *ei)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree;
	struct extent_node *en;
	bool ret = false;

	f2fs_bug_on(sbi, !et);

	trace_f2fs_lookup_extent_tree_start(inode, pgofs);

	read_lock(&et->lock);

	if (et->largest.fofs <= pgofs &&
			et->largest.fofs + et->largest.len > pgofs) {
		*ei = et->largest;
		ret = true;
		stat_inc_read_hit(sbi);
		goto out;
	}

	en = __lookup_extent_tree(et, pgofs);
	if (en) {
		*ei = en->ei;
		spin_lock(&sbi->extent_lock);
		if (!list_empty(&en->list))
			list_move_tail(&en->list, &sbi->extent_list);
		et->cached_en = en;
		spin_unlock(&sbi->extent_lock);
		ret = true;
		stat_inc_read_hit(sbi);
	}
out:
	stat_inc_total_hit(sbi);
	read_unlock(&et->lock);

	trace_f2fs_lookup_extent_tree_end(inode, pgofs, ei);
	return ret;
}


/*
 * lookup extent at @fofs, if hit, return the extent
 * if not, return NULL and
 * @prev_ex: extent before fofs
 * @next_ex: extent after fofs
 * @insert_p: insert point for new extent at fofs
 * in order to simpfy the insertion after.
 * tree must stay unchanged between lookup and insertion.
 */
static struct extent_node *__lookup_extent_tree_ret(struct extent_tree *et,
				unsigned int fofs, struct extent_node **prev_ex,
				struct extent_node **next_ex,
				struct rb_node ***insert_p,
				struct rb_node **insert_parent)
{
	struct rb_node **pnode = &et->root.rb_node;
	struct rb_node *parent = NULL, *tmp_node;
	struct extent_node *en;

	if (et->cached_en) {
		struct extent_info *cei = &et->cached_en->ei;

		if (cei->fofs <= fofs && cei->fofs + cei->len > fofs)
			return et->cached_en;
	}

	while (*pnode) {
		parent = *pnode;
		en = rb_entry(*pnode, struct extent_node, rb_node);

		if (fofs < en->ei.fofs)
			pnode = &(*pnode)->rb_left;
		else if (fofs >= en->ei.fofs + en->ei.len)
			pnode = &(*pnode)->rb_right;
		else
			return en;
	}

	*insert_p = pnode;
	*insert_parent = parent;

	en = rb_entry(parent, struct extent_node, rb_node);
	tmp_node = parent;
	if (parent && fofs > en->ei.fofs)
		tmp_node = rb_next(parent);
	*next_ex = tmp_node ?
		rb_entry(tmp_node, struct extent_node, rb_node) : NULL;

	tmp_node = parent;
	if (parent && fofs < en->ei.fofs)
		tmp_node = rb_prev(parent);
	*prev_ex = tmp_node ?
		rb_entry(tmp_node, struct extent_node, rb_node) : NULL;

	return NULL;
}

static struct extent_node *__insert_extent_tree_ret(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct extent_node **den,
				struct extent_node *prev_ex,
				struct extent_node *next_ex,
				struct rb_node **insert_p,
				struct rb_node *insert_parent)
{
	struct rb_node **p = &et->root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_node *en = NULL;
	int merged = 0;

	if (prev_ex && __is_back_mergeable(ei, &prev_ex->ei)) {
		f2fs_bug_on(sbi, !den);
		merged = 1;
		prev_ex->ei.len += ei->len;
		ei = &prev_ex->ei;
		en = prev_ex;
	}
	if (next_ex && __is_front_mergeable(ei, &next_ex->ei)) {
		f2fs_bug_on(sbi, !den);
		if (merged++) {
			__detach_extent_node(sbi, et, prev_ex);
			*den = prev_ex;
		}
		next_ex->ei.fofs = ei->fofs;
		next_ex->ei.blk = ei->blk;
		next_ex->ei.len += ei->len;
		en = next_ex;
	}
	if (merged)
		goto update_out;

	if (insert_p && insert_parent) {
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	while (*p) {
		parent = *p;
		en = rb_entry(parent, struct extent_node, rb_node);

		if (ei->fofs < en->ei.fofs)
			p = &(*p)->rb_left;
		else if (ei->fofs >= en->ei.fofs + en->ei.len)
			p = &(*p)->rb_right;
		else
			f2fs_bug_on(sbi, 1);
	}
do_insert:
	en = __attach_extent_node(sbi, et, ei, parent, p);
	if (!en)
		return NULL;
update_out:
	if (en->ei.len > et->largest.len)
		et->largest = en->ei;
	et->cached_en = en;
	return en;
}

/* return true, if on-disk extent should be updated */
static bool f2fs_update_extent_tree(struct inode *inode, pgoff_t fofs,
							block_t blkaddr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree;
	struct extent_node *en = NULL, *en1 = NULL, *en2 = NULL, *en3 = NULL;
	struct extent_node *den = NULL, *prev_ex = NULL, *next_ex = NULL;
	struct extent_info ei, dei, prev;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	unsigned int endofs;

	if (!et)
		return false;

	trace_f2fs_update_extent_tree(inode, fofs, blkaddr);

	write_lock(&et->lock);

	if (is_inode_flag_set(F2FS_I(inode), FI_NO_EXTENT)) {
		write_unlock(&et->lock);
		return false;
	}

	prev = et->largest;
	dei.len = 0;

	/* we do not guarantee that the largest extent is cached all the time */
	f2fs_drop_largest_extent(inode, fofs);

	/* 1. lookup and remove existing extent info in cache */
	en = __lookup_extent_tree_ret(et, fofs, &prev_ex, &next_ex,
					&insert_p, &insert_parent);
	if (!en)
		goto update_extent;

	dei = en->ei;
	__detach_extent_node(sbi, et, en);

	/* 2. if extent can be split, try to split it */
	if (dei.len > F2FS_MIN_EXTENT_LEN) {
		/*  insert left part of split extent into cache */
		if (fofs - dei.fofs >= F2FS_MIN_EXTENT_LEN) {
			set_extent_info(&ei, dei.fofs, dei.blk,
						fofs - dei.fofs);
			en1 = __insert_extent_tree_ret(sbi, et, &ei, NULL,
						NULL, NULL, NULL, NULL);
		}

		/* insert right part of split extent into cache */
		endofs = dei.fofs + dei.len - 1;
		if (endofs - fofs >= F2FS_MIN_EXTENT_LEN) {
			set_extent_info(&ei, fofs + 1,
				fofs - dei.fofs + dei.blk + 1, endofs - fofs);
			en2 = __insert_extent_tree_ret(sbi, et, &ei, NULL,
						NULL, NULL, NULL, NULL);
		}
	}

update_extent:
	/* 3. update extent in extent cache */
	if (blkaddr) {
		set_extent_info(&ei, fofs, blkaddr, 1);
		en3 = __insert_extent_tree_ret(sbi, et, &ei, &den,
				prev_ex, next_ex, insert_p, insert_parent);

		/* give up extent_cache, if split and small updates happen */
		if (dei.len >= 1 &&
				prev.len < F2FS_MIN_EXTENT_LEN &&
				et->largest.len < F2FS_MIN_EXTENT_LEN) {
			et->largest.len = 0;
			set_inode_flag(F2FS_I(inode), FI_NO_EXTENT);
		}
	}

	/* 4. update in global extent list */
	spin_lock(&sbi->extent_lock);
	if (en && !list_empty(&en->list))
		list_del(&en->list);
	/*
	 * en1 and en2 split from en, they will become more and more smaller
	 * fragments after splitting several times. So if the length is smaller
	 * than F2FS_MIN_EXTENT_LEN, we will not add them into extent tree.
	 */
	if (en1)
		list_add_tail(&en1->list, &sbi->extent_list);
	if (en2)
		list_add_tail(&en2->list, &sbi->extent_list);
	if (en3) {
		if (list_empty(&en3->list))
			list_add_tail(&en3->list, &sbi->extent_list);
		else
			list_move_tail(&en3->list, &sbi->extent_list);
	}
	if (den && !list_empty(&den->list))
		list_del(&den->list);
	spin_unlock(&sbi->extent_lock);

	/* 5. release extent node */
	if (en)
		kmem_cache_free(extent_node_slab, en);
	if (den)
		kmem_cache_free(extent_node_slab, den);

	if (is_inode_flag_set(F2FS_I(inode), FI_NO_EXTENT))
		__free_extent_tree(sbi, et, true);

	write_unlock(&et->lock);

	return !__is_extent_same(&prev, &et->largest);
}

unsigned int f2fs_shrink_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct extent_tree *treevec[EXT_TREE_VEC_SIZE];
	struct extent_node *en, *tmp;
	unsigned long ino = F2FS_ROOT_INO(sbi);
	struct radix_tree_root *root = &sbi->extent_tree_root;
	unsigned int found;
	unsigned int node_cnt = 0, tree_cnt = 0;
	int remained;

	if (!test_opt(sbi, EXTENT_CACHE))
		return 0;

	if (!down_write_trylock(&sbi->extent_tree_lock))
		goto out;

	/* 1. remove unreferenced extent tree */
	while ((found = radix_tree_gang_lookup(root,
				(void **)treevec, ino, EXT_TREE_VEC_SIZE))) {
		unsigned i;

		ino = treevec[found - 1]->ino + 1;
		for (i = 0; i < found; i++) {
			struct extent_tree *et = treevec[i];

			if (!atomic_read(&et->refcount)) {
				write_lock(&et->lock);
				node_cnt += __free_extent_tree(sbi, et, true);
				write_unlock(&et->lock);

				radix_tree_delete(root, et->ino);
				kmem_cache_free(extent_tree_slab, et);
				sbi->total_ext_tree--;
				tree_cnt++;

				if (node_cnt + tree_cnt >= nr_shrink)
					goto unlock_out;
			}
		}
	}
	up_write(&sbi->extent_tree_lock);

	/* 2. remove LRU extent entries */
	if (!down_write_trylock(&sbi->extent_tree_lock))
		goto out;

	remained = nr_shrink - (node_cnt + tree_cnt);

	spin_lock(&sbi->extent_lock);
	list_for_each_entry_safe(en, tmp, &sbi->extent_list, list) {
		if (!remained--)
			break;
		list_del_init(&en->list);
	}
	spin_unlock(&sbi->extent_lock);

	while ((found = radix_tree_gang_lookup(root,
				(void **)treevec, ino, EXT_TREE_VEC_SIZE))) {
		unsigned i;

		ino = treevec[found - 1]->ino + 1;
		for (i = 0; i < found; i++) {
			struct extent_tree *et = treevec[i];

			write_lock(&et->lock);
			node_cnt += __free_extent_tree(sbi, et, false);
			write_unlock(&et->lock);

			if (node_cnt + tree_cnt >= nr_shrink)
				break;
		}
	}
unlock_out:
	up_write(&sbi->extent_tree_lock);
out:
	trace_f2fs_shrink_extent_tree(sbi, node_cnt, tree_cnt);

	return node_cnt + tree_cnt;
}

unsigned int f2fs_destroy_extent_node(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree;
	unsigned int node_cnt = 0;

	if (!et)
		return 0;

	write_lock(&et->lock);
	node_cnt = __free_extent_tree(sbi, et, true);
	write_unlock(&et->lock);

	return node_cnt;
}

void f2fs_destroy_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree;
	unsigned int node_cnt = 0;

	if (!et)
		return;

	if (inode->i_nlink && !is_bad_inode(inode) && et->count) {
		atomic_dec(&et->refcount);
		return;
	}

	/* free all extent info belong to this extent tree */
	node_cnt = f2fs_destroy_extent_node(inode);

	/* delete extent tree entry in radix tree */
	down_write(&sbi->extent_tree_lock);
	atomic_dec(&et->refcount);
	f2fs_bug_on(sbi, atomic_read(&et->refcount) || et->count);
	radix_tree_delete(&sbi->extent_tree_root, inode->i_ino);
	kmem_cache_free(extent_tree_slab, et);
	sbi->total_ext_tree--;
	up_write(&sbi->extent_tree_lock);

	F2FS_I(inode)->extent_tree = NULL;

	trace_f2fs_destroy_extent_tree(inode, node_cnt);
}

bool f2fs_lookup_extent_cache(struct inode *inode, pgoff_t pgofs,
					struct extent_info *ei)
{
	if (!f2fs_may_extent_tree(inode))
		return false;

	return f2fs_lookup_extent_tree(inode, pgofs, ei);
}

void f2fs_update_extent_cache(struct dnode_of_data *dn)
{
	struct f2fs_inode_info *fi = F2FS_I(dn->inode);
	pgoff_t fofs;

	if (!f2fs_may_extent_tree(dn->inode))
		return;

	f2fs_bug_on(F2FS_I_SB(dn->inode), dn->data_blkaddr == NEW_ADDR);

	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), fi) +
							dn->ofs_in_node;

	if (f2fs_update_extent_tree(dn->inode, fofs, dn->data_blkaddr))
		sync_inode_page(dn);
}

void init_extent_cache_info(struct f2fs_sb_info *sbi)
{
	INIT_RADIX_TREE(&sbi->extent_tree_root, GFP_NOIO);
	init_rwsem(&sbi->extent_tree_lock);
	INIT_LIST_HEAD(&sbi->extent_list);
	spin_lock_init(&sbi->extent_lock);
	sbi->total_ext_tree = 0;
	atomic_set(&sbi->total_ext_node, 0);
}

int __init create_extent_cache(void)
{
	extent_tree_slab = f2fs_kmem_cache_create("f2fs_extent_tree",
			sizeof(struct extent_tree));
	if (!extent_tree_slab)
		return -ENOMEM;
	extent_node_slab = f2fs_kmem_cache_create("f2fs_extent_node",
			sizeof(struct extent_node));
	if (!extent_node_slab) {
		kmem_cache_destroy(extent_tree_slab);
		return -ENOMEM;
	}
	return 0;
}

void destroy_extent_cache(void)
{
	kmem_cache_destroy(extent_node_slab);
	kmem_cache_destroy(extent_tree_slab);
}
