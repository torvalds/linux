#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"

static int find_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			    *orig_root, u64 num_blocks, u64 search_start, u64
			    search_end, struct btrfs_key *ins, int data);
static int finish_current_insert(struct btrfs_trans_handle *trans, struct
				 btrfs_root *extent_root);
static int del_pending_extents(struct btrfs_trans_handle *trans, struct
			       btrfs_root *extent_root);

struct btrfs_block_group_cache *btrfs_find_block_group(struct btrfs_root *root,
						 struct btrfs_block_group_cache
						 *hint, int data)
{
	struct btrfs_block_group_cache *cache[8];
	struct btrfs_block_group_cache *found_group = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	u64 used;
	u64 last = 0;
	u64 hint_last;
	int i;
	int ret;
	int full_search = 0;
	if (!data && hint) {
		used = btrfs_block_group_used(&hint->item);
		if (used < (hint->key.offset * 2) / 3) {
			return hint;
		}
		radix_tree_tag_clear(&info->block_group_radix,
				     hint->key.objectid + hint->key.offset - 1,
				     BTRFS_BLOCK_GROUP_AVAIL);
		last = hint->key.objectid + hint->key.offset;
		hint_last = last;
	} else {
		hint_last = 0;
		last = 0;
	}
	while(1) {
		ret = radix_tree_gang_lookup_tag(&info->block_group_radix,
						 (void **)cache,
						 last, ARRAY_SIZE(cache),
						 BTRFS_BLOCK_GROUP_AVAIL);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			last = cache[i]->key.objectid +
				cache[i]->key.offset;
			if (!full_search && !data &&
			   (cache[i]->key.objectid & cache[i]->key.offset))
				continue;
			if (!full_search && data &&
			   (cache[i]->key.objectid & cache[i]->key.offset) == 0)
				continue;
			used = btrfs_block_group_used(&cache[i]->item);
			if (used < (cache[i]->key.offset * 2) / 3) {
				info->block_group_cache = cache[i];
				found_group = cache[i];
				goto found;
			}
			radix_tree_tag_clear(&info->block_group_radix,
					   cache[i]->key.objectid +
					   cache[i]->key.offset - 1,
					   BTRFS_BLOCK_GROUP_AVAIL);
		}
	}
	last = hint_last;
again:
	while(1) {
		ret = radix_tree_gang_lookup(&info->block_group_radix,
						 (void **)cache,
						 last, ARRAY_SIZE(cache));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			last = cache[i]->key.objectid +
				cache[i]->key.offset;
			if (!full_search && !data &&
			   (cache[i]->key.objectid & cache[i]->key.offset))
				continue;
			if (!full_search && data &&
			   (cache[i]->key.objectid & cache[i]->key.offset) == 0)
				continue;
			used = btrfs_block_group_used(&cache[i]->item);
			if (used < cache[i]->key.offset) {
				info->block_group_cache = cache[i];
				found_group = cache[i];
				goto found;
			}
			radix_tree_tag_clear(&info->block_group_radix,
					   cache[i]->key.objectid +
					   cache[i]->key.offset - 1,
					   BTRFS_BLOCK_GROUP_AVAIL);
		}
	}
	info->block_group_cache = NULL;
	if (!full_search) {
		last = 0;
		full_search = 1;
		goto again;
	}
found:
	if (!found_group) {
		ret = radix_tree_gang_lookup(&info->block_group_radix,
					     (void **)&found_group, 0, 1);
		BUG_ON(ret != 1);
	}
	return found_group;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 blocknr, u64 num_blocks)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *l;
	struct btrfs_extent_item *item;
	struct btrfs_key ins;
	u32 refs;

	find_free_extent(trans, root->fs_info->extent_root, 0, 0, (u64)-1,
			 &ins, 0);
	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	key.objectid = blocknr;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	key.offset = num_blocks;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 1);
	if (ret != 0) {
printk("can't find block %Lu %Lu\n", blocknr, num_blocks);
		BUG();
	}
	BUG_ON(ret != 0);
	l = btrfs_buffer_leaf(path->nodes[0]);
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(item);
	btrfs_set_extent_refs(item, refs + 1);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_release_path(root->fs_info->extent_root, path);
	btrfs_free_path(path);
	finish_current_insert(trans, root->fs_info->extent_root);
	del_pending_extents(trans, root->fs_info->extent_root);
	return 0;
}

