#include <linux/module.h>
#include <linux/radix-tree.h>
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"

static int find_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			    *orig_root, u64 num_blocks, u64 search_start, u64
			    search_end, struct btrfs_key *ins);
static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);
/*
 * pending extents are blocks that we're trying to allocate in the extent
 * map while trying to grow the map because of other allocations.  To avoid
 * recursing, they are tagged in the radix tree and cleaned up after
 * other allocations are done.  The pending tag is also used in the same
 * manner for deletes.
 */
#define CTREE_EXTENT_PENDING_DEL 0
#define CTREE_EXTENT_PINNED 1

static int inc_block_ref(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, u64 blocknr)
{
	struct btrfs_path path;
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *l;
	struct btrfs_extent_item *item;
	struct btrfs_key ins;
	u32 refs;

	find_free_extent(trans, root->fs_info->extent_root, 0, 0, (u64)-1,
			 &ins);
	btrfs_init_path(&path);
	key.objectid = blocknr;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = 1;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, &path,
				0, 1);
	if (ret != 0)
		BUG();
	BUG_ON(ret != 0);
	l = btrfs_buffer_leaf(path.nodes[0]);
	item = btrfs_item_ptr(l, path.slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(item);
	btrfs_set_extent_refs(item, refs + 1);
	mark_buffer_dirty(path.nodes[0]);

	btrfs_release_path(root->fs_info->extent_root, &path);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	return 0;
}

static int lookup_block_ref(struct btrfs_trans_handle *trans, struct btrfs_root
			    *root, u64 blocknr, u32 *refs)
{
	struct btrfs_path path;
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *l;
	struct btrfs_extent_item *item;
	btrfs_init_path(&path);
	key.objectid = blocknr;
	key.offset = 1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, &path,
				0, 0);
	if (ret != 0)
		BUG();
	l = btrfs_buffer_leaf(path.nodes[0]);
	item = btrfs_item_ptr(l, path.slots[0], struct btrfs_extent_item);
	*refs = btrfs_extent_refs(item);
	btrfs_release_path(root->fs_info->extent_root, &path);
	return 0;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct buffer_head *buf)
{
	u64 blocknr;
	struct btrfs_node *buf_node;
	int i;

	if (!root->ref_cows)
		return 0;
	buf_node = btrfs_buffer_node(buf);
	if (btrfs_is_leaf(buf_node))
		return 0;

	for (i = 0; i < btrfs_header_nritems(&buf_node->header); i++) {
		blocknr = btrfs_node_blockptr(buf_node, i);
		inc_block_ref(trans, root, blocknr);
	}
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans, struct
			       btrfs_root *root)
{
	struct buffer_head *gang[8];
	u64 first = 0;
	int ret;
	int i;

	while(1) {
		ret = radix_tree_gang_lookup_tag(&root->fs_info->pinned_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang),
					     CTREE_EXTENT_PINNED);
		if (!ret)
			break;
		if (!first)
			first = gang[0]->b_blocknr;
		for (i = 0; i < ret; i++) {
			radix_tree_delete(&root->fs_info->pinned_radix,
					  gang[i]->b_blocknr);
			brelse(gang[i]);
		}
	}
	if (root->fs_info->last_insert.objectid > first)
		root->fs_info->last_insert.objectid = first;
	root->fs_info->last_insert.offset = 0;
	return 0;
}

static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root)
{
	struct btrfs_key ins;
	struct btrfs_extent_item extent_item;
	int i;
	int ret;
	u64 super_blocks_used;
	struct btrfs_fs_info *info = extent_root->fs_info;

	btrfs_set_extent_refs(&extent_item, 1);
	btrfs_set_extent_owner(&extent_item,
		btrfs_header_parentid(btrfs_buffer_header(extent_root->node)));
	ins.offset = 1;
	ins.flags = 0;
	btrfs_set_key_type(&ins, BTRFS_EXTENT_ITEM_KEY);

	for (i = 0; i < extent_root->fs_info->current_insert.flags; i++) {
		ins.objectid = extent_root->fs_info->current_insert.objectid +
				i;
		super_blocks_used = btrfs_super_blocks_used(info->disk_super);
		btrfs_set_super_blocks_used(info->disk_super,
					    super_blocks_used + 1);
		ret = btrfs_insert_item(trans, extent_root, &ins, &extent_item,
					sizeof(extent_item));
		BUG_ON(ret);
	}
	extent_root->fs_info->current_insert.offset = 0;
	return 0;
}

