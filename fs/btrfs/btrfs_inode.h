#ifndef __BTRFS_I__
#define __BTRFS_I__

struct btrfs_inode {
	u32 magic;
	struct inode vfs_inode;
	u32 magic2;
};
static inline struct btrfs_inode *BTRFS_I(struct inode *inode)
{
	return container_of(inode, struct btrfs_inode, vfs_inode);
}

#endif
