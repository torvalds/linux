// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs extent cache support
 *
 * Copyright (c) 2015 Motorola Mobility
 * Copyright (c) 2015 Samsung Electronics
 * Authors: Jaegeuk Kim <jaegeuk@kernel.org>
 *          Chao Yu <chao2.yu@samsung.com>
 *
 * block_age-based extent cache added by:
 * Copyright (c) 2022 xiaomi Co., Ltd.
 *             http://www.xiaomi.com/
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "node.h"
#include <trace/events/f2fs.h>

bool sanity_check_extent_cache(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct extent_tree *et = fi->extent_tree[EX_READ];
	struct extent_info *ei;

	if (!et)
		return true;

	ei = &et->largest;
	if (!ei->len)
		return true;

	/* Let's drop, if checkpoint got corrupted. */
	if (is_set_ckpt_flags(sbi, CP_ERROR_FLAG)) {
		ei->len = 0;
		et->largest_updated = true;
		return true;
	}

	if (!f2fs_is_valid_blkaddr(sbi, ei->blk, DATA_GENERIC_ENHANCE) ||
	    !f2fs_is_valid_blkaddr(sbi, ei->blk + ei->len - 1,
					DATA_GENERIC_ENHANCE)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: inode (ino=%lx) extent info [%u, %u, %u] is incorrect, run fsck to fix",
			  __func__, inode->i_ino,
			  ei->blk, ei->fofs, ei->len);
		return false;
	}
	return true;
}

static void __set_extent_info(struct extent_info *ei,
				unsigned int fofs, unsigned int len,
				block_t blk, bool keep_clen,
				unsigned long age, unsigned long last_blocks,
				enum extent_type type)
{
	ei->fofs = fofs;
	ei->len = len;

	if (type == EX_READ) {
		ei->blk = blk;
		if (keep_clen)
			return;
#ifdef CONFIG_F2FS_FS_COMPRESSION
		ei->c_len = 0;
#endif
	} else if (type == EX_BLOCK_AGE) {
		ei->age = age;
		ei->last_blocks = last_blocks;
	}
}

static bool __may_read_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (!test_opt(sbi, READ_EXTENT_CACHE))
		return false;
	if (is_inode_flag_set(inode, FI_NO_EXTENT))
		return false;
	if (is_inode_flag_set(inode, FI_COMPRESSED_FILE) &&
			 !f2fs_sb_has_readonly(sbi))
		return false;
	return S_ISREG(inode->i_mode);
}

static bool __may_age_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (!test_opt(sbi, AGE_EXTENT_CACHE))
		return false;
	/* don't cache block age info for cold file */
	if (is_inode_flag_set(inode, FI_COMPRESSED_FILE))
		return false;
	if (file_is_cold(inode))
		return false;

	return S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode);
}

static bool __init_may_extent_tree(struct inode *inode, enum extent_type type)
{
	if (type == EX_READ)
		return __may_read_extent_tree(inode);
	else if (type == EX_BLOCK_AGE)
		return __may_age_extent_tree(inode);
	return false;
}

static bool __may_extent_tree(struct inode *inode, enum extent_type type)
{
	/*
	 * for recovered files during mount do not create extents
	 * if shrinker is not registered.
	 */
	if (list_empty(&F2FS_I_SB(inode)->s_list))
		return false;

	return __init_may_extent_tree(inode, type);
}

static void __try_update_largest_extent(struct extent_tree *et,
						struct extent_node *en)
{
	if (et->type != EX_READ)
		return;
	if (en->ei.len <= et->largest.len)
		return;

	et->largest = en->ei;
	et->largest_updated = true;
}

static bool __is_extent_mergeable(struct extent_info *back,
		struct extent_info *front, enum extent_type type)
{
	if (type == EX_READ) {
#ifdef CONFIG_F2FS_FS_COMPRESSION
		if (back->c_len && back->len != back->c_len)
			return false;
		if (front->c_len && front->len != front->c_len)
			return false;
#endif
		return (back->fofs + back->len == front->fofs &&
				back->blk + back->len == front->blk);
	} else if (type == EX_BLOCK_AGE) {
		return (back->fofs + back->len == front->fofs &&
			abs(back->age - front->age) <= SAME_AGE_REGION &&
			abs(back->last_blocks - front->last_blocks) <=
							SAME_AGE_REGION);
	}
	return false;
}

static bool __is_back_mergeable(struct extent_info *cur,
		struct extent_info *back, enum extent_type type)
{
	return __is_extent_mergeable(back, cur, type);
}