static int pin_down_block(struct btrfs_root *root, u64 blocknr, int tag)
{
	int err;
	struct buffer_head *bh = sb_getblk(root->fs_info->sb, blocknr);
	BUG_ON(!bh);
	err = radix_tree_insert(&root->fs_info->pinned_radix,
				blocknr, bh);
	BUG_ON(err);
	if (err)
		return err;
	radix_tree_tag_set(&root->fs_info->pinned_radix, blocknr,
			   tag);
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, u64 blocknr, u64 num_blocks)
{
	struct btrfs_path path;
	struct btrfs_key key;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	int ret;
	struct btrfs_extent_item *ei;
	struct btrfs_key ins;
	u32 refs;

	key.objectid = blocknr;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_blocks;

	find_free_extent(trans, root, 0, 0, (u64)-1, &ins);
	btrfs_init_path(&path);
	ret = btrfs_search_slot(trans, extent_root, &key, &path, -1, 1);
	if (ret) {
		printk("failed to find %Lu\n", key.objectid);
		btrfs_print_tree(extent_root, extent_root->node);
		printk("failed to find %Lu\n", key.objectid);
		BUG();
	}
	ei = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			    struct btrfs_extent_item);
	BUG_ON(ei->refs == 0);
	refs = btrfs_extent_refs(ei) - 1;
	btrfs_set_extent_refs(ei, refs);
	if (refs == 0) {
		u64 super_blocks_used;
		super_blocks_used = btrfs_super_blocks_used(info->disk_super);
		btrfs_set_super_blocks_used(info->disk_super,
					    super_blocks_used - num_blocks);
		ret = btrfs_del_item(trans, extent_root, &path);
		if (extent_root->fs_info->last_insert.objectid >
		    blocknr)
			extent_root->fs_info->last_insert.objectid = blocknr;
		if (ret)
			BUG();
	}
	mark_buffer_dirty(path.nodes[0]);
	btrfs_release_path(extent_root, &path);
	finish_current_insert(trans, extent_root);
	return ret;
}

/*
 * find all the blocks marked as pending in the radix tree and remove
 * them from the extent map
 */
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root)
{
	int ret;
	int wret;
	int err = 0;
	struct buffer_head *gang[4];
	int i;
	struct radix_tree_root *radix = &extent_root->fs_info->pinned_radix;

	while(1) {
		ret = radix_tree_gang_lookup_tag(
					&extent_root->fs_info->pinned_radix,
					(void **)gang, 0,
					ARRAY_SIZE(gang),
					CTREE_EXTENT_PENDING_DEL);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			radix_tree_tag_set(radix, gang[i]->b_blocknr,
					   CTREE_EXTENT_PINNED);
			radix_tree_tag_clear(radix, gang[i]->b_blocknr,
					     CTREE_EXTENT_PENDING_DEL);
			wret = __free_extent(trans, extent_root,
					     gang[i]->b_blocknr, 1);
			if (wret)
				err = wret;
		}
	}
	return err;
}

/*
 * remove an extent from the root, returns 0 on success
 */
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, u64 blocknr, u64 num_blocks, int pin)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct buffer_head *t;
	int pending_ret;
	int ret;

	if (root == extent_root) {
		t = find_tree_block(root, blocknr);
		pin_down_block(root, blocknr, CTREE_EXTENT_PENDING_DEL);
		return 0;
	}
	if (pin) {
		ret = pin_down_block(root, blocknr, CTREE_EXTENT_PINNED);
		BUG_ON(ret);
	}
	ret = __free_extent(trans, root, blocknr, num_blocks);
	pending_ret = del_pending_extents(trans, root->fs_info->extent_root);
	return ret ? ret : pending_ret;
}

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == block start
 * ins->flags = BTRFS_EXTENT_ITEM_KEY
 * ins->offset == number of blocks
 * Any available blocks before search_start are skipped.
 */
