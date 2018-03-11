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
struct file_system_type;
struct iomap;
struct iomap_ops;
struct linux_binprm;
struct path;
struct mount;
struct shrink_control;

/*
 * block_dev.c
 */
#ifdef CONFIG_BLOCK
extern void __init bdev_cache_init(void);

extern int __sync_blockdev(struct block_device *bdev, int wait);

#else
static inline void bdev_cache_init(void)
{
}

static inline int __sync_blockdev(struct block_device *bdev, int wait)
{
	return 0;
}
#endif

/*
 * buffer.c
 */
extern void guard_bio_eod(int rw, struct bio *bio);
extern int __block_write_begin_int(struct page *page, loff_t pos, unsigned len,
		get_block_t *get_block, struct iomap *iomap);

/*
 * char_dev.c
 */
extern void __init chrdev_init(void);

/*
 * namei.c
 */
extern int user_path_mountpoint_at(int, const char __user *, unsigned int, struct path *);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
			   const char *, unsigned int, struct path *);
long do_rmdir(int dfd, const char __user *pathname);
long do_unlinkat(int dfd, struct filename *name);

/*
 * namespace.c
 */
extern void *copy_mount_options(const void __user *);
extern char *copy_mount_string(const void __user *);

extern struct vfsmount *lookup_mnt(const struct path *);
extern int finish_automount(struct vfsmount *, struct path *);

extern int sb_prepare_remount_readonly(struct super_block *);

extern void __init mnt_init(void);

extern int __mnt_want_write(struct vfsmount *);
extern int __mnt_want_write_file(struct file *);
extern int mnt_want_write_file_path(struct file *);
extern void __mnt_drop_write(struct vfsmount *);
extern void __mnt_drop_write_file(struct file *);
extern void mnt_drop_write_file_path(struct file *);

/*
 * fs_struct.c
 */
extern void chroot_fs_refs(const struct path *, const struct path *);

/*
 * file_table.c
 */
extern struct file *get_empty_filp(void);

/*
 * super.c
 */
extern int do_remount_sb(struct super_block *, int, void *, int);
extern bool trylock_super(struct super_block *sb);
extern struct dentry *mount_fs(struct file_system_type *,
			       int, const char *, void *);
extern struct super_block *user_get_super(dev_t);

/*
 * open.c
 */
struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};
extern struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op);
extern struct file *do_file_open_root(struct dentry *, struct vfsmount *,
		const char *, const struct open_flags *);

extern int open_check_o_direct(struct file *f);
extern int vfs_open(const struct path *, struct file *, const struct cred *);
extern struct file *filp_clone_open(struct file *);

/*
 * inode.c
 */
extern long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);
extern void inode_add_lru(struct inode *inode);
extern int dentry_needs_remove_privs(struct dentry *dentry);

extern bool __atime_needs_update(const struct path *, struct inode *, bool);
static inline bool atime_needs_update_rcu(const struct path *path,
					  struct inode *inode)
{
	return __atime_needs_update(path, inode, true);
}

/*
 * fs-writeback.c
 */
extern void inode_io_list_del(struct inode *inode);

extern long get_nr_dirty_inodes(void);
extern int invalidate_inodes(struct super_block *, bool);

/*
 * dcache.c
 */
extern struct dentry *__d_alloc(struct super_block *, const struct qstr *);
extern int d_set_mounted(struct dentry *dentry);
extern long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc);
extern struct dentry *d_alloc_cursor(struct dentry *);

/*
 * read_write.c
 */
extern int rw_verify_area(int, struct file *, const loff_t *, size_t);

/*
 * pipe.c
 */
extern const struct file_operations pipefifo_fops;

/*
 * fs_pin.c
 */
extern void group_pin_kill(struct hlist_head *p);
extern void mnt_pin_kill(struct mount *m);

/*
 * fs/nsfs.c
 */
extern const struct dentry_operations ns_dentry_operations;

/*
 * fs/ioctl.c
 */
extern int do_vfs_ioctl(struct file *file, unsigned int fd, unsigned int cmd,
		    unsigned long arg);
extern long vfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/*
 * iomap support:
 */
typedef loff_t (*iomap_actor_t)(struct inode *inode, loff_t pos, loff_t len,
		void *data, struct iomap *iomap);

loff_t iomap_apply(struct inode *inode, loff_t pos, loff_t length,
		unsigned flags, const struct iomap_ops *ops, void *data,
		iomap_actor_t actor);

/* direct-io.c: */
int sb_init_dio_done_wq(struct super_block *sb);