static int lookup_extent_ref(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 blocknr,
			     u64 num_blocks, u32 *refs)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *l;
	struct btrfs_extent_item *item;

	path = btrfs_alloc_path();
	btrfs_init_path(path);
	key.objectid = blocknr;
	key.offset = num_blocks;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret != 0)
		BUG();
	l = btrfs_buffer_leaf(path->nodes[0]);
	item = btrfs_item_ptr(l, path->slots[0], struct btrfs_extent_item);
	*refs = btrfs_extent_refs(item);
	btrfs_release_path(root->fs_info->extent_root, path);
	btrfs_free_path(path);
	return 0;
}

int btrfs_inc_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root)
{
	return btrfs_inc_extent_ref(trans, root, bh_blocknr(root->node), 1);
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct buffer_head *buf)
{
	u64 blocknr;
	struct btrfs_node *buf_node;
	struct btrfs_leaf *buf_leaf;
	struct btrfs_disk_key *key;
	struct btrfs_file_extent_item *fi;
	int i;
	int leaf;
	int ret;

	if (!root->ref_cows)
		return 0;
	buf_node = btrfs_buffer_node(buf);
	leaf = btrfs_is_leaf(buf_node);
	buf_leaf = btrfs_buffer_leaf(buf);
	for (i = 0; i < btrfs_header_nritems(&buf_node->header); i++) {
		if (leaf) {
			key = &buf_leaf->items[i].key;
			if (btrfs_disk_key_type(key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf_leaf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			ret = btrfs_inc_extent_ref(trans, root,
				    btrfs_file_extent_disk_blocknr(fi),
				    btrfs_file_extent_disk_num_blocks(fi));
			BUG_ON(ret);
		} else {
			blocknr = btrfs_node_blockptr(buf_node, i);
			ret = btrfs_inc_extent_ref(trans, root, blocknr, 1);
			BUG_ON(ret);
		}
	}
	return 0;
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	int pending_ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct btrfs_block_group_item *bi;
	struct btrfs_key ins;

	find_free_extent(trans, extent_root, 0, 0, (u64)-1, &ins, 0);
	ret = btrfs_search_slot(trans, extent_root, &cache->key, path, 0, 1);
	BUG_ON(ret);
	bi = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			    struct btrfs_block_group_item);
	memcpy(bi, &cache->item, sizeof(*bi));
	mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(extent_root, path);

	finish_current_insert(trans, extent_root);
	pending_ret = del_pending_extents(trans, extent_root);
	if (ret)
		return ret;
	if (pending_ret)
		return pending_ret;
	return 0;

}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root)
{
	struct btrfs_block_group_cache *cache[8];
	int ret;
	int err = 0;
	int werr = 0;
	struct radix_tree_root *radix = &root->fs_info->block_group_radix;
	int i;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = radix_tree_gang_lookup_tag(radix, (void **)cache,
						 0, ARRAY_SIZE(cache),
						 BTRFS_BLOCK_GROUP_DIRTY);
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			radix_tree_tag_clear(radix, cache[i]->key.objectid +
					     cache[i]->key.offset - 1,
					     BTRFS_BLOCK_GROUP_DIRTY);
			err = write_one_cache_group(trans, root,
						    path, cache[i]);
			if (err)
				werr = err;
			cache[i]->last_alloc = cache[i]->first_free;
		}
	}
	btrfs_free_path(path);
	return werr;
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 blocknr, u64 num, int alloc)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num;
	u64 old_val;
	u64 block_in_group;
	int ret;
	while(total) {
		ret = radix_tree_gang_lookup(&info->block_group_radix,
					     (void **)&cache, blocknr, 1);
		if (!ret) {
			printk(KERN_CRIT "blocknr %Lu lookup failed\n",
			       blocknr);
			return -1;
		}
		block_in_group = blocknr - cache->key.objectid;
		WARN_ON(block_in_group > cache->key.offset);
		radix_tree_tag_set(&info->block_group_radix,
				   cache->key.objectid + cache->key.offset - 1,
				   BTRFS_BLOCK_GROUP_DIRTY);

		old_val = btrfs_block_group_used(&cache->item);
		num = min(total, cache->key.offset - block_in_group);
		total -= num;
		blocknr += num;
		if (alloc) {
			old_val += num;
			if (blocknr > cache->last_alloc)
				cache->last_alloc = blocknr;
		} else {
			old_val -= num;
			if (blocknr < cache->first_free)
				cache->first_free = blocknr;
		}
		btrfs_set_block_group_used(&cache->item, old_val);
	}
	return 0;
}

