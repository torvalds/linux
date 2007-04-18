#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

#define MAX_CSUM_ITEMS(r) ((((BTRFS_LEAF_DATA_SIZE(r) - \
				 sizeof(struct btrfs_item) * 2) / \
				sizeof(struct btrfs_csum_item)) - 1))
int btrfs_insert_file_extent(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       u64 objectid, u64 pos,
			       u64 offset, u64 num_blocks)
{
	int ret = 0;
	struct btrfs_file_extent_item *item;
	struct btrfs_key file_key;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	file_key.objectid = objectid;
	file_key.offset = pos;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);

	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(*item));
	BUG_ON(ret);
	item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			      struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_blocknr(item, offset);
	btrfs_set_file_extent_disk_num_blocks(item, num_blocks);
	btrfs_set_file_extent_offset(item, 0);
	btrfs_set_file_extent_num_blocks(item, num_blocks);
	btrfs_set_file_extent_generation(item, trans->transid);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return 0;
}

struct btrfs_csum_item *btrfs_lookup_csum(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 objectid, u64 offset,
					  int cow)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_csum_item *item;
	struct btrfs_leaf *leaf;
	u64 csum_offset = 0;
	int csums_in_item;

	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
	ret = btrfs_search_slot(trans, root, &file_key, path, 0, cow);
	if (ret < 0)
		goto fail;
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	if (ret > 0) {
		ret = 1;
		if (path->slots[0] == 0)
			goto fail;
		path->slots[0]--;
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[path->slots[0]].key);
		if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
		    found_key.objectid != objectid) {
			goto fail;
		}
		csum_offset = (offset - found_key.offset) >>
				root->fs_info->sb->s_blocksize_bits;
		csums_in_item = btrfs_item_size(leaf->items + path->slots[0]);
		csums_in_item /= sizeof(struct btrfs_csum_item);

		if (csum_offset >= csums_in_item) {
			ret = -EFBIG;
			goto fail;
		}
	}
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item += csum_offset;
	return item;
fail:
	if (ret > 0)
		ret = -ENOENT;
	return ERR_PTR(ret);
}


int btrfs_lookup_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid,
			     u64 offset, int mod)
{
	int ret;
	struct btrfs_key file_key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;

	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);
	ret = btrfs_search_slot(trans, root, &file_key, path, ins_len, cow);
	return ret;
}

int btrfs_csum_file_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 objectid, u64 offset,
			  char *data, size_t len)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_csum_item *item;
	struct btrfs_leaf *leaf;
	u64 csum_offset;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);

	item = btrfs_lookup_csum(trans, root, path, objectid, offset, 1);
	if (!IS_ERR(item))
		goto found;
	ret = PTR_ERR(item);
	if (ret == -EFBIG) {
		u32 item_size;
		/* we found one, but it isn't big enough yet */
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		item_size = btrfs_item_size(leaf->items + path->slots[0]);
		if ((item_size / sizeof(struct btrfs_csum_item)) >=
		    MAX_CSUM_ITEMS(root)) {
			/* already at max size, make a new one */
			goto insert;
		}
	} else {
		/* we didn't find a csum item, insert one */
		goto insert;
	}

	/*
	 * at this point, we know the tree has an item, but it isn't big
	 * enough yet to put our csum in.  Grow it
	 */
	btrfs_release_path(root, path);
	ret = btrfs_search_slot(trans, root, &file_key, path,
				sizeof(struct btrfs_csum_item), 1);
	if (ret < 0)
		goto fail;
	if (ret == 0) {
		BUG();
	}
	if (path->slots[0] == 0) {
		goto insert;
	}
	path->slots[0]--;
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	btrfs_disk_key_to_cpu(&found_key, &leaf->items[path->slots[0]].key);
	csum_offset = (offset - found_key.offset) >>
			root->fs_info->sb->s_blocksize_bits;
	if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
	    found_key.objectid != objectid ||
	    csum_offset >= MAX_CSUM_ITEMS(root)) {
		WARN_ON(1);
		goto insert;
	}
	if (csum_offset >= btrfs_item_size(leaf->items + path->slots[0]) /
	    sizeof(struct btrfs_csum_item)) {
		u32 diff = (csum_offset + 1) * sizeof(struct btrfs_csum_item);
		diff = diff - btrfs_item_size(leaf->items + path->slots[0]);
		WARN_ON(diff != sizeof(struct btrfs_csum_item));
		ret = btrfs_extend_item(trans, root, path, diff);
		BUG_ON(ret);
		goto csum;
	}

insert:
	btrfs_release_path(root, path);
	csum_offset = 0;
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(struct btrfs_csum_item));
	if (ret != 0) {
		printk("at insert for %Lu %u %Lu ret is %d\n", file_key.objectid, file_key.flags, file_key.offset, ret);
		WARN_ON(1);
		goto fail;
	}
csum:
	item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			      struct btrfs_csum_item);
	ret = 0;
	item += csum_offset;
found:
	btrfs_check_bounds(item->csum, BTRFS_CSUM_SIZE, path->nodes[0]->b_data, root->fs_info->sb->s_blocksize);
	ret = btrfs_csum_data(root, data, len, item->csum);
	btrfs_mark_buffer_dirty(path->nodes[0]);
fail:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int btrfs_csum_verify_file_block(struct btrfs_root *root,
				 u64 objectid, u64 offset,
				 char *data, size_t len)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_path *path;
	struct btrfs_csum_item *item;
	char result[BTRFS_CSUM_SIZE];

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
	mutex_lock(&root->fs_info->fs_mutex);

	item = btrfs_lookup_csum(NULL, root, path, objectid, offset, 0);
	if (IS_ERR(item)) {
		ret = PTR_ERR(item);
		/* a csum that isn't present is a preallocated region. */
		if (ret == -ENOENT || ret == -EFBIG)
			ret = 1;
		goto fail;
	}

	ret = btrfs_csum_data(root, data, len, result);
	WARN_ON(ret);
	if (memcmp(result, item->csum, BTRFS_CSUM_SIZE))
		ret = 1;
fail:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

