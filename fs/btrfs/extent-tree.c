#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

/*
 * pending extents are blocks that we're trying to allocate in the extent
 * map while trying to grow the map because of other allocations.  To avoid
 * recursing, they are tagged in the radix tree and cleaned up after
 * other allocations are done.  The pending tag is also used in the same
 * manner for deletes.
 */
#define CTREE_EXTENT_PENDING 0

/*
 * find all the blocks marked as pending in the radix tree and remove
 * them from the extent map
 */
static int del_pending_extents(struct ctree_root *extent_root)
{
	int ret;
	struct key key;
	struct tree_buffer *gang[4];
	int i;
	struct ctree_path path;

	while(1) {
		ret = radix_tree_gang_lookup_tag(&extent_root->cache_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 CTREE_EXTENT_PENDING);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			key.objectid = gang[i]->blocknr;
			key.flags = 0;
			key.offset = 1;
			init_path(&path);
			ret = search_slot(extent_root, &key, &path, -1);
			if (ret) {
				print_tree(extent_root, extent_root->node);
				printf("unable to find %Lu\n", key.objectid);
				BUG();
				// FIXME undo it and return sane
				return ret;
			}
			ret = del_item(extent_root, &path);
			if (ret) {
				BUG();
				return ret;
			}
			release_path(extent_root, &path);
			radix_tree_tag_clear(&extent_root->cache_radix,
						gang[i]->blocknr,
						CTREE_EXTENT_PENDING);
			tree_block_release(extent_root, gang[i]);
		}
	}
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
int free_extent(struct ctree_root *root, u64 blocknr, u64 num_blocks)
{
	struct ctree_path path;
	struct key key;
	struct ctree_root *extent_root = root->extent_root;
	struct tree_buffer *t;
	int pending_ret;
	int ret;
	key.objectid = blocknr;
	key.flags = 0;
	key.offset = num_blocks;
	if (root == extent_root) {
		t = read_tree_block(root, key.objectid);
		radix_tree_tag_set(&root->cache_radix, key.objectid,
				   CTREE_EXTENT_PENDING);
		return 0;
	}
	init_path(&path);
	ret = search_slot(extent_root, &key, &path, -1);
	if (ret) {
		print_tree(extent_root, extent_root->node);
		printf("failed to find %Lu\n", key.objectid);
		BUG();
	}
	ret = del_item(extent_root, &path);
	if (ret)
		BUG();
	release_path(extent_root, &path);
	pending_ret = del_pending_extents(root->extent_root);
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
	int start_found;
	struct leaf *l;
	struct ctree_root * root = orig_root->extent_root;

check_failed:
	init_path(&path);
	ins->objectid = search_start;
	ins->offset = 0;
	ins->flags = 0;
	start_found = 0;
	ret = search_slot(root, ins, &path, 0);
	if (ret < 0)
		goto error;

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
				ins->offset = num_blocks;
				start_found = 1;
				goto check_pending;
			}
			ins->objectid = last_block > search_start ?
					last_block : search_start;
			ins->offset = num_blocks;
			goto check_pending;
		}
		key = &l->items[slot].key;
		if (key->objectid >= search_start) {
			if (start_found) {
				hole_size = key->objectid - last_block;
				if (hole_size > num_blocks) {
					ins->objectid = last_block;
					ins->offset = num_blocks;
					goto check_pending;
				}
			} else
				start_found = 1;
			last_block = key->objectid + key->offset;
		}
		path.slots[0]++;
	}
	// FIXME -ENOSPC
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	release_path(root, &path);
	BUG_ON(ins->objectid < search_start);
	if (orig_root->extent_root == orig_root) {
		BUG_ON(num_blocks != 1);
		if ((root->current_insert.objectid <= ins->objectid &&
		    root->current_insert.objectid +
		    root->current_insert.offset > ins->objectid) ||
		   (root->current_insert.objectid > ins->objectid &&
		    root->current_insert.objectid <= ins->objectid +
		    ins->offset) ||
		   radix_tree_tag_get(&root->cache_radix, ins->objectid,
				      CTREE_EXTENT_PENDING)) {
			search_start = ins->objectid + 1;
			goto check_failed;
		}
	}
	if (ins->offset != 1)
		BUG();
	return 0;
error:
	release_path(root, &path);
	return ret;
}

/*
 * insert all of the pending extents reserved during the original
 * allocation.  (CTREE_EXTENT_PENDING).  Returns zero if it all worked out
 */
static int insert_pending_extents(struct ctree_root *extent_root)
{
	int ret;
	struct key key;
	struct extent_item item;
	struct tree_buffer *gang[4];
	int i;

	// FIXME -ENOSPC
	item.refs = 1;
	item.owner = extent_root->node->node.header.parentid;
	while(1) {
		ret = radix_tree_gang_lookup_tag(&extent_root->cache_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 CTREE_EXTENT_PENDING);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			key.objectid = gang[i]->blocknr;
			key.flags = 0;
			key.offset = 1;
			ret = insert_item(extent_root, &key, &item,
					  sizeof(item));
			if (ret) {
				BUG();
				// FIXME undo it and return sane
				return ret;
			}
			radix_tree_tag_clear(&extent_root->cache_radix,
					     gang[i]->blocknr,
					     CTREE_EXTENT_PENDING);
			tree_block_release(extent_root, gang[i]);
		}
	}
	return 0;
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
int alloc_extent(struct ctree_root *root, u64 num_blocks, u64 search_start,
			 u64 search_end, u64 owner, struct key *ins,
			 struct tree_buffer **buf)
{
	int ret;
	int pending_ret;
	struct extent_item extent_item;
	extent_item.refs = 1;
	extent_item.owner = owner;

	ret = find_free_extent(root, num_blocks, search_start, search_end, ins);
	if (ret)
		return ret;
	if (root != root->extent_root) {
		memcpy(&root->extent_root->current_insert, ins, sizeof(*ins));
		ret = insert_item(root->extent_root, ins, &extent_item,
				  sizeof(extent_item));
		memset(&root->extent_root->current_insert, 0,
		       sizeof(struct key));
		pending_ret = insert_pending_extents(root->extent_root);
		if (ret)
			return ret;
		if (pending_ret)
			return pending_ret;
		*buf = find_tree_block(root, ins->objectid);
		return 0;
	}
	/* we're allocating an extent for the extent tree, don't recurse */
	BUG_ON(ins->offset != 1);
	*buf = find_tree_block(root, ins->objectid);
	BUG_ON(!*buf);
	radix_tree_tag_set(&root->cache_radix, ins->objectid,
			   CTREE_EXTENT_PENDING);
	(*buf)->count++;
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
	struct tree_buffer *buf = NULL;

	ret = alloc_extent(root, 1, 0, (unsigned long)-1,
			   root->node->node.header.parentid,
			   &ins, &buf);

	if (ret) {
		BUG();
		return NULL;
	}
	if (root != root->extent_root)
		BUG_ON(radix_tree_tag_get(&root->extent_root->cache_radix,
					  buf->blocknr, CTREE_EXTENT_PENDING));
	return buf;
}
