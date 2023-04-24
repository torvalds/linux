// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/ext4/extents_status.c
 *
 * Written by Yongqiang Yang <xiaoqiangnk@gmail.com>
 * Modified by
 *	Allison Henderson <achender@linux.vnet.ibm.com>
 *	Hugh Dickins <hughd@google.com>
 *	Zheng Liu <wenqing.lz@taobao.com>
 *
 * Ext4 extents status tree core functions.
 */
#include <linux/list_sort.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "ext4.h"

#include <trace/events/ext4.h>

/*
 * According to previous discussion in Ext4 Developer Workshop, we
 * will introduce a new structure called io tree to track all extent
 * status in order to solve some problems that we have met
 * (e.g. Reservation space warning), and provide extent-level locking.
 * Delay extent tree is the first step to achieve this goal.  It is
 * original built by Yongqiang Yang.  At that time it is called delay
 * extent tree, whose goal is only track delayed extents in memory to
 * simplify the implementation of fiemap and bigalloc, and introduce
 * lseek SEEK_DATA/SEEK_HOLE support.  That is why it is still called
 * delay extent tree at the first commit.  But for better understand
 * what it does, it has been rename to extent status tree.
 *
 * Step1:
 * Currently the first step has been done.  All delayed extents are
 * tracked in the tree.  It maintains the delayed extent when a delayed
 * allocation is issued, and the delayed extent is written out or
 * invalidated.  Therefore the implementation of fiemap and bigalloc
 * are simplified, and SEEK_DATA/SEEK_HOLE are introduced.
 *
 * The following comment describes the implemenmtation of extent
 * status tree and future works.
 *
 * Step2:
 * In this step all extent status are tracked by extent status tree.
 * Thus, we can first try to lookup a block mapping in this tree before
 * finding it in extent tree.  Hence, single extent cache can be removed
 * because extent status tree can do a better job.  Extents in status
 * tree are loaded on-demand.  Therefore, the extent status tree may not
 * contain all of the extents in a file.  Meanwhile we define a shrinker
 * to reclaim memory from extent status tree because fragmented extent
 * tree will make status tree cost too much memory.  written/unwritten/-
 * hole extents in the tree will be reclaimed by this shrinker when we
 * are under high memory pressure.  Delayed extents will not be
 * reclimed because fiemap, bigalloc, and seek_data/hole need it.
 */

/*
 * Extent status tree implementation for ext4.
 *
 *
 * ==========================================================================
 * Extent status tree tracks all extent status.
 *
 * 1. Why we need to implement extent status tree?
 *
 * Without extent status tree, ext4 identifies a delayed extent by looking
 * up page cache, this has several deficiencies - complicated, buggy,
 * and inefficient code.
 *
 * FIEMAP, SEEK_HOLE/DATA, bigalloc, and writeout all need to know if a
 * block or a range of blocks are belonged to a delayed extent.
 *
 * Let us have a look at how they do without extent status tree.
 *   --	FIEMAP
 *	FIEMAP looks up page cache to identify delayed allocations from holes.
 *
 *   --	SEEK_HOLE/DATA
 *	SEEK_HOLE/DATA has the same problem as FIEMAP.
 *
 *   --	bigalloc
 *	bigalloc looks up page cache to figure out if a block is
 *	already under delayed allocation or not to determine whether
 *	quota reserving is needed for the cluster.
 *
 *   --	writeout
 *	Writeout looks up whole page cache to see if a buffer is
 *	mapped, If there are not very many delayed buffers, then it is
 *	time consuming.
 *
 * With extent status tree implementation, FIEMAP, SEEK_HOLE/DATA,
 * bigalloc and writeout can figure out if a block or a range of
 * blocks is under delayed allocation(belonged to a delayed extent) or
 * not by searching the extent tree.
 *
 *
 * ==========================================================================
 * 2. Ext4 extent status tree impelmentation
 *
 *   --	extent
 *	A extent is a range of blocks which are contiguous logically and
 *	physically.  Unlike extent in extent tree, this extent in ext4 is
 *	a in-memory struct, there is no corresponding on-disk data.  There
 *	is no limit on length of extent, so an extent can contain as many
 *	blocks as they are contiguous logically and physically.
 *
 *   --	extent status tree
 *	Every inode has an extent status tree and all allocation blocks
 *	are added to the tree with different status.  The extent in the
 *	tree are ordered by logical block no.
 *
 *   --	operations on a extent status tree
 *	There are three important operations on a delayed extent tree: find
 *	next extent, adding a extent(a range of blocks) and removing a extent.
 *
 *   --	race on a extent status tree
 *	Extent status tree is protected by inode->i_es_lock.
 *
 *   --	memory consumption
 *      Fragmented extent tree will make extent status tree cost too much
 *      memory.  Hence, we will reclaim written/unwritten/hole extents from
 *      the tree under a heavy memory pressure.
 *
 *
 * ==========================================================================
 * 3. Performance analysis
 *
 *   --	overhead
 *	1. There is a cache extent for write access, so if writes are
 *	not very random, adding space operaions are in O(1) time.
 *
 *   --	gain
 *	2. Code is much simpler, more readable, more maintainable and
 *	more efficient.
 *
 *
 * ==========================================================================
 * 4. TODO list
 *
 *   -- Refactor delayed space reservation
 *
 *   -- Extent-level locking
 */

static struct kmem_cache *ext4_es_cachep;
static struct kmem_cache *ext4_pending_cachep;

static int __es_insert_extent(struct inode *inode, struct extent_status *newes,
			      struct extent_status *prealloc);
static int __es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			      ext4_lblk_t end, int *reserved,
			      struct extent_status *prealloc);
static int es_reclaim_extents(struct ext4_inode_info *ei, int *nr_to_scan);
static int __es_shrink(struct ext4_sb_info *sbi, int nr_to_scan,
		       struct ext4_inode_info *locked_ei);
static void __revise_pending(struct inode *inode, ext4_lblk_t lblk,
			     ext4_lblk_t len);