static int try_remove_page(struct address_space *mapping, unsigned long index)
{
	int ret;
	ret = invalidate_mapping_pages(mapping, index, index);
	return ret;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans, struct
			       btrfs_root *root)
{
	unsigned long gang[8];
	struct inode *btree_inode = root->fs_info->btree_inode;
	u64 first = 0;
	int ret;
	int i;
	struct radix_tree_root *pinned_radix = &root->fs_info->pinned_radix;

	while(1) {
		ret = find_first_radix_bit(pinned_radix, gang,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;
		if (!first)
			first = gang[0];
		for (i = 0; i < ret; i++) {
			clear_radix_bit(pinned_radix, gang[i]);
			try_remove_page(btree_inode->i_mapping,
					gang[i] << (PAGE_CACHE_SHIFT -
						    btree_inode->i_blkbits));
		}
	}
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
	ins.offset = 1;
	ins.flags = 0;
	btrfs_set_key_type(&ins, BTRFS_EXTENT_ITEM_KEY);
	btrfs_set_extent_owner(&extent_item, extent_root->root_key.objectid);

	for (i = 0; i < extent_root->fs_info->extent_tree_insert_nr; i++) {
		ins.objectid = extent_root->fs_info->extent_tree_insert[i];
		super_blocks_used = btrfs_super_blocks_used(info->disk_super);
		btrfs_set_super_blocks_used(info->disk_super,
					    super_blocks_used + 1);
		ret = btrfs_insert_item(trans, extent_root, &ins, &extent_item,
					sizeof(extent_item));
		BUG_ON(ret);
	}
	extent_root->fs_info->extent_tree_insert_nr = 0;
	extent_root->fs_info->extent_tree_prealloc_nr = 0;
	return 0;
}

static int pin_down_block(struct btrfs_root *root, u64 blocknr, int pending)
{
	int err;
	struct btrfs_header *header;
	struct buffer_head *bh;

	if (!pending) {
		bh = btrfs_find_tree_block(root, blocknr);
		if (bh) {
			if (buffer_uptodate(bh)) {
				u64 transid =
				    root->fs_info->running_transaction->transid;
				header = btrfs_buffer_header(bh);
				if (btrfs_header_generation(header) ==
				    transid) {
					btrfs_block_release(root, bh);
					return 0;
				}
			}
			btrfs_block_release(root, bh);
		}
		err = set_radix_bit(&root->fs_info->pinned_radix, blocknr);
	} else {
		err = set_radix_bit(&root->fs_info->pending_del_radix, blocknr);
	}
	BUG_ON(err);
	return 0;
}

/*
 * remove an extent from the root, returns 0 on success
 */
static int __free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, u64 blocknr, u64 num_blocks, int pin)
{
	struct btrfs_path *path;
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

	find_free_extent(trans, root, 0, 0, (u64)-1, &ins, 0);
	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);

	ret = btrfs_search_slot(trans, extent_root, &key, path, -1, 1);
	if (ret) {
		printk("failed to find %Lu\n", key.objectid);
		btrfs_print_tree(extent_root, extent_root->node);
		printk("failed to find %Lu\n", key.objectid);
		BUG();
	}
	ei = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			    struct btrfs_extent_item);
	BUG_ON(ei->refs == 0);
	refs = btrfs_extent_refs(ei) - 1;
	btrfs_set_extent_refs(ei, refs);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	if (refs == 0) {
		u64 super_blocks_used;

		if (pin) {
			ret = pin_down_block(root, blocknr, 0);
			BUG_ON(ret);
		}

		super_blocks_used = btrfs_super_blocks_used(info->disk_super);
		btrfs_set_super_blocks_used(info->disk_super,
					    super_blocks_used - num_blocks);
		ret = btrfs_del_item(trans, extent_root, path);
		if (ret)
			BUG();
		ret = update_block_group(trans, root, blocknr, num_blocks, 0);
		BUG_ON(ret);
	}
	btrfs_release_path(extent_root, path);
	btrfs_free_path(path);
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
	unsigned long gang[4];
	int i;
	struct radix_tree_root *pending_radix;
	struct radix_tree_root *pinned_radix;

	pending_radix = &extent_root->fs_info->pending_del_radix;
	pinned_radix = &extent_root->fs_info->pinned_radix;

	while(1) {
		ret = find_first_radix_bit(pending_radix, gang,
					   ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			wret = set_radix_bit(pinned_radix, gang[i]);
			BUG_ON(wret);
			wret = clear_radix_bit(pending_radix, gang[i]);
			BUG_ON(wret);
			wret = __free_extent(trans, extent_root,
					     gang[i], 1, 0);
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
	int pending_ret;
	int ret;

	if (root == extent_root) {
		pin_down_block(root, blocknr, 1);
		return 0;
	}
	ret = __free_extent(trans, root, blocknr, num_blocks, pin);
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
			    search_end, struct btrfs_key *ins, int data)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	u64 hole_size = 0;
	int slot = 0;
	u64 last_block = 0;
	u64 test_block;
	int start_found;
	struct btrfs_leaf *l;
	struct btrfs_root * root = orig_root->fs_info->extent_root;
	struct btrfs_fs_info *info = root->fs_info;
	int total_needed = num_blocks;
	int total_found = 0;
	int fill_prealloc = 0;
	int level;
	struct btrfs_block_group_cache *block_group;

	path = btrfs_alloc_path();
	ins->flags = 0;
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);

	level = btrfs_header_level(btrfs_buffer_header(root->node));
	if (num_blocks == 0) {
		fill_prealloc = 1;
		num_blocks = 1;
		total_needed = (min(level + 1, BTRFS_MAX_LEVEL) + 2) * 3;
	}
	block_group = btrfs_find_block_group(root, trans->block_group, data);
	if (block_group->last_alloc > search_start)
		search_start = block_group->last_alloc;