static int find_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			    *orig_root, u64 num_blocks, u64 search_start, u64
			    search_end, struct btrfs_key *ins)
{
	struct btrfs_path path;
	struct btrfs_key key;
	int ret;
	u64 hole_size = 0;
	int slot = 0;
	u64 last_block = 0;
	u64 test_block;
	int start_found;
	struct btrfs_leaf *l;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	int total_needed = num_blocks;
	int level;

	level = btrfs_header_level(btrfs_buffer_header(root->node));
	total_needed += (level + 1) * 3;
	if (root->fs_info->last_insert.objectid > search_start)
		search_start = root->fs_info->last_insert.objectid;

	ins->flags = 0;
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

check_failed:
	btrfs_init_path(&path);
	ins->objectid = search_start;
	ins->offset = 0;
	start_found = 0;
	ret = btrfs_search_slot(trans, root, ins, &path, 0, 0);
	if (ret < 0)
		goto error;

	if (path.slots[0] > 0)
		path.slots[0]--;

	while (1) {
		l = btrfs_buffer_leaf(path.nodes[0]);
		slot = path.slots[0];
		if (slot >= btrfs_header_nritems(&l->header)) {
			ret = btrfs_next_leaf(root, &path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			if (!start_found) {
				ins->objectid = search_start;
				ins->offset = (u64)-1;
				start_found = 1;
				goto check_pending;
			}
			ins->objectid = last_block > search_start ?
					last_block : search_start;
			ins->offset = (u64)-1;
			goto check_pending;
		}
		btrfs_disk_key_to_cpu(&key, &l->items[slot].key);
		if (key.objectid >= search_start) {
			if (start_found) {
				if (last_block < search_start)
					last_block = search_start;
				hole_size = key.objectid - last_block;
				if (hole_size > total_needed) {
					ins->objectid = last_block;
					ins->offset = hole_size;
					goto check_pending;
				}
			}
		}
		start_found = 1;
		last_block = key.objectid + key.offset;
		path.slots[0]++;
	}
	// FIXME -ENOSPC
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	btrfs_release_path(root, &path);
	BUG_ON(ins->objectid < search_start);
	for (test_block = ins->objectid;
	     test_block < ins->objectid + total_needed; test_block++) {
		if (radix_tree_lookup(&root->fs_info->pinned_radix,
				      test_block)) {
			search_start = test_block + 1;
			goto check_failed;
		}
	}
	BUG_ON(root->fs_info->current_insert.offset);
	root->fs_info->current_insert.offset = total_needed - num_blocks;
	root->fs_info->current_insert.objectid = ins->objectid + num_blocks;
	root->fs_info->current_insert.flags = 0;
	root->fs_info->last_insert.objectid = ins->objectid;
	ins->offset = num_blocks;
	return 0;
error:
	btrfs_release_path(root, &path);
	return ret;
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
static int alloc_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			*root, u64 num_blocks, u64 search_start, u64
			search_end, u64 owner, struct btrfs_key *ins)
{
	int ret;
	int pending_ret;
	u64 super_blocks_used;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct btrfs_extent_item extent_item;

	btrfs_set_extent_refs(&extent_item, 1);
	btrfs_set_extent_owner(&extent_item, owner);

	if (root == extent_root) {
		BUG_ON(extent_root->fs_info->current_insert.offset == 0);
		BUG_ON(num_blocks != 1);
		BUG_ON(extent_root->fs_info->current_insert.flags ==
		       extent_root->fs_info->current_insert.offset);
		ins->offset = 1;
		ins->objectid = extent_root->fs_info->current_insert.objectid +
				extent_root->fs_info->current_insert.flags++;
		return 0;
	}
	ret = find_free_extent(trans, root, num_blocks, search_start,
			       search_end, ins);
	if (ret)
		return ret;

	super_blocks_used = btrfs_super_blocks_used(info->disk_super);
	btrfs_set_super_blocks_used(info->disk_super, super_blocks_used +
				    num_blocks);
	ret = btrfs_insert_item(trans, extent_root, ins, &extent_item,
				sizeof(extent_item));

	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);
	if (ret)
		return ret;
	if (pending_ret)
		return pending_ret;
	return 0;
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct buffer_head *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root)
{
	struct btrfs_key ins;
	int ret;
	struct buffer_head *buf;

	ret = alloc_extent(trans, root, 1, 0, (unsigned long)-1,
		btrfs_header_parentid(btrfs_buffer_header(root->node)), &ins);
	if (ret) {
		BUG();
		return NULL;
	}
	buf = find_tree_block(root, ins.objectid);
	dirty_tree_block(trans, root, buf);
	return buf;
}