int __init ext4_init_es(void)
{
	ext4_es_cachep = kmem_cache_create("ext4_extent_status",
					   sizeof(struct extent_status),
					   0, (SLAB_RECLAIM_ACCOUNT), NULL);
	if (ext4_es_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void ext4_exit_es(void)
{
	kmem_cache_destroy(ext4_es_cachep);
}

void ext4_es_init_tree(struct ext4_es_tree *tree)
{
	tree->root = RB_ROOT;
	tree->cache_es = NULL;
}

#ifdef ES_DEBUG__
static void ext4_es_print_tree(struct inode *inode)
{
	struct ext4_es_tree *tree;
	struct rb_node *node;

	printk(KERN_DEBUG "status extents for inode %lu:", inode->i_ino);
	tree = &EXT4_I(inode)->i_es_tree;
	node = rb_first(&tree->root);
	while (node) {
		struct extent_status *es;
		es = rb_entry(node, struct extent_status, rb_node);
		printk(KERN_DEBUG " [%u/%u) %llu %x",
		       es->es_lblk, es->es_len,
		       ext4_es_pblock(es), ext4_es_status(es));
		node = rb_next(node);
	}
	printk(KERN_DEBUG "\n");
}
#else
#define ext4_es_print_tree(inode)
#endif

static inline ext4_lblk_t ext4_es_end(struct extent_status *es)
{
	BUG_ON(es->es_lblk + es->es_len < es->es_lblk);
	return es->es_lblk + es->es_len - 1;
}

/*
 * search through the tree for an delayed extent with a given offset.  If
 * it can't be found, try to find next extent.
 */
static struct extent_status *__es_tree_search(struct rb_root *root,
					      ext4_lblk_t lblk)
{
	struct rb_node *node = root->rb_node;
	struct extent_status *es = NULL;

	while (node) {
		es = rb_entry(node, struct extent_status, rb_node);
		if (lblk < es->es_lblk)
			node = node->rb_left;
		else if (lblk > ext4_es_end(es))
			node = node->rb_right;
		else
			return es;
	}

	if (es && lblk < es->es_lblk)
		return es;

	if (es && lblk > ext4_es_end(es)) {
		node = rb_next(&es->rb_node);
		return node ? rb_entry(node, struct extent_status, rb_node) :
			      NULL;
	}

	return NULL;
}

/*
 * ext4_es_find_extent_range - find extent with specified status within block
 *                             range or next extent following block range in
 *                             extents status tree
 *
 * @inode - file containing the range
 * @matching_fn - pointer to function that matches extents with desired status
 * @lblk - logical block defining start of range
 * @end - logical block defining end of range
 * @es - extent found, if any
 *
 * Find the first extent within the block range specified by @lblk and @end
 * in the extents status tree that satisfies @matching_fn.  If a match
 * is found, it's returned in @es.  If not, and a matching extent is found
 * beyond the block range, it's returned in @es.  If no match is found, an
 * extent is returned in @es whose es_lblk, es_len, and es_pblk components
 * are 0.
 */
static void __es_find_extent_range(struct inode *inode,
				   int (*matching_fn)(struct extent_status *es),
				   ext4_lblk_t lblk, ext4_lblk_t end,
				   struct extent_status *es)
{
	struct ext4_es_tree *tree = NULL;
	struct extent_status *es1 = NULL;
	struct rb_node *node;

	WARN_ON(es == NULL);
	WARN_ON(end < lblk);

	tree = &EXT4_I(inode)->i_es_tree;

	/* see if the extent has been cached */
	es->es_lblk = es->es_len = es->es_pblk = 0;
	es1 = READ_ONCE(tree->cache_es);
	if (es1 && in_range(lblk, es1->es_lblk, es1->es_len)) {
		es_debug("%u cached by [%u/%u) %llu %x\n",
			 lblk, es1->es_lblk, es1->es_len,
			 ext4_es_pblock(es1), ext4_es_status(es1));
		goto out;
	}

	es1 = __es_tree_search(&tree->root, lblk);

out:
	if (es1 && !matching_fn(es1)) {
		while ((node = rb_next(&es1->rb_node)) != NULL) {
			es1 = rb_entry(node, struct extent_status, rb_node);
			if (es1->es_lblk > end) {
				es1 = NULL;
				break;
			}
			if (matching_fn(es1))
				break;
		}
	}

	if (es1 && matching_fn(es1)) {
		WRITE_ONCE(tree->cache_es, es1);
		es->es_lblk = es1->es_lblk;
		es->es_len = es1->es_len;
		es->es_pblk = es1->es_pblk;
	}

}

/*
 * Locking for __es_find_extent_range() for external use
 */
void ext4_es_find_extent_range(struct inode *inode,
			       int (*matching_fn)(struct extent_status *es),
			       ext4_lblk_t lblk, ext4_lblk_t end,
			       struct extent_status *es)
{
	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return;

	trace_ext4_es_find_extent_range_enter(inode, lblk);

	read_lock(&EXT4_I(inode)->i_es_lock);
	__es_find_extent_range(inode, matching_fn, lblk, end, es);
	read_unlock(&EXT4_I(inode)->i_es_lock);

	trace_ext4_es_find_extent_range_exit(inode, es);
}

/*
 * __es_scan_range - search block range for block with specified status
 *                   in extents status tree
 *
 * @inode - file containing the range
 * @matching_fn - pointer to function that matches extents with desired status
 * @lblk - logical block defining start of range
 * @end - logical block defining end of range
 *
 * Returns true if at least one block in the specified block range satisfies
 * the criterion specified by @matching_fn, and false if not.  If at least
 * one extent has the specified status, then there is at least one block
 * in the cluster with that status.  Should only be called by code that has
 * taken i_es_lock.
 */
static bool __es_scan_range(struct inode *inode,
			    int (*matching_fn)(struct extent_status *es),
			    ext4_lblk_t start, ext4_lblk_t end)
{
	struct extent_status es;

	__es_find_extent_range(inode, matching_fn, start, end, &es);
	if (es.es_len == 0)
		return false;   /* no matching extent in the tree */
	else if (es.es_lblk <= start &&
		 start < es.es_lblk + es.es_len)
		return true;
	else if (start <= es.es_lblk && es.es_lblk <= end)
		return true;
	else
		return false;
}
/*
 * Locking for __es_scan_range() for external use
 */
bool ext4_es_scan_range(struct inode *inode,
			int (*matching_fn)(struct extent_status *es),
			ext4_lblk_t lblk, ext4_lblk_t end)
{
	bool ret;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return false;

	read_lock(&EXT4_I(inode)->i_es_lock);
	ret = __es_scan_range(inode, matching_fn, lblk, end);
	read_unlock(&EXT4_I(inode)->i_es_lock);

	return ret;
}

/*
 * __es_scan_clu - search cluster for block with specified status in
 *                 extents status tree
 *
 * @inode - file containing the cluster
 * @matching_fn - pointer to function that matches extents with desired status
 * @lblk - logical block in cluster to be searched
 *
 * Returns true if at least one extent in the cluster containing @lblk
 * satisfies the criterion specified by @matching_fn, and false if not.  If at
 * least one extent has the specified status, then there is at least one block
 * in the cluster with that status.  Should only be called by code that has
 * taken i_es_lock.
 */
static bool __es_scan_clu(struct inode *inode,
			  int (*matching_fn)(struct extent_status *es),
			  ext4_lblk_t lblk)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	ext4_lblk_t lblk_start, lblk_end;

	lblk_start = EXT4_LBLK_CMASK(sbi, lblk);
	lblk_end = lblk_start + sbi->s_cluster_ratio - 1;

	return __es_scan_range(inode, matching_fn, lblk_start, lblk_end);
}

/*
 * Locking for __es_scan_clu() for external use
 */
bool ext4_es_scan_clu(struct inode *inode,
		      int (*matching_fn)(struct extent_status *es),
		      ext4_lblk_t lblk)
{
	bool ret;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return false;

	read_lock(&EXT4_I(inode)->i_es_lock);
	ret = __es_scan_clu(inode, matching_fn, lblk);
	read_unlock(&EXT4_I(inode)->i_es_lock);

	return ret;
}

static void ext4_es_list_add(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	if (!list_empty(&ei->i_es_list))
		return;

	spin_lock(&sbi->s_es_lock);
	if (list_empty(&ei->i_es_list)) {
		list_add_tail(&ei->i_es_list, &sbi->s_es_list);
		sbi->s_es_nr_inode++;
	}
	spin_unlock(&sbi->s_es_lock);
}

static void ext4_es_list_del(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	spin_lock(&sbi->s_es_lock);
	if (!list_empty(&ei->i_es_list)) {
		list_del_init(&ei->i_es_list);
		sbi->s_es_nr_inode--;
		WARN_ON_ONCE(sbi->s_es_nr_inode < 0);
	}
	spin_unlock(&sbi->s_es_lock);
}

/*
 * Returns true if we cannot fail to allocate memory for this extent_status
 * entry and cannot reclaim it until its status changes.
 */
static inline bool ext4_es_must_keep(struct extent_status *es)
{
	/* fiemap, bigalloc, and seek_data/hole need to use it. */
	if (ext4_es_is_delayed(es))
		return true;

	return false;
}

static inline struct extent_status *__es_alloc_extent(bool nofail)
{
	if (!nofail)
		return kmem_cache_alloc(ext4_es_cachep, GFP_ATOMIC);

	return kmem_cache_zalloc(ext4_es_cachep, GFP_KERNEL | __GFP_NOFAIL);
}

static void ext4_es_init_extent(struct inode *inode, struct extent_status *es,
		ext4_lblk_t lblk, ext4_lblk_t len, ext4_fsblk_t pblk)
{
	es->es_lblk = lblk;
	es->es_len = len;
	es->es_pblk = pblk;

	/* We never try to reclaim a must kept extent, so we don't count it. */
	if (!ext4_es_must_keep(es)) {
		if (!EXT4_I(inode)->i_es_shk_nr++)
			ext4_es_list_add(inode);
		percpu_counter_inc(&EXT4_SB(inode->i_sb)->
					s_es_stats.es_stats_shk_cnt);
	}

	EXT4_I(inode)->i_es_all_nr++;
	percpu_counter_inc(&EXT4_SB(inode->i_sb)->s_es_stats.es_stats_all_cnt);
}

static inline void __es_free_extent(struct extent_status *es)
{
	kmem_cache_free(ext4_es_cachep, es);
}

static void ext4_es_free_extent(struct inode *inode, struct extent_status *es)
{
	EXT4_I(inode)->i_es_all_nr--;
	percpu_counter_dec(&EXT4_SB(inode->i_sb)->s_es_stats.es_stats_all_cnt);

	/* Decrease the shrink counter when we can reclaim the extent. */
	if (!ext4_es_must_keep(es)) {
		BUG_ON(EXT4_I(inode)->i_es_shk_nr == 0);
		if (!--EXT4_I(inode)->i_es_shk_nr)
			ext4_es_list_del(inode);
		percpu_counter_dec(&EXT4_SB(inode->i_sb)->
					s_es_stats.es_stats_shk_cnt);
	}

	__es_free_extent(es);
}

/*
 * Check whether or not two extents can be merged
 * Condition:
 *  - logical block number is contiguous
 *  - physical block number is contiguous
 *  - status is equal
 */
static int ext4_es_can_be_merged(struct extent_status *es1,
				 struct extent_status *es2)
{
	if (ext4_es_type(es1) != ext4_es_type(es2))
		return 0;

	if (((__u64) es1->es_len) + es2->es_len > EXT_MAX_BLOCKS) {
		pr_warn("ES assertion failed when merging extents. "
			"The sum of lengths of es1 (%d) and es2 (%d) "
			"is bigger than allowed file size (%d)\n",
			es1->es_len, es2->es_len, EXT_MAX_BLOCKS);
		WARN_ON(1);
		return 0;
	}

	if (((__u64) es1->es_lblk) + es1->es_len != es2->es_lblk)
		return 0;

	if ((ext4_es_is_written(es1) || ext4_es_is_unwritten(es1)) &&
	    (ext4_es_pblock(es1) + es1->es_len == ext4_es_pblock(es2)))
		return 1;

	if (ext4_es_is_hole(es1))
		return 1;

	/* we need to check delayed extent is without unwritten status */
	if (ext4_es_is_delayed(es1) && !ext4_es_is_unwritten(es1))
		return 1;

	return 0;
}

static struct extent_status *
ext4_es_try_to_merge_left(struct inode *inode, struct extent_status *es)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct extent_status *es1;
	struct rb_node *node;

	node = rb_prev(&es->rb_node);
	if (!node)
		return es;

	es1 = rb_entry(node, struct extent_status, rb_node);
	if (ext4_es_can_be_merged(es1, es)) {
		es1->es_len += es->es_len;
		if (ext4_es_is_referenced(es))
			ext4_es_set_referenced(es1);
		rb_erase(&es->rb_node, &tree->root);
		ext4_es_free_extent(inode, es);
		es = es1;
	}

	return es;
}