check_failed:
	btrfs_init_path(path);
	ins->objectid = search_start;
	ins->offset = 0;
	start_found = 0;
	ret = btrfs_search_slot(trans, root, ins, path, 0, 0);
	if (ret < 0)
		goto error;

	if (path->slots[0] > 0)
		path->slots[0]--;

	while (1) {
		l = btrfs_buffer_leaf(path->nodes[0]);
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(&l->header)) {
			if (fill_prealloc) {
				info->extent_tree_prealloc_nr = 0;
				total_found = 0;
			}
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			if (!start_found) {
				ins->objectid = search_start;
				ins->offset = (u64)-1 - search_start;
				start_found = 1;
				goto check_pending;
			}
			ins->objectid = last_block > search_start ?
					last_block : search_start;
			ins->offset = (u64)-1 - ins->objectid;
			goto check_pending;
		}
		btrfs_disk_key_to_cpu(&key, &l->items[slot].key);
		if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY)
			goto next;
		if (key.objectid >= search_start) {
			if (start_found) {
				if (last_block < search_start)
					last_block = search_start;
				hole_size = key.objectid - last_block;
				if (hole_size >= num_blocks) {
					ins->objectid = last_block;
					ins->offset = hole_size;
					goto check_pending;
				}
			}
		}
		start_found = 1;
		last_block = key.objectid + key.offset;
next:
		path->slots[0]++;
	}
	// FIXME -ENOSPC
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	btrfs_release_path(root, path);
	BUG_ON(ins->objectid < search_start);
	if (ins->objectid >= btrfs_super_total_blocks(info->disk_super)) {
		if (search_start == 0)
			return -ENOSPC;
		search_start = 0;
		goto check_failed;
	}
	for (test_block = ins->objectid;
	     test_block < ins->objectid + num_blocks; test_block++) {
		if (test_radix_bit(&info->pinned_radix, test_block)) {
			search_start = test_block + 1;
			goto check_failed;
		}
	}
	if (!fill_prealloc && info->extent_tree_insert_nr) {
		u64 last =
		  info->extent_tree_insert[info->extent_tree_insert_nr - 1];
		if (ins->objectid + num_blocks >
		    info->extent_tree_insert[0] &&
		    ins->objectid <= last) {
			search_start = last + 1;
			WARN_ON(1);
			goto check_failed;
		}
	}
	if (!fill_prealloc && info->extent_tree_prealloc_nr) {
		u64 first =
		  info->extent_tree_prealloc[info->extent_tree_prealloc_nr - 1];
		if (ins->objectid + num_blocks > first &&
		    ins->objectid <= info->extent_tree_prealloc[0]) {
			search_start = info->extent_tree_prealloc[0] + 1;
			WARN_ON(1);
			goto check_failed;
		}
	}
	if (fill_prealloc) {
		int nr;
		test_block = ins->objectid;
		while(test_block < ins->objectid + ins->offset &&
		      total_found < total_needed) {
			nr = total_needed - total_found - 1;
			BUG_ON(nr < 0);
			info->extent_tree_prealloc[nr] = test_block;
			total_found++;
			test_block++;
		}
		if (total_found < total_needed) {
			search_start = test_block;
			goto check_failed;
		}
		info->extent_tree_prealloc_nr = total_found;
	}
	ret = radix_tree_gang_lookup(&info->block_group_radix,
				     (void **)&block_group,
				     ins->objectid, 1);
	if (ret) {
		block_group->last_alloc = ins->objectid;
		if (!data)
			trans->block_group = block_group;
	}
	ins->offset = num_blocks;
	btrfs_free_path(path);
	return 0;
error:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}
/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns 0 if everything worked, non-zero otherwise.
 */
int btrfs_alloc_extent(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, u64 owner,
		       u64 num_blocks, u64 search_start,
		       u64 search_end, struct btrfs_key *ins, int data)
{
	int ret;
	int pending_ret;
	u64 super_blocks_used;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct btrfs_extent_item extent_item;
	struct btrfs_key prealloc_key;

	btrfs_set_extent_refs(&extent_item, 1);
	btrfs_set_extent_owner(&extent_item, owner);

	if (root == extent_root) {
		int nr;
		BUG_ON(info->extent_tree_prealloc_nr == 0);
		BUG_ON(num_blocks != 1);
		ins->offset = 1;
		info->extent_tree_prealloc_nr--;
		nr = info->extent_tree_prealloc_nr;
		ins->objectid = info->extent_tree_prealloc[nr];
		info->extent_tree_insert[info->extent_tree_insert_nr++] =
			ins->objectid;
		ret = update_block_group(trans, root,
					 ins->objectid, ins->offset, 1);
		BUG_ON(ret);
		return 0;
	}
	/* do the real allocation */
	ret = find_free_extent(trans, root, num_blocks, search_start,
			       search_end, ins, data);
	if (ret)
		return ret;

	/* then do prealloc for the extent tree */
	ret = find_free_extent(trans, root, 0, ins->objectid + ins->offset,
			       search_end, &prealloc_key, 0);
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
	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1);
	return 0;
}

/*
 * helper function to allocate a block for a given tree
 * returns the tree buffer or NULL.
 */
struct buffer_head *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root, u64 hint)
{
	struct btrfs_key ins;
	int ret;
	struct buffer_head *buf;

	ret = btrfs_alloc_extent(trans, root, root->root_key.objectid,
				 1, 0, (unsigned long)-1, &ins, 0);
	if (ret) {
		BUG();
		return NULL;
	}
	BUG_ON(ret);
	buf = btrfs_find_create_tree_block(root, ins.objectid);
	set_buffer_uptodate(buf);
	set_buffer_checked(buf);
	set_radix_bit(&trans->transaction->dirty_pages, buf->b_page->index);
	return buf;
}

