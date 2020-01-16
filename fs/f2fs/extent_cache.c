// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs extent cache support
 *
 * Copyright (c) 2015 Motorola Mobility
 * Copyright (c) 2015 Samsung Electronics
 * Authors: Jaegeuk Kim <jaegeuk@kernel.org>
 *          Chao Yu <chao2.yu@samsung.com>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "yesde.h"
#include <trace/events/f2fs.h>

static struct rb_entry *__lookup_rb_tree_fast(struct rb_entry *cached_re,
							unsigned int ofs)
{
	if (cached_re) {
		if (cached_re->ofs <= ofs &&
				cached_re->ofs + cached_re->len > ofs) {
			return cached_re;
		}
	}
	return NULL;
}

static struct rb_entry *__lookup_rb_tree_slow(struct rb_root_cached *root,
							unsigned int ofs)
{
	struct rb_yesde *yesde = root->rb_root.rb_yesde;
	struct rb_entry *re;

	while (yesde) {
		re = rb_entry(yesde, struct rb_entry, rb_yesde);

		if (ofs < re->ofs)
			yesde = yesde->rb_left;
		else if (ofs >= re->ofs + re->len)
			yesde = yesde->rb_right;
		else
			return re;
	}
	return NULL;
}

struct rb_entry *f2fs_lookup_rb_tree(struct rb_root_cached *root,
				struct rb_entry *cached_re, unsigned int ofs)
{
	struct rb_entry *re;

	re = __lookup_rb_tree_fast(cached_re, ofs);
	if (!re)
		return __lookup_rb_tree_slow(root, ofs);

	return re;
}

struct rb_yesde **f2fs_lookup_rb_tree_for_insert(struct f2fs_sb_info *sbi,
				struct rb_root_cached *root,
				struct rb_yesde **parent,
				unsigned int ofs, bool *leftmost)
{
	struct rb_yesde **p = &root->rb_root.rb_yesde;
	struct rb_entry *re;

	while (*p) {
		*parent = *p;
		re = rb_entry(*parent, struct rb_entry, rb_yesde);

		if (ofs < re->ofs) {
			p = &(*p)->rb_left;
		} else if (ofs >= re->ofs + re->len) {
			p = &(*p)->rb_right;
			*leftmost = false;
		} else {
			f2fs_bug_on(sbi, 1);
		}
	}

	return p;
}

/*
 * lookup rb entry in position of @ofs in rb-tree,
 * if hit, return the entry, otherwise, return NULL
 * @prev_ex: extent before ofs
 * @next_ex: extent after ofs
 * @insert_p: insert point for new extent at ofs
 * in order to simpfy the insertion after.
 * tree must stay unchanged between lookup and insertion.
 */
struct rb_entry *f2fs_lookup_rb_tree_ret(struct rb_root_cached *root,
				struct rb_entry *cached_re,
				unsigned int ofs,
				struct rb_entry **prev_entry,
				struct rb_entry **next_entry,
				struct rb_yesde ***insert_p,
				struct rb_yesde **insert_parent,
				bool force, bool *leftmost)
{
	struct rb_yesde **pyesde = &root->rb_root.rb_yesde;
	struct rb_yesde *parent = NULL, *tmp_yesde;
	struct rb_entry *re = cached_re;

	*insert_p = NULL;
	*insert_parent = NULL;
	*prev_entry = NULL;
	*next_entry = NULL;

	if (RB_EMPTY_ROOT(&root->rb_root))
		return NULL;

	if (re) {
		if (re->ofs <= ofs && re->ofs + re->len > ofs)
			goto lookup_neighbors;
	}

	if (leftmost)
		*leftmost = true;

	while (*pyesde) {
		parent = *pyesde;
		re = rb_entry(*pyesde, struct rb_entry, rb_yesde);

		if (ofs < re->ofs) {
			pyesde = &(*pyesde)->rb_left;
		} else if (ofs >= re->ofs + re->len) {
			pyesde = &(*pyesde)->rb_right;
			if (leftmost)
				*leftmost = false;
		} else {
			goto lookup_neighbors;
		}
	}

	*insert_p = pyesde;
	*insert_parent = parent;

	re = rb_entry(parent, struct rb_entry, rb_yesde);
	tmp_yesde = parent;
	if (parent && ofs > re->ofs)
		tmp_yesde = rb_next(parent);
	*next_entry = rb_entry_safe(tmp_yesde, struct rb_entry, rb_yesde);

	tmp_yesde = parent;
	if (parent && ofs < re->ofs)
		tmp_yesde = rb_prev(parent);
	*prev_entry = rb_entry_safe(tmp_yesde, struct rb_entry, rb_yesde);
	return NULL;

lookup_neighbors:
	if (ofs == re->ofs || force) {
		/* lookup prev yesde for merging backward later */
		tmp_yesde = rb_prev(&re->rb_yesde);
		*prev_entry = rb_entry_safe(tmp_yesde, struct rb_entry, rb_yesde);
	}
	if (ofs == re->ofs + re->len - 1 || force) {
		/* lookup next yesde for merging frontward later */
		tmp_yesde = rb_next(&re->rb_yesde);
		*next_entry = rb_entry_safe(tmp_yesde, struct rb_entry, rb_yesde);
	}
	return re;
}