static struct extent_status *
ext4_es_try_to_merge_right(struct inode *inode, struct extent_status *es)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct extent_status *es1;
	struct rb_node *node;

	node = rb_next(&es->rb_node);
	if (!node)
		return es;

	es1 = rb_entry(node, struct extent_status, rb_node);
	if (ext4_es_can_be_merged(es, es1)) {
		es->es_len += es1->es_len;
		if (ext4_es_is_referenced(es1))
			ext4_es_set_referenced(es);
		rb_erase(node, &tree->root);
		ext4_es_free_extent(inode, es1);
	}

	return es;
}

#ifdef ES_AGGRESSIVE_TEST
#include "ext4_extents.h"	/* Needed when ES_AGGRESSIVE_TEST is defined */

static void ext4_es_insert_extent_ext_check(struct inode *inode,
					    struct extent_status *es)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ex;
	ext4_lblk_t ee_block;
	ext4_fsblk_t ee_start;
	unsigned short ee_len;
	int depth, ee_status, es_status;

	path = ext4_find_extent(inode, es->es_lblk, NULL, EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return;

	depth = ext_depth(inode);
	ex = path[depth].p_ext;

	if (ex) {

		ee_block = le32_to_cpu(ex->ee_block);
		ee_start = ext4_ext_pblock(ex);
		ee_len = ext4_ext_get_actual_len(ex);

		ee_status = ext4_ext_is_unwritten(ex) ? 1 : 0;
		es_status = ext4_es_is_unwritten(es) ? 1 : 0;

		/*
		 * Make sure ex and es are not overlap when we try to insert
		 * a delayed/hole extent.
		 */
		if (!ext4_es_is_written(es) && !ext4_es_is_unwritten(es)) {
			if (in_range(es->es_lblk, ee_block, ee_len)) {
				pr_warn("ES insert assertion failed for "
					"inode: %lu we can find an extent "
					"at block [%d/%d/%llu/%c], but we "
					"want to add a delayed/hole extent "
					"[%d/%d/%llu/%x]\n",
					inode->i_ino, ee_block, ee_len,
					ee_start, ee_status ? 'u' : 'w',
					es->es_lblk, es->es_len,
					ext4_es_pblock(es), ext4_es_status(es));
			}
			goto out;
		}

		/*
		 * We don't check ee_block == es->es_lblk, etc. because es
		 * might be a part of whole extent, vice versa.
		 */
		if (es->es_lblk < ee_block ||
		    ext4_es_pblock(es) != ee_start + es->es_lblk - ee_block) {
			pr_warn("ES insert assertion failed for inode: %lu "
				"ex_status [%d/%d/%llu/%c] != "
				"es_status [%d/%d/%llu/%c]\n", inode->i_ino,
				ee_block, ee_len, ee_start,
				ee_status ? 'u' : 'w', es->es_lblk, es->es_len,
				ext4_es_pblock(es), es_status ? 'u' : 'w');
			goto out;
		}

		if (ee_status ^ es_status) {
			pr_warn("ES insert assertion failed for inode: %lu "
				"ex_status [%d/%d/%llu/%c] != "
				"es_status [%d/%d/%llu/%c]\n", inode->i_ino,
				ee_block, ee_len, ee_start,
				ee_status ? 'u' : 'w', es->es_lblk, es->es_len,
				ext4_es_pblock(es), es_status ? 'u' : 'w');
		}
	} else {
		/*
		 * We can't find an extent on disk.  So we need to make sure
		 * that we don't want to add an written/unwritten extent.
		 */
		if (!ext4_es_is_delayed(es) && !ext4_es_is_hole(es)) {
			pr_warn("ES insert assertion failed for inode: %lu "
				"can't find an extent at block %d but we want "
				"to add a written/unwritten extent "
				"[%d/%d/%llu/%x]\n", inode->i_ino,
				es->es_lblk, es->es_lblk, es->es_len,
				ext4_es_pblock(es), ext4_es_status(es));
		}
	}
out:
	ext4_free_ext_path(path);
}

static void ext4_es_insert_extent_ind_check(struct inode *inode,
					    struct extent_status *es)
{
	struct ext4_map_blocks map;
	int retval;

	/*
	 * Here we call ext4_ind_map_blocks to lookup a block mapping because
	 * 'Indirect' structure is defined in indirect.c.  So we couldn't
	 * access direct/indirect tree from outside.  It is too dirty to define
	 * this function in indirect.c file.
	 */

	map.m_lblk = es->es_lblk;
	map.m_len = es->es_len;

	retval = ext4_ind_map_blocks(NULL, inode, &map, 0);
	if (retval > 0) {
		if (ext4_es_is_delayed(es) || ext4_es_is_hole(es)) {
			/*
			 * We want to add a delayed/hole extent but this
			 * block has been allocated.
			 */
			pr_warn("ES insert assertion failed for inode: %lu "
				"We can find blocks but we want to add a "
				"delayed/hole extent [%d/%d/%llu/%x]\n",
				inode->i_ino, es->es_lblk, es->es_len,
				ext4_es_pblock(es), ext4_es_status(es));
			return;
		} else if (ext4_es_is_written(es)) {
			if (retval != es->es_len) {
				pr_warn("ES insert assertion failed for "
					"inode: %lu retval %d != es_len %d\n",
					inode->i_ino, retval, es->es_len);
				return;
			}
			if (map.m_pblk != ext4_es_pblock(es)) {
				pr_warn("ES insert assertion failed for "
					"inode: %lu m_pblk %llu != "
					"es_pblk %llu\n",
					inode->i_ino, map.m_pblk,
					ext4_es_pblock(es));
				return;
			}
		} else {
			/*
			 * We don't need to check unwritten extent because
			 * indirect-based file doesn't have it.
			 */
			BUG();
		}
	} else if (retval == 0) {
		if (ext4_es_is_written(es)) {
			pr_warn("ES insert assertion failed for inode: %lu "
				"We can't find the block but we want to add "
				"a written extent [%d/%d/%llu/%x]\n",
				inode->i_ino, es->es_lblk, es->es_len,
				ext4_es_pblock(es), ext4_es_status(es));
			return;
		}
	}
}

static inline void ext4_es_insert_extent_check(struct inode *inode,
					       struct extent_status *es)
{
	/*
	 * We don't need to worry about the race condition because
	 * caller takes i_data_sem locking.
	 */
	BUG_ON(!rwsem_is_locked(&EXT4_I(inode)->i_data_sem));
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		ext4_es_insert_extent_ext_check(inode, es);
	else
		ext4_es_insert_extent_ind_check(inode, es);
}
#else
static inline void ext4_es_insert_extent_check(struct inode *inode,
					       struct extent_status *es)
{
}
#endif

static int __es_insert_extent(struct inode *inode, struct extent_status *newes,
			      struct extent_status *prealloc)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct rb_node **p = &tree->root.rb_node;
	struct rb_node *parent = NULL;
	struct extent_status *es;

	while (*p) {
		parent = *p;
		es = rb_entry(parent, struct extent_status, rb_node);

		if (newes->es_lblk < es->es_lblk) {
			if (ext4_es_can_be_merged(newes, es)) {
				/*
				 * Here we can modify es_lblk directly
				 * because it isn't overlapped.
				 */
				es->es_lblk = newes->es_lblk;
				es->es_len += newes->es_len;
				if (ext4_es_is_written(es) ||
				    ext4_es_is_unwritten(es))
					ext4_es_store_pblock(es,
							     newes->es_pblk);
				es = ext4_es_try_to_merge_left(inode, es);
				goto out;
			}
			p = &(*p)->rb_left;
		} else if (newes->es_lblk > ext4_es_end(es)) {
			if (ext4_es_can_be_merged(es, newes)) {
				es->es_len += newes->es_len;
				es = ext4_es_try_to_merge_right(inode, es);
				goto out;
			}
			p = &(*p)->rb_right;
		} else {
			BUG();
			return -EINVAL;
		}
	}

	if (prealloc)
		es = prealloc;
	else
		es = __es_alloc_extent(false);
	if (!es)
		return -ENOMEM;
	ext4_es_init_extent(inode, es, newes->es_lblk, newes->es_len,
			    newes->es_pblk);

	rb_link_node(&es->rb_node, parent, p);
	rb_insert_color(&es->rb_node, &tree->root);