static int drop_leaf_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root, struct buffer_head *cur)
{
	struct btrfs_disk_key *key;
	struct btrfs_leaf *leaf;
	struct btrfs_file_extent_item *fi;
	int i;
	int nritems;
	int ret;

	BUG_ON(!btrfs_is_leaf(btrfs_buffer_node(cur)));
	leaf = btrfs_buffer_leaf(cur);
	nritems = btrfs_header_nritems(&leaf->header);
	for (i = 0; i < nritems; i++) {
		key = &leaf->items[i].key;
		if (btrfs_disk_key_type(key) != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(fi) == BTRFS_FILE_EXTENT_INLINE)
			continue;
		/*
		 * FIXME make sure to insert a trans record that
		 * repeats the snapshot del on crash
		 */
		ret = btrfs_free_extent(trans, root,
					btrfs_file_extent_disk_blocknr(fi),
					btrfs_file_extent_disk_num_blocks(fi),
					0);
		BUG_ON(ret);
	}
	return 0;
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

	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = lookup_extent_ref(trans, root, bh_blocknr(path->nodes[*level]),
			       1, &refs);
	BUG_ON(ret);
	if (refs > 1)
		goto out;
	/*
	 * walk down to the last node level and free all the leaves
	 */
	while(*level >= 0) {
		WARN_ON(*level < 0);
		WARN_ON(*level >= BTRFS_MAX_LEVEL);
		cur = path->nodes[*level];
		if (btrfs_header_level(btrfs_buffer_header(cur)) != *level)
			WARN_ON(1);
		if (path->slots[*level] >=
		    btrfs_header_nritems(btrfs_buffer_header(cur)))
			break;
		if (*level == 0) {
			ret = drop_leaf_ref(trans, root, cur);
			BUG_ON(ret);
			break;
		}
		blocknr = btrfs_node_blockptr(btrfs_buffer_node(cur),
					      path->slots[*level]);
		ret = lookup_extent_ref(trans, root, blocknr, 1, &refs);
		BUG_ON(ret);
		if (refs != 1) {
			path->slots[*level]++;
			ret = btrfs_free_extent(trans, root, blocknr, 1, 1);
			BUG_ON(ret);
			continue;
		}
		next = read_tree_block(root, blocknr);
		WARN_ON(*level <= 0);
		if (path->nodes[*level-1])
			btrfs_block_release(root, path->nodes[*level-1]);
		path->nodes[*level-1] = next;
		*level = btrfs_header_level(btrfs_buffer_header(next));
		path->slots[*level] = 0;
	}
out:
	WARN_ON(*level < 0);
	WARN_ON(*level >= BTRFS_MAX_LEVEL);
	ret = btrfs_free_extent(trans, root,
				bh_blocknr(path->nodes[*level]), 1, 1);
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
						bh_blocknr(path->nodes[*level]),
						1, 1);
			BUG_ON(ret);
			btrfs_block_release(root, path->nodes[*level]);
			path->nodes[*level] = NULL;
			*level = i + 1;
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
	struct btrfs_path *path;
	int i;
	int orig_level;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);

	level = btrfs_header_level(btrfs_buffer_header(snap));
	orig_level = level;
	path->nodes[level] = snap;
	path->slots[level] = 0;
	while(1) {
		wret = walk_down_tree(trans, root, path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;

		wret = walk_up_tree(trans, root, path, &level);
		if (wret > 0)
			break;
		if (wret < 0)
			ret = wret;
		btrfs_btree_balance_dirty(root);
	}
	for (i = 0; i <= orig_level; i++) {
		if (path->nodes[i]) {
			btrfs_block_release(root, path->nodes[i]);
		}
	}
	btrfs_free_path(path);
	return ret;
}

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	int ret;
	struct btrfs_block_group_cache *cache[8];
	int i;

	while(1) {
		ret = radix_tree_gang_lookup(&info->block_group_radix,
					     (void **)cache, 0,
					     ARRAY_SIZE(cache));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			radix_tree_delete(&info->block_group_radix,
					  cache[i]->key.objectid +
					  cache[i]->key.offset - 1);
			kfree(cache[i]);
		}
	}
	return 0;
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	int err = 0;
	struct btrfs_block_group_item *bi;
	struct btrfs_block_group_cache *cache;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_leaf *leaf;
	u64 group_size_blocks = BTRFS_BLOCK_GROUP_SIZE / root->blocksize;
	u64 used;

	root = root->fs_info->extent_root;
	key.objectid = 0;
	key.offset = group_size_blocks;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while(1) {
		ret = btrfs_search_slot(NULL, root->fs_info->extent_root,
					&key, path, 0, 0);
		if (ret != 0) {
			err = ret;
			break;
		}
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[path->slots[0]].key);
		cache = kmalloc(sizeof(*cache), GFP_NOFS);
		if (!cache) {
			err = -1;
			break;
		}
		bi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_block_group_item);
		memcpy(&cache->item, bi, sizeof(*bi));
		memcpy(&cache->key, &found_key, sizeof(found_key));
		cache->last_alloc = cache->key.objectid;
		cache->first_free = cache->key.objectid;
		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(root, path);
		ret = radix_tree_insert(&root->fs_info->block_group_radix,
					found_key.objectid +
					found_key.offset - 1,
					(void *)cache);
		BUG_ON(ret);
		used = btrfs_block_group_used(bi);
		if (used < (key.offset * 2) / 3) {
			radix_tree_tag_set(&root->fs_info->block_group_radix,
					   found_key.objectid +
					   found_key.offset - 1,
					   BTRFS_BLOCK_GROUP_AVAIL);
		}
		if (key.objectid >=
		    btrfs_super_total_blocks(root->fs_info->disk_super))
			break;
	}

	btrfs_free_path(path);
	return 0;
}