bool f2fs_check_rb_tree_consistence(struct f2fs_sb_info *sbi,
						struct rb_root_cached *root)
{
#ifdef CONFIG_F2FS_CHECK_FS
	struct rb_yesde *cur = rb_first_cached(root), *next;
	struct rb_entry *cur_re, *next_re;

	if (!cur)
		return true;

	while (cur) {
		next = rb_next(cur);
		if (!next)
			return true;

		cur_re = rb_entry(cur, struct rb_entry, rb_yesde);
		next_re = rb_entry(next, struct rb_entry, rb_yesde);

		if (cur_re->ofs + cur_re->len > next_re->ofs) {
			f2fs_info(sbi, "inconsistent rbtree, cur(%u, %u) next(%u, %u)",
				  cur_re->ofs, cur_re->len,
				  next_re->ofs, next_re->len);
			return false;
		}

		cur = next;
	}
#endif
	return true;
}

static struct kmem_cache *extent_tree_slab;
static struct kmem_cache *extent_yesde_slab;

static struct extent_yesde *__attach_extent_yesde(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_yesde *parent, struct rb_yesde **p,
				bool leftmost)
{
	struct extent_yesde *en;

	en = kmem_cache_alloc(extent_yesde_slab, GFP_ATOMIC);
	if (!en)
		return NULL;

	en->ei = *ei;
	INIT_LIST_HEAD(&en->list);
	en->et = et;

	rb_link_yesde(&en->rb_yesde, parent, p);
	rb_insert_color_cached(&en->rb_yesde, &et->root, leftmost);
	atomic_inc(&et->yesde_cnt);
	atomic_inc(&sbi->total_ext_yesde);
	return en;
}

static void __detach_extent_yesde(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_yesde *en)
{
	rb_erase_cached(&en->rb_yesde, &et->root);
	atomic_dec(&et->yesde_cnt);
	atomic_dec(&sbi->total_ext_yesde);

	if (et->cached_en == en)
		et->cached_en = NULL;
	kmem_cache_free(extent_yesde_slab, en);
}

/*
 * Flow to release an extent_yesde:
 * 1. list_del_init
 * 2. __detach_extent_yesde
 * 3. kmem_cache_free.
 */
static void __release_extent_yesde(struct f2fs_sb_info *sbi,
			struct extent_tree *et, struct extent_yesde *en)
{
	spin_lock(&sbi->extent_lock);
	f2fs_bug_on(sbi, list_empty(&en->list));
	list_del_init(&en->list);
	spin_unlock(&sbi->extent_lock);

	__detach_extent_yesde(sbi, et, en);
}

static struct extent_tree *__grab_extent_tree(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et;
	nid_t iyes = iyesde->i_iyes;

	mutex_lock(&sbi->extent_tree_lock);
	et = radix_tree_lookup(&sbi->extent_tree_root, iyes);
	if (!et) {
		et = f2fs_kmem_cache_alloc(extent_tree_slab, GFP_NOFS);
		f2fs_radix_tree_insert(&sbi->extent_tree_root, iyes, et);
		memset(et, 0, sizeof(struct extent_tree));
		et->iyes = iyes;
		et->root = RB_ROOT_CACHED;
		et->cached_en = NULL;
		rwlock_init(&et->lock);
		INIT_LIST_HEAD(&et->list);
		atomic_set(&et->yesde_cnt, 0);
		atomic_inc(&sbi->total_ext_tree);
	} else {
		atomic_dec(&sbi->total_zombie_tree);
		list_del_init(&et->list);
	}
	mutex_unlock(&sbi->extent_tree_lock);

	/* never died until evict_iyesde */
	F2FS_I(iyesde)->extent_tree = et;

	return et;
}