out:
	tree->cache_es = es;
	return 0;
}

/*
 * ext4_es_insert_extent() adds information to an inode's extent
 * status tree.
 *
 * Return 0 on success, error code on failure.
 */
int ext4_es_insert_extent(struct inode *inode, ext4_lblk_t lblk,
			  ext4_lblk_t len, ext4_fsblk_t pblk,
			  unsigned int status)
{
	struct extent_status newes;
	ext4_lblk_t end = lblk + len - 1;
	int err = 0;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return 0;

	es_debug("add [%u/%u) %llu %x to extent status tree of inode %lu\n",
		 lblk, len, pblk, status, inode->i_ino);

	if (!len)
		return 0;

	BUG_ON(end < lblk);

	if ((status & EXTENT_STATUS_DELAYED) &&
	    (status & EXTENT_STATUS_WRITTEN)) {
		ext4_warning(inode->i_sb, "Inserting extent [%u/%u] as "
				" delayed and written which can potentially "
				" cause data loss.", lblk, len);
		WARN_ON(1);
	}

	newes.es_lblk = lblk;
	newes.es_len = len;
	ext4_es_store_pblock_status(&newes, pblk, status);
	trace_ext4_es_insert_extent(inode, &newes);

	ext4_es_insert_extent_check(inode, &newes);

	write_lock(&EXT4_I(inode)->i_es_lock);
	err = __es_remove_extent(inode, lblk, end, NULL, NULL);
	if (err != 0)
		goto error;
retry:
	err = __es_insert_extent(inode, &newes, NULL);
	if (err == -ENOMEM && __es_shrink(EXT4_SB(inode->i_sb),
					  128, EXT4_I(inode)))
		goto retry;
	if (err == -ENOMEM && !ext4_es_must_keep(&newes))
		err = 0;

	if (sbi->s_cluster_ratio > 1 && test_opt(inode->i_sb, DELALLOC) &&
	    (status & EXTENT_STATUS_WRITTEN ||
	     status & EXTENT_STATUS_UNWRITTEN))
		__revise_pending(inode, lblk, len);

error:
	write_unlock(&EXT4_I(inode)->i_es_lock);

	ext4_es_print_tree(inode);

	return err;
}

/*
 * ext4_es_cache_extent() inserts information into the extent status
 * tree if and only if there isn't information about the range in
 * question already.
 */
void ext4_es_cache_extent(struct inode *inode, ext4_lblk_t lblk,
			  ext4_lblk_t len, ext4_fsblk_t pblk,
			  unsigned int status)
{
	struct extent_status *es;
	struct extent_status newes;
	ext4_lblk_t end = lblk + len - 1;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return;

	newes.es_lblk = lblk;
	newes.es_len = len;
	ext4_es_store_pblock_status(&newes, pblk, status);
	trace_ext4_es_cache_extent(inode, &newes);

	if (!len)
		return;

	BUG_ON(end < lblk);

	write_lock(&EXT4_I(inode)->i_es_lock);

	es = __es_tree_search(&EXT4_I(inode)->i_es_tree.root, lblk);
	if (!es || es->es_lblk > end)
		__es_insert_extent(inode, &newes, NULL);
	write_unlock(&EXT4_I(inode)->i_es_lock);
}

/*
 * ext4_es_lookup_extent() looks up an extent in extent status tree.
 *
 * ext4_es_lookup_extent is called by ext4_map_blocks/ext4_da_map_blocks.
 *
 * Return: 1 on found, 0 on not
 */
int ext4_es_lookup_extent(struct inode *inode, ext4_lblk_t lblk,
			  ext4_lblk_t *next_lblk,
			  struct extent_status *es)
{
	struct ext4_es_tree *tree;
	struct ext4_es_stats *stats;
	struct extent_status *es1 = NULL;
	struct rb_node *node;
	int found = 0;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return 0;

	trace_ext4_es_lookup_extent_enter(inode, lblk);
	es_debug("lookup extent in block %u\n", lblk);

	tree = &EXT4_I(inode)->i_es_tree;
	read_lock(&EXT4_I(inode)->i_es_lock);

	/* find extent in cache firstly */
	es->es_lblk = es->es_len = es->es_pblk = 0;
	es1 = READ_ONCE(tree->cache_es);
	if (es1 && in_range(lblk, es1->es_lblk, es1->es_len)) {
		es_debug("%u cached by [%u/%u)\n",
			 lblk, es1->es_lblk, es1->es_len);
		found = 1;
		goto out;
	}

	node = tree->root.rb_node;
	while (node) {
		es1 = rb_entry(node, struct extent_status, rb_node);
		if (lblk < es1->es_lblk)
			node = node->rb_left;
		else if (lblk > ext4_es_end(es1))
			node = node->rb_right;
		else {
			found = 1;
			break;
		}
	}

out:
	stats = &EXT4_SB(inode->i_sb)->s_es_stats;
	if (found) {
		BUG_ON(!es1);
		es->es_lblk = es1->es_lblk;
		es->es_len = es1->es_len;
		es->es_pblk = es1->es_pblk;
		if (!ext4_es_is_referenced(es1))
			ext4_es_set_referenced(es1);
		percpu_counter_inc(&stats->es_stats_cache_hits);
		if (next_lblk) {
			node = rb_next(&es1->rb_node);
			if (node) {
				es1 = rb_entry(node, struct extent_status,
					       rb_node);
				*next_lblk = es1->es_lblk;
			} else
				*next_lblk = 0;
		}
	} else {
		percpu_counter_inc(&stats->es_stats_cache_misses);
	}

	read_unlock(&EXT4_I(inode)->i_es_lock);

	trace_ext4_es_lookup_extent_exit(inode, es, found);
	return found;
}

struct rsvd_count {
	int ndelonly;
	bool first_do_lblk_found;
	ext4_lblk_t first_do_lblk;
	ext4_lblk_t last_do_lblk;
	struct extent_status *left_es;
	bool partial;
	ext4_lblk_t lclu;
};

/*
 * init_rsvd - initialize reserved count data before removing block range
 *	       in file from extent status tree
 *
 * @inode - file containing range
 * @lblk - first block in range
 * @es - pointer to first extent in range
 * @rc - pointer to reserved count data
 *
 * Assumes es is not NULL
 */
static void init_rsvd(struct inode *inode, ext4_lblk_t lblk,
		      struct extent_status *es, struct rsvd_count *rc)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct rb_node *node;

	rc->ndelonly = 0;

	/*
	 * for bigalloc, note the first delonly block in the range has not
	 * been found, record the extent containing the block to the left of
	 * the region to be removed, if any, and note that there's no partial
	 * cluster to track
	 */
	if (sbi->s_cluster_ratio > 1) {
		rc->first_do_lblk_found = false;
		if (lblk > es->es_lblk) {
			rc->left_es = es;
		} else {
			node = rb_prev(&es->rb_node);
			rc->left_es = node ? rb_entry(node,
						      struct extent_status,
						      rb_node) : NULL;
		}
		rc->partial = false;
	}
}

/*
 * count_rsvd - count the clusters containing delayed and not unwritten
 *		(delonly) blocks in a range within an extent and add to
 *	        the running tally in rsvd_count
 *
 * @inode - file containing extent
 * @lblk - first block in range
 * @len - length of range in blocks
 * @es - pointer to extent containing clusters to be counted
 * @rc - pointer to reserved count data
 *
 * Tracks partial clusters found at the beginning and end of extents so
 * they aren't overcounted when they span adjacent extents
 */
static void count_rsvd(struct inode *inode, ext4_lblk_t lblk, long len,
		       struct extent_status *es, struct rsvd_count *rc)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	ext4_lblk_t i, end, nclu;

	if (!ext4_es_is_delonly(es))
		return;

	WARN_ON(len <= 0);

	if (sbi->s_cluster_ratio == 1) {
		rc->ndelonly += (int) len;
		return;
	}

	/* bigalloc */

	i = (lblk < es->es_lblk) ? es->es_lblk : lblk;
	end = lblk + (ext4_lblk_t) len - 1;
	end = (end > ext4_es_end(es)) ? ext4_es_end(es) : end;

	/* record the first block of the first delonly extent seen */
	if (!rc->first_do_lblk_found) {
		rc->first_do_lblk = i;
		rc->first_do_lblk_found = true;
	}

	/* update the last lblk in the region seen so far */
	rc->last_do_lblk = end;

	/*
	 * if we're tracking a partial cluster and the current extent
	 * doesn't start with it, count it and stop tracking
	 */
	if (rc->partial && (rc->lclu != EXT4_B2C(sbi, i))) {
		rc->ndelonly++;
		rc->partial = false;
	}

	/*
	 * if the first cluster doesn't start on a cluster boundary but
	 * ends on one, count it
	 */
	if (EXT4_LBLK_COFF(sbi, i) != 0) {
		if (end >= EXT4_LBLK_CFILL(sbi, i)) {
			rc->ndelonly++;
			rc->partial = false;
			i = EXT4_LBLK_CFILL(sbi, i) + 1;
		}
	}

	/*
	 * if the current cluster starts on a cluster boundary, count the
	 * number of whole delonly clusters in the extent
	 */
	if ((i + sbi->s_cluster_ratio - 1) <= end) {
		nclu = (end - i + 1) >> sbi->s_cluster_bits;
		rc->ndelonly += nclu;
		i += nclu << sbi->s_cluster_bits;
	}

	/*
	 * start tracking a partial cluster if there's a partial at the end
	 * of the current extent and we're not already tracking one
	 */
	if (!rc->partial && i <= end) {
		rc->partial = true;
		rc->lclu = EXT4_B2C(sbi, i);
	}
}