static bool __is_front_mergeable(struct extent_info *cur,
		struct extent_info *front, enum extent_type type)
{
	return __is_extent_mergeable(cur, front, type);
}

static struct extent_node *__lookup_extent_node(struct rb_root_cached *root,
			struct extent_node *cached_en, unsigned int fofs)
{
	struct rb_node *node = root->rb_root.rb_node;
	struct extent_node *en;

	/* check a cached entry */
	if (cached_en && cached_en->ei.fofs <= fofs &&
			cached_en->ei.fofs + cached_en->ei.len > fofs)
		return cached_en;

	/* check rb_tree */
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

/*
 * lookup rb entry in position of @fofs in rb-tree,
 * if hit, return the entry, otherwise, return NULL
 * @prev_ex: extent before fofs
 * @next_ex: extent after fofs
 * @insert_p: insert point for new extent at fofs
 * in order to simplify the insertion after.
 * tree must stay unchanged between lookup and insertion.
 */
static struct extent_node *__lookup_extent_node_ret(struct rb_root_cached *root,
				struct extent_node *cached_en,
				unsigned int fofs,
				struct extent_node **prev_entry,
				struct extent_node **next_entry,
				struct rb_node ***insert_p,
				struct rb_node **insert_parent,
				bool *leftmost)
{
	struct rb_node **pnode = &root->rb_root.rb_node;
	struct rb_node *parent = NULL, *tmp_node;
	struct extent_node *en = cached_en;

	*insert_p = NULL;
	*insert_parent = NULL;
	*prev_entry = NULL;
	*next_entry = NULL;

	if (RB_EMPTY_ROOT(&root->rb_root))
		return NULL;

	if (en && en->ei.fofs <= fofs && en->ei.fofs + en->ei.len > fofs)
		goto lookup_neighbors;

	*leftmost = true;

	while (*pnode) {
		parent = *pnode;
		en = rb_entry(*pnode, struct extent_node, rb_node);

		if (fofs < en->ei.fofs) {
			pnode = &(*pnode)->rb_left;
		} else if (fofs >= en->ei.fofs + en->ei.len) {
			pnode = &(*pnode)->rb_right;
			*leftmost = false;
		} else {
			goto lookup_neighbors;
		}
	}

	*insert_p = pnode;
	*insert_parent = parent;

	en = rb_entry(parent, struct extent_node, rb_node);
	tmp_node = parent;
	if (parent && fofs > en->ei.fofs)
		tmp_node = rb_next(parent);
	*next_entry = rb_entry_safe(tmp_node, struct extent_node, rb_node);

	tmp_node = parent;
	if (parent && fofs < en->ei.fofs)
		tmp_node = rb_prev(parent);
	*prev_entry = rb_entry_safe(tmp_node, struct extent_node, rb_node);
	return NULL;

lookup_neighbors:
	if (fofs == en->ei.fofs) {
		/* lookup prev node for merging backward later */
		tmp_node = rb_prev(&en->rb_node);
		*prev_entry = rb_entry_safe(tmp_node,
					struct extent_node, rb_node);
	}
	if (fofs == en->ei.fofs + en->ei.len - 1) {
		/* lookup next node for merging frontward later */
		tmp_node = rb_next(&en->rb_node);
		*next_entry = rb_entry_safe(tmp_node,
					struct extent_node, rb_node);
	}
	return en;
}

static struct kmem_cache *extent_tree_slab;
static struct kmem_cache *extent_node_slab;

static struct extent_node *__attach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_node *parent, struct rb_node **p,
				bool leftmost)
{
	struct extent_tree_info *eti = &sbi->extent_tree[et->type];
	struct extent_node *en;

	en = f2fs_kmem_cache_alloc(extent_node_slab, GFP_ATOMIC, false, sbi);
	if (!en)
		return NULL;

	en->ei = *ei;
	INIT_LIST_HEAD(&en->list);
	en->et = et;

	rb_link_node(&en->rb_node, parent, p);
	rb_insert_color_cached(&en->rb_node, &et->root, leftmost);
	atomic_inc(&et->node_cnt);
	atomic_inc(&eti->total_ext_node);
	return en;
}

static void __detach_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_node *en)
{
	struct extent_tree_info *eti = &sbi->extent_tree[et->type];

	rb_erase_cached(&en->rb_node, &et->root);
	atomic_dec(&et->node_cnt);
	atomic_dec(&eti->total_ext_node);

	if (et->cached_en == en)
		et->cached_en = NULL;
	kmem_cache_free(extent_node_slab, en);
}

