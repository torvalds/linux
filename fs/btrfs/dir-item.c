#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "hash.h"
#include "transaction.h"

int insert_with_overflow(struct btrfs_trans_handle *trans, struct btrfs_root
			    *root, struct btrfs_path *path, struct btrfs_key
			    *cpu_key, u32 data_size)
{
	int overflow;
	int ret;

	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	overflow = btrfs_key_overflow(cpu_key);

	while(ret == -EEXIST && overflow < BTRFS_KEY_OVERFLOW_MAX) {
		overflow++;
		btrfs_set_key_overflow(cpu_key, overflow);
		btrfs_release_path(root, path);
		ret = btrfs_insert_empty_item(trans, root, path, cpu_key,
					      data_size);
	}
	return ret;
}

int btrfs_insert_dir_item(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, const char *name, int name_len, u64 dir,
			  struct btrfs_key *location, u8 type)
{
	int ret = 0;
	struct btrfs_path *path;
	struct btrfs_dir_item *dir_item;
	char *name_ptr;
	struct btrfs_key key;
	u32 data_size;

	key.objectid = dir;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	path = btrfs_alloc_path();
	btrfs_init_path(path);
	data_size = sizeof(*dir_item) + name_len;
	ret = insert_with_overflow(trans, root, path, &key, data_size);
	if (ret)
		goto out;

	dir_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0],
				  struct btrfs_dir_item);
	btrfs_cpu_key_to_disk(&dir_item->location, location);
	btrfs_set_dir_type(dir_item, type);
	btrfs_set_dir_flags(dir_item, 0);
	btrfs_set_dir_name_len(dir_item, name_len);
	name_ptr = (char *)(dir_item + 1);

	btrfs_memcpy(root, path->nodes[0]->b_data, name_ptr, name, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	/* FIXME, use some real flag for selecting the extra index */
	if (root == root->fs_info->tree_root)
		goto out;

	btrfs_release_path(root, path);

	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	key.offset = location->objectid;
	ret = insert_with_overflow(trans, root, path, &key, data_size);
	// FIXME clear the dirindex bit
	if (ret)
		goto out;

	dir_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0],
				  struct btrfs_dir_item);
	btrfs_cpu_key_to_disk(&dir_item->location, location);
	btrfs_set_dir_type(dir_item, type);
	btrfs_set_dir_flags(dir_item, 0);
	btrfs_set_dir_name_len(dir_item, name_len);
	name_ptr = (char *)(dir_item + 1);
	btrfs_memcpy(root, path->nodes[0]->b_data, name_ptr, name, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int btrfs_lookup_dir_item(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, u64 dir,
			  const char *name, int name_len, int mod)
{
	int ret;
	struct btrfs_key key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;
	u32 overflow;

	key.objectid = dir;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	btrfs_set_key_overflow(&key, BTRFS_KEY_OVERFLOW_MAX - 1);
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	while(1) {
		ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
		if (ret < 0)
			return ret;
		if (ret > 0) {
			if (path->slots[0] == 0)
				return 1;
			path->slots[0]--;
		}
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		found_key = &leaf->items[path->slots[0]].key;

		if (btrfs_disk_key_objectid(found_key) != dir ||
		    btrfs_disk_key_type(found_key) != BTRFS_DIR_ITEM_KEY ||
		    btrfs_disk_key_offset(found_key) != key.offset)
			return 1;

		if (btrfs_match_dir_item_name(root, path, name, name_len))
			return 0;

		overflow = btrfs_disk_key_overflow(found_key);
		if (overflow == 0)
			return 1;
		btrfs_set_key_overflow(&key, overflow - 1);
		btrfs_release_path(root, path);
	}
	return 1;
}

int btrfs_lookup_dir_index_item(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_path *path, u64 dir,
				u64 objectid, int mod)
{
	int ret;
	struct btrfs_key key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;

	key.objectid = dir;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	btrfs_set_key_overflow(&key, BTRFS_KEY_OVERFLOW_MAX - 1);
	key.offset = objectid;
	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ret;
	if (ret > 0) {
		if (path->slots[0] == 0)
			return 1;
		path->slots[0]--;
	}
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	found_key = &leaf->items[path->slots[0]].key;

	if (btrfs_disk_key_objectid(found_key) != dir ||
	    btrfs_disk_key_type(found_key) != BTRFS_DIR_INDEX_KEY)
		return 1;
	if (btrfs_disk_key_offset(found_key) == objectid)
		return 0;
	return 1;
}

int btrfs_match_dir_item_name(struct btrfs_root *root,
			      struct btrfs_path *path,
			      const char *name, int name_len)
{
	struct btrfs_dir_item *dir_item;
	char *name_ptr;

	dir_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0],
				  struct btrfs_dir_item);
	if (btrfs_dir_name_len(dir_item) != name_len)
		return 0;
	name_ptr = (char *)(dir_item + 1);
	if (memcmp(name_ptr, name, name_len))
		return 0;
	return 1;
}