/*
 * __pr_tree_search - search for a pending cluster reservation
 *
 * @root - root of pending reservation tree
 * @lclu - logical cluster to search for
 *
 * Returns the pending reservation for the cluster identified by @lclu
 * if found.  If not, returns a reservation for the next cluster if any,
 * and if not, returns NULL.
 */
static struct pending_reservation *__pr_tree_search(struct rb_root *root,
						    ext4_lblk_t lclu)
{
	struct rb_node *node = root->rb_node;
	struct pending_reservation *pr = NULL;

	while (node) {
		pr = rb_entry(node, struct pending_reservation, rb_node);
		if (lclu < pr->lclu)
			node = node->rb_left;
		else if (lclu > pr->lclu)
			node = node->rb_right;
		else
			return pr;
	}
	if (pr && lclu < pr->lclu)
		return pr;
	if (pr && lclu > pr->lclu) {
		node = rb_next(&pr->rb_node);
		return node ? rb_entry(node, struct pending_reservation,
				       rb_node) : NULL;
	}
	return NULL;
}

/*
 * get_rsvd - calculates and returns the number of cluster reservations to be
 *	      released when removing a block range from the extent status tree
 *	      and releases any pending reservations within the range
 *
 * @inode - file containing block range
 * @end - last block in range
 * @right_es - pointer to extent containing next block beyond end or NULL
 * @rc - pointer to reserved count data
 *
 * The number of reservations to be released is equal to the number of
 * clusters containing delayed and not unwritten (delonly) blocks within
 * the range, minus the number of clusters still containing delonly blocks
 * at the ends of the range, and minus the number of pending reservations
 * within the range.
 */
static unsigned int get_rsvd(struct inode *inode, ext4_lblk_t end,
			     struct extent_status *right_es,
			     struct rsvd_count *rc)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct pending_reservation *pr;
	struct ext4_pending_tree *tree = &EXT4_I(inode)->i_pending_tree;
	struct rb_node *node;
	ext4_lblk_t first_lclu, last_lclu;
	bool left_delonly, right_delonly, count_pending;
	struct extent_status *es;

	if (sbi->s_cluster_ratio > 1) {
		/* count any remaining partial cluster */
		if (rc->partial)
			rc->ndelonly++;

		if (rc->ndelonly == 0)
			return 0;

		first_lclu = EXT4_B2C(sbi, rc->first_do_lblk);
		last_lclu = EXT4_B2C(sbi, rc->last_do_lblk);

		/*
		 * decrease the delonly count by the number of clusters at the
		 * ends of the range that still contain delonly blocks -
		 * these clusters still need to be reserved
		 */
		left_delonly = right_delonly = false;

		es = rc->left_es;
		while (es && ext4_es_end(es) >=
		       EXT4_LBLK_CMASK(sbi, rc->first_do_lblk)) {
			if (ext4_es_is_delonly(es)) {
				rc->ndelonly--;
				left_delonly = true;
				break;
			}
			node = rb_prev(&es->rb_node);
			if (!node)
				break;
			es = rb_entry(node, struct extent_status, rb_node);
		}
		if (right_es && (!left_delonly || first_lclu != last_lclu)) {
			if (end < ext4_es_end(right_es)) {
				es = right_es;
			} else {
				node = rb_next(&right_es->rb_node);
				es = node ? rb_entry(node, struct extent_status,
						     rb_node) : NULL;
			}
			while (es && es->es_lblk <=
			       EXT4_LBLK_CFILL(sbi, rc->last_do_lblk)) {
				if (ext4_es_is_delonly(es)) {
					rc->ndelonly--;
					right_delonly = true;
					break;
				}
				node = rb_next(&es->rb_node);
				if (!node)
					break;
				es = rb_entry(node, struct extent_status,
					      rb_node);
			}
		}

		/*
		 * Determine the block range that should be searched for
		 * pending reservations, if any.  Clusters on the ends of the
		 * original removed range containing delonly blocks are
		 * excluded.  They've already been accounted for and it's not
		 * possible to determine if an associated pending reservation
		 * should be released with the information available in the
		 * extents status tree.
		 */
		if (first_lclu == last_lclu) {
			if (left_delonly | right_delonly)
				count_pending = false;
			else
				count_pending = true;
		} else {
			if (left_delonly)
				first_lclu++;
			if (right_delonly)
				last_lclu--;
			if (first_lclu <= last_lclu)
				count_pending = true;
			else
				count_pending = false;
		}

		/*
		 * a pending reservation found between first_lclu and last_lclu
		 * represents an allocated cluster that contained at least one
		 * delonly block, so the delonly total must be reduced by one
		 * for each pending reservation found and released
		 */
		if (count_pending) {
			pr = __pr_tree_search(&tree->root, first_lclu);
			while (pr && pr->lclu <= last_lclu) {
				rc->ndelonly--;
				node = rb_next(&pr->rb_node);
				rb_erase(&pr->rb_node, &tree->root);
				kmem_cache_free(ext4_pending_cachep, pr);
				if (!node)
					break;
				pr = rb_entry(node, struct pending_reservation,
					      rb_node);
			}
		}
	}
	return rc->ndelonly;
}


/*
 * __es_remove_extent - removes block range from extent status tree
 *
 * @inode - file containing range
 * @lblk - first block in range
 * @end - last block in range
 * @reserved - number of cluster reservations released
 * @prealloc - pre-allocated es to avoid memory allocation failures
 *
 * If @reserved is not NULL and delayed allocation is enabled, counts
 * block/cluster reservations freed by removing range and if bigalloc
 * enabled cancels pending reservations as needed. Returns 0 on success,
 * error code on failure.
 */
static int __es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			      ext4_lblk_t end, int *reserved,
			      struct extent_status *prealloc)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct rb_node *node;
	struct extent_status *es;
	struct extent_status orig_es;
	ext4_lblk_t len1, len2;
	ext4_fsblk_t block;
	int err = 0;
	bool count_reserved = true;
	struct rsvd_count rc;

	if (reserved == NULL || !test_opt(inode->i_sb, DELALLOC))
		count_reserved = false;

	es = __es_tree_search(&tree->root, lblk);
	if (!es)
		goto out;
	if (es->es_lblk > end)
		goto out;

	/* Simply invalidate cache_es. */
	tree->cache_es = NULL;
	if (count_reserved)
		init_rsvd(inode, lblk, es, &rc);

	orig_es.es_lblk = es->es_lblk;
	orig_es.es_len = es->es_len;
	orig_es.es_pblk = es->es_pblk;

	len1 = lblk > es->es_lblk ? lblk - es->es_lblk : 0;
	len2 = ext4_es_end(es) > end ? ext4_es_end(es) - end : 0;
	if (len1 > 0)
		es->es_len = len1;
	if (len2 > 0) {
		if (len1 > 0) {
			struct extent_status newes;

			newes.es_lblk = end + 1;
			newes.es_len = len2;
			block = 0x7FDEADBEEFULL;
			if (ext4_es_is_written(&orig_es) ||
			    ext4_es_is_unwritten(&orig_es))
				block = ext4_es_pblock(&orig_es) +
					orig_es.es_len - len2;
			ext4_es_store_pblock_status(&newes, block,
						    ext4_es_status(&orig_es));
			err = __es_insert_extent(inode, &newes, prealloc);
			if (err) {
				if (!ext4_es_must_keep(&newes))
					return 0;

				es->es_lblk = orig_es.es_lblk;
				es->es_len = orig_es.es_len;
				goto out;
			}
		} else {
			es->es_lblk = end + 1;
			es->es_len = len2;
			if (ext4_es_is_written(es) ||
			    ext4_es_is_unwritten(es)) {
				block = orig_es.es_pblk + orig_es.es_len - len2;
				ext4_es_store_pblock(es, block);
			}
		}
		if (count_reserved)
			count_rsvd(inode, orig_es.es_lblk + len1,
				   orig_es.es_len - len1 - len2, &orig_es, &rc);
		goto out_get_reserved;
	}

	if (len1 > 0) {
		if (count_reserved)
			count_rsvd(inode, lblk, orig_es.es_len - len1,
				   &orig_es, &rc);
		node = rb_next(&es->rb_node);
		if (node)
			es = rb_entry(node, struct extent_status, rb_node);
		else
			es = NULL;
	}

	while (es && ext4_es_end(es) <= end) {
		if (count_reserved)
			count_rsvd(inode, es->es_lblk, es->es_len, es, &rc);
		node = rb_next(&es->rb_node);
		rb_erase(&es->rb_node, &tree->root);
		ext4_es_free_extent(inode, es);
		if (!node) {
			es = NULL;
			break;
		}
		es = rb_entry(node, struct extent_status, rb_node);
	}

	if (es && es->es_lblk < end + 1) {
		ext4_lblk_t orig_len = es->es_len;

		len1 = ext4_es_end(es) - end;
		if (count_reserved)
			count_rsvd(inode, es->es_lblk, orig_len - len1,
				   es, &rc);
		es->es_lblk = end + 1;
		es->es_len = len1;
		if (ext4_es_is_written(es) || ext4_es_is_unwritten(es)) {
			block = es->es_pblk + orig_len - len1;
			ext4_es_store_pblock(es, block);
		}
	}