/*
 * helper function for drop_snapshot, this walks down the tree dropping ref
 * counts as it goes.
 */
static int walk_down_tree(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int *level)
{
	struct buffer_head *next;
	struct buffer_head *cur;
	u64 blocknr;
	int ret;
	u32 refs;

	ret = lookup_block_ref(trans, root, path->nodes[*level]->b_blocknr,
			       &refs);
	BUG_ON(ret);
	if (refs > 1)
		goto out;
	/*
	 * walk down to the last node level and free all the leaves
	 */
	while(*level > 0) {
		cur = path->nodes[*level];
		if (path->slots[*level] >=
		    btrfs_header_nritems(btrfs_buffer_header(cur)))
			break;
		blocknr = btrfs_node_blockptr(btrfs_buffer_node(cur),
					      path->slots[*level]);
		ret = lookup_block_ref(trans, root, blocknr, &refs);
		if (refs != 1 || *level == 1) {
			path->slots[*level]++;
			ret = btrfs_free_extent(trans, root, blocknr, 1, 1);
			BUG_ON(ret);
			continue;
		}
		BUG_ON(ret);
		next = read_tree_block(root, blocknr);
		if (path->nodes[*level-1])
			btrfs_block_release(root, path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(btrfs_buffer_header(next));
		path->slots[*level] = 0;
	}
out:
	ret = btrfs_free_extent(trans, root, path->nodes[*level]->b_blocknr,
				1, 1);
	btrfs_block_release(root, path->nodes[*level]);
	path->nodes[*level] = NULL;
	*level += 1;
	BUG_ON(ret);
	return 0;
}

/*
 * helper for dropping snapshots.  This walks back up the tree in the path
 * to find the first node higher up where we haven't yet gone through
 * all the slots
 */
static int walk_up_tree(struct btrfs_trans_handle *trans, struct btrfs_root
			*root, struct btrfs_path *path, int *level)
{
	int i;
	int slot;
	int ret;
	for(i = *level; i < BTRFS_MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < btrfs_header_nritems(
		    btrfs_buffer_header(path->nodes[i])) - 1) {
			path->slots[i]++;
			*level = i;
			return 0;
		} else {
			ret = btrfs_free_extent(trans, root,
						path->nodes[*level]->b_blocknr,
						1, 1);
			btrfs_block_release(root, path->nodes[*level]);
			path->nodes[*level] = NULL;
			*level = i + 1;
			BUG_ON(ret);
		}
	}
	return 1;
}

/*
 * drop the reference count on the tree rooted at 'snap'.  This traverses
 * the tree freeing any blocks that have a ref count of zero after being
 * decremented.
 */
int btrfs_drop_snapshot(struct btrfs_trans_handle *trans, struct btrfs_root
			*root, struct buffer_head *snap)
{
	int ret = 0;
	int wret;
	int level;
	struct btrfs_path path;
	int i;
	int orig_level;

	btrfs_init_path(&path);

	level = btrfs_header_level(btrfs_buffer_header(snap));
	orig_level = level;
	path.nodes[level] = snap;
	path.slots[level] = 0;
	while(1) {
		wret = walk_down_tree(trans, root, &path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = walk_up_tree(trans, root, &path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;
	}
	for (i = 0; i <= orig_level; i++) {
		if (path.nodes[i]) {
			btrfs_block_release(root, path.nodes[i]);
		}
	}
	return ret;
}
