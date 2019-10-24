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

static int __es_insert_extent(struct inode *inode, struct extent_status *newes);
static int __es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			      ext4_lblk_t end);
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
	if (tree->cache_es) {
		es1 = tree->cache_es;
		if (in_range(lblk, es1->es_lblk, es1->es_len)) {
			es_debug("%u cached by [%u/%u) %llu %x\n",
				 lblk, es1->es_lblk, es1->es_len,
				 ext4_es_pblock(es1), ext4_es_status(es1));
			goto out;
		}
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
		tree->cache_es = es1;
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

static struct extent_status *
ext4_es_alloc_extent(struct inode *inode, ext4_lblk_t lblk, ext4_lblk_t len,
		     ext4_fsblk_t pblk)
{
	struct extent_status *es;
	es = kmem_cache_alloc(ext4_es_cachep, GFP_ATOMIC);
	if (es == NULL)
		return NULL;
	es->es_lblk = lblk;
	es->es_len = len;
	es->es_pblk = pblk;

	/*
	 * We don't count delayed extent because we never try to reclaim them
	 */
	if (!ext4_es_is_delayed(es)) {
		if (!EXT4_I(inode)->i_es_shk_nr++)
			ext4_es_list_add(inode);
		percpu_counter_inc(&EXT4_SB(inode->i_sb)->
					s_es_stats.es_stats_shk_cnt);
	}

	EXT4_I(inode)->i_es_all_nr++;
	percpu_counter_inc(&EXT4_SB(inode->i_sb)->s_es_stats.es_stats_all_cnt);

	return es;
}

static void ext4_es_free_extent(struct inode *inode, struct extent_status *es)
{
	EXT4_I(inode)->i_es_all_nr--;
	percpu_counter_dec(&EXT4_SB(inode->i_sb)->s_es_stats.es_stats_all_cnt);

	/* Decrease the shrink counter when this es is not delayed */
	if (!ext4_es_is_delayed(es)) {
		BUG_ON(EXT4_I(inode)->i_es_shk_nr == 0);
		if (!--EXT4_I(inode)->i_es_shk_nr)
			ext4_es_list_del(inode);
		percpu_counter_dec(&EXT4_SB(inode->i_sb)->
					s_es_stats.es_stats_shk_cnt);
	}

	kmem_cache_free(ext4_es_cachep, es);
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
	ext4_ext_drop_refs(path);
	kfree(path);
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

static int __es_insert_extent(struct inode *inode, struct extent_status *newes)
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

	es = ext4_es_alloc_extent(inode, newes->es_lblk, newes->es_len,
				  newes->es_pblk);
	if (!es)
		return -ENOMEM;
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
	err = __es_remove_extent(inode, lblk, end);
	if (err != 0)
		goto error;
retry:
	err = __es_insert_extent(inode, &newes);
	if (err == -ENOMEM && __es_shrink(EXT4_SB(inode->i_sb),
					  128, EXT4_I(inode)))
		goto retry;
	if (err == -ENOMEM && !ext4_es_is_delayed(&newes))
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
		__es_insert_extent(inode, &newes);
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
			  struct extent_status *es)
{
	struct ext4_es_tree *tree;
	struct ext4_es_stats *stats;
	struct extent_status *es1 = NULL;
	struct rb_node *node;
	int found = 0;

	trace_ext4_es_lookup_extent_enter(inode, lblk);
	es_debug("lookup extent in block %u\n", lblk);

	tree = &EXT4_I(inode)->i_es_tree;
	read_lock(&EXT4_I(inode)->i_es_lock);

	/* find extent in cache firstly */
	es->es_lblk = es->es_len = es->es_pblk = 0;
	if (tree->cache_es) {
		es1 = tree->cache_es;
		if (in_range(lblk, es1->es_lblk, es1->es_len)) {
			es_debug("%u cached by [%u/%u)\n",
				 lblk, es1->es_lblk, es1->es_len);
			found = 1;
			goto out;
		}
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
		stats->es_stats_cache_hits++;
	} else {
		stats->es_stats_cache_misses++;
	}

	read_unlock(&EXT4_I(inode)->i_es_lock);

	trace_ext4_es_lookup_extent_exit(inode, es, found);
	return found;
}

static int __es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			      ext4_lblk_t end)
{
	struct ext4_es_tree *tree = &EXT4_I(inode)->i_es_tree;
	struct rb_node *node;
	struct extent_status *es;
	struct extent_status orig_es;
	ext4_lblk_t len1, len2;
	ext4_fsblk_t block;
	int err;

retry:
	err = 0;
	es = __es_tree_search(&tree->root, lblk);
	if (!es)
		goto out;
	if (es->es_lblk > end)
		goto out;

	/* Simply invalidate cache_es. */
	tree->cache_es = NULL;

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
			err = __es_insert_extent(inode, &newes);
			if (err) {
				es->es_lblk = orig_es.es_lblk;
				es->es_len = orig_es.es_len;
				if ((err == -ENOMEM) &&
				    __es_shrink(EXT4_SB(inode->i_sb),
							128, EXT4_I(inode)))
					goto retry;
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
		goto out;
	}

	if (len1 > 0) {
		node = rb_next(&es->rb_node);
		if (node)
			es = rb_entry(node, struct extent_status, rb_node);
		else
			es = NULL;
	}

	while (es && ext4_es_end(es) <= end) {
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
		es->es_lblk = end + 1;
		es->es_len = len1;
		if (ext4_es_is_written(es) || ext4_es_is_unwritten(es)) {
			block = es->es_pblk + orig_len - len1;
			ext4_es_store_pblock(es, block);
		}
	}

out:
	return err;
}