out_get_reserved:
	if (count_reserved)
		*reserved = get_rsvd(inode, end, es, &rc);
out:
	return err;
}

/*
 * ext4_es_remove_extent - removes block range from extent status tree
 *
 * @inode - file containing range
 * @lblk - first block in range
 * @len - number of blocks to remove
 *
 * Reduces block/cluster reservation count and for bigalloc cancels pending
 * reservations as needed. Returns 0 on success, error code on failure.
 */
int ext4_es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			  ext4_lblk_t len)
{
	ext4_lblk_t end;
	int err = 0;
	int reserved = 0;
	struct extent_status *es = NULL;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return 0;

	trace_ext4_es_remove_extent(inode, lblk, len);
	es_debug("remove [%u/%u) from extent status tree of inode %lu\n",
		 lblk, len, inode->i_ino);

	if (!len)
		return err;

	end = lblk + len - 1;
	BUG_ON(end < lblk);

retry:
	if (err && !es)
		es = __es_alloc_extent(true);
	/*
	 * ext4_clear_inode() depends on us taking i_es_lock unconditionally
	 * so that we are sure __es_shrink() is done with the inode before it
	 * is reclaimed.
	 */
	write_lock(&EXT4_I(inode)->i_es_lock);
	err = __es_remove_extent(inode, lblk, end, &reserved, es);
	if (es && !es->es_len)
		__es_free_extent(es);
	write_unlock(&EXT4_I(inode)->i_es_lock);
	if (err)
		goto retry;

	ext4_es_print_tree(inode);
	ext4_da_release_space(inode, reserved);
	return 0;
}

static int __es_shrink(struct ext4_sb_info *sbi, int nr_to_scan,
		       struct ext4_inode_info *locked_ei)
{
	struct ext4_inode_info *ei;
	struct ext4_es_stats *es_stats;
	ktime_t start_time;
	u64 scan_time;
	int nr_to_walk;
	int nr_shrunk = 0;
	int retried = 0, nr_skipped = 0;

	es_stats = &sbi->s_es_stats;
	start_time = ktime_get();

retry:
	spin_lock(&sbi->s_es_lock);
	nr_to_walk = sbi->s_es_nr_inode;
	while (nr_to_walk-- > 0) {
		if (list_empty(&sbi->s_es_list)) {
			spin_unlock(&sbi->s_es_lock);
			goto out;
		}
		ei = list_first_entry(&sbi->s_es_list, struct ext4_inode_info,
				      i_es_list);
		/* Move the inode to the tail */
		list_move_tail(&ei->i_es_list, &sbi->s_es_list);

		/*
		 * Normally we try hard to avoid shrinking precached inodes,
		 * but we will as a last resort.
		 */
		if (!retried && ext4_test_inode_state(&ei->vfs_inode,
						EXT4_STATE_EXT_PRECACHED)) {
			nr_skipped++;
			continue;
		}

		if (ei == locked_ei || !write_trylock(&ei->i_es_lock)) {
			nr_skipped++;
			continue;
		}
		/*
		 * Now we hold i_es_lock which protects us from inode reclaim
		 * freeing inode under us
		 */
		spin_unlock(&sbi->s_es_lock);

		nr_shrunk += es_reclaim_extents(ei, &nr_to_scan);
		write_unlock(&ei->i_es_lock);

		if (nr_to_scan <= 0)
			goto out;
		spin_lock(&sbi->s_es_lock);
	}
	spin_unlock(&sbi->s_es_lock);

	/*
	 * If we skipped any inodes, and we weren't able to make any
	 * forward progress, try again to scan precached inodes.
	 */
	if ((nr_shrunk == 0) && nr_skipped && !retried) {
		retried++;
		goto retry;
	}

	if (locked_ei && nr_shrunk == 0)
		nr_shrunk = es_reclaim_extents(locked_ei, &nr_to_scan);

out:
	scan_time = ktime_to_ns(ktime_sub(ktime_get(), start_time));
	if (likely(es_stats->es_stats_scan_time))
		es_stats->es_stats_scan_time = (scan_time +
				es_stats->es_stats_scan_time*3) / 4;
	else
		es_stats->es_stats_scan_time = scan_time;
	if (scan_time > es_stats->es_stats_max_scan_time)
		es_stats->es_stats_max_scan_time = scan_time;
	if (likely(es_stats->es_stats_shrunk))
		es_stats->es_stats_shrunk = (nr_shrunk +
				es_stats->es_stats_shrunk*3) / 4;
	else
		es_stats->es_stats_shrunk = nr_shrunk;

	trace_ext4_es_shrink(sbi->s_sb, nr_shrunk, scan_time,
			     nr_skipped, retried);
	return nr_shrunk;
}

static unsigned long ext4_es_count(struct shrinker *shrink,
				   struct shrink_control *sc)
{
	unsigned long nr;
	struct ext4_sb_info *sbi;

	sbi = container_of(shrink, struct ext4_sb_info, s_es_shrinker);
	nr = percpu_counter_read_positive(&sbi->s_es_stats.es_stats_shk_cnt);
	trace_ext4_es_shrink_count(sbi->s_sb, sc->nr_to_scan, nr);
	return nr;
}

static unsigned long ext4_es_scan(struct shrinker *shrink,
				  struct shrink_control *sc)
{
	struct ext4_sb_info *sbi = container_of(shrink,
					struct ext4_sb_info, s_es_shrinker);
	int nr_to_scan = sc->nr_to_scan;
	int ret, nr_shrunk;

	ret = percpu_counter_read_positive(&sbi->s_es_stats.es_stats_shk_cnt);
	trace_ext4_es_shrink_scan_enter(sbi->s_sb, nr_to_scan, ret);

	nr_shrunk = __es_shrink(sbi, nr_to_scan, NULL);

	ret = percpu_counter_read_positive(&sbi->s_es_stats.es_stats_shk_cnt);
	trace_ext4_es_shrink_scan_exit(sbi->s_sb, nr_shrunk, ret);
	return nr_shrunk;
}

int ext4_seq_es_shrinker_info_show(struct seq_file *seq, void *v)
{
	struct ext4_sb_info *sbi = EXT4_SB((struct super_block *) seq->private);
	struct ext4_es_stats *es_stats = &sbi->s_es_stats;
	struct ext4_inode_info *ei, *max = NULL;
	unsigned int inode_cnt = 0;

	if (v != SEQ_START_TOKEN)
		return 0;

	/* here we just find an inode that has the max nr. of objects */
	spin_lock(&sbi->s_es_lock);
	list_for_each_entry(ei, &sbi->s_es_list, i_es_list) {
		inode_cnt++;
		if (max && max->i_es_all_nr < ei->i_es_all_nr)
			max = ei;
		else if (!max)
			max = ei;
	}
	spin_unlock(&sbi->s_es_lock);

	seq_printf(seq, "stats:\n  %lld objects\n  %lld reclaimable objects\n",
		   percpu_counter_sum_positive(&es_stats->es_stats_all_cnt),
		   percpu_counter_sum_positive(&es_stats->es_stats_shk_cnt));
	seq_printf(seq, "  %lld/%lld cache hits/misses\n",
		   percpu_counter_sum_positive(&es_stats->es_stats_cache_hits),
		   percpu_counter_sum_positive(&es_stats->es_stats_cache_misses));
	if (inode_cnt)
		seq_printf(seq, "  %d inodes on list\n", inode_cnt);

	seq_printf(seq, "average:\n  %llu us scan time\n",
	    div_u64(es_stats->es_stats_scan_time, 1000));
	seq_printf(seq, "  %lu shrunk objects\n", es_stats->es_stats_shrunk);
	if (inode_cnt)
		seq_printf(seq,
		    "maximum:\n  %lu inode (%u objects, %u reclaimable)\n"
		    "  %llu us max scan time\n",
		    max->vfs_inode.i_ino, max->i_es_all_nr, max->i_es_shk_nr,
		    div_u64(es_stats->es_stats_max_scan_time, 1000));

	return 0;
}

