#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

#define MAX_CSUM_ITEMS(r) ((((BTRFS_LEAF_DATA_SIZE(r) - \
				 sizeof(struct btrfs_item)) / \
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
	/*
	ret = btrfs_alloc_extent(trans, root, num_blocks, hint_block,
				 (u64)-1, &ins);
				 */
	BUG_ON(ret);
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

	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
printk("__lookup for %Lu\n", offset);
	ret = btrfs_search_slot(trans, root, &file_key, path, 0, cow);
	if (ret < 0)
		goto fail;
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	if (ret > 0) {
		ret = 1;
		if (path->slots[0] == 0) {
printk("fail1\n");
			goto fail;
		}
		path->slots[0]--;
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[path->slots[0]].key);
		if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
		    found_key.objectid != objectid) {
printk("fail2 type %u %Lu %Lu\n", btrfs_key_type(&found_key), found_key.objectid, objectid);
			goto fail;
		}
		csum_offset = (offset - found_key.offset) >>
				root->fs_info->sb->s_blocksize_bits;
		if (csum_offset >=
		    btrfs_item_size(leaf->items + path->slots[0]) /
		    sizeof(struct btrfs_csum_item)) {
printk("fail3, csum offset %lu size %u\n", csum_offset, btrfs_item_size(leaf->items + path->slots[0]) / sizeof(struct btrfs_csum_item));
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
	struct btrfs_csum_item *csum_item;

	csum_item = btrfs_lookup_csum(trans, root, path, objectid, offset, 0);
	if (IS_ERR(csum_item))
		return PTR_ERR(csum_item);
	file_key.objectid = objectid;
	file_key.offset = btrfs_csum_extent_offset(csum_item);
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);
	btrfs_release_path(root, path);
printk("lookup file extent searches for %Lu\n", file_key.offset);
	ret = btrfs_search_slot(trans, root, &file_key, path, ins_len, cow);
printk("ret is %d\n", ret);
	return ret;
}

int btrfs_csum_file_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 objectid, u64 offset,
			  u64 extent_offset,
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
	btrfs_init_path(path);

	item = btrfs_lookup_csum(trans, root, path, objectid, offset, 0);
	if (!IS_ERR(item))
		goto found;
	btrfs_release_path(root, path);
	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
printk("searching for csum %Lu %Lu\n", objectid, offset);
	ret = btrfs_search_slot(trans, root, &file_key, path,
				sizeof(struct btrfs_csum_item), 1);
printk("ret %d\n", ret);
	if (ret < 0)
		goto fail;
	if (ret == 0) {
		BUG();
	}
	if (path->slots[0] == 0) {
		btrfs_release_path(root, path);
		goto insert;
	}
	path->slots[0]--;
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	btrfs_disk_key_to_cpu(&found_key, &leaf->items[path->slots[0]].key);
printk("found key %Lu %Lu %u\n", found_key.objectid, found_key.offset, found_key.flags);
	csum_offset = (offset - found_key.offset) >>
			root->fs_info->sb->s_blocksize_bits;
printk("csum_offset %Lu\n", csum_offset);
	if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
	    found_key.objectid != objectid ||
	    csum_offset >= MAX_CSUM_ITEMS(root)) {
		btrfs_release_path(root, path);
printk("insert1\n");
		goto insert;
	}
	if (csum_offset >= btrfs_item_size(leaf->items + path->slots[0]) /
	    sizeof(struct btrfs_csum_item)) {
		ret = btrfs_extend_item(trans, root, path,
					sizeof(struct btrfs_csum_item));
		BUG_ON(ret);
printk("item extended\n");
		goto csum;
	}

insert:
	csum_offset = 0;
printk("inserting item %Lu %Lu %u\n", file_key.objectid, file_key.offset, file_key.flags);
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(struct btrfs_csum_item));
	if (ret != 0 && ret != -EEXIST)
		goto fail;
csum:
	item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			      struct btrfs_csum_item);
	ret = 0;
	item += csum_offset;
found:
	ret = btrfs_csum_data(root, data, len, item->csum);
	btrfs_set_csum_extent_offset(item, extent_offset);
	btrfs_mark_buffer_dirty(path->nodes[0]);
fail:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
printk("return ret %d\n", ret);
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

