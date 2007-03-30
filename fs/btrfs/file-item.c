#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

int btrfs_alloc_file_extent(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       u64 objectid, u64 offset,
			       u64 num_blocks, u64 hint_block,
			       u64 *result)
{
	struct btrfs_key ins;
	int ret = 0;
	struct btrfs_file_extent_item *item;
	struct btrfs_key file_key;
	struct btrfs_path path;

	btrfs_init_path(&path);
	ret = btrfs_alloc_extent(trans, root, num_blocks, hint_block,
				 (u64)-1, objectid, &ins);
	BUG_ON(ret);
	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);

	ret = btrfs_insert_empty_item(trans, root, &path, &file_key,
				      sizeof(*item));
	BUG_ON(ret);
	item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			      struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_blocknr(item, ins.objectid);
	btrfs_set_file_extent_disk_num_blocks(item, ins.offset);
	btrfs_set_file_extent_offset(item, 0);
	btrfs_set_file_extent_num_blocks(item, ins.offset);
	btrfs_set_file_extent_generation(item, trans->transid);
	btrfs_mark_buffer_dirty(path.nodes[0]);
	*result = ins.objectid;
	btrfs_release_path(root, &path);
	return 0;
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
	struct btrfs_path path;
	struct btrfs_csum_item *item;

	btrfs_init_path(&path);
	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
	ret = btrfs_insert_empty_item(trans, root, &path, &file_key,
				      BTRFS_CSUM_SIZE);
	if (ret != 0 && ret != -EEXIST)
		goto fail;
	item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			      struct btrfs_csum_item);
	ret = 0;
	ret = btrfs_csum_data(root, data, len, item->csum);
	btrfs_mark_buffer_dirty(path.nodes[0]);
fail:
	btrfs_release_path(root, &path);
	return ret;
}

int btrfs_csum_verify_file_block(struct btrfs_root *root,
				 u64 objectid, u64 offset,
				 char *data, size_t len)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_path path;
	struct btrfs_csum_item *item;
	char result[BTRFS_CSUM_SIZE];

	btrfs_init_path(&path);
	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.flags = 0;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root, &file_key, &path, 0, 0);
	if (ret)
		goto fail;
	item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			      struct btrfs_csum_item);
	ret = 0;
	ret = btrfs_csum_data(root, data, len, result);
	WARN_ON(ret);
	if (memcmp(result, item->csum, BTRFS_CSUM_SIZE))
		ret = 1;
fail:
	btrfs_release_path(root, &path);
	return ret;
}