/*
 * Flow to release an extent_node:
 * 1. list_del_init
 * 2. __detach_extent_node
 * 3. kmem_cache_free.
 */
static void __release_extent_node(struct f2fs_sb_info *sbi,
			struct extent_tree *et, struct extent_node *en)
{
	struct extent_tree_info *eti = &sbi->extent_tree[et->type];

	spin_lock(&eti->extent_lock);
	f2fs_bug_on(sbi, list_empty(&en->list));
	list_del_init(&en->list);
	spin_unlock(&eti->extent_lock);

	__detach_extent_node(sbi, et, en);
}

static struct extent_tree *__grab_extent_tree(struct inode *inode,
						enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree_info *eti = &sbi->extent_tree[type];
	struct extent_tree *et;
	nid_t ino = inode->i_ino;

	mutex_lock(&eti->extent_tree_lock);
	et = radix_tree_lookup(&eti->extent_tree_root, ino);
	if (!et) {
		et = f2fs_kmem_cache_alloc(extent_tree_slab,
					GFP_NOFS, true, NULL);
		f2fs_radix_tree_insert(&eti->extent_tree_root, ino, et);
		memset(et, 0, sizeof(struct extent_tree));
		et->ino = ino;
		et->type = type;
		et->root = RB_ROOT_CACHED;
		et->cached_en = NULL;
		rwlock_init(&et->lock);
		INIT_LIST_HEAD(&et->list);
		atomic_set(&et->node_cnt, 0);
		atomic_inc(&eti->total_ext_tree);
	} else {
		atomic_dec(&eti->total_zombie_tree);
		list_del_init(&et->list);
	}
	mutex_unlock(&eti->extent_tree_lock);

	/* never died until evict_inode */
	F2FS_I(inode)->extent_tree[type] = et;

	return et;
}

static unsigned int __free_extent_tree(struct f2fs_sb_info *sbi,
					struct extent_tree *et)
{
	struct rb_node *node, *next;
	struct extent_node *en;
	unsigned int count = atomic_read(&et->node_cnt);

	node = rb_first_cached(&et->root);
	while (node) {
		next = rb_next(node);
		en = rb_entry(node, struct extent_node, rb_node);
		__release_extent_node(sbi, et, en);
		node = next;
	}

	return count - atomic_read(&et->node_cnt);
}

static void __drop_largest_extent(struct extent_tree *et,
					pgoff_t fofs, unsigned int len)
{
	if (fofs < et->largest.fofs + et->largest.len &&
			fofs + len > et->largest.fofs) {
		et->largest.len = 0;
		et->largest_updated = true;
	}
}

void f2fs_init_read_extent_tree(struct inode *inode, struct page *ipage)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree_info *eti = &sbi->extent_tree[EX_READ];
	struct f2fs_extent *i_ext = &F2FS_INODE(ipage)->i_ext;
	struct extent_tree *et;
	struct extent_node *en;
	struct extent_info ei;

	if (!__may_extent_tree(inode, EX_READ)) {
		/* drop largest read extent */
		if (i_ext && i_ext->len) {
			f2fs_wait_on_page_writeback(ipage, NODE, true, true);
			i_ext->len = 0;
			set_page_dirty(ipage);
		}
		goto out;
	}

	et = __grab_extent_tree(inode, EX_READ);

	if (!i_ext || !i_ext->len)
		goto out;

	get_read_extent_info(&ei, i_ext);

	write_lock(&et->lock);
	if (atomic_read(&et->node_cnt))
		goto unlock_out;

	en = __attach_extent_node(sbi, et, &ei, NULL,
				&et->root.rb_root.rb_node, true);
	if (en) {
		et->largest = en->ei;
		et->cached_en = en;

		spin_lock(&eti->extent_lock);
		list_add_tail(&en->list, &eti->extent_list);
		spin_unlock(&eti->extent_lock);
	}
unlock_out:
	write_unlock(&et->lock);
out:
	if (!F2FS_I(inode)->extent_tree[EX_READ])
		set_inode_flag(inode, FI_NO_EXTENT);
}

void f2fs_init_age_extent_tree(struct inode *inode)
{
	if (!__init_may_extent_tree(inode, EX_BLOCK_AGE))
		return;
	__grab_extent_tree(inode, EX_BLOCK_AGE);
}

void f2fs_init_extent_tree(struct inode *inode)
{
	/* initialize read cache */
	if (__init_may_extent_tree(inode, EX_READ))
		__grab_extent_tree(inode, EX_READ);

	/* initialize block age cache */
	if (__init_may_extent_tree(inode, EX_BLOCK_AGE))
		__grab_extent_tree(inode, EX_BLOCK_AGE);
}

