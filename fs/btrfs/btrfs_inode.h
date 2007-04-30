#ifndef __BTRFS_I__
#define __BTRFS_I__

struct btrfs_inode {
	struct btrfs_root *root;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_key location;
	struct inode vfs_inode;
};
static inline struct btrfs_inode *BTRFS_I(struct inode *inode)
{
	return container_of(inode, struct btrfs_inode, vfs_inode);
}

#endif
