/* fs/ internal definitions
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct super_block;

/*
 * block_dev.c
 */
#ifdef CONFIG_BLOCK
extern struct super_block *blockdev_superblock;
extern void __init bdev_cache_init(void);

static inline int sb_is_blkdev_sb(struct super_block *sb)
{
	return sb == blockdev_superblock;
}

#else
static inline void bdev_cache_init(void)
{
}

static inline int sb_is_blkdev_sb(struct super_block *sb)
{
	return 0;
}
#endif

/*
 * char_dev.c
 */
extern void __init chrdev_init(void);

/*
 * namespace.c
 */
extern int copy_mount_options(const void __user *, unsigned long *);

extern void free_vfsmnt(struct vfsmount *);
extern struct vfsmount *alloc_vfsmnt(const char *);
extern struct vfsmount *__lookup_mnt(struct vfsmount *, struct dentry *, int);
extern void mnt_set_mountpoint(struct vfsmount *, struct dentry *,
				struct vfsmount *);
extern void release_mounts(struct list_head *);
extern void umount_tree(struct vfsmount *, int, struct list_head *);
extern struct vfsmount *copy_tree(struct vfsmount *, struct dentry *, int);

extern void __init mnt_init(void);
