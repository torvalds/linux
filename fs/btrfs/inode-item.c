#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

int btrfs_insert_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, u64 objectid, struct btrfs_inode_item
		       *inode_item)
{
	struct btrfs_path path;
	struct btrfs_key key;
	int ret;
	key.objectid = objectid;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	btrfs_init_path(&path);
	ret = btrfs_insert_item(trans, root, &key, inode_item,
				sizeof(*inode_item));
	btrfs_release_path(root, &path);
	return ret;
}

int btrfs_lookup_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, struct btrfs_path *path, u64 objectid, int mod)
{
	struct btrfs_key key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;

	key.objectid = objectid;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;
	return btrfs_search_slot(trans, root, &key, path, ins_len, cow);
}