int ext4_es_register_shrinker(struct ext4_sb_info *sbi)
{
	int err;

	/* Make sure we have enough bits for physical block number */
	BUILD_BUG_ON(ES_SHIFT < 48);
	INIT_LIST_HEAD(&sbi->s_es_list);
	sbi->s_es_nr_inode = 0;
	spin_lock_init(&sbi->s_es_lock);
	sbi->s_es_stats.es_stats_shrunk = 0;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_cache_hits, 0,
				  GFP_KERNEL);
	if (err)
		return err;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_cache_misses, 0,
				  GFP_KERNEL);
	if (err)
		goto err1;
	sbi->s_es_stats.es_stats_scan_time = 0;
	sbi->s_es_stats.es_stats_max_scan_time = 0;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_all_cnt, 0, GFP_KERNEL);
	if (err)
		goto err2;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_shk_cnt, 0, GFP_KERNEL);
	if (err)
		goto err3;

	sbi->s_es_shrinker.scan_objects = ext4_es_scan;
	sbi->s_es_shrinker.count_objects = ext4_es_count;
	sbi->s_es_shrinker.seeks = DEFAULT_SEEKS;
	err = register_shrinker(&sbi->s_es_shrinker, "ext4-es:%s",
				sbi->s_sb->s_id);
	if (err)
		goto err4;

	return 0;
err4:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_shk_cnt);
err3:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_all_cnt);
err2:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_cache_misses);
err1:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_cache_hits);
	return err;
}

void ext4_es_unregister_shrinker(struct ext4_sb_info *sbi)
{
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_cache_hits);
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_cache_misses);
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_all_cnt);
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_shk_cnt);
	unregister_shrinker(&sbi->s_es_shrinker);
}

/*
 * Shrink extents in given inode from ei->i_es_shrink_lblk till end. Scan at
 * most *nr_to_scan extents, update *nr_to_scan accordingly.
 *
 * Return 0 if we hit end of tree / interval, 1 if we exhausted nr_to_scan.
 * Increment *nr_shrunk by the number of reclaimed extents. Also update
 * ei->i_es_shrink_lblk to where we should continue scanning.
 */
static int es_do_reclaim_extents(struct ext4_inode_info *ei, ext4_lblk_t end,
				 int *nr_to_scan, int *nr_shrunk)
{
	struct inode *inode = &ei->vfs_inode;
	struct ext4_es_tree *tree = &ei->i_es_tree;
	struct extent_status *es;
	struct rb_node *node;

	es = __es_tree_search(&tree->root, ei->i_es_shrink_lblk);
	if (!es)
		goto out_wrap;

	while (*nr_to_scan > 0) {
		if (es->es_lblk > end) {
			ei->i_es_shrink_lblk = end + 1;
			return 0;
		}

		(*nr_to_scan)--;
		node = rb_next(&es->rb_node);

		if (ext4_es_must_keep(es))
			goto next;
		if (ext4_es_is_referenced(es)) {
			ext4_es_clear_referenced(es);
			goto next;
		}

		rb_erase(&es->rb_node, &tree->root);
		ext4_es_free_extent(inode, es);
		(*nr_shrunk)++;
next:
		if (!node)
			goto out_wrap;
		es = rb_entry(node, struct extent_status, rb_node);
	}
	ei->i_es_shrink_lblk = es->es_lblk;
	return 1;
out_wrap:
	ei->i_es_shrink_lblk = 0;
	return 0;
}

static int es_reclaim_extents(struct ext4_inode_info *ei, int *nr_to_scan)
{
	struct inode *inode = &ei->vfs_inode;
	int nr_shrunk = 0;
	ext4_lblk_t start = ei->i_es_shrink_lblk;
	static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if (ei->i_es_shk_nr == 0)
		return 0;

	if (ext4_test_inode_state(inode, EXT4_STATE_EXT_PRECACHED) &&
	    __ratelimit(&_rs))
		ext4_warning(inode->i_sb, "forced shrink of precached extents");

	if (!es_do_reclaim_extents(ei, EXT_MAX_BLOCKS, nr_to_scan, &nr_shrunk) &&
	    start != 0)
		es_do_reclaim_extents(ei, start - 1, nr_to_scan, &nr_shrunk);

	ei->i_es_tree.cache_es = NULL;
	return nr_shrunk;
}

/*
 * Called to support EXT4_IOC_CLEAR_ES_CACHE.  We can only remove
 * discretionary entries from the extent status cache.  (Some entries
 * must be present for proper operations.)
 */
void ext4_clear_inode_es(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct extent_status *es;
	struct ext4_es_tree *tree;
	struct rb_node *node;

	write_lock(&ei->i_es_lock);
	tree = &EXT4_I(inode)->i_es_tree;
	tree->cache_es = NULL;
	node = rb_first(&tree->root);
	while (node) {
		es = rb_entry(node, struct extent_status, rb_node);
		node = rb_next(node);
		if (!ext4_es_must_keep(es)) {
			rb_erase(&es->rb_node, &tree->root);
			ext4_es_free_extent(inode, es);
		}
	}
	ext4_clear_inode_state(inode, EXT4_STATE_EXT_PRECACHED);
	write_unlock(&ei->i_es_lock);
}

#ifdef ES_DEBUG__
static void ext4_print_pending_tree(struct inode *inode)
{
	struct ext4_pending_tree *tree;
	struct rb_node *node;
	struct pending_reservation *pr;

	printk(KERN_DEBUG "pending reservations for inode %lu:", inode->i_ino);
	tree = &EXT4_I(inode)->i_pending_tree;
	node = rb_first(&tree->root);
	while (node) {
		pr = rb_entry(node, struct pending_reservation, rb_node);
		printk(KERN_DEBUG " %u", pr->lclu);
		node = rb_next(node);
	}
	printk(KERN_DEBUG "\n");
}
#else
#define ext4_print_pending_tree(inode)
#endif

int __init ext4_init_pending(void)
{
	ext4_pending_cachep = kmem_cache_create("ext4_pending_reservation",
					   sizeof(struct pending_reservation),
					   0, (SLAB_RECLAIM_ACCOUNT), NULL);
	if (ext4_pending_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void ext4_exit_pending(void)
{
	kmem_cache_destroy(ext4_pending_cachep);
}

void ext4_init_pending_tree(struct ext4_pending_tree *tree)
{
	tree->root = RB_ROOT;
}

/*
 * __get_pending - retrieve a pointer to a pending reservation
 *
 * @inode - file containing the pending cluster reservation
 * @lclu - logical cluster of interest
 *
 * Returns a pointer to a pending reservation if it's a member of
 * the set, and NULL if not.  Must be called holding i_es_lock.
 */
static struct pending_reservation *__get_pending(struct inode *inode,
						 ext4_lblk_t lclu)
{
	struct ext4_pending_tree *tree;
	struct rb_node *node;
	struct pending_reservation *pr = NULL;

	tree = &EXT4_I(inode)->i_pending_tree;
	node = (&tree->root)->rb_node;

	while (node) {
		pr = rb_entry(node, struct pending_reservation, rb_node);
		if (lclu < pr->lclu)
			node = node->rb_left;
		else if (lclu > pr->lclu)
			node = node->rb_right;
		else if (lclu == pr->lclu)
			return pr;
	}
	return NULL;
}

/*
 * __insert_pending - adds a pending cluster reservation to the set of
 *                    pending reservations
 *
 * @inode - file containing the cluster
 * @lblk - logical block in the cluster to be added
 *
 * Returns 0 on successful insertion and -ENOMEM on failure.  If the
 * pending reservation is already in the set, returns successfully.
 */
static int __insert_pending(struct inode *inode, ext4_lblk_t lblk)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_pending_tree *tree = &EXT4_I(inode)->i_pending_tree;
	struct rb_node **p = &tree->root.rb_node;
	struct rb_node *parent = NULL;
	struct pending_reservation *pr;
	ext4_lblk_t lclu;
	int ret = 0;

	lclu = EXT4_B2C(sbi, lblk);
	/* search to find parent for insertion */
	while (*p) {
		parent = *p;
		pr = rb_entry(parent, struct pending_reservation, rb_node);

		if (lclu < pr->lclu) {
			p = &(*p)->rb_left;
		} else if (lclu > pr->lclu) {
			p = &(*p)->rb_right;
		} else {
			/* pending reservation already inserted */
			goto out;
		}
	}

	pr = kmem_cache_alloc(ext4_pending_cachep, GFP_ATOMIC);
	if (pr == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	pr->lclu = lclu;

	rb_link_node(&pr->rb_node, parent, p);
	rb_insert_color(&pr->rb_node, &tree->root);

out:
	return ret;
}

/*
 * __remove_pending - removes a pending cluster reservation from the set
 *                    of pending reservations
 *
 * @inode - file containing the cluster
 * @lblk - logical block in the pending cluster reservation to be removed
 *
 * Returns successfully if pending reservation is not a member of the set.
 */
static void __remove_pending(struct inode *inode, ext4_lblk_t lblk)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct pending_reservation *pr;
	struct ext4_pending_tree *tree;

	pr = __get_pending(inode, EXT4_B2C(sbi, lblk));
	if (pr != NULL) {
		tree = &EXT4_I(inode)->i_pending_tree;
		rb_erase(&pr->rb_node, &tree->root);
		kmem_cache_free(ext4_pending_cachep, pr);
	}
}

/*
 * ext4_remove_pending - removes a pending cluster reservation from the set
 *                       of pending reservations
 *
 * @inode - file containing the cluster
 * @lblk - logical block in the pending cluster reservation to be removed
 *
 * Locking for external use of __remove_pending.
 */
void ext4_remove_pending(struct inode *inode, ext4_lblk_t lblk)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	write_lock(&ei->i_es_lock);
	__remove_pending(inode, lblk);
	write_unlock(&ei->i_es_lock);
}

