#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "hash.h"

int btrfs_insert_dir_item(struct btrfs_root *root, char *name, int name_len,
			  u64 dir, u64 objectid, u8 type)
{
	int ret = 0;
	struct btrfs_path path;
	struct btrfs_dir_item *dir_item;
	char *name_ptr;
	struct btrfs_key key;
	u32 data_size;

	key.objectid = dir;
	key.flags = 0;
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	btrfs_init_path(&path);
	data_size = sizeof(*dir_item) + name_len;
	ret = btrfs_insert_empty_item(root, &path, &key, data_size);
	if (ret)
		goto out;

	dir_item = btrfs_item_ptr(&path.nodes[0]->leaf, path.slots[0],
				  struct btrfs_dir_item);
	btrfs_set_dir_objectid(dir_item, objectid);
	btrfs_set_dir_type(dir_item, type);
	btrfs_set_dir_flags(dir_item, 0);
	name_ptr = (char *)(dir_item + 1);
	memcpy(name_ptr, name, name_len);
out:
	btrfs_release_path(root, &path);
	return ret;
}

int btrfs_del_dir_item(struct btrfs_root *root, u64 dir, char *name,
		       int name_len)
{
	int ret = 0;
	struct btrfs_path path;
	struct btrfs_key key;

	key.objectid = dir;
	key.flags = 0;
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	btrfs_init_path(&path);
	ret = btrfs_search_slot(root, &key, &path, 0, 1);
	if (ret)
		goto out;
	ret = btrfs_del_item(root, &path);
out:
	btrfs_release_path(root, &path);
	return ret;
}

int btrfs_lookup_dir_item(struct btrfs_root *root, u64 dir, char *name,
			  int name_len, u64 *objectid)
{
	int ret = 0;
	struct btrfs_path path;
	struct btrfs_dir_item *dir_item;
	char *name_ptr;
	struct btrfs_key key;
	u32 item_len;
	struct btrfs_item *item;

	key.objectid = dir;
	key.flags = 0;
	ret = btrfs_name_hash(name, name_len, &key.offset);
	BUG_ON(ret);
	btrfs_init_path(&path);
	ret = btrfs_search_slot(root, &key, &path, 0, 0);
	if (ret)
		goto out;

	dir_item = btrfs_item_ptr(&path.nodes[0]->leaf, path.slots[0],
				  struct btrfs_dir_item);

	item = path.nodes[0]->leaf.items + path.slots[0];
	item_len = btrfs_item_size(item);
	if (item_len != name_len + sizeof(struct btrfs_dir_item)) {
		BUG();
		ret = 1;
		goto out;
	}
	name_ptr = (char *)(dir_item + 1);
	if (memcmp(name_ptr, name, name_len)) {
		BUG();
		ret = 1;
		goto out;
	}
	*objectid = btrfs_dir_objectid(dir_item);
out:
	btrfs_release_path(root, &path);
	return ret;
}
