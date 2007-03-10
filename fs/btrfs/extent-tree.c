#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

static int find_free_extent(struct ctree_root *orig_root, u64 num_blocks,
			    u64 search_start, u64 search_end, struct key *ins);
static int finish_current_insert(struct ctree_root *extent_root);
static int run_pending(struct ctree_root *extent_root);

/*
 * pending extents are blocks that we're trying to allocate in the extent
 * map while trying to grow the map because of other allocations.  To avoid
 * recursing, they are tagged in the radix tree and cleaned up after
 * other allocations are done.  The pending tag is also used in the same
 * manner for deletes.
 */
#define CTREE_EXTENT_PENDING_DEL 0

static int inc_block_ref(struct ctree_root *root, u64 blocknr)
{
	struct ctree_path path;
	int ret;
	struct key key;
	struct leaf *l;
	struct extent_item *item;
	struct key ins;

	find_free_extent(root->extent_root, 0, 0, (u64)-1, &ins);
	init_path(&path);
	key.objectid = blocknr;
	key.flags = 0;
	key.offset = 1;
	ret = search_slot(root->extent_root, &key, &path, 0, 1);
	if (ret != 0)
		BUG();
	BUG_ON(ret != 0);
	l = &path.nodes[0]->leaf;
	item = (struct extent_item *)(l->data +
				      l->items[path.slots[0]].offset);
	item->refs++;

	BUG_ON(list_empty(&path.nodes[0]->dirty));
	release_path(root->extent_root, &path);
	finish_current_insert(root->extent_root);
	run_pending(root->extent_root);
	return 0;
}

static int lookup_block_ref(struct ctree_root *root, u64 blocknr, u32 *refs)
{
	struct ctree_path path;
	int ret;
	struct key key;
	struct leaf *l;
	struct extent_item *item;
	init_path(&path);
	key.objectid = blocknr;
	key.flags = 0;
	key.offset = 1;
	ret = search_slot(root->extent_root, &key, &path, 0, 0);
	if (ret != 0)
		BUG();
	l = &path.nodes[0]->leaf;
	item = (struct extent_item *)(l->data +
				      l->items[path.slots[0]].offset);
	*refs = item->refs;
	release_path(root->extent_root, &path);
	return 0;
}

int btrfs_inc_ref(struct ctree_root *root, struct tree_buffer *buf)
{
	u64 blocknr;
	int i;

	if (root == root->extent_root)
		return 0;
	if (is_leaf(buf->node.header.flags))
		return 0;

	for (i = 0; i < buf->node.header.nritems; i++) {
		blocknr = buf->node.blockptrs[i];
		inc_block_ref(root, blocknr);
	}
	return 0;
}

int btrfs_finish_extent_commit(struct ctree_root *root)
{
	struct ctree_root *extent_root = root->extent_root;
	unsigned long gang[8];
	int ret;
	int i;

	while(1) {
		ret = radix_tree_gang_lookup(&extent_root->pinned_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			radix_tree_delete(&extent_root->pinned_radix, gang[i]);
		}
	}
	extent_root->last_insert.objectid = 0;
	extent_root->last_insert.offset = 0;
	return 0;
}

static int finish_current_insert(struct ctree_root *extent_root)
{
	struct key ins;
	struct extent_item extent_item;
	int i;
	int ret;

	extent_item.refs = 1;
	extent_item.owner = extent_root->node->node.header.parentid;
	ins.offset = 1;
	ins.flags = 0;

	for (i = 0; i < extent_root->current_insert.flags; i++) {
		ins.objectid = extent_root->current_insert.objectid + i;
		ret = insert_item(extent_root, &ins, &extent_item,
				  sizeof(extent_item));
		BUG_ON(ret);
	}
	extent_root->current_insert.offset = 0;
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
int __free_extent(struct ctree_root *root, u64 blocknr, u64 num_blocks)
{
	struct ctree_path path;
	struct key key;
	struct ctree_root *extent_root = root->extent_root;
	int ret;
	struct item *item;
	struct extent_item *ei;
	struct key ins;

	key.objectid = blocknr;
	key.flags = 0;
	key.offset = num_blocks;

	find_free_extent(root, 0, 0, (u64)-1, &ins);
	init_path(&path);
	ret = search_slot(extent_root, &key, &path, -1, 1);
	if (ret) {
		printf("failed to find %Lu\n", key.objectid);
		print_tree(extent_root, extent_root->node);
		printf("failed to find %Lu\n", key.objectid);
		BUG();
	}
	item = path.nodes[0]->leaf.items + path.slots[0];
	ei = (struct extent_item *)(path.nodes[0]->leaf.data + item->offset);
	BUG_ON(ei->refs == 0);
	ei->refs--;
	if (ei->refs == 0) {
		if (root == extent_root) {
			int err;
			radix_tree_preload(GFP_KERNEL);
			err = radix_tree_insert(&extent_root->pinned_radix,
					  blocknr, (void *)blocknr);
			BUG_ON(err);
			radix_tree_preload_end();
		}
		ret = del_item(extent_root, &path);
		if (root != extent_root &&
		    extent_root->last_insert.objectid < blocknr)
			extent_root->last_insert.objectid = blocknr;
		if (ret)
			BUG();
	}
	release_path(extent_root, &path);
	finish_current_insert(extent_root);
	return ret;
}

/*
 * find all the blocks marked as pending in the radix tree and remove
 * them from the extent map
 */
static int del_pending_extents(struct ctree_root *extent_root)
{
	int ret;
	struct tree_buffer *gang[4];
	int i;

	while(1) {
		ret = radix_tree_gang_lookup_tag(&extent_root->cache_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 CTREE_EXTENT_PENDING_DEL);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			ret = __free_extent(extent_root, gang[i]->blocknr, 1);
			radix_tree_tag_clear(&extent_root->cache_radix,
						gang[i]->blocknr,
						CTREE_EXTENT_PENDING_DEL);
			tree_block_release(extent_root, gang[i]);
		}
	}
	return 0;
}