/*
 * ext4_is_pending - determine whether a cluster has a pending reservation
 *                   on it
 *
 * @inode - file containing the cluster
 * @lblk - logical block in the cluster
 *
 * Returns true if there's a pending reservation for the cluster in the
 * set of pending reservations, and false if not.
 */
bool ext4_is_pending(struct inode *inode, ext4_lblk_t lblk)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	bool ret;

	read_lock(&ei->i_es_lock);
	ret = (bool)(__get_pending(inode, EXT4_B2C(sbi, lblk)) != NULL);
	read_unlock(&ei->i_es_lock);

	return ret;
}

/*
 * ext4_es_insert_delayed_block - adds a delayed block to the extents status
 *                                tree, adding a pending reservation where
 *                                needed
 *
 * @inode - file containing the newly added block
 * @lblk - logical block to be added
 * @allocated - indicates whether a physical cluster has been allocated for
 *              the logical cluster that contains the block
 *
 * Returns 0 on success, negative error code on failure.
 */
int ext4_es_insert_delayed_block(struct inode *inode, ext4_lblk_t lblk,
				 bool allocated)
{
	struct extent_status newes;
	int err1 = 0;
	int err2 = 0;
	struct extent_status *es1 = NULL;
	struct extent_status *es2 = NULL;

	if (EXT4_SB(inode->i_sb)->s_mount_state & EXT4_FC_REPLAY)
		return 0;

	es_debug("add [%u/1) delayed to extent status tree of inode %lu\n",
		 lblk, inode->i_ino);

	newes.es_lblk = lblk;
	newes.es_len = 1;
	ext4_es_store_pblock_status(&newes, ~0, EXTENT_STATUS_DELAYED);
	trace_ext4_es_insert_delayed_block(inode, &newes, allocated);

	ext4_es_insert_extent_check(inode, &newes);

retry:
	if (err1 && !es1)
		es1 = __es_alloc_extent(true);
	if ((err1 || err2) && !es2)
		es2 = __es_alloc_extent(true);
	write_lock(&EXT4_I(inode)->i_es_lock);

	err1 = __es_remove_extent(inode, lblk, lblk, NULL, es1);
	if (err1 != 0)
		goto error;

	err2 = __es_insert_extent(inode, &newes, es2);
	if (err2 != 0)
		goto error;

	if (allocated)
		__insert_pending(inode, lblk);

	/* es is pre-allocated but not used, free it. */
	if (es1 && !es1->es_len)
		__es_free_extent(es1);
	if (es2 && !es2->es_len)
		__es_free_extent(es2);
error:
	write_unlock(&EXT4_I(inode)->i_es_lock);
	if (err1 || err2)
		goto retry;

	ext4_es_print_tree(inode);
	ext4_print_pending_tree(inode);
	return 0;
}

/*
 * __es_delayed_clu - count number of clusters containing blocks that
 *                    are delayed only
 *
 * @inode - file containing block range
 * @start - logical block defining start of range
 * @end - logical block defining end of range
 *
 * Returns the number of clusters containing only delayed (not delayed
 * and unwritten) blocks in the range specified by @start and @end.  Any
 * cluster or part of a cluster within the range and containing a delayed
 * and not unwritten block within the range is counted as a whole cluster.
 */
static unsigned int __es_delayed_clu(struct inode *inode, ext4_lblk_t start,
				     ext4_lblk_t end)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct extent_status *es;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct rb_node *node;
	ext4_lblk_t first_lclu, last_lclu;
	unsigned long long last_counted_lclu;
	unsigned int n = 0;

	/* guaranteed to be unequal to any ext4_lblk_t value */
	last_counted_lclu = ~0ULL;

	es = __es_tree_search(&tree->root, start);

	while (es && (es->es_lblk <= end)) {
		if (ext4_es_is_delonly(es)) {
			if (es->es_lblk <= start)
				first_lclu = EXT4_B2C(sbi, start);
			else
				first_lclu = EXT4_B2C(sbi, es->es_lblk);

			if (ext4_es_end(es) >= end)
				last_lclu = EXT4_B2C(sbi, end);
			else
				last_lclu = EXT4_B2C(sbi, ext4_es_end(es));

			if (first_lclu == last_counted_lclu)
				n += last_lclu - first_lclu;
			else
				n += last_lclu - first_lclu + 1;
			last_counted_lclu = last_lclu;
		}
		node = rb_next(&es->rb_node);
		if (!node)
			break;
		es = rb_entry(node, struct extent_status, rb_node);
	}

	return n;
}

/*
 * ext4_es_delayed_clu - count number of clusters containing blocks that
 *                       are both delayed and unwritten
 *
 * @inode - file containing block range
 * @lblk - logical block defining start of range
 * @len - number of blocks in range
 *
 * Locking for external use of __es_delayed_clu().
 */
unsigned int ext4_es_delayed_clu(struct inode *inode, ext4_lblk_t lblk,
				 ext4_lblk_t len)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	ext4_lblk_t end;
	unsigned int n;

	if (len == 0)
		return 0;

	end = lblk + len - 1;
	WARN_ON(end < lblk);

	read_lock(&ei->i_es_lock);

	n = __es_delayed_clu(inode, lblk, end);

	read_unlock(&ei->i_es_lock);

	return n;
}

/*
 * __revise_pending - makes, cancels, or leaves unchanged pending cluster
 *                    reservations for a specified block range depending
 *                    upon the presence or absence of delayed blocks
 *                    outside the range within clusters at the ends of the
 *                    range
 *
 * @inode - file containing the range
 * @lblk - logical block defining the start of range
 * @len  - length of range in blocks
 *
 * Used after a newly allocated extent is added to the extents status tree.
 * Requires that the extents in the range have either written or unwritten
 * status.  Must be called while holding i_es_lock.
 */
static void __revise_pending(struct inode *inode, ext4_lblk_t lblk,
			     ext4_lblk_t len)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	ext4_lblk_t end = lblk + len - 1;
	ext4_lblk_t first, last;
	bool f_del = false, l_del = false;

	if (len == 0)
		return;

	/*
	 * Two cases - block range within single cluster and block range
	 * spanning two or more clusters.  Note that a cluster belonging
	 * to a range starting and/or ending on a cluster boundary is treated
	 * as if it does not contain a delayed extent.  The new range may
	 * have allocated space for previously delayed blocks out to the
	 * cluster boundary, requiring that any pre-existing pending
	 * reservation be canceled.  Because this code only looks at blocks
	 * outside the range, it should revise pending reservations
	 * correctly even if the extent represented by the range can't be
	 * inserted in the extents status tree due to ENOSPC.
	 */

	if (EXT4_B2C(sbi, lblk) == EXT4_B2C(sbi, end)) {
		first = EXT4_LBLK_CMASK(sbi, lblk);
		if (first != lblk)
			f_del = __es_scan_range(inode, &ext4_es_is_delonly,
						first, lblk - 1);
		if (f_del) {
			__insert_pending(inode, first);
		} else {
			last = EXT4_LBLK_CMASK(sbi, end) +
			       sbi->s_cluster_ratio - 1;
			if (last != end)
				l_del = __es_scan_range(inode,
							&ext4_es_is_delonly,
							end + 1, last);
			if (l_del)
				__insert_pending(inode, last);
			else
				__remove_pending(inode, last);
		}
	} else {
		first = EXT4_LBLK_CMASK(sbi, lblk);
		if (first != lblk)
			f_del = __es_scan_range(inode, &ext4_es_is_delonly,
						first, lblk - 1);
		if (f_del)
			__insert_pending(inode, first);
		else
			__remove_pending(inode, first);

		last = EXT4_LBLK_CMASK(sbi, end) + sbi->s_cluster_ratio - 1;
		if (last != end)
			l_del = __es_scan_range(inode, &ext4_es_is_delonly,
						end + 1, last);
		if (l_del)
			__insert_pending(inode, last);
		else
			__remove_pending(inode, last);
	}
}