static bool __lookup_extent_tree(struct inode *inode, pgoff_t pgofs,
			struct extent_info *ei, enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree_info *eti = &sbi->extent_tree[type];
	struct extent_tree *et = F2FS_I(inode)->extent_tree[type];
	struct extent_node *en;
	bool ret = false;

	if (!et)
		return false;

	trace_f2fs_lookup_extent_tree_start(inode, pgofs, type);

	read_lock(&et->lock);

	if (type == EX_READ &&
			et->largest.fofs <= pgofs &&
			et->largest.fofs + et->largest.len > pgofs) {
		*ei = et->largest;
		ret = true;
		stat_inc_largest_node_hit(sbi);
		goto out;
	}

	en = __lookup_extent_node(&et->root, et->cached_en, pgofs);
	if (!en)
		goto out;

	if (en == et->cached_en)
		stat_inc_cached_node_hit(sbi, type);
	else
		stat_inc_rbtree_node_hit(sbi, type);

	*ei = en->ei;
	spin_lock(&eti->extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &eti->extent_list);
		et->cached_en = en;
	}
	spin_unlock(&eti->extent_lock);
	ret = true;
out:
	stat_inc_total_hit(sbi, type);
	read_unlock(&et->lock);

	if (type == EX_READ)
		trace_f2fs_lookup_read_extent_tree_end(inode, pgofs, ei);
	else if (type == EX_BLOCK_AGE)
		trace_f2fs_lookup_age_extent_tree_end(inode, pgofs, ei);
	return ret;
}

static struct extent_node *__try_merge_extent_node(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct extent_node *prev_ex,
				struct extent_node *next_ex)
{
	struct extent_tree_info *eti = &sbi->extent_tree[et->type];
	struct extent_node *en = NULL;

	if (prev_ex && __is_back_mergeable(ei, &prev_ex->ei, et->type)) {
		prev_ex->ei.len += ei->len;
		ei = &prev_ex->ei;
		en = prev_ex;
	}

	if (next_ex && __is_front_mergeable(ei, &next_ex->ei, et->type)) {
		next_ex->ei.fofs = ei->fofs;
		next_ex->ei.len += ei->len;
		if (et->type == EX_READ)
			next_ex->ei.blk = ei->blk;
		if (en)
			__release_extent_node(sbi, et, prev_ex);

		en = next_ex;
	}

	if (!en)
		return NULL;

	__try_update_largest_extent(et, en);

	spin_lock(&eti->extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &eti->extent_list);
		et->cached_en = en;
	}
	spin_unlock(&eti->extent_lock);
	return en;
}