static int run_pending(struct ctree_root *extent_root)
{
	while(radix_tree_tagged(&extent_root->cache_radix,
			        CTREE_EXTENT_PENDING_DEL))
		del_pending_extents(extent_root);
	return 0;
}


/*
 * remove an extent from the root, returns 0 on success
 */
int free_extent(struct ctree_root *root, u64 blocknr, u64 num_blocks)
{
	struct key key;
	struct ctree_root *extent_root = root->extent_root;
	struct tree_buffer *t;
	int pending_ret;
	int ret;

	if (root == extent_root) {
		t = find_tree_block(root, blocknr);
		radix_tree_tag_set(&root->cache_radix, blocknr,
				   CTREE_EXTENT_PENDING_DEL);
		return 0;
	}
	key.objectid = blocknr;
	key.flags = 0;
	key.offset = num_blocks;
	ret = __free_extent(root, blocknr, num_blocks);
	pending_ret = run_pending(root->extent_root);
	return ret ? ret : pending_ret;
}

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == block start
 * ins->flags = 0
 * ins->offset == number of blocks
 * Any available blocks before search_start are skipped.
 */
static int find_free_extent(struct ctree_root *orig_root, u64 num_blocks,
			    u64 search_start, u64 search_end, struct key *ins)
{
	struct ctree_path path;
	struct key *key;
	int ret;
	u64 hole_size = 0;
	int slot = 0;
	u64 last_block;
	u64 test_block;
	int start_found;
	struct leaf *l;
	struct ctree_root * root = orig_root->extent_root;
	int total_needed = num_blocks;

	total_needed += (node_level(root->node->node.header.flags) + 1) * 3;
	if (root->last_insert.objectid > search_start)
		search_start = root->last_insert.objectid;
check_failed:
	init_path(&path);
	ins->objectid = search_start;
	ins->offset = 0;
	ins->flags = 0;
	start_found = 0;
	ret = search_slot(root, ins, &path, 0, 0);
	if (ret < 0)
		goto error;

	if (path.slots[0] > 0)
		path.slots[0]--;

	while (1) {
		l = &path.nodes[0]->leaf;
		slot = path.slots[0];
		if (slot >= l->header.nritems) {
			ret = next_leaf(root, &path);
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
		key = &l->items[slot].key;
		if (key->objectid >= search_start) {
			if (start_found) {
				if (last_block < search_start)
					last_block = search_start;
				hole_size = key->objectid - last_block;
				if (hole_size > total_needed) {
					ins->objectid = last_block;
					ins->offset = hole_size;
					goto check_pending;
				}
			}
		}
		start_found = 1;
		last_block = key->objectid + key->offset;
		path.slots[0]++;
	}
	// FIXME -ENOSPC
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	release_path(root, &path);
	BUG_ON(ins->objectid < search_start);
	for (test_block = ins->objectid;
	     test_block < ins->objectid + total_needed; test_block++) {
		if (radix_tree_lookup(&root->pinned_radix, test_block)) {
			search_start = test_block + 1;
			goto check_failed;
		}
	}
	BUG_ON(root->current_insert.offset);
	root->current_insert.offset = total_needed - num_blocks;
	root->current_insert.objectid = ins->objectid + num_blocks;
	root->current_insert.flags = 0;
	root->last_insert.objectid = ins->objectid;
	ins->offset = num_blocks;
	return 0;
error:
	release_path(root, &path);
	return ret;
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
int alloc_extent(struct ctree_root *root, u64 num_blocks, u64 search_start,
			 u64 search_end, u64 owner, struct key *ins)
{
	int ret;
	int pending_ret;
	struct ctree_root *extent_root = root->extent_root;
	struct extent_item extent_item;

	extent_item.refs = 1;
	extent_item.owner = owner;

	if (root == extent_root) {
		BUG_ON(extent_root->current_insert.offset == 0);
		BUG_ON(num_blocks != 1);
		BUG_ON(extent_root->current_insert.flags ==
		       extent_root->current_insert.offset);
		ins->offset = 1;
		ins->objectid = extent_root->current_insert.objectid +
				extent_root->current_insert.flags++;
		return 0;
	}
	ret = find_free_extent(root, num_blocks, search_start,
			       search_end, ins);
	if (ret)
		return ret;

	ret = insert_item(extent_root, ins, &extent_item,
			  sizeof(extent_item));

	finish_current_insert(extent_root);
	pending_ret = run_pending(extent_root);
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
struct tree_buffer *alloc_free_block(struct ctree_root *root)
{
	struct key ins;
	int ret;
	struct tree_buffer *buf;

	ret = alloc_extent(root, 1, 0, (unsigned long)-1,
			   root->node->node.header.parentid,
			   &ins);
	if (ret) {
		BUG();
		return NULL;
	}
	buf = find_tree_block(root, ins.objectid);
	dirty_tree_block(root, buf);
	return buf;
}

int walk_down_tree(struct ctree_root *root, struct ctree_path *path, int *level)
{
	struct tree_buffer *next;
	struct tree_buffer *cur;
	u64 blocknr;
	int ret;
	u32 refs;

	ret = lookup_block_ref(root, path->nodes[*level]->blocknr, &refs);
	BUG_ON(ret);
	if (refs > 1)
		goto out;
	while(*level > 0) {
		cur = path->nodes[*level];
		if (path->slots[*level] >= cur->node.header.nritems)
			break;
		blocknr = cur->node.blockptrs[path->slots[*level]];
		ret = lookup_block_ref(root, blocknr, &refs);
		if (refs != 1 || *level == 1) {
			path->slots[*level]++;
			ret = free_extent(root, blocknr, 1);
			BUG_ON(ret);
			continue;
		}
		BUG_ON(ret);
		next = read_tree_block(root, blocknr);
		if (path->nodes[*level-1]) {
			tree_block_release(root, path->nodes[*level-1]);
		}
		path->nodes[*level-1] = next;
		*level = node_level(next->node.header.flags);
		path->slots[*level] = 0;
	}
out:
	ret = free_extent(root, path->nodes[*level]->blocknr, 1);
	path->nodes[*level] = NULL;
	*level += 1;
	BUG_ON(ret);
	return 0;
}

int walk_up_tree(struct ctree_root *root, struct ctree_path *path, int *level)
{
	int i;
	int slot;
	int ret;
	for(i = *level; i < MAX_LEVEL - 1 && path->nodes[i]; i++) {
		slot = path->slots[i];
		if (slot < path->nodes[i]->node.header.nritems - 1) {
			path->slots[i]++;
			*level = i;
			return 0;
		} else {
			ret = free_extent(root,
					  path->nodes[*level]->blocknr, 1);
			*level = i + 1;
			BUG_ON(ret);
		}
	}
	return 1;
}

int btrfs_drop_snapshot(struct ctree_root *root, struct tree_buffer *snap)
{
	int ret;
	int level;
	struct ctree_path path;
	int i;
	int orig_level;

	init_path(&path);

	level = node_level(snap->node.header.flags);
	orig_level = level;
	path.nodes[level] = snap;
	path.slots[level] = 0;
	while(1) {
		ret = walk_down_tree(root, &path, &level);
		if (ret > 0)
			break;
		ret = walk_up_tree(root, &path, &level);
		if (ret > 0)
			break;
	}
	for (i = 0; i < orig_level; i++) {
		if (path.nodes[i])
			tree_block_release(root, path.nodes[i]);
	}

	return 0;
}


#if 0
int btrfs_drop_snapshot(struct ctree_root *root, struct tree_buffer *snap)
{
	int ret;
	int level;
	int refs;
	u64 blocknr = snap->blocknr;

	level = node_level(snap->node.header.flags);
	ret = lookup_block_ref(root, snap->blocknr, &refs);
	BUG_ON(ret);
	if (refs == 1 && level != 0) {
		struct node *n = &snap->node;
		struct tree_buffer *b;
		int i;
		for (i = 0; i < n->header.nritems; i++) {
			b = read_tree_block(root, n->blockptrs[i]);
			/* FIXME, don't recurse here */
			ret = btrfs_drop_snapshot(root, b);
			BUG_ON(ret);
			tree_block_release(root, b);
		}
	}
	ret = free_extent(root, blocknr, 1);
	BUG_ON(ret);
	return 0;
}
#endif