/*
 * ext4_es_remove_extent() removes a space from a extent status tree.
 *
 * Return 0 on success, error code on failure.
 */
int ext4_es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			  ext4_lblk_t len)
{
	ext4_lblk_t end;
	int err = 0;

	trace_ext4_es_remove_extent(inode, lblk, len);
	es_debug("remove [%u/%u) from extent status tree of inode %lu\n",
		 lblk, len, inode->i_ino);

	if (!len)
		return err;

	end = lblk + len - 1;
	BUG_ON(end < lblk);

	/*
	 * ext4_clear_inode() depends on us taking i_es_lock unconditionally
	 * so that we are sure __es_shrink() is done with the inode before it
	 * is reclaimed.
	 */
	write_lock(&EXT4_I(inode)->i_es_lock);
	err = __es_remove_extent(inode, lblk, end);
	write_unlock(&EXT4_I(inode)->i_es_lock);
	ext4_es_print_tree(inode);
	return err;
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

	if (!nr_to_scan)
		return ret;

	nr_shrunk = __es_shrink(sbi, nr_to_scan, NULL);

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
	seq_printf(seq, "  %lu/%lu cache hits/misses\n",
		   es_stats->es_stats_cache_hits,
		   es_stats->es_stats_cache_misses);
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
	sbi->s_es_stats.es_stats_cache_hits = 0;
	sbi->s_es_stats.es_stats_cache_misses = 0;
	sbi->s_es_stats.es_stats_scan_time = 0;
	sbi->s_es_stats.es_stats_max_scan_time = 0;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_all_cnt, 0, GFP_KERNEL);
	if (err)
		return err;
	err = percpu_counter_init(&sbi->s_es_stats.es_stats_shk_cnt, 0, GFP_KERNEL);
	if (err)
		goto err1;

	sbi->s_es_shrinker.scan_objects = ext4_es_scan;
	sbi->s_es_shrinker.count_objects = ext4_es_count;
	sbi->s_es_shrinker.seeks = DEFAULT_SEEKS;
	err = register_shrinker(&sbi->s_es_shrinker);
	if (err)
		goto err2;

	return 0;

err2:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_shk_cnt);
err1:
	percpu_counter_destroy(&sbi->s_es_stats.es_stats_all_cnt);
	return err;
}