static struct extent_node *__insert_extent_tree(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_node **insert_p,
				struct rb_node *insert_parent,
				bool leftmost)
{
	struct extent_tree_info *eti = &sbi->extent_tree[et->type];
	struct rb_node **p = &et->root.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_node *en = NULL;

	if (insert_p && insert_parent) {
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	leftmost = true;

	/* look up extent_node in the rb tree */
	while (*p) {
		parent = *p;
		en = rb_entry(parent, struct extent_node, rb_node);

		if (ei->fofs < en->ei.fofs) {
			p = &(*p)->rb_left;
		} else if (ei->fofs >= en->ei.fofs + en->ei.len) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else {
			f2fs_bug_on(sbi, 1);
		}
	}

do_insert:
	en = __attach_extent_node(sbi, et, ei, parent, p, leftmost);
	if (!en)
		return NULL;

	__try_update_largest_extent(et, en);

	/* update in global extent list */
	spin_lock(&eti->extent_lock);
	list_add_tail(&en->list, &eti->extent_list);
	et->cached_en = en;
	spin_unlock(&eti->extent_lock);
	return en;
}

static void __update_extent_tree_range(struct inode *inode,
			struct extent_info *tei, enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree[type];
	struct extent_node *en = NULL, *en1 = NULL;
	struct extent_node *prev_en = NULL, *next_en = NULL;
	struct extent_info ei, dei, prev;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	unsigned int fofs = tei->fofs, len = tei->len;
	unsigned int end = fofs + len;
	bool updated = false;
	bool leftmost = false;

	if (!et)
		return;

	if (type == EX_READ)
		trace_f2fs_update_read_extent_tree_range(inode, fofs, len,
						tei->blk, 0);
	else if (type == EX_BLOCK_AGE)
		trace_f2fs_update_age_extent_tree_range(inode, fofs, len,
						tei->age, tei->last_blocks);

	write_lock(&et->lock);

	if (type == EX_READ) {
		if (is_inode_flag_set(inode, FI_NO_EXTENT)) {
			write_unlock(&et->lock);
			return;
		}

		prev = et->largest;
		dei.len = 0;

		/*
		 * drop largest extent before lookup, in case it's already
		 * been shrunk from extent tree
		 */
		__drop_largest_extent(et, fofs, len);
	}

	/* 1. lookup first extent node in range [fofs, fofs + len - 1] */
	en = __lookup_extent_node_ret(&et->root,
					et->cached_en, fofs,
					&prev_en, &next_en,
					&insert_p, &insert_parent,
					&leftmost);
	if (!en)
		en = next_en;

	/* 2. invalidate all extent nodes in range [fofs, fofs + len - 1] */
	while (en && en->ei.fofs < end) {
		unsigned int org_end;
		int parts = 0;	/* # of parts current extent split into */

		next_en = en1 = NULL;

		dei = en->ei;
		org_end = dei.fofs + dei.len;
		f2fs_bug_on(sbi, fofs >= org_end);

		if (fofs > dei.fofs && (type != EX_READ ||
				fofs - dei.fofs >= F2FS_MIN_EXTENT_LEN)) {
			en->ei.len = fofs - en->ei.fofs;
			prev_en = en;
			parts = 1;
		}

		if (end < org_end && (type != EX_READ ||
				org_end - end >= F2FS_MIN_EXTENT_LEN)) {
			if (parts) {
				__set_extent_info(&ei,
					end, org_end - end,
					end - dei.fofs + dei.blk, false,
					dei.age, dei.last_blocks,
					type);
				en1 = __insert_extent_tree(sbi, et, &ei,
							NULL, NULL, true);
				next_en = en1;
			} else {
				__set_extent_info(&en->ei,
					end, en->ei.len - (end - dei.fofs),
					en->ei.blk + (end - dei.fofs), true,
					dei.age, dei.last_blocks,
					type);
				next_en = en;
			}
			parts++;
		}

		if (!next_en) {
			struct rb_node *node = rb_next(&en->rb_node);

			next_en = rb_entry_safe(node, struct extent_node,
						rb_node);
		}

		if (parts)
			__try_update_largest_extent(et, en);
		else
			__release_extent_node(sbi, et, en);

		/*
		 * if original extent is split into zero or two parts, extent
		 * tree has been altered by deletion or insertion, therefore
		 * invalidate pointers regard to tree.
		 */
		if (parts != 1) {
			insert_p = NULL;
			insert_parent = NULL;
		}
		en = next_en;
	}

	if (type == EX_BLOCK_AGE)
		goto update_age_extent_cache;

	/* 3. update extent in read extent cache */
	BUG_ON(type != EX_READ);

	if (tei->blk) {
		__set_extent_info(&ei, fofs, len, tei->blk, false,
				  0, 0, EX_READ);
		if (!__try_merge_extent_node(sbi, et, &ei, prev_en, next_en))
			__insert_extent_tree(sbi, et, &ei,
					insert_p, insert_parent, leftmost);

		/* give up extent_cache, if split and small updates happen */
		if (dei.len >= 1 &&
				prev.len < F2FS_MIN_EXTENT_LEN &&
				et->largest.len < F2FS_MIN_EXTENT_LEN) {
			et->largest.len = 0;
			et->largest_updated = true;
			set_inode_flag(inode, FI_NO_EXTENT);
		}
	}

	if (is_inode_flag_set(inode, FI_NO_EXTENT))
		__free_extent_tree(sbi, et);

	if (et->largest_updated) {
		et->largest_updated = false;
		updated = true;
	}
	goto out_read_extent_cache;
update_age_extent_cache:
	if (!tei->last_blocks)
		goto out_read_extent_cache;

	__set_extent_info(&ei, fofs, len, 0, false,
			tei->age, tei->last_blocks, EX_BLOCK_AGE);
	if (!__try_merge_extent_node(sbi, et, &ei, prev_en, next_en))
		__insert_extent_tree(sbi, et, &ei,
					insert_p, insert_parent, leftmost);
out_read_extent_cache:
	write_unlock(&et->lock);

	if (updated)
		f2fs_mark_inode_dirty_sync(inode, true);
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
void f2fs_update_read_extent_tree_range_compressed(struct inode *inode,
				pgoff_t fofs, block_t blkaddr, unsigned int llen,
				unsigned int c_len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree[EX_READ];
	struct extent_node *en = NULL;
	struct extent_node *prev_en = NULL, *next_en = NULL;
	struct extent_info ei;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	bool leftmost = false;

	trace_f2fs_update_read_extent_tree_range(inode, fofs, llen,
						blkaddr, c_len);

	/* it is safe here to check FI_NO_EXTENT w/o et->lock in ro image */
	if (is_inode_flag_set(inode, FI_NO_EXTENT))
		return;

	write_lock(&et->lock);

	en = __lookup_extent_node_ret(&et->root,
					et->cached_en, fofs,
					&prev_en, &next_en,
					&insert_p, &insert_parent,
					&leftmost);
	if (en)
		goto unlock_out;

	__set_extent_info(&ei, fofs, llen, blkaddr, true, 0, 0, EX_READ);
	ei.c_len = c_len;

	if (!__try_merge_extent_node(sbi, et, &ei, prev_en, next_en))
		__insert_extent_tree(sbi, et, &ei,
				insert_p, insert_parent, leftmost);
unlock_out:
	write_unlock(&et->lock);
}
#endif

static unsigned long long __calculate_block_age(struct f2fs_sb_info *sbi,
						unsigned long long new,
						unsigned long long old)
{
	unsigned int rem_old, rem_new;
	unsigned long long res;
	unsigned int weight = sbi->last_age_weight;

	res = div_u64_rem(new, 100, &rem_new) * (100 - weight)
		+ div_u64_rem(old, 100, &rem_old) * weight;

	if (rem_new)
		res += rem_new * (100 - weight) / 100;
	if (rem_old)
		res += rem_old * weight / 100;

	return res;
}

/* This returns a new age and allocated blocks in ei */
static int __get_new_block_age(struct inode *inode, struct extent_info *ei,
						block_t blkaddr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	loff_t f_size = i_size_read(inode);
	unsigned long long cur_blocks =
				atomic64_read(&sbi->allocated_data_blocks);
	struct extent_info tei = *ei;	/* only fofs and len are valid */

	/*
	 * When I/O is not aligned to a PAGE_SIZE, update will happen to the last
	 * file block even in seq write. So don't record age for newly last file
	 * block here.
	 */
	if ((f_size >> PAGE_SHIFT) == ei->fofs && f_size & (PAGE_SIZE - 1) &&
			blkaddr == NEW_ADDR)
		return -EINVAL;

	if (__lookup_extent_tree(inode, ei->fofs, &tei, EX_BLOCK_AGE)) {
		unsigned long long cur_age;

		if (cur_blocks >= tei.last_blocks)
			cur_age = cur_blocks - tei.last_blocks;
		else
			/* allocated_data_blocks overflow */
			cur_age = ULLONG_MAX - tei.last_blocks + cur_blocks;

		if (tei.age)
			ei->age = __calculate_block_age(sbi, cur_age, tei.age);
		else
			ei->age = cur_age;
		ei->last_blocks = cur_blocks;
		WARN_ON(ei->age > cur_blocks);
		return 0;
	}

	f2fs_bug_on(sbi, blkaddr == NULL_ADDR);

	/* the data block was allocated for the first time */
	if (blkaddr == NEW_ADDR)
		goto out;

	if (__is_valid_data_blkaddr(blkaddr) &&
	    !f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC_ENHANCE)) {
		f2fs_bug_on(sbi, 1);
		return -EINVAL;
	}
out:
	/*
	 * init block age with zero, this can happen when the block age extent
	 * was reclaimed due to memory constraint or system reboot
	 */
	ei->age = 0;
	ei->last_blocks = cur_blocks;
	return 0;
}

static void __update_extent_cache(struct dnode_of_data *dn, enum extent_type type)
{
	struct extent_info ei = {};

	if (!__may_extent_tree(dn->inode, type))
		return;

	ei.fofs = f2fs_start_bidx_of_node(ofs_of_node(dn->node_page), dn->inode) +
								dn->ofs_in_node;
	ei.len = 1;

	if (type == EX_READ) {
		if (dn->data_blkaddr == NEW_ADDR)
			ei.blk = NULL_ADDR;
		else
			ei.blk = dn->data_blkaddr;
	} else if (type == EX_BLOCK_AGE) {
		if (__get_new_block_age(dn->inode, &ei, dn->data_blkaddr))
			return;
	}
	__update_extent_tree_range(dn->inode, &ei, type);
}

static unsigned int __shrink_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink,
					enum extent_type type)
{
	struct extent_tree_info *eti = &sbi->extent_tree[type];
	struct extent_tree *et, *next;
	struct extent_node *en;
	unsigned int node_cnt = 0, tree_cnt = 0;
	int remained;

