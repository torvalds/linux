#ifndef __BTRFS_INODE_MAP
#define __BTRFS_INODE_MAP

void btrfs_init_free_ino_ctl(struct btrfs_root *root);
void btrfs_unpin_free_ino(struct btrfs_root *root);
void btrfs_return_ino(struct btrfs_root *root, u64 objectid);
int btrfs_find_free_ino(struct btrfs_root *root, u64 *objectid);

int btrfs_find_free_objectid(struct btrfs_root *root, u64 *objectid);

#endif