static struct extent_yesde *__init_extent_tree(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei)
{
	struct rb_yesde **p = &et->root.rb_root.rb_yesde;
	struct extent_yesde *en;

	en = __attach_extent_yesde(sbi, et, ei, NULL, p, true);
	if (!en)
		return NULL;

	et->largest = en->ei;
	et->cached_en = en;
	return en;
}

static unsigned int __free_extent_tree(struct f2fs_sb_info *sbi,
					struct extent_tree *et)
{
	struct rb_yesde *yesde, *next;
	struct extent_yesde *en;
	unsigned int count = atomic_read(&et->yesde_cnt);

	yesde = rb_first_cached(&et->root);
	while (yesde) {
		next = rb_next(yesde);
		en = rb_entry(yesde, struct extent_yesde, rb_yesde);
		__release_extent_yesde(sbi, et, en);
		yesde = next;
	}

	return count - atomic_read(&et->yesde_cnt);
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

/* return true, if iyesde page is changed */
static bool __f2fs_init_extent_tree(struct iyesde *iyesde, struct f2fs_extent *i_ext)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et;
	struct extent_yesde *en;
	struct extent_info ei;

	if (!f2fs_may_extent_tree(iyesde)) {
		/* drop largest extent */
		if (i_ext && i_ext->len) {
			i_ext->len = 0;
			return true;
		}
		return false;
	}

	et = __grab_extent_tree(iyesde);

	if (!i_ext || !i_ext->len)
		return false;

	get_extent_info(&ei, i_ext);

	write_lock(&et->lock);
	if (atomic_read(&et->yesde_cnt))
		goto out;

	en = __init_extent_tree(sbi, et, &ei);
	if (en) {
		spin_lock(&sbi->extent_lock);
		list_add_tail(&en->list, &sbi->extent_list);
		spin_unlock(&sbi->extent_lock);
	}
out:
	write_unlock(&et->lock);
	return false;
}

bool f2fs_init_extent_tree(struct iyesde *iyesde, struct f2fs_extent *i_ext)
{
	bool ret =  __f2fs_init_extent_tree(iyesde, i_ext);

	if (!F2FS_I(iyesde)->extent_tree)
		set_iyesde_flag(iyesde, FI_NO_EXTENT);

	return ret;
}

static bool f2fs_lookup_extent_tree(struct iyesde *iyesde, pgoff_t pgofs,
							struct extent_info *ei)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;
	struct extent_yesde *en;
	bool ret = false;

	f2fs_bug_on(sbi, !et);

	trace_f2fs_lookup_extent_tree_start(iyesde, pgofs);

	read_lock(&et->lock);

	if (et->largest.fofs <= pgofs &&
			et->largest.fofs + et->largest.len > pgofs) {
		*ei = et->largest;
		ret = true;
		stat_inc_largest_yesde_hit(sbi);
		goto out;
	}

	en = (struct extent_yesde *)f2fs_lookup_rb_tree(&et->root,
				(struct rb_entry *)et->cached_en, pgofs);
	if (!en)
		goto out;

	if (en == et->cached_en)
		stat_inc_cached_yesde_hit(sbi);
	else
		stat_inc_rbtree_yesde_hit(sbi);

	*ei = en->ei;
	spin_lock(&sbi->extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &sbi->extent_list);
		et->cached_en = en;
	}
	spin_unlock(&sbi->extent_lock);
	ret = true;
out:
	stat_inc_total_hit(sbi);
	read_unlock(&et->lock);

	trace_f2fs_lookup_extent_tree_end(iyesde, pgofs, ei);
	return ret;
}

static struct extent_yesde *__try_merge_extent_yesde(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct extent_yesde *prev_ex,
				struct extent_yesde *next_ex)
{
	struct extent_yesde *en = NULL;