	if (!atomic_read(&eti->total_zombie_tree))
		goto free_node;

	if (!mutex_trylock(&eti->extent_tree_lock))
		goto out;

	/* 1. remove unreferenced extent tree */
	list_for_each_entry_safe(et, next, &eti->zombie_list, list) {
		if (atomic_read(&et->node_cnt)) {
			write_lock(&et->lock);
			node_cnt += __free_extent_tree(sbi, et);
			write_unlock(&et->lock);
		}
		f2fs_bug_on(sbi, atomic_read(&et->node_cnt));
		list_del_init(&et->list);
		radix_tree_delete(&eti->extent_tree_root, et->ino);
		kmem_cache_free(extent_tree_slab, et);
		atomic_dec(&eti->total_ext_tree);
		atomic_dec(&eti->total_zombie_tree);
		tree_cnt++;

		if (node_cnt + tree_cnt >= nr_shrink)
			goto unlock_out;
		cond_resched();
	}
	mutex_unlock(&eti->extent_tree_lock);

free_node:
	/* 2. remove LRU extent entries */
	if (!mutex_trylock(&eti->extent_tree_lock))
		goto out;

	remained = nr_shrink - (node_cnt + tree_cnt);

	spin_lock(&eti->extent_lock);
	for (; remained > 0; remained--) {
		if (list_empty(&eti->extent_list))
			break;
		en = list_first_entry(&eti->extent_list,
					struct extent_node, list);
		et = en->et;
		if (!write_trylock(&et->lock)) {
			/* refresh this extent node's position in extent list */
			list_move_tail(&en->list, &eti->extent_list);
			continue;
		}

		list_del_init(&en->list);
		spin_unlock(&eti->extent_lock);

		__detach_extent_node(sbi, et, en);

		write_unlock(&et->lock);
		node_cnt++;
		spin_lock(&eti->extent_lock);
	}
	spin_unlock(&eti->extent_lock);

unlock_out:
	mutex_unlock(&eti->extent_tree_lock);
out:
	trace_f2fs_shrink_extent_tree(sbi, node_cnt, tree_cnt, type);