void ext4_es_unregister_shrinker(struct ext4_sb_info *sbi)
{
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
		/*
		 * We can't reclaim delayed extent from status tree because
		 * fiemap, bigallic, and seek_data/hole need to use it.
		 */
		if (ext4_es_is_delayed(es))
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
	int err = 0;

	es_debug("add [%u/1) delayed to extent status tree of inode %lu\n",
		 lblk, inode->i_ino);

	newes.es_lblk = lblk;
	newes.es_len = 1;
	ext4_es_store_pblock_status(&newes, ~0, EXTENT_STATUS_DELAYED);
	trace_ext4_es_insert_delayed_block(inode, &newes, allocated);

	ext4_es_insert_extent_check(inode, &newes);

	write_lock(&EXT4_I(inode)->i_es_lock);

	err = __es_remove_extent(inode, lblk, lblk);
	if (err != 0)
		goto error;
retry:
	err = __es_insert_extent(inode, &newes);
	if (err == -ENOMEM && __es_shrink(EXT4_SB(inode->i_sb),
					  128, EXT4_I(inode)))
		goto retry;
	if (err != 0)
		goto error;

	if (allocated)
		__insert_pending(inode, lblk);

error:
	write_unlock(&EXT4_I(inode)->i_es_lock);

	ext4_es_print_tree(inode);
	ext4_print_pending_tree(inode);

	return err;
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

/*
 * ext4_es_remove_blks - remove block range from extents status tree and
 *                       reduce reservation count or cancel pending
 *                       reservation as needed
 *
 * @inode - file containing range
 * @lblk - first block in range
 * @len - number of blocks to remove
 *
 */
void ext4_es_remove_blks(struct inode *inode, ext4_lblk_t lblk,
			 ext4_lblk_t len)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	unsigned int clu_size, reserved = 0;
	ext4_lblk_t last_lclu, first, length, remainder, last;
	bool delonly;
	int err = 0;
	struct pending_reservation *pr;
	struct ext4_pending_tree *tree;

	/*
	 * Process cluster by cluster for bigalloc - there may be up to
	 * two clusters in a 4k page with a 1k block size and two blocks
	 * per cluster.  Also necessary for systems with larger page sizes
	 * and potentially larger block sizes.
	 */
	clu_size = sbi->s_cluster_ratio;
	last_lclu = EXT4_B2C(sbi, lblk + len - 1);

	write_lock(&EXT4_I(inode)->i_es_lock);

	for (first = lblk, remainder = len;
	     remainder > 0;
	     first += length, remainder -= length) {

		if (EXT4_B2C(sbi, first) == last_lclu)
			length = remainder;
		else
			length = clu_size - EXT4_LBLK_COFF(sbi, first);

		/*
		 * The BH_Delay flag, which triggers calls to this function,
		 * and the contents of the extents status tree can be
		 * inconsistent due to writepages activity. So, note whether
		 * the blocks to be removed actually belong to an extent with
		 * delayed only status.
		 */
		delonly = __es_scan_clu(inode, &ext4_es_is_delonly, first);

		/*
		 * because of the writepages effect, written and unwritten
		 * blocks could be removed here
		 */
		last = first + length - 1;
		err = __es_remove_extent(inode, first, last);
		if (err)
			ext4_warning(inode->i_sb,
				     "%s: couldn't remove page (err = %d)",
				     __func__, err);

		/* non-bigalloc case: simply count the cluster for release */
		if (sbi->s_cluster_ratio == 1 && delonly) {
			reserved++;
			continue;
		}

		/*
		 * bigalloc case: if all delayed allocated only blocks have
		 * just been removed from a cluster, either cancel a pending
		 * reservation if it exists or count a cluster for release
		 */
		if (delonly &&
		    !__es_scan_clu(inode, &ext4_es_is_delonly, first)) {
			pr = __get_pending(inode, EXT4_B2C(sbi, first));
			if (pr != NULL) {
				tree = &EXT4_I(inode)->i_pending_tree;
				rb_erase(&pr->rb_node, &tree->root);
				kmem_cache_free(ext4_pending_cachep, pr);
			} else {
				reserved++;
			}
		}
	}

	write_unlock(&EXT4_I(inode)->i_es_lock);

	ext4_da_release_space(inode, reserved);
}