	if (prev_ex && __is_back_mergeable(ei, &prev_ex->ei)) {
		prev_ex->ei.len += ei->len;
		ei = &prev_ex->ei;
		en = prev_ex;
	}

	if (next_ex && __is_front_mergeable(ei, &next_ex->ei)) {
		next_ex->ei.fofs = ei->fofs;
		next_ex->ei.blk = ei->blk;
		next_ex->ei.len += ei->len;
		if (en)
			__release_extent_yesde(sbi, et, prev_ex);

		en = next_ex;
	}

	if (!en)
		return NULL;

	__try_update_largest_extent(et, en);

	spin_lock(&sbi->extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &sbi->extent_list);
		et->cached_en = en;
	}
	spin_unlock(&sbi->extent_lock);
	return en;
}

static struct extent_yesde *__insert_extent_tree(struct f2fs_sb_info *sbi,
				struct extent_tree *et, struct extent_info *ei,
				struct rb_yesde **insert_p,
				struct rb_yesde *insert_parent,
				bool leftmost)
{
	struct rb_yesde **p;
	struct rb_yesde *parent = NULL;
	struct extent_yesde *en = NULL;

	if (insert_p && insert_parent) {
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	leftmost = true;

	p = f2fs_lookup_rb_tree_for_insert(sbi, &et->root, &parent,
						ei->fofs, &leftmost);
do_insert:
	en = __attach_extent_yesde(sbi, et, ei, parent, p, leftmost);
	if (!en)
		return NULL;

	__try_update_largest_extent(et, en);

	/* update in global extent list */
	spin_lock(&sbi->extent_lock);
	list_add_tail(&en->list, &sbi->extent_list);
	et->cached_en = en;
	spin_unlock(&sbi->extent_lock);
	return en;
}

static void f2fs_update_extent_tree_range(struct iyesde *iyesde,
				pgoff_t fofs, block_t blkaddr, unsigned int len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;
	struct extent_yesde *en = NULL, *en1 = NULL;
	struct extent_yesde *prev_en = NULL, *next_en = NULL;
	struct extent_info ei, dei, prev;
	struct rb_yesde **insert_p = NULL, *insert_parent = NULL;
	unsigned int end = fofs + len;
	unsigned int pos = (unsigned int)fofs;
	bool updated = false;
	bool leftmost = false;

	if (!et)
		return;

	trace_f2fs_update_extent_tree_range(iyesde, fofs, blkaddr, len);

	write_lock(&et->lock);

	if (is_iyesde_flag_set(iyesde, FI_NO_EXTENT)) {
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

	/* 1. lookup first extent yesde in range [fofs, fofs + len - 1] */
	en = (struct extent_yesde *)f2fs_lookup_rb_tree_ret(&et->root,
					(struct rb_entry *)et->cached_en, fofs,
					(struct rb_entry **)&prev_en,
					(struct rb_entry **)&next_en,
					&insert_p, &insert_parent, false,
					&leftmost);
	if (!en)
		en = next_en;

	/* 2. invlidate all extent yesdes in range [fofs, fofs + len - 1] */
	while (en && en->ei.fofs < end) {
		unsigned int org_end;
		int parts = 0;	/* # of parts current extent split into */

		next_en = en1 = NULL;

		dei = en->ei;
		org_end = dei.fofs + dei.len;
		f2fs_bug_on(sbi, pos >= org_end);

		if (pos > dei.fofs &&	pos - dei.fofs >= F2FS_MIN_EXTENT_LEN) {
			en->ei.len = pos - en->ei.fofs;
			prev_en = en;
			parts = 1;
		}

		if (end < org_end && org_end - end >= F2FS_MIN_EXTENT_LEN) {
			if (parts) {
				set_extent_info(&ei, end,
						end - dei.fofs + dei.blk,
						org_end - end);
				en1 = __insert_extent_tree(sbi, et, &ei,
							NULL, NULL, true);
				next_en = en1;
			} else {
				en->ei.fofs = end;
				en->ei.blk += end - dei.fofs;
				en->ei.len -= end - dei.fofs;
				next_en = en;
			}
			parts++;
		}

		if (!next_en) {
			struct rb_yesde *yesde = rb_next(&en->rb_yesde);

			next_en = rb_entry_safe(yesde, struct extent_yesde,
						rb_yesde);
		}

		if (parts)
			__try_update_largest_extent(et, en);
		else
			__release_extent_yesde(sbi, et, en);

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

	/* 3. update extent in extent cache */
	if (blkaddr) {

		set_extent_info(&ei, fofs, blkaddr, len);
		if (!__try_merge_extent_yesde(sbi, et, &ei, prev_en, next_en))
			__insert_extent_tree(sbi, et, &ei,
					insert_p, insert_parent, leftmost);

		/* give up extent_cache, if split and small updates happen */
		if (dei.len >= 1 &&
				prev.len < F2FS_MIN_EXTENT_LEN &&
				et->largest.len < F2FS_MIN_EXTENT_LEN) {
			et->largest.len = 0;
			et->largest_updated = true;
			set_iyesde_flag(iyesde, FI_NO_EXTENT);
		}
	}

	if (is_iyesde_flag_set(iyesde, FI_NO_EXTENT))
		__free_extent_tree(sbi, et);

	if (et->largest_updated) {
		et->largest_updated = false;
		updated = true;
	}

	write_unlock(&et->lock);

	if (updated)
		f2fs_mark_iyesde_dirty_sync(iyesde, true);
}

unsigned int f2fs_shrink_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct extent_tree *et, *next;
	struct extent_yesde *en;
	unsigned int yesde_cnt = 0, tree_cnt = 0;
	int remained;

	if (!test_opt(sbi, EXTENT_CACHE))
		return 0;

	if (!atomic_read(&sbi->total_zombie_tree))
		goto free_yesde;

	if (!mutex_trylock(&sbi->extent_tree_lock))
		goto out;

	/* 1. remove unreferenced extent tree */
	list_for_each_entry_safe(et, next, &sbi->zombie_list, list) {
		if (atomic_read(&et->yesde_cnt)) {
			write_lock(&et->lock);
			yesde_cnt += __free_extent_tree(sbi, et);
			write_unlock(&et->lock);
		}
		f2fs_bug_on(sbi, atomic_read(&et->yesde_cnt));
		list_del_init(&et->list);
		radix_tree_delete(&sbi->extent_tree_root, et->iyes);
		kmem_cache_free(extent_tree_slab, et);
		atomic_dec(&sbi->total_ext_tree);
		atomic_dec(&sbi->total_zombie_tree);
		tree_cnt++;

		if (yesde_cnt + tree_cnt >= nr_shrink)
			goto unlock_out;
		cond_resched();
	}
	mutex_unlock(&sbi->extent_tree_lock);

free_yesde:
	/* 2. remove LRU extent entries */
	if (!mutex_trylock(&sbi->extent_tree_lock))
		goto out;

	remained = nr_shrink - (yesde_cnt + tree_cnt);

	spin_lock(&sbi->extent_lock);
	for (; remained > 0; remained--) {
		if (list_empty(&sbi->extent_list))
			break;
		en = list_first_entry(&sbi->extent_list,
					struct extent_yesde, list);
		et = en->et;
		if (!write_trylock(&et->lock)) {
			/* refresh this extent yesde's position in extent list */
			list_move_tail(&en->list, &sbi->extent_list);
			continue;
		}

		list_del_init(&en->list);
		spin_unlock(&sbi->extent_lock);

		__detach_extent_yesde(sbi, et, en);

		write_unlock(&et->lock);
		yesde_cnt++;
		spin_lock(&sbi->extent_lock);
	}
	spin_unlock(&sbi->extent_lock);

unlock_out:
	mutex_unlock(&sbi->extent_tree_lock);
out:
	trace_f2fs_shrink_extent_tree(sbi, yesde_cnt, tree_cnt);

	return yesde_cnt + tree_cnt;
}

unsigned int f2fs_destroy_extent_yesde(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;
	unsigned int yesde_cnt = 0;

	if (!et || !atomic_read(&et->yesde_cnt))
		return 0;

	write_lock(&et->lock);
	yesde_cnt = __free_extent_tree(sbi, et);
	write_unlock(&et->lock);

	return yesde_cnt;
}

void f2fs_drop_extent_tree(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;
	bool updated = false;

	if (!f2fs_may_extent_tree(iyesde))
		return;

	set_iyesde_flag(iyesde, FI_NO_EXTENT);

	write_lock(&et->lock);
	__free_extent_tree(sbi, et);
	if (et->largest.len) {
		et->largest.len = 0;
		updated = true;
	}
	write_unlock(&et->lock);
	if (updated)
		f2fs_mark_iyesde_dirty_sync(iyesde, true);
}

void f2fs_destroy_extent_tree(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;
	unsigned int yesde_cnt = 0;

	if (!et)
		return;

	if (iyesde->i_nlink && !is_bad_iyesde(iyesde) &&
					atomic_read(&et->yesde_cnt)) {
		mutex_lock(&sbi->extent_tree_lock);
		list_add_tail(&et->list, &sbi->zombie_list);
		atomic_inc(&sbi->total_zombie_tree);
		mutex_unlock(&sbi->extent_tree_lock);
		return;
	}

	/* free all extent info belong to this extent tree */
	yesde_cnt = f2fs_destroy_extent_yesde(iyesde);

	/* delete extent tree entry in radix tree */
	mutex_lock(&sbi->extent_tree_lock);
	f2fs_bug_on(sbi, atomic_read(&et->yesde_cnt));
	radix_tree_delete(&sbi->extent_tree_root, iyesde->i_iyes);
	kmem_cache_free(extent_tree_slab, et);
	atomic_dec(&sbi->total_ext_tree);
	mutex_unlock(&sbi->extent_tree_lock);

	F2FS_I(iyesde)->extent_tree = NULL;

	trace_f2fs_destroy_extent_tree(iyesde, yesde_cnt);
}

bool f2fs_lookup_extent_cache(struct iyesde *iyesde, pgoff_t pgofs,
					struct extent_info *ei)
{
	if (!f2fs_may_extent_tree(iyesde))
		return false;

	return f2fs_lookup_extent_tree(iyesde, pgofs, ei);
}

void f2fs_update_extent_cache(struct dyesde_of_data *dn)
{
	pgoff_t fofs;
	block_t blkaddr;

	if (!f2fs_may_extent_tree(dn->iyesde))
		return;

	if (dn->data_blkaddr == NEW_ADDR)
		blkaddr = NULL_ADDR;
	else
		blkaddr = dn->data_blkaddr;

	fofs = f2fs_start_bidx_of_yesde(ofs_of_yesde(dn->yesde_page), dn->iyesde) +
								dn->ofs_in_yesde;
	f2fs_update_extent_tree_range(dn->iyesde, fofs, blkaddr, 1);
}

void f2fs_update_extent_cache_range(struct dyesde_of_data *dn,
				pgoff_t fofs, block_t blkaddr, unsigned int len)

{
	if (!f2fs_may_extent_tree(dn->iyesde))
		return;

	f2fs_update_extent_tree_range(dn->iyesde, fofs, blkaddr, len);
}

void f2fs_init_extent_cache_info(struct f2fs_sb_info *sbi)
{
	INIT_RADIX_TREE(&sbi->extent_tree_root, GFP_NOIO);
	mutex_init(&sbi->extent_tree_lock);
	INIT_LIST_HEAD(&sbi->extent_list);
	spin_lock_init(&sbi->extent_lock);
	atomic_set(&sbi->total_ext_tree, 0);
	INIT_LIST_HEAD(&sbi->zombie_list);
	atomic_set(&sbi->total_zombie_tree, 0);
	atomic_set(&sbi->total_ext_yesde, 0);
}

int __init f2fs_create_extent_cache(void)
{
	extent_tree_slab = f2fs_kmem_cache_create("f2fs_extent_tree",
			sizeof(struct extent_tree));
	if (!extent_tree_slab)
		return -ENOMEM;
	extent_yesde_slab = f2fs_kmem_cache_create("f2fs_extent_yesde",
			sizeof(struct extent_yesde));
	if (!extent_yesde_slab) {
		kmem_cache_destroy(extent_tree_slab);
		return -ENOMEM;
	}
	return 0;
}

void f2fs_destroy_extent_cache(void)
{
	kmem_cache_destroy(extent_yesde_slab);
	kmem_cache_destroy(extent_tree_slab);
}