	return node_cnt + tree_cnt;
}

/* read extent cache operations */
bool f2fs_lookup_read_extent_cache(struct inode *inode, pgoff_t pgofs,
				struct extent_info *ei)
{
	if (!__may_extent_tree(inode, EX_READ))
		return false;

	return __lookup_extent_tree(inode, pgofs, ei, EX_READ);
}

bool f2fs_lookup_read_extent_cache_block(struct inode *inode, pgoff_t index,
				block_t *blkaddr)
{
	struct extent_info ei = {};

	if (!f2fs_lookup_read_extent_cache(inode, index, &ei))
		return false;
	*blkaddr = ei.blk + index - ei.fofs;
	return true;
}

void f2fs_update_read_extent_cache(struct dnode_of_data *dn)
{
	return __update_extent_cache(dn, EX_READ);
}

void f2fs_update_read_extent_cache_range(struct dnode_of_data *dn,
				pgoff_t fofs, block_t blkaddr, unsigned int len)
{
	struct extent_info ei = {
		.fofs = fofs,
		.len = len,
		.blk = blkaddr,
	};

	if (!__may_extent_tree(dn->inode, EX_READ))
		return;

	__update_extent_tree_range(dn->inode, &ei, EX_READ);
}

unsigned int f2fs_shrink_read_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	if (!test_opt(sbi, READ_EXTENT_CACHE))
		return 0;

	return __shrink_extent_tree(sbi, nr_shrink, EX_READ);
}

/* block age extent cache operations */
bool f2fs_lookup_age_extent_cache(struct inode *inode, pgoff_t pgofs,
				struct extent_info *ei)
{
	if (!__may_extent_tree(inode, EX_BLOCK_AGE))
		return false;

	return __lookup_extent_tree(inode, pgofs, ei, EX_BLOCK_AGE);
}

void f2fs_update_age_extent_cache(struct dnode_of_data *dn)
{
	return __update_extent_cache(dn, EX_BLOCK_AGE);
}

void f2fs_update_age_extent_cache_range(struct dnode_of_data *dn,
				pgoff_t fofs, unsigned int len)
{
	struct extent_info ei = {
		.fofs = fofs,
		.len = len,
	};

	if (!__may_extent_tree(dn->inode, EX_BLOCK_AGE))
		return;

	__update_extent_tree_range(dn->inode, &ei, EX_BLOCK_AGE);
}

