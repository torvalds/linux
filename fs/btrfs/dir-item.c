#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "hash.h"
#include "transaction.h"

static struct btrfs_dir_item *insert_with_overflow(struct btrfs_trans_handle
						   *trans,
						   struct btrfs_root *root,
						   struct btrfs_path *path,
						   struct btrfs_key *cpu_key,
						   u32 data_size)
{
	int ret;
	char *ptr;
	struct btrfs_item *item;
	struct btrfs_leaf *leaf;

	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (ret == -EEXIST) {
		ret = btrfs_extend_item(trans, root, path, data_size);
		WARN_ON(ret > 0);
		if (ret)
			return ERR_PTR(ret);
	}
	WARN_ON(ret > 0);
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	item = leaf->items + path->slots[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0], char);
	BUG_ON(data_size > btrfs_item_size(item));
	ptr += btrfs_item_size(item) - data_size;
	return (struct btrfs_dir_item *)ptr;
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
	dir_item = insert_with_overflow(trans, root, path, &key, data_size);
	if (IS_ERR(dir_item)) {
		ret = PTR_ERR(dir_item);
		goto out;
	}

	btrfs_cpu_key_to_disk(&dir_item->location, location);
	btrfs_set_dir_type(dir_item, type);
	btrfs_set_dir_flags(dir_item, 0);
	btrfs_set_dir_name_len(dir_item, name_len);
	name_ptr = (char *)(dir_item + 1);

	btrfs_memcpy(root, path->nodes[0]->b_data, name_ptr, name, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);

	/* FIXME, use some real flag for selecting the extra index */
	if (root == root->fs_info->tree_root) {
		ret = 0;
		goto out;
	}

	btrfs_release_path(root, path);

	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	key.offset = location->objectid;
	dir_item = insert_with_overflow(trans, root, path, &key, data_size);
	if (IS_ERR(dir_item)) {
		ret = PTR_ERR(dir_item);
		goto out;
	}
	btrfs_cpu_key_to_disk(&dir_item->location, location);
	btrfs_set_dir_type(dir_item, type);
	btrfs_set_dir_flags(dir_item, 0);
	btrfs_set_dir_name_len(dir_item, name_len);
	name_ptr = (char *)(dir_item + 1);
	btrfs_memcpy(root, path->nodes[0]->b_data, name_ptr, name, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	return ret;
}

struct btrfs_dir_item *btrfs_lookup_dir_item(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path, u64 dir,
					     const char *name, int name_len,
					     int mod)
{
	int ret;
	struct btrfs_key key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;

	key.objectid = dir;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0) {
		if (path->slots[0] == 0)
			return NULL;
		path->slots[0]--;
	}
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	found_key = &leaf->items[path->slots[0]].key;

	if (btrfs_disk_key_objectid(found_key) != dir ||
	    btrfs_disk_key_type(found_key) != BTRFS_DIR_ITEM_KEY ||
	    btrfs_disk_key_offset(found_key) != key.offset)
		return NULL;

	return btrfs_match_dir_item_name(root, path, name, name_len);
}

struct btrfs_dir_item *
btrfs_lookup_dir_index_item(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, u64 dir,
			    u64 objectid, const char *name, int name_len,
			    int mod)
{
	int ret;
	struct btrfs_key key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;

	key.objectid = dir;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	key.offset = objectid;

	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0)
		return ERR_PTR(-ENOENT);
	return btrfs_match_dir_item_name(root, path, name, name_len);
}

struct btrfs_dir_item *btrfs_match_dir_item_name(struct btrfs_root *root,
			      struct btrfs_path *path,
			      const char *name, int name_len)
{
	struct btrfs_dir_item *dir_item;
	char *name_ptr;
	u32 total_len;
	u32 cur = 0;
	u32 this_len;
	struct btrfs_leaf *leaf;

	leaf = btrfs_buffer_leaf(path->nodes[0]);
	dir_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dir_item);
	total_len = btrfs_item_size(leaf->items + path->slots[0]);
	while(cur < total_len) {
		this_len = sizeof(*dir_item) + btrfs_dir_name_len(dir_item);
		name_ptr = (char *)(dir_item + 1);

		if (btrfs_dir_name_len(dir_item) == name_len &&
		    memcmp(name_ptr, name, name_len) == 0)
			return dir_item;

		cur += this_len;
		dir_item = (struct btrfs_dir_item *)((char *)dir_item +
						     this_len);
	}
	return NULL;
}

int btrfs_delete_one_dir_name(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct btrfs_path *path,
			      struct btrfs_dir_item *di)
{

	struct btrfs_leaf *leaf;
	u32 sub_item_len;
	u32 item_len;
	int ret;

	leaf = btrfs_buffer_leaf(path->nodes[0]);
	sub_item_len = sizeof(*di) + btrfs_dir_name_len(di);
	item_len = btrfs_item_size(leaf->items + path->slots[0]);
	if (sub_item_len == btrfs_item_size(leaf->items + path->slots[0])) {
		ret = btrfs_del_item(trans, root, path);
		BUG_ON(ret);
	} else {
		char *ptr = (char *)di;
		char *start = btrfs_item_ptr(leaf, path->slots[0], char);
		btrfs_memmove(root, leaf, ptr, ptr + sub_item_len,
			item_len - (ptr + sub_item_len - start));
		ret = btrfs_truncate_item(trans, root, path,
					  item_len - sub_item_len);
		BUG_ON(ret);
	}
	return 0;
}