unsigned int f2fs_shrink_age_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	if (!test_opt(sbi, AGE_EXTENT_CACHE))
		return 0;

	return __shrink_extent_tree(sbi, nr_shrink, EX_BLOCK_AGE);
}

static unsigned int __destroy_extent_node(struct inode *inode,
					enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree[type];
	unsigned int node_cnt = 0;

	if (!et || !atomic_read(&et->node_cnt))
		return 0;

	write_lock(&et->lock);
	node_cnt = __free_extent_tree(sbi, et);
	write_unlock(&et->lock);

	return node_cnt;
}

void f2fs_destroy_extent_node(struct inode *inode)
{
	__destroy_extent_node(inode, EX_READ);
	__destroy_extent_node(inode, EX_BLOCK_AGE);
}

static void __drop_extent_tree(struct inode *inode, enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree *et = F2FS_I(inode)->extent_tree[type];
	bool updated = false;

	if (!__may_extent_tree(inode, type))
		return;

	write_lock(&et->lock);
	__free_extent_tree(sbi, et);
	if (type == EX_READ) {
		set_inode_flag(inode, FI_NO_EXTENT);
		if (et->largest.len) {
			et->largest.len = 0;
			updated = true;
		}
	}
	write_unlock(&et->lock);
	if (updated)
		f2fs_mark_inode_dirty_sync(inode, true);
}

void f2fs_drop_extent_tree(struct inode *inode)
{
	__drop_extent_tree(inode, EX_READ);
	__drop_extent_tree(inode, EX_BLOCK_AGE);
}

static void __destroy_extent_tree(struct inode *inode, enum extent_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct extent_tree_info *eti = &sbi->extent_tree[type];
	struct extent_tree *et = F2FS_I(inode)->extent_tree[type];
	unsigned int node_cnt = 0;

	if (!et)
		return;

	if (inode->i_nlink && !is_bad_inode(inode) &&
					atomic_read(&et->node_cnt)) {
		mutex_lock(&eti->extent_tree_lock);
		list_add_tail(&et->list, &eti->zombie_list);
		atomic_inc(&eti->total_zombie_tree);
		mutex_unlock(&eti->extent_tree_lock);
		return;
	}

	/* free all extent info belong to this extent tree */
	node_cnt = __destroy_extent_node(inode, type);

	/* delete extent tree entry in radix tree */
	mutex_lock(&eti->extent_tree_lock);
	f2fs_bug_on(sbi, atomic_read(&et->node_cnt));
	radix_tree_delete(&eti->extent_tree_root, inode->i_ino);
	kmem_cache_free(extent_tree_slab, et);
	atomic_dec(&eti->total_ext_tree);
	mutex_unlock(&eti->extent_tree_lock);

	F2FS_I(inode)->extent_tree[type] = NULL;

	trace_f2fs_destroy_extent_tree(inode, node_cnt, type);
}

void f2fs_destroy_extent_tree(struct inode *inode)
{
	__destroy_extent_tree(inode, EX_READ);
	__destroy_extent_tree(inode, EX_BLOCK_AGE);
}

static void __init_extent_tree_info(struct extent_tree_info *eti)
{
	INIT_RADIX_TREE(&eti->extent_tree_root, GFP_NOIO);
	mutex_init(&eti->extent_tree_lock);
	INIT_LIST_HEAD(&eti->extent_list);
	spin_lock_init(&eti->extent_lock);
	atomic_set(&eti->total_ext_tree, 0);
	INIT_LIST_HEAD(&eti->zombie_list);
	atomic_set(&eti->total_zombie_tree, 0);
	atomic_set(&eti->total_ext_node, 0);
}

void f2fs_init_extent_cache_info(struct f2fs_sb_info *sbi)
{
	__init_extent_tree_info(&sbi->extent_tree[EX_READ]);
	__init_extent_tree_info(&sbi->extent_tree[EX_BLOCK_AGE]);

	/* initialize for block age extents */
	atomic64_set(&sbi->allocated_data_blocks, 0);
	sbi->hot_data_age_threshold = DEF_HOT_DATA_AGE_THRESHOLD;
	sbi->warm_data_age_threshold = DEF_WARM_DATA_AGE_THRESHOLD;
	sbi->last_age_weight = LAST_AGE_WEIGHT;
}

int __init f2fs_create_extent_cache(void)
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

void f2fs_destroy_extent_cache(void)
{
	kmem_cache_destroy(extent_node_slab);
	kmem_cache_destroy(extent_tree_slab);
}
